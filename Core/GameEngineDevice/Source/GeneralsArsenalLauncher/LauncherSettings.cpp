/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherSettings.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace GeneralsArsenalLauncher
{
namespace
{

struct IniLine
{
	std::string original;
	std::string key;
	std::string value;
};

std::string YesNo(bool value);
std::string LanguageName(LanguageMode language);
std::string WindowModeName(LauncherWindowMode mode);
void ReplaceOrAppend(std::vector<IniLine> &lines, const std::string &key, const std::string &value);

std::string Trim(const std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool ParseBool(const std::string &value, bool fallback)
{
	const std::string normalized = ToLower(Trim(value));
	if (normalized == "yes" || normalized == "true" || normalized == "1" || normalized == "on") return true;
	if (normalized == "no" || normalized == "false" || normalized == "0" || normalized == "off") return false;
	return fallback;
}

int ParseInteger(const std::string &value, int fallback, int minimum, int maximum)
{
	const std::string normalized = Trim(value);
	char *end = nullptr;
	const long parsed = std::strtol(normalized.c_str(), &end, 10);
	if (!end || *end != '\0' || parsed < minimum || parsed > maximum) return fallback;
	return static_cast<int>(parsed);
}

float ParseFloat(const std::string &value, float fallback, float minimum, float maximum)
{
	const std::string normalized = Trim(value);
	char *end = nullptr;
	const float parsed = std::strtof(normalized.c_str(), &end);
	if (!end || *end != '\0' || !std::isfinite(parsed) || parsed < minimum || parsed > maximum) return fallback;
	return parsed;
}

std::vector<IniLine> ReadIniLines(const fs::path &path)
{
	std::vector<IniLine> lines;
	std::ifstream input(path);
	std::string line;
	while (std::getline(input, line)) {
		IniLine parsed;
		parsed.original = line;
		const size_t equals = line.find('=');
		if (equals != std::string::npos) {
			parsed.key = ToLower(Trim(line.substr(0, equals)));
			parsed.value = Trim(line.substr(equals + 1));
		}
		lines.push_back(std::move(parsed));
	}
	return lines;
}

std::unordered_map<std::string, std::string> ToValueMap(const std::vector<IniLine> &lines)
{
	std::unordered_map<std::string, std::string> values;
	for (const IniLine &line : lines) {
		if (!line.key.empty() && values.find(line.key) == values.end()) values.emplace(line.key, line.value);
	}
	return values;
}

std::unordered_map<std::string, std::string> ToBlockValueMap(const std::vector<IniLine> &lines, const std::string &blockName)
{
	std::unordered_map<std::string, std::string> values;
	bool insideBlock = false;
	const std::string normalizedBlockName = ToLower(blockName);
	for (const IniLine &line : lines) {
		const std::string marker = ToLower(Trim(line.original));
		if (!insideBlock) {
			insideBlock = marker == normalizedBlockName;
			continue;
		}
		if (marker == "end") break;
		if (!line.key.empty() && values.find(line.key) == values.end()) values.emplace(line.key, line.value);
	}
	return values;
}

bool WriteAtomically(const fs::path &path, const std::string &contents, std::string &errorMessage)
{
	errorMessage.clear();
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	if (error) {
		errorMessage = "Cannot create settings directory: " + error.message();
		return false;
	}
	const fs::path temporary = path.string() + ".tmp";
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output) {
			errorMessage = "Cannot create temporary settings file";
			return false;
		}
		output << contents;
		output.flush();
		if (!output) {
			errorMessage = "Cannot write temporary settings file";
			return false;
		}
	}
	if (!SDL_RenamePath(temporary.string().c_str(), path.string().c_str())) {
		errorMessage = std::string("Cannot publish settings file: ") + SDL_GetError();
		fs::remove(temporary, error);
		return false;
	}
	return true;
}

std::string FormatFloat(float value)
{
	std::ostringstream output;
	output << std::fixed << std::setprecision(2) << value;
	std::string result = output.str();
	while (result.size() > 1 && result.back() == '0') result.pop_back();
	if (!result.empty() && result.back() == '.') result.push_back('0');
	return result;
}

// GeneralsArsenal @feature Codex 13/08/2026 Publish all profile files as one recoverable transaction.
bool PublishSettingsFiles(const std::vector<std::pair<fs::path, std::string>> &files,
	const fs::path &journalPath, std::string &errorMessage)
{
	errorMessage.clear();
	struct Publication
	{
		fs::path target;
		fs::path staged;
		fs::path backup;
		bool hadTarget = false;
		bool backedUp = false;
		bool published = false;
	};
	std::vector<Publication> publications;
	publications.reserve(files.size());
	std::error_code error;
	auto removeStagedFiles = [&]() {
		for (const Publication &publication : publications) fs::remove(publication.staged, error);
	};
	const std::string transaction = std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count());
	for (const auto &[target, contents] : files) {
		fs::create_directories(target.parent_path(), error);
		if (error) {
			errorMessage = "Cannot create settings directory: " + error.message();
			removeStagedFiles();
			return false;
		}
		Publication publication;
		publication.target = target;
		publication.staged = target.string() + ".stage-" + transaction;
		publication.backup = target.string() + ".backup-" + transaction;
		publication.hadTarget = fs::exists(target, error);
		if (error) {
			errorMessage = "Cannot inspect settings file: " + error.message();
			removeStagedFiles();
			return false;
		}
		std::ofstream output(publication.staged, std::ios::binary | std::ios::trunc);
		output << contents;
		output.flush();
		if (!output) {
			errorMessage = "Cannot stage settings file: " + publication.staged.string();
			removeStagedFiles();
			fs::remove(publication.staged, error);
			return false;
		}
		publications.push_back(std::move(publication));
	}
	std::ostringstream journal;
	journal << transaction << '\n';
	for (const Publication &publication : publications) {
		journal << (publication.hadTarget ? "1" : "0") << '\n';
	}
	if (!WriteAtomically(journalPath, journal.str(), errorMessage)) {
		removeStagedFiles();
		return false;
	}

	for (Publication &publication : publications) {
		if (publication.hadTarget && !SDL_RenamePath(publication.target.string().c_str(), publication.backup.string().c_str())) {
			errorMessage = std::string("Cannot back up settings file: ") + SDL_GetError();
			break;
		}
		publication.backedUp = publication.hadTarget;
		if (!SDL_RenamePath(publication.staged.string().c_str(), publication.target.string().c_str())) {
			errorMessage = std::string("Cannot publish settings file: ") + SDL_GetError();
			break;
		}
		publication.published = true;
	}
	if (!errorMessage.empty()) {
		bool rollbackComplete = true;
		std::string rollbackError;
		for (auto publication = publications.rbegin(); publication != publications.rend(); ++publication) {
			if (publication->backedUp) {
				if (!SDL_RenamePath(publication->backup.string().c_str(), publication->target.string().c_str())) {
					rollbackComplete = false;
					rollbackError = SDL_GetError();
				}
			} else if (publication->published) {
				fs::remove(publication->target, error);
				if (error) {
					rollbackComplete = false;
					rollbackError = error.message();
				}
			}
			fs::remove(publication->staged, error);
		}
		if (rollbackComplete) fs::remove(journalPath, error);
		else errorMessage += "; rollback will resume on next launch: " + rollbackError;
		return false;
	}
	fs::remove(journalPath, error);
	if (error) {
		errorMessage = "Cannot commit settings transaction: " + error.message();
		for (auto publication = publications.rbegin(); publication != publications.rend(); ++publication) {
			fs::remove(publication->target, error);
			if (publication->backedUp) SDL_RenamePath(publication->backup.string().c_str(), publication->target.string().c_str());
		}
		return false;
	}
	for (const Publication &publication : publications) {
		if (publication.hadTarget) fs::remove(publication.backup, error);
	}
	return true;
}

