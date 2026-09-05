/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherDownloads.h"
#include "LauncherLocalization.h"
#include "LauncherS3.h"

#include <curl/curl.h>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

constexpr uint64_t kMaximumPackageBytes = 20ull * 1024ull * 1024ull * 1024ull;

struct DownloadContext
{
	std::ofstream output;
	uint64_t maximumBytes = kMaximumPackageBytes;
	uint64_t writtenBytes = 0;
};

struct RemoteMetadata
{
	uint64_t size = 0;
	std::string etag;
};

struct DownloadProgressContext
{
	ModificationOperationProgress *progress = nullptr;
	std::atomic_uint64_t *aggregateCompleted = nullptr;
	uint64_t resumeOffset = 0;
	uint64_t reportedBytes = 0;
};

void EnsureCurlInitialized()
{
	static std::once_flag initialized;
	std::call_once(initialized, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

void ApplyQaCertificateBundle(CURL *curl)
{
	// Echelon @test Codex 14/08/2026 Trust only an explicitly injected private CA in isolated download fixtures.
	if (const char *path = std::getenv("ECHELON_QA_CA_BUNDLE"); path && path[0]) {
		curl_easy_setopt(curl, CURLOPT_CAINFO, path);
	}
}

std::string Trim(std::string value)
{
	const size_t first = value.find_first_not_of(" \t\r\n\"");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n\"");
	return value.substr(first, last - first + 1);
}

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
	bool separator = false;
	for (unsigned char character : value) {
		if (std::isalnum(character)) {
			if (separator && !result.empty()) result.push_back('-');
			result.push_back(static_cast<char>(std::tolower(character)));
			separator = false;
		} else if (!result.empty()) {
			separator = true;
		}
	}
	return result.empty() ? "package" : result;
}

std::string Base64Url(const std::string &value)
{
	std::vector<unsigned char> encoded(4 * ((value.size() + 2) / 3) + 1);
	const int size = EVP_EncodeBlock(encoded.data(), reinterpret_cast<const unsigned char *>(value.data()),
		static_cast<int>(value.size()));
	std::string result(reinterpret_cast<char *>(encoded.data()), static_cast<size_t>(std::max(size, 0)));
	std::replace(result.begin(), result.end(), '+', '-');
	std::replace(result.begin(), result.end(), '/', '_');
	while (!result.empty() && result.back() == '=') result.pop_back();
	return result;
}

std::string NormalizeDownloadUrl(std::string url)
{
	if (url.rfind("https://www.dropbox.com/", 0) == 0 || url.rfind("https://dropbox.com/", 0) == 0) {
		// Echelon @bugfix Codex 15/08/2026 Preserve Dropbox share keys while requesting the direct file response.
		const size_t fragment = url.find('#');
		const size_t queryEnd = fragment == std::string::npos ? url.size() : fragment;
		const size_t query = url.find('?');
		if (query == std::string::npos || query > queryEnd) {
			url.insert(queryEnd, "?dl=1");
			return url;
		}
		for (size_t parameter = query + 1; parameter < queryEnd;) {
			const size_t separator = url.find('&', parameter);
			const size_t parameterEnd = separator == std::string::npos || separator > queryEnd ? queryEnd : separator;
			const size_t equals = url.find('=', parameter);
			if (url.compare(parameter, (equals < parameterEnd ? equals : parameterEnd) - parameter, "dl") == 0) {
				const size_t value = equals < parameterEnd ? equals + 1 : parameterEnd;
				url.replace(value, parameterEnd - value, "1");
				return url;
			}
			if (separator == std::string::npos || separator >= queryEnd) break;
			parameter = separator + 1;
		}
		url.insert(queryEnd, "&dl=1");
		return url;
	}
	if (url.rfind("https://1drv.ms/", 0) == 0 || url.rfind("https://onedrive.live.com/", 0) == 0) {
		return "https://api.onedrive.com/v1.0/shares/u!" + Base64Url(url) + "/root/content";
	}
	return url;
}

