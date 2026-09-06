/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherInstaller.h"

#include "LauncherLocks.h"
#include "LauncherIntegration/ArchiveLoadPolicy.h"

#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

constexpr uint64_t kMaximumExtractedBytes = 20ull * 1024ull * 1024ull * 1024ull;
constexpr uint64_t kMinimumArchiveAllowance = 1024ull * 1024ull * 1024ull;
constexpr uint64_t kMaximumFileCount = 200000;

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string Slugify(const std::string &value)
{
	std::string result;
	bool separatorPending = false;
	for (unsigned char character : value) {
		if (std::isalnum(character)) {
			if (separatorPending && !result.empty()) result.push_back('-');
			result.push_back(static_cast<char>(std::tolower(character)));
			separatorPending = false;
		} else if (!result.empty()) {
			separatorPending = true;
		}
	}
	return result.empty() ? "local-modification" : result;
}

std::string Timestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t value = std::chrono::system_clock::to_time_t(now);
	std::tm local{};
#if defined(_WIN32)
	localtime_s(&local, &value);
#else
	localtime_r(&value, &local);
#endif
	char buffer[32]{};
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
	return buffer;
}

bool IsInsideRoot(const fs::path &candidate, const fs::path &root)
{
	auto candidatePart = candidate.begin();
	for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++candidatePart) {
		if (candidatePart == candidate.end() || *candidatePart != *rootPart) return false;
	}
	return true;
}

bool IsForbiddenBinary(const fs::path &path)
{
	const std::string extension = ToLower(path.extension().string());
	return extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".dylib" ||
		extension == ".asi";
}

bool IsSafeMetadataValue(const std::string &value)
{
	return value.find_first_of("\r\n") == std::string::npos && value.find('\0') == std::string::npos;
}

bool IsSafeCompatibilitySelector(const std::string &value)
{
	if (value.empty() || value.size() > 256 || value.find_first_of("\r\n,") != std::string::npos ||
		value.find('\0') != std::string::npos) return false;
	const size_t sourceSeparator = value.find(':');
	if (sourceSeparator == std::string::npos || sourceSeparator == 0 ||
		value.find(':', sourceSeparator + 1) != std::string::npos) return false;
	const size_t versionSeparator = value.rfind('@');
	if (versionSeparator != std::string::npos && (versionSeparator <= sourceSeparator + 1 ||
		versionSeparator + 1 >= value.size() || value.find('@') != versionSeparator)) return false;
	return std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isalnum(character) || character == '.' || character == '_' || character == '-' ||
			character == ':' || character == '@' || character == '+' || character == '~';
	});
}

bool ValidateCompatibilitySelectors(const std::vector<std::string> &selectors, const std::string &selfId)
{
	if (selectors.size() > 64) return false;
	std::unordered_set<std::string> seen;
	for (const std::string &selector : selectors) {
		if (!IsSafeCompatibilitySelector(selector) || !seen.insert(ToLower(selector)).second ||
			ToLower(selector.substr(0, selector.find('@'))) == ToLower(selfId)) return false;
	}
	return true;
}

std::string JoinCompatibilitySelectors(const std::vector<std::string> &selectors)
{
	std::ostringstream output;
	for (size_t index = 0; index < selectors.size(); ++index) {
		if (index != 0) output << ',';
		output << ToLower(selectors[index]);
	}
	return output.str();
}

bool IsSha256(const std::string &value)
{
	return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isxdigit(character) != 0;
	});
}

bool IsSafeArchivePath(const fs::path &path)
{
	if (path.empty() || path.is_absolute() || path.has_root_directory() || path.has_root_name()) return false;
	const fs::path normalized = path.lexically_normal();
	if (normalized.empty() || normalized == ".") return false;
	for (const fs::path &part : normalized) {
		if (part == "..") return false;
	}
	return true;
}