std::string SerializeLauncherSettings(const LauncherSettings &settings)
{
	std::ostringstream output;
	output << "[Launcher]\nSchemaVersion=2\nLanguage=" << LanguageName(settings.language)
		<< "\nWindowMode=" << WindowModeName(settings.windowMode)
		<< "\nWindowWidth=" << std::clamp(settings.windowWidth, 640, 16384)
		<< "\nWindowHeight=" << std::clamp(settings.windowHeight, 480, 16384) << "\n\n";
	auto writeProfile = [&](const char *name, const ProfileLaunchSettings &profile) {
		std::string arguments = profile.additionalArguments;
		arguments.erase(std::remove(arguments.begin(), arguments.end(), '\r'), arguments.end());
		std::replace(arguments.begin(), arguments.end(), '\n', ' ');
		output << '[' << name << "]\nWindowed=" << YesNo(profile.windowed)
			<< "\nQuickStart=" << YesNo(profile.quickStart)
			<< "\nNoShellMap=" << YesNo(profile.noShellMap)
			<< "\nRussianLocalization=" << YesNo(profile.russianLocalization)
			<< "\nAdditionalArguments=" << arguments << "\n\n";
	};
	writeProfile("Generals", settings.generals);
	writeProfile("ZeroHour", settings.zeroHour);
	return output.str();
}

