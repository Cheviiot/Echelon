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

#include <filesystem>
#include <string>
#include <vector>

namespace EchelonLauncher
{

// Echelon @feature Codex 13/08/2026 Keep launcher and per-engine options in a versioned, testable model.
enum class LanguageMode
{
	System,
	English,
	Russian
};

// Echelon @feature Codex 13/08/2026 Persist the launcher's own display mode independently from either game.
enum class LauncherWindowMode
{
	Windowed,
	Fullscreen
};

struct ProfileLaunchSettings
{
	bool windowed = false;
	bool quickStart = false;
	bool noShellMap = false;
	bool russianLocalization = true;
	std::string additionalArguments;
};

struct LauncherSettings
{
	LanguageMode language = LanguageMode::System;
	LauncherWindowMode windowMode = LauncherWindowMode::Windowed;
	int windowWidth = 1280;
	int windowHeight = 800;
	ProfileLaunchSettings generals;
	ProfileLaunchSettings zeroHour;
};

struct GameOptions
{
	int resolutionWidth = 1280;
	int resolutionHeight = 800;
	int maxParticleCount = 2500;
	int textureReduction = 1;
	bool useShadowVolumes = false;
	bool buildingOcclusion = false;
	bool useShadowDecals = true;
	bool showTrees = false;
	bool useCloudMap = false;
	bool extraAnimations = false;
	bool useLightMap = false;
	bool dynamicLOD = true;
	bool showSoftWaterEdge = false;
	bool heatEffects = false;
	bool useAlternateMouse = false;
};

struct SagePatchOptions
{
	float maxCameraHeight = 350.0f;
	float minCameraHeight = 100.0f;
	bool enforceMaxCameraHeight = false;
	float keyboardScrollSpeed = 1.0f;
	float terrainDrawDistanceScale = 1.05f;
	bool useFpsLimit = true;
	int framesPerSecondLimit = 60;
};

LauncherSettings LoadLauncherSettings(const std::filesystem::path &path, std::string &errorMessage);
bool SaveLauncherSettings(const std::filesystem::path &path, const LauncherSettings &settings, std::string &errorMessage);

GameOptions LoadGameOptions(const std::filesystem::path &path, std::string &errorMessage);
bool SaveGameOptions(const std::filesystem::path &path, const GameOptions &options, std::string &errorMessage);

SagePatchOptions LoadSagePatchOptions(const std::filesystem::path &path, std::string &errorMessage);
bool SaveSagePatchOptions(const std::filesystem::path &path, const SagePatchOptions &options, std::string &errorMessage);

bool SaveSettingsBundle(const std::filesystem::path &root, const LauncherSettings &launcher,
	const GameOptions &generalsOptions, const GameOptions &zeroHourOptions,
	const SagePatchOptions &generalsSagePatch, const SagePatchOptions &zeroHourSagePatch,
	std::string &errorMessage);
bool RecoverInterruptedSettingsBundle(const std::filesystem::path &root, std::string &errorMessage);

ProfileLaunchSettings &SettingsForProfile(LauncherSettings &settings, const std::string &profileId);
const ProfileLaunchSettings &SettingsForProfile(const LauncherSettings &settings, const std::string &profileId);

bool AppendProfileArguments(const ProfileLaunchSettings &settings,
	std::vector<std::string> &arguments, std::string &errorMessage);

} // namespace EchelonLauncher
