/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherWorkspace.h"

#include "LauncherInstaller.h"
#include "LauncherLocks.h"
#include "LauncherIntegration/EngineModuleAPI.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

std::string SafeComponent(std::string value)
{
	for (char &character : value) {
		const unsigned char current = static_cast<unsigned char>(character);
		if (!((current >= 'a' && current <= 'z') || (current >= 'A' && current <= 'Z') ||
			(current >= '0' && current <= '9') || current == '-' || current == '_' || current == '.')) {
			character = '_';
		}
	}
	return value.empty() ? "default" : value;
}

uint64_t Generation()
{
	return static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
}

fs::path WorkspaceTarget(const fs::path &modsRoot, const std::string &profileId,
	const std::string &engine, const std::string &fingerprint)
{
	return modsRoot / "Workspaces" / SafeComponent(engine) /
		(SafeComponent(profileId) + "-" + SafeComponent(fingerprint));
}

void QuarantineWorkspaceStaging(const fs::path &modsRoot, const fs::path &staging,
	const std::string &profileId)
{
	std::error_code error;
	const fs::path quarantine = modsRoot / ".trash" /
		("workspace-failed-" + SafeComponent(profileId) + "-" + std::to_string(Generation()));
	fs::create_directories(quarantine.parent_path(), error);
	if (!error) fs::rename(staging, quarantine, error);
	if (error) fs::remove_all(staging, error);
}

bool WriteRecord(const fs::path &path, const WorkspaceRecord &record, std::string &errorMessage)
{
	std::ofstream output(path / "workspace.ini", std::ios::binary | std::ios::trunc);
	if (!output) {
		errorMessage = "Cannot create workspace record";
		return false;
	}
	output << "[Workspace]\nSchemaVersion=2\nState=ready\nProfile=" << record.profileId
		<< "\nEngine=" << record.engine << "\nFingerprint=" << record.fingerprint
		<< "\nGeneration=" << record.generation << "\nLayerCount=" << record.layerRoots.size() << '\n';
	for (size_t index = 0; index < record.layerRoots.size(); ++index) {
		output << "Layer" << index << "=" << record.layerRoots[index].string() << '\n';
		if (index < record.layerFingerprints.size() && !record.layerFingerprints[index].empty()) {
			output << "LayerFingerprint" << index << "=" << record.layerFingerprints[index] << '\n';
		}
	}
	output.flush();
	if (!output) {
		errorMessage = "Cannot finish workspace record";
		return false;
	}
	return true;
}

bool ParseUnsigned(const std::string &value, uint64_t &parsed)
{
	if (value.empty()) return false;
	const auto begin = value.data();
	const auto end = begin + value.size();
	const auto conversion = std::from_chars(begin, end, parsed, 10);
	return conversion.ec == std::errc{} && conversion.ptr == end;
}

bool ParseIndexedKey(const std::string &key, std::string_view prefix, size_t &index)
{
	if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) return false;
	uint64_t parsed = 0;
	const std::string suffix = key.substr(prefix.size());
	if (!ParseUnsigned(suffix, parsed) || parsed >= ECHELON_MAX_CONTENT_LAYERS) return false;
	index = static_cast<size_t>(parsed);
	return true;
}

// Echelon @feature Codex 06/09/2026 Reuse immutable read-only files without exposing writable source content.
bool TryHardLinkReadOnlyFile(const fs::path &source, const fs::path &destination, std::error_code &error)
{
	const fs::file_status status = fs::status(source, error);
	if (error || !fs::is_regular_file(status)) return false;
	const fs::perms writeBits = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
	if ((status.permissions() & writeBits) != fs::perms::none) return false;
	fs::create_hard_link(source, destination, error);
	if (!error) return true;
	error.clear();
	return false;
}

