/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherMods.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <openssl/evp.h>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

constexpr uintmax_t kMaximumManifestSize = 64 * 1024;
constexpr size_t kMaximumCatalogEntries = 10000;

struct VersionToken
{
	std::string value;
	bool numeric = false;
};

std::vector<VersionToken> TokenizeVersion(const std::string &version)
{
	std::vector<VersionToken> tokens;
	for (size_t index = 0; index < version.size();) {
		const unsigned char current = static_cast<unsigned char>(version[index]);
		if (!std::isalnum(current)) {
			++index;
			continue;
		}
		const bool numeric = std::isdigit(current) != 0;
		const size_t begin = index;
		while (index < version.size()) {
			const unsigned char character = static_cast<unsigned char>(version[index]);
			if (!std::isalnum(character) || (std::isdigit(character) != 0) != numeric) break;
			++index;
		}
		std::string value = version.substr(begin, index - begin);
		if (numeric) {
			const size_t significant = value.find_first_not_of('0');
			value = significant == std::string::npos ? "0" : value.substr(significant);
		} else {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
		}
		tokens.push_back({std::move(value), numeric});
	}
	return tokens;
}

int CompareVersionToken(const VersionToken &left, const VersionToken &right)
{
	if (left.numeric != right.numeric) return left.numeric ? 1 : -1;
	if (left.numeric && left.value.size() != right.value.size()) {
		return left.value.size() < right.value.size() ? -1 : 1;
	}
	if (left.value == right.value) return 0;
	return left.value < right.value ? -1 : 1;
}

int CompareVersionTail(const std::vector<VersionToken> &tokens, size_t begin)
{
	for (size_t index = begin; index < tokens.size(); ++index) {
		if (tokens[index].numeric) {
			if (tokens[index].value != "0") return 1;
		} else {
			return -1;
		}
	}
	return 0;
}

std::string Trim(const std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool IsSafeSlug(const std::string &value)
{
	return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isalnum(character) || character == '.' || character == '_' || character == '-';
	});
}

bool IsSafeVersion(const std::string &value)
{
	return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isalnum(character) || character == '.' || character == '_' || character == '-' ||
			character == '+' || character == '~';
	});
}

bool IsSafeNormalizedId(const std::string &value)
{
	const size_t separator = value.find(':');
	return separator != std::string::npos && value.find(':', separator + 1) == std::string::npos &&
		IsSafeSlug(value.substr(0, separator)) && IsSafeSlug(value.substr(separator + 1));
}

bool SplitModificationSelector(const std::string &selector, std::string &id, std::string &version)
{
	const std::string normalized = ToLower(Trim(selector));
	const size_t separator = normalized.rfind('@');
	id = separator == std::string::npos ? normalized : normalized.substr(0, separator);
	version = separator == std::string::npos ? std::string{} : normalized.substr(separator + 1);
	return IsSafeNormalizedId(id) && (version.empty() || IsSafeVersion(version)) &&
		(separator == std::string::npos || !version.empty());
}

bool ParseSelectorList(const std::string &value, std::vector<std::string> &selectors)
{
	if (value.empty()) return true;
	std::unordered_set<std::string> seen;
	std::istringstream input(value);
	std::string selector;
	while (std::getline(input, selector, ',')) {
		std::string id;
		std::string version;
		if (!SplitModificationSelector(selector, id, version)) return false;
		const std::string normalized = id + (version.empty() ? std::string{} : "@" + version);
		if (!seen.insert(normalized).second || selectors.size() >= 64) return false;
		selectors.push_back(normalized);
	}
	return true;
}

bool IsHexDigest(const std::string &value)
{
	return value.empty() || (value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return std::isxdigit(character);
	}));
}

bool IsInsideRoot(const fs::path &candidate, const fs::path &root)
{
	auto candidatePart = candidate.begin();
	for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++candidatePart) {
		if (candidatePart == candidate.end() || *candidatePart != *rootPart) return false;
	}
	return true;
}

