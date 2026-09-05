/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherS3.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace GeneralsArsenalLauncher
{
namespace
{

constexpr uint64_t kMaximumS3Bytes = 20ull * 1024ull * 1024ull * 1024ull;
constexpr size_t kMaximumS3Files = 200000;
constexpr size_t kMaximumListBytes = 32 * 1024 * 1024;

struct Credentials
{
	std::string accessKey;
	std::string secretKey;
	std::string sessionToken;
	std::string region = "us-east-1";
};

struct S3Object
{
	std::string key;
	std::string etag;
	uint64_t size = 0;
};

struct FileDownloadContext
{
	std::ofstream output;
	ModificationOperationProgress *progress = nullptr;
	uint64_t currentSize = 0;
	uint64_t maximumSize = 0;
};

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string Trim(std::string value)
{
	const size_t first = value.find_first_not_of(" \t\r\n\"");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n\"");
	return value.substr(first, last - first + 1);
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
	return result.empty() ? "s3-item" : result;
}

std::string Hex(const unsigned char *data, size_t size)
{
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (size_t index = 0; index < size; ++index) output << std::setw(2) << static_cast<unsigned int>(data[index]);
	return output.str();
}

std::vector<unsigned char> Sha256(const std::string &value)
{
	std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
	unsigned int size = 0;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
		EVP_DigestUpdate(context, value.data(), value.size()) != 1 ||
		EVP_DigestFinal_ex(context, digest.data(), &size) != 1) {
		if (context) EVP_MD_CTX_free(context);
		return {};
	}
	EVP_MD_CTX_free(context);
	digest.resize(size);
	return digest;
}

std::vector<unsigned char> Hmac(const std::vector<unsigned char> &key, const std::string &value)
{
	std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
	unsigned int size = 0;
	if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
		reinterpret_cast<const unsigned char *>(value.data()), value.size(), digest.data(), &size)) return {};
	digest.resize(size);
	return digest;
}

std::string UriEncode(const std::string &value, bool preserveSlash)
{
	std::ostringstream output;
	output << std::uppercase << std::hex << std::setfill('0');
	for (unsigned char character : value) {
		if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~' ||
			(preserveSlash && character == '/')) {
			output << static_cast<char>(character);
		} else {
			output << '%' << std::setw(2) << static_cast<unsigned int>(character);
		}
	}
	return output.str();
}

std::string XmlDecode(std::string value)
{
	struct Replacement { const char *encoded; const char *decoded; };
	static constexpr Replacement replacements[] = {
		{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}
	};
	for (const auto &replacement : replacements) {
		size_t position = 0;
		while ((position = value.find(replacement.encoded, position)) != std::string::npos) {
			value.replace(position, std::strlen(replacement.encoded), replacement.decoded);
			position += std::strlen(replacement.decoded);
		}
	}
	return value;
}

std::string XmlTag(const std::string &xml, const std::string &tag, size_t start = 0)
{
	const std::string opening = "<" + tag + ">";
	const std::string closing = "</" + tag + ">";
	const size_t first = xml.find(opening, start);
	if (first == std::string::npos) return {};
	const size_t last = xml.find(closing, first + opening.size());
	if (last == std::string::npos) return {};
	return XmlDecode(xml.substr(first + opening.size(), last - first - opening.size()));
}

bool IsSafeRelativePath(const fs::path &path)
{
	if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
	for (const fs::path &component : path.lexically_normal()) if (component == "..") return false;
	return true;
}

bool IsForbiddenBinary(const fs::path &path)
{
	const std::string extension = ToLower(path.extension().string());
	return extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".dylib" ||
		extension == ".asi";
}

bool IsSafeS3Etag(const std::string &etag)
{
	return !etag.empty() && std::all_of(etag.begin(), etag.end(), [](unsigned char character) {
		return std::isxdigit(character) || character == '-';
	});
}

