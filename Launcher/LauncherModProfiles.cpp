/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherModProfiles.h"

#include "LauncherIntegration/EngineModuleAPI.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

constexpr size_t kMaximumProfileBytes = 64 * 1024;
constexpr size_t kMaximumAddons = ECHELON_MAX_CONTENT_LAYERS - 2;

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string Trim(const std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool IsSupportedEngine(const std::string &engine)
{
	return engine == "generals" || engine == "zerohour";
}

bool IsSafeProfileValue(const std::string &value)
{
	return value.size() <= 1024 && value.find('\r') == std::string::npos &&
		value.find('\n') == std::string::npos && value.find('\0') == std::string::npos;
}

fs::path ProfilePath(const fs::path &modsRoot, const std::string &engine)
{
	return modsRoot / "Profiles" / (engine + ".ini");
}

bool ParentIsActive(const InstalledModification &modification, const InstalledModification *mod,
	const InstalledModification *patch)
{
	return modification.parentId.empty() || (mod && modification.parentId == mod->id) ||
		(modification.type == ModificationType::Addon && patch && modification.parentId == patch->id);
}

std::string SelectionId(const std::string &selection)
{
	const size_t separator = selection.rfind('@');
	return separator == std::string::npos ? ToLower(selection) : ToLower(selection.substr(0, separator));
}

} // namespace

ModificationSelectionProfileV1 LoadModificationSelectionProfile(const fs::path &modsRoot,
	const std::string &engine, std::string &warning)
{
	ModificationSelectionProfileV1 profile;
	profile.engine = ToLower(engine);
	if (!IsSupportedEngine(profile.engine)) {
		warning = "Unsupported modification profile engine";
		return profile;
	}
	const fs::path path = ProfilePath(modsRoot, profile.engine);
	std::error_code error;
	if (!fs::exists(path, error)) return profile;
	if (error || !fs::is_regular_file(path, error) || fs::file_size(path, error) > kMaximumProfileBytes) {
		warning = "Modification profile is unavailable or too large: " + path.string();
		return profile;
	}
	std::ifstream input(path);
	std::string line;
	bool validSchema = false;
	std::string declaredEngine;
	while (std::getline(input, line)) {
		if (line.size() > 4096) {
			warning = "Modification profile contains an oversized line";
			return ModificationSelectionProfileV1{profile.engine};
		}
		const size_t separator = line.find('=');
		if (separator == std::string::npos) continue;
		const std::string key = ToLower(Trim(line.substr(0, separator)));
		const std::string value = Trim(line.substr(separator + 1));
		if (!IsSafeProfileValue(value)) continue;
		if (key == "schemaversion") validSchema = value == "1";
		else if (key == "engine") declaredEngine = ToLower(value);
		else if (key == "mod") profile.modSelection = ToLower(value);
		else if (key == "patch") profile.patchSelection = ToLower(value);
		else if (key == "addon" && profile.addonSelections.size() < kMaximumAddons) {
			profile.addonSelections.push_back(ToLower(value));
		}
	}
	if (!input.eof() || !validSchema || declaredEngine != profile.engine) {
		warning = "Invalid modification profile: " + path.string();
		return ModificationSelectionProfileV1{profile.engine};
	}
	return profile;
}

bool SaveModificationSelectionProfile(const fs::path &modsRoot,
	const ModificationSelectionProfileV1 &profile, std::string &errorMessage)
{
	const std::string engine = ToLower(profile.engine);
	if (!IsSupportedEngine(engine) || profile.addonSelections.size() > kMaximumAddons ||
		!IsSafeProfileValue(profile.modSelection) || !IsSafeProfileValue(profile.patchSelection) ||
		!std::all_of(profile.addonSelections.begin(), profile.addonSelections.end(), IsSafeProfileValue)) {
		errorMessage = "Invalid modification profile state";
		return false;
	}
	const fs::path path = ProfilePath(modsRoot, engine);
	const fs::path temporary = path.string() + ".tmp";
	const fs::path backup = path.string() + ".bak";
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	if (error) {
		errorMessage = "Cannot create modification profile directory: " + error.message();
		return false;
	}
	{
		std::ofstream output(temporary, std::ios::trunc);
		output << "[ModificationStack]\nSchemaVersion=1\nEngine=" << engine
			<< "\nMod=" << profile.modSelection << "\nPatch=" << profile.patchSelection << '\n';
		for (const std::string &addon : profile.addonSelections) output << "Addon=" << addon << '\n';
		output.flush();
		if (!output) {
			fs::remove(temporary, error);
			errorMessage = "Cannot write modification profile";
			return false;
		}
	}
	fs::remove(backup, error);
	error.clear();
	if (fs::exists(path, error)) fs::rename(path, backup, error);
	if (error) {
		fs::remove(temporary, error);
		errorMessage = "Cannot prepare modification profile publication: " + error.message();
		return false;
	}
	fs::rename(temporary, path, error);
	if (error) {
		std::error_code rollbackError;
		if (fs::exists(backup, rollbackError)) fs::rename(backup, path, rollbackError);
		fs::remove(temporary, rollbackError);
		errorMessage = "Cannot publish modification profile: " + error.message();
		return false;
	}
	fs::remove(backup, error);
	errorMessage.clear();
	return true;
}

