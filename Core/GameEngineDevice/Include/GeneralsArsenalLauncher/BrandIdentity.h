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

// GeneralsArsenal @feature Codex 12/08/2026 Central public brand identity for fork-owned C++ code.
namespace GeneralsArsenalBrand
{
inline constexpr const char *kProductName = "Generals: Arsenal";
inline constexpr const wchar_t *kProductNameWide = L"Generals: Arsenal";
inline constexpr const char *kProductSlug = "GeneralsArsenal";
inline constexpr const char *kDataDirectory = ".GeneralsArsenal";
inline constexpr const char *kInstallDataDirectory = "generals-arsenal";
inline constexpr const char *kModuleExport = "GeneralsArsenal_GetEngineModuleV2";
inline constexpr const char *kReleaseApiUrl = "https://api.github.com/repos/Cheviiot/GeneralsArsenal/releases/latest";
inline constexpr const char *kReleasesUrl = "https://github.com/Cheviiot/GeneralsArsenal/releases";
// GeneralsArsenal @feature Codex 15/08/2026 Use the fork-owned GitHub Releases catalog without external storage credentials.
inline constexpr const char *kRepositoryCatalogUrl =
	"https://github.com/Cheviiot/GeneralsArsenalRepository/releases/latest/download/catalog.json";
inline constexpr const char *kUpdateUserAgent = "GeneralsArsenal/update-checker";
inline constexpr const char *kReleaseTagPrefix = "GeneralsArsenal-";
inline constexpr const char *kReleaseTagPrefixLower = "generalsarsenal-";
inline constexpr const char *kProjectWatermark = "Generals: Arsenal - Unified C&C Generals";
} // namespace GeneralsArsenalBrand