Credentials LoadCredentials(const fs::path &modsRoot, const std::string &sourceId)
{
	Credentials result;
	std::ifstream input(modsRoot.parent_path() / "Launcher" / "Credentials.ini");
	std::string activeSection;
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos || line[first] == ';' || line[first] == '#') continue;
		if (line[first] == '[') {
			const size_t closing = line.find(']', first + 1);
			activeSection = closing == std::string::npos ? std::string{} : ToLower(line.substr(first + 1, closing - first - 1));
			continue;
		}
		if (activeSection != "s3" && activeSection != "s3." + ToLower(sourceId)) continue;
		const size_t separator = line.find('=', first);
		if (separator == std::string::npos) continue;
		const std::string key = ToLower(Trim(line.substr(first, separator - first)));
		const std::string value = Trim(line.substr(separator + 1));
		if (key == "accesskey") result.accessKey = value;
		else if (key == "secretkey") result.secretKey = value;
		else if (key == "sessiontoken") result.sessionToken = value;
		else if (key == "region" && !value.empty()) result.region = value;
	}
	auto overrideFromEnvironment = [](const char *name, std::string &target) {
		if (const char *value = std::getenv(name); value && value[0]) target = value;
	};
	overrideFromEnvironment("GENERALS_ARSENAL_S3_ACCESS_KEY", result.accessKey);
	overrideFromEnvironment("GENERALS_ARSENAL_S3_SECRET_KEY", result.secretKey);
	overrideFromEnvironment("GENERALS_ARSENAL_S3_SESSION_TOKEN", result.sessionToken);
	overrideFromEnvironment("GENERALS_ARSENAL_S3_REGION", result.region);
	return result;
}

bool NormalizeEndpoint(const std::string &host, std::string &endpoint, std::string &authority)
{
	endpoint = host;
	if (endpoint.rfind("https://", 0) != 0) endpoint = "https://" + endpoint;
	while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
	if (endpoint.rfind("https://", 0) != 0 || endpoint.size() <= 8 || endpoint.find_first_of("\r\n") != std::string::npos) return false;
	authority = endpoint.substr(8);
	return !authority.empty() && authority.find('/') == std::string::npos;
}

std::vector<std::string> SignedHeaders(const Credentials &credentials, const std::string &authority,
	const std::string &canonicalUri, const std::string &canonicalQuery, const std::string &date,
	const std::string &dateTime)
{
	static const std::string emptyPayloadHash = []() {
		const std::vector<unsigned char> digest = Sha256({});
		return Hex(digest.data(), digest.size());
	}();
	std::string canonicalHeaders = "host:" + authority + "\nx-amz-content-sha256:" + emptyPayloadHash +
		"\nx-amz-date:" + dateTime + "\n";
	std::string signedHeaderNames = "host;x-amz-content-sha256;x-amz-date";
	if (!credentials.sessionToken.empty()) {
		canonicalHeaders += "x-amz-security-token:" + credentials.sessionToken + "\n";
		signedHeaderNames += ";x-amz-security-token";
	}
	const std::string canonicalRequest = "GET\n" + canonicalUri + "\n" + canonicalQuery + "\n" +
		canonicalHeaders + "\n" + signedHeaderNames + "\n" + emptyPayloadHash;
	const std::string scope = date + "/" + credentials.region + "/s3/aws4_request";
	const std::vector<unsigned char> canonicalDigest = Sha256(canonicalRequest);
	const std::string stringToSign = "AWS4-HMAC-SHA256\n" + dateTime + "\n" + scope + "\n" +
		Hex(canonicalDigest.data(), canonicalDigest.size());
	const std::string secret = "AWS4" + credentials.secretKey;
	std::vector<unsigned char> key(secret.begin(), secret.end());
	key = Hmac(key, date);
	key = Hmac(key, credentials.region);
	key = Hmac(key, "s3");
	key = Hmac(key, "aws4_request");
	const std::vector<unsigned char> signature = Hmac(key, stringToSign);
	return {
		"Authorization: AWS4-HMAC-SHA256 Credential=" + credentials.accessKey + "/" + scope +
			", SignedHeaders=" + signedHeaderNames + ", Signature=" + Hex(signature.data(), signature.size()),
		"x-amz-content-sha256: " + emptyPayloadHash,
		"x-amz-date: " + dateTime,
		credentials.sessionToken.empty() ? std::string{} : "x-amz-security-token: " + credentials.sessionToken
	};
}