bool IsTransientPath(const fs::path &path, const fs::path &modsRoot)
{
	std::error_code error;
	const fs::path relative = fs::relative(path, modsRoot, error);
	if (error) return true;
	for (const fs::path &part : relative) {
		const std::string name = ToLower(part.string());
		if (name == ".staging" || name == ".trash" || name == "cache") return true;
	}
	return false;
}

bool ReadValues(const fs::path &manifestPath, std::unordered_map<std::string, std::string> &values,
	std::string &warning)
{
	std::error_code fileError;
	const uintmax_t manifestSize = fs::file_size(manifestPath, fileError);
	if (fileError || manifestSize > kMaximumManifestSize) {
		warning = "Modification manifest is unreadable or too large: " + manifestPath.string();
		return false;
	}
	std::ifstream input(manifestPath);
	if (!input) {
		warning = "Cannot read modification manifest: " + manifestPath.string();
		return false;
	}
	std::string section;
	std::string line;
	while (std::getline(input, line)) {
		if (line.size() > 4096) {
			warning = "Modification manifest contains an oversized line: " + manifestPath.string();
			return false;
		}
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line.front() == '[' && line.back() == ']') {
			section = ToLower(Trim(line.substr(1, line.size() - 2)));
			continue;
		}
		if (section != "modification") continue;
		const size_t equals = line.find('=');
		if (equals == std::string::npos) continue;
		const std::string key = ToLower(Trim(line.substr(0, equals)));
		if (values.find(key) == values.end()) values.emplace(key, Trim(line.substr(equals + 1)));
	}
	return true;
}

std::string Value(const std::unordered_map<std::string, std::string> &values, const char *key)
{
	const auto found = values.find(key);
	return found == values.end() ? std::string{} : found->second;
}

bool ParseType(const std::string &value, ModificationType &type)
{
	const std::string normalized = ToLower(value);
	if (normalized == "mod") type = ModificationType::Mod;
	else if (normalized == "patch") type = ModificationType::Patch;
	else if (normalized == "addon") type = ModificationType::Addon;
	else return false;
	return true;
}

bool ResolveOptionalPath(const fs::path &manifestDirectory, const std::string &relativeValue,
	fs::path &result, bool mustExist)
{
	if (relativeValue.empty()) return true;
	const fs::path relative(relativeValue);
	if (relative.is_absolute()) return false;
	std::error_code error;
	const fs::path candidate = fs::weakly_canonical(manifestDirectory / relative, error);
	if (error || !IsInsideRoot(candidate, manifestDirectory)) return false;
	if (mustExist && (!fs::exists(candidate, error) || error)) return false;
	result = candidate;
	return true;
}

// Echelon @feature Codex 07/09/2026 Load the immutable file list emitted beside imported content manifests.
bool ReadContentFileRecords(const fs::path &versionRoot, const fs::path &contentRoot,
	std::vector<ContentFileRecord> &files, std::string &warning)
{
	const fs::path indexPath = versionRoot / "files.sha256";
	std::error_code error;
	if (!fs::exists(indexPath, error)) {
		if (error) warning = "Modification file index is unreadable: " + indexPath.string();
		return !error;
	}
	if (fs::is_symlink(indexPath, error) || error || !fs::is_regular_file(indexPath, error)) {
		warning = "Modification file index is unreadable: " + indexPath.string();
		return false;
	}
	std::ifstream input(indexPath);
	if (!input) {
		warning = "Cannot read modification file index: " + indexPath.string();
		return false;
	}
	std::string line;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		if (line.size() > 4096 || files.size() >= kMaximumCatalogEntries) {
			warning = "Modification file index is too large: " + indexPath.string();
			return false;
		}
		const size_t separator = line.find("  ");
		if (separator == std::string::npos) {
			warning = "Modification file index has an invalid entry: " + indexPath.string();
			return false;
		}
		const std::string digest = ToLower(Trim(line.substr(0, separator)));
		const std::string relativeText = Trim(line.substr(separator + 2));
		const fs::path relative(relativeText);
		if (digest.size() != 64 || !IsHexDigest(digest) || relative.empty() || relative.is_absolute() ||
			relative.has_root_name() || relative.has_root_directory() || relative.lexically_normal() != relative) {
			warning = "Modification file index contains an unsafe entry: " + indexPath.string();
			return false;
		}
		const fs::path candidate = fs::weakly_canonical(contentRoot / relative, error);
		if (error || !IsInsideRoot(candidate, contentRoot) || !fs::is_regular_file(candidate, error) || error) {
			warning = "Modification file index points outside content: " + indexPath.string();
			return false;
		}
		const uintmax_t size = fs::file_size(candidate, error);
		if (error) {
			warning = "Cannot read modification file size: " + candidate.string();
			return false;
		}
		files.push_back({relative.generic_string(), static_cast<uint64_t>(size), digest});
	}
	return true;
}

