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

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace EchelonLauncher
{

// Echelon @compat Codex 06/09/2026 Reuse the validated installed manifest and resolved stack as the host content model.
using ContentManifest = InstalledModification;
using ResolvedContentSnapshot = ModificationStack;

struct WorkspaceRecord
{
	std::filesystem::path path;
	std::string profileId;
	std::string engine;
	std::string fingerprint;
	uint64_t generation = 0;
	std::vector<std::filesystem::path> layerRoots;
	std::vector<std::string> layerFingerprints;
};

struct WorkspacePreparationResult
{
	bool success = false;
	bool reused = false;
	std::string message;
	WorkspaceRecord record;
};

// Echelon @feature Codex 06/09/2026 Publish an immutable content snapshot before an engine session.
WorkspacePreparationResult PrepareContentWorkspace(const std::filesystem::path &modsRoot,
	const std::string &profileId, const ModificationStack *stack);

} // namespace EchelonLauncher
