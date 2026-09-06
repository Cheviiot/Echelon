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

#include "LauncherDiagnostics.h"
#include "LauncherMods.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace EchelonLauncher
{

struct LocalImportRequest
{
	std::filesystem::path inputPath;
	std::filesystem::path modsRoot;
	std::string engine;
	ModificationType type = ModificationType::Mod;
	std::string parentId;
	std::vector<std::string> requirements;
	std::vector<std::string> conflicts;
	std::string displayName;
	std::string version;
	std::string sourceId = "local";
	std::string modificationId;
	std::string source = "local-import";
	std::string expectedSha256;
	uint64_t expectedSize = 0;
	std::filesystem::path coverImagePath;
};

struct ModificationOperationProgress
{
	std::atomic_uint64_t completedBytes{0};
	std::atomic_uint64_t totalBytes{0};
	std::atomic_bool cancelRequested{false};
};

struct ModificationOperationResult
{
	bool success = false;
	bool cancelled = false;
	std::string message;
	std::string selectionKey;
	std::filesystem::path installedRoot;
	OperationDiagnostic diagnostic;
};

struct ModificationRecoverySummary
{
	size_t resumableDownloads = 0;
	size_t resumableS3Transfers = 0;
	size_t preservedInterruptedImports = 0;
	size_t preservedInterruptedWorkspaces = 0;
	size_t recoverableTrashEntries = 0;
	std::vector<std::string> warnings;
};

struct RecoverableModification
{
	std::filesystem::path path;
	std::string label;
};

ModificationOperationResult ImportLocalModification(const LocalImportRequest &request,
	ModificationOperationProgress *progress = nullptr);
ModificationOperationResult MoveInstalledModificationToTrash(const std::filesystem::path &modsRoot,
	const InstalledModification &modification);
ModificationRecoverySummary RecoverInterruptedModificationOperations(const std::filesystem::path &modsRoot);
std::vector<RecoverableModification> ListRecoverableModifications(const std::filesystem::path &modsRoot);
ModificationOperationResult RestoreModificationFromTrash(const std::filesystem::path &modsRoot,
	const std::filesystem::path &trashPath);
ModificationOperationResult ReplaceModificationCover(const std::filesystem::path &modsRoot,
	const InstalledModification &modification, const std::filesystem::path &imagePath);
bool VerifyInstalledModification(const InstalledModification &modification,
	std::string &errorMessage, ModificationOperationProgress *progress = nullptr);

} // namespace EchelonLauncher