std::string SerializeGameOptions(const fs::path &path, const GameOptions &options)
{
	std::vector<IniLine> lines = ReadIniLines(path);
	ReplaceOrAppend(lines, "Resolution", std::to_string(std::clamp(options.resolutionWidth, 640, 16384)) + " " +
		std::to_string(std::clamp(options.resolutionHeight, 480, 16384)));
	ReplaceOrAppend(lines, "MaxParticleCount", std::to_string(std::clamp(options.maxParticleCount, 100, 10000)));
	ReplaceOrAppend(lines, "TextureReduction", std::to_string(std::clamp(options.textureReduction, 0, 2)));
	ReplaceOrAppend(lines, "UseShadowVolumes", YesNo(options.useShadowVolumes));
	ReplaceOrAppend(lines, "BuildingOcclusion", YesNo(options.buildingOcclusion));
	ReplaceOrAppend(lines, "UseShadowDecals", YesNo(options.useShadowDecals));
	ReplaceOrAppend(lines, "ShowTrees", YesNo(options.showTrees));
	ReplaceOrAppend(lines, "UseCloudMap", YesNo(options.useCloudMap));
	ReplaceOrAppend(lines, "ExtraAnimations", YesNo(options.extraAnimations));
	ReplaceOrAppend(lines, "UseLightMap", YesNo(options.useLightMap));
	ReplaceOrAppend(lines, "DynamicLOD", YesNo(options.dynamicLOD));
	ReplaceOrAppend(lines, "ShowSoftWaterEdge", YesNo(options.showSoftWaterEdge));
	ReplaceOrAppend(lines, "HeatEffects", YesNo(options.heatEffects));
	ReplaceOrAppend(lines, "UseAlternateMouse", YesNo(options.useAlternateMouse));
	std::ostringstream output;
	for (const IniLine &line : lines) output << line.original << '\n';
	return output.str();
}

std::string SerializeSagePatchOptions(const fs::path &path, const SagePatchOptions &options)
{
	std::vector<IniLine> lines = ReadIniLines(path);
	const bool hasGameData = std::any_of(lines.begin(), lines.end(), [](const IniLine &line) {
		return ToLower(Trim(line.original)) == "gamedata";
	});
	if (lines.empty()) {
		lines.push_back({"; Generals: Arsenal safe QoL overrides", {}, {}});
	}
	if (!hasGameData) {
		if (!lines.empty() && !lines.back().original.empty()) lines.push_back({});
		lines.push_back({"GameData", {}, {}});
		lines.push_back({"End", {}, {}});
	}
	auto insertSetting = [&](const std::string &key, const std::string &value) {
		auto gameData = std::find_if(lines.begin(), lines.end(), [](const IniLine &line) {
			return ToLower(Trim(line.original)) == "gamedata";
		});
		auto gameDataEnd = std::find_if(gameData, lines.end(), [](const IniLine &line) {
			return ToLower(Trim(line.original)) == "end";
		});
		if (gameDataEnd == lines.end()) {
			lines.push_back({"End", {}, {}});
			gameData = std::find_if(lines.begin(), lines.end(), [](const IniLine &line) {
				return ToLower(Trim(line.original)) == "gamedata";
			});
			gameDataEnd = std::prev(lines.end());
		}
		const std::string normalized = ToLower(key);
		bool replaced = false;
		for (auto line = std::next(gameData); line != gameDataEnd; ++line) {
			if (line->key == normalized) {
				line->original = "  " + key + " = " + value;
				line->value = value;
				replaced = true;
			}
		}
		if (replaced) return;
		const IniLine setting{"  " + key + " = " + value, normalized, value};
		lines.insert(gameDataEnd, setting);
	};
	insertSetting("MaxCameraHeight", FormatFloat(std::clamp(options.maxCameraHeight, 250.0f, 1000.0f)));
	insertSetting("MinCameraHeight", FormatFloat(std::clamp(options.minCameraHeight, 40.0f, 250.0f)));
	insertSetting("EnforceMaxCameraHeight", YesNo(options.enforceMaxCameraHeight));
	insertSetting("KeyboardScrollSpeedFactor", FormatFloat(std::clamp(options.keyboardScrollSpeed, 0.25f, 3.0f)));
	insertSetting("TerrainDrawDistanceScale", FormatFloat(std::clamp(options.terrainDrawDistanceScale, 0.75f, 2.0f)));
	insertSetting("UseFPSLimit", YesNo(options.useFpsLimit));
	insertSetting("FramesPerSecondLimit", std::to_string(std::clamp(options.framesPerSecondLimit, 30, 360)));
	std::ostringstream output;
	for (const IniLine &line : lines) output << line.original << '\n';
	return output.str();
}

