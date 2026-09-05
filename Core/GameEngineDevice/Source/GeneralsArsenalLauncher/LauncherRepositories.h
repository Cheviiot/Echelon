/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#pragma once

#include "LauncherMods.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace GeneralsArsenalLauncher
{

enum class RepositoryFormat
{
	GenLauncher,
	ArsenalV1
};

struct RepositorySource
{
	std::string id;
	std::string engine;
	std::string indexUrl;
	RepositoryFormat format = RepositoryFormat::ArsenalV1;
};

struct RepositoryDescriptor
{
	std::string sourceId;
	std::string engine;
	ModificationType type = ModificationType::Mod;
	std::string parentName;
	std::string fallbackName;
	std::string url;
};

struct RepositoryContentFile
{
	std::filesystem::path path;
	std::string downloadUrl;
	std::string sha256;
	uint64_t size = 0;
};

struct RepositoryModification
{
	std::string id;
	std::string sourceId;
	std::string engine;
	ModificationType type = ModificationType::Mod;
	std::string parentId;
	std::vector<std::string> requirements;
	std::vector<std::string> conflicts;
	std::string name;
	std::string version;
	std::string descriptorUrl;
	std::string downloadUrl;
	std::string coverUrl;
	std::filesystem::path coverCachePath;
	std::string modDbUrl;
	std::string discordUrl;
	std::string newsUrl;
	std::string supportUrl;
	std::string sha256;
	uint64_t size = 0;
	// GeneralsArsenal @feature Codex 15/08/2026 Allow catalog entries to build content from user-owned retail data.
	std::string generator;
	// GeneralsArsenal @feature Codex 15/08/2026 Install author-hosted multi-file releases without repackaging them.
	std::vector<RepositoryContentFile> contentFiles;
	std::string s3Host;
	std::string s3Bucket;
	std::string s3Folder;
};

struct RepositoryIndex
{
	std::vector<RepositoryDescriptor> descriptors;
	std::vector<std::string> warnings;
};

struct RepositoryCatalog
{
	std::vector<RepositoryModification> modifications;
	std::vector<std::string> warnings;
	bool usedCachedData = false;
};

std::vector<RepositorySource> BuiltInRepositorySources();
std::vector<RepositorySource> LoadConfiguredRepositorySources(const std::filesystem::path &modsRoot,
	std::vector<std::string> &warnings);
RepositoryIndex ParseGenLauncherRepositoryIndex(const std::string &yamlText,
	const std::string &sourceId, const std::string &engine);
bool ParseGenLauncherModification(const std::string &yamlText, const RepositoryDescriptor &descriptor,
	RepositoryModification &modification, std::string &errorMessage);
RepositoryCatalog ParseArsenalRepositoryCatalogV1(const std::string &yamlText,
	const std::string &fallbackSourceId, const std::string &catalogUrl = {});
RepositoryCatalog LoadCachedRepositoryCatalog(const std::filesystem::path &cacheRoot);
RepositoryCatalog RefreshRepositoryCatalog(const std::filesystem::path &cacheRoot, bool forceRefresh,
	const std::atomic_bool *cancelRequested = nullptr);

} // namespace GeneralsArsenalLauncher
