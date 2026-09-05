/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherIntegration/ContentLayerRuntime.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace EchelonContentRuntime
{
namespace
{

std::vector<ContentLayer> g_layers;

bool IsSafeIdentity(const char *value, size_t maximumLength)
{
	if (!value || value[0] == '\0') return false;
	const std::string text(value);
	return text.size() <= maximumLength && std::all_of(text.begin(), text.end(), [](unsigned char character) {
		return std::isalnum(character) || character == ':' || character == '.' || character == '_' ||
			character == '-' || character == '+' || character == '~';
	});
}

bool IsHexFingerprint(const char *value)
{
	if (!value) return false;
	const std::string text(value);
	return text.size() == 64 && std::all_of(text.begin(), text.end(), [](unsigned char character) {
		return std::isxdigit(character) != 0;
	});
}

bool IsInside(const fs::path &candidate, const fs::path &root)
{
	auto candidatePart = candidate.begin();
	for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++candidatePart) {
		if (candidatePart == candidate.end() || *candidatePart != *rootPart) return false;
	}
	return true;
}

bool ResolveCaseInsensitive(const fs::path &root, const fs::path &relative, fs::path &resolved)
{
	resolved = root;
	std::error_code error;
	for (const fs::path &part : relative) {
		if (part.empty() || part == ".") continue;
		if (part == "..") return false;
		const fs::path exact = resolved / part;
		if (fs::exists(exact, error) && !error) {
			resolved = exact;
			continue;
		}
		error.clear();
		bool found = false;
		const std::string wanted = part.string();
		for (fs::directory_iterator iterator(resolved, fs::directory_options::skip_permission_denied, error), end;
			!error && iterator != end; iterator.increment(error)) {
			std::string actual = iterator->path().filename().string();
			if (actual.size() != wanted.size()) continue;
			const bool equal = std::equal(actual.begin(), actual.end(), wanted.begin(), [](unsigned char left, unsigned char right) {
				return std::tolower(left) == std::tolower(right);
			});
			if (equal) {
				resolved /= iterator->path().filename();
				found = true;
				break;
			}
		}
		if (!found || error) return false;
	}
	return true;
}

} // namespace

bool Configure(const EchelonContentLayerV1 *layers, uint32_t count, std::string &errorMessage)
{
	Clear();
	if (count == 0) return true;
	if (!layers || count > ECHELON_MAX_CONTENT_LAYERS) {
		errorMessage = "Invalid managed content layer count";
		return false;
	}
	std::vector<ContentLayer> validated;
	validated.reserve(count);
	uint32_t previousPriority = 0;
	for (uint32_t index = 0; index < count; ++index) {
		const EchelonContentLayerV1 &source = layers[index];
		if (source.struct_size < sizeof(EchelonContentLayerV1) ||
			source.layer_type > ECHELON_CONTENT_LAYER_ADDON ||
			!IsSafeIdentity(source.id, 256) || !IsSafeIdentity(source.version, 128) ||
			!IsHexFingerprint(source.content_fingerprint) || !source.root_path || source.root_path[0] == '\0' ||
			source.priority == 0 || (index > 0 && source.priority <= previousPriority)) {
			errorMessage = "Invalid managed content layer metadata at index " + std::to_string(index);
			return false;
		}
		std::error_code error;
		fs::path root = fs::canonical(fs::path(source.root_path), error);
		if (error || !root.is_absolute() || !fs::is_directory(root, error) || error) {
			errorMessage = "Managed content root is unavailable: " + std::string(source.root_path);
			return false;
		}
		const bool duplicate = std::any_of(validated.begin(), validated.end(), [&](const ContentLayer &layer) {
			return layer.id == source.id && layer.version == source.version;
		});
		if (duplicate) {
			errorMessage = "Managed content layer is duplicated: " + std::string(source.id);
			return false;
		}
		validated.push_back(ContentLayer{source.layer_type, source.id, source.version, std::move(root),
			source.priority, source.content_fingerprint});
		previousPriority = source.priority;
	}
	g_layers = std::move(validated);
	return true;
}

void Clear()
{
	g_layers.clear();
	g_layers.shrink_to_fit();
}

bool IsClear()
{
	return g_layers.empty();
}

const std::vector<ContentLayer> &Layers()
{
	return g_layers;
}

bool ResolveReadPath(const char *relativePath, fs::path &resolvedPath)
{
	resolvedPath.clear();
	if (!relativePath || relativePath[0] == '\0') return false;
	std::string portable(relativePath);
	std::replace(portable.begin(), portable.end(), '\\', '/');
	const fs::path relative(portable);
	if (relative.is_absolute()) return false;
	for (auto iterator = g_layers.rbegin(); iterator != g_layers.rend(); ++iterator) {
		fs::path candidate;
		if (!ResolveCaseInsensitive(iterator->rootPath, relative, candidate)) continue;
		std::error_code error;
		candidate = fs::weakly_canonical(candidate, error);
		if (error || !IsInside(candidate, iterator->rootPath) || !fs::is_regular_file(candidate, error) || error) continue;
		resolvedPath = std::move(candidate);
		return true;
	}
	return false;
}

std::vector<fs::path> ListFiles(const char *relativeDirectory, const char *searchName, bool recursive)
{
	std::vector<fs::path> result;
	if (!relativeDirectory || !searchName) return result;
	std::string portableDirectory(relativeDirectory);
	std::replace(portableDirectory.begin(), portableDirectory.end(), '\\', '/');
	const fs::path relative(portableDirectory);
	if (relative.is_absolute()) return result;
	std::string wantedExtension = fs::path(searchName).extension().string();
	std::transform(wantedExtension.begin(), wantedExtension.end(), wantedExtension.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	std::map<std::string, fs::path> visibleFiles;
	for (const ContentLayer &layer : g_layers) {
		const fs::path directory = (layer.rootPath / relative).lexically_normal();
		if (!IsInside(directory, layer.rootPath)) continue;
		std::error_code error;
		if (!fs::is_directory(directory, error) || error) continue;
		auto accept = [&](const fs::directory_entry &entry) {
			std::error_code entryError;
			if (!entry.is_regular_file(entryError) || entryError) return;
			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			if (!wantedExtension.empty() && extension != wantedExtension) return;
			fs::path canonicalFile = fs::weakly_canonical(entry.path(), entryError);
			if (entryError || !IsInside(canonicalFile, layer.rootPath)) return;
			std::string logical = fs::relative(canonicalFile, layer.rootPath, entryError).generic_string();
			if (entryError) return;
			std::transform(logical.begin(), logical.end(), logical.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			visibleFiles[logical] = std::move(canonicalFile);
		};
		if (recursive) {
			for (fs::recursive_directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
				!error && iterator != end; iterator.increment(error)) {
				if (iterator->is_symlink(error)) {
					if (iterator->is_directory(error)) iterator.disable_recursion_pending();
					continue;
				}
				accept(*iterator);
			}
		} else {
			for (fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
				!error && iterator != end; iterator.increment(error)) {
				if (!iterator->is_symlink(error)) accept(*iterator);
			}
		}
	}
	result.reserve(visibleFiles.size());
	for (auto &[logical, physical] : visibleFiles) result.push_back(std::move(physical));
	return result;
}

} // namespace EchelonContentRuntime