size_t CaptureHeader(char *data, size_t size, size_t count, void *userData)
{
	auto *metadata = static_cast<RemoteMetadata *>(userData);
	const size_t bytes = size * count;
	std::string header(data, bytes);
	const size_t separator = header.find(':');
	if (separator != std::string::npos) {
		const std::string name = ToLower(Trim(header.substr(0, separator)));
		if (name == "etag") metadata->etag = Trim(header.substr(separator + 1));
	}
	return bytes;
}

size_t WritePackage(char *data, size_t size, size_t count, void *userData)
{
	auto *context = static_cast<DownloadContext *>(userData);
	const size_t bytes = size * count;
	if (bytes > context->maximumBytes - std::min(context->writtenBytes, context->maximumBytes)) return 0;
	context->output.write(data, static_cast<std::streamsize>(bytes));
	if (!context->output) return 0;
	context->writtenBytes += bytes;
	return bytes;
}

int CancelTransfer(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto *progress = static_cast<ModificationOperationProgress *>(userData);
	return progress && progress->cancelRequested.load(std::memory_order_relaxed) ? 1 : 0;
}

int DownloadTransferProgress(void *userData, curl_off_t downloadTotal, curl_off_t downloadNow, curl_off_t, curl_off_t)
{
	auto *context = static_cast<DownloadProgressContext *>(userData);
	if (!context || !context->progress) return 0;
	if (!context->aggregateCompleted && downloadTotal > 0) context->progress->totalBytes.store(
		context->resumeOffset + static_cast<uint64_t>(downloadTotal), std::memory_order_relaxed);
	if (downloadNow >= 0) {
		const uint64_t current = context->resumeOffset + static_cast<uint64_t>(downloadNow);
		if (context->aggregateCompleted) {
			if (current > context->reportedBytes) {
				const uint64_t total = context->aggregateCompleted->fetch_add(
					current - context->reportedBytes, std::memory_order_relaxed) + current - context->reportedBytes;
				context->reportedBytes = current;
				context->progress->completedBytes.store(total, std::memory_order_relaxed);
			}
		} else {
			context->progress->completedBytes.store(current, std::memory_order_relaxed);
		}
	}
	return context->progress->cancelRequested.load(std::memory_order_relaxed) ? 1 : 0;
}

std::string DigestFile(const fs::path &path, std::string &errorMessage)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		errorMessage = "Cannot read downloaded file: " + path.filename().string();
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
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(count)) != 1) {
			EVP_MD_CTX_free(context);
			errorMessage = "Cannot update SHA-256";
			return {};
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
	for (unsigned int index = 0; index < digestSize; ++index) {
		output << std::setw(2) << static_cast<unsigned>(digest[index]);
	}
	return output.str();
}

bool IsSafeStrongEtag(const std::string &etag)
{
	return !etag.empty() && etag.rfind("W/", 0) != 0 &&
		std::all_of(etag.begin(), etag.end(), [](unsigned char character) {
			return std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == ':';
		});
}

bool ReadMetadata(const fs::path &path, RemoteMetadata &metadata)
{
	std::ifstream input(path);
	if (!input) return false;
	std::string line;
	while (std::getline(input, line)) {
		if (line.rfind("Size=", 0) == 0) {
			try { metadata.size = std::stoull(line.substr(5)); } catch (...) { return false; }
		} else if (line.rfind("ETag=", 0) == 0) {
			metadata.etag = line.substr(5);
		}
	}
	return true;
}

bool WriteMetadata(const fs::path &path, const RemoteMetadata &metadata)
{
	std::ofstream output(path, std::ios::trunc);
	if (!output) return false;
	output << "Size=" << metadata.size << "\nETag=" << metadata.etag << '\n';
	return static_cast<bool>(output);
}