std::string YesNo(bool value)
{
	return value ? "yes" : "no";
}

std::string LanguageName(LanguageMode language)
{
	switch (language) {
		case LanguageMode::English: return "english";
		case LanguageMode::Russian: return "russian";
		default: return "system";
	}
}

std::string WindowModeName(LauncherWindowMode mode)
{
	return mode == LauncherWindowMode::Fullscreen ? "fullscreen" : "windowed";
}

LanguageMode ParseLanguage(const std::string &value)
{
	const std::string normalized = ToLower(Trim(value));
	if (normalized == "english") return LanguageMode::English;
	if (normalized == "russian") return LanguageMode::Russian;
	return LanguageMode::System;
}

LauncherWindowMode ParseWindowMode(const std::string &value)
{
	return ToLower(Trim(value)) == "fullscreen" ? LauncherWindowMode::Fullscreen : LauncherWindowMode::Windowed;
}

std::string ValueOr(const std::unordered_map<std::string, std::string> &values,
	const std::string &key, const std::string &fallback)
{
	const auto found = values.find(ToLower(key));
	return found == values.end() ? fallback : found->second;
}

void ReplaceOrAppend(std::vector<IniLine> &lines, const std::string &key, const std::string &value)
{
	const std::string normalized = ToLower(key);
	bool replaced = false;
	for (IniLine &line : lines) {
		if (line.key == normalized) {
			line.original = key + " = " + value;
			line.value = value;
			replaced = true;
		}
	}
	if (!replaced) lines.push_back({key + " = " + value, normalized, value});
}

bool ContainsArgument(const std::vector<std::string> &arguments, const std::string &needle)
{
	const std::string normalizedNeedle = ToLower(needle);
	return std::any_of(arguments.begin(), arguments.end(), [&](const std::string &argument) {
		return ToLower(argument) == normalizedNeedle;
	});
}

bool TokenizeArguments(const std::string &text, std::vector<std::string> &arguments, std::string &errorMessage)
{
	std::string current;
	char quote = '\0';
	for (size_t index = 0; index < text.size(); ++index) {
		const char character = text[index];
		if (character == '\\') {
			if (index + 1 < text.size()) {
				const char next = text[index + 1];
				if (next == '\\' || (quote != '\0' && next == quote) ||
					(quote == '\0' && std::isspace(static_cast<unsigned char>(next)))) {
					current.push_back(next);
					++index;
					continue;
				}
			}
			current.push_back(character);
			continue;
		}
		if (quote != '\0') {
			if (character == quote) quote = '\0';
			else current.push_back(character);
			continue;
		}
		if (character == '\'' || character == '"') {
			quote = character;
		} else if (std::isspace(static_cast<unsigned char>(character))) {
			if (!current.empty()) {
				arguments.push_back(current);
				current.clear();
			}
		} else {
			current.push_back(character);
		}
	}
	if (quote != '\0') {
		errorMessage = "Additional launch arguments contain an unmatched quote";
		return false;
	}
	if (!current.empty()) arguments.push_back(current);
	return true;
}

} // namespace