uint32_t ReadBigEndian32(const unsigned char *bytes)
{
	return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
		(static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

uint32_t ReadLittleEndian32(const unsigned char *bytes)
{
	return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
		(static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool ValidateBigArchive(const fs::path &path, std::string &errorMessage)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		errorMessage = "Cannot read BIG archive: " + path.string();
		return false;
	}
	input.seekg(0, std::ios::end);
	const std::streamoff archiveSize = input.tellg();
	input.seekg(0, std::ios::beg);
	if (archiveSize < 16 || archiveSize > static_cast<std::streamoff>(UINT32_MAX)) {
		errorMessage = "BIG archive has an invalid size: " + path.filename().string();
		return false;
	}
	unsigned char header[16]{};
	input.read(reinterpret_cast<char *>(header), sizeof(header));
	if (!input || std::memcmp(header, "BIGF", 4) != 0) {
		errorMessage = "BIG archive has an invalid header: " + path.filename().string();
		return false;
	}
	const uint32_t declaredArchiveSize = ReadLittleEndian32(header + 4);
	const uint32_t fileCount = ReadBigEndian32(header + 8);
	const uint32_t directoryEnd = ReadBigEndian32(header + 12);
	if (declaredArchiveSize != static_cast<uint64_t>(archiveSize) || fileCount > 1000000 ||
		directoryEnd < 16 || directoryEnd > static_cast<uint64_t>(archiveSize)) {
		errorMessage = "BIG archive directory is invalid: " + path.filename().string();
		return false;
	}
	for (uint32_t index = 0; index < fileCount; ++index) {
		unsigned char entry[8]{};
		input.read(reinterpret_cast<char *>(entry), sizeof(entry));
		if (!input) {
			errorMessage = "BIG archive directory is truncated: " + path.filename().string();
			return false;
		}
		const uint64_t offset = ReadBigEndian32(entry);
		const uint64_t size = ReadBigEndian32(entry + 4);
		std::string name;
		for (;;) {
			const std::streamoff position = input.tellg();
			if (position < 0 || position >= directoryEnd ||
				name.size() > EchelonArchivePolicy::kMaximumEntryPathBytes) {
				errorMessage = "BIG archive contains an invalid filename: " + path.filename().string();
				return false;
			}
			char character = 0;
			input.get(character);
			if (!input) {
				errorMessage = "BIG archive directory is truncated: " + path.filename().string();
				return false;
			}
			if (character == '\0') break;
			name.push_back(character);
		}
		std::replace(name.begin(), name.end(), '\\', '/');
		const fs::path logicalPath(name);
		if (name.empty() || name.find(':') != std::string::npos || !IsSafeArchivePath(logicalPath) ||
			IsForbiddenBinary(logicalPath) || offset < directoryEnd || offset > static_cast<uint64_t>(archiveSize) ||
			size > static_cast<uint64_t>(archiveSize) - offset) {
			errorMessage = "BIG archive contains an unsafe entry: " + path.filename().string();
			return false;
		}
	}
	const std::streamoff entriesEnd = input.tellg();
	if (entriesEnd < 0 || entriesEnd > static_cast<std::streamoff>(directoryEnd)) {
		errorMessage = "BIG archive directory length does not match its header: " + path.filename().string();
		return false;
	}
	const size_t trailerSize = static_cast<size_t>(directoryEnd - entriesEnd);
	if (trailerSize != 0) {
		// Echelon @bugfix Codex 15/08/2026 Accept the two directory suffixes produced by deployed SAGE BIG tools.
		// FinalBIG archives use eight zero alignment bytes, while GenLauncher tombstone GIBs use the seven-byte Lnnn marker.
		if (trailerSize == 8) {
			unsigned char padding[8]{};
			input.read(reinterpret_cast<char *>(padding), sizeof(padding));
			if (!input || !std::all_of(padding, padding + sizeof(padding),
				[](unsigned char byte) { return byte == 0; })) {
				errorMessage = "BIG archive has an invalid directory trailer: " + path.filename().string();
				return false;
			}
			return true;
		}
		if (trailerSize != 7) {
			errorMessage = "BIG archive has an unknown directory trailer: " + path.filename().string();
			return false;
		}
		unsigned char trailer[7]{};
		input.read(reinterpret_cast<char *>(trailer), sizeof(trailer));
		const bool validVersionTrailer = input && trailer[0] == 'L' &&
			std::isdigit(trailer[1]) && std::isdigit(trailer[2]) && std::isdigit(trailer[3]) &&
			trailer[4] == 0 && trailer[5] == 0 && trailer[6] == 0;
		if (!validVersionTrailer) {
			errorMessage = "BIG archive has an invalid directory trailer: " + path.filename().string();
			return false;
		}
	}
	return true;
}

bool Cancelled(ModificationOperationProgress *progress)
{
	return progress && progress->cancelRequested.load(std::memory_order_relaxed);
}

void AddProgress(ModificationOperationProgress *progress, uint64_t bytes)
{
	if (progress) progress->completedBytes.fetch_add(bytes, std::memory_order_relaxed);
}

std::string DigestFile(const fs::path &path, std::string &errorMessage,
	ModificationOperationProgress *progress = nullptr)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		errorMessage = "Cannot read " + path.string();
		return {};
	}
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
		if (context) EVP_MD_CTX_free(context);
		errorMessage = "Cannot initialize SHA-256";
		return {};
	}
	std::vector<char> buffer(1024 * 1024);
	while (input) {
		if (Cancelled(progress)) {
			EVP_MD_CTX_free(context);
			errorMessage = "Operation cancelled";
			return {};
		}
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count > 0) {
			if (EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(count)) != 1) {
				EVP_MD_CTX_free(context);
				errorMessage = "Cannot update SHA-256";
				return {};
			}
			AddProgress(progress, static_cast<uint64_t>(count));
		}
	}
	unsigned char digest[EVP_MAX_MD_SIZE]{};
	unsigned int digestSize = 0;
	if (EVP_DigestFinal_ex(context, digest, &digestSize) != 1) {
		EVP_MD_CTX_free(context);
		errorMessage = "Cannot finish SHA-256";
		return {};
	}
	EVP_MD_CTX_free(context);
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (unsigned int index = 0; index < digestSize; ++index) output << std::setw(2) << static_cast<unsigned>(digest[index]);
	return output.str();
}