bool QueryRemoteMetadata(const std::string &url, RemoteMetadata &metadata, std::string &errorMessage,
	ModificationOperationProgress *progress)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		errorMessage = "Cannot initialize HTTPS transport";
		return false;
	}
	char curlError[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	ApplyQaCertificateBundle(curl);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Echelon/1 ModificationInstallerV1");
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CaptureHeader);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &metadata);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CancelTransfer);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
	const CURLcode result = curl_easy_perform(curl);
	curl_off_t contentLength = -1;
	curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	curl_easy_cleanup(curl);
	if (result != CURLE_OK || responseCode < 200 || responseCode >= 400) {
		errorMessage = curlError[0] ? curlError : curl_easy_strerror(result);
		return false;
	}
	if (contentLength > 0) metadata.size = static_cast<uint64_t>(contentLength);
	return true;
}

bool DownloadPackage(const std::string &url, const fs::path &partialPath, const fs::path &metadataPath,
	uint64_t expectedSize, std::string &errorMessage, ModificationOperationProgress *progress,
	std::atomic_uint64_t *aggregateCompleted = nullptr, bool allowFullRestart = true)
{
	RemoteMetadata remote;
	if (!QueryRemoteMetadata(url, remote, errorMessage, progress)) return false;
	if (remote.size > kMaximumPackageBytes || expectedSize > kMaximumPackageBytes) {
		errorMessage = "Modification package exceeds the safety limit";
		return false;
	}
	if (expectedSize != 0 && remote.size != 0 && expectedSize != remote.size) {
		errorMessage = "Remote package size does not match its catalog";
		return false;
	}
	std::error_code error;
	RemoteMetadata previous;
	uint64_t resumeOffset = fs::is_regular_file(partialPath, error) ? fs::file_size(partialPath, error) : 0;
	if (resumeOffset > 0 && (!ReadMetadata(metadataPath, previous) ||
		(!remote.etag.empty() && remote.etag != previous.etag) || (remote.size != 0 && remote.size != previous.size))) {
		fs::remove(partialPath, error);
		resumeOffset = 0;
	}
	if (remote.size != 0 && resumeOffset > remote.size) {
		fs::remove(partialPath, error);
		resumeOffset = 0;
	}
	DownloadProgressContext progressContext{progress, aggregateCompleted, resumeOffset, resumeOffset};
	if (progress) {
		if (aggregateCompleted) {
			const uint64_t total = aggregateCompleted->fetch_add(resumeOffset, std::memory_order_relaxed) + resumeOffset;
			progress->completedBytes.store(total, std::memory_order_relaxed);
		} else {
			progress->completedBytes.store(resumeOffset, std::memory_order_relaxed);
			progress->totalBytes.store(expectedSize != 0 ? expectedSize : remote.size, std::memory_order_relaxed);
		}
	}
	if (remote.size != 0 && resumeOffset == remote.size) return true;
	if (!WriteMetadata(metadataPath, remote)) {
		errorMessage = "Cannot save resumable download metadata";
		return false;
	}
	DownloadContext context;
	context.maximumBytes = expectedSize != 0 ? expectedSize : (remote.size != 0 ? remote.size : kMaximumPackageBytes);
	context.writtenBytes = resumeOffset;
	context.output.open(partialPath, std::ios::binary | (resumeOffset ? std::ios::app : std::ios::trunc));
	if (!context.output) {
		errorMessage = "Cannot open resumable package file";
		return false;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		errorMessage = "Cannot initialize HTTPS transport";
		return false;
	}
	char curlError[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	ApplyQaCertificateBundle(curl);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Echelon/1 ModificationInstallerV1");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WritePackage);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadTransferProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressContext);
	if (resumeOffset != 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeOffset));
	curl_slist *requestHeaders = nullptr;
	if (IsSafeStrongEtag(remote.etag)) {
		requestHeaders = curl_slist_append(requestHeaders, ("If-Match: \"" + remote.etag + "\"").c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);
	}
	const CURLcode result = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	if (requestHeaders) curl_slist_free_all(requestHeaders);
	curl_easy_cleanup(curl);
	context.output.close();
	if (responseCode == 412) {
		fs::remove(partialPath, error);
		fs::remove(metadataPath, error);
		errorMessage = "Remote package changed during download";
		return false;
	}
	if (resumeOffset != 0 && responseCode == 200) {
		// Echelon @bugfix Codex 15/08/2026 Restart safely when an origin ignores Range instead of requiring a second click.
		std::error_code cleanupError;
		fs::remove(partialPath, cleanupError);
		if (!cleanupError) fs::remove(metadataPath, cleanupError);
		if (aggregateCompleted && progressContext.reportedBytes != 0) {
			const uint64_t remaining = aggregateCompleted->fetch_sub(
				progressContext.reportedBytes, std::memory_order_relaxed) - progressContext.reportedBytes;
			if (progress) progress->completedBytes.store(remaining, std::memory_order_relaxed);
		} else if (progress) {
			progress->completedBytes.store(0, std::memory_order_relaxed);
		}
		if (cleanupError) {
			errorMessage = "Cannot discard a partial download after the server ignored Range: " + cleanupError.message();
			return false;
		}
		if (!allowFullRestart) {
			errorMessage = "Remote server repeatedly ignored the resumable Range request";
			return false;
		}
		fprintf(stderr, "INFO: Download origin ignored Range; restarting %s from byte zero\n",
			partialPath.filename().string().c_str());
		fflush(stderr);
		return DownloadPackage(url, partialPath, metadataPath, expectedSize, errorMessage, progress,
			aggregateCompleted, false);
	}
	if (resumeOffset != 0 && responseCode != 206) {
		errorMessage = "Remote server returned an invalid response to the resumable Range request";
		return false;
	}
	if (result != CURLE_OK || responseCode < 200 || responseCode >= 300) {
		errorMessage = curlError[0] ? curlError : curl_easy_strerror(result);
		return false;
	}
	const uint64_t downloadedSize = fs::file_size(partialPath, error);
	const uint64_t requiredSize = expectedSize != 0 ? expectedSize : remote.size;
	if (error || downloadedSize > kMaximumPackageBytes || (requiredSize != 0 && downloadedSize != requiredSize)) {
		errorMessage = "Downloaded package is incomplete";
		return false;
	}
	if (progress && progressContext.reportedBytes < downloadedSize) {
		if (aggregateCompleted) {
			const uint64_t total = aggregateCompleted->fetch_add(
				downloadedSize - progressContext.reportedBytes, std::memory_order_relaxed) +
				downloadedSize - progressContext.reportedBytes;
			progress->completedBytes.store(total, std::memory_order_relaxed);
		} else {
			progress->completedBytes.store(downloadedSize, std::memory_order_relaxed);
		}
	}
	return true;
}