LauncherSettings LoadLauncherSettings(const fs::path &path, std::string &errorMessage)
{
	errorMessage.clear();
	LauncherSettings settings;
	std::error_code fileError;
	if (!fs::exists(path, fileError)) {
		if (fileError) errorMessage = "Cannot inspect launcher settings: " + fileError.message();
		return settings;
	}

	std::ifstream input(path);
	if (!input) {
		errorMessage = "Cannot read launcher settings";
		return settings;
	}
	std::string section;
	std::string line;
	while (std::getline(input, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line.front() == '[' && line.back() == ']') {
			section = ToLower(Trim(line.substr(1, line.size() - 2)));
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos) continue;
		const std::string key = ToLower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));
		if (section == "launcher") {
			if (key == "language") settings.language = ParseLanguage(value);
			else if (key == "windowmode") settings.windowMode = ParseWindowMode(value);
			else if (key == "windowwidth") settings.windowWidth = ParseInteger(value, settings.windowWidth, 640, 16384);
			else if (key == "windowheight") settings.windowHeight = ParseInteger(value, settings.windowHeight, 480, 16384);
			continue;
		}
		ProfileLaunchSettings *profile = section == "generals" ? &settings.generals :
			(section == "zerohour" ? &settings.zeroHour : nullptr);
		if (!profile) continue;
		if (key == "windowed") profile->windowed = ParseBool(value, profile->windowed);
		else if (key == "quickstart") profile->quickStart = ParseBool(value, profile->quickStart);
		else if (key == "noshellmap") profile->noShellMap = ParseBool(value, profile->noShellMap);
		else if (key == "russianlocalization") profile->russianLocalization = ParseBool(value, profile->russianLocalization);
		// GeneralsArsenal @refactor Codex 14/08/2026 Ignore the V1 ActiveModification key so vanilla profile buttons stay clean.
		else if (key == "additionalarguments") profile->additionalArguments = value;
	}
	return settings;
}

bool SaveLauncherSettings(const fs::path &path, const LauncherSettings &settings, std::string &errorMessage)
{
	return WriteAtomically(path, SerializeLauncherSettings(settings), errorMessage);
}

GameOptions LoadGameOptions(const fs::path &path, std::string &errorMessage)
{
	errorMessage.clear();
	GameOptions options;
	std::error_code fileError;
	if (!fs::exists(path, fileError)) {
		if (fileError) errorMessage = "Cannot inspect Options.ini: " + fileError.message();
		return options;
	}
	const std::vector<IniLine> lines = ReadIniLines(path);
	const auto values = ToValueMap(lines);
	const uintmax_t fileSize = fs::file_size(path, fileError);
	if (fileError) {
		errorMessage = "Cannot inspect Options.ini size: " + fileError.message();
		return options;
	}
	if (values.empty() && fileSize > 0) {
		errorMessage = "Options.ini contains no readable settings";
		return options;
	}

	std::istringstream resolution(ValueOr(values, "Resolution", "1280 800"));
	int width = options.resolutionWidth;
	int height = options.resolutionHeight;
	if (resolution >> width >> height && width >= 640 && width <= 16384 && height >= 480 && height <= 16384) {
		options.resolutionWidth = width;
		options.resolutionHeight = height;
	}
	options.maxParticleCount = ParseInteger(ValueOr(values, "MaxParticleCount", "2500"), 2500, 100, 10000);
	options.textureReduction = ParseInteger(ValueOr(values, "TextureReduction", "1"), 1, 0, 2);
	options.useShadowVolumes = ParseBool(ValueOr(values, "UseShadowVolumes", "no"), false);
	options.buildingOcclusion = ParseBool(ValueOr(values, "BuildingOcclusion", "no"), false);
	options.useShadowDecals = ParseBool(ValueOr(values, "UseShadowDecals", "yes"), true);
	options.showTrees = ParseBool(ValueOr(values, "ShowTrees", "no"), false);
	options.useCloudMap = ParseBool(ValueOr(values, "UseCloudMap", "no"), false);
	options.extraAnimations = ParseBool(ValueOr(values, "ExtraAnimations", "no"), false);
	options.useLightMap = ParseBool(ValueOr(values, "UseLightMap", "no"), false);
	options.dynamicLOD = ParseBool(ValueOr(values, "DynamicLOD", "yes"), true);
	options.showSoftWaterEdge = ParseBool(ValueOr(values, "ShowSoftWaterEdge", "no"), false);
	options.heatEffects = ParseBool(ValueOr(values, "HeatEffects", "no"), false);
	options.useAlternateMouse = ParseBool(ValueOr(values, "UseAlternateMouse", "no"), false);
	return options;
}