std::string DigestBytes(const std::string &value, std::string &errorMessage)
{
	unsigned char digest[EVP_MAX_MD_SIZE]{};
	unsigned int digestSize = 0;
	if (EVP_Digest(value.data(), value.size(), digest, &digestSize, EVP_sha256(), nullptr) != 1) {
		errorMessage = "Cannot calculate SHA-256";
		return {};
	}
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (unsigned int index = 0; index < digestSize; ++index) output << std::setw(2) << static_cast<unsigned>(digest[index]);
	return output.str();
}

bool CopyRegularFile(const fs::path &source, const fs::path &target, std::string &errorMessage,
	ModificationOperationProgress *progress)
{
	if (IsForbiddenBinary(source)) {
		errorMessage = "Incompatible executable component: " + source.filename().string();
		return false;
	}
	std::error_code error;
	fs::create_directories(target.parent_path(), error);
	if (error) {
		errorMessage = error.message();
		return false;
	}
	std::ifstream input(source, std::ios::binary);
	std::ofstream output(target, std::ios::binary | std::ios::trunc);
	if (!input || !output) {
		errorMessage = "Cannot copy " + source.string();
		return false;
	}
	std::vector<char> buffer(1024 * 1024);
	while (input) {
		if (Cancelled(progress)) {
			errorMessage = "Operation cancelled";
			return false;
		}
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count > 0) {
			output.write(buffer.data(), count);
			if (!output) {
				errorMessage = "Cannot write " + target.string();
				return false;
			}
			AddProgress(progress, static_cast<uint64_t>(count));
		}
	}
	return true;
}

