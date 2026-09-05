/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherRepositories.h"

#include "GeneralsArsenalLauncher/BrandIdentity.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace GeneralsArsenalLauncher
{
namespace
{

constexpr size_t kMaximumCatalogBytes = 16 * 1024 * 1024;
constexpr size_t kMaximumContentFiles = 4096;
constexpr uint64_t kMaximumModificationBytes = 20ull * 1024ull * 1024ull * 1024ull;
constexpr auto kCacheLifetime = std::chrono::hours(24);

void EnsureCurlInitialized()
{
	static std::once_flag initialized;
	std::call_once(initialized, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string StringValue(const YAML::Node &node, const char *key)
{
	const YAML::Node value = node[key];
	if (!value || !value.IsScalar()) return {};
	try {
		return value.as<std::string>();
	} catch (const YAML::Exception &) {
		return {};
	}
}

uint64_t UnsignedValue(const YAML::Node &node, const char *key)
{
	const YAML::Node value = node[key];
	if (!value || !value.IsScalar()) return 0;
	try {
		return value.as<uint64_t>();
	} catch (const YAML::Exception &) {
		return 0;
	}
}

std::string HexDigest(const std::string &value)
{
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context) return {};
	unsigned char digest[EVP_MAX_MD_SIZE]{};
	unsigned int digestSize = 0;
	const bool success = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
		EVP_DigestUpdate(context, value.data(), value.size()) == 1 &&
		EVP_DigestFinal_ex(context, digest, &digestSize) == 1;
	EVP_MD_CTX_free(context);
	if (!success) return {};
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (unsigned int index = 0; index < digestSize; ++index) output << std::setw(2) << static_cast<unsigned>(digest[index]);
	return output.str();
}

std::string Slugify(const std::string &name, const std::string &stableFallback)
{
	std::string result;
	bool separatorPending = false;
	for (unsigned char character : name) {
		if (std::isalnum(character)) {
			if (separatorPending && !result.empty()) result.push_back('-');
			result.push_back(static_cast<char>(std::tolower(character)));
			separatorPending = false;
		} else if (!result.empty()) {
			separatorPending = true;
		}
	}
	if (!result.empty()) return result;
	const std::string digest = HexDigest(stableFallback);
	return digest.empty() ? "unnamed" : "item-" + digest.substr(0, 16);
}

bool ParseModificationType(const std::string &value, ModificationType &type)
{
	const std::string normalized = ToLower(value);
	if (normalized == "mod") type = ModificationType::Mod;
	else if (normalized == "patch") type = ModificationType::Patch;
	else if (normalized == "addon") type = ModificationType::Addon;
	else return false;
	return true;
}

bool IsSha256(const std::string &value)
{
	return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isxdigit(character) != 0;
	});
}

bool IsSafeRepositoryId(const std::string &value)
{
	return !value.empty() && value.size() <= 96 && std::isalnum(static_cast<unsigned char>(value.front())) &&
		std::all_of(value.begin(), value.end(), [](unsigned char character) {
			return std::islower(character) || std::isdigit(character) || character == '.' || character == '-';
		});
}

bool IsSafeSelectorVersion(const std::string &value)
{
	return !value.empty() && value.size() <= 128 &&
		std::all_of(value.begin(), value.end(), [](unsigned char character) {
			return std::isalnum(character) || character == '.' || character == '_' || character == '-' ||
				character == '+' || character == '~';
		});
}

std::string HttpsOrigin(const std::string &url);

bool IsForbiddenContentExtension(const fs::path &path)
{
	const std::string extension = ToLower(path.extension().string());
	return extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".dylib" ||
		extension == ".asi";
}

bool IsSafeContentPath(const std::string &value)
{
	if (value.empty() || value.size() > 1024 || value.front() == '/' || value.front() == '\\' ||
		value.find('\\') != std::string::npos || value.find(':') != std::string::npos ||
		std::any_of(value.begin(), value.end(), [](unsigned char character) { return character < 32; })) return false;
	const fs::path path(value);
	if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
		path.lexically_normal() != path || IsForbiddenContentExtension(path)) return false;
	for (const fs::path &part : path) {
		if (part.empty() || part == "." || part == "..") return false;
	}
	return true;
}

