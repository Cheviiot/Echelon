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

#include "GeneralsArsenalLauncher/EngineModuleAPI.h"

#include <filesystem>
#include <string>
#include <vector>

namespace GeneralsArsenalContentRuntime
{

struct ContentLayer
{
	uint32_t type = GENERALS_ARSENAL_CONTENT_LAYER_MOD;
	std::string id;
	std::string version;
	std::filesystem::path rootPath;
	uint32_t priority = 0;
	std::string fingerprint;
};

bool Configure(const GeneralsArsenalContentLayerV1 *layers, uint32_t count, std::string &errorMessage);
void Clear();
bool IsClear();
const std::vector<ContentLayer> &Layers();
bool ResolveReadPath(const char *relativePath, std::filesystem::path &resolvedPath);
std::vector<std::filesystem::path> ListFiles(
	const char *relativeDirectory, const char *searchName, bool recursive);

} // namespace GeneralsArsenalContentRuntime