bool CopyValidatedTree(const fs::path &source, const fs::path &target, std::string &errorMessage,
	ModificationOperationProgress *progress)
{
	std::error_code error;
	uint64_t fileCount = 0;
	for (fs::recursive_directory_iterator iterator(source, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (Cancelled(progress)) {
			errorMessage = "Operation cancelled";
			return false;
		}
		if (iterator->is_symlink(error) || (!iterator->is_directory(error) && !iterator->is_regular_file(error))) {
			errorMessage = "Unsupported symlink or device entry: " + iterator->path().string();
			return false;
		}
		const fs::path relative = fs::relative(iterator->path(), source, error);
		if (error || !IsSafeArchivePath(relative)) {
			errorMessage = "Unsafe imported path";
			return false;
		}
		if (iterator->is_directory(error)) {
			fs::create_directories(target / relative, error);
			if (error) {
				errorMessage = error.message();
				return false;
			}
		} else {
			if (++fileCount > kMaximumFileCount) {
				errorMessage = "Imported directory contains too many files";
				return false;
			}
			if (!CopyRegularFile(iterator->path(), target / relative, errorMessage, progress)) return false;
		}
	}
	if (error) {
		errorMessage = "Cannot enumerate imported directory: " + error.message();
		return false;
	}
	return true;
}

bool ExtractValidatedArchive(const fs::path &source, const fs::path &target, std::string &errorMessage,
	ModificationOperationProgress *progress)
{
	std::error_code fileError;
	const uint64_t compressedBytes = fs::file_size(source, fileError);
	if (fileError) {
		errorMessage = fileError.message();
		return false;
	}
	const uint64_t ratioAllowance = compressedBytes > kMaximumExtractedBytes / 200 ? kMaximumExtractedBytes : compressedBytes * 200;
	const uint64_t byteLimit = std::min(kMaximumExtractedBytes, std::max(kMinimumArchiveAllowance, ratioAllowance));
	archive *reader = archive_read_new();
	archive_read_support_filter_all(reader);
	archive_read_support_format_all(reader);
	if (archive_read_open_filename(reader, source.string().c_str(), 1024 * 1024) != ARCHIVE_OK) {
		errorMessage = archive_error_string(reader) ? archive_error_string(reader) : "Cannot open archive";
		archive_read_free(reader);
		return false;
	}
	uint64_t extractedBytes = 0;
	uint64_t fileCount = 0;
	archive_entry *entry = nullptr;
	while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
		if (Cancelled(progress)) {
			errorMessage = "Operation cancelled";
			archive_read_free(reader);
			return false;
		}
		const char *entryName = archive_entry_pathname_utf8(entry);
		if (!entryName) entryName = archive_entry_pathname(entry);
		const fs::path relative = entryName ? fs::path(entryName).lexically_normal() : fs::path{};
		if (!IsSafeArchivePath(relative) || IsForbiddenBinary(relative)) {
			errorMessage = "Unsafe or incompatible archive entry: " + relative.string();
			archive_read_free(reader);
			return false;
		}
		const mode_t fileType = archive_entry_filetype(entry);
		const fs::path outputPath = target / relative;
		if (fileType == AE_IFDIR) {
			fs::create_directories(outputPath, fileError);
			if (fileError) {
				errorMessage = fileError.message();
				archive_read_free(reader);
				return false;
			}
			continue;
		}
		if (fileType != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {
			errorMessage = "Archive contains a symlink, hardlink, or device entry";
			archive_read_free(reader);
			return false;
		}
		if (++fileCount > kMaximumFileCount) {
			errorMessage = "Archive contains too many files";
			archive_read_free(reader);
			return false;
		}
		fs::create_directories(outputPath.parent_path(), fileError);
		std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
		if (fileError || !output) {
			errorMessage = "Cannot create extracted file";
			archive_read_free(reader);
			return false;
		}
		const void *buffer = nullptr;
		size_t size = 0;
		la_int64_t offset = 0;
		int result = ARCHIVE_OK;
		while ((result = archive_read_data_block(reader, &buffer, &size, &offset)) == ARCHIVE_OK) {
			if (Cancelled(progress) || size > byteLimit - std::min(extractedBytes, byteLimit)) {
				errorMessage = Cancelled(progress) ? "Operation cancelled" : "Archive extraction limit exceeded";
				archive_read_free(reader);
				return false;
			}
			output.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
			if (!output) {
				errorMessage = "Cannot write extracted file";
				archive_read_free(reader);
				return false;
			}
			extractedBytes += size;
			AddProgress(progress, size);
		}
		if (result != ARCHIVE_EOF) {
			errorMessage = archive_error_string(reader) ? archive_error_string(reader) : "Cannot extract archive entry";
			archive_read_free(reader);
			return false;
		}
	}
	if (archive_errno(reader) != 0) {
		errorMessage = archive_error_string(reader) ? archive_error_string(reader) : "Cannot finish archive extraction";
		archive_read_free(reader);
		return false;
	}
	archive_read_free(reader);
	return fileCount > 0;
}

bool BuildContentIndex(const fs::path &root, std::string &fingerprint, std::string &indexText,
	std::string &errorMessage, ModificationOperationProgress *progress)
{
	std::vector<fs::path> files;
	std::error_code error;
	for (fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (iterator->is_symlink(error) || (!iterator->is_directory(error) && !iterator->is_regular_file(error))) {
			errorMessage = "Installed content contains an unsupported entry";
			return false;
		}
		if (iterator->is_regular_file(error)) files.push_back(iterator->path());
	}
	if (error || files.empty()) {
		errorMessage = error ? error.message() : "Installed content is empty";
		return false;
	}
	std::sort(files.begin(), files.end(), [&](const fs::path &left, const fs::path &right) {
		return fs::relative(left, root).generic_string() < fs::relative(right, root).generic_string();
	});
	std::ostringstream index;
	for (const fs::path &file : files) {
		if (IsForbiddenBinary(file)) {
			errorMessage = "Installed content contains an executable component";
			return false;
		}
		const std::string extension = ToLower(file.extension().string());
		// Echelon @feature Codex 15/08/2026 Treat GenLauncher .gib payloads as immutable BIG archives without retail-file renaming.
		if ((extension == ".big" || extension == ".gib") && !ValidateBigArchive(file, errorMessage)) return false;
		const std::string digest = DigestFile(file, errorMessage, progress);
		if (digest.empty()) return false;
		index << digest << "  " << fs::relative(file, root).generic_string() << '\n';
	}
	indexText = index.str();
	// Echelon @bugfix Codex 14/08/2026 Verification must work for a genuinely read-only installed tree.
	// Hash the deterministic index in memory instead of creating a shared scratch file beside the content root.
	fingerprint = DigestBytes(indexText, errorMessage);
	return !fingerprint.empty();
}

fs::path UniqueStagingPath(const fs::path &modsRoot)
{
	const std::string prefix = "import-" + Timestamp();
	std::error_code error;
	for (unsigned int suffix = 0; suffix < 10000; ++suffix) {
		const fs::path candidate = modsRoot / ".staging" /
			(suffix == 0 ? prefix : prefix + "-" + std::to_string(suffix));
		if (!fs::exists(candidate, error)) return candidate;
	}
	return {};
}

void RecordFailure(const fs::path &stagingRoot, const std::string &message)
{
	if (stagingRoot.empty()) return;
	std::ofstream output(stagingRoot / "operation.error", std::ios::trunc);
	if (output) output << message << '\n';
}

std::unordered_map<std::string, std::string> ReadIniValues(const fs::path &path)
{
	std::unordered_map<std::string, std::string> values;
	std::ifstream input(path);
	std::string line;
	while (std::getline(input, line)) {
		const size_t separator = line.find('=');
		if (separator == std::string::npos) continue;
		std::string key = ToLower(line.substr(0, separator));
		key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char value) { return std::isspace(value); }), key.end());
		std::string value = line.substr(separator + 1);
		while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
		size_t first = value.find_first_not_of(" \t");
		if (first != std::string::npos) value.erase(0, first);
		else value.clear();
		if (IsSafeMetadataValue(key) && IsSafeMetadataValue(value)) values[key] = value;
	}
	return values;
}