bool ParseContentFiles(const YAML::Node &item, std::vector<RepositoryContentFile> &files)
{
	const YAML::Node entries = item["Files"];
	if (!entries) return true;
	if (!entries.IsSequence() || entries.size() == 0 || entries.size() > kMaximumContentFiles) return false;
	std::unordered_set<std::string> paths;
	uint64_t totalSize = 0;
	for (const YAML::Node &entry : entries) {
		if (!entry.IsMap()) return false;
		RepositoryContentFile file;
		const std::string path = StringValue(entry, "Path");
		file.downloadUrl = StringValue(entry, "DownloadUrl");
		file.sha256 = ToLower(StringValue(entry, "SHA256"));
		file.size = UnsignedValue(entry, "Size");
		const std::string pathKey = ToLower(path);
		if (!IsSafeContentPath(path) || HttpsOrigin(file.downloadUrl).empty() || !IsSha256(file.sha256) ||
			file.size == 0 || file.size > kMaximumModificationBytes - totalSize || !paths.insert(pathKey).second) {
			return false;
		}
		totalSize += file.size;
		file.path = fs::path(path);
		files.push_back(std::move(file));
	}
	return !files.empty();
}

std::string SelectorId(const std::string &selector)
{
	const size_t separator = selector.rfind('@');
	return separator == std::string::npos ? selector : selector.substr(0, separator);
}

bool ParseCatalogSelectors(const YAML::Node &item, const char *key, const std::string &sourceId,
	std::vector<std::string> &selectors)
{
	const YAML::Node values = item[key];
	if (!values) return true;
	if (!values.IsSequence() || values.size() > 64) return false;
	std::unordered_set<std::string> seen;
	for (const YAML::Node &value : values) {
		if (!value.IsScalar()) return false;
		const std::string raw = ToLower(value.as<std::string>(""));
		const size_t separator = raw.rfind('@');
		const std::string id = separator == std::string::npos ? raw : raw.substr(0, separator);
		const std::string version = separator == std::string::npos ? std::string{} : raw.substr(separator + 1);
		if (!IsSafeRepositoryId(id) || (separator != std::string::npos && !IsSafeSelectorVersion(version))) return false;
		const std::string qualified = sourceId + ":" + id +
			(separator == std::string::npos ? std::string{} : "@" + version);
		if (!seen.insert(qualified).second) return false;
		selectors.push_back(qualified);
	}
	return true;
}

std::string HttpsOrigin(const std::string &url)
{
	if (url.rfind("https://", 0) != 0) return {};
	const size_t authorityEnd = url.find_first_of("/?#", 8);
	const std::string authority = url.substr(8, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - 8);
	if (authority.empty() || authority.find('@') != std::string::npos || authority.find('\\') != std::string::npos) return {};
	return "https://" + ToLower(authority);
}

bool IsOptionalHttpsUrl(const std::string &url)
{
	return url.empty() || !HttpsOrigin(url).empty();
}

void AppendDescriptorList(RepositoryIndex &index, const YAML::Node &links, const std::string &sourceId,
	const std::string &engine, ModificationType type, const std::string &parentName)
{
	if (!links || !links.IsSequence()) return;
	for (const YAML::Node &linkNode : links) {
		if (!linkNode.IsScalar()) continue;
		const std::string url = linkNode.as<std::string>("");
		if (url.rfind("https://", 0) != 0) {
			index.warnings.push_back("Rejected non-HTTPS repository descriptor: " + url);
			continue;
		}
		index.descriptors.push_back({sourceId, engine, type, parentName, {}, url});
	}
}

