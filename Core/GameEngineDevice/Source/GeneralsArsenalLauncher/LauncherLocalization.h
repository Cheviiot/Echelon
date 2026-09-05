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

#include "LauncherInstaller.h"
#include "LauncherRepositories.h"

namespace GeneralsArsenalLauncher
{

ModificationOperationResult GenerateRetailRussianLocalization(
	const RepositoryModification &modification, const std::filesystem::path &modsRoot,
	ModificationOperationProgress *progress = nullptr);

} // namespace GeneralsArsenalLauncher