// Echelon @feature Codex 15/08/2026 Download immutable author-hosted file sets concurrently and resume each file independently.
bool DownloadContentFiles(const RepositoryModification &modification, const fs::path &downloadRoot,
	fs::path &contentRoot, std::string &errorMessage, ModificationOperationProgress *progress)
{
	contentRoot = downloadRoot / "content";
	const fs::path partialRoot = downloadRoot / "partials";
	std::error_code error;
	fs::create_directories(contentRoot, error);
	if (!error) fs::create_directories(partialRoot, error);
	if (error) {
		errorMessage = "Cannot create multi-file download directory: " + error.message();
		return false;
	}
	uint64_t totalSize = 0;
	for (const RepositoryContentFile &file : modification.contentFiles) {
		if (file.size > kMaximumPackageBytes - totalSize) {
			errorMessage = "Modification file set exceeds the safety limit";
			return false;
		}
		totalSize += file.size;
	}
	std::atomic_uint64_t aggregateCompleted{0};
	if (progress) {
		progress->completedBytes.store(0, std::memory_order_relaxed);
		progress->totalBytes.store(totalSize, std::memory_order_relaxed);
	}
	std::atomic_size_t nextFile{0};
	std::atomic_bool failed{false};
	std::mutex failureMutex;
	auto worker = [&]() {
		while (!failed.load(std::memory_order_relaxed) &&
			!(progress && progress->cancelRequested.load(std::memory_order_relaxed))) {
			const size_t index = nextFile.fetch_add(1, std::memory_order_relaxed);
			if (index >= modification.contentFiles.size()) return;
			const RepositoryContentFile &file = modification.contentFiles[index];
			const fs::path target = contentRoot / file.path;
			const fs::path partial = fs::path((partialRoot / file.path).string() + ".partial");
			const fs::path metadata = fs::path((partialRoot / file.path).string() + ".download.ini");
			std::error_code fileError;
			fs::create_directories(target.parent_path(), fileError);
			if (!fileError) fs::create_directories(partial.parent_path(), fileError);
			std::string failure;
			bool ready = false;
			const bool targetExists = !fileError && fs::is_regular_file(target, fileError);
			if (fileError == std::errc::no_such_file_or_directory) fileError.clear();
			if (!fileError && targetExists && fs::file_size(target, fileError) == file.size) {
				ready = DigestFile(target, failure) == ToLower(file.sha256);
				if (ready) {
					const uint64_t completed = aggregateCompleted.fetch_add(file.size, std::memory_order_relaxed) + file.size;
					if (progress) progress->completedBytes.store(completed, std::memory_order_relaxed);
				}
			}
			if (!ready && !fileError) {
				fs::remove(target, fileError);
				if (fileError == std::errc::no_such_file_or_directory) fileError.clear();
				const std::string url = NormalizeDownloadUrl(file.downloadUrl);
				if (url.rfind("https://", 0) != 0) {
					failure = "Modification content URL is not HTTPS";
				} else if (DownloadPackage(url, partial, metadata, file.size, failure, progress, &aggregateCompleted)) {
					const std::string digest = DigestFile(partial, failure);
					if (!digest.empty() && digest == ToLower(file.sha256)) {
						fs::rename(partial, target, fileError);
						if (!fileError) {
							fs::remove(metadata, fileError);
							ready = true;
						}
					} else if (failure.empty()) {
						failure = "Downloaded file SHA-256 does not match its catalog";
					}
				}
			}
			if (!ready) {
				if (failure.empty()) failure = fileError ? fileError.message() : "Cannot publish downloaded content file";
				std::lock_guard<std::mutex> lock(failureMutex);
				if (!failed.exchange(true, std::memory_order_relaxed)) {
					errorMessage = file.path.generic_string() + ": " + failure;
				}
				return;
			}
		}
	};
	const size_t workerCount = std::min<size_t>(4, modification.contentFiles.size());
	std::vector<std::thread> workers;
	workers.reserve(workerCount);
	for (size_t index = 0; index < workerCount; ++index) workers.emplace_back(worker);
	for (std::thread &thread : workers) thread.join();
	if (progress && progress->cancelRequested.load(std::memory_order_relaxed)) return false;
	return !failed.load(std::memory_order_relaxed) &&
		nextFile.load(std::memory_order_relaxed) >= modification.contentFiles.size();
}

} // namespace