std::string ReadTextFile(const fs::path &path)
{
	std::error_code error;
	const uintmax_t size = fs::file_size(path, error);
	if (error || size > kMaximumCatalogBytes) return {};
	std::ifstream input(path, std::ios::binary);
	if (!input) return {};
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool IsFresh(const fs::path &path)
{
	std::error_code error;
	const auto writeTime = fs::last_write_time(path, error);
	if (error) return false;
	return fs::file_time_type::clock::now() - writeTime < kCacheLifetime;
}

size_t WriteBody(char *data, size_t size, size_t count, void *userData)
{
	auto *body = static_cast<std::string *>(userData);
	const size_t bytes = size * count;
	if (bytes > kMaximumCatalogBytes - std::min(body->size(), kMaximumCatalogBytes)) return 0;
	body->append(data, bytes);
	return bytes;
}

int TransferProgress(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *cancelRequested = static_cast<const std::atomic_bool *>(userData);
	return cancelRequested && cancelRequested->load(std::memory_order_relaxed) ? 1 : 0;
}

bool DownloadHttps(const std::string &url, std::string &body, std::string &errorMessage,
	const std::atomic_bool *cancelRequested)
{
	if (url.rfind("https://", 0) != 0) {
		errorMessage = "Catalog URL is not HTTPS";
		return false;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		errorMessage = "Cannot initialize HTTPS transport";
		return false;
	}
	body.clear();
	char curlError[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Generals-Arsenal/1 RepositoryCatalogV1");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, TransferProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelRequested);
	const CURLcode result = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	curl_easy_cleanup(curl);
	if (result != CURLE_OK || responseCode < 200 || responseCode >= 300) {
		errorMessage = curlError[0] ? curlError : curl_easy_strerror(result);
		body.clear();
		return false;
	}
	return true;
}

bool PublishText(const fs::path &target, const std::string &body, std::string &errorMessage)
{
	std::error_code error;
	fs::create_directories(target.parent_path(), error);
	if (error) {
		errorMessage = error.message();
		return false;
	}
	const fs::path temporary = target.string() + ".downloading";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) {
		errorMessage = "Cannot create repository cache file";
		return false;
	}
	output.write(body.data(), static_cast<std::streamsize>(body.size()));
	output.flush();
	if (!output) {
		output.close();
		fs::remove(temporary, error);
		errorMessage = "Cannot finish repository cache file";
		return false;
	}
	output.close();
	fs::rename(temporary, target, error);
	if (error) {
		fs::remove(target, error);
		error.clear();
		fs::rename(temporary, target, error);
	}
	if (error) {
		fs::remove(temporary, error);
		errorMessage = "Cannot publish repository cache: " + error.message();
		return false;
	}
	return true;
}

RepositoryCatalog LoadSourceCache(const fs::path &sourceRoot, const RepositorySource &source)
{
	RepositoryCatalog result;
	const std::string indexText = ReadTextFile(sourceRoot / "index.yaml");
	if (indexText.empty()) return result;
	result.usedCachedData = true;
	if (source.format == RepositoryFormat::ArsenalV1) {
		result = ParseArsenalRepositoryCatalogV1(indexText, source.id, source.indexUrl);
		result.usedCachedData = true;
		for (RepositoryModification &modification : result.modifications) {
			modification.descriptorUrl = source.indexUrl;
			const fs::path coverPath = sourceRoot / "covers" / (HexDigest(modification.coverUrl) + ".image");
			if (!modification.coverUrl.empty() && fs::is_regular_file(coverPath)) modification.coverCachePath = coverPath;
		}
		return result;
	}
	RepositoryIndex index = ParseGenLauncherRepositoryIndex(indexText, source.id, source.engine);
	result.warnings.insert(result.warnings.end(), index.warnings.begin(), index.warnings.end());
	for (const RepositoryDescriptor &descriptor : index.descriptors) {
		const fs::path descriptorPath = sourceRoot / "items" / (HexDigest(descriptor.url) + ".yaml");
		const std::string itemText = ReadTextFile(descriptorPath);
		if (itemText.empty()) continue;
		RepositoryModification modification;
		std::string parseError;
		if (ParseGenLauncherModification(itemText, descriptor, modification, parseError)) {
			const fs::path coverPath = sourceRoot / "covers" / (HexDigest(modification.coverUrl) + ".image");
			if (!modification.coverUrl.empty() && fs::is_regular_file(coverPath)) modification.coverCachePath = coverPath;
			result.modifications.push_back(std::move(modification));
		} else {
			result.warnings.push_back(parseError);
		}
	}
	return result;
}

std::vector<RepositorySource> AllRepositorySources(const fs::path &modsRoot, std::vector<std::string> &warnings)
{
	// GeneralsArsenal @feature Codex 14/08/2026 Use one centrally administered source in normal launcher sessions.
	if (fs::is_regular_file(modsRoot / "Repositories.ini")) {
		warnings.push_back("Repositories.ini is ignored; Arsenal uses its centralized repository");
	}
	return BuiltInRepositorySources();
}

void MergeCatalog(RepositoryCatalog &target, RepositoryCatalog source)
{
	for (RepositoryModification &modification : source.modifications) {
		const bool duplicate = std::any_of(target.modifications.begin(), target.modifications.end(), [&](const auto &existing) {
			return existing.id == modification.id && existing.version == modification.version && existing.engine == modification.engine;
		});
		if (!duplicate) target.modifications.push_back(std::move(modification));
	}
	target.warnings.insert(target.warnings.end(), source.warnings.begin(), source.warnings.end());
	target.usedCachedData = target.usedCachedData || source.usedCachedData;
}

} // namespace