bool SaveGameOptions(const fs::path &path, const GameOptions &options, std::string &errorMessage)
{
	return WriteAtomically(path, SerializeGameOptions(path, options), errorMessage);
}

SagePatchOptions LoadSagePatchOptions(const fs::path &path, std::string &errorMessage)
{
	errorMessage.clear();
	SagePatchOptions options;
	std::error_code fileError;
	if (!fs::exists(path, fileError)) {
		if (fileError) errorMessage = "Cannot inspect SagePatch.ini: " + fileError.message();
		return options;
	}
	const std::vector<IniLine> lines = ReadIniLines(path);
	const auto values = ToBlockValueMap(lines, "GameData");
	const uintmax_t fileSize = fs::file_size(path, fileError);
	if (fileError) {
		errorMessage = "Cannot inspect SagePatch.ini size: " + fileError.message();
		return options;
	}
	if (values.empty() && fileSize > 0) {
		errorMessage = "SagePatch.ini contains no readable settings";
		return options;
	}
	options.maxCameraHeight = ParseFloat(ValueOr(values, "MaxCameraHeight", "350"), 350.0f, 250.0f, 1000.0f);
	options.minCameraHeight = ParseFloat(ValueOr(values, "MinCameraHeight", "100"), 100.0f, 40.0f, 250.0f);
	options.enforceMaxCameraHeight = ParseBool(ValueOr(values, "EnforceMaxCameraHeight", "no"), false);
	options.keyboardScrollSpeed = ParseFloat(ValueOr(values, "KeyboardScrollSpeedFactor", "1"), 1.0f, 0.25f, 3.0f);
	options.terrainDrawDistanceScale = ParseFloat(ValueOr(values, "TerrainDrawDistanceScale", "1.05"), 1.05f, 0.75f, 2.0f);
	options.useFpsLimit = ParseBool(ValueOr(values, "UseFPSLimit", "yes"), true);
	options.framesPerSecondLimit = ParseInteger(ValueOr(values, "FramesPerSecondLimit", "60"), 60, 30, 360);
	if (options.minCameraHeight > options.maxCameraHeight) options.minCameraHeight = options.maxCameraHeight;
	return options;
}

bool SaveSagePatchOptions(const fs::path &path, const SagePatchOptions &options, std::string &errorMessage)
{
	return WriteAtomically(path, SerializeSagePatchOptions(path, options), errorMessage);
}

bool SaveSettingsBundle(const fs::path &root, const LauncherSettings &launcher,
	const GameOptions &generalsOptions, const GameOptions &zeroHourOptions,
	const SagePatchOptions &generalsSagePatch, const SagePatchOptions &zeroHourSagePatch,
	std::string &errorMessage)
{
	std::vector<std::string> arguments;
	if (!AppendProfileArguments(launcher.generals, arguments, errorMessage)) return false;
	arguments.clear();
	if (!AppendProfileArguments(launcher.zeroHour, arguments, errorMessage)) return false;
	const fs::path generalsOptionsPath = root / "UserData" / "Generals" / "Options.ini";
	const fs::path zeroHourOptionsPath = root / "UserData" / "GeneralsZH" / "Options.ini";
	const fs::path generalsSagePatchPath = root / "UserData" / "Generals" / "SagePatch.ini";
	const fs::path zeroHourSagePatchPath = root / "UserData" / "GeneralsZH" / "SagePatch.ini";
	return PublishSettingsFiles({
		{generalsOptionsPath, SerializeGameOptions(generalsOptionsPath, generalsOptions)},
		{zeroHourOptionsPath, SerializeGameOptions(zeroHourOptionsPath, zeroHourOptions)},
		{generalsSagePatchPath, SerializeSagePatchOptions(generalsSagePatchPath, generalsSagePatch)},
		{zeroHourSagePatchPath, SerializeSagePatchOptions(zeroHourSagePatchPath, zeroHourSagePatch)},
		{root / "Launcher" / "Settings.ini", SerializeLauncherSettings(launcher)}
	}, root / "Launcher" / "Settings.transaction", errorMessage);
}