size_t CountTransferRoots(const fs::path &root, const char *marker)
{
	std::error_code error;
	if (!fs::is_directory(root, error)) return 0;
	size_t count = 0;
	for (fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (((iterator->is_regular_file(error) && std::strcmp(marker, "content") != 0) ||
			(iterator->is_directory(error) && std::strcmp(marker, "content") == 0)) &&
			iterator->path().filename() == marker) ++count;
	}
	return count;
}

} // namespace

ModificationOperationResult ImportLocalModification(const LocalImportRequest &request,
	ModificationOperationProgress *progress)
{
	ModificationOperationResult result;
	std::error_code error;
	const bool inputIsDirectory = fs::is_directory(request.inputPath, error);
	const bool inputIsFile = fs::is_regular_file(request.inputPath, error);
	if (error || (!inputIsDirectory && !inputIsFile)) {
		result.message = "Imported path does not exist";
		return result;
	}
	if (request.engine != "generals" && request.engine != "zerohour") {
		result.message = "Invalid target engine";
		return result;
	}
	const std::string name = request.displayName.empty() ? request.inputPath.stem().string() : request.displayName;
	const std::string slug = Slugify(name);
	const std::string sourceId = request.sourceId.empty() ? "local" : ToLower(request.sourceId);
	const std::string id = request.modificationId.empty() ? sourceId + ":" + slug : ToLower(request.modificationId);
	const std::string version = request.version.empty() ? "local-" + Timestamp() : request.version;
	if (!IsSafeMetadataValue(name) || !IsSafeMetadataValue(sourceId) || !IsSafeMetadataValue(id) ||
		!IsSafeMetadataValue(version) || !IsSafeMetadataValue(request.parentId) || !IsSafeMetadataValue(request.source) ||
		id.find(':') == std::string::npos || Slugify(sourceId) != sourceId ||
		!ValidateCompatibilitySelectors(request.requirements, id) ||
		!ValidateCompatibilitySelectors(request.conflicts, id)) {
		result.message = "Invalid modification metadata";
		return result;
	}
	if (!request.expectedSha256.empty() && !IsSha256(request.expectedSha256)) {
		result.message = "Invalid expected SHA-256";
		return result;
	}
	if (inputIsFile && request.expectedSize != 0 && fs::file_size(request.inputPath, error) != request.expectedSize) {
		result.message = "Downloaded package size does not match its catalog";
		return result;
	}
	std::string lockError;
	ScopedDirectoryLock contentLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(request.modsRoot, "content", "operation"), lockError);
	if (!contentLock.acquired()) {
		result.message = lockError.empty() ? "Another content operation is already active" : lockError;
		return result;
	}
	std::string inputDigest;
	if (inputIsFile && !request.expectedSha256.empty()) {
		inputDigest = DigestFile(request.inputPath, result.message, nullptr);
		if (inputDigest.empty() || inputDigest != ToLower(request.expectedSha256)) {
			if (result.message.empty()) result.message = "Downloaded package SHA-256 does not match its catalog";
			return result;
		}
	}
	const fs::path stagingRoot = UniqueStagingPath(request.modsRoot);
	if (stagingRoot.empty()) {
		result.message = "Cannot allocate a staging operation";
		return result;
	}
	fs::create_directories(stagingRoot / "content", error);
	if (error) {
		result.message = "Cannot create staging directory: " + error.message();
		return result;
	}
	{
		std::ofstream journal(stagingRoot / "operation.ini", std::ios::trunc);
		journal << "[Operation]\nSchemaVersion=1\nState=importing\nInput=" << request.inputPath.string()
			<< "\nEngine=" << request.engine << "\nType=" << ModificationTypeName(request.type) << '\n';
	}
	if (progress) {
		progress->completedBytes.store(0, std::memory_order_relaxed);
		progress->totalBytes.store(inputIsFile ? fs::file_size(request.inputPath, error) : 0, std::memory_order_relaxed);
	}
	std::string operationError;
	bool copied = false;
	if (inputIsDirectory) {
		copied = CopyValidatedTree(request.inputPath, stagingRoot / "content", operationError, progress);
	} else if (const std::string extension = ToLower(request.inputPath.extension().string());
		extension == ".big" || extension == ".gib") {
		// Echelon @bugfix Codex 15/08/2026 Import standalone GenLauncher .gib payloads through the same validated BIG path.
		copied = CopyRegularFile(request.inputPath, stagingRoot / "content" / request.inputPath.filename(), operationError, progress);
	} else {
		copied = ExtractValidatedArchive(request.inputPath, stagingRoot / "content", operationError, progress);
	}
	if (!copied) {
		result.cancelled = Cancelled(progress);
		result.message = operationError.empty() ? "Cannot import modification content" : operationError;
		RecordFailure(stagingRoot, result.message);
		return result;
	}
	std::string fingerprint;
	std::string indexText;
	if (!BuildContentIndex(stagingRoot / "content", fingerprint, indexText, operationError, progress)) {
		result.cancelled = Cancelled(progress);
		result.message = operationError;
		RecordFailure(stagingRoot, result.message);
		return result;
	}
	if (inputIsFile && inputDigest.empty()) inputDigest = DigestFile(request.inputPath, operationError, nullptr);
	std::string coverImage;
	if (!request.coverImagePath.empty() && fs::is_regular_file(request.coverImagePath, error)) {
		coverImage = "cover.image";
		if (!CopyRegularFile(request.coverImagePath, stagingRoot / coverImage, operationError, nullptr)) {
			result.message = "Cannot preserve modification cover: " + operationError;
			RecordFailure(stagingRoot, result.message);
			return result;
		}
	}
	{
		std::ofstream index(stagingRoot / "files.sha256", std::ios::binary | std::ios::trunc);
		index << indexText;
		std::ofstream manifest(stagingRoot / "manifest.ini", std::ios::trunc);
		manifest << "[Modification]\nSchemaVersion=2\nId=" << id << "\nSourceId=" << sourceId << "\nName=" << name
			<< "\nVersion=" << version << "\nEngine=" << request.engine << "\nType=" << ModificationTypeName(request.type)
			<< "\nParentId=" << request.parentId << "\nRequirements=" << JoinCompatibilitySelectors(request.requirements)
			<< "\nConflicts=" << JoinCompatibilitySelectors(request.conflicts)
			<< "\nSource=" << request.source << "\nRootPath=content\nCoverImage="
			<< coverImage << "\nSHA256="
			<< inputDigest << "\nContentFingerprint=" << fingerprint << '\n';
		if (!index || !manifest) {
			result.message = "Cannot write installation metadata";
			RecordFailure(stagingRoot, result.message);
			return result;
		}
	}
	const fs::path target = request.modsRoot / "Installed" / request.engine / ModificationTypeName(request.type) /
		Slugify(id) / Slugify(version);
	if (fs::exists(target, error)) {
		result.message = "This modification version is already installed";
		RecordFailure(stagingRoot, result.message);
		return result;
	}
	fs::create_directories(target.parent_path(), error);
	if (!error) fs::rename(stagingRoot, target, error);
	if (error) {
		result.message = "Cannot publish installation atomically: " + error.message();
		RecordFailure(stagingRoot, result.message);
		return result;
	}
	result.success = true;
	result.message = "Modification installed";
	result.selectionKey = id + "@" + ToLower(version);
	result.installedRoot = target;
	return result;
}