// Echelon @feature Codex 06/09/2026 Copy verified layers into an isolated workspace before engine startup.
bool MaterializeLayer(const fs::path &source, const fs::path &destination, std::string &errorMessage)
{
	std::error_code error;
	if (fs::is_symlink(source, error) || error) {
		errorMessage = "Workspace layer source is a symbolic link or cannot be inspected";
		return false;
	}
	error.clear();
	if (!fs::is_directory(source, error) || error) {
		errorMessage = "Workspace layer source is not a regular directory";
		return false;
	}
	fs::create_directories(destination, error);
	if (error) {
		errorMessage = "Cannot create workspace layer: " + error.message();
		return false;
	}
	for (fs::recursive_directory_iterator iterator(source, fs::directory_options::skip_permission_denied, error), end;
		!error && iterator != end; iterator.increment(error)) {
		const fs::directory_entry &entry = *iterator;
		const fs::path relative = entry.path().lexically_relative(source);
		const fs::path target = destination / relative;
		if (entry.is_symlink(error) || error) {
			errorMessage = "Workspace layer contains a symbolic link: " + relative.string();
			return false;
		}
		error.clear();
		if (entry.is_directory(error) && !error) {
			fs::create_directories(target, error);
		} else {
			if (error) {
				errorMessage = "Cannot inspect workspace layer entry " + relative.string() + ": " + error.message();
				return false;
			}
			error.clear();
			if (entry.is_regular_file(error) && !error) {
				fs::create_directories(target.parent_path(), error);
				if (!error && !TryHardLinkReadOnlyFile(entry.path(), target, error)) {
					fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, error);
				}
			} else if (!error) {
				errorMessage = "Workspace layer contains an unsupported file: " + relative.string();
				return false;
			}
		}
		if (error) {
			errorMessage = "Cannot materialize workspace layer file " + relative.string() + ": " + error.message();
			return false;
		}
	}
	if (error) {
		errorMessage = "Cannot enumerate workspace layer: " + error.message();
		return false;
	}
	return true;
}

bool ReadRecord(const fs::path &path, const std::string &profileId, const std::string &engine,
	const std::string &fingerprint, WorkspaceRecord &record)
{
	std::ifstream input(path / "workspace.ini");
	if (!input) return false;
	std::string line;
	std::string state;
	std::string profile;
	std::string storedEngine;
	std::string storedFingerprint;
	uint64_t schemaVersion = 0;
	uint64_t generation = 0;
	uint64_t declaredLayerCount = 0;
	bool hasLayerCount = false;
	bool malformedIndexedKey = false;
	std::map<size_t, fs::path> rootsByIndex;
	std::map<size_t, std::string> fingerprintsByIndex;
	while (std::getline(input, line)) {
		const size_t separator = line.find('=');
		if (separator == std::string::npos) continue;
		const std::string key = line.substr(0, separator);
		const std::string value = line.substr(separator + 1);
		if (key == "SchemaVersion") {
			if (!ParseUnsigned(value, schemaVersion)) malformedIndexedKey = true;
		}
		else if (key == "State") state = value;
		else if (key == "Profile") profile = value;
		else if (key == "Engine") storedEngine = value;
		else if (key == "Fingerprint") storedFingerprint = value;
		else if (key == "Generation") {
			if (!ParseUnsigned(value, generation)) malformedIndexedKey = true;
		}
		else if (key == "LayerCount") {
			hasLayerCount = ParseUnsigned(value, declaredLayerCount);
			if (!hasLayerCount) malformedIndexedKey = true;
		}
		else {
			size_t index = 0;
			if (ParseIndexedKey(key, "LayerFingerprint", index)) {
				if (!fingerprintsByIndex.emplace(index, value).second) malformedIndexedKey = true;
			} else if (ParseIndexedKey(key, "Layer", index)) {
				if (!rootsByIndex.emplace(index, fs::path(value)).second) malformedIndexedKey = true;
			} else if (key.rfind("Layer", 0) == 0) {
				malformedIndexedKey = true;
			}
		}
	}
	const fs::path layerBase = (path / "layers").lexically_normal();
	const bool fingerprintsComplete = fingerprintsByIndex.empty() ||
		(std::all_of(fingerprintsByIndex.begin(), fingerprintsByIndex.end(), [](const auto &entry) {
			return !entry.second.empty();
		}) && fingerprintsByIndex.size() == rootsByIndex.size());
	if (!input.eof() || malformedIndexedKey || schemaVersion != 2 || state != "ready" || profile != profileId ||
		storedEngine != engine || storedFingerprint != fingerprint || generation == 0 || !hasLayerCount ||
		declaredLayerCount > ECHELON_MAX_CONTENT_LAYERS || declaredLayerCount != rootsByIndex.size() ||
		!fingerprintsComplete) {
		return false;
	}
	std::vector<fs::path> roots;
	std::vector<std::string> fingerprints;
	roots.reserve(rootsByIndex.size());
	fingerprints.reserve(fingerprintsByIndex.size());
	for (size_t index = 0; index < rootsByIndex.size(); ++index) {
		const auto root = rootsByIndex.find(index);
		const auto layerFingerprint = fingerprintsByIndex.find(index);
		if (root == rootsByIndex.end()) return false;
		std::error_code pathError;
		if (root->second.lexically_normal() != (layerBase / std::to_string(index)).lexically_normal() ||
			fs::is_symlink(root->second, pathError) || pathError) return false;
		pathError.clear();
		if (!fs::is_directory(root->second, pathError) || pathError) return false;
		roots.push_back(root->second);
		fingerprints.push_back(layerFingerprint == fingerprintsByIndex.end() ? std::string{} : layerFingerprint->second);
	}
	record.path = path;
	record.profileId = profile;
	record.engine = storedEngine;
	record.fingerprint = storedFingerprint;
	record.generation = generation;
	record.layerRoots = std::move(roots);
	record.layerFingerprints = std::move(fingerprints);
	return true;
}