bool PublishMigratedManifest(const fs::path &legacyPath,
	const std::unordered_map<std::string, std::string> &values, fs::path &publishedPath, std::string &warning)
{
	publishedPath = legacyPath.parent_path() / "manifest.ini";
	std::error_code error;
	if (fs::exists(publishedPath, error)) return !error;
	const std::string legacyId = ToLower(Value(values, "id"));
	if (!IsSafeSlug(legacyId)) return false;
	const fs::path temporary = publishedPath.string() + ".migrating";
	std::ofstream output(temporary, std::ios::trunc);
	if (!output) {
		warning = "Cannot create migrated modification manifest: " + temporary.string();
		return false;
	}
	// Echelon @feature Codex 14/08/2026 Publish V1 metadata atomically while leaving the legacy file recoverable.
	output << "[Modification]\nSchemaVersion=2\n"
		<< "Id=local:" << legacyId << "\nSourceId=local\n"
		<< "Name=" << Value(values, "name") << "\nVersion=" << Value(values, "version")
		<< "\nEngine=" << ToLower(Value(values, "engine")) << "\nType=mod\nParentId=\nSource=local-import\n"
		<< "RootPath=" << Value(values, "launchpath") << "\nCoverImage=\nSHA256=\nContentFingerprint=\n";
	output.flush();
	if (!output) {
		output.close();
		fs::remove(temporary, error);
		warning = "Cannot finish migrated modification manifest: " + temporary.string();
		return false;
	}
	output.close();
	fs::rename(temporary, publishedPath, error);
	if (error) {
		fs::remove(temporary, error);
		warning = "Cannot publish migrated modification manifest: " + error.message();
		return false;
	}
	return true;
}