std::vector<RepositorySource> BuiltInRepositorySources()
{
	std::string catalogUrl = GeneralsArsenalBrand::kRepositoryCatalogUrl;
	if (const char *overrideUrl = std::getenv("GENERALS_ARSENAL_REPOSITORY_URL");
		overrideUrl && !HttpsOrigin(overrideUrl).empty()) {
		catalogUrl = overrideUrl;
	}
	return {
		{"arsenal", {}, std::move(catalogUrl), RepositoryFormat::ArsenalV1}
	};
}

std::vector<RepositorySource> LoadConfiguredRepositorySources(const fs::path &modsRoot,
	std::vector<std::string> &warnings)
{
	std::vector<RepositorySource> result;
	std::ifstream input(modsRoot / "Repositories.ini");
	if (!input) return result;
	RepositorySource current;
	bool inSource = false;
	auto publish = [&]() {
		if (!inSource) return;
		current.id = ToLower(current.id);
		current.engine = ToLower(current.engine);
		const bool safeId = !current.id.empty() && std::all_of(current.id.begin(), current.id.end(), [](unsigned char character) {
			return std::isalnum(character) || character == '-';
		});
		if (!safeId || current.indexUrl.rfind("https://", 0) != 0 ||
			(current.engine != "generals" && current.engine != "zerohour" && !current.engine.empty())) {
			warnings.push_back("Rejected malformed repository source: " + current.id);
		} else {
			result.push_back(current);
		}
		current = {};
		current.format = RepositoryFormat::ArsenalV1;
	};
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos || line[first] == ';' || line[first] == '#') continue;
		if (line[first] == '[') {
			publish();
			inSource = line.compare(first, 7, "[Source") == 0;
			continue;
		}
		if (!inSource) continue;
		const size_t separator = line.find('=', first);
		if (separator == std::string::npos) continue;
		const std::string key = ToLower(line.substr(first, separator - first));
		const size_t valueFirst = line.find_first_not_of(" \t", separator + 1);
		const std::string value = valueFirst == std::string::npos ? std::string{} : line.substr(valueFirst);
		if (key == "id") current.id = value;
		else if (key == "engine") current.engine = value;
		else if (key == "url") current.indexUrl = value;
		else if (key == "format") current.format = ToLower(value) == "genlauncher" ?
			RepositoryFormat::GenLauncher : RepositoryFormat::ArsenalV1;
	}
	publish();
	return result;
}

RepositoryIndex ParseGenLauncherRepositoryIndex(const std::string &yamlText,
	const std::string &sourceId, const std::string &engine)
{
	RepositoryIndex result;
	try {
		const YAML::Node root = YAML::Load(yamlText);
		const YAML::Node mods = root["modDatas"];
		if (!mods || !mods.IsSequence()) {
			result.warnings.push_back("GenLauncher repository has no modDatas sequence");
			return result;
		}
		for (const YAML::Node &mod : mods) {
			const std::string name = StringValue(mod, "ModName");
			const std::string url = StringValue(mod, "ModLink");
			if (name.empty() || url.rfind("https://", 0) != 0) {
				result.warnings.push_back("Rejected malformed GenLauncher mod descriptor");
				continue;
			}
			result.descriptors.push_back({sourceId, engine, ModificationType::Mod, {}, name, url});
			AppendDescriptorList(result, mod["ModPatches"], sourceId, engine, ModificationType::Patch, name);
			AppendDescriptorList(result, mod["ModAddons"], sourceId, engine, ModificationType::Addon, name);
		}
		AppendDescriptorList(result, root["globalAddonsData"], sourceId, engine, ModificationType::Addon, {});
		AppendDescriptorList(result, root["originalGameAddons"], sourceId, engine, ModificationType::Addon, "Original Game");
		AppendDescriptorList(result, root["originalGamePatches"], sourceId, engine, ModificationType::Patch, "Original Game");
	} catch (const YAML::Exception &exception) {
		result.warnings.push_back(std::string("Cannot parse GenLauncher repository: ") + exception.what());
	}
	return result;
}