std::vector<std::string> BuildRequestHeaders(const Credentials &credentials, const std::string &authority,
	const std::string &uri, const std::string &query)
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &time);
#else
	gmtime_r(&time, &utc);
#endif
	char date[9]{};
	char dateTime[17]{};
	std::strftime(date, sizeof(date), "%Y%m%d", &utc);
	std::strftime(dateTime, sizeof(dateTime), "%Y%m%dT%H%M%SZ", &utc);
	return SignedHeaders(credentials, authority, uri, query, date, dateTime);
}

curl_slist *MakeCurlHeaders(const std::vector<std::string> &headers)
{
	curl_slist *result = nullptr;
	for (const std::string &header : headers) if (!header.empty()) result = curl_slist_append(result, header.c_str());
	return result;
}

size_t WriteText(char *data, size_t size, size_t count, void *userData)
{
	auto *body = static_cast<std::string *>(userData);
	const size_t bytes = size * count;
	if (bytes > kMaximumListBytes - std::min(body->size(), kMaximumListBytes)) return 0;
	body->append(data, bytes);
	return bytes;
}

size_t WriteFile(char *data, size_t size, size_t count, void *userData)
{
	auto *context = static_cast<FileDownloadContext *>(userData);
	const size_t bytes = size * count;
	if (bytes > context->maximumSize - std::min(context->currentSize, context->maximumSize)) return 0;
	context->output.write(data, static_cast<std::streamsize>(bytes));
	if (!context->output) return 0;
	context->currentSize += bytes;
	if (context->progress) context->progress->completedBytes.fetch_add(bytes, std::memory_order_relaxed);
	return bytes;
}

int TransferProgress(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *progress = static_cast<const ModificationOperationProgress *>(userData);
	return progress && progress->cancelRequested.load(std::memory_order_relaxed) ? 1 : 0;
}

bool PerformSignedGet(const std::string &url, const std::vector<std::string> &headers,
	size_t (*writeCallback)(char *, size_t, size_t, void *), void *writeData,
	ModificationOperationProgress *progress, std::string &errorMessage, uint64_t resumeOffset = 0,
	const std::string &expectedEtag = {})
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		errorMessage = "Cannot initialize S3 HTTPS transport";
		return false;
	}
	std::vector<std::string> requestHeaders = headers;
	if (!expectedEtag.empty()) requestHeaders.push_back("If-Match: \"" + expectedEtag + "\"");
	curl_slist *curlHeaders = MakeCurlHeaders(requestHeaders);
	char curlError[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Generals-Arsenal/1 S3SigV4");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, writeData);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, TransferProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
	if (resumeOffset != 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeOffset));
	const CURLcode result = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	curl_slist_free_all(curlHeaders);
	curl_easy_cleanup(curl);
	if (responseCode == 412) {
		errorMessage = "S3 object changed during download";
		return false;
	}
	if (resumeOffset != 0 && responseCode != 206) {
		errorMessage = "S3 endpoint did not honor the resumable Range request";
		return false;
	}
	if (result != CURLE_OK || responseCode < 200 || responseCode >= 300) {
		errorMessage = curlError[0] ? curlError : curl_easy_strerror(result);
		return false;
	}
	return true;
}