ModificationOperationResult MoveInstalledModificationToTrash(const fs::path &modsRoot,
	const InstalledModification &modification)
{
	ModificationOperationResult result;
	std::string lockError;
	ScopedDirectoryLock contentLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "content", "operation"), lockError);
	if (!contentLock.acquired()) {
		result.message = lockError.empty() ? "Another content operation is already active" : lockError;
		return result;
	}
	std::error_code error;
	const fs::path installedRoot = fs::weakly_canonical(modsRoot / "Installed", error);
	const fs::path versionRoot = fs::weakly_canonical(modification.manifestPath.parent_path(), error);
	if (error || !IsInsideRoot(versionRoot, installedRoot)) {
		result.message = "Refusing to remove content outside Mods/Installed";
		return result;
	}
	const fs::path trashRoot = modsRoot / ".trash" /
		(Timestamp() + "-" + Slugify(modification.id) + "-" + Slugify(modification.version));
	fs::create_directories(trashRoot.parent_path(), error);
	if (!error) fs::rename(versionRoot, trashRoot, error);
	if (error) {
		result.message = "Cannot move modification to recoverable trash: " + error.message();
		return result;
	}
	result.success = true;
	result.message = "Modification moved to recoverable trash";
	result.installedRoot = trashRoot;
	return result;
}