bool ParseGenLauncherModification(const std::string &yamlText, const RepositoryDescriptor &descriptor,
	RepositoryModification &modification, std::string &errorMessage)
{
	errorMessage.clear();
	try {
		const YAML::Node root = YAML::Load(yamlText);
		modification.sourceId = descriptor.sourceId;
		modification.engine = descriptor.engine;
		modification.type = descriptor.type;
		ModificationType declaredType;
		if (!ParseModificationType(StringValue(root, "ModificationType"), declaredType) || declaredType != descriptor.type) {
			errorMessage = "Repository descriptor type does not match its parent index: " + descriptor.url;
			return false;
		}
		modification.name = StringValue(root, "Name");
		if (modification.name.empty()) modification.name = descriptor.fallbackName;
		modification.version = StringValue(root, "Version");
		if (modification.name.empty() || modification.version.empty()) {
			errorMessage = "Repository descriptor is missing Name or Version: " + descriptor.url;
			return false;
		}
		modification.id = descriptor.sourceId + ":" + Slugify(modification.name, descriptor.url);
		if (!descriptor.parentName.empty() && descriptor.parentName != "Original Game") {
			modification.parentId = descriptor.sourceId + ":" + Slugify(descriptor.parentName, descriptor.parentName);
		}
		modification.descriptorUrl = descriptor.url;
		modification.downloadUrl = StringValue(root, "SimpleDownloadLink");
		modification.coverUrl = StringValue(root, "UIImageSourceLink");
		modification.modDbUrl = StringValue(root, "ModDBLink");
		modification.discordUrl = StringValue(root, "DiscordLink");
		modification.newsUrl = StringValue(root, "NewsLink");
		modification.supportUrl = StringValue(root, "SupportLink");
		modification.sha256 = ToLower(StringValue(root, "SHA256"));
		modification.size = UnsignedValue(root, "Size");
		modification.s3Host = StringValue(root, "S3HostLink");
		modification.s3Bucket = StringValue(root, "S3BucketName");
		modification.s3Folder = StringValue(root, "S3FolderName");
		return true;
	} catch (const YAML::Exception &exception) {
		errorMessage = std::string("Cannot parse repository descriptor: ") + exception.what();
		return false;
	}
}