ModificationOperationResult DownloadAndInstallModification(const RepositoryModification &modification,
	const fs::path &modsRoot, ModificationOperationProgress *progress)
{
	if (modification.generator == "retail-russian-merge-v1") {
		return GenerateRetailRussianLocalization(modification, modsRoot, progress);
	}
	ModificationOperationResult result;
	if (modification.downloadUrl.empty() && modification.contentFiles.empty() && modification.s3Host.empty()) {
		result.message = "Repository item has no downloadable package";
		return result;
	}
	if (!modification.contentFiles.empty()) {
		EnsureCurlInitialized();
		const fs::path downloadRoot = modsRoot / ".staging" / "downloads" /
			Slugify(modification.id) / Slugify(modification.version);
		fs::path contentRoot;
		if (!DownloadContentFiles(modification, downloadRoot, contentRoot, result.message, progress)) {
			result.cancelled = progress && progress->cancelRequested.load(std::memory_order_relaxed);
			if (result.cancelled) result.message = "Download paused; progress was preserved";
			return result;
		}
		LocalImportRequest request;
		request.inputPath = contentRoot;
		request.modsRoot = modsRoot;
		request.engine = modification.engine;
		request.type = modification.type;
		request.parentId = modification.parentId;
		request.requirements = modification.requirements;
		request.conflicts = modification.conflicts;
		request.displayName = modification.name;
		request.version = modification.version;
		request.sourceId = modification.sourceId;
		request.modificationId = modification.id;
		request.source = modification.descriptorUrl;
		request.coverImagePath = modification.coverCachePath;
		result = ImportLocalModification(request, progress);
		std::error_code error;
		if (result.success) fs::remove_all(downloadRoot, error);
		return result;
	}
	if (modification.downloadUrl.empty() && !modification.s3Host.empty() && !modification.s3Folder.empty()) {
		fs::path contentRoot;
		if (!DownloadS3Modification(modification, modsRoot, contentRoot, result.message, progress)) {
			result.cancelled = progress && progress->cancelRequested.load(std::memory_order_relaxed);
			return result;
		}
		LocalImportRequest request;
		request.inputPath = contentRoot;
		request.modsRoot = modsRoot;
		request.engine = modification.engine;
		request.type = modification.type;
		request.parentId = modification.parentId;
		request.requirements = modification.requirements;
		request.conflicts = modification.conflicts;
		request.displayName = modification.name;
		request.version = modification.version;
		request.sourceId = modification.sourceId;
		request.modificationId = modification.id;
		request.source = modification.descriptorUrl;
		request.coverImagePath = modification.coverCachePath;
		result = ImportLocalModification(request, progress);
		std::error_code error;
		if (result.success) fs::remove_all(contentRoot.parent_path(), error);
		return result;
	}
	const std::string url = NormalizeDownloadUrl(modification.downloadUrl);
	if (url.rfind("https://", 0) != 0) {
		result.message = "Modification package URL is not HTTPS";
		return result;
	}
	EnsureCurlInitialized();
	const fs::path downloadRoot = modsRoot / ".staging" / "downloads" /
		Slugify(modification.id) / Slugify(modification.version);
	std::error_code error;
	fs::create_directories(downloadRoot, error);
	if (error) {
		result.message = "Cannot create resumable download directory: " + error.message();
		return result;
	}
	const fs::path packagePath = downloadRoot / "package.partial";
	std::string downloadError;
	if (!DownloadPackage(url, packagePath, downloadRoot / "download.ini", modification.size,
		downloadError, progress)) {
		result.cancelled = progress && progress->cancelRequested.load(std::memory_order_relaxed);
		result.message = result.cancelled ? "Download paused; progress was preserved" : downloadError;
		return result;
	}
	LocalImportRequest request;
	request.inputPath = packagePath;
	request.modsRoot = modsRoot;
	request.engine = modification.engine;
	request.type = modification.type;
	request.parentId = modification.parentId;
	request.requirements = modification.requirements;
	request.conflicts = modification.conflicts;
	request.displayName = modification.name;
	request.version = modification.version;
	request.sourceId = modification.sourceId;
	request.modificationId = modification.id;
	request.source = modification.descriptorUrl.empty() ? modification.downloadUrl : modification.descriptorUrl;
	request.expectedSha256 = modification.sha256;
	request.expectedSize = modification.size;
	request.coverImagePath = modification.coverCachePath;
	result = ImportLocalModification(request, progress);
	if (result.success) fs::remove_all(downloadRoot, error);
	return result;
}

} // namespace EchelonLauncher