ModificationRecoverySummary RecoverInterruptedModificationOperations(const fs::path &modsRoot)
{
	ModificationRecoverySummary summary;
	std::string lockError;
	ScopedDirectoryLock recoveryLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "content", "operation"), lockError);
	if (!recoveryLock.acquired()) {
		summary.warnings.push_back("Content recovery skipped because an import is active");
		return summary;
	}
	const fs::path stagingRoot = modsRoot / ".staging";
	const fs::path trashRoot = modsRoot / ".trash";
	std::error_code error;
	fs::create_directories(stagingRoot, error);
	if (!error) fs::create_directories(trashRoot, error);
	if (error) {
		summary.warnings.push_back("Cannot prepare modification recovery directories: " + error.message());
		return summary;
	}
	std::vector<fs::path> interruptedImports;
	for (fs::directory_iterator iterator(stagingRoot, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (!iterator->is_directory(error)) continue;
		const std::string name = iterator->path().filename().string();
		if (name.rfind("import-", 0) == 0) interruptedImports.push_back(iterator->path());
	}
	if (error) summary.warnings.push_back("Cannot scan interrupted modification imports: " + error.message());
	error.clear();
	for (const fs::path &interruptedImport : interruptedImports) {
		const std::string name = interruptedImport.filename().string();
		fs::path preserved = trashRoot / ("interrupted-" + name);
		for (unsigned suffix = 1; fs::exists(preserved, error) && suffix < 10000; ++suffix) {
			preserved = trashRoot / ("interrupted-" + name + "-" + std::to_string(suffix));
		}
		error.clear();
		fs::rename(interruptedImport, preserved, error);
		if (error) summary.warnings.push_back("Cannot preserve interrupted import " + name + ": " + error.message());
		else ++summary.preservedInterruptedImports;
	}
	if (error) summary.warnings.push_back("Cannot finish modification recovery scan: " + error.message());
	summary.resumableDownloads = CountTransferRoots(stagingRoot / "downloads", "download.ini");
	summary.resumableS3Transfers = CountTransferRoots(stagingRoot / "s3", "content");
	error.clear();
	for (fs::directory_iterator iterator(trashRoot, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (iterator->is_directory(error) && fs::is_regular_file(iterator->path() / "manifest.ini", error)) {
			++summary.recoverableTrashEntries;
		}
	}
	return summary;
}

std::vector<RecoverableModification> ListRecoverableModifications(const fs::path &modsRoot)
{
	std::vector<RecoverableModification> result;
	const fs::path trashRoot = modsRoot / ".trash";
	std::error_code error;
	for (fs::directory_iterator iterator(trashRoot, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (!iterator->is_directory(error)) continue;
		if (!fs::is_regular_file(iterator->path() / "manifest.ini", error)) {
			error.clear();
			continue;
		}
		const auto values = ReadIniValues(iterator->path() / "manifest.ini");
		auto name = values.find("name");
		auto version = values.find("version");
		RecoverableModification recoverable;
		recoverable.path = iterator->path();
		recoverable.label = name == values.end() ? iterator->path().filename().string() : name->second;
		if (version != values.end()) recoverable.label += " " + version->second;
		result.push_back(std::move(recoverable));
	}
	std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
		return left.path.filename().string() > right.path.filename().string();
	});
	return result;
}

ModificationOperationResult RestoreModificationFromTrash(const fs::path &modsRoot, const fs::path &trashPath)
{
	ModificationOperationResult result;
	std::string lockError;
	ScopedDirectoryLock contentLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "content", "operation"), lockError);
	if (!contentLock.acquired()) {
		result.message = lockError.empty() ? "Another content operation is already active" : lockError;
		return result;
	}
	std::error_code error;
	const fs::path canonicalTrashRoot = fs::weakly_canonical(modsRoot / ".trash", error);
	const fs::path canonicalSource = fs::weakly_canonical(trashPath, error);
	if (error || canonicalSource.parent_path() != canonicalTrashRoot || !fs::is_directory(canonicalSource, error)) {
		result.message = "Refusing to restore content outside Mods/.trash";
		return result;
	}
	const auto values = ReadIniValues(canonicalSource / "manifest.ini");
	auto value = [&](const char *key) -> std::string {
		const auto found = values.find(key);
		return found == values.end() ? std::string{} : found->second;
	};
	const std::string engine = ToLower(value("engine"));
	const std::string type = ToLower(value("type"));
	const std::string id = ToLower(value("id"));
	const std::string version = value("version");
	if ((engine != "generals" && engine != "zerohour") ||
		(type != "mod" && type != "patch" && type != "addon") || id.find(':') == std::string::npos ||
		!IsSafeMetadataValue(id) || !IsSafeMetadataValue(version) || version.empty()) {
		result.message = "Recoverable modification has invalid metadata";
		return result;
	}
	const fs::path target = modsRoot / "Installed" / engine / type / Slugify(id) / Slugify(version);
	if (fs::exists(target, error)) {
		result.message = "This modification version is already installed";
		return result;
	}
	fs::create_directories(target.parent_path(), error);
	if (!error) fs::rename(canonicalSource, target, error);
	if (error) {
		result.message = "Cannot restore modification atomically: " + error.message();
		return result;
	}
	const ModificationCatalog catalog = LoadModificationCatalog(modsRoot);
	const std::string selection = id + "@" + ToLower(version);
	if (!FindModification(catalog, engine, selection)) {
		std::error_code rollbackError;
		fs::rename(target, canonicalSource, rollbackError);
		result.message = "Restored modification failed manifest validation";
		return result;
	}
	result.success = true;
	result.message = "Modification restored";
	result.selectionKey = selection;
	result.installedRoot = target;
	return result;
}

