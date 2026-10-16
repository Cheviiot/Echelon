// Echelon @feature Codex 16/10/2026 core FsLock
/*
** Implementation of the advisory directory lock.
*/

#include "Echelon/Core/FsLock.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
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

namespace fs = std::filesystem;

constexpr auto kStaleLockAge = std::chrono::hours(24);
std::atomic<std::uint64_t> s_lockSequence{0};

std::uint64_t CurrentProcessId()
{
#if defined(_WIN32)
	return static_cast<std::uint64_t>(_getpid());
#else
	return static_cast<std::uint64_t>(getpid());
#endif
}

// Carried over from Launcher/LauncherLocks.cpp (OwnerProcessIsAlive).
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

// Echelon @bugfix Codex 16/10/2026 Never report a lock as owned without a
// durable owner record (carried over from Launcher/LauncherLocks.cpp).
std::string MakeOwnerToken()
{
	const std::uint64_t sequence = ++s_lockSequence;
	const std::uint64_t timestamp = static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	return std::to_string(CurrentProcessId()) + "-" + std::to_string(timestamp) + "-" +
		std::to_string(sequence);
}

std::string OwnerRecord(const std::string &ownerToken)
{
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	return "pid=" + std::to_string(CurrentProcessId()) + "\ncreated=" + std::to_string(now) +
		"\ntoken=" + ownerToken + "\n";
}

#if defined(_WIN32)
using NativeHandle = HANDLE;
const NativeHandle kInvalidNativeHandle = INVALID_HANDLE_VALUE;
#else
using NativeHandle = int;
const NativeHandle kInvalidNativeHandle = -1;
#endif

// Creates the lock file exclusively. Returns 1 when created, 0 when the
// file already exists, -1 on hard errors (message filled).
int CreateLockExclusive(const fs::path &path, NativeHandle &handle, std::string &errorMessage)
{
#if defined(_WIN32)
	handle = CreateFileA(path.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle != INVALID_HANDLE_VALUE) return 1;
	const DWORD lastError = GetLastError();
	if (lastError == ERROR_FILE_EXISTS || lastError == ERROR_ALREADY_EXISTS) return 0;
	errorMessage = "win32 error " + std::to_string(static_cast<unsigned long>(lastError));
	return -1;
#else
	handle = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (handle >= 0) return 1;
	if (errno == EEXIST) return 0;
	errorMessage = std::strerror(errno);
	return -1;
#endif
}

bool WriteAllToHandle(NativeHandle handle, const std::string &data)
{
	size_t total = 0;
	while (total < data.size()) {
#if defined(_WIN32)
		DWORD chunk = 0;
		if (!WriteFile(handle, data.data() + total,
				static_cast<DWORD>(data.size() - total), &chunk, nullptr) ||
			chunk == 0) {
			return false;
		}
		total += chunk;
#else
		const ssize_t chunk = write(handle, data.data() + total, data.size() - total);
		if (chunk <= 0) return false;
		total += static_cast<size_t>(chunk);
#endif
	}
	return true;
}

bool SyncHandle(NativeHandle handle)
{
#if defined(_WIN32)
	return FlushFileBuffers(handle) != 0;
#else
	return fsync(handle) == 0;
#endif
}

void CloseNativeHandle(NativeHandle handle)
{
#if defined(_WIN32)
	CloseHandle(handle);
#else
	close(handle);
#endif
}

enum class LockCreateResult
{
	Published,
	Exists,
	Failed
};

// Atomically creates the lock file and publishes the owner record.
LockCreateResult PublishOwner(const fs::path &lockFile, std::string &ownerToken,
	std::string &failureDetail)
{
	NativeHandle handle = kInvalidNativeHandle;
	std::string createError;
	const int created = CreateLockExclusive(lockFile, handle, createError);
	if (created != 1) {
		failureDetail = createError;
		return created == 0 ? LockCreateResult::Exists : LockCreateResult::Failed;
	}
	const std::string token = MakeOwnerToken();
	bool ok = WriteAllToHandle(handle, OwnerRecord(token));
	// Echelon @bugfix Codex 16/10/2026 fsync lock file for durability
	if (ok) ok = SyncHandle(handle);
	CloseNativeHandle(handle);
	if (!ok) {
		std::error_code removeError;
		fs::remove(lockFile, removeError);
		failureDetail = "cannot write lock owner record";
		return LockCreateResult::Failed;
	}
	ownerToken = token;
	return LockCreateResult::Published;
}