bool ReadManifest(const fs::path &inputPath, const fs::path &canonicalRoot,
	InstalledModification &modification, std::string &warning)
{
	std::error_code fileError;
	const fs::path canonicalManifestDirectory = fs::weakly_canonical(inputPath.parent_path(), fileError);
	if (fileError || !IsInsideRoot(canonicalManifestDirectory, canonicalRoot)) {
		warning = "Modification manifest escapes Mods: " + inputPath.string();
		return false;
	}
	std::unordered_map<std::string, std::string> values;
	if (!ReadValues(inputPath, values, warning)) return false;
	fs::path manifestPath = inputPath;
	const std::string schemaVersion = Value(values, "schemaversion");
	if (schemaVersion == "1") {
		fs::path migratedPath;
		if (!PublishMigratedManifest(inputPath, values, migratedPath, warning)) return false;
		values.clear();
		if (!ReadValues(migratedPath, values, warning)) return false;
		manifestPath = migratedPath;
		modification.legacyManifest = true;
	} else if (schemaVersion != "2") {
		warning = "Unsupported modification schema: " + inputPath.string();
		return false;
	}

	modification.id = ToLower(Value(values, "id"));
	modification.sourceId = ToLower(Value(values, "sourceid"));
	modification.name = Value(values, "name");
	modification.version = Value(values, "version");
	modification.engine = ToLower(Value(values, "engine"));
	modification.parentId = ToLower(Value(values, "parentid"));
	if (!ParseSelectorList(Value(values, "requirements"), modification.requirements) ||
		!ParseSelectorList(Value(values, "conflicts"), modification.conflicts)) {
		warning = "Invalid modification compatibility metadata: " + manifestPath.string();
		return false;
	}
	modification.source = Value(values, "source");
	modification.sha256 = ToLower(Value(values, "sha256"));
	modification.contentFingerprint = ToLower(Value(values, "contentfingerprint"));
	if (!ParseType(Value(values, "type"), modification.type) || !IsSafeNormalizedId(modification.id) ||
		!IsSafeSlug(modification.sourceId) || modification.id.substr(0, modification.id.find(':')) != modification.sourceId ||
		modification.name.empty() || modification.name.size() > 512 || !IsSafeVersion(modification.version) ||
		(modification.engine != "generals" && modification.engine != "zerohour") ||
		(!modification.parentId.empty() && !IsSafeNormalizedId(modification.parentId)) ||
		std::any_of(modification.requirements.begin(), modification.requirements.end(),
			[&](const std::string &selector) { return selector.substr(0, selector.find('@')) == modification.id; }) ||
		std::any_of(modification.conflicts.begin(), modification.conflicts.end(),
			[&](const std::string &selector) { return selector.substr(0, selector.find('@')) == modification.id; }) ||
		!IsHexDigest(modification.sha256) || !IsHexDigest(modification.contentFingerprint)) {
		warning = "Invalid modification manifest: " + manifestPath.string();
		return false;
	}
	const std::string rootValue = Value(values, "rootpath");
	if (rootValue.empty() || !ResolveOptionalPath(canonicalManifestDirectory, rootValue, modification.launchPath, true)) {
		warning = "Modification content root is missing or escapes its version directory: " + manifestPath.string();
		return false;
	}
	if (!ReadContentFileRecords(manifestPath.parent_path(), modification.launchPath, modification.files, warning)) return false;
	if (!ResolveOptionalPath(canonicalManifestDirectory, Value(values, "coverimage"), modification.coverImagePath, true)) {
		warning = "Modification cover image escapes its version directory: " + manifestPath.string();
		return false;
	}
	modification.manifestPath = manifestPath;
	return true;
}

} // namespace

// Echelon @feature Codex 14/08/2026 Compare numeric version components naturally while keeping release builds newer than pre-releases.
int CompareModificationVersions(const std::string &left, const std::string &right)
{
	const std::vector<VersionToken> leftTokens = TokenizeVersion(left);
	const std::vector<VersionToken> rightTokens = TokenizeVersion(right);
	const size_t common = std::min(leftTokens.size(), rightTokens.size());
	for (size_t index = 0; index < common; ++index) {
		const int comparison = CompareVersionToken(leftTokens[index], rightTokens[index]);
		if (comparison != 0) return comparison;
	}
	if (leftTokens.size() == rightTokens.size()) return 0;
	if (leftTokens.size() > common) return CompareVersionTail(leftTokens, common);
	return -CompareVersionTail(rightTokens, common);
}

bool ModificationSelectorMatches(const std::string &selector, const InstalledModification &modification)
{
	std::string id;
	std::string version;
	if (!SplitModificationSelector(selector, id, version) || id != ToLower(modification.id)) return false;
	return version.empty() || version == ToLower(modification.version);
}