bool ReconcileModificationSelectionProfile(const ModificationCatalog &catalog,
	ModificationSelectionProfileV1 &profile, std::string &warning)
{
	bool changed = false;
	const InstalledModification *mod = profile.modSelection.empty() ? nullptr :
		FindModification(catalog, profile.engine, profile.modSelection);
	if (!mod || mod->type != ModificationType::Mod) {
		if (!profile.modSelection.empty()) changed = true;
		profile.modSelection.clear();
		mod = nullptr;
	}
	const InstalledModification *patch = profile.patchSelection.empty() ? nullptr :
		FindModification(catalog, profile.engine, profile.patchSelection);
	if (!patch || patch->type != ModificationType::Patch || !ParentIsActive(*patch, mod, nullptr)) {
		if (!profile.patchSelection.empty()) changed = true;
		profile.patchSelection.clear();
		patch = nullptr;
	}
	std::vector<std::string> addons;
	std::unordered_set<std::string> seen;
	for (const std::string &selection : profile.addonSelections) {
		const InstalledModification *addon = FindModification(catalog, profile.engine, selection);
		if (!addon || addon->type != ModificationType::Addon || !ParentIsActive(*addon, mod, patch) ||
			!seen.insert(ToLower(selection)).second) {
			changed = true;
			continue;
		}
		addons.push_back(ToLower(selection));
	}
	// Echelon @bugfix Codex 15/08/2026 Rebuild saved stacks progressively so conflicts and exact requirements cannot survive restart.
	ModificationSelectionProfileV1 validated;
	validated.engine = profile.engine;
	ModificationStack stack;
	std::string compatibilityError;
	if (mod) {
		validated.modSelection = profile.modSelection;
		if (!BuildSelectedModificationStack(catalog, validated, stack, compatibilityError)) {
			changed = true;
			validated.modSelection.clear();
			mod = nullptr;
		}
	}
	if (mod && patch) {
		ModificationSelectionProfileV1 candidate = validated;
		candidate.patchSelection = profile.patchSelection;
		if (BuildSelectedModificationStack(catalog, candidate, stack, compatibilityError)) {
			validated = std::move(candidate);
		} else {
			changed = true;
			patch = nullptr;
		}
	}
	for (const std::string &selection : addons) {
		ModificationSelectionProfileV1 candidate = validated;
		candidate.addonSelections.push_back(selection);
		if (BuildSelectedModificationStack(catalog, candidate, stack, compatibilityError)) {
			validated = std::move(candidate);
		} else {
			changed = true;
		}
	}
	profile = std::move(validated);
	if (changed) warning = "Unavailable or incompatible items were removed from the saved modification stack";
	return changed;
}

bool BuildSelectedModificationStack(const ModificationCatalog &catalog,
	const ModificationSelectionProfileV1 &profile, ModificationStack &stack, std::string &errorMessage)
{
	std::vector<std::string> patches;
	if (!profile.patchSelection.empty()) patches.push_back(profile.patchSelection);
	return ResolveModificationStack(catalog, profile.engine, profile.modSelection, patches,
		profile.addonSelections, stack, errorMessage);
}

bool IsModificationSelected(const ModificationSelectionProfileV1 &profile,
	const InstalledModification &modification)
{
	const std::string key = ModificationSelectionKey(modification);
	if (modification.type == ModificationType::Mod) return profile.modSelection == key;
	if (modification.type == ModificationType::Patch) return profile.patchSelection == key;
	return std::find(profile.addonSelections.begin(), profile.addonSelections.end(), key) != profile.addonSelections.end();
}

