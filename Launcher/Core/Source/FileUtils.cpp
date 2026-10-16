// Echelon @feature Codex 16/10/2026 core FileUtils
/*
** Implementation of the core filesystem helpers.
*/

#include "Echelon/Core/FileUtils.h"

#include "Echelon/Core/Logger.h"

#include <openssl/evp.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace echelon::core
{
namespace
{

int FileDescriptor(std::FILE *handle)
{
#if defined(_WIN32)
	return _fileno(handle);
#else
	return fileno(handle);
#endif
}

int SyncFileDescriptor(int descriptor)
{
#if defined(_WIN32)
	return _commit(descriptor);
#else
	return fsync(descriptor);
#endif
}

// fsync of the containing directory makes the rename() durable. Windows has
// no direct equivalent, so there the flush is a documented no-op.
bool SyncDirectory(const std::filesystem::path &directory)
{
#if defined(_WIN32)
	return true;
#else
	std::FILE *handle = std::fopen(directory.c_str(), "rb");
	if (!handle) return false;
	const bool synced = SyncFileDescriptor(FileDescriptor(handle)) == 0;
	std::fclose(handle);
	return synced;
#endif
}

std::uint64_t CurrentProcessIdForTemp()
{
#if defined(_WIN32)
	return static_cast<std::uint64_t>(_getpid());
#else
	return static_cast<std::uint64_t>(getpid());
#endif
}

// Process liveness check carried over from Launcher/LauncherLocks.cpp
// (OwnerProcessIsAlive) so temp garbage collection never removes a temp
// file a live writer is still holding.
bool OwnerProcessIsAlive(std::uint64_t processId)
{
	if (processId == 0) return false;
#if defined(_WIN32)
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
	if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
	DWORD exitCode = 0;
	const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
	CloseHandle(process);
	return alive;
#else
	if (kill(static_cast<pid_t>(processId), 0) == 0) return true;
	return errno == EPERM;
#endif
}

// Extracts the pid embedded in a "<name>.tmp-<pid>" temp file name.
// Returns false when the name does not match the pattern or the suffix is
// not a plain number, so unrelated files are never touched.
bool TempOwnerPid(const std::string &name, const std::string &prefix, std::uint64_t &processId)
{
	if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) return false;
	processId = 0;
	for (const char character : name.substr(prefix.size())) {
		if (character < '0' || character > '9') return false;
		processId = processId * 10 + static_cast<std::uint64_t>(character - '0');
	}
	return true;
}

void ReapStaleTemps(const std::filesystem::path &parent, const std::string &fileName)
{
	const std::string prefix = fileName + ".tmp-";
	std::error_code error;
	for (const std::filesystem::directory_entry &entry :
		std::filesystem::directory_iterator(parent, error)) {
		if (error) break;
		std::error_code typeError;
		if (!entry.is_regular_file(typeError) || typeError) continue;
		std::uint64_t ownerPid = 0;
		if (!TempOwnerPid(entry.path().filename().string(), prefix, ownerPid)) continue;
		if (OwnerProcessIsAlive(ownerPid)) continue;
		if (!std::filesystem::remove(entry.path(), error) && error) {
			ECHELON_LOG_WARN("file", "cannot reap stale temp file %s: %s",
				entry.path().string().c_str(), error.message().c_str());
		}
	}
}

} // namespace

bool IsInsideRoot(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
	// Echelon @refactor Codex 16/10/2026 lexically_normal() both sides so
	// lexical ".." escapes collapse before comparison; the monolithic canon
	// relied on callers passing already-canonical paths.
	const std::filesystem::path normalizedRoot = root.lexically_normal();
	const std::filesystem::path normalizedCandidate = candidate.lexically_normal();
	// Component-wise prefix comparison carried over from
	// Launcher/LauncherInstaller.cpp (IsInsideRoot): a plain string prefix
	// would accept "/root-evil" as a child of "/root".
	auto candidatePart = normalizedCandidate.begin();
	for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end();
		++rootPart, ++candidatePart) {
		if (candidatePart == normalizedCandidate.end() || *candidatePart != *rootPart) return false;
	}
	return true;
}

Result<std::string> ReadFile(const std::filesystem::path &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		return Result<std::string>::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot read " + path.string()});
	}
	std::ostringstream contents;
	contents << input.rdbuf();
	if (input.bad()) {
		return Result<std::string>::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot read " + path.string()});
	}
	return Result<std::string>::Ok(contents.str());
}

Status WriteFile(const std::filesystem::path &path, const std::string &bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		return Status::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot write " + path.string()});
	}
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	output.flush();
	if (!output) {
		return Status::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot write " + path.string()});
	}
	return Status::Ok();
}

