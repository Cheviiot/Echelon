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

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace EchelonLauncher
{

// Echelon @feature Codex 14/08/2026 Model the three independently selectable content roles used by a managed stack.
enum class ModificationType
{
	Mod,
	Patch,
	Addon
};

// Echelon @feature Codex 07/09/2026 Keep the manifest's file digest records typed and available to workspace verification.
struct ContentFileRecord
{
	std::filesystem::path relativePath;
	uint64_t size = 0;
	std::string sha256;
};

// Echelon @feature Codex 14/08/2026 Keep installed content metadata independent from its catalog source and UI.
struct ContentManifest
{
	std::string id;
	std::string sourceId;
	std::string name;
	std::string version;
	std::string engine;
	ModificationType type = ModificationType::Mod;
	std::string parentId;
	std::vector<std::string> requirements;
	std::vector<std::string> conflicts;
	std::string source;
	std::string sha256;
	std::string contentFingerprint;
	std::filesystem::path coverImagePath;
	std::filesystem::path launchPath;
	std::filesystem::path manifestPath;
	std::vector<ContentFileRecord> files;
	bool legacyManifest = false;
};

using InstalledModification = ContentManifest;

struct ModificationCatalog
{
	std::vector<InstalledModification> modifications;
	std::vector<std::string> warnings;
};

// Echelon @feature Codex 07/09/2026 Resolve one immutable content snapshot in base -> mod -> patch -> addon order.
struct ResolvedContentSnapshot
{
	std::string engine;
	std::vector<const ContentManifest *> layers;
	std::string fingerprint;
};

using ModificationStack = ResolvedContentSnapshot;

ModificationCatalog LoadModificationCatalog(const std::filesystem::path &modsRoot);
const InstalledModification *FindModification(const ModificationCatalog &catalog,
	const std::string &engine, const std::string &selection);
std::string ModificationSelectionKey(const InstalledModification &modification);
std::vector<const InstalledModification *> ModificationsForEngine(const ModificationCatalog &catalog,
	const std::string &engine);
std::vector<const InstalledModification *> ModificationsForEngineAndType(const ModificationCatalog &catalog,
	const std::string &engine, ModificationType type);
const char *ModificationTypeName(ModificationType type);
int CompareModificationVersions(const std::string &left, const std::string &right);
bool ModificationSelectorMatches(const std::string &selector, const InstalledModification &modification);
bool ResolveModificationStack(const ModificationCatalog &catalog, const std::string &engine,
	const std::string &modSelection, const std::vector<std::string> &patchSelections,
	const std::vector<std::string> &addonSelections, ModificationStack &stack, std::string &errorMessage);

} // namespace EchelonLauncher