bool SelectModification(ModificationSelectionProfileV1 &profile,
	const InstalledModification *modification, std::string &errorMessage)
{
	if (!modification) {
		profile.modSelection.clear();
		profile.patchSelection.clear();
		profile.addonSelections.clear();
		errorMessage.clear();
		return true;
	}
	if (modification->engine != profile.engine) {
		errorMessage = "Modification belongs to another engine";
		return false;
	}
	const std::string key = ModificationSelectionKey(*modification);
	if (modification->type == ModificationType::Mod) {
		if (profile.modSelection == key) {
			errorMessage.clear();
			return true;
		}
		const bool sameModification = !profile.modSelection.empty() &&
			SelectionId(profile.modSelection) == ToLower(modification->id);
		profile.modSelection = key;
		if (!sameModification) {
			profile.patchSelection.clear();
			profile.addonSelections.clear();
		}
	} else if (modification->type == ModificationType::Patch) {
		if (profile.patchSelection == key) {
			profile.patchSelection.clear();
		} else {
			profile.patchSelection = key;
		}
	} else {
		auto existing = std::find(profile.addonSelections.begin(), profile.addonSelections.end(), key);
		if (existing == profile.addonSelections.end()) {
			auto sameAddon = std::find_if(profile.addonSelections.begin(), profile.addonSelections.end(),
				[&](const std::string &selection) { return SelectionId(selection) == ToLower(modification->id); });
			if (sameAddon != profile.addonSelections.end()) {
				*sameAddon = key;
				errorMessage.clear();
				return true;
			}
			if (profile.addonSelections.size() >= kMaximumAddons) {
				errorMessage = "The modification stack has reached its add-on limit";
				return false;
			}
			profile.addonSelections.push_back(key);
		} else {
			profile.addonSelections.erase(existing);
		}
	}
	errorMessage.clear();
	return true;
}

// Echelon @bugfix Codex 15/08/2026 Preserve base-mod add-ons across patch changes and remove only incompatible layers.
bool SelectCompatibleModification(const ModificationCatalog &catalog,
	ModificationSelectionProfileV1 &profile, const InstalledModification *modification,
	std::string &warningOrError)
{
	const ModificationSelectionProfileV1 previous = profile;
	if (!SelectModification(profile, modification, warningOrError)) return false;
	const bool targetMustRemainSelected = modification && IsModificationSelected(profile, *modification);
	ModificationStack candidate;
	if (BuildSelectedModificationStack(catalog, profile, candidate, warningOrError)) return true;
	const std::string compatibilityError = warningOrError;
	std::string reconcileWarning;
	ReconcileModificationSelectionProfile(catalog, profile, reconcileWarning);
	if ((targetMustRemainSelected && !IsModificationSelected(profile, *modification)) ||
		!BuildSelectedModificationStack(catalog, profile, candidate, warningOrError)) {
		profile = previous;
		if (!compatibilityError.empty()) warningOrError = compatibilityError;
		return false;
	}
	warningOrError = reconcileWarning;
	return true;
}

bool MoveSelectedAddon(ModificationSelectionProfileV1 &profile,
	const std::string &selection, int direction)
{
	auto current = std::find(profile.addonSelections.begin(), profile.addonSelections.end(), ToLower(selection));
	if (current == profile.addonSelections.end() || direction == 0) return false;
	const auto target = direction < 0 ? (current == profile.addonSelections.begin() ? current : std::prev(current)) :
		(std::next(current) == profile.addonSelections.end() ? current : std::next(current));
	if (target == current) return false;
	std::iter_swap(current, target);
	return true;
}

bool MoveSelectedAddonTo(ModificationSelectionProfileV1 &profile,
	const std::string &selection, size_t targetIndex)
{
	auto current = std::find(profile.addonSelections.begin(), profile.addonSelections.end(), ToLower(selection));
	if (current == profile.addonSelections.end() || targetIndex >= profile.addonSelections.size()) return false;
	const size_t currentIndex = static_cast<size_t>(std::distance(profile.addonSelections.begin(), current));
	if (currentIndex == targetIndex) return false;
	const std::string value = *current;
	profile.addonSelections.erase(current);
	profile.addonSelections.insert(profile.addonSelections.begin() + static_cast<std::ptrdiff_t>(targetIndex), value);
	return true;
}

} // namespace EchelonLauncher
