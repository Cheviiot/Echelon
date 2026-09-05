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

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace GeneralsArsenalLauncher
{

// GeneralsArsenal @feature Codex 14/08/2026 Persist one user-controlled stack per engine without affecting vanilla buttons.
struct ModificationSelectionProfileV1
{
	std::string engine;
	std::string modSelection;
	std::string patchSelection;
	std::vector<std::string> addonSelections;
};

ModificationSelectionProfileV1 LoadModificationSelectionProfile(const std::filesystem::path &modsRoot,
	const std::string &engine, std::string &warning);
bool SaveModificationSelectionProfile(const std::filesystem::path &modsRoot,
	const ModificationSelectionProfileV1 &profile, std::string &errorMessage);
bool ReconcileModificationSelectionProfile(const ModificationCatalog &catalog,
	ModificationSelectionProfileV1 &profile, std::string &warning);
bool BuildSelectedModificationStack(const ModificationCatalog &catalog,
	const ModificationSelectionProfileV1 &profile, ModificationStack &stack, std::string &errorMessage);
bool IsModificationSelected(const ModificationSelectionProfileV1 &profile,
	const InstalledModification &modification);
bool SelectModification(ModificationSelectionProfileV1 &profile,
	const InstalledModification *modification, std::string &errorMessage);
bool SelectCompatibleModification(const ModificationCatalog &catalog,
	ModificationSelectionProfileV1 &profile, const InstalledModification *modification,
	std::string &warningOrError);
bool MoveSelectedAddon(ModificationSelectionProfileV1 &profile,
	const std::string &selection, int direction);
bool MoveSelectedAddonTo(ModificationSelectionProfileV1 &profile,
	const std::string &selection, size_t targetIndex);

} // namespace GeneralsArsenalLauncher