Status AtomicWriteFile(const std::filesystem::path &path, const std::string &bytes)
{
	const std::string fileName = path.filename().string();
	if (fileName.empty()) {
		return Status::Err({ErrorStage::Validation, ErrorCode::InvalidInput, false,
			"AtomicWriteFile target has no file name: " + path.string()});
	}
	std::filesystem::path parent = path.parent_path();
	if (parent.empty()) parent = ".";
	std::error_code error;
	if (!std::filesystem::is_directory(parent, error)) {
		return Status::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"AtomicWriteFile directory is missing: " + parent.string()});
	}

	ReapStaleTemps(parent, fileName);

	const std::filesystem::path temp =
		parent / (fileName + ".tmp-" + std::to_string(CurrentProcessIdForTemp()));
	std::FILE *handle = std::fopen(temp.c_str(), "wb");
	if (!handle) {
		const int openError = errno;
		return Status::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot write " + temp.string() + ": " + std::strerror(openError)});
	}

	std::string failureDetail;
	if (!bytes.empty() &&
		std::fwrite(bytes.data(), 1, bytes.size(), handle) != bytes.size()) {
		failureDetail = "Cannot write " + temp.string() + ": short write";
	}
	if (failureDetail.empty() && std::fflush(handle) != 0) {
		failureDetail = "Cannot flush " + temp.string() + ": " + std::strerror(errno);
	}
	if (failureDetail.empty() && SyncFileDescriptor(FileDescriptor(handle)) != 0) {
		failureDetail = "Cannot sync " + temp.string() + ": " + std::strerror(errno);
	}
	if (std::fclose(handle) != 0 && failureDetail.empty()) {
		failureDetail = "Cannot close " + temp.string() + ": " + std::strerror(errno);
	}
	if (!failureDetail.empty()) {
		std::error_code removeError;
		std::filesystem::remove(temp, removeError);
		return Status::Err(
			{ErrorStage::IO, ErrorCode::FilesystemFailure, true, std::move(failureDetail)});
	}

	std::filesystem::rename(temp, path, error);
	if (error) {
		const std::string reason = error.message();
		std::error_code removeError;
		std::filesystem::remove(temp, removeError);
		return Status::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot rename " + temp.string() + " to " + path.string() + ": " + reason});
	}
	if (!SyncDirectory(parent)) {
		// The payload is already in place; only the durability of the rename
		// is weakened, so warn instead of reporting a failed write.
		ECHELON_LOG_WARN("file", "cannot sync directory after atomic write: %s",
			parent.string().c_str());
	}
	return Status::Ok();
}

Result<std::string> Sha256HexFile(const std::filesystem::path &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		return Result<std::string>::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot read " + path.string()});
	}
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
		if (context) EVP_MD_CTX_free(context);
		return Result<std::string>::Err(
			{ErrorStage::Hash, ErrorCode::None, false, "Cannot initialize SHA-256"});
	}
	std::vector<char> buffer(1024 * 1024);
	while (input) {
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count > 0 &&
			EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(count)) != 1) {
			EVP_MD_CTX_free(context);
			return Result<std::string>::Err(
				{ErrorStage::Hash, ErrorCode::None, false, "Cannot update SHA-256"});
		}
	}
	// Echelon @bugfix Codex 16/10/2026 a mid-stream read error used to fall
	// out of the loop silently and digest the partial file; the monolithic
	// DigestFile has the same flaw, so check badbit explicitly.
	if (input.bad()) {
		EVP_MD_CTX_free(context);
		return Result<std::string>::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true,
			"Cannot read " + path.string()});
	}
	unsigned char digest[EVP_MAX_MD_SIZE]{};
	unsigned int digestSize = 0;
	if (EVP_DigestFinal_ex(context, digest, &digestSize) != 1) {
		EVP_MD_CTX_free(context);
		return Result<std::string>::Err(
			{ErrorStage::Hash, ErrorCode::None, false, "Cannot finish SHA-256"});
	}
	EVP_MD_CTX_free(context);
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (unsigned int index = 0; index < digestSize; ++index) {
		output << std::setw(2) << static_cast<unsigned>(digest[index]);
	}
	return Result<std::string>::Ok(output.str());
}

bool EnsureDir(const std::filesystem::path &path)
{
	std::error_code error;
	std::filesystem::create_directories(path, error);
	if (error) {
		ECHELON_LOG_WARN("file", "cannot create directory %s: %s", path.string().c_str(),
			error.message().c_str());
	}
	return std::filesystem::is_directory(path);
}

} // namespace echelon::core