bool VerifyWorkspaceCopies(const WorkspaceRecord &record, const std::string &engine, std::string &errorMessage)
{
	if (record.layerRoots.size() != record.layerFingerprints.size()) {
		errorMessage = "Workspace record has incomplete layer fingerprints";
		return false;
	}
	for (size_t index = 0; index < record.layerRoots.size(); ++index) {
		InstalledModification workspaceLayer;
		workspaceLayer.id = "workspace-layer-" + std::to_string(index);
		workspaceLayer.engine = engine;
		workspaceLayer.launchPath = record.layerRoots[index];
		workspaceLayer.contentFingerprint = record.layerFingerprints[index];
		if (!VerifyInstalledModification(workspaceLayer, errorMessage)) return false;
	}
	return true;
}

bool WorkspaceRecordMatchesLayers(const WorkspaceRecord &record,
	const std::vector<const InstalledModification *> &layers)
{
	if (record.layerRoots.size() != layers.size() || record.layerFingerprints.size() != layers.size()) return false;
	for (size_t index = 0; index < layers.size(); ++index) {
		if (!layers[index]) return false;
		if (!layers[index]->contentFingerprint.empty() &&
			record.layerFingerprints[index] != layers[index]->contentFingerprint) return false;
	}
	return true;
}

} // namespace