bool OwnerTokenMatches(const fs::path &lockFile, const std::string &ownerToken)
{
	std::ifstream owner(lockFile);
	std::string key;
	std::string token;
	while (owner >> key) {
		if (key.rfind("token=", 0) == 0) token = key.substr(6);
	}
	return !owner.bad() && !ownerToken.empty() && token == ownerToken;
}

} // namespace

bool FsLock::Acquire(const fs::path &directory, OperationError &error)
{
	error = OperationError{};
	if (directory.empty()) {
		error.stage = ErrorStage::Validation;
		error.code = ErrorCode::InvalidInput;
		error.detail = "Lock path is empty";
		return false;
	}
	if (m_held) {
		error.stage = ErrorStage::Validation;
		error.code = ErrorCode::InvalidInput;
		error.detail = "Lock is already held: " + m_lockFile.string();
		return false;
	}

	std::error_code fsError;
	fs::create_directories(directory, fsError);
	if (fsError) {
		error.stage = ErrorStage::IO;
		error.code = ErrorCode::FilesystemFailure;
		error.retryable = true;
		error.detail = "Cannot create lock directory: " + fsError.message();
		return false;
	}
	const fs::path lockFile = LockFileFor(directory);

	std::string ownerToken;
	std::string failureDetail;
	LockCreateResult outcome = PublishOwner(lockFile, ownerToken, failureDetail);
	if (outcome == LockCreateResult::Exists) {
		// Reap-and-retry carried over from Launcher/LauncherLocks.cpp: a
		// dead owner forfeits the lock.
		if (IsStaleLock(directory)) {
			std::error_code removeError;
			fs::remove(lockFile, removeError);
			if (!removeError) {
				outcome = PublishOwner(lockFile, ownerToken, failureDetail);
			}
		}
	}

	if (outcome == LockCreateResult::Published) {
		m_lockFile = lockFile;
		m_ownerToken = std::move(ownerToken);
		m_held = true;
		return true;
	}
	if (outcome == LockCreateResult::Exists) {
		error.stage = ErrorStage::Discovery;
		error.code = ErrorCode::LockBusy;
		error.detail = "Another Echelon operation already owns lock: " + lockFile.string();
		return false;
	}
	error.stage = ErrorStage::IO;
	error.code = ErrorCode::FilesystemFailure;
	error.retryable = true;
	error.detail = "Cannot write lock owner: " + lockFile.string() +
		(failureDetail.empty() ? "" : ": " + failureDetail);
	return false;
}

void FsLock::Release()
{
	if (!m_held) return;
	std::error_code error;
	// Echelon @bugfix Codex 16/10/2026 Do not remove a lock reclaimed by
	// another process (carried over from Launcher/LauncherLocks.cpp).
	if (OwnerTokenMatches(m_lockFile, m_ownerToken)) fs::remove(m_lockFile, error);
	m_held = false;
	m_ownerToken.clear();
}

bool FsLock::IsStaleLock(const fs::path &directory)
{
	const fs::path lockFile = LockFileFor(directory);
	std::error_code error;
	std::ifstream owner(lockFile);
	std::string key;
	std::uint64_t processId = 0;
	while (owner >> key) {
		if (key.rfind("pid=", 0) == 0) processId = std::strtoull(key.c_str() + 4, nullptr, 10);
	}
	if (processId != 0 && !OwnerProcessIsAlive(processId)) return true;
	const fs::file_time_type modified = fs::last_write_time(lockFile, error);
	if (error) return false;
	const fs::file_time_type now = fs::file_time_type::clock::now();
	return now > modified && now - modified > kStaleLockAge;
}

fs::path FsLock::LockFileFor(const fs::path &directory)
{
	return directory / ".echelon-lock";
}

} // namespace echelon::core