RepositoryCatalog ParseArsenalRepositoryCatalogV1(const std::string &yamlText,
	const std::string &fallbackSourceId, const std::string &catalogUrl)
{
	RepositoryCatalog result;
	try {
		const YAML::Node root = YAML::Load(yamlText);
		if (root["SchemaVersion"].as<int>(0) != 1) {
			result.warnings.push_back("Unsupported RepositoryCatalog schema");
			return result;
		}
		const std::string sourceId = StringValue(root, "SourceId").empty() ? ToLower(fallbackSourceId) :
			ToLower(StringValue(root, "SourceId"));
		if (!IsSafeRepositoryId(sourceId)) {
			result.warnings.push_back("Rejected unsafe Arsenal repository source ID");
			return result;
		}
		const std::string catalogOrigin = HttpsOrigin(catalogUrl);
		const YAML::Node items = root["Items"];
		if (!items || !items.IsSequence()) return result;
		std::unordered_set<std::string> seenItems;
		for (const YAML::Node &item : items) {
			RepositoryModification modification;
			modification.sourceId = sourceId;
			modification.engine = ToLower(StringValue(item, "Engine"));
			modification.name = StringValue(item, "Name");
			modification.version = StringValue(item, "Version");
			if (!ParseModificationType(StringValue(item, "Type"), modification.type) ||
				(modification.engine != "generals" && modification.engine != "zerohour") ||
				modification.name.empty() || modification.version.empty()) {
				result.warnings.push_back("Rejected malformed Arsenal catalog item");
				continue;
			}
			const std::string explicitId = ToLower(StringValue(item, "Id"));
			const std::string itemId = explicitId.empty() ? Slugify(modification.name, modification.name) : explicitId;
			if (!IsSafeRepositoryId(itemId)) {
				result.warnings.push_back("Rejected Arsenal catalog item with unsafe ID");
				continue;
			}
			modification.id = sourceId + ":" + itemId;
			const std::string parent = ToLower(StringValue(item, "ParentId"));
			if (!parent.empty() && !IsSafeRepositoryId(parent)) {
				result.warnings.push_back("Rejected Arsenal catalog item with unsafe parent ID");
				continue;
			}
			if (!parent.empty()) modification.parentId = sourceId + ":" + parent;
			// GeneralsArsenal @feature Codex 15/08/2026 Preserve exact requirements and conflicts for real multi-branch ecosystems such as ROTR.
			if (!ParseCatalogSelectors(item, "Requires", sourceId, modification.requirements) ||
				!ParseCatalogSelectors(item, "Conflicts", sourceId, modification.conflicts)) {
				result.warnings.push_back("Rejected Arsenal catalog item with malformed compatibility selectors");
				continue;
			}
			modification.downloadUrl = StringValue(item, "DownloadUrl");
			modification.coverUrl = StringValue(item, "CoverUrl");
			modification.modDbUrl = StringValue(item, "ModDBLink");
			modification.discordUrl = StringValue(item, "DiscordLink");
			modification.newsUrl = StringValue(item, "NewsLink");
			modification.supportUrl = StringValue(item, "SupportLink");
			modification.sha256 = ToLower(StringValue(item, "SHA256"));
			modification.size = UnsignedValue(item, "Size");
			modification.generator = ToLower(StringValue(item, "Generator"));
			if (!ParseContentFiles(item, modification.contentFiles)) {
				result.warnings.push_back("Rejected Arsenal catalog item with malformed content files");
				continue;
			}
			const std::string downloadOrigin = HttpsOrigin(modification.downloadUrl);
			const std::string coverOrigin = HttpsOrigin(modification.coverUrl);
			const bool hasSinglePackage = !modification.downloadUrl.empty() || !modification.sha256.empty() || modification.size != 0;
			const bool hasContentFiles = !modification.contentFiles.empty();
			const bool hasGenerator = modification.generator == "retail-russian-merge-v1";
			const bool validPackage = hasGenerator ? (!hasSinglePackage && !hasContentFiles) :
				(hasContentFiles ? !hasSinglePackage :
					(!downloadOrigin.empty() && IsSha256(modification.sha256) && modification.size != 0 &&
						modification.size <= kMaximumModificationBytes));
			if (!validPackage ||
				(!modification.coverUrl.empty() && (coverOrigin.empty() || (!catalogOrigin.empty() && coverOrigin != catalogOrigin))) ||
				!IsOptionalHttpsUrl(modification.modDbUrl) || !IsOptionalHttpsUrl(modification.discordUrl) ||
				!IsOptionalHttpsUrl(modification.newsUrl) || !IsOptionalHttpsUrl(modification.supportUrl)) {
				result.warnings.push_back("Rejected Arsenal catalog item without HTTPS, SHA-256, or size");
				continue;
			}
			const std::string uniqueKey = modification.engine + "\n" + modification.id + "\n" + modification.version;
			if (!seenItems.insert(uniqueKey).second) {
				result.warnings.push_back("Rejected duplicate Arsenal catalog item: " + modification.id);
				continue;
			}
			result.modifications.push_back(std::move(modification));
		}
		bool removed = false;
		do {
			removed = false;
			std::unordered_set<std::string> knownIds;
			std::unordered_set<std::string> knownSelections;
			for (const RepositoryModification &modification : result.modifications) {
				knownIds.insert(modification.engine + "\n" + modification.id);
				knownSelections.insert(modification.engine + "\n" + modification.id + "@" + ToLower(modification.version));
			}
			result.modifications.erase(std::remove_if(result.modifications.begin(), result.modifications.end(),
				[&](const RepositoryModification &modification) {
					auto knownSelector = [&](const std::string &selector) {
						const std::string exact = modification.engine + "\n" + selector;
						return selector.find('@') == std::string::npos ?
							knownIds.count(exact) != 0 : knownSelections.count(exact) != 0;
					};
					const bool selfReference = std::any_of(modification.requirements.begin(), modification.requirements.end(),
						[&](const std::string &selector) { return SelectorId(selector) == modification.id; }) ||
						std::any_of(modification.conflicts.begin(), modification.conflicts.end(),
							[&](const std::string &selector) { return SelectorId(selector) == modification.id; });
					const bool unknownSelector = !std::all_of(modification.requirements.begin(), modification.requirements.end(), knownSelector) ||
						!std::all_of(modification.conflicts.begin(), modification.conflicts.end(), knownSelector);
					const bool unknownParent = !modification.parentId.empty() &&
						knownIds.count(modification.engine + "\n" + modification.parentId) == 0;
					if (!selfReference && !unknownSelector && !unknownParent) return false;
					result.warnings.push_back("Rejected Arsenal catalog item with invalid compatibility graph: " + modification.id);
					removed = true;
					return true;
				}), result.modifications.end());
		} while (removed);
	} catch (const YAML::Exception &exception) {
		result.warnings.push_back(std::string("Cannot parse Arsenal repository: ") + exception.what());
	}
	return result;
}