ModificationOperationResult ReplaceModificationCover(const fs::path &modsRoot,
	const InstalledModification &modification, const fs::path &imagePath)
{
	ModificationOperationResult result;
	std::string lockError;
	ScopedDirectoryLock contentLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "content", "operation"), lockError);
	if (!contentLock.acquired()) {
		result.message = lockError.empty() ? "Another content operation is already active" : lockError;
		return result;
	}
	std::error_code error;
	const fs::path canonicalInstalledRoot = fs::weakly_canonical(modsRoot / "Installed", error);
	const fs::path versionRoot = fs::weakly_canonical(modification.manifestPath.parent_path(), error);
	const fs::path canonicalImage = fs::weakly_canonical(imagePath, error);
	if (error || !IsInsideRoot(versionRoot, canonicalInstalledRoot) || !fs::is_regular_file(canonicalImage, error)) {
		result.message = "Cannot replace a cover outside an installed modification";
		return result;
	}
	const std::string extension = ToLower(canonicalImage.extension().string());
	if ((extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".webp" && extension != ".bmp") ||
		fs::file_size(canonicalImage, error) > 32ull * 1024ull * 1024ull) {
		result.message = "Cover image must be PNG, JPEG, WebP, or BMP and no larger than 32 MiB";
		return result;
	}
	std::ifstream manifestInput(modification.manifestPath, std::ios::binary);
	if (!manifestInput) {
		result.message = "Cannot read installed modification manifest";
		return result;
	}
	std::string manifest((std::istreambuf_iterator<char>(manifestInput)), std::istreambuf_iterator<char>());
	if (manifestInput.bad() || manifest.size() > 64 * 1024) {
		result.message = "Cannot read installed modification manifest";
		return result;
	}
	const std::string coverName = "cover.custom-" + Timestamp() + extension;
	const fs::path coverTemporary = versionRoot / (coverName + ".tmp");
	const fs::path coverTarget = versionRoot / coverName;
	std::string copyError;
	if (!CopyRegularFile(canonicalImage, coverTemporary, copyError, nullptr)) {
		result.message = "Cannot copy modification cover: " + copyError;
		return result;
	}
	fs::rename(coverTemporary, coverTarget, error);
	if (error) {
		fs::remove(coverTemporary, error);
		result.message = "Cannot publish modification cover: " + error.message();
		return result;
	}
	const size_t coverLine = manifest.find("CoverImage=");
	if (coverLine == std::string::npos) {
		fs::remove(coverTarget, error);
		result.message = "Installed modification manifest has no cover field";
		return result;
	}
	const size_t coverLineEnd = manifest.find('\n', coverLine);
	manifest.replace(coverLine, (coverLineEnd == std::string::npos ? manifest.size() : coverLineEnd) - coverLine,
		"CoverImage=" + coverName);
	const fs::path manifestTemporary = modification.manifestPath.string() + ".cover.tmp";
	{
		std::ofstream output(manifestTemporary, std::ios::binary | std::ios::trunc);
		output << manifest;
		output.flush();
		if (!output) {
			fs::remove(manifestTemporary, error);
			fs::remove(coverTarget, error);
			result.message = "Cannot update modification manifest cover";
			return result;
		}
	}
	fs::rename(manifestTemporary, modification.manifestPath, error);
	if (error) {
		fs::remove(manifestTemporary, error);
		fs::remove(coverTarget, error);
		result.message = "Cannot publish modification manifest cover: " + error.message();
		return result;
	}
	if (!modification.coverImagePath.empty() && modification.coverImagePath != coverTarget &&
		modification.coverImagePath.filename().string().rfind("cover.custom", 0) == 0) {
		fs::remove(modification.coverImagePath, error);
	}
	result.success = true;
	result.message = "Modification cover updated";
	result.selectionKey = ModificationSelectionKey(modification);
	result.installedRoot = versionRoot;
	return result;
}

bool VerifyInstalledModification(const InstalledModification &modification,
	std::string &errorMessage, ModificationOperationProgress *progress)
{
	std::string fingerprint;
	std::string indexText;
	if (!BuildContentIndex(modification.launchPath, fingerprint, indexText, errorMessage, progress)) return false;
	if (!modification.contentFingerprint.empty() && fingerprint != ToLower(modification.contentFingerprint)) {
		errorMessage = "Installed content fingerprint does not match its manifest";
		return false;
	}
	errorMessage.clear();
	return true;
}

} // namespace EchelonLauncher