WorkspacePreparationResult PrepareContentWorkspace(const fs::path &modsRoot,
	const std::string &profileId, const ModificationStack *stack)
{
	WorkspacePreparationResult result;
	const std::string engine = stack ? stack->engine : profileId;
	const std::string fingerprint = stack ? (stack->fingerprint.empty() ? "vanilla" : stack->fingerprint) : "vanilla";
	if (profileId.empty() || (engine != "generals" && engine != "zerohour")) {
		result.message = "Invalid workspace profile";
		return result;
	}
	if (stack && stack->engine != profileId) {
		result.message = "Workspace content stack belongs to another profile";
		return result;
	}

	std::string lockError;
	ScopedDirectoryLock workspaceLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "workspace", profileId), lockError);
	if (!workspaceLock.acquired()) {
		result.message = lockError.empty() ? "Another workspace operation is active" : lockError;
		return result;
	}
	// Echelon @bugfix Codex 06/09/2026 Serialize workspace verification with imports and restores.
	// A catalog mutation must not race the fingerprint scan that feeds a published workspace.
	ScopedDirectoryLock contentOperationLock = ScopedDirectoryLock::TryAcquire(
		MakeLockPath(modsRoot, "content", "operation"), lockError);
	if (!contentOperationLock.acquired()) {
		result.message = lockError.empty() ? "Another content operation is active" : lockError;
		return result;
	}

	std::vector<const InstalledModification *> verifiedLayers;
	if (stack) {
		if (stack->layers.size() > ECHELON_MAX_CONTENT_LAYERS) {
			result.message = "Workspace content stack has too many layers";
			return result;
		}
		verifiedLayers.reserve(stack->layers.size());
	}
	WorkspaceRecord record;
	record.profileId = profileId;
	record.engine = engine;
	record.fingerprint = fingerprint;
	if (stack) {
		for (const InstalledModification *layer : stack->layers) {
			if (!layer || layer->engine != engine || layer->launchPath.empty()) {
				result.message = "Workspace content stack contains an invalid layer";
				return result;
			}
			std::string verificationError;
			if (!VerifyInstalledModification(*layer, verificationError)) {
				result.message = "Cannot verify workspace layer " + layer->id + ": " + verificationError;
				return result;
			}
			verifiedLayers.push_back(layer);
		}
	}

	const fs::path target = WorkspaceTarget(modsRoot, profileId, engine, fingerprint);
	std::error_code targetError;
	const bool targetIsSymlink = fs::is_symlink(target, targetError);
	if (!targetError && !targetIsSymlink && ReadRecord(target, profileId, engine, fingerprint, record)) {
		std::string verificationError;
		if (!WorkspaceRecordMatchesLayers(record, verifiedLayers) ||
			!VerifyWorkspaceCopies(record, engine, verificationError)) {
			result.message = "Published workspace content is damaged and will be rebuilt";
		} else {
			result.success = true;
			result.reused = true;
			result.record = std::move(record);
			result.message = "Workspace reused";
			return result;
		}
	}

	std::error_code error;
	targetError.clear();
	const bool targetExists = fs::exists(target, targetError);
	if (targetError) {
		result.message = "Cannot inspect existing workspace: " + targetError.message();
		return result;
	}
	if (targetExists || targetIsSymlink) {
		const fs::path quarantine = modsRoot / ".trash" /
			("workspace-invalid-" + SafeComponent(profileId) + "-" + std::to_string(Generation()));
		fs::create_directories(quarantine.parent_path(), error);
		if (!error) fs::rename(target, quarantine, error);
		if (error) {
			result.message = "Cannot quarantine invalid workspace: " + error.message();
			return result;
		}
	}
	fs::create_directories(target.parent_path(), error);
	if (error) {
		result.message = "Cannot create workspace directory: " + error.message();
		return result;
	}
	const uint64_t generation = Generation();
	const fs::path staging = modsRoot / ".staging" /
		("workspace-" + SafeComponent(profileId) + "-" + SafeComponent(fingerprint) + "-" + std::to_string(generation));
	fs::create_directories(staging, error);
	if (error) {
		result.message = "Cannot create workspace staging directory: " + error.message();
		return result;
	}
	record.path = target;
	record.generation = generation;
	record.layerRoots.clear();
	record.layerFingerprints.clear();
	for (size_t index = 0; index < verifiedLayers.size(); ++index) {
		const fs::path source = verifiedLayers[index]->launchPath;
		const fs::path destination = staging / "layers" / std::to_string(index);
		if (!MaterializeLayer(source, destination, result.message)) {
			QuarantineWorkspaceStaging(modsRoot, staging, profileId);
			return result;
		}
		record.layerRoots.push_back(target / "layers" / std::to_string(index));
		record.layerFingerprints.push_back(verifiedLayers[index]->contentFingerprint);
	}
	if (!WriteRecord(staging, record, result.message)) {
		QuarantineWorkspaceStaging(modsRoot, staging, profileId);
		return result;
	}
	fs::rename(staging, target, error);
	if (error) {
		QuarantineWorkspaceStaging(modsRoot, staging, profileId);
		result.message = "Cannot publish workspace atomically: " + error.message();
		return result;
	}
	result.success = true;
	result.record = std::move(record);
	result.message = "Workspace prepared";
	return result;
}

} // namespace EchelonLauncher