RepositoryCatalog LoadCachedRepositoryCatalog(const fs::path &cacheRoot)
{
	RepositoryCatalog result;
	std::vector<std::string> warnings;
	for (const RepositorySource &source : AllRepositorySources(cacheRoot.parent_path(), warnings)) {
		MergeCatalog(result, LoadSourceCache(cacheRoot / "Repositories" / source.id, source));
	}
	result.warnings.insert(result.warnings.end(), warnings.begin(), warnings.end());
	return result;
}

RepositoryCatalog RefreshRepositoryCatalog(const fs::path &cacheRoot, bool forceRefresh,
	const std::atomic_bool *cancelRequested)
{
	RepositoryCatalog result;
	EnsureCurlInitialized();
	std::vector<std::string> sourceWarnings;
	const std::vector<RepositorySource> sources = AllRepositorySources(cacheRoot.parent_path(), sourceWarnings);
	result.warnings.insert(result.warnings.end(), sourceWarnings.begin(), sourceWarnings.end());
	for (const RepositorySource &source : sources) {
		if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) break;
		const fs::path sourceRoot = cacheRoot / "Repositories" / source.id;
		const fs::path indexPath = sourceRoot / "index.yaml";
		if (forceRefresh || !IsFresh(indexPath)) {
			std::string body;
			std::string downloadError;
			if (DownloadHttps(source.indexUrl, body, downloadError, cancelRequested)) {
				if (!PublishText(indexPath, body, downloadError)) result.warnings.push_back(downloadError);
			} else {
				result.warnings.push_back("Cannot refresh " + source.id + ": " + downloadError);
			}
		}
		if (source.format == RepositoryFormat::GenLauncher) {
			const std::string indexText = ReadTextFile(indexPath);
			const RepositoryIndex index = ParseGenLauncherRepositoryIndex(indexText, source.id, source.engine);
			result.warnings.insert(result.warnings.end(), index.warnings.begin(), index.warnings.end());
			for (const RepositoryDescriptor &descriptor : index.descriptors) {
				if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) break;
				const fs::path descriptorPath = sourceRoot / "items" / (HexDigest(descriptor.url) + ".yaml");
				if (forceRefresh || !IsFresh(descriptorPath)) {
					std::string body;
					std::string downloadError;
					if (DownloadHttps(descriptor.url, body, downloadError, cancelRequested)) {
						if (!PublishText(descriptorPath, body, downloadError)) result.warnings.push_back(downloadError);
					} else if (!fs::exists(descriptorPath)) {
						result.warnings.push_back("Cannot refresh item " + descriptor.url + ": " + downloadError);
					}
				}
			}
		}
		RepositoryCatalog sourceCatalog = LoadSourceCache(sourceRoot, source);
		for (RepositoryModification &modification : sourceCatalog.modifications) {
			if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) break;
			if (modification.coverUrl.empty()) continue;
			const fs::path coverPath = sourceRoot / "covers" / (HexDigest(modification.coverUrl) + ".image");
			if (forceRefresh || !IsFresh(coverPath)) {
				std::string body;
				std::string downloadError;
				if (DownloadHttps(modification.coverUrl, body, downloadError, cancelRequested)) {
					if (!PublishText(coverPath, body, downloadError)) result.warnings.push_back(downloadError);
				} else if (!fs::exists(coverPath)) {
					result.warnings.push_back("Cannot refresh cover " + modification.coverUrl + ": " + downloadError);
				}
			}
			if (fs::is_regular_file(coverPath)) modification.coverCachePath = coverPath;
		}
		MergeCatalog(result, std::move(sourceCatalog));
	}
	return result;
}

} // namespace GeneralsArsenalLauncher