bool ParseListObjects(const std::string &xml, std::vector<S3Object> &objects,
	bool &truncated, std::string &continuationToken, std::string &errorMessage)
{
	truncated = ToLower(XmlTag(xml, "IsTruncated")) == "true";
	continuationToken = XmlTag(xml, "NextContinuationToken");
	size_t position = 0;
	while ((position = xml.find("<Contents>", position)) != std::string::npos) {
		const size_t end = xml.find("</Contents>", position);
		if (end == std::string::npos) {
			errorMessage = "Malformed S3 ListObjectsV2 response";
			return false;
		}
		const std::string entry = xml.substr(position, end - position);
		S3Object object;
		object.key = XmlTag(entry, "Key");
		object.etag = ToLower(Trim(XmlTag(entry, "ETag")));
		try { object.size = std::stoull(XmlTag(entry, "Size")); } catch (...) {
			errorMessage = "Malformed S3 object size";
			return false;
		}
		if (!object.key.empty()) objects.push_back(std::move(object));
		position = end + 11;
	}
	if (truncated && continuationToken.empty()) {
		errorMessage = "S3 response is truncated without a continuation token";
		return false;
	}
	return true;
}

std::string DigestFile(const fs::path &path, const EVP_MD *algorithm)
{
	std::ifstream input(path, std::ios::binary);
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!input || !context || EVP_DigestInit_ex(context, algorithm, nullptr) != 1) {
		if (context) EVP_MD_CTX_free(context);
		return {};
	}
	std::vector<char> buffer(1024 * 1024);
	while (input) {
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(count)) != 1) {
			EVP_MD_CTX_free(context);
			return {};
		}
	}
	unsigned char digest[EVP_MAX_MD_SIZE]{};
	unsigned int size = 0;
	const bool success = EVP_DigestFinal_ex(context, digest, &size) == 1;
	EVP_MD_CTX_free(context);
	return success ? Hex(digest, size) : std::string{};
}

} // namespace