bool RecoverInterruptedSettingsBundle(const fs::path &root, std::string &errorMessage)
{
	errorMessage.clear();
	const fs::path journalPath = root / "Launcher" / "Settings.transaction";
	std::error_code error;
	if (!fs::exists(journalPath, error)) {
		if (error) errorMessage = "Cannot inspect settings transaction: " + error.message();
		return !error;
	}
	std::ifstream journal(journalPath);
	std::string transaction;
	if (!std::getline(journal, transaction) || transaction.empty()) {
		errorMessage = "Settings transaction journal is damaged";
		return false;
	}
	const fs::path targets[] = {
		root / "UserData" / "Generals" / "Options.ini",
		root / "UserData" / "GeneralsZH" / "Options.ini",
		root / "UserData" / "Generals" / "SagePatch.ini",
		root / "UserData" / "GeneralsZH" / "SagePatch.ini",
		root / "Launcher" / "Settings.ini"
	};
	std::vector<bool> hadTarget;
	for (size_t index = 0; index < std::size(targets); ++index) {
		std::string state;
		if (!std::getline(journal, state) || (state != "0" && state != "1")) {
			errorMessage = "Settings transaction journal is incomplete";
			return false;
		}
		hadTarget.push_back(state == "1");
	}
	for (size_t index = 0; index < std::size(targets); ++index) {
		const fs::path backup = targets[index].string() + ".backup-" + transaction;
		const fs::path staged = targets[index].string() + ".stage-" + transaction;
		if (fs::exists(backup, error)) {
			fs::remove(targets[index], error);
			if (!SDL_RenamePath(backup.string().c_str(), targets[index].string().c_str())) {
				errorMessage = std::string("Cannot restore interrupted settings transaction: ") + SDL_GetError();
				return false;
			}
		} else if (!hadTarget[index]) {
			fs::remove(targets[index], error);
		}
		fs::remove(staged, error);
	}
	fs::remove(journalPath, error);
	if (error) {
		errorMessage = "Cannot finish settings recovery: " + error.message();
		return false;
	}
	return true;
}

ProfileLaunchSettings &SettingsForProfile(LauncherSettings &settings, const std::string &profileId)
{
	return ToLower(profileId) == "zerohour" ? settings.zeroHour : settings.generals;
}

const ProfileLaunchSettings &SettingsForProfile(const LauncherSettings &settings, const std::string &profileId)
{
	return ToLower(profileId) == "zerohour" ? settings.zeroHour : settings.generals;
}

bool AppendProfileArguments(const ProfileLaunchSettings &settings,
	std::vector<std::string> &arguments, std::string &errorMessage)
{
	std::vector<std::string> configured;
	if (!TokenizeArguments(settings.additionalArguments, configured, errorMessage)) return false;
	if (settings.windowed && !ContainsArgument(arguments, "-win") && !ContainsArgument(configured, "-win")) {
		arguments.emplace_back("-win");
	}
	// GeneralsArsenal @feature Codex 13/08/2026 The shared engine parser supports quick start in both game branches.
	if (settings.quickStart && !ContainsArgument(arguments, "-quickstart") && !ContainsArgument(configured, "-quickstart")) {
		arguments.emplace_back("-quickstart");
	}
	if (settings.noShellMap && !ContainsArgument(arguments, "-noshellmap") && !ContainsArgument(configured, "-noshellmap")) {
		arguments.emplace_back("-noshellmap");
	}
	arguments.insert(arguments.end(), configured.begin(), configured.end());
	return true;
}

} // namespace GeneralsArsenalLauncher