ModificationCatalog LoadModificationCatalog(const fs::path &modsRoot)
{
	ModificationCatalog catalog;
	std::error_code error;
	fs::create_directories(modsRoot / "Installed", error);
	if (!error) fs::create_directories(modsRoot / ".staging", error);
	if (!error) fs::create_directories(modsRoot / ".trash", error);
	if (!error) fs::create_directories(modsRoot / "Cache", error);
	if (error) {
		catalog.warnings.push_back("Cannot create modification directories: " + error.message());
		return catalog;
	}
	const fs::path canonicalRoot = fs::weakly_canonical(modsRoot, error);
	if (error) {
		catalog.warnings.push_back("Cannot resolve modifications directory: " + error.message());
		return catalog;
	}
	size_t visitedEntries = 0;
	for (fs::recursive_directory_iterator iterator(modsRoot, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		if (++visitedEntries > kMaximumCatalogEntries) {
			catalog.warnings.push_back("Modification catalog entry limit reached");
			break;
		}
		if (iterator->is_directory(error) && IsTransientPath(iterator->path(), modsRoot)) {
			iterator.disable_recursion_pending();
			continue;
		}
		if (!iterator->is_regular_file(error)) continue;
		const std::string filename = ToLower(iterator->path().filename().string());
		if (filename != "manifest.ini" && filename != "mod.ini") continue;
		if (filename == "mod.ini" && fs::exists(iterator->path().parent_path() / "manifest.ini", error)) continue;
		InstalledModification modification;
		std::string warning;
		if (!ReadManifest(iterator->path(), canonicalRoot, modification, warning)) {
			catalog.warnings.push_back(std::move(warning));
			continue;
		}
		const bool duplicate = std::any_of(catalog.modifications.begin(), catalog.modifications.end(), [&](const auto &existing) {
			return existing.engine == modification.engine && existing.id == modification.id &&
				ToLower(existing.version) == ToLower(modification.version);
		});
		if (duplicate) {
			catalog.warnings.push_back("Duplicate modification version ignored: " + modification.engine + "/" +
				modification.id + " " + modification.version);
			continue;
		}
		catalog.modifications.push_back(std::move(modification));
	}
	if (error) catalog.warnings.push_back("Cannot finish scanning modifications: " + error.message());
	std::sort(catalog.modifications.begin(), catalog.modifications.end(), [](const auto &left, const auto &right) {
		if (left.engine != right.engine) return left.engine < right.engine;
		if (left.type != right.type) return left.type < right.type;
		if (left.name != right.name) return left.name < right.name;
		return CompareModificationVersions(left.version, right.version) < 0;
	});
	return catalog;
}

const InstalledModification *FindModification(const ModificationCatalog &catalog,
	const std::string &engine, const std::string &selection)
{
	if (selection.empty()) return nullptr;
	const std::string normalizedEngine = ToLower(engine);
	const std::string normalizedSelection = ToLower(selection);
	for (const InstalledModification &modification : catalog.modifications) {
		if (modification.engine != normalizedEngine) continue;
		if (ModificationSelectionKey(modification) == normalizedSelection) return &modification;
		if (modification.sourceId == "local" && modification.id.substr(6) + "@" + ToLower(modification.version) == normalizedSelection) {
			return &modification;
		}
	}
	return nullptr;
}

std::string ModificationSelectionKey(const InstalledModification &modification)
{
	return modification.id + "@" + ToLower(modification.version);
}

std::vector<const InstalledModification *> ModificationsForEngine(const ModificationCatalog &catalog,
	const std::string &engine)
{
	std::vector<const InstalledModification *> result;
	const std::string normalizedEngine = ToLower(engine);
	for (const InstalledModification &modification : catalog.modifications) {
		if (modification.engine == normalizedEngine) result.push_back(&modification);
	}
	return result;
}

std::vector<const InstalledModification *> ModificationsForEngineAndType(const ModificationCatalog &catalog,
	const std::string &engine, ModificationType type)
{
	std::vector<const InstalledModification *> result;
	const std::string normalizedEngine = ToLower(engine);
	for (const InstalledModification &modification : catalog.modifications) {
		if (modification.engine == normalizedEngine && modification.type == type) result.push_back(&modification);
	}
	return result;
}

const char *ModificationTypeName(ModificationType type)
{
	switch (type) {
		case ModificationType::Patch: return "patch";
		case ModificationType::Addon: return "addon";
		default: return "mod";
	}
}

bool ResolveModificationStack(const ModificationCatalog &catalog, const std::string &engine,
	const std::string &modSelection, const std::vector<std::string> &patchSelections,
	const std::vector<std::string> &addonSelections, ModificationStack &stack, std::string &errorMessage)
{
	stack = {};
	stack.engine = ToLower(engine);
	if (stack.engine != "generals" && stack.engine != "zerohour") {
		errorMessage = "Unknown modification engine profile";
		return false;
	}
	if (patchSelections.size() > 1) {
		errorMessage = "Only one patch can be active in a modification stack";
		return false;
	}
	auto append = [&](const std::string &selection, ModificationType expectedType) -> bool {
		if (selection.empty()) return true;
		const InstalledModification *modification = FindModification(catalog, stack.engine, selection);
		if (!modification) {
			errorMessage = "Installed modification was not found: " + selection;
			return false;
		}
		if (modification->type != expectedType) {
			errorMessage = "Modification has the wrong content type: " + selection;
			return false;
		}
		if (modification->contentFingerprint.size() != 64) {
			errorMessage = "Modification must be verified before launch: " + selection;
			return false;
		}
		if (std::find(stack.layers.begin(), stack.layers.end(), modification) != stack.layers.end()) {
			errorMessage = "Modification layer is selected more than once: " + selection;
			return false;
		}
		stack.layers.push_back(modification);
		return true;
	};
	if (!append(modSelection, ModificationType::Mod)) return false;
	if (!patchSelections.empty() && !append(patchSelections.front(), ModificationType::Patch)) return false;
	for (const std::string &selection : addonSelections) {
		if (!append(selection, ModificationType::Addon)) return false;
	}
	const InstalledModification *selectedMod = nullptr;
	const InstalledModification *selectedPatch = nullptr;
	for (const InstalledModification *layer : stack.layers) {
		if (layer->type == ModificationType::Mod) selectedMod = layer;
		else if (layer->type == ModificationType::Patch) selectedPatch = layer;
		if (layer->parentId.empty()) continue;
		const bool validParent = (selectedMod && layer->parentId == selectedMod->id) ||
			(layer->type == ModificationType::Addon && selectedPatch && layer->parentId == selectedPatch->id);
		if (!validParent) {
			errorMessage = "Modification parent is not active: " + layer->id + " requires " + layer->parentId;
			return false;
		}
	}
	// Echelon @feature Codex 15/08/2026 Reject incomplete or conflicting managed stacks before the VFS sees any content.
	for (const InstalledModification *layer : stack.layers) {
		for (const std::string &requirement : layer->requirements) {
			const bool present = std::any_of(stack.layers.begin(), stack.layers.end(), [&](const auto *candidate) {
				return ModificationSelectorMatches(requirement, *candidate);
			});
			if (!present) {
				errorMessage = "Modification requirement is not active: " + layer->id + " requires " + requirement;
				return false;
			}
		}
		for (const std::string &conflict : layer->conflicts) {
			const bool present = std::any_of(stack.layers.begin(), stack.layers.end(), [&](const auto *candidate) {
				return candidate != layer && ModificationSelectorMatches(conflict, *candidate);
			});
			if (present) {
				errorMessage = "Modification conflict is active: " + layer->id + " conflicts with " + conflict;
				return false;
			}
		}
	}
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
		if (context) EVP_MD_CTX_free(context);
		errorMessage = "Cannot initialize the modification stack fingerprint";
		return false;
	}
	auto digest = [&](const std::string &value) {
		EVP_DigestUpdate(context, value.data(), value.size());
		const unsigned char separator = 0;
		EVP_DigestUpdate(context, &separator, sizeof(separator));
	};
	digest(stack.engine);
	for (const InstalledModification *layer : stack.layers) {
		digest(ModificationTypeName(layer->type));
		digest(layer->id);
		digest(layer->version);
		for (const std::string &requirement : layer->requirements) digest("requires=" + requirement);
		for (const std::string &conflict : layer->conflicts) digest("conflicts=" + conflict);
		digest(layer->contentFingerprint.empty() ? layer->sha256 : layer->contentFingerprint);
	}
	unsigned char bytes[EVP_MAX_MD_SIZE]{};
	unsigned int byteCount = 0;
	if (EVP_DigestFinal_ex(context, bytes, &byteCount) != 1) {
		EVP_MD_CTX_free(context);
		errorMessage = "Cannot finalize the modification stack fingerprint";
		return false;
	}
	EVP_MD_CTX_free(context);
	std::ostringstream hexadecimal;
	hexadecimal << std::hex << std::setfill('0');
	for (unsigned int index = 0; index < byteCount; ++index) hexadecimal << std::setw(2) << static_cast<unsigned>(bytes[index]);
	stack.fingerprint = hexadecimal.str();
	return true;
}

} // namespace EchelonLauncher