bool DownloadS3Modification(const RepositoryModification &modification,
	const fs::path &modsRoot, fs::path &contentRoot, std::string &errorMessage,
	ModificationOperationProgress *progress)
{
	errorMessage.clear();
	Credentials credentials = LoadCredentials(modsRoot, modification.sourceId);
	if (credentials.accessKey.empty() || credentials.secretKey.empty()) {
		errorMessage = "S3 credentials are not configured for this repository";
		return false;
	}
	std::string endpoint;
	std::string authority;
	if (!NormalizeEndpoint(modification.s3Host, endpoint, authority)) {
		errorMessage = "S3 endpoint must use a valid HTTPS host";
		return false;
	}
	if (modification.s3Bucket.empty() || modification.s3Folder.empty()) {
		errorMessage = "S3 repository item is missing bucket or folder";
		return false;
	}
	const std::string bucket = UriEncode(modification.s3Bucket, false);
	const std::string prefix = modification.s3Folder.back() == '/' ? modification.s3Folder : modification.s3Folder + "/";
	std::vector<S3Object> objects;
	std::string continuationToken;
	bool truncated = false;
	do {
		std::vector<std::pair<std::string, std::string>> queryFields = {
			{"list-type", "2"}, {"max-keys", "1000"}, {"prefix", prefix}
		};
		if (!continuationToken.empty()) queryFields.push_back({"continuation-token", continuationToken});
		std::sort(queryFields.begin(), queryFields.end());
		std::string query;
		for (const auto &[key, value] : queryFields) {
			if (!query.empty()) query.push_back('&');
			query += UriEncode(key, false) + "=" + UriEncode(value, false);
		}
		const std::string uri = "/" + bucket;
		std::string body;
		const std::vector<std::string> headers = BuildRequestHeaders(credentials, authority, uri, query);
		if (!PerformSignedGet(endpoint + uri + "?" + query, headers, WriteText, &body, progress, errorMessage)) return false;
		continuationToken.clear();
		if (!ParseListObjects(body, objects, truncated, continuationToken, errorMessage)) return false;
		if (objects.size() > kMaximumS3Files) {
			errorMessage = "S3 modification contains too many files";
			return false;
		}
	} while (truncated);
	if (objects.empty()) {
		errorMessage = "S3 modification folder is empty";
		return false;
	}
	uint64_t totalBytes = 0;
	for (const S3Object &object : objects) {
		if (object.size > kMaximumS3Bytes - std::min(totalBytes, kMaximumS3Bytes)) {
			errorMessage = "S3 modification exceeds the safety limit";
			return false;
		}
		totalBytes += object.size;
	}
	if (progress) {
		progress->completedBytes.store(0, std::memory_order_relaxed);
		progress->totalBytes.store(totalBytes, std::memory_order_relaxed);
	}
	contentRoot = modsRoot / ".staging" / "s3" / Slugify(modification.id) / Slugify(modification.version) / "content";
	std::error_code fileError;
	fs::create_directories(contentRoot, fileError);
	if (fileError) {
		errorMessage = "Cannot create S3 staging directory: " + fileError.message();
		return false;
	}
	for (const S3Object &object : objects) {
		if (progress && progress->cancelRequested.load(std::memory_order_relaxed)) {
			errorMessage = "S3 download paused; progress was preserved";
			return false;
		}
		if (object.key.rfind(prefix, 0) != 0) {
			errorMessage = "S3 object escaped the declared folder";
			return false;
		}
		if (!IsSafeS3Etag(object.etag)) {
			errorMessage = "S3 object has an invalid ETag";
			return false;
		}
		const fs::path relative = fs::path(object.key.substr(prefix.size())).lexically_normal();
		if (!IsSafeRelativePath(relative) || IsForbiddenBinary(relative)) {
			errorMessage = "S3 modification contains an unsafe or executable path";
			return false;
		}
		const fs::path target = contentRoot / relative;
		if (object.key.back() == '/') {
			fs::create_directories(target, fileError);
			continue;
		}
		fs::create_directories(target.parent_path(), fileError);
		const fs::path partial = target.string() + ".partial";
		uint64_t resumeOffset = fs::is_regular_file(partial, fileError) ? fs::file_size(partial, fileError) : 0;
		if (resumeOffset > object.size) {
			fs::remove(partial, fileError);
			resumeOffset = 0;
		}
		if (fs::is_regular_file(target, fileError) && fs::file_size(target, fileError) == object.size &&
			(object.etag.find('-') != std::string::npos || DigestFile(target, EVP_md5()) == object.etag)) {
			if (progress) progress->completedBytes.fetch_add(object.size, std::memory_order_relaxed);
			continue;
		}
		FileDownloadContext context;
		context.progress = progress;
		context.currentSize = resumeOffset;
		context.maximumSize = object.size;
		context.output.open(partial, std::ios::binary | (resumeOffset ? std::ios::app : std::ios::trunc));
		if (!context.output) {
			errorMessage = "Cannot open S3 staging file";
			return false;
		}
		if (progress && resumeOffset) progress->completedBytes.fetch_add(resumeOffset, std::memory_order_relaxed);
		const std::string uri = "/" + bucket + "/" + UriEncode(object.key, true);
		const std::vector<std::string> headers = BuildRequestHeaders(credentials, authority, uri, {});
		if (!PerformSignedGet(endpoint + uri, headers, WriteFile, &context, progress, errorMessage,
			resumeOffset, object.etag)) {
			if (errorMessage.find("changed during download") != std::string::npos ||
				errorMessage.find("did not honor") != std::string::npos) fs::remove(partial, fileError);
			return false;
		}
		context.output.close();
		if (fs::file_size(partial, fileError) != object.size ||
			(object.etag.find('-') == std::string::npos && !object.etag.empty() && DigestFile(partial, EVP_md5()) != object.etag)) {
			fs::remove(partial, fileError);
			errorMessage = "S3 object failed its ETag/MD5 integrity check";
			return false;
		}
		fs::rename(partial, target, fileError);
		if (fileError) {
			errorMessage = "Cannot publish downloaded S3 object: " + fileError.message();
			return false;
		}
	}
	return true;
}

} // namespace GeneralsArsenalLauncher
