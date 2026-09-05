/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherIntegration/BrandIdentity.h"
#include "LauncherIntegration/EngineModuleAPI.h"
#include "LauncherInstaller.h"
#include "LauncherDataImport.h"
#include "LauncherModProfiles.h"
#include "LauncherMods.h"
#include "LauncherSettings.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>


namespace fs = std::filesystem;


namespace
{

constexpr int kLogicalWidth = 1600;
constexpr int kLogicalHeight = 1000;
constexpr const char *kGeneralsProfileId = "generals";
constexpr const char *kZeroHourProfileId = "zerohour";
constexpr int kWorkerExitClean = 0;
constexpr int kWorkerExitRecoverableGraphicsFailure = 75;
constexpr int kMaximumRecoveriesPerMinute = 3;

std::atomic_bool g_sdlVideoConnectionFailed{false};
SDL_LogOutputFunction g_previousSDLLogOutput = nullptr;
void *g_previousSDLLogUserData = nullptr;

void SDLCALL LauncherSDLLogOutput(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	if (category == SDL_LOG_CATEGORY_VIDEO && message && std::strstr(message, "Wayland display connection closed")) {
		g_sdlVideoConnectionFailed.store(true, std::memory_order_release);
		if (g_previousSDLLogOutput) {
			g_previousSDLLogOutput(g_previousSDLLogUserData, category, priority, message);
		} else {
			SDL_GetDefaultLogOutputFunction()(userdata, category, priority, message);
		}
		fflush(stderr);
		// Echelon @bugfix Codex 13/08/2026 Never let SDL's synthetic quit event disguise a poisoned Wayland connection as a clean user exit.
		std::_Exit(kWorkerExitRecoverableGraphicsFailure);
	}
	if (g_previousSDLLogOutput) {
		g_previousSDLLogOutput(g_previousSDLLogUserData, category, priority, message);
	} else {
		SDL_GetDefaultLogOutputFunction()(userdata, category, priority, message);
	}
}

struct LauncherPaths
{
	fs::path home;
	fs::path root;
	fs::path launcherData;
	fs::path profiles;
	fs::path mods;
	fs::path settings;
};

struct LauncherProfile
{
	std::string id;
	std::string engine;
	std::string nameEn;
	std::string nameRu;
	std::string modPath;
	fs::path assetRoot;
	fs::path baseAssetRoot;
	fs::path userDataRoot;
	bool enabled = false;
	bool hasRussianLocalization = false;
};

struct MigrationResult
{
	bool success = false;
	std::string message;
};

struct LoadedModule
{
	SDL_SharedObject *handle = nullptr;
	const EchelonEngineModuleV2 *api = nullptr;
};

struct FolderDialogState
{
	std::mutex mutex;
	bool pending = false;
	bool completed = false;
	std::optional<fs::path> selected;
	std::string error;
	std::string defaultLocation;
};

struct FileDialogState
{
	std::mutex mutex;
	bool pending = false;
	bool completed = false;
	std::vector<fs::path> selected;
	std::string error;
	std::string defaultLocation;
};

std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string Trim(const std::string &value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

LauncherPaths BuildLauncherPaths()
{
	const char *homeValue = std::getenv("HOME");
	fs::path home = homeValue && homeValue[0] ? fs::path(homeValue) : fs::current_path();
	const fs::path root = home / EchelonBrand::kDataDirectory;
	return {
		home,
		root,
		root / "Launcher",
		root / "Profiles",
		root / "Mods",
		root / "Launcher" / "Settings.ini"
	};
}

bool FileNameEquals(const fs::path &directory, const std::string &expected)
{
	std::error_code error;
	if (!fs::is_directory(directory, error)) {
		return false;
	}
	const std::string expectedLower = ToLower(expected);
	for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
		if (error) {
			return false;
		}
		if (ToLower(entry.path().filename().string()) == expectedLower && entry.is_regular_file(error)) {
			return true;
		}
	}
	return false;
}

bool IsGeneralsAssetRoot(const fs::path &path)
{
	return FileNameEquals(path, "INI.big");
}

bool IsZeroHourAssetRoot(const fs::path &path)
{
	return FileNameEquals(path, "INIZH.big");
}

bool HasRussianLocalization(const fs::path &path)
{
	std::error_code error;
	if (!fs::is_directory(path, error)) {
		return false;
	}
	for (const fs::directory_entry &entry : fs::directory_iterator(path, error)) {
		if (error || !entry.is_regular_file(error)) {
			continue;
		}
		const std::string name = ToLower(entry.path().filename().string());
		if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".big") == 0 && name.find("russian") != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::string Timestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t value = std::chrono::system_clock::to_time_t(now);
	std::tm tmValue{};
#if defined(_WIN32)
	localtime_s(&tmValue, &value);
#else
	localtime_r(&value, &tmValue);
#endif
	char buffer[32] = {};
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tmValue);
	return buffer;
}

void WriteProfileIfMissing(const fs::path &path, const std::string &contents)
{
	std::error_code error;
	if (fs::exists(path, error)) {
		return;
	}
	std::ofstream output(path);
	if (output) {
		output << contents;
	}
}

bool EnsureLayout(const LauncherPaths &paths, std::string &errorMessage)
{
	std::error_code error;
	for (const fs::path &path : {
		paths.root / "Generals",
		paths.root / "GeneralsZH",
		paths.root / "UserData" / "Generals",
		paths.root / "UserData" / "GeneralsZH",
		paths.profiles,
		paths.mods,
		paths.mods / "Installed" / "generals" / "mod",
		paths.mods / "Installed" / "generals" / "patch",
		paths.mods / "Installed" / "generals" / "addon",
		paths.mods / "Installed" / "zerohour" / "mod",
		paths.mods / "Installed" / "zerohour" / "patch",
		paths.mods / "Installed" / "zerohour" / "addon",
		paths.mods / ".staging",
		paths.mods / ".trash",
		paths.mods / "Cache",
		paths.launcherData}) {
		fs::create_directories(path, error);
		if (error) {
			errorMessage = "Cannot create " + path.string() + ": " + error.message();
			return false;
		}
	}

	WriteProfileIfMissing(paths.profiles / "generals.ini",
		"[Profile]\nSchemaVersion=1\nId=generals\nEngine=generals\nName.en=Command & Conquer: Generals\n"
		"Name.ru=Command & Conquer: Generals\nModPath=\n");
	WriteProfileIfMissing(paths.profiles / "zerohour.ini",
		"[Profile]\nSchemaVersion=1\nId=zerohour\nEngine=zerohour\nName.en=Command & Conquer: Generals - Zero Hour\n"
		"Name.ru=Command & Conquer: Generals — Час расплаты\nModPath=\n");
	return true;
}

LauncherProfile ReadProfile(const fs::path &profilePath, const LauncherPaths &paths)
{
	LauncherProfile profile;
	std::ifstream input(profilePath);
	std::string section;
	std::string line;
	int schemaVersion = 0;
	while (std::getline(input, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#') {
			continue;
		}
		if (line.front() == '[' && line.back() == ']') {
			section = ToLower(Trim(line.substr(1, line.size() - 2)));
			continue;
		}
		if (section != "profile") {
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos) {
			continue;
		}
		const std::string key = ToLower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));
		if (key == "schemaversion") schemaVersion = std::atoi(value.c_str());
		else if (key == "id") profile.id = ToLower(value);
		else if (key == "engine") profile.engine = ToLower(value);
		else if (key == "name.en") profile.nameEn = value;
		else if (key == "name.ru") profile.nameRu = value;
		else if (key == "modpath") profile.modPath = value;
	}

	if (schemaVersion != 1 || (profile.id != kGeneralsProfileId && profile.id != kZeroHourProfileId) || profile.engine != profile.id) {
		return {};
	}
	if (profile.id == kGeneralsProfileId) {
		profile.assetRoot = paths.root / "Generals";
		profile.baseAssetRoot = profile.assetRoot;
		profile.userDataRoot = paths.root / "UserData" / "Generals";
		profile.enabled = IsGeneralsAssetRoot(profile.assetRoot);
	} else {
		profile.assetRoot = paths.root / "GeneralsZH";
		profile.baseAssetRoot = paths.root / "Generals";
		profile.userDataRoot = paths.root / "UserData" / "GeneralsZH";
		profile.enabled = IsZeroHourAssetRoot(profile.assetRoot) && IsGeneralsAssetRoot(profile.baseAssetRoot);
	}
	profile.hasRussianLocalization = HasRussianLocalization(profile.assetRoot);
	return profile;
}

std::vector<LauncherProfile> LoadProfiles(const LauncherPaths &paths)
{
	std::vector<LauncherProfile> profiles;
	for (const char *name : {"generals.ini", "zerohour.ini"}) {
		LauncherProfile profile = ReadProfile(paths.profiles / name, paths);
		if (!profile.id.empty()) {
			profiles.push_back(std::move(profile));
		}
	}
	return profiles;
}

MigrationResult ImportSelectedData(const LauncherPaths &paths, const fs::path &selected)
{
	std::error_code error;
	if (!fs::is_directory(selected, error)) {
		return {false, "The selected path is not a directory"};
	}
	fs::path generalsSource;
	fs::path zeroHourSource;
	if (IsGeneralsAssetRoot(selected / "Generals")) generalsSource = selected / "Generals";
	else if (IsGeneralsAssetRoot(selected)) generalsSource = selected;
	if (IsZeroHourAssetRoot(selected / "GeneralsZH")) zeroHourSource = selected / "GeneralsZH";
	else if (IsZeroHourAssetRoot(selected / "GeneralsMD")) zeroHourSource = selected / "GeneralsMD";
	else if (IsZeroHourAssetRoot(selected)) zeroHourSource = selected;
	if (generalsSource.empty() && zeroHourSource.empty()) {
		return {false, "No INI.big or INIZH.big was found in the selected directory"};
	}

	std::string errorMessage;
	if (!EnsureLayout(paths, errorMessage)) {
		return {false, errorMessage};
	}
	const fs::path journal = paths.launcherData / "migration.log";
	const fs::path backup = paths.root / "MigrationBackup" / Timestamp();
	if (!generalsSource.empty() && !fs::equivalent(generalsSource, paths.root / "Generals", error)) {
		if (!EchelonLauncher::CopyRetailDataTree(generalsSource, paths.root / "Generals", backup / "Generals", journal, errorMessage)) {
			return {false, errorMessage};
		}
	}
	error.clear();
	if (!zeroHourSource.empty() && !fs::equivalent(zeroHourSource, paths.root / "GeneralsZH", error)) {
		if (!EchelonLauncher::CopyRetailDataTree(zeroHourSource, paths.root / "GeneralsZH", backup / "GeneralsZH", journal, errorMessage)) {
			return {false, errorMessage};
		}
	}
	return {true, "Game data imported successfully"};
}

bool IsRussianLocale()
{
	for (const char *variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
		if (const char *value = std::getenv(variable); value && value[0]) {
			const std::string locale = ToLower(value);
			return locale.compare(0, 2, "ru") == 0;
		}
	}
	return false;
}

// GeneralsX @bugfix Codex 11/08/2026 Exclude Lavapipe before the host initializes Vulkan for DXVK.
void FilterSoftwareVulkanICDs()
{
#if defined(__linux__)
	if (std::getenv("VK_DRIVER_FILES") || std::getenv("VK_ICD_FILENAMES")) {
		return;
	}
	std::vector<std::string> gpuVendors;
	std::error_code drmError;
	const fs::path drmRoot("/sys/class/drm");
	if (fs::is_directory(drmRoot, drmError)) {
		for (const fs::directory_entry &entry : fs::directory_iterator(drmRoot, drmError)) {
			const std::string deviceName = entry.path().filename().string();
			if (deviceName.compare(0, 4, "card") != 0 || deviceName.find('-') != std::string::npos) continue;
			std::ifstream vendorFile(entry.path() / "device" / "vendor");
			std::string vendor;
			if (vendorFile >> vendor) gpuVendors.push_back(ToLower(vendor));
		}
	}
	auto matchesDetectedGpu = [&gpuVendors](const std::string &name) {
		if (gpuVendors.empty()) return true;
		for (const std::string &vendor : gpuVendors) {
			if (vendor == "0x1002" && name.find("radeon") != std::string::npos) return true;
			if (vendor == "0x8086" && name.find("intel") != std::string::npos) return true;
			if (vendor == "0x10de" && (name.find("nvidia") != std::string::npos || name.find("nouveau") != std::string::npos)) return true;
			if (vendor == "0x1af4" && name.find("virtio") != std::string::npos) return true;
		}
		return false;
	};

	std::vector<fs::path> hardwareDrivers;
	for (const fs::path &directory : {
		fs::path("/usr/share/vulkan/icd.d"),
		fs::path("/etc/vulkan/icd.d"),
		fs::path("/usr/lib64/GL/vulkan/icd.d"),
		fs::path("/usr/lib/x86_64-linux-gnu/GL/vulkan/icd.d")}) {
		std::error_code error;
		if (!fs::is_directory(directory, error)) continue;
		for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
			if (error || !entry.is_regular_file(error) || ToLower(entry.path().extension().string()) != ".json") continue;
			const std::string name = ToLower(entry.path().filename().string());
			if (name.find("i686") != std::string::npos || name.find("i586") != std::string::npos ||
				name.find("i386") != std::string::npos || !matchesDetectedGpu(name)) continue;
			if (name.find("lvp") != std::string::npos || name.find("lavapipe") != std::string::npos ||
				name.find("llvmpipe") != std::string::npos || name.find("softpipe") != std::string::npos) {
				continue;
			}
			hardwareDrivers.push_back(entry.path());
		}
	}
	std::sort(hardwareDrivers.begin(), hardwareDrivers.end());
	hardwareDrivers.erase(std::unique(hardwareDrivers.begin(), hardwareDrivers.end()), hardwareDrivers.end());
	if (hardwareDrivers.empty()) return;
	std::string driverList;
	for (const fs::path &driver : hardwareDrivers) {
		if (!driverList.empty()) driverList += ':';
		driverList += driver.string();
	}
	setenv("VK_DRIVER_FILES", driverList.c_str(), 1);
	fprintf(stderr, "INFO: Echelon Vulkan ICD filter: %s\n", driverList.c_str());
	fflush(stderr);
#endif
}

// GeneralsX @feature Codex 11/08/2026 Render launcher text from a system font without shipping retail assets.
class LauncherFont
{
public:
	bool initialize(SDL_Renderer *renderer)
	{
		reset();
		const char *overrideFont = std::getenv("ECHELON_LAUNCHER_FONT");
		std::vector<fs::path> candidates;
		if (overrideFont && overrideFont[0]) candidates.emplace_back(overrideFont);
		const char *basePath = SDL_GetBasePath();
		const fs::path executableDirectory = basePath && basePath[0] ? fs::path(basePath) : fs::current_path();
		// Echelon @bugfix Codex 12/08/2026 Prefer the packaged launcher font and cover Freedesktop runtime layouts.
		candidates.insert(candidates.end(), {
			executableDirectory / ECHELON_LAUNCHER_FONT_FILE,
			executableDirectory / ".." / "share" / EchelonBrand::kInstallDataDirectory /
				ECHELON_LAUNCHER_FONT_FILE,
			executableDirectory / ".." / "Resources" / ECHELON_LAUNCHER_FONT_FILE,
			"/usr/share/fonts/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/ttf/google-noto-vf/NotoSans-VF.ttf",
			"/usr/share/fonts/ttf/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
			"/run/host/fonts/dejavu/DejaVuSans.ttf",
			"/run/host/fonts/ttf/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
			"/System/Library/Fonts/Supplemental/Arial.ttf"
		});
		fs::path selected;
		for (const fs::path &candidate : candidates) {
			if (fs::is_regular_file(candidate)) {
				selected = candidate;
				break;
			}
		}
		if (selected.empty()) {
			fprintf(stderr, "WARNING: Cannot find a launcher font; button labels will be unavailable\n");
			fflush(stderr);
			return false;
		}
		fprintf(stderr, "INFO: Echelon launcher font: %s\n", selected.string().c_str());
		fflush(stderr);

		std::ifstream input(selected, std::ios::binary | std::ios::ate);
		if (!input) return false;
		const std::streamsize size = input.tellg();
		input.seekg(0);
		m_fontBytes.resize(static_cast<size_t>(size));
		if (!input.read(reinterpret_cast<char *>(m_fontBytes.data()), size)) return false;

		constexpr int atlasWidth = 1024;
		constexpr int atlasHeight = 1024;
		std::vector<unsigned char> alpha(atlasWidth * atlasHeight, 0);
		stbtt_pack_context context{};
		if (!stbtt_PackBegin(&context, alpha.data(), atlasWidth, atlasHeight, 0, 1, nullptr)) return false;
		stbtt_PackSetOversampling(&context, 1, 1);
		const int asciiOk = stbtt_PackFontRange(&context, m_fontBytes.data(), 0, 42.0f, 32, 95, m_ascii);
		const int cyrillicOk = stbtt_PackFontRange(&context, m_fontBytes.data(), 0, 42.0f, 0x400, 256, m_cyrillic);
		stbtt_PackEnd(&context);
		if (!asciiOk || !cyrillicOk) return false;

		std::vector<uint32_t> pixels(alpha.size());
		for (size_t index = 0; index < alpha.size(); ++index) {
			pixels[index] = 0xFFFFFF00u | alpha[index];
		}
		m_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, atlasWidth, atlasHeight);
		if (!m_texture) return false;
		SDL_UpdateTexture(m_texture, nullptr, pixels.data(), atlasWidth * static_cast<int>(sizeof(uint32_t)));
		SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
		m_renderer = renderer;
		m_atlasWidth = atlasWidth;
		m_atlasHeight = atlasHeight;
		return true;
	}

	void reset()
	{
		if (m_texture) SDL_DestroyTexture(m_texture);
		m_texture = nullptr;
		m_renderer = nullptr;
		m_fontBytes.clear();
	}

	~LauncherFont()
	{
		reset();
	}

	bool valid() const { return m_texture != nullptr; }

	float measure(const std::string &text, float size) const
	{
		float rawX = 0.0f;
	for (size_t offset = 0; offset < text.size();) {
			const uint32_t codepoint = DecodeUtf8(text, offset);
			const stbtt_packedchar *glyph = FindGlyph(codepoint);
			if (glyph) rawX += glyph->xadvance;
		}
		return rawX * (size / 42.0f);
	}

	void draw(const std::string &text, float x, float baseline, float size, SDL_Color color, bool centered = false)
	{
		if (!m_texture || !m_renderer) {
			return;
		}
		const float scale = size / 42.0f;
		if (centered) x -= measure(text, size) * 0.5f;
		float rawX = 0.0f;
		float rawY = 0.0f;
		SDL_SetTextureColorMod(m_texture, color.r, color.g, color.b);
		SDL_SetTextureAlphaMod(m_texture, color.a);
		for (size_t offset = 0; offset < text.size();) {
			const uint32_t codepoint = DecodeUtf8(text, offset);
			const stbtt_packedchar *glyph = FindGlyph(codepoint);
			if (!glyph) continue;
			stbtt_aligned_quad quad{};
			stbtt_GetPackedQuad(glyph, m_atlasWidth, m_atlasHeight, 0, &rawX, &rawY, &quad, 1);
			const SDL_FRect source{quad.s0 * m_atlasWidth, quad.t0 * m_atlasHeight,
				(quad.s1 - quad.s0) * m_atlasWidth, (quad.t1 - quad.t0) * m_atlasHeight};
			const SDL_FRect destination{x + quad.x0 * scale, baseline + quad.y0 * scale,
				(quad.x1 - quad.x0) * scale, (quad.y1 - quad.y0) * scale};
			SDL_RenderTexture(m_renderer, m_texture, &source, &destination);
		}
	}

private:
	static uint32_t DecodeUtf8(const std::string &text, size_t &offset)
	{
		const unsigned char first = static_cast<unsigned char>(text[offset++]);
		if (first < 0x80) return first;
		if ((first & 0xE0) == 0xC0 && offset < text.size()) {
			return ((first & 0x1F) << 6) | (static_cast<unsigned char>(text[offset++]) & 0x3F);
		}
		if ((first & 0xF0) == 0xE0 && offset + 1 < text.size()) {
			const uint32_t value = ((first & 0x0F) << 12) |
				((static_cast<unsigned char>(text[offset]) & 0x3F) << 6) |
				(static_cast<unsigned char>(text[offset + 1]) & 0x3F);
			offset += 2;
			return value;
		}
		while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) ++offset;
		return '?';
	}

	const stbtt_packedchar *FindGlyph(uint32_t codepoint) const
	{
		if (codepoint >= 32 && codepoint < 127) return &m_ascii[codepoint - 32];
		if (codepoint >= 0x400 && codepoint < 0x500) return &m_cyrillic[codepoint - 0x400];
		return &m_ascii['?' - 32];
	}

	SDL_Renderer *m_renderer = nullptr;
	SDL_Texture *m_texture = nullptr;
	int m_atlasWidth = 0;
	int m_atlasHeight = 0;
	std::vector<unsigned char> m_fontBytes;
	stbtt_packedchar m_ascii[95]{};
	stbtt_packedchar m_cyrillic[256]{};
};

// Echelon @refactor Codex 13/08/2026 Keep the launcher's independent emerald HUD identity in one palette.
namespace LauncherPalette
{
constexpr SDL_Color kAccent{24, 214, 105, 255};
constexpr SDL_Color kAccentBright{82, 255, 151, 255};
constexpr SDL_Color kAccentSoft{117, 230, 165, 255};
constexpr SDL_Color kAccentMuted{42, 126, 78, 220};
constexpr SDL_Color kFrame{48, 164, 96, 225};
constexpr SDL_Color kFrameSoft{34, 108, 69, 145};
constexpr SDL_Color kGrid{22, 112, 67, 50};
constexpr SDL_Color kPanel{0, 10, 7, 224};
constexpr SDL_Color kPanelStrong{0, 8, 5, 242};
constexpr SDL_Color kControl{0, 13, 8, 220};
constexpr SDL_Color kControlHilite{2, 31, 17, 240};
constexpr SDL_Color kControlPressed{0, 7, 4, 248};
constexpr SDL_Color kText{220, 244, 229, 255};
constexpr SDL_Color kTextBright{238, 255, 244, 255};
constexpr SDL_Color kValue{137, 242, 178, 255};
constexpr SDL_Color kDisabledBorder{44, 76, 57, 255};
constexpr SDL_Color kDisabledText{90, 112, 98, 255};
constexpr SDL_Color kError{240, 110, 95, 255};
}

struct Button
{
	SDL_FRect rect{};
	std::string label;
	bool enabled = true;
	bool hilited = false;
	bool selected = false;
	bool focused = false;
	int transitionDelay = 0;
	int lastTransitionFrame = -1;
	SDL_Color accent{LauncherPalette::kAccent};
	SDL_Color enabledText{LauncherPalette::kText};
	SDL_Color hiliteText{LauncherPalette::kTextBright};
	SDL_Color disabledText{LauncherPalette::kDisabledText};
};

// GeneralsX @feature Codex 11/08/2026 Keep all launcher geometry in one logical composition derived from SAGE menu proportions.
struct LauncherLayout
{
	SDL_FRect frame{80.0f, 68.0f, 1440.0f, 865.0f};
	SDL_FRect innerFrame{86.0f, 74.0f, 1428.0f, 853.0f};
	SDL_FRect logo{1025.0f, 64.0f, 480.0f, 120.0f};
	SDL_FRect mainPanel{1020.0f, 140.0f, 490.0f, 395.0f};
	SDL_FRect generalsButton{1040.0f, 194.0f, 455.0f, 58.0f};
	SDL_FRect zeroHourButton{1040.0f, 260.0f, 455.0f, 58.0f};
	SDL_FRect modsButton{1040.0f, 326.0f, 455.0f, 58.0f};
	SDL_FRect settingsButton{1040.0f, 392.0f, 455.0f, 58.0f};
	SDL_FRect exitButton{1040.0f, 458.0f, 455.0f, 58.0f};
	SDL_FRect progressOverlay{470.0f, 410.0f, 660.0f, 150.0f};

	float mainCenterX() const { return mainPanel.x + mainPanel.w * 0.5f; }
};

constexpr LauncherLayout kLauncherLayout{};

// Echelon @feature Codex 13/08/2026 Give launcher and per-engine settings a full-size SAGE-styled workspace.
struct SettingsLayout
{
	SDL_FRect panel{155.0f, 105.0f, 1290.0f, 790.0f};
	SDL_FRect generalsTab{210.0f, 165.0f, 386.0f, 54.0f};
	SDL_FRect zeroHourTab{607.0f, 165.0f, 386.0f, 54.0f};
	SDL_FRect launcherTab{1004.0f, 165.0f, 386.0f, 54.0f};
	SDL_FRect content{210.0f, 245.0f, 1180.0f, 530.0f};
	SDL_FRect scrollUp{1305.0f, 263.0f, 67.0f, 72.0f};
	SDL_FRect scrollDown{1305.0f, 685.0f, 67.0f, 72.0f};
	SDL_FRect defaultsButton{210.0f, 810.0f, 260.0f, 54.0f};
	SDL_FRect cancelButton{850.0f, 810.0f, 250.0f, 54.0f};
	SDL_FRect applyButton{1110.0f, 810.0f, 280.0f, 54.0f};
};

constexpr SettingsLayout kSettingsLayout{};

// Echelon @feature Codex 14/08/2026 Reserve one full logical workspace for the native modification manager.
struct ModsLayout
{
	SDL_FRect panel{90.0f, 72.0f, 1420.0f, 858.0f};
	SDL_FRect generalsTab{130.0f, 125.0f, 290.0f, 52.0f};
	SDL_FRect zeroHourTab{430.0f, 125.0f, 290.0f, 52.0f};
	SDL_FRect modsTab{790.0f, 125.0f, 210.0f, 52.0f};
	SDL_FRect patchesTab{1010.0f, 125.0f, 210.0f, 52.0f};
	SDL_FRect addonsTab{1230.0f, 125.0f, 210.0f, 52.0f};
	SDL_FRect catalog{120.0f, 200.0f, 810.0f, 590.0f};
	SDL_FRect details{950.0f, 200.0f, 530.0f, 590.0f};
	SDL_FRect versionButton{970.0f, 640.0f, 490.0f, 42.0f};
	SDL_FRect verifyButton{970.0f, 690.0f, 235.0f, 42.0f};
	SDL_FRect coverButton{1215.0f, 690.0f, 245.0f, 42.0f};
	SDL_FRect folderButton{970.0f, 740.0f, 235.0f, 42.0f};
	SDL_FRect linkButton{1215.0f, 740.0f, 245.0f, 42.0f};
	SDL_FRect importButton{120.0f, 820.0f, 290.0f, 54.0f};
	SDL_FRect importFolderButton{420.0f, 820.0f, 280.0f, 54.0f};
	SDL_FRect removeButton{710.0f, 820.0f, 150.0f, 54.0f};
	SDL_FRect restoreButton{870.0f, 820.0f, 180.0f, 54.0f};
	SDL_FRect launchButton{1080.0f, 820.0f, 190.0f, 54.0f};
	SDL_FRect backButton{1280.0f, 820.0f, 200.0f, 54.0f};
};

constexpr ModsLayout kModsLayout{};

struct ModsContextLayout
{
	SDL_FRect panel{950.0f, 200.0f, 530.0f, 590.0f};
	SDL_FRect first{995.0f, 315.0f, 440.0f, 54.0f};
	SDL_FRect second{995.0f, 382.0f, 440.0f, 54.0f};
	SDL_FRect third{995.0f, 449.0f, 440.0f, 54.0f};
	SDL_FRect fourth{995.0f, 516.0f, 440.0f, 54.0f};
	SDL_FRect close{995.0f, 665.0f, 440.0f, 54.0f};
};

constexpr ModsContextLayout kModsContextLayout{};

enum class SettingsPage
{
	Generals,
	ZeroHour,
	Launcher
};

enum class ModsPage
{
	Mods,
	Patches,
	Addons
};

enum class ModsContextMode
{
	None,
	Folders,
	Links
};

enum class ModificationFileDialogPurpose
{
	Import,
	Cover
};

// Echelon @refactor Codex 05/09/2026 The product UI represents installed local content only.
struct ModificationViewItem
{
	const EchelonLauncher::InstalledModification *installed = nullptr;
	std::string id() const { return installed->id; }
	std::string name() const { return installed->name; }
	std::string version() const { return installed->version; }
	std::string sourceId() const { return installed->sourceId; }
	EchelonLauncher::ModificationType type() const { return installed->type; }
	const std::vector<std::string> &requirements() const { return installed->requirements; }
	const std::vector<std::string> &conflicts() const { return installed->conflicts; }
	fs::path coverPath() const { return installed->coverImagePath; }
};

enum class LauncherUISound
{
	ButtonsFadeIn,
	Click,
	DisabledClick
};

// GeneralsX @feature Codex 11/08/2026 Preserve the original SAGE button sound timing without distributing retail audio.
class LauncherUIAudio
{
public:
	bool initialize()
	{
		reset();
		SDL_AudioSpec specification{};
		specification.format = SDL_AUDIO_F32;
		specification.channels = 1;
		specification.freq = 48000;
		m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specification, nullptr, nullptr);
		if (!m_stream) return false;
		if (!SDL_ResumeAudioStreamDevice(m_stream)) {
			reset();
			return false;
	}
	return true;
}

	void play(LauncherUISound sound)
	{
		if (!m_stream) return;
		const int sampleRate = 48000;
		const int sampleCount = sound == LauncherUISound::ButtonsFadeIn ? 1344 : 1920;
		const float startFrequency = sound == LauncherUISound::ButtonsFadeIn ? 940.0f :
			(sound == LauncherUISound::Click ? 520.0f : 170.0f);
		const float endFrequency = sound == LauncherUISound::ButtonsFadeIn ? 1320.0f :
			(sound == LauncherUISound::Click ? 270.0f : 105.0f);
		const float volume = sound == LauncherUISound::DisabledClick ? 0.075f : 0.11f;
		std::vector<float> samples(static_cast<size_t>(sampleCount));
		float phase = 0.0f;
		for (int index = 0; index < sampleCount; ++index) {
			const float progress = static_cast<float>(index) / static_cast<float>(sampleCount);
			const float frequency = startFrequency + (endFrequency - startFrequency) * progress;
			phase += frequency / static_cast<float>(sampleRate);
			if (phase >= 1.0f) phase -= 1.0f;
			const float triangle = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
			const float attack = progress < 0.12f ? progress / 0.12f : 1.0f;
			const float envelope = attack * (1.0f - progress) * (1.0f - progress);
			samples[static_cast<size_t>(index)] = triangle * envelope * volume;
		}
		SDL_PutAudioStreamData(m_stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
	}

	void reset()
	{
		if (m_stream) SDL_DestroyAudioStream(m_stream);
		m_stream = nullptr;
	}

	~LauncherUIAudio()
	{
		reset();
	}

private:
	SDL_AudioStream *m_stream = nullptr;
};

// GeneralsX @feature Codex 11/08/2026 Load the original distributable launcher backdrop independently of retail data.
class LauncherBackdrop
{
public:
	bool initialize(SDL_Renderer *renderer)
	{
		reset();
		std::vector<fs::path> candidates;
		if (const char *overridePath = std::getenv("ECHELON_LAUNCHER_BACKGROUND"); overridePath && overridePath[0]) {
			candidates.emplace_back(overridePath);
		}
		const char *basePath = SDL_GetBasePath();
		const fs::path executableDirectory = basePath && basePath[0] ? fs::path(basePath) : fs::current_path();
		candidates.insert(candidates.end(), {
			executableDirectory / ECHELON_LAUNCHER_BACKGROUND_FILE,
			executableDirectory / ".." / "share" / EchelonBrand::kInstallDataDirectory /
				ECHELON_LAUNCHER_BACKGROUND_FILE,
			executableDirectory / ".." / "Resources" / ECHELON_LAUNCHER_BACKGROUND_FILE
		});
		for (const fs::path &candidate : candidates) {
			m_texture = IMG_LoadTexture(renderer, candidate.string().c_str());
			if (m_texture) {
				SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR);
				return true;
			}
		}
		fprintf(stderr, "WARNING: Cannot load launcher background: %s\n", SDL_GetError());
		fflush(stderr);
		return false;
	}

	void draw(SDL_Renderer *renderer) const
	{
		if (!m_texture) return;
		const SDL_FRect destination{0.0f, 0.0f, static_cast<float>(kLogicalWidth), static_cast<float>(kLogicalHeight)};
		SDL_RenderTexture(renderer, m_texture, nullptr, &destination);
	}

	void reset()
	{
		if (m_texture) SDL_DestroyTexture(m_texture);
		m_texture = nullptr;
	}

	bool valid() const { return m_texture != nullptr; }

	~LauncherBackdrop()
	{
		reset();
	}

private:
	SDL_Texture *m_texture = nullptr;
};

// Echelon @feature Codex 12/08/2026 Render the Echelon wordmark with aspect-preserving scaling.
class LauncherLogo
{
public:
	bool initialize(SDL_Renderer *renderer)
	{
		reset();
		std::vector<fs::path> candidates;
		if (const char *overridePath = std::getenv("ECHELON_LAUNCHER_LOGO"); overridePath && overridePath[0]) {
			candidates.emplace_back(overridePath);
		}
		const char *basePath = SDL_GetBasePath();
		const fs::path executableDirectory = basePath && basePath[0] ? fs::path(basePath) : fs::current_path();
		candidates.insert(candidates.end(), {
			executableDirectory / ECHELON_LAUNCHER_LOGO_FILE,
			executableDirectory / ".." / "share" / EchelonBrand::kInstallDataDirectory /
				ECHELON_LAUNCHER_LOGO_FILE,
			executableDirectory / ".." / "Resources" / ECHELON_LAUNCHER_LOGO_FILE
		});
		for (const fs::path &candidate : candidates) {
			m_texture = IMG_LoadTexture(renderer, candidate.string().c_str());
			if (m_texture && SDL_GetTextureSize(m_texture, &m_width, &m_height)) {
				SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR);
				return true;
			}
			reset();
		}
		fprintf(stderr, "WARNING: Cannot load launcher logo: %s\n", SDL_GetError());
		fflush(stderr);
		return false;
	}

	void draw(SDL_Renderer *renderer) const
	{
		if (!m_texture || m_width <= 0.0f || m_height <= 0.0f) return;
		const float scale = std::min(kLauncherLayout.logo.w / m_width, kLauncherLayout.logo.h / m_height);
		const SDL_FRect destination{
			kLauncherLayout.logo.x + (kLauncherLayout.logo.w - m_width * scale) * 0.5f,
			kLauncherLayout.logo.y + (kLauncherLayout.logo.h - m_height * scale) * 0.5f,
			m_width * scale,
			m_height * scale
		};
		SDL_RenderTexture(renderer, m_texture, nullptr, &destination);
	}

	void reset()
	{
		if (m_texture) SDL_DestroyTexture(m_texture);
		m_texture = nullptr;
		m_width = 0.0f;
		m_height = 0.0f;
	}

	bool valid() const { return m_texture != nullptr; }

	~LauncherLogo()
	{
		reset();
	}

private:
	SDL_Texture *m_texture = nullptr;
	float m_width = 0.0f;
	float m_height = 0.0f;
};

// Echelon @feature Codex 14/08/2026 Cache the exact color and grayscale card art variants used by the mod catalog.
class ModCoverCache
{
public:
	bool draw(SDL_Renderer *renderer, const fs::path &path, bool selected, const SDL_FRect &destination)
	{
		if (path.empty()) return false;
		const std::string key = path.string();
		auto found = m_entries.find(key);
		if (found == m_entries.end()) found = m_entries.emplace(key, load(renderer, path)).first;
		SDL_Texture *texture = selected ? found->second.color : found->second.grayscale;
		if (!texture) return false;
		return SDL_RenderTexture(renderer, texture, nullptr, &destination);
	}

	void reset()
	{
		for (auto &[key, entry] : m_entries) {
			if (entry.color) SDL_DestroyTexture(entry.color);
			if (entry.grayscale) SDL_DestroyTexture(entry.grayscale);
		}
		m_entries.clear();
	}

	~ModCoverCache()
	{
		reset();
	}

private:
	struct Entry
	{
		SDL_Texture *color = nullptr;
		SDL_Texture *grayscale = nullptr;
	};

	static Entry load(SDL_Renderer *renderer, const fs::path &path)
	{
		Entry entry;
		SDL_Surface *loaded = IMG_Load(path.string().c_str());
		if (!loaded) return entry;
		SDL_Surface *surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(loaded);
		if (!surface) return entry;
		entry.color = SDL_CreateTextureFromSurface(renderer, surface);
		if (entry.color) SDL_SetTextureScaleMode(entry.color, SDL_SCALEMODE_LINEAR);
		SDL_Surface *grayscale = SDL_DuplicateSurface(surface);
		if (grayscale) {
			for (int y = 0; y < grayscale->h; ++y) {
				for (int x = 0; x < grayscale->w; ++x) {
					Uint8 red = 0;
					Uint8 green = 0;
					Uint8 blue = 0;
					Uint8 alpha = 0;
					if (!SDL_ReadSurfacePixel(grayscale, x, y, &red, &green, &blue, &alpha)) continue;
					const Uint8 luminance = static_cast<Uint8>((54u * red + 183u * green + 19u * blue) >> 8u);
					SDL_WriteSurfacePixel(grayscale, x, y, luminance, luminance, luminance, alpha);
				}
			}
			entry.grayscale = SDL_CreateTextureFromSurface(renderer, grayscale);
			if (entry.grayscale) SDL_SetTextureScaleMode(entry.grayscale, SDL_SCALEMODE_LINEAR);
			SDL_DestroySurface(grayscale);
		}
		SDL_DestroySurface(surface);
		return entry;
	}

	std::unordered_map<std::string, Entry> m_entries;
};

bool Contains(const SDL_FRect &rect, float x, float y)
{
	return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

void DrawFilledRect(SDL_Renderer *renderer, const SDL_FRect &rect, SDL_Color color)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	SDL_RenderFillRect(renderer, &rect);
}

void DrawOutline(SDL_Renderer *renderer, const SDL_FRect &rect, SDL_Color color, int thickness = 1)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	for (int index = 0; index < thickness; ++index) {
		SDL_FRect line{rect.x + index, rect.y + index, rect.w - index * 2.0f, rect.h - index * 2.0f};
		SDL_RenderRect(renderer, &line);
	}
}

SDL_FColor ToFloatColor(SDL_Color color)
{
	return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

void DrawGradientRect(SDL_Renderer *renderer, const SDL_FRect &rect, SDL_Color left, SDL_Color right)
{
	const SDL_Vertex vertices[] = {
		{{rect.x, rect.y}, ToFloatColor(left), {0.0f, 0.0f}},
		{{rect.x + rect.w, rect.y}, ToFloatColor(right), {1.0f, 0.0f}},
		{{rect.x + rect.w, rect.y + rect.h}, ToFloatColor(right), {1.0f, 1.0f}},
		{{rect.x, rect.y + rect.h}, ToFloatColor(left), {0.0f, 1.0f}}
	};
	const int indices[] = {0, 1, 2, 0, 2, 3};
	SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

bool ButtonIsHilited(const Button &button)
{
	return button.enabled && (button.hilited || button.focused);
}

void DrawSageButtonBase(SDL_Renderer *renderer, LauncherFont &font, const Button &button)
{
	SDL_FRect rect = button.rect;
	const bool hilited = ButtonIsHilited(button);
	if (button.selected) {
		rect.x += 2.0f;
		rect.y += 2.0f;
		rect.w -= 2.0f;
		rect.h -= 2.0f;
	}

	const SDL_FRect shadow{rect.x + 4.0f, rect.y + 5.0f, rect.w, rect.h};
	DrawFilledRect(renderer, shadow, SDL_Color{0, 0, 0, 145});
	SDL_Color fill = button.enabled ? LauncherPalette::kControl : SDL_Color{12, 19, 15, 225};
	if (hilited) fill = LauncherPalette::kControlHilite;
	if (button.selected) fill = LauncherPalette::kControlPressed;
	DrawFilledRect(renderer, rect, fill);

	SDL_Color border = button.enabled ? button.accent : LauncherPalette::kDisabledBorder;
	if (hilited) {
		border.r = static_cast<Uint8>(std::min(255, border.r + 38));
		border.g = static_cast<Uint8>(std::min(255, border.g + 38));
		border.b = static_cast<Uint8>(std::min(255, border.b + 38));
	}
	DrawOutline(renderer, rect, border, 2);
	const SDL_FRect inner{rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f};
	DrawOutline(renderer, inner, SDL_Color{border.r, border.g, border.b, static_cast<Uint8>(hilited ? 150 : 62)}, 1);

	const SDL_FRect leftCap{rect.x, rect.y + 9.0f, 5.0f, rect.h - 18.0f};
	const SDL_FRect rightCap{rect.x + rect.w - 5.0f, rect.y + 9.0f, 5.0f, rect.h - 18.0f};
	DrawFilledRect(renderer, leftCap, SDL_Color{border.r, border.g, border.b, static_cast<Uint8>(hilited ? 220 : 125)});
	DrawFilledRect(renderer, rightCap, SDL_Color{border.r, border.g, border.b, static_cast<Uint8>(hilited ? 220 : 125)});

	SDL_Color textColor = button.enabled ? (hilited ? button.hiliteText : button.enabledText) : button.disabledText;
	const float textX = rect.x + rect.w * 0.5f;
	const float textY = rect.y + rect.h * 0.5f + 10.0f;
	font.draw(button.label, textX + 2.0f, textY + 2.0f, 29.0f, SDL_Color{0, 0, 0, 240}, true);
	font.draw(button.label, textX, textY, 29.0f, textColor, true);
}

int ButtonTransitionFrame(const Button &button, Uint64 transitionEpoch)
{
	constexpr Uint64 frameMilliseconds = 1000 / 30;
	const Uint64 now = SDL_GetTicks();
	const Uint64 elapsedFrames = now > transitionEpoch ? (now - transitionEpoch) / frameMilliseconds : 0;
	return static_cast<int>(elapsedFrames) - button.transitionDelay;
}

bool ButtonAcceptsInput(const Button &button, Uint64 transitionEpoch)
{
	return ButtonTransitionFrame(button, transitionEpoch) >= 11;
}

// GeneralsX @feature Codex 11/08/2026 Port SAGE ButtonFlashTransition's 17-frame sequence at its original 30 Hz cadence.
void DrawSageButton(SDL_Renderer *renderer, LauncherFont &font, Button &button, Uint64 transitionEpoch,
	LauncherUIAudio &audio, bool useTransition = true)
{
	const int frame = useTransition ? ButtonTransitionFrame(button, transitionEpoch) : 17;
	if (useTransition && frame >= 1 && button.lastTransitionFrame < 1) audio.play(LauncherUISound::ButtonsFadeIn);
	button.lastTransitionFrame = frame;
	if (frame < 1) return;

	if (frame >= 4 || frame >= 8) DrawSageButtonBase(renderer, font, button);
	if (frame >= 17) return;

	if (frame >= 1 && frame <= 3) {
		const Uint8 alpha[] = {75, 150, 200};
		DrawFilledRect(renderer, button.rect, SDL_Color{LauncherPalette::kAccentBright.r,
			LauncherPalette::kAccentBright.g, LauncherPalette::kAccentBright.b, alpha[frame - 1]});
		DrawOutline(renderer, button.rect, SDL_Color{LauncherPalette::kAccentBright.r,
			LauncherPalette::kAccentBright.g, LauncherPalette::kAccentBright.b,
			static_cast<Uint8>(alpha[frame - 1] + 25)}, 1);
	} else if (frame >= 4 && frame <= 7) {
		const Uint8 alpha[] = {150, 100, 50, 15};
		DrawFilledRect(renderer, button.rect, SDL_Color{LauncherPalette::kAccentBright.r,
			LauncherPalette::kAccentBright.g, LauncherPalette::kAccentBright.b, alpha[frame - 4]});
		DrawOutline(renderer, button.rect, LauncherPalette::kAccentBright, 1);
	} else if (frame >= 11 && frame <= 16) {
		const Uint8 alpha[] = {100, 200, 150, 100, 50, 17};
		const Uint8 value = alpha[frame - 11];
		DrawGradientRect(renderer, button.rect,
			SDL_Color{LauncherPalette::kAccentBright.r, LauncherPalette::kAccentBright.g,
				LauncherPalette::kAccentBright.b, 0},
			SDL_Color{LauncherPalette::kAccentBright.r, LauncherPalette::kAccentBright.g,
				LauncherPalette::kAccentBright.b, value});
	}
}

std::string Localized(bool russian, const char *english, const char *russianText)
{
	return russian ? russianText : english;
}

bool LauncherUsesRussian(const EchelonLauncher::LauncherSettings &settings)
{
	if (settings.language == EchelonLauncher::LanguageMode::Russian) return true;
	if (settings.language == EchelonLauncher::LanguageMode::English) return false;
	return IsRussianLocale();
}

std::string OnOff(bool russian, bool value)
{
	return value ? Localized(russian, "ON", "ВКЛ") : Localized(russian, "OFF", "ВЫКЛ");
}

void RemoveLastUtf8Codepoint(std::string &text)
{
	if (text.empty()) return;
	size_t offset = text.size() - 1;
	while (offset > 0 && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) --offset;
	text.erase(offset);
}

void DrawSettingsRow(SDL_Renderer *renderer, LauncherFont &font, const SDL_FRect &row,
	const std::string &label, const std::string &value, bool selected = false)
{
	DrawFilledRect(renderer, row, selected ? LauncherPalette::kControlHilite : LauncherPalette::kControl);
	DrawOutline(renderer, row, selected ? LauncherPalette::kAccentBright : LauncherPalette::kAccentMuted, selected ? 2 : 1);
	font.draw(label, row.x + 22.0f, row.y + row.h * 0.5f + 7.0f, 21.0f, LauncherPalette::kText);
	std::string displayedValue = value;
	constexpr float maximumValueWidth = 510.0f;
	while (displayedValue.size() > 3 && font.measure(displayedValue, 21.0f) > maximumValueWidth) {
		RemoveLastUtf8Codepoint(displayedValue);
	}
	if (displayedValue != value) displayedValue += "...";
	font.draw(displayedValue, row.x + row.w - 22.0f - font.measure(displayedValue, 21.0f), row.y + row.h * 0.5f + 7.0f, 21.0f,
		selected ? LauncherPalette::kAccentBright : LauncherPalette::kValue);
}

void DrawModificationCard(SDL_Renderer *renderer, LauncherFont &font, ModCoverCache &covers,
	const ModificationViewItem &modification, const SDL_FRect &card, bool selected, bool active,
	int activeOrder, bool russian)
{
	DrawFilledRect(renderer, card, selected ? LauncherPalette::kControlHilite : LauncherPalette::kControl);
	DrawOutline(renderer, card, selected ? LauncherPalette::kAccentBright : LauncherPalette::kAccentMuted,
		selected ? 3 : 1);
	const SDL_FRect cover{card.x + 10.0f, card.y + 10.0f, 500.0f, 100.0f};
	if (!covers.draw(renderer, modification.coverPath(), selected, cover)) {
		DrawGradientRect(renderer, cover, selected ? SDL_Color{9, 71, 37, 255} : SDL_Color{15, 30, 22, 255},
			selected ? SDL_Color{1, 23, 12, 255} : SDL_Color{3, 11, 7, 255});
		DrawOutline(renderer, cover, selected ? LauncherPalette::kAccent : LauncherPalette::kFrameSoft, 1);
		font.draw(modification.name(), cover.x + cover.w * 0.5f, cover.y + 59.0f, 24.0f,
			selected ? LauncherPalette::kTextBright : LauncherPalette::kDisabledText, true);
	}
	const float textX = card.x + 528.0f;
	font.draw(modification.name(), textX, card.y + 35.0f, 20.0f,
		selected ? LauncherPalette::kTextBright : LauncherPalette::kText);
	font.draw(modification.version(), textX, card.y + 65.0f, 18.0f, LauncherPalette::kValue);
	std::string status = Localized(russian, "INSTALLED", "УСТАНОВЛЕН");
	font.draw(status, textX, card.y + 94.0f, 16.0f,
		modification.installed ?
			(selected ? LauncherPalette::kAccentBright : LauncherPalette::kAccentSoft) : LauncherPalette::kValue);
	if (active) {
		const std::string activeLabel = activeOrder > 0 ?
			Localized(russian, "ACTIVE #", "АКТИВЕН #") + std::to_string(activeOrder) :
			Localized(russian, "ACTIVE", "АКТИВЕН");
		font.draw(activeLabel, card.x + card.w - 14.0f - font.measure(activeLabel, 15.0f),
			card.y + 22.0f, 15.0f, LauncherPalette::kAccentBright);
	}
}

void DrawSettingsScrollArrow(SDL_Renderer *renderer, const Button &button, bool pointsUp)
{
	const float centerX = button.rect.x + button.rect.w * 0.5f;
	const float centerY = button.rect.y + button.rect.h * 0.5f;
	const SDL_FColor color = ToFloatColor(button.enabled ? LauncherPalette::kTextBright : LauncherPalette::kDisabledText);
	const SDL_Vertex vertices[] = {
		{{centerX, centerY + (pointsUp ? -10.0f : 10.0f)}, color, {0.0f, 0.0f}},
		{{centerX - 13.0f, centerY + (pointsUp ? 9.0f : -9.0f)}, color, {0.0f, 0.0f}},
		{{centerX + 13.0f, centerY + (pointsUp ? 9.0f : -9.0f)}, color, {0.0f, 0.0f}}
	};
	const int indices[] = {0, 1, 2};
	SDL_RenderGeometry(renderer, nullptr, vertices, 3, indices, 3);
}

void DrawLauncherBackground(SDL_Renderer *renderer, const LauncherBackdrop &backdrop)
{
	SDL_SetRenderDrawColor(renderer, 2, 14, 9, 255);
	SDL_RenderClear(renderer);

	if (backdrop.valid()) {
		backdrop.draw(renderer);
	} else {
		DrawGradientRect(renderer, SDL_FRect{0, 0, kLogicalWidth, kLogicalHeight},
			SDL_Color{10, 54, 28, 255}, SDL_Color{2, 20, 12, 255});
	}
	DrawGradientRect(renderer, SDL_FRect{0, 0, kLogicalWidth, kLogicalHeight},
		SDL_Color{0, 7, 3, 18}, SDL_Color{0, 20, 9, 135});
	DrawFilledRect(renderer, SDL_FRect{0, 0, kLogicalWidth, 64}, SDL_Color{0, 12, 6, 90});
	DrawFilledRect(renderer, SDL_FRect{0, 936, kLogicalWidth, 64}, SDL_Color{0, 10, 5, 130});

	SDL_SetRenderDrawColor(renderer, LauncherPalette::kGrid.r, LauncherPalette::kGrid.g,
		LauncherPalette::kGrid.b, LauncherPalette::kGrid.a);
	for (int x = 0; x <= kLogicalWidth; x += 40) SDL_RenderLine(renderer, static_cast<float>(x), 0.0f, static_cast<float>(x), kLogicalHeight);
	for (int y = 0; y <= kLogicalHeight; y += 40) SDL_RenderLine(renderer, 0.0f, static_cast<float>(y), kLogicalWidth, static_cast<float>(y));

	DrawOutline(renderer, kLauncherLayout.frame, LauncherPalette::kFrame, 1);
	DrawOutline(renderer, kLauncherLayout.innerFrame, LauncherPalette::kFrameSoft, 1);
	const int firstTickX = static_cast<int>(kLauncherLayout.innerFrame.x) + 10;
	const int lastTickX = static_cast<int>(kLauncherLayout.innerFrame.x + kLauncherLayout.innerFrame.w) - 10;
	for (int x = firstTickX; x < lastTickX; x += 16) {
		const float height = ((x - firstTickX) % 64 == 0) ? 13.0f : 7.0f;
		SDL_RenderLine(renderer, static_cast<float>(x), kLauncherLayout.innerFrame.y,
			static_cast<float>(x), kLauncherLayout.innerFrame.y + height);
		SDL_RenderLine(renderer, static_cast<float>(x), kLauncherLayout.innerFrame.y + kLauncherLayout.innerFrame.h - height,
			static_cast<float>(x), kLauncherLayout.innerFrame.y + kLauncherLayout.innerFrame.h);
	}
}

void DrawLauncherBranding(SDL_Renderer *renderer, LauncherFont &font, const LauncherLogo &logo)
{
	if (logo.valid()) {
		logo.draw(renderer);
		return;
	}
	const float centerX = kLauncherLayout.logo.x + kLauncherLayout.logo.w * 0.5f;
	font.draw(EchelonBrand::kProductName, centerX, 157.0f, 67.0f, LauncherPalette::kAccentBright, true);
}

void UpdateButtonHilite(Button &button, float mouseX, float mouseY, bool acceptsInput)
{
	button.hilited = button.enabled && acceptsInput && Contains(button.rect, mouseX, mouseY);
	if (!button.hilited && button.selected) button.selected = false;
}

void SDLCALL FolderDialogCallback(void *userData, const char *const *fileList, int)
{
	auto *state = static_cast<FolderDialogState *>(userData);
	std::lock_guard<std::mutex> lock(state->mutex);
	state->selected.reset();
	state->error.clear();
	if (!fileList) {
		state->error = SDL_GetError();
	} else if (fileList[0] && fileList[0][0]) {
		state->selected = fs::path(fileList[0]);
	}
	state->pending = false;
	state->completed = true;
}

bool BeginFolderDialog(FolderDialogState &state, SDL_Window *window, const fs::path &defaultLocation)
{
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.pending) return false;
	state.pending = true;
	state.completed = false;
	state.selected.reset();
	state.error.clear();
	state.defaultLocation = defaultLocation.string();
	SDL_ShowOpenFolderDialog(FolderDialogCallback, &state, window, state.defaultLocation.c_str(), false);
	return true;
}

std::optional<MigrationResult> ConsumeFolderDialog(FolderDialogState &state, std::optional<fs::path> &selected)
{
	std::lock_guard<std::mutex> lock(state.mutex);
	if (!state.completed) return std::nullopt;
	state.completed = false;
	selected = state.selected;
	if (!state.error.empty()) return MigrationResult{false, "Folder dialog failed: " + state.error};
	return MigrationResult{true, {}};
}

void SDLCALL FileDialogCallback(void *userData, const char *const *fileList, int)
{
	auto *state = static_cast<FileDialogState *>(userData);
	std::lock_guard<std::mutex> lock(state->mutex);
	state->selected.clear();
	state->error.clear();
	if (!fileList) {
		state->error = SDL_GetError();
	} else {
		for (const char *const *file = fileList; *file && **file; ++file) state->selected.emplace_back(*file);
	}
	state->pending = false;
	state->completed = true;
}

bool BeginModificationFileDialog(FileDialogState &state, SDL_Window *window, const fs::path &defaultLocation)
{
	static constexpr SDL_DialogFileFilter kFilters[] = {
		{"Supported modification packages", "zip;7z;rar;big"},
		{"All files", "*"}
	};
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.pending) return false;
	state.pending = true;
	state.completed = false;
	state.selected.clear();
	state.error.clear();
	state.defaultLocation = defaultLocation.string();
	SDL_ShowOpenFileDialog(FileDialogCallback, &state, window, kFilters, static_cast<int>(std::size(kFilters)),
		state.defaultLocation.c_str(), true);
	return true;
}

bool BeginCoverImageFileDialog(FileDialogState &state, SDL_Window *window, const fs::path &defaultLocation)
{
	static constexpr SDL_DialogFileFilter kFilters[] = {
		{"Images", "png;jpg;jpeg;webp;bmp"}
	};
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.pending) return false;
	state.pending = true;
	state.completed = false;
	state.selected.clear();
	state.error.clear();
	state.defaultLocation = defaultLocation.string();
	SDL_ShowOpenFileDialog(FileDialogCallback, &state, window, kFilters, static_cast<int>(std::size(kFilters)),
		state.defaultLocation.c_str(), false);
	return true;
}

std::string FileUri(const fs::path &path)
{
	static constexpr char hexadecimal[] = "0123456789ABCDEF";
	std::string uri = "file://";
	for (const unsigned char character : path.string()) {
		if (std::isalnum(character) || character == '/' || character == '-' || character == '_' ||
			character == '.' || character == '~') {
			uri.push_back(static_cast<char>(character));
		} else {
			uri.push_back('%');
			uri.push_back(hexadecimal[character >> 4]);
			uri.push_back(hexadecimal[character & 15]);
		}
	}
	return uri;
}

std::optional<MigrationResult> ConsumeModificationFileDialog(FileDialogState &state,
	std::vector<fs::path> &selected)
{
	std::lock_guard<std::mutex> lock(state.mutex);
	if (!state.completed) return std::nullopt;
	state.completed = false;
	selected = std::move(state.selected);
	state.selected.clear();
	if (!state.error.empty()) return MigrationResult{false, "File dialog failed: " + state.error};
	return MigrationResult{true, {}};
}

std::string ModuleFileName(const std::string &engine)
{
	return engine == kGeneralsProfileId ? ECHELON_GENERALS_MODULE_FILE : ECHELON_ZEROHOUR_MODULE_FILE;
}

fs::path ExecutableDirectory()
{
	const char *basePath = SDL_GetBasePath();
	const fs::path base = basePath && basePath[0] ? fs::path(basePath) : fs::current_path();
	// SDL returns Resources for macOS bundles; workers and modules live in MacOS.
	const fs::path bundleBinaryDirectory = base / ".." / "MacOS";
	if (fs::is_regular_file(bundleBinaryDirectory / EchelonBrand::kProductSlug)) return bundleBinaryDirectory;
	return base;
}

LoadedModule *LoadModule(const std::string &engine, std::unordered_map<std::string, LoadedModule> &modules, std::string &errorMessage)
{
	if (auto found = modules.find(engine); found != modules.end()) {
		return &found->second;
	}
	std::vector<fs::path> candidates = {
		ExecutableDirectory() / ModuleFileName(engine),
		ExecutableDirectory() / ".." / "lib" / EchelonBrand::kInstallDataDirectory / ModuleFileName(engine)
	};
	SDL_SharedObject *handle = nullptr;
	for (const fs::path &candidate : candidates) {
		handle = SDL_LoadObject(candidate.string().c_str());
		if (handle) break;
	}
	if (!handle) {
		errorMessage = "Cannot load " + ModuleFileName(engine) + ": " + SDL_GetError();
		return nullptr;
	}
	const SDL_FunctionPointer function = SDL_LoadFunction(handle, EchelonBrand::kModuleExport);
	if (!function) {
		errorMessage = "Engine module has no Echelon_GetEngineModuleV2 export";
		return nullptr;
	}
	auto getModule = reinterpret_cast<EchelonGetEngineModuleV2Fn>(function);
	const EchelonEngineModuleV2 *api = getModule();
	if (!api || api->struct_size < sizeof(EchelonEngineModuleV2) || api->abi_version != ECHELON_ENGINE_ABI_VERSION ||
		!api->engine_id || engine != api->engine_id || !api->run || !api->query_quiescence || !api->shutdown) {
		errorMessage = "Engine module ABI mismatch";
		return nullptr;
	}
	auto [iterator, inserted] = modules.emplace(engine, LoadedModule{handle, api});
	return inserted ? &iterator->second : nullptr;
}

void EngineLog(void *, const char *message)
{
	fprintf(stderr, "[ENGINE] %s\n", message ? message : "");
	fflush(stderr);
}

void EnginePhase(void *, EchelonEnginePhaseV2 phase, const char *message)
{
	fprintf(stderr, "[ENGINE-LIFECYCLE] phase=%u message=%s\n", static_cast<unsigned int>(phase), message ? message : "");
	fflush(stderr);
}

void SetEnvironment(const char *name, const std::string &value)
{
#if defined(_WIN32)
	_putenv_s(name, value.c_str());
#else
	setenv(name, value.c_str(), 1);
#endif
}

struct WorkerResourceSnapshot
{
	long residentKilobytes = -1;
	long threadCount = -1;
	long descriptorCount = -1;
};

struct SharedWindowState
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	int pixelWidth = 0;
	int pixelHeight = 0;
	Uint64 stateFlags = 0;
};

bool SynchronizeWindow(SDL_Window *window, const char *operation)
{
	if (SDL_SyncWindow(window)) return true;
	fprintf(stderr, "ERROR: Cannot synchronize the shared window after %s: %s\n", operation, SDL_GetError());
	fflush(stderr);
	return false;
}

bool CanControlWindowPosition()
{
	const char *driver = SDL_GetCurrentVideoDriver();
	// Echelon @bugfix Codex 14/08/2026 Wayland intentionally gives placement authority to the compositor.
	return !driver || std::strcmp(driver, "wayland") != 0;
}

// Echelon @feature Codex 14/08/2026 Centralize every launcher/engine presentation transition under host ownership.
bool ApplyWindowedPresentation(SDL_Window *window, int logicalWidth, int logicalHeight,
	bool centerWindow, std::optional<std::pair<int, int>> position = std::nullopt)
{
	const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
	if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0) {
		if (!SDL_SetWindowFullscreen(window, false) || !SynchronizeWindow(window, "leaving fullscreen")) return false;
	}
	if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
		if (!SDL_RestoreWindow(window) || !SynchronizeWindow(window, "restoring a maximized window")) return false;
	}
	logicalWidth = std::clamp(logicalWidth, 1, 16384);
	logicalHeight = std::clamp(logicalHeight, 1, 16384);
	if (!SDL_SetWindowSize(window, logicalWidth, logicalHeight) || !SynchronizeWindow(window, "applying windowed size")) {
		return false;
	}
	int targetX = 0;
	int targetY = 0;
	bool moveWindow = false;
	if (position) {
		targetX = position->first;
		targetY = position->second;
		moveWindow = true;
	} else if (centerWindow) {
		SDL_Rect usable{};
		if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable)) {
			targetX = usable.x + std::max(0, usable.w - logicalWidth) / 2;
			targetY = usable.y + std::max(0, usable.h - logicalHeight) / 2;
			moveWindow = true;
		}
	}
	if (moveWindow && CanControlWindowPosition() && (!SDL_SetWindowPosition(window, targetX, targetY) ||
		!SynchronizeWindow(window, "applying windowed position"))) {
		return false;
	}
	SDL_ShowWindow(window);
	return SynchronizeWindow(window, "showing windowed presentation");
}

bool ApplyFullscreenPresentation(SDL_Window *window)
{
	if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
		if (!SDL_RestoreWindow(window) || !SynchronizeWindow(window, "restoring before fullscreen")) return false;
	}
	if (!SDL_SetWindowFullscreenMode(window, nullptr)) return false;
	if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0 && !SDL_SetWindowFullscreen(window, true)) return false;
	SDL_ShowWindow(window);
	return SynchronizeWindow(window, "entering desktop fullscreen");
}

bool RestoreSharedWindowState(SDL_Window *window, const SharedWindowState &state)
{
	if ((state.stateFlags & SDL_WINDOW_FULLSCREEN) != 0) return ApplyFullscreenPresentation(window);
	if (!ApplyWindowedPresentation(window, state.width, state.height, false, std::pair{state.x, state.y})) return false;
	if ((state.stateFlags & SDL_WINDOW_MAXIMIZED) != 0) {
		if (!SDL_MaximizeWindow(window) || !SynchronizeWindow(window, "restoring maximized launcher state")) return false;
	}
	return true;
}

struct EngineWindowCoordinator
{
	SDL_Window *window = nullptr;

	bool apply(bool windowed, uint32_t renderWidth, uint32_t renderHeight)
	{
		if (!window || renderWidth == 0 || renderHeight == 0) return false;
		fprintf(stderr, "[WINDOW-MODE] owner=engine requested=%s render=%ux%u\n",
			windowed ? "windowed" : "fullscreen", renderWidth, renderHeight);
		fflush(stderr);
		if (!windowed) return ApplyFullscreenPresentation(window);
		float density = SDL_GetWindowPixelDensity(window);
		if (!(density > 0.0f)) density = 1.0f;
		const int logicalWidth = std::max(1, static_cast<int>(std::lround(renderWidth / density)));
		const int logicalHeight = std::max(1, static_cast<int>(std::lround(renderHeight / density)));
		return ApplyWindowedPresentation(window, logicalWidth, logicalHeight, true);
	}
};

uint32_t EngineWindowModeCallback(void *userData, uint32_t windowed, uint32_t renderWidth, uint32_t renderHeight)
{
	auto *coordinator = static_cast<EngineWindowCoordinator *>(userData);
	return coordinator && coordinator->apply(windowed != 0, renderWidth, renderHeight) ? 1u : 0u;
}

// Echelon @test Codex 14/08/2026 Make the host-owned window invariant observable in automated lifecycle tests.
SharedWindowState CaptureSharedWindowState(SDL_Window *window)
{
	SharedWindowState state;
	SDL_GetWindowPosition(window, &state.x, &state.y);
	SDL_GetWindowSize(window, &state.width, &state.height);
	SDL_GetWindowSizeInPixels(window, &state.pixelWidth, &state.pixelHeight);
	state.stateFlags = SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED);
	return state;
}

bool SharedWindowStateEquals(const SharedWindowState &left, const SharedWindowState &right)
{
	const bool positionMatches = !CanControlWindowPosition() || (left.x == right.x && left.y == right.y);
	return positionMatches && left.width == right.width && left.height == right.height &&
		left.pixelWidth == right.pixelWidth && left.pixelHeight == right.pixelHeight && left.stateFlags == right.stateFlags;
}

void LogSharedWindowState(const char *phase, const SharedWindowState &state)
{
	fprintf(stderr, "[WINDOW-HANDOFF] phase=%s pos=%d,%d logical=%dx%d pixels=%dx%d flags=0x%llx\n",
		phase, state.x, state.y, state.width, state.height, state.pixelWidth, state.pixelHeight,
		static_cast<unsigned long long>(state.stateFlags));
	fflush(stderr);
}

WorkerResourceSnapshot CaptureWorkerResources()
{
	WorkerResourceSnapshot snapshot;
#if defined(__linux__)
	std::ifstream status("/proc/self/status");
	std::string key;
	while (status >> key) {
		if (key == "VmRSS:") {
			status >> snapshot.residentKilobytes;
		} else if (key == "Threads:") {
			status >> snapshot.threadCount;
		}
		std::string remainder;
		std::getline(status, remainder);
	}
	std::error_code error;
	long descriptors = 0;
	for (fs::directory_iterator iterator("/proc/self/fd", error), end; !error && iterator != end; iterator.increment(error)) {
		++descriptors;
	}
	if (!error) snapshot.descriptorCount = descriptors;
#endif
	return snapshot;
}

void LogWorkerResources(uint32_t cycle)
{
	const WorkerResourceSnapshot snapshot = CaptureWorkerResources();
	fprintf(stderr, "[WORKER-RESOURCES] cycle=%u rss_kb=%ld threads=%ld fds=%ld\n",
		cycle, snapshot.residentKilobytes, snapshot.threadCount, snapshot.descriptorCount);
	fflush(stderr);
}

EchelonEngineResultV2 RunProfile(LauncherProfile &profile, SDL_Window *window, const std::vector<std::string> &gameArguments,
	const EchelonLauncher::LauncherSettings &launcherSettings,
	bool headless, uint32_t internalTestReturnAfterUpdates,
	std::unordered_map<std::string, LoadedModule> &modules,
	uint32_t &quiescenceFlags, std::string &errorMessage, EngineWindowCoordinator *windowCoordinator = nullptr,
	const EchelonLauncher::ModificationStack *contentStack = nullptr)
{
	quiescenceFlags = 0;
	std::vector<std::string> arguments;
	arguments.emplace_back(EchelonBrand::kProductSlug);
	arguments.insert(arguments.end(), gameArguments.begin(), gameArguments.end());
	if (contentStack && !contentStack->layers.empty()) {
		const bool hasLegacyMod = std::any_of(arguments.begin(), arguments.end(), [](const std::string &argument) {
			const std::string normalized = ToLower(argument);
			return normalized == "-mod" || normalized.compare(0, 5, "-mod=") == 0;
		});
		if (hasLegacyMod) {
			errorMessage = "Managed content layers cannot be combined with the legacy -mod argument";
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		if (contentStack->engine != profile.engine) {
			errorMessage = "Modification stack belongs to a different engine profile";
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		// Echelon @bugfix Codex 14/08/2026 Rebuild every content fingerprint immediately before handing paths to the engine.
		// A modified, truncated, or symlink-injected installation must never enter the read-only overlay under a stale manifest digest.
		for (const EchelonLauncher::InstalledModification *layer : contentStack->layers) {
			if (!layer || !EchelonLauncher::VerifyInstalledModification(*layer, errorMessage)) {
				if (errorMessage.empty()) errorMessage = "Modification stack contains an invalid layer";
				else errorMessage = layer ? ("Cannot verify " + layer->id + "@" + layer->version + ": " + errorMessage) : errorMessage;
				return ECHELON_ENGINE_FATAL_ERROR;
			}
		}
	}
	LoadedModule *module = LoadModule(profile.engine, modules, errorMessage);
	if (!module) return ECHELON_ENGINE_FATAL_ERROR;
	const EchelonLauncher::ProfileLaunchSettings &profileSettings =
		EchelonLauncher::SettingsForProfile(launcherSettings, profile.id);
	if (!headless) {
		if (!EchelonLauncher::AppendProfileArguments(profileSettings, arguments, errorMessage)) {
			return ECHELON_ENGINE_FATAL_ERROR;
		}
	}
	// Echelon @refactor Codex 14/08/2026 Vanilla profile buttons never inherit a launcher-selected modification.
	// Explicit legacy -mod arguments remain untouched for direct and automated launches.
	// Echelon @feature Codex 14/08/2026 Resolve the final command-line display intent before DXVK creates its swapchain.
	if (!headless && windowCoordinator) {
		std::string optionsError;
		const EchelonLauncher::GameOptions options =
			EchelonLauncher::LoadGameOptions(profile.userDataRoot / "Options.ini", optionsError);
		if (!optionsError.empty()) {
			errorMessage = optionsError;
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		bool engineWindowed = false;
		int renderWidth = options.resolutionWidth;
		int renderHeight = options.resolutionHeight;
		for (size_t index = 1; index < arguments.size(); ++index) {
			const std::string argument = ToLower(arguments[index]);
			if (argument == "-win") engineWindowed = true;
			else if (argument == "-fullscreen") engineWindowed = false;
			else if ((argument == "-xres" || argument == "-yres") && index + 1 < arguments.size()) {
				const long value = std::strtol(arguments[index + 1].c_str(), nullptr, 10);
				if (value >= 1 && value <= 16384) {
					if (argument == "-xres") renderWidth = static_cast<int>(value);
					else renderHeight = static_cast<int>(value);
				}
				++index;
			}
		}
		if (!windowCoordinator->apply(engineWindowed, static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight))) {
			errorMessage = std::string("Cannot apply engine display mode: ") + SDL_GetError();
			return ECHELON_ENGINE_FATAL_ERROR;
		}
	}
	std::vector<char *> argumentPointers;
	argumentPointers.reserve(arguments.size());
	for (std::string &argument : arguments) argumentPointers.push_back(argument.data());

	SetEnvironment("CNC_GENERALS_INSTALLPATH", profile.assetRoot.string());
	SetEnvironment("CNC_GENERALS_PATH", profile.baseAssetRoot.string());
	if (profile.engine == kZeroHourProfileId) SetEnvironment("CNC_GENERALS_ZH_PATH", profile.assetRoot.string());
	SetEnvironment("ECHELON_USER_DATA_ROOT", profile.userDataRoot.string());
	const bool useRussianLocalization = headless || profileSettings.russianLocalization;
	if (!useRussianLocalization) {
		SetEnvironment("ECHELON_DISABLED_BIG_FILES", profile.engine == kZeroHourProfileId ?
			"00Russia.big;00Russian.big;00RussiaZH.big;00RussianZH.big" : "00Russia.big;00Russian.big");
	} else {
		SetEnvironment("ECHELON_DISABLED_BIG_FILES", "");
	}
	SetEnvironment("ECHELON_UI_LANGUAGE",
		profile.hasRussianLocalization && useRussianLocalization ? "ru" : "en");

	const fs::path previousDirectory = fs::current_path();
	std::error_code directoryError;
	fs::current_path(profile.assetRoot, directoryError);
	if (directoryError) {
		SetEnvironment("ECHELON_DISABLED_BIG_FILES", "");
		errorMessage = "Cannot enter game data directory: " + directoryError.message();
		return ECHELON_ENGINE_FATAL_ERROR;
	}

	EchelonEngineHostV2 host{};
	host.struct_size = sizeof(host);
	host.abi_version = ECHELON_ENGINE_ABI_VERSION;
	host.sdl_window = window;
	host.argc = static_cast<int>(argumentPointers.size());
	host.argv = argumentPointers.data();
	host.profile_id = profile.id.c_str();
	const std::string baseAssetRoot = profile.baseAssetRoot.string();
	const std::string assetRoot = profile.assetRoot.string();
	const std::string userDataRoot = profile.userDataRoot.string();
	host.asset_root = assetRoot.c_str();
	host.base_asset_root = baseAssetRoot.c_str();
	host.user_data_root = userDataRoot.c_str();
	host.headless = headless ? 1u : 0u;
	host.log_callback = EngineLog;
	host.phase_callback = EnginePhase;
	host.internal_test_return_after_updates = internalTestReturnAfterUpdates;
	// Echelon @feature Codex 14/08/2026 The launcher retains physical window ownership across every in-process engine session.
	host.window_policy = headless ? ECHELON_ENGINE_WINDOW_POLICY_ENGINE_OWNED :
		ECHELON_ENGINE_WINDOW_POLICY_HOST_OWNED;
	host.window_mode_user_data = windowCoordinator;
	host.window_mode_callback = windowCoordinator ? EngineWindowModeCallback : nullptr;
	std::vector<std::string> contentRootPaths;
	std::vector<EchelonContentLayerV1> hostContentLayers;
	if (contentStack) {
		contentRootPaths.reserve(contentStack->layers.size());
		hostContentLayers.reserve(contentStack->layers.size());
		uint32_t priority = 100;
		for (const EchelonLauncher::InstalledModification *layer : contentStack->layers) {
			contentRootPaths.push_back(layer->launchPath.string());
			uint32_t type = ECHELON_CONTENT_LAYER_MOD;
			if (layer->type == EchelonLauncher::ModificationType::Patch) type = ECHELON_CONTENT_LAYER_PATCH;
			else if (layer->type == EchelonLauncher::ModificationType::Addon) type = ECHELON_CONTENT_LAYER_ADDON;
			hostContentLayers.push_back(EchelonContentLayerV1{sizeof(EchelonContentLayerV1), type,
				layer->id.c_str(), layer->version.c_str(), contentRootPaths.back().c_str(), priority,
				layer->contentFingerprint.c_str()});
			priority += 100;
		}
		host.content_layers = hostContentLayers.data();
		host.content_layer_count = static_cast<uint32_t>(hostContentLayers.size());
		host.content_stack_fingerprint = contentStack->fingerprint.c_str();
	}
	fprintf(stderr, "[CONTENT-STACK] engine=%s layers=%u fingerprint=%s\n", profile.engine.c_str(),
		host.content_layer_count, host.content_stack_fingerprint ? host.content_stack_fingerprint : "vanilla");
	fflush(stderr);

	const EchelonEngineResultV2 result = module->api->run(&host);
	EchelonEngineQuiescenceReportV2 report{};
	report.struct_size = sizeof(report);
	quiescenceFlags = module->api->query_quiescence(&report);
	fprintf(stderr, "INFO: Echelon engine quiescence flags: 0x%08x\n", quiescenceFlags);
	fflush(stderr);
	fs::current_path(previousDirectory, directoryError);
	SetEnvironment("ECHELON_DISABLED_BIG_FILES", "");
	return result;
}

struct ParsedArguments
{
	std::string profile;
	std::string modSelection;
	std::vector<std::string> patchSelections;
	std::vector<std::string> addonSelections;
	std::string argumentError;
	bool forceLauncher = false;
	bool showMods = false;
	bool noMods = false;
	bool headless = false;
	bool internalUIWorker = false;
	bool internalRecovered = false;
	bool internalTestSupervisorRecovery = false;
	bool internalTestOpenSettings = false;
	std::string internalTestSettingsPage;
	std::string internalTestModsPage;
	std::string internalTestModsEngine;
	std::string internalTestModsContext;
	std::string internalTestScreenshot;
	int internalTestSettingsRow = -1;
	int internalTestWindowWidth = 0;
	int internalTestWindowHeight = 0;
	uint32_t internalTestReturnAfterUpdates = 0;
	uint32_t internalTestCycles = 0;
	std::vector<std::string> engineArguments;
};

int RunSupervisor(int argc, char **argv)
{
	const fs::path executable = ExecutableDirectory() / EchelonBrand::kProductSlug;
	std::vector<std::string> workerArguments;
	workerArguments.reserve(static_cast<size_t>(argc) + 3);
	workerArguments.push_back(executable.string());
	workerArguments.emplace_back("--internal-ui-worker");
	for (int index = 1; index < argc; ++index) {
		if (argv[index]) workerArguments.emplace_back(argv[index]);
	}

	std::vector<std::chrono::steady_clock::time_point> recoveries;
	bool recovered = false;
	for (;;) {
		std::vector<std::string> currentArguments = workerArguments;
		if (recovered) {
			currentArguments.emplace_back("--launcher");
			currentArguments.emplace_back("--internal-recovered");
		}
		std::vector<const char *> argumentPointers;
		argumentPointers.reserve(currentArguments.size() + 1);
		for (const std::string &argument : currentArguments) argumentPointers.push_back(argument.c_str());
		argumentPointers.push_back(nullptr);

		SDL_Process *worker = SDL_CreateProcess(argumentPointers.data(), false);
		if (!worker) {
			fprintf(stderr, "FATAL: Cannot start the Echelon UI worker: %s\n", SDL_GetError());
			return 1;
		}
		const Sint64 workerPid = SDL_GetNumberProperty(
			SDL_GetProcessProperties(worker), SDL_PROP_PROCESS_PID_NUMBER, -1);
		fprintf(stderr, "INFO: Echelon supervisor started UI worker pid=%lld recovered=%d\n",
			static_cast<long long>(workerPid), recovered ? 1 : 0);
		fflush(stderr);
		int exitCode = -255;
		const bool waited = SDL_WaitProcess(worker, true, &exitCode);
		SDL_DestroyProcess(worker);
		if (!waited) {
			fprintf(stderr, "FATAL: Cannot wait for the Echelon UI worker: %s\n", SDL_GetError());
			return 1;
		}
		if (exitCode == kWorkerExitClean) {
			return 0;
		}

		const auto now = std::chrono::steady_clock::now();
		recoveries.erase(std::remove_if(recoveries.begin(), recoveries.end(), [&](const auto &time) {
			return now - time > std::chrono::minutes(1);
		}), recoveries.end());
		recoveries.push_back(now);
		fprintf(stderr, "WARNING: Echelon UI worker exited with code %d; restarting the launcher (%zu/%d)\n",
			exitCode, recoveries.size(), kMaximumRecoveriesPerMinute);
		fflush(stderr);
		if (recoveries.size() > static_cast<size_t>(kMaximumRecoveriesPerMinute)) {
			fprintf(stderr, "FATAL: Echelon recovery stopped after repeated UI worker failures\n");
			return exitCode == 0 ? 1 : exitCode;
		}
		recovered = true;
	}
}

ParsedArguments ParseArguments(int argc, char **argv)
{
	ParsedArguments parsed;
	for (int index = 1; index < argc; ++index) {
		const std::string argument = argv[index] ? argv[index] : "";
		if (argument == "--launcher") {
			parsed.forceLauncher = true;
		} else if (argument == "--mods") {
			parsed.showMods = true;
			parsed.forceLauncher = true;
		} else if (argument == "--no-mods") {
			parsed.noMods = true;
		} else if (argument.compare(0, 6, "--mod=") == 0) {
			if (!parsed.modSelection.empty()) parsed.argumentError = "--mod may be specified only once";
			parsed.modSelection = ToLower(argument.substr(6));
		} else if (argument.compare(0, 8, "--patch=") == 0) {
			parsed.patchSelections.push_back(ToLower(argument.substr(8)));
		} else if (argument.compare(0, 8, "--addon=") == 0) {
			parsed.addonSelections.push_back(ToLower(argument.substr(8)));
		} else if (argument == "--internal-ui-worker") {
			parsed.internalUIWorker = true;
		} else if (argument == "--internal-recovered") {
			parsed.internalRecovered = true;
		} else if (argument == "--internal-test-supervisor-recovery") {
			parsed.internalTestSupervisorRecovery = true;
		} else if (argument == "--internal-test-open-settings") {
			parsed.internalTestOpenSettings = true;
		} else if (argument.compare(0, 30, "--internal-test-settings-page=") == 0) {
			parsed.internalTestSettingsPage = ToLower(argument.substr(30));
		} else if (argument.compare(0, 26, "--internal-test-mods-page=") == 0) {
			parsed.internalTestModsPage = ToLower(argument.substr(26));
		} else if (argument.compare(0, 28, "--internal-test-mods-engine=") == 0) {
			parsed.internalTestModsEngine = ToLower(argument.substr(28));
		} else if (argument.compare(0, 29, "--internal-test-mods-context=") == 0) {
			parsed.internalTestModsContext = ToLower(argument.substr(29));
		} else if (argument.compare(0, 27, "--internal-test-screenshot=") == 0) {
			parsed.internalTestScreenshot = argument.substr(27);
		} else if (argument.compare(0, 28, "--internal-test-window-size=") == 0) {
			const std::string dimensions = argument.substr(28);
			const size_t separator = dimensions.find('x');
			if (separator != std::string::npos) {
				const long width = std::strtol(dimensions.substr(0, separator).c_str(), nullptr, 10);
				const long height = std::strtol(dimensions.substr(separator + 1).c_str(), nullptr, 10);
				if (width >= 640 && width <= 16384 && height >= 480 && height <= 16384) {
					parsed.internalTestWindowWidth = static_cast<int>(width);
					parsed.internalTestWindowHeight = static_cast<int>(height);
				}
			}
		} else if (argument.compare(0, 29, "--internal-test-settings-row=") == 0) {
			const long value = std::strtol(argument.c_str() + 29, nullptr, 10);
			if (value >= 0 && value <= 1000) parsed.internalTestSettingsRow = static_cast<int>(value);
		} else if (argument.compare(0, 37, "--internal-test-return-after-updates=") == 0) {
			const unsigned long value = std::strtoul(argument.c_str() + 37, nullptr, 10);
			parsed.internalTestReturnAfterUpdates = value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
		} else if (argument.compare(0, 23, "--internal-test-cycles=") == 0) {
			const unsigned long value = std::strtoul(argument.c_str() + 23, nullptr, 10);
			parsed.internalTestCycles = value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
		} else if (argument.compare(0, 10, "--profile=") == 0) {
			parsed.profile = ToLower(argument.substr(10));
		} else {
			if (argument == "-headless" || argument == "--headless") parsed.headless = true;
			parsed.engineArguments.push_back(argument);
		}
	}
	return parsed;
}

LauncherProfile *FindProfile(std::vector<LauncherProfile> &profiles, const std::string &id)
{
	for (LauncherProfile &profile : profiles) {
		if (profile.id == id) return &profile;
	}
	return nullptr;
}

[[noreturn]] void ExitWithoutGlobalDestructors(int exitCode)
{
	std::_Exit(exitCode);
}

} // namespace

// GeneralsX @feature Codex 11/08/2026 Own the single SDL3 window and hand it between launcher and engine modules.
int main(int argc, char **argv)
{
	const ParsedArguments parsed = ParseArguments(argc, argv);
	// Echelon @feature Codex 13/08/2026 Keep a windowless parent alive so a fatal Wayland/Vulkan worker failure is recoverable.
	if (!parsed.internalUIWorker && !parsed.headless) {
		return RunSupervisor(argc, argv);
	}
	const LauncherPaths paths = BuildLauncherPaths();
	const bool rootExistedAtStartup = fs::exists(paths.root);
	std::string setupError;
	if (!EnsureLayout(paths, setupError)) {
		fprintf(stderr, "ERROR: %s\n", setupError.c_str());
		return 1;
	}
	const EchelonLauncher::ModificationRecoverySummary modificationRecovery =
		EchelonLauncher::RecoverInterruptedModificationOperations(paths.mods);
	for (const std::string &warning : modificationRecovery.warnings) {
		fprintf(stderr, "WARNING: %s\n", warning.c_str());
	}
	if (modificationRecovery.resumableDownloads || modificationRecovery.resumableS3Transfers ||
		modificationRecovery.preservedInterruptedImports || modificationRecovery.recoverableTrashEntries) {
		fprintf(stderr, "[MOD-RECOVERY] http=%zu s3=%zu interrupted=%zu trash=%zu\n",
			modificationRecovery.resumableDownloads, modificationRecovery.resumableS3Transfers,
			modificationRecovery.preservedInterruptedImports, modificationRecovery.recoverableTrashEntries);
		fflush(stderr);
	}
	std::vector<LauncherProfile> profiles = LoadProfiles(paths);
	EchelonLauncher::ModificationCatalog modificationCatalog =
		EchelonLauncher::LoadModificationCatalog(paths.mods);

	std::string modificationProfileWarning;
	EchelonLauncher::ModificationSelectionProfileV1 generalsModificationProfile =
		EchelonLauncher::LoadModificationSelectionProfile(paths.mods, kGeneralsProfileId, modificationProfileWarning);
	if (!modificationProfileWarning.empty()) {
		fprintf(stderr, "WARNING: %s\n", modificationProfileWarning.c_str());
		modificationProfileWarning.clear();
	}
	EchelonLauncher::ModificationSelectionProfileV1 zeroHourModificationProfile =
		EchelonLauncher::LoadModificationSelectionProfile(paths.mods, kZeroHourProfileId, modificationProfileWarning);
	if (!modificationProfileWarning.empty()) fprintf(stderr, "WARNING: %s\n", modificationProfileWarning.c_str());
	for (const std::string &warning : modificationCatalog.warnings) {
		fprintf(stderr, "WARNING: %s\n", warning.c_str());
	}
	std::unordered_map<std::string, LoadedModule> modules;
	std::optional<EchelonLauncher::ModificationStack> commandLineStack;
	const bool hasExplicitStack = !parsed.modSelection.empty() || !parsed.patchSelections.empty() || !parsed.addonSelections.empty();
	if (!parsed.argumentError.empty() || (parsed.noMods && hasExplicitStack)) {
		fprintf(stderr, "ERROR: %s\n", !parsed.argumentError.empty() ? parsed.argumentError.c_str() :
			"--no-mods cannot be combined with --mod, --patch, or --addon");
		return 2;
	}
	if (hasExplicitStack) {
		if (parsed.profile.empty()) {
			fprintf(stderr, "ERROR: explicit modification arguments require --profile=generals or --profile=zerohour\n");
			return 2;
		}
		EchelonLauncher::ModificationStack stack;
		std::string stackError;
		if (!EchelonLauncher::ResolveModificationStack(modificationCatalog, parsed.profile,
			parsed.modSelection, parsed.patchSelections, parsed.addonSelections, stack, stackError)) {
			fprintf(stderr, "ERROR: %s\n", stackError.c_str());
			return 2;
		}
		commandLineStack = std::move(stack);
	}
	std::string settingsLoadError;
	if (!EchelonLauncher::RecoverInterruptedSettingsBundle(paths.root, settingsLoadError)) {
		fprintf(stderr, "WARNING: %s\n", settingsLoadError.c_str());
		fflush(stderr);
	}
	settingsLoadError.clear();
	EchelonLauncher::LauncherSettings launcherSettings =
		EchelonLauncher::LoadLauncherSettings(paths.settings, settingsLoadError);
	if (!settingsLoadError.empty()) {
		fprintf(stderr, "WARNING: %s\n", settingsLoadError.c_str());
		fflush(stderr);
	}

	if (parsed.headless) {
		if (parsed.profile.empty()) {
			fprintf(stderr, "ERROR: headless mode requires --profile=generals or --profile=zerohour\n");
			return 2;
		}
		LauncherProfile *profile = FindProfile(profiles, parsed.profile);
		if (!profile || !profile->enabled) {
			fprintf(stderr, "ERROR: requested profile is unavailable in %s\n", paths.root.string().c_str());
			return 2;
		}
		std::string errorMessage;
		uint32_t quiescenceFlags = 0;
		const EchelonEngineResultV2 result = RunProfile(
			*profile, nullptr, parsed.engineArguments, launcherSettings, true,
			parsed.internalTestReturnAfterUpdates, modules, quiescenceFlags, errorMessage, nullptr,
			commandLineStack ? &*commandLineStack : nullptr);
		for (auto &[id, module] : modules) module.api->shutdown();
		if (!errorMessage.empty()) fprintf(stderr, "ERROR: %s\n", errorMessage.c_str());
		ExitWithoutGlobalDestructors(result == ECHELON_ENGINE_FATAL_ERROR ? 1 : 0);
	}

	SetEnvironment("DXVK_WSI_DRIVER", "SDL3");
	FilterSoftwareVulkanICDs();
	SDL_GetLogOutputFunction(&g_previousSDLLogOutput, &g_previousSDLLogUserData);
	SDL_SetLogOutputFunction(LauncherSDLLogOutput, nullptr);
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		fprintf(stderr, "FATAL: SDL initialization failed: %s\n", SDL_GetError());
		return 1;
	}
	if (!SDL_Vulkan_LoadLibrary(nullptr)) {
		fprintf(stderr, "WARNING: Vulkan loader initialization failed: %s\n", SDL_GetError());
	}
	const bool testWindowOverride = parsed.internalTestWindowWidth > 0 || parsed.internalTestWindowHeight > 0 ||
		!parsed.internalTestScreenshot.empty() || parsed.internalTestSupervisorRecovery;
	Uint32 windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
	if (!testWindowOverride && launcherSettings.windowMode == EchelonLauncher::LauncherWindowMode::Fullscreen) {
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}
#if defined(__APPLE__)
	windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
	const int initialWindowWidth = parsed.internalTestWindowWidth > 0 ? parsed.internalTestWindowWidth :
		std::clamp(launcherSettings.windowWidth, 640, 16384);
	const int initialWindowHeight = parsed.internalTestWindowHeight > 0 ? parsed.internalTestWindowHeight :
		std::clamp(launcherSettings.windowHeight, 480, 16384);
	SDL_Window *window = SDL_CreateWindow(EchelonBrand::kProductName, initialWindowWidth, initialWindowHeight, windowFlags);
	if (!window) {
		fprintf(stderr, "FATAL: Cannot create launcher window: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	if (!SDL_SyncWindow(window)) {
		fprintf(stderr, "FATAL: Cannot apply initial launcher display mode: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	fprintf(stderr, "INFO: Echelon launcher display mode=%s size=%dx%d\n",
		(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed",
		initialWindowWidth, initialWindowHeight);
	fflush(stderr);

	SDL_Renderer *renderer = nullptr;
	LauncherFont font;
	LauncherBackdrop backdrop;
	LauncherLogo logo;
	ModCoverCache modCoverCache;
	LauncherUIAudio uiAudio;
	auto createRenderer = [&]() -> bool {
		// Echelon @bugfix Codex 13/08/2026 Use one explicit backend and validate its first present at every handoff.
		renderer = SDL_CreateRenderer(window, "vulkan");
		if (!renderer) return false;
		fprintf(stderr, "INFO: Echelon launcher renderer: %s\n", SDL_GetRendererName(renderer));
		fflush(stderr);
		// GeneralsX @tweak Codex 11/08/2026 Match SAGE menus by scaling the logical UI independently to the full window dimensions.
		SDL_SetRenderLogicalPresentation(renderer, kLogicalWidth, kLogicalHeight, SDL_LOGICAL_PRESENTATION_STRETCH);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		if (!font.initialize(renderer)) {
			fprintf(stderr, "WARNING: Echelon launcher font initialization failed\n");
			fflush(stderr);
		}
		backdrop.initialize(renderer);
		logo.initialize(renderer);
		modCoverCache.reset();
		if (!uiAudio.initialize()) {
			fprintf(stderr, "WARNING: Cannot initialize launcher UI audio: %s\n", SDL_GetError());
			fflush(stderr);
		}
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		if (!SDL_RenderPresent(renderer)) {
			fprintf(stderr, "ERROR: Cannot present the restored launcher surface: %s\n", SDL_GetError());
			fflush(stderr);
			return false;
		}
		return !g_sdlVideoConnectionFailed.load(std::memory_order_acquire);
	};
	if (!createRenderer()) {
		fprintf(stderr, "FATAL: Cannot create launcher renderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	if (parsed.internalTestSupervisorRecovery && !parsed.internalRecovered) {
		fprintf(stderr, "INFO: Echelon supervisor test is terminating the initial UI worker abruptly\n");
		fflush(stderr);
		std::abort();
	}

	bool russian = LauncherUsesRussian(launcherSettings);
	FolderDialogState folderDialog;
	FolderDialogState modificationFolderDialog;
	FileDialogState modificationFileDialog;
	bool running = true;
	bool settingsOpen = false;
	bool modsOpen = false;
	bool settingsTextEditing = false;
	SettingsPage settingsPage = SettingsPage::Generals;
	ModsPage modsPage = ModsPage::Mods;
	ModsContextMode modsContextMode = ModsContextMode::None;
	std::string modsEngine = kGeneralsProfileId;
	int modsSelectedItem = 0;
	int modsScroll = 0;
	int settingsSelectedRow = 0;
	int settingsScroll = 0;
	EchelonLauncher::LauncherSettings settingsDraft = launcherSettings;
	EchelonLauncher::GameOptions generalsOptionsDraft;
	EchelonLauncher::GameOptions zeroHourOptionsDraft;
	EchelonLauncher::SagePatchOptions generalsSagePatchDraft;
	EchelonLauncher::SagePatchOptions zeroHourSagePatchDraft;
	bool initialPickerPending = !rootExistedAtStartup;
	bool pickerWasAutomatic = false;
	float mouseX = -1.0f;
	float mouseY = -1.0f;
	std::string statusMessage;
	bool statusError = false;
	int workerExitCode = kWorkerExitClean;
	bool internalScreenshotWritten = false;
	std::future<MigrationResult> migration;
	bool migrationRunning = false;
	std::future<EchelonLauncher::ModificationOperationResult> modificationOperation;
	bool modificationOperationRunning = false;
	std::shared_ptr<EchelonLauncher::ModificationOperationProgress> modificationOperationProgress;
	std::optional<EchelonLauncher::ModificationType> modificationOperationType;
	std::string modificationOperationEngine;
	std::optional<EchelonLauncher::ModificationStack> pendingModificationLaunch;
	std::vector<fs::path> modificationImportQueue;
	ModificationFileDialogPurpose modificationFileDialogPurpose = ModificationFileDialogPurpose::Import;
	std::string modificationCoverTargetEngine;
	std::string modificationCoverTargetSelection;
	std::vector<EchelonLauncher::RecoverableModification> recoverableModifications =
		EchelonLauncher::ListRecoverableModifications(paths.mods);
	EchelonLauncher::LocalImportRequest activeImportRequest;
	Uint64 transitionEpoch = SDL_GetTicks();
	Button *capturedButton = nullptr;
	int capturedModificationCard = -1;
	float modificationCardPressY = -1.0f;
	bool modificationCardDragging = false;
	Button *focusedButton = nullptr;
	if (parsed.internalTestSupervisorRecovery && parsed.internalRecovered) {
		fprintf(stderr, "INFO: Echelon supervisor recovery test completed\n");
		fflush(stderr);
		running = false;
	} else if (parsed.internalRecovered) {
		statusMessage = Localized(russian,
			"The launcher recovered after a graphics failure",
			"Лаунчер восстановлен после графического сбоя");
		statusError = true;
	}

	// Echelon @refactor Codex 13/08/2026 Keep SAGE button behavior while giving every launcher control one emerald palette.
	Button generalsButton{kLauncherLayout.generalsButton, "GENERALS"};
	generalsButton.transitionDelay = 5;
	Button zeroHourButton{kLauncherLayout.zeroHourButton, "ZERO HOUR"};
	zeroHourButton.transitionDelay = 6;
	Button modsButton{kLauncherLayout.modsButton, Localized(russian, "MODS", "МОДЫ")};
	modsButton.transitionDelay = 7;
	Button settingsButton{kLauncherLayout.settingsButton, Localized(russian, "OPTIONS", "НАСТРОЙКИ")};
	settingsButton.transitionDelay = 8;
	Button exitButton{kLauncherLayout.exitButton, Localized(russian, "EXIT", "ВЫХОД")};
	exitButton.transitionDelay = 9;
	std::vector<Button *> mainButtons{&generalsButton, &zeroHourButton, &modsButton, &settingsButton, &exitButton};

	Button settingsGeneralsTab{kSettingsLayout.generalsTab, "GENERALS"};
	Button settingsZeroHourTab{kSettingsLayout.zeroHourTab, "ZERO HOUR"};
	Button settingsLauncherTab{kSettingsLayout.launcherTab, Localized(russian, "LAUNCHER", "ЛАУНЧЕР")};
	Button settingsScrollUp{kSettingsLayout.scrollUp, ""};
	Button settingsScrollDown{kSettingsLayout.scrollDown, ""};
	Button settingsDefaults{kSettingsLayout.defaultsButton, Localized(russian, "DEFAULTS", "ПО УМОЛЧАНИЮ")};
	Button settingsCancel{kSettingsLayout.cancelButton, Localized(russian, "CANCEL", "ОТМЕНА")};
	Button settingsApply{kSettingsLayout.applyButton, Localized(russian, "APPLY", "ПРИМЕНИТЬ")};
	std::vector<Button *> settingsButtons{
		&settingsGeneralsTab, &settingsZeroHourTab, &settingsLauncherTab,
		&settingsScrollUp, &settingsScrollDown, &settingsDefaults, &settingsCancel, &settingsApply};

	Button modsGeneralsTab{kModsLayout.generalsTab, "GENERALS"};
	Button modsZeroHourTab{kModsLayout.zeroHourTab, "ZERO HOUR"};
	Button modsModsTab{kModsLayout.modsTab, Localized(russian, "MODS", "МОДЫ")};
	Button modsPatchesTab{kModsLayout.patchesTab, Localized(russian, "PATCHES", "ПАТЧИ")};
	Button modsAddonsTab{kModsLayout.addonsTab, Localized(russian, "ADDONS", "АДДОНЫ")};
	Button modsVersion{kModsLayout.versionButton, Localized(russian, "INSTALLED VERSION", "УСТАНОВЛЕННАЯ ВЕРСИЯ")};
	Button modsVerify{kModsLayout.verifyButton, Localized(russian, "VERIFY", "ПРОВЕРИТЬ")};
	Button modsCover{kModsLayout.coverButton, Localized(russian, "CHANGE IMAGE", "СМЕНИТЬ КАРТИНКУ")};
	Button modsFolder{kModsLayout.folderButton, Localized(russian, "OPEN FOLDER", "ОТКРЫТЬ ПАПКУ")};
	Button modsLink{kModsLayout.linkButton, Localized(russian, "OPEN LINK", "ОТКРЫТЬ ССЫЛКУ")};
	Button modsImport{kModsLayout.importButton, Localized(russian, "ADD FROM FILES", "ДОБАВИТЬ ИЗ ФАЙЛОВ")};
	Button modsImportFolder{kModsLayout.importFolderButton, Localized(russian, "ADD FOLDER", "ДОБАВИТЬ ПАПКУ")};
	Button modsRemove{kModsLayout.removeButton, Localized(russian, "REMOVE", "УДАЛИТЬ")};
	Button modsRestore{kModsLayout.restoreButton, Localized(russian, "RESTORE", "ВОССТАНОВИТЬ")};
	Button modsLaunch{kModsLayout.launchButton, Localized(russian, "LAUNCH", "ЗАПУСТИТЬ")};
	Button modsBack{kModsLayout.backButton, Localized(russian, "BACK", "НАЗАД")};
	Button modsContextFirst{kModsContextLayout.first, ""};
	Button modsContextSecond{kModsContextLayout.second, ""};
	Button modsContextThird{kModsContextLayout.third, ""};
	Button modsContextFourth{kModsContextLayout.fourth, ""};
	Button modsContextClose{kModsContextLayout.close, Localized(russian, "CLOSE", "ЗАКРЫТЬ")};
	std::vector<Button *> modsButtons{&modsGeneralsTab, &modsZeroHourTab, &modsModsTab, &modsPatchesTab,
		&modsAddonsTab, &modsVersion, &modsVerify, &modsCover, &modsFolder, &modsLink,
		&modsImport, &modsImportFolder, &modsRemove, &modsRestore, &modsLaunch, &modsBack};
	std::vector<Button *> modsContextButtons{
		&modsContextFirst, &modsContextSecond, &modsContextThird, &modsContextFourth, &modsContextClose};

	// Echelon @feature Codex 13/08/2026 Apply display settings only when the user explicitly changes them.
	auto applyLauncherWindowSettings = [&](const EchelonLauncher::LauncherSettings &settings, bool centerWindow) -> bool {
		const bool fullscreen = !testWindowOverride &&
			settings.windowMode == EchelonLauncher::LauncherWindowMode::Fullscreen;
		return fullscreen ? ApplyFullscreenPresentation(window) :
			ApplyWindowedPresentation(window, std::clamp(settings.windowWidth, 640, 16384),
				std::clamp(settings.windowHeight, 480, 16384), centerWindow);
	};

	auto updateLocalizedButtonLabels = [&]() {
		modsButton.label = Localized(russian, "MODS", "МОДЫ");
		settingsButton.label = Localized(russian, "OPTIONS", "НАСТРОЙКИ");
		exitButton.label = Localized(russian, "EXIT", "ВЫХОД");
		settingsLauncherTab.label = Localized(russian, "LAUNCHER", "ЛАУНЧЕР");
		settingsDefaults.label = Localized(russian, "DEFAULTS", "ПО УМОЛЧАНИЮ");
		settingsCancel.label = Localized(russian, "CANCEL", "ОТМЕНА");
		settingsApply.label = Localized(russian, "APPLY", "ПРИМЕНИТЬ");
		modsModsTab.label = Localized(russian, "MODS", "МОДЫ");
		modsPatchesTab.label = Localized(russian, "PATCHES", "ПАТЧИ");
		modsAddonsTab.label = Localized(russian, "ADDONS", "АДДОНЫ");
		modsVersion.label = Localized(russian, "INSTALLED VERSION", "УСТАНОВЛЕННАЯ ВЕРСИЯ");
		modsVerify.label = Localized(russian, "VERIFY", "ПРОВЕРИТЬ");
		modsCover.label = Localized(russian, "CHANGE IMAGE", "СМЕНИТЬ КАРТИНКУ");
		modsFolder.label = Localized(russian, "OPEN FOLDER", "ОТКРЫТЬ ПАПКУ");
		modsLink.label = Localized(russian, "OPEN LINK", "ОТКРЫТЬ ССЫЛКУ");
		modsImport.label = Localized(russian, "ADD FROM FILES", "ДОБАВИТЬ ИЗ ФАЙЛОВ");
		modsImportFolder.label = Localized(russian, "ADD FOLDER", "ДОБАВИТЬ ПАПКУ");
		modsRemove.label = Localized(russian, "REMOVE", "УДАЛИТЬ");
		modsRestore.label = Localized(russian, "RESTORE", "ВОССТАНОВИТЬ");
		modsLaunch.label = Localized(russian, "LAUNCH", "ЗАПУСТИТЬ");
		modsBack.label = Localized(russian, "BACK", "НАЗАД");
		modsContextClose.label = Localized(russian, "CLOSE", "ЗАКРЫТЬ");
	};

	auto selectedModificationType = [&]() {
		switch (modsPage) {
			case ModsPage::Patches: return EchelonLauncher::ModificationType::Patch;
			case ModsPage::Addons: return EchelonLauncher::ModificationType::Addon;
			default: return EchelonLauncher::ModificationType::Mod;
		}
	};
	auto currentModificationProfile = [&]() -> EchelonLauncher::ModificationSelectionProfileV1 & {
		return modsEngine == kZeroHourProfileId ? zeroHourModificationProfile : generalsModificationProfile;
	};
	auto saveModificationProfile = [&](EchelonLauncher::ModificationSelectionProfileV1 &profile) {
		std::string profileError;
		if (!EchelonLauncher::SaveModificationSelectionProfile(paths.mods, profile, profileError)) {
			statusMessage = profileError;
			statusError = true;
			return false;
		}
		return true;
	};
	auto reconcileModificationProfiles = [&]() {
		for (EchelonLauncher::ModificationSelectionProfileV1 *profile :
			{&generalsModificationProfile, &zeroHourModificationProfile}) {
			std::string profileWarning;
			if (EchelonLauncher::ReconcileModificationSelectionProfile(
				modificationCatalog, *profile, profileWarning)) {
				saveModificationProfile(*profile);
				if (!profileWarning.empty()) {
					statusMessage = profileWarning;
					statusError = true;
				}
			}
		}
	};
	auto parentMatchesActiveStack = [&](const std::string &parentId,
		EchelonLauncher::ModificationType type) {
		if (parentId.empty() || type == EchelonLauncher::ModificationType::Mod) return true;
		const auto &profile = currentModificationProfile();
		const auto *activeMod = EchelonLauncher::FindModification(
			modificationCatalog, modsEngine, profile.modSelection);
		const auto *activePatch = EchelonLauncher::FindModification(
			modificationCatalog, modsEngine, profile.patchSelection);
		return (activeMod && parentId == activeMod->id) ||
			(type == EchelonLauncher::ModificationType::Addon && activePatch && parentId == activePatch->id);
	};
	auto visibleModifications = [&]() {
		std::vector<ModificationViewItem> items;
		const auto selectedType = selectedModificationType();
		for (const EchelonLauncher::InstalledModification &installed : modificationCatalog.modifications) {
			if (installed.engine != modsEngine || installed.type != selectedType ||
				!parentMatchesActiveStack(installed.parentId, installed.type)) continue;
			auto represented = std::find_if(items.begin(), items.end(), [&](const ModificationViewItem &item) {
				return ToLower(item.id()) == ToLower(installed.id);
			});
			if (represented == items.end()) {
				items.push_back({&installed});
				continue;
			}
			const std::string activeSelection = selectedType == EchelonLauncher::ModificationType::Mod ?
				currentModificationProfile().modSelection :
				(selectedType == EchelonLauncher::ModificationType::Patch ?
					currentModificationProfile().patchSelection : std::string{});
			const bool candidateIsActive = EchelonLauncher::ModificationSelectionKey(installed) == activeSelection ||
				(selectedType == EchelonLauncher::ModificationType::Addon &&
					EchelonLauncher::IsModificationSelected(currentModificationProfile(), installed));
			const bool currentIsActive = represented->installed &&
				EchelonLauncher::IsModificationSelected(currentModificationProfile(), *represented->installed);
			if (candidateIsActive || (!currentIsActive &&
				EchelonLauncher::CompareModificationVersions(represented->installed->version, installed.version) < 0)) {
				represented->installed = &installed;
			}
		}
		std::sort(items.begin(), items.end(), [&](const ModificationViewItem &left, const ModificationViewItem &right) {
			if (selectedType == EchelonLauncher::ModificationType::Addon) {
				auto activeOrder = [&](const ModificationViewItem &item) {
					if (!item.installed) return std::numeric_limits<size_t>::max();
					const auto &addons = currentModificationProfile().addonSelections;
					const auto found = std::find(addons.begin(), addons.end(),
						EchelonLauncher::ModificationSelectionKey(*item.installed));
					return found == addons.end() ? std::numeric_limits<size_t>::max() :
						static_cast<size_t>(std::distance(addons.begin(), found));
				};
				const size_t leftOrder = activeOrder(left);
				const size_t rightOrder = activeOrder(right);
				if (leftOrder != rightOrder) return leftOrder < rightOrder;
			}
			if (left.name() != right.name()) return left.name() < right.name();
			return EchelonLauncher::CompareModificationVersions(left.version(), right.version()) < 0;
		});
		return items;
	};
	auto installedVersions = [&](const ModificationViewItem *item) {
		std::vector<const EchelonLauncher::InstalledModification *> versions;
		if (!item) return versions;
		for (const auto &installed : modificationCatalog.modifications) {
			if (installed.engine == modsEngine && installed.type == item->type() &&
				ToLower(installed.id) == ToLower(item->id())) versions.push_back(&installed);
		}
		std::sort(versions.begin(), versions.end(), [](const auto *left, const auto *right) {
			return EchelonLauncher::CompareModificationVersions(left->version, right->version) < 0;
		});
		return versions;
	};
	auto selectedModificationItem = [&]() -> std::optional<ModificationViewItem> {
		const std::vector<ModificationViewItem> items = visibleModifications();
		if (items.empty()) return std::nullopt;
		return items[static_cast<size_t>(std::clamp(modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
	};
	auto modificationContextFolders = [&]() {
		std::vector<fs::path> folders(4);
		const std::optional<ModificationViewItem> selected = selectedModificationItem();
		if (selected && selected->installed) folders[0] = selected->installed->launchPath;
		if (const LauncherProfile *profile = FindProfile(profiles, modsEngine)) {
			folders[1] = profile->assetRoot;
			folders[2] = profile->userDataRoot / "Maps";
			folders[3] = profile->userDataRoot / "Replays";
		}
		return folders;
	};
	auto modificationContextLinks = [&]() {
		std::vector<std::string> links(4);
		const std::optional<ModificationViewItem> selected = selectedModificationItem();
		if (!selected) return links;
		links[0] = selected->installed->source;
		for (std::string &link : links) {
			if (link.rfind("https://", 0) != 0) link.clear();
		}
		return links;
	};
	auto clampModsSelection = [&]() {
		const int count = static_cast<int>(visibleModifications().size());
		if (count == 0) {
			modsSelectedItem = 0;
			modsScroll = 0;
			return;
		}
		modsSelectedItem = std::clamp(modsSelectedItem, 0, count - 1);
		modsScroll = std::clamp(modsScroll, 0, std::max(0, count - 4));
		if (modsSelectedItem < modsScroll) modsScroll = modsSelectedItem;
		if (modsSelectedItem >= modsScroll + 4) modsScroll = modsSelectedItem - 3;
	};
	auto reloadModificationCatalog = [&]() {
		modificationCatalog = EchelonLauncher::LoadModificationCatalog(paths.mods);
		recoverableModifications = EchelonLauncher::ListRecoverableModifications(paths.mods);
		reconcileModificationProfiles();
		modCoverCache.reset();
		clampModsSelection();
		statusMessage.clear();
		statusError = false;
		if (!modificationCatalog.warnings.empty()) {
			statusMessage = modificationCatalog.warnings.front();
			statusError = true;
		}
	};
	auto startNextModificationImport = [&]() {
		if (modificationOperationRunning || modificationImportQueue.empty()) return;
		activeImportRequest = {};
		activeImportRequest.inputPath = modificationImportQueue.front();
		modificationImportQueue.erase(modificationImportQueue.begin());
		activeImportRequest.modsRoot = paths.mods;
		activeImportRequest.engine = modsEngine;
		activeImportRequest.type = selectedModificationType();
		const auto &profile = currentModificationProfile();
		const auto *activeMod = EchelonLauncher::FindModification(
			modificationCatalog, modsEngine, profile.modSelection);
		const auto *activePatch = EchelonLauncher::FindModification(
			modificationCatalog, modsEngine, profile.patchSelection);
		if (activeImportRequest.type == EchelonLauncher::ModificationType::Patch && activeMod) {
			activeImportRequest.parentId = activeMod->id;
		} else if (activeImportRequest.type == EchelonLauncher::ModificationType::Addon) {
			if (activePatch) activeImportRequest.parentId = activePatch->id;
			else if (activeMod) activeImportRequest.parentId = activeMod->id;
		}
		activeImportRequest.displayName = activeImportRequest.inputPath.stem().string();
		modificationOperationProgress = std::make_shared<EchelonLauncher::ModificationOperationProgress>();
		const auto request = activeImportRequest;
		const auto progress = modificationOperationProgress;
		modificationOperationRunning = true;
		modificationOperationType = request.type;
		modificationOperationEngine = request.engine;
		statusMessage = Localized(russian, "IMPORTING: ", "ИМПОРТ: ") + request.inputPath.filename().string();
		statusError = false;
		modificationOperation = std::async(std::launch::async, [request, progress]() {
			return EchelonLauncher::ImportLocalModification(request, progress.get());
		});
	};
	auto openMods = [&]() {
		reloadModificationCatalog();
		modsOpen = true;
		settingsOpen = false;
		modsSelectedItem = 0;
		modsScroll = 0;
		modsContextMode = ModsContextMode::None;
		capturedButton = nullptr;
		capturedModificationCard = -1;
		modificationCardDragging = false;
		focusedButton = nullptr;
		for (Button *button : mainButtons) {
			button->focused = false;
			button->selected = false;
		}
	};
	auto closeMods = [&]() {
		modsOpen = false;
		modsContextMode = ModsContextMode::None;
		capturedButton = nullptr;
		capturedModificationCard = -1;
		modificationCardDragging = false;
		focusedButton = nullptr;
		for (Button *button : modsButtons) {
			button->hilited = false;
			button->selected = false;
		}
		for (Button *button : modsContextButtons) {
			button->hilited = false;
			button->selected = false;
		}
		transitionEpoch = SDL_GetTicks();
	};

	auto openSettings = [&]() {
		statusMessage.clear();
		statusError = false;
		settingsDraft = launcherSettings;
		modificationCatalog = EchelonLauncher::LoadModificationCatalog(paths.mods);
		std::string optionsError;
		generalsOptionsDraft = EchelonLauncher::LoadGameOptions(
			paths.root / "UserData" / "Generals" / "Options.ini", optionsError);
		if (!optionsError.empty()) {
			statusMessage = optionsError;
			statusError = true;
		}
		optionsError.clear();
		zeroHourOptionsDraft = EchelonLauncher::LoadGameOptions(
			paths.root / "UserData" / "GeneralsZH" / "Options.ini", optionsError);
		if (!optionsError.empty()) {
			statusMessage = optionsError;
			statusError = true;
		}
		optionsError.clear();
		generalsSagePatchDraft = EchelonLauncher::LoadSagePatchOptions(
			paths.root / "UserData" / "Generals" / "SagePatch.ini", optionsError);
		if (!optionsError.empty()) {
			statusMessage = optionsError;
			statusError = true;
		}
		optionsError.clear();
		zeroHourSagePatchDraft = EchelonLauncher::LoadSagePatchOptions(
			paths.root / "UserData" / "GeneralsZH" / "SagePatch.ini", optionsError);
		if (!optionsError.empty()) {
			statusMessage = optionsError;
			statusError = true;
		}
		settingsOpen = true;
		modsOpen = false;
		settingsTextEditing = false;
		settingsPage = SettingsPage::Generals;
		settingsSelectedRow = 0;
		settingsScroll = 0;
		capturedButton = nullptr;
		focusedButton = nullptr;
		for (Button *button : mainButtons) {
			button->focused = false;
			button->selected = false;
		}
	};

	auto closeSettings = [&]() {
		if (settingsTextEditing) SDL_StopTextInput(window);
		settingsTextEditing = false;
		settingsOpen = false;
		capturedButton = nullptr;
		focusedButton = nullptr;
	};

	auto saveSettings = [&]() -> bool {
		std::string errorMessage;
		if (!applyLauncherWindowSettings(settingsDraft, true)) {
			statusMessage = std::string("Cannot apply launcher display settings: ") + SDL_GetError();
			statusError = true;
			return false;
		}
		if (!EchelonLauncher::SaveSettingsBundle(paths.root, settingsDraft,
			generalsOptionsDraft, zeroHourOptionsDraft, generalsSagePatchDraft, zeroHourSagePatchDraft, errorMessage)) {
			if (!applyLauncherWindowSettings(launcherSettings, true)) {
				fprintf(stderr, "WARNING: Cannot roll back launcher display settings: %s\n", SDL_GetError());
				fflush(stderr);
			}
			statusMessage = errorMessage;
			statusError = true;
			return false;
		}
		launcherSettings = settingsDraft;
		russian = LauncherUsesRussian(launcherSettings);
		updateLocalizedButtonLabels();
		statusMessage = Localized(russian, "Settings saved", "Настройки сохранены");
		statusError = false;
		closeSettings();
		for (Button *button : mainButtons) {
			button->hilited = false;
			button->selected = false;
			button->focused = false;
			button->lastTransitionFrame = -1;
		}
		transitionEpoch = SDL_GetTicks();
		return true;
	};

	constexpr int kGameSettingsRowCount = 26;
	// Echelon @tweak Codex 14/08/2026 Keep the launcher page limited to persistent launcher preferences.
	constexpr int kLauncherSettingsRowCount = 3;
	constexpr int kVisibleSettingsRows = 9;
	auto settingsRowCount = [&]() {
		return settingsPage == SettingsPage::Launcher ? kLauncherSettingsRowCount : kGameSettingsRowCount;
	};
	auto clampSettingsSelection = [&]() {
		settingsSelectedRow = std::clamp(settingsSelectedRow, 0, settingsRowCount() - 1);
		const int maximumScroll = std::max(0, settingsRowCount() - kVisibleSettingsRows);
		settingsScroll = std::clamp(settingsScroll, 0, maximumScroll);
		if (settingsSelectedRow < settingsScroll) settingsScroll = settingsSelectedRow;
		if (settingsSelectedRow >= settingsScroll + kVisibleSettingsRows) {
			settingsScroll = settingsSelectedRow - kVisibleSettingsRows + 1;
		}
	};
	auto currentLaunchSettings = [&]() -> EchelonLauncher::ProfileLaunchSettings & {
		return settingsPage == SettingsPage::ZeroHour ? settingsDraft.zeroHour : settingsDraft.generals;
	};
	auto currentGameOptions = [&]() -> EchelonLauncher::GameOptions & {
		return settingsPage == SettingsPage::ZeroHour ? zeroHourOptionsDraft : generalsOptionsDraft;
	};
	auto currentSagePatchOptions = [&]() -> EchelonLauncher::SagePatchOptions & {
		return settingsPage == SettingsPage::ZeroHour ? zeroHourSagePatchDraft : generalsSagePatchDraft;
	};
	auto cycleResolution = [&](EchelonLauncher::GameOptions &options, int direction) {
		static const std::pair<int, int> resolutions[] = {
			{1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1440, 900},
			{1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440},
			{2560, 1600}, {3440, 1440}, {3840, 2160}
		};
		int index = 0;
		for (size_t candidate = 0; candidate < std::size(resolutions); ++candidate) {
			if (resolutions[candidate].first == options.resolutionWidth && resolutions[candidate].second == options.resolutionHeight) {
				index = static_cast<int>(candidate);
				break;
			}
		}
		index = (index + direction + static_cast<int>(std::size(resolutions))) % static_cast<int>(std::size(resolutions));
		options.resolutionWidth = resolutions[index].first;
		options.resolutionHeight = resolutions[index].second;
	};
	auto cycleLauncherResolution = [&](int direction) {
		static const std::pair<int, int> resolutions[] = {
			{800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768},
			{1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200},
			{2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160}
		};
		int index = 3;
		for (size_t candidate = 0; candidate < std::size(resolutions); ++candidate) {
			if (resolutions[candidate].first == settingsDraft.windowWidth &&
				resolutions[candidate].second == settingsDraft.windowHeight) {
				index = static_cast<int>(candidate);
				break;
			}
		}
		index = (index + direction + static_cast<int>(std::size(resolutions))) % static_cast<int>(std::size(resolutions));
		settingsDraft.windowWidth = resolutions[index].first;
		settingsDraft.windowHeight = resolutions[index].second;
	};
	auto adjustSettingsRow = [&](int direction) {
		if (settingsPage == SettingsPage::Launcher) {
			if (settingsSelectedRow == 0) {
				int language = static_cast<int>(settingsDraft.language);
				language = (language + direction + 3) % 3;
				settingsDraft.language = static_cast<EchelonLauncher::LanguageMode>(language);
			} else if (settingsSelectedRow == 1) {
				settingsDraft.windowMode = settingsDraft.windowMode == EchelonLauncher::LauncherWindowMode::Windowed ?
					EchelonLauncher::LauncherWindowMode::Fullscreen : EchelonLauncher::LauncherWindowMode::Windowed;
			} else if (settingsSelectedRow == 2) {
				cycleLauncherResolution(direction);
			}
			return;
		}
		auto &launch = currentLaunchSettings();
		auto &options = currentGameOptions();
		auto &sagePatch = currentSagePatchOptions();
		switch (settingsSelectedRow) {
			case 0: launch.windowed = !launch.windowed; break;
			case 1: launch.quickStart = !launch.quickStart; break;
			case 2: launch.noShellMap = !launch.noShellMap; break;
			case 3:
				settingsTextEditing = true;
				SDL_StartTextInput(window);
				break;
			case 4: cycleResolution(options, direction); break;
			case 5: options.maxParticleCount = std::clamp(options.maxParticleCount + direction * 100, 100, 10000); break;
			case 6: options.textureReduction = std::clamp(options.textureReduction - direction, 0, 2); break;
			case 7: options.useShadowVolumes = !options.useShadowVolumes; break;
			case 8: options.buildingOcclusion = !options.buildingOcclusion; break;
			case 9: options.useShadowDecals = !options.useShadowDecals; break;
			case 10: options.showTrees = !options.showTrees; break;
			case 11: options.useCloudMap = !options.useCloudMap; break;
			case 12: options.extraAnimations = !options.extraAnimations; break;
			case 13: options.useLightMap = !options.useLightMap; break;
			case 14: options.dynamicLOD = !options.dynamicLOD; break;
			case 15: options.showSoftWaterEdge = !options.showSoftWaterEdge; break;
			case 16: options.heatEffects = !options.heatEffects; break;
			case 17: options.useAlternateMouse = !options.useAlternateMouse; break;
			case 18:
				sagePatch.maxCameraHeight = std::clamp(sagePatch.maxCameraHeight + direction * 25.0f, 250.0f, 1000.0f);
				if (sagePatch.minCameraHeight > sagePatch.maxCameraHeight) sagePatch.minCameraHeight = sagePatch.maxCameraHeight;
				break;
			case 19:
				sagePatch.minCameraHeight = std::clamp(sagePatch.minCameraHeight + direction * 10.0f,
					40.0f, std::min(250.0f, sagePatch.maxCameraHeight));
				break;
			case 20: sagePatch.enforceMaxCameraHeight = !sagePatch.enforceMaxCameraHeight; break;
			case 21: sagePatch.keyboardScrollSpeed = std::clamp(sagePatch.keyboardScrollSpeed + direction * 0.25f, 0.25f, 3.0f); break;
			case 22: sagePatch.terrainDrawDistanceScale = std::clamp(sagePatch.terrainDrawDistanceScale + direction * 0.05f, 0.75f, 2.0f); break;
			case 23: sagePatch.useFpsLimit = !sagePatch.useFpsLimit; break;
			case 24: {
				static constexpr int limits[] = {30, 60, 75, 90, 120, 144, 165, 240, 360};
				int selected = 1;
				for (size_t index = 0; index < std::size(limits); ++index) {
					if (limits[index] == sagePatch.framesPerSecondLimit) selected = static_cast<int>(index);
				}
				selected = (selected + direction + static_cast<int>(std::size(limits))) % static_cast<int>(std::size(limits));
				sagePatch.framesPerSecondLimit = limits[selected];
				break;
			}
			case 25: launch.russianLocalization = !launch.russianLocalization; break;
		}
	};
	auto applySettingsDefaults = [&]() {
		if (settingsPage == SettingsPage::Launcher) {
			settingsDraft.language = EchelonLauncher::LanguageMode::System;
			settingsDraft.windowMode = EchelonLauncher::LauncherWindowMode::Windowed;
			settingsDraft.windowWidth = 1280;
			settingsDraft.windowHeight = 800;
		} else {
			currentLaunchSettings() = {};
			currentGameOptions() = {};
			currentSagePatchOptions() = {};
		}
	};
	auto setSettingsPage = [&](SettingsPage page) {
		if (settingsTextEditing) SDL_StopTextInput(window);
		settingsTextEditing = false;
		settingsPage = page;
		settingsSelectedRow = 0;
		settingsScroll = 0;
	};
	auto settingsRowAt = [&](float x, float y) -> int {
		if (!Contains(kSettingsLayout.content, x, y)) return -1;
		constexpr float rowHeight = 50.0f;
		constexpr float rowGap = 5.0f;
		const int visible = static_cast<int>((y - kSettingsLayout.content.y - 18.0f) / (rowHeight + rowGap));
		if (visible < 0 || visible >= kVisibleSettingsRows) return -1;
		const int row = settingsScroll + visible;
		return row < settingsRowCount() ? row : -1;
	};
	auto settingsButtonAt = [&](float x, float y) -> Button * {
		for (Button *button : settingsButtons) {
			if (Contains(button->rect, x, y)) return button;
		}
		return nullptr;
	};
	auto modsButtonAt = [&](float x, float y) -> Button * {
		const std::vector<Button *> &buttons = modsContextMode == ModsContextMode::None ?
			modsButtons : modsContextButtons;
		for (Button *button : buttons) {
			if (button->enabled && Contains(button->rect, x, y)) return button;
		}
		return nullptr;
	};
	auto modsCardAt = [&](float x, float y) -> int {
		if (!Contains(kModsLayout.catalog, x, y)) return -1;
		for (int visible = 0; visible < 4; ++visible) {
			const SDL_FRect card{kModsLayout.catalog.x + 15.0f, kModsLayout.catalog.y + 15.0f + visible * 137.0f,
				kModsLayout.catalog.w - 30.0f, 120.0f};
			if (Contains(card, x, y)) {
				const int index = modsScroll + visible;
				return index < static_cast<int>(visibleModifications().size()) ? index : -1;
			}
		}
		return -1;
	};
	auto setModsPage = [&](ModsPage page) {
		modsPage = page;
		modsContextMode = ModsContextMode::None;
		modsSelectedItem = 0;
		modsScroll = 0;
		clampModsSelection();
	};
	auto selectCompatibleModification = [&](EchelonLauncher::ModificationSelectionProfileV1 &profile,
		const EchelonLauncher::InstalledModification *modification, std::string &errorMessage) {
		return EchelonLauncher::SelectCompatibleModification(
			modificationCatalog, profile, modification, errorMessage);
	};
	auto activateSelectedModificationCard = [&]() {
		const std::vector<ModificationViewItem> items = visibleModifications();
		if (items.empty()) return;
		const ModificationViewItem &selected = items[static_cast<size_t>(
			std::clamp(modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
		if (!selected.installed) return;
		std::string selectionError;
		if (!selectCompatibleModification(currentModificationProfile(),
			selected.installed, selectionError)) {
			statusMessage = selectionError;
			statusError = true;
			return;
		}
		std::string reconcileWarning;
		EchelonLauncher::ReconcileModificationSelectionProfile(
			modificationCatalog, currentModificationProfile(), reconcileWarning);
		if (saveModificationProfile(currentModificationProfile())) {
			statusMessage = Localized(russian, "ACTIVE STACK UPDATED", "АКТИВНЫЙ СТЕК ОБНОВЛЁН");
			statusError = false;
		}
		clampModsSelection();
	};
	auto activateModsButton = [&](Button *button) {
		if (!button || !button->enabled) return;
		if (button == &modsContextClose) {
			modsContextMode = ModsContextMode::None;
		} else if (button == &modsContextFirst || button == &modsContextSecond ||
			button == &modsContextThird || button == &modsContextFourth) {
			const size_t index = button == &modsContextFirst ? 0 :
				(button == &modsContextSecond ? 1 : (button == &modsContextThird ? 2 : 3));
			if (modsContextMode == ModsContextMode::Folders) {
				const std::vector<fs::path> folders = modificationContextFolders();
				if (!folders[index].empty()) {
					std::error_code directoryError;
					if (index >= 2) fs::create_directories(folders[index], directoryError);
					if (directoryError) {
						statusMessage = "Cannot prepare folder: " + directoryError.message();
						statusError = true;
					} else if (!SDL_OpenURL(FileUri(folders[index]).c_str())) {
						statusMessage = SDL_GetError();
						statusError = true;
					}
				}
			} else if (modsContextMode == ModsContextMode::Links) {
				const std::vector<std::string> links = modificationContextLinks();
				if (!links[index].empty() && !SDL_OpenURL(links[index].c_str())) {
					statusMessage = SDL_GetError();
					statusError = true;
				}
			}
		} else if (button == &modsGeneralsTab) {
			modsEngine = kGeneralsProfileId;
			modsContextMode = ModsContextMode::None;
			modsSelectedItem = 0;
			modsScroll = 0;
		} else if (button == &modsZeroHourTab) {
			modsEngine = kZeroHourProfileId;
			modsContextMode = ModsContextMode::None;
			modsSelectedItem = 0;
			modsScroll = 0;
		} else if (button == &modsModsTab) setModsPage(ModsPage::Mods);
		else if (button == &modsPatchesTab) setModsPage(ModsPage::Patches);
		else if (button == &modsAddonsTab) setModsPage(ModsPage::Addons);
		else if (button == &modsVersion) {
			const std::vector<ModificationViewItem> items = visibleModifications();
			if (!items.empty()) {
				const auto &selected = items[static_cast<size_t>(std::clamp(
					modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
				const auto versions = installedVersions(&selected);
				if (versions.size() > 1) {
					auto current = std::find(versions.begin(), versions.end(), selected.installed);
					if (current == versions.end() || ++current == versions.end()) current = versions.begin();
					std::string selectionError;
					if (selectCompatibleModification(
						currentModificationProfile(), *current, selectionError)) {
						std::string reconcileWarning;
						EchelonLauncher::ReconcileModificationSelectionProfile(
							modificationCatalog, currentModificationProfile(), reconcileWarning);
						saveModificationProfile(currentModificationProfile());
					}
				}
			}
		}
		else if (button == &modsVerify) {
			const std::vector<ModificationViewItem> items = visibleModifications();
			if (!items.empty()) {
				const auto &selected = items[static_cast<size_t>(std::clamp(
					modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
				if (selected.installed && !modificationOperationRunning) {
					const EchelonLauncher::InstalledModification modification = *selected.installed;
					modificationOperationProgress = std::make_shared<EchelonLauncher::ModificationOperationProgress>();
					const auto progress = modificationOperationProgress;
					modificationOperationRunning = true;
					modificationOperationType.reset();
					modificationOperationEngine.clear();
					statusMessage = Localized(russian, "VERIFYING INSTALLED FILES...", "ПРОВЕРКА УСТАНОВЛЕННЫХ ФАЙЛОВ...");
					statusError = false;
					modificationOperation = std::async(std::launch::async, [modification, progress]() {
						EchelonLauncher::ModificationOperationResult result;
						result.success = EchelonLauncher::VerifyInstalledModification(
							modification, result.message, progress.get());
						result.cancelled = progress->cancelRequested.load(std::memory_order_relaxed);
						if (result.success) result.message = "Installed modification verified";
						return result;
					});
				}
			}
		} else if (button == &modsCover) {
			const std::vector<ModificationViewItem> items = visibleModifications();
			if (!items.empty()) {
				const auto &selected = items[static_cast<size_t>(std::clamp(
					modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
				if (selected.installed) {
					modificationFileDialogPurpose = ModificationFileDialogPurpose::Cover;
					modificationCoverTargetEngine = selected.installed->engine;
					modificationCoverTargetSelection = EchelonLauncher::ModificationSelectionKey(*selected.installed);
					BeginCoverImageFileDialog(modificationFileDialog, window,
						selected.installed->coverImagePath.empty() ? paths.home : selected.installed->coverImagePath.parent_path());
				}
			}
		} else if (button == &modsFolder) {
			modsContextMode = ModsContextMode::Folders;
		} else if (button == &modsLink) {
			modsContextMode = ModsContextMode::Links;
		} else if (button == &modsImport) {
			if (modificationOperationRunning) {
				if (modificationOperationProgress) {
					modificationOperationProgress->cancelRequested.store(true, std::memory_order_relaxed);
					modificationImportQueue.clear();
					statusMessage = Localized(russian, "STOPPING IMPORT SAFELY...", "БЕЗОПАСНАЯ ОСТАНОВКА ИМПОРТА...");
				}
			} else {
				modificationFileDialogPurpose = ModificationFileDialogPurpose::Import;
				BeginModificationFileDialog(modificationFileDialog, window, paths.home);
			}
		} else if (button == &modsImportFolder) {
			BeginFolderDialog(modificationFolderDialog, window, paths.home);
		} else if (button == &modsRemove) {
			const std::vector<ModificationViewItem> items = visibleModifications();
			if (!items.empty()) {
				const auto &selected = items[static_cast<size_t>(
					std::clamp(modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
				if (selected.installed) {
					const auto result = EchelonLauncher::MoveInstalledModificationToTrash(paths.mods, *selected.installed);
					statusMessage = result.message;
					statusError = !result.success;
					if (result.success) reloadModificationCatalog();
				}
			}
		} else if (button == &modsRestore) {
			if (!recoverableModifications.empty()) {
				const auto result = EchelonLauncher::RestoreModificationFromTrash(
					paths.mods, recoverableModifications.front().path);
				statusMessage = result.message;
				statusError = !result.success;
				if (result.success) reloadModificationCatalog();
			}
		} else if (button == &modsLaunch) {
			EchelonLauncher::ModificationStack stack;
			std::string stackError;
			if (EchelonLauncher::BuildSelectedModificationStack(
				modificationCatalog, currentModificationProfile(), stack, stackError)) {
				pendingModificationLaunch = std::move(stack);
			} else {
				statusMessage = stackError;
				statusError = true;
			}
		}
		else if (button == &modsBack) closeMods();
		clampModsSelection();
	};
	auto activateSettingsButton = [&](Button *button) {
		if (!button || !button->enabled) return;
		if (button == &settingsGeneralsTab) setSettingsPage(SettingsPage::Generals);
		else if (button == &settingsZeroHourTab) setSettingsPage(SettingsPage::ZeroHour);
		else if (button == &settingsLauncherTab) setSettingsPage(SettingsPage::Launcher);
		else if (button == &settingsScrollUp) {
			settingsSelectedRow = std::max(0, settingsSelectedRow - kVisibleSettingsRows);
			clampSettingsSelection();
		} else if (button == &settingsScrollDown) {
			settingsSelectedRow = std::min(settingsRowCount() - 1, settingsSelectedRow + kVisibleSettingsRows);
			clampSettingsSelection();
		} else if (button == &settingsDefaults) applySettingsDefaults();
		else if (button == &settingsCancel) closeSettings();
		else if (button == &settingsApply) saveSettings();
	};
	auto settingsLanguageValue = [&]() {
		switch (settingsDraft.language) {
			case EchelonLauncher::LanguageMode::English: return Localized(russian, "ENGLISH", "АНГЛИЙСКИЙ");
			case EchelonLauncher::LanguageMode::Russian: return Localized(russian, "RUSSIAN", "РУССКИЙ");
			default: return Localized(russian, "SYSTEM", "СИСТЕМНЫЙ");
		}
	};
	auto settingsRowLabelValue = [&](int row) -> std::pair<std::string, std::string> {
		if (settingsPage == SettingsPage::Launcher) {
			if (row == 0) return {Localized(russian, "Interface language", "Язык интерфейса"), settingsLanguageValue()};
			if (row == 1) return {Localized(russian, "Display mode", "Режим экрана"),
				settingsDraft.windowMode == EchelonLauncher::LauncherWindowMode::Fullscreen ?
					Localized(russian, "FULLSCREEN", "ПОЛНЫЙ ЭКРАН") : Localized(russian, "WINDOWED", "В ОКНЕ")};
			if (row == 2) return {Localized(russian, "Window size", "Размер окна"),
				std::to_string(settingsDraft.windowWidth) + " x " + std::to_string(settingsDraft.windowHeight)};
			return {};
		}
		const auto &launch = currentLaunchSettings();
		const auto &options = currentGameOptions();
		const auto &sagePatch = currentSagePatchOptions();
		auto decimal = [](float value) {
			std::ostringstream output;
			output << std::fixed << std::setprecision(2) << value;
			std::string text = output.str();
			while (text.size() > 1 && text.back() == '0') text.pop_back();
			if (!text.empty() && text.back() == '.') text.push_back('0');
			return text;
		};
		switch (row) {
			case 0: return {Localized(russian, "Windowed mode", "Оконный режим"), OnOff(russian, launch.windowed)};
			case 1: return {Localized(russian, "Quick start", "Быстрый запуск"), OnOff(russian, launch.quickStart)};
			case 2: return {Localized(russian, "Disable shell map", "Отключить фоновую карту"), OnOff(russian, launch.noShellMap)};
			case 3: return {Localized(russian, "Additional launch arguments", "Дополнительные параметры"),
				launch.additionalArguments.empty() ? Localized(russian, "NONE", "НЕТ") : launch.additionalArguments};
			case 4: return {Localized(russian, "Resolution", "Разрешение"),
				std::to_string(options.resolutionWidth) + " x " + std::to_string(options.resolutionHeight)};
			case 5: return {Localized(russian, "Maximum particles", "Максимум частиц"), std::to_string(options.maxParticleCount)};
			case 6: return {Localized(russian, "Texture quality", "Качество текстур"),
				options.textureReduction == 0 ? Localized(russian, "HIGH", "ВЫСОКОЕ") :
				(options.textureReduction == 1 ? Localized(russian, "MEDIUM", "СРЕДНЕЕ") : Localized(russian, "LOW", "НИЗКОЕ"))};
			case 7: return {Localized(russian, "Volume shadows", "Объёмные тени"), OnOff(russian, options.useShadowVolumes)};
			case 8: return {Localized(russian, "Building occlusion", "Объекты за зданиями"), OnOff(russian, options.buildingOcclusion)};
			case 9: return {Localized(russian, "Shadow decals", "Плоские тени"), OnOff(russian, options.useShadowDecals)};
			case 10: return {Localized(russian, "Trees and props", "Деревья и объекты"), OnOff(russian, options.showTrees)};
			case 11: return {Localized(russian, "Cloud shadows", "Тени облаков"), OnOff(russian, options.useCloudMap)};
			case 12: return {Localized(russian, "Extra animations", "Дополнительная анимация"), OnOff(russian, options.extraAnimations)};
			case 13: return {Localized(russian, "Extra ground lighting", "Доп. освещение земли"), OnOff(russian, options.useLightMap)};
			case 14: return {Localized(russian, "Dynamic detail level", "Динамическая детализация"), OnOff(russian, options.dynamicLOD)};
			case 15: return {Localized(russian, "Smooth water edges", "Сглаживание воды"), OnOff(russian, options.showSoftWaterEdge)};
			case 16: return {Localized(russian, "Heat distortion", "Тепловые эффекты"), OnOff(russian, options.heatEffects)};
			case 17: return {Localized(russian, "Alternate mouse", "Альтернативная мышь"), OnOff(russian, options.useAlternateMouse)};
			case 18: return {Localized(russian, "Maximum camera height", "Максимальная высота камеры"), decimal(sagePatch.maxCameraHeight)};
			case 19: return {Localized(russian, "Minimum camera height", "Минимальная высота камеры"), decimal(sagePatch.minCameraHeight)};
			case 20: return {Localized(russian, "Hard camera limit", "Жёсткое ограничение камеры"), OnOff(russian, sagePatch.enforceMaxCameraHeight)};
			case 21: return {Localized(russian, "Keyboard scroll speed", "Скорость прокрутки клавиатурой"), decimal(sagePatch.keyboardScrollSpeed)};
			case 22: return {Localized(russian, "Terrain draw distance", "Дальность отрисовки земли"), decimal(sagePatch.terrainDrawDistanceScale)};
			case 23: return {Localized(russian, "Frame-rate limit", "Ограничение кадров"), OnOff(russian, sagePatch.useFpsLimit)};
			case 24: return {Localized(russian, "Frame-rate target", "Целевая частота кадров"),
				sagePatch.useFpsLimit ? std::to_string(sagePatch.framesPerSecondLimit) : Localized(russian, "DISABLED", "ОТКЛЮЧЕНО")};
			case 25: return {Localized(russian, "Russian BIG localization", "Русская BIG-локализация"),
				OnOff(russian, launch.russianLocalization)};
			default: return {};
		}
	};

	auto resetButtonStates = [&]() {
		for (Button *button : mainButtons) {
			button->hilited = false;
			button->selected = false;
			button->focused = false;
			button->lastTransitionFrame = -1;
		}
		capturedButton = nullptr;
		focusedButton = nullptr;
		transitionEpoch = SDL_GetTicks();
	};

	auto refreshProfiles = [&]() {
		if (fs::exists(paths.root)) {
			std::string errorMessage;
			EnsureLayout(paths, errorMessage);
			profiles = LoadProfiles(paths);
		}
	};

	auto startMigration = [&](auto operation) {
		migrationRunning = true;
		statusMessage.clear();
		migration = std::async(std::launch::async, operation);
	};

	// Echelon @feature Codex 14/08/2026 Restore the exact launcher presentation only after DXVK is fully quiescent.
	auto restoreLauncherWindow = [&](const SharedWindowState &launcherWindowState) -> bool {
		// Echelon @bugfix Codex 13/08/2026 Commit DXVK destruction before changing window state or attaching a software surface.
		if (!SDL_SyncWindow(window)) {
			fprintf(stderr, "WARNING: Cannot synchronize the window after DXVK teardown: %s\n", SDL_GetError());
			fflush(stderr);
		}
		return RestoreSharedWindowState(window, launcherWindowState);
	};

	auto launchProfile = [&](LauncherProfile &profile, const EchelonLauncher::ModificationStack *contentStack) {
		uiAudio.reset();
		modCoverCache.reset();
		backdrop.reset();
		logo.reset();
		font.reset();
		SDL_DestroyRenderer(renderer);
		renderer = nullptr;
		if (!SDL_SyncWindow(window)) {
			fprintf(stderr, "ERROR: Cannot synchronize the launcher surface teardown: %s\n", SDL_GetError());
			fflush(stderr);
			// Echelon @bugfix Codex 13/08/2026 Do not touch a potentially poisoned graphics connection during cleanup.
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		const SharedWindowState windowStateBeforeEngine = CaptureSharedWindowState(window);
		LogSharedWindowState("launcher-before-engine", windowStateBeforeEngine);
		SDL_SetWindowTitle(window, profile.nameEn.c_str());
		std::string engineError;
		uint32_t quiescenceFlags = 0;
		EngineWindowCoordinator windowCoordinator{window};
		const EchelonEngineResultV2 result = RunProfile(profile, window, parsed.engineArguments,
			launcherSettings, false,
			parsed.internalTestReturnAfterUpdates, modules, quiescenceFlags, engineError, &windowCoordinator, contentStack);
		fprintf(stderr, "INFO: Echelon engine session result: %u\n", static_cast<unsigned int>(result));
		fflush(stderr);
		if (result == ECHELON_ENGINE_EXIT_APPLICATION) {
			running = false;
			return;
		}
		if ((quiescenceFlags & ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) !=
			ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) {
			fprintf(stderr, "ERROR: Refusing to reuse a window after incomplete engine teardown (flags=0x%08x)\n", quiescenceFlags);
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		if (!SDL_SyncWindow(window)) {
			fprintf(stderr, "ERROR: Cannot synchronize the shared window after engine teardown: %s\n", SDL_GetError());
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		const SharedWindowState engineWindowState = CaptureSharedWindowState(window);
		LogSharedWindowState("engine-released", engineWindowState);
		if (result == ECHELON_ENGINE_FATAL_ERROR) {
			statusMessage = engineError.empty() ? Localized(russian, "The engine session failed", "Ошибка запуска движка") : engineError;
			statusError = true;
		}
		if (!restoreLauncherWindow(windowStateBeforeEngine)) {
			fprintf(stderr, "ERROR: Cannot restore launcher presentation after the engine: %s\n", SDL_GetError());
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		const SharedWindowState restoredLauncherState = CaptureSharedWindowState(window);
		LogSharedWindowState("launcher-restored", restoredLauncherState);
		if (!SharedWindowStateEquals(windowStateBeforeEngine, restoredLauncherState)) {
			fprintf(stderr, "ERROR: Launcher window state was not restored after engine handoff\n");
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		SDL_SetWindowTitle(window, EchelonBrand::kProductName);
		if (!createRenderer()) {
			fprintf(stderr, "ERROR: Cannot restore launcher renderer: %s\n", SDL_GetError());
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		resetButtonStates();
	};

	if (parsed.internalTestCycles > 0) {
		if (parsed.internalTestReturnAfterUpdates == 0) {
			fprintf(stderr, "ERROR: internal lifecycle cycles require --internal-test-return-after-updates=N\n");
			workerExitCode = 2;
			running = false;
		} else {
			const bool startWithZeroHour = parsed.profile == kZeroHourProfileId;
			LogWorkerResources(0);
			for (uint32_t cycle = 0; cycle < parsed.internalTestCycles && running; ++cycle) {
				const bool useZeroHour = startWithZeroHour ? cycle % 2 == 0 : cycle % 2 != 0;
				LauncherProfile *profile = FindProfile(profiles, useZeroHour ? kZeroHourProfileId : kGeneralsProfileId);
				if (!profile || !profile->enabled) {
					fprintf(stderr, "ERROR: lifecycle test profile is unavailable\n");
					workerExitCode = 2;
					running = false;
					break;
				}
				fprintf(stderr, "INFO: Echelon lifecycle test cycle %u/%u profile=%s\n",
					cycle + 1, parsed.internalTestCycles, profile->id.c_str());
				fflush(stderr);
				launchProfile(*profile, nullptr);
				if (running) LogWorkerResources(cycle + 1);
			}
			if (running) {
				fprintf(stderr, "INFO: Echelon lifecycle test completed %u cycles\n", parsed.internalTestCycles);
				fflush(stderr);
				running = false;
			}
		}
	} else if (!parsed.forceLauncher && !parsed.profile.empty()) {
		if (LauncherProfile *profile = FindProfile(profiles, parsed.profile); profile && profile->enabled) {
			launchProfile(*profile, commandLineStack ? &*commandLineStack : nullptr);
		} else {
			statusMessage = Localized(russian, "Requested profile is unavailable", "Выбранная версия игры недоступна");
			statusError = true;
		}
	}
	if (running && parsed.internalTestOpenSettings) {
		openSettings();
		if (parsed.internalTestSettingsPage == "zerohour") setSettingsPage(SettingsPage::ZeroHour);
		else if (parsed.internalTestSettingsPage == "launcher") setSettingsPage(SettingsPage::Launcher);
		if (parsed.internalTestSettingsRow >= 0) {
			settingsSelectedRow = parsed.internalTestSettingsRow;
			clampSettingsSelection();
		}
	}
	if (running && parsed.showMods) {
		openMods();
		if (parsed.internalTestModsEngine == kZeroHourProfileId) modsEngine = kZeroHourProfileId;
		if (parsed.internalTestModsPage == "patches") setModsPage(ModsPage::Patches);
		else if (parsed.internalTestModsPage == "addons") setModsPage(ModsPage::Addons);
		if (parsed.internalTestModsContext == "folders") modsContextMode = ModsContextMode::Folders;
		else if (parsed.internalTestModsContext == "links") modsContextMode = ModsContextMode::Links;
	}

	while (running) {
		if (initialPickerPending && !folderDialog.pending) {
			BeginFolderDialog(folderDialog, window, paths.home);
			initialPickerPending = false;
			pickerWasAutomatic = true;
		}

		if (migrationRunning && migration.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
			const MigrationResult result = migration.get();
			migrationRunning = false;
			statusMessage = result.message;
			statusError = !result.success;
			if (result.success) {
				refreshProfiles();
			}
		}
		if (modificationOperationRunning &&
			modificationOperation.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
			const EchelonLauncher::ModificationOperationResult result = modificationOperation.get();
			modificationOperationRunning = false;
			modificationOperationProgress.reset();
			if (result.success) {
				reloadModificationCatalog();
				if (modificationOperationType && !result.selectionKey.empty()) {
					auto &profile = modificationOperationEngine == kZeroHourProfileId ?
						zeroHourModificationProfile : generalsModificationProfile;
					if (const auto *installed = EchelonLauncher::FindModification(
						modificationCatalog, modificationOperationEngine, result.selectionKey)) {
						std::string selectionError;
						if (installed->type == *modificationOperationType &&
							selectCompatibleModification(profile, installed, selectionError)) {
							std::string reconcileWarning;
							EchelonLauncher::ReconcileModificationSelectionProfile(
								modificationCatalog, profile, reconcileWarning);
							saveModificationProfile(profile);
						}
					}
				}
			}
			modificationOperationType.reset();
			modificationOperationEngine.clear();
			statusMessage = result.message;
			statusError = !result.success && !result.cancelled;
			if (result.success && !modificationImportQueue.empty()) {
				startNextModificationImport();
			} else if (!result.success) {
				modificationImportQueue.clear();
			}
		}

		std::optional<fs::path> selectedFolder;
		if (const auto dialogResult = ConsumeFolderDialog(folderDialog, selectedFolder)) {
			if (!dialogResult->success) {
				statusMessage = dialogResult->message;
				statusError = true;
			} else if (selectedFolder) {
				const fs::path selected = *selectedFolder;
				startMigration([paths, selected]() { return ImportSelectedData(paths, selected); });
			} else if (pickerWasAutomatic) {
				statusMessage = Localized(russian, "Game data selection was cancelled", "Выбор папки с данными игры отменён");
				statusError = true;
			}
			pickerWasAutomatic = false;
		}
		std::optional<fs::path> selectedModificationFolder;
		if (const auto dialogResult = ConsumeFolderDialog(modificationFolderDialog, selectedModificationFolder)) {
			if (!dialogResult->success) {
				statusMessage = dialogResult->message;
				statusError = true;
			} else if (selectedModificationFolder) {
				modificationImportQueue = {*selectedModificationFolder};
				startNextModificationImport();
			}
		}
		std::vector<fs::path> selectedModificationFiles;
		if (const auto dialogResult = ConsumeModificationFileDialog(modificationFileDialog, selectedModificationFiles)) {
			if (!dialogResult->success) {
				statusMessage = dialogResult->message;
				statusError = true;
			} else if (modificationFileDialogPurpose == ModificationFileDialogPurpose::Cover &&
				!selectedModificationFiles.empty()) {
				const auto *target = EchelonLauncher::FindModification(modificationCatalog,
					modificationCoverTargetEngine, modificationCoverTargetSelection);
				if (!target) {
					statusMessage = Localized(russian, "MODIFICATION IS NO LONGER INSTALLED", "МОДИФИКАЦИЯ БОЛЬШЕ НЕ УСТАНОВЛЕНА");
					statusError = true;
				} else {
					const auto result = EchelonLauncher::ReplaceModificationCover(
						paths.mods, *target, selectedModificationFiles.front());
					statusMessage = result.message;
					statusError = !result.success;
					if (result.success) reloadModificationCatalog();
				}
			} else if (!selectedModificationFiles.empty()) {
				modificationImportQueue = std::move(selectedModificationFiles);
				startNextModificationImport();
			}
			modificationCoverTargetEngine.clear();
			modificationCoverTargetSelection.clear();
		}

		LauncherProfile *generals = FindProfile(profiles, kGeneralsProfileId);
		LauncherProfile *zeroHour = FindProfile(profiles, kZeroHourProfileId);
		generalsButton.enabled = generals && generals->enabled && !migrationRunning;
		zeroHourButton.enabled = zeroHour && zeroHour->enabled && !migrationRunning;
		modsButton.enabled = !migrationRunning;
		settingsButton.enabled = !migrationRunning;
		exitButton.enabled = !migrationRunning;
		for (Button *button : settingsButtons) button->enabled = !migrationRunning && !folderDialog.pending;
		settingsScrollUp.enabled = !migrationRunning && !folderDialog.pending && settingsScroll > 0;
		settingsScrollDown.enabled = !migrationRunning && !folderDialog.pending &&
			settingsScroll < std::max(0, settingsRowCount() - kVisibleSettingsRows);
		for (Button *button : modsButtons) button->enabled = !migrationRunning && !folderDialog.pending &&
			!modificationFolderDialog.pending && !modificationFileDialog.pending && !modificationOperationRunning;
		modsImport.enabled = !migrationRunning && !folderDialog.pending && !modificationFolderDialog.pending &&
			!modificationFileDialog.pending;
		modsImportFolder.enabled = !migrationRunning && !folderDialog.pending && !modificationFolderDialog.pending &&
			!modificationFileDialog.pending && !modificationOperationRunning;
		modsImport.label = modificationOperationRunning ? Localized(russian, "STOP", "СТОП") :
			Localized(russian, "ADD FROM FILES", "ДОБАВИТЬ ИЗ ФАЙЛОВ");
		const std::vector<ModificationViewItem> currentModItems = visibleModifications();
		modsRemove.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && !currentModItems.empty() &&
			currentModItems[static_cast<size_t>(std::clamp(modsSelectedItem, 0,
				static_cast<int>(currentModItems.size()) - 1))].installed;
		modsRestore.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && !recoverableModifications.empty();
		const ModificationViewItem *currentModItem = currentModItems.empty() ? nullptr :
			&currentModItems[static_cast<size_t>(std::clamp(modsSelectedItem, 0,
				static_cast<int>(currentModItems.size()) - 1))];
		const bool selectedInstalled = currentModItem && currentModItem->installed;
		const auto currentInstalledVersions = installedVersions(currentModItem);
		modsVersion.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && currentInstalledVersions.size() > 1;
		modsVersion.label = currentModItem && currentModItem->installed ?
			Localized(russian, "INSTALLED VERSION: ", "УСТАНОВЛЕННАЯ ВЕРСИЯ: ") +
				currentModItem->installed->version + "  (" + std::to_string(currentInstalledVersions.size()) + ")" :
			Localized(russian, "INSTALLED VERSION", "УСТАНОВЛЕННАЯ ВЕРСИЯ");
		modsVerify.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && selectedInstalled;
		modsCover.enabled = modsVerify.enabled;
		modsFolder.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && currentModItem && FindProfile(profiles, modsEngine);
		modsLink.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && selectedInstalled &&
			currentModItem->installed->source.rfind("https://", 0) == 0;
		for (Button *button : modsContextButtons) button->enabled = false;
		if (modsContextMode == ModsContextMode::Folders) {
			modsContextFirst.label = Localized(russian, "MOD FOLDER", "ПАПКА МОДА");
			modsContextSecond.label = Localized(russian, "GAME FOLDER", "ПАПКА ИГРЫ");
			modsContextThird.label = Localized(russian, "MAPS FOLDER", "ПАПКА КАРТ");
			modsContextFourth.label = Localized(russian, "REPLAYS FOLDER", "ПАПКА РЕПЛЕЕВ");
			const std::vector<fs::path> folders = modificationContextFolders();
			modsContextFirst.enabled = !folders[0].empty();
			modsContextSecond.enabled = !folders[1].empty();
			modsContextThird.enabled = !folders[2].empty();
			modsContextFourth.enabled = !folders[3].empty();
			modsContextClose.enabled = true;
		} else if (modsContextMode == ModsContextMode::Links) {
			modsContextFirst.label = "MODDB";
			modsContextSecond.label = "DISCORD";
			modsContextThird.label = Localized(russian, "NEWS", "НОВОСТИ");
			modsContextFourth.label = Localized(russian, "SUPPORT", "ПОДДЕРЖКА");
			const std::vector<std::string> links = modificationContextLinks();
			modsContextFirst.enabled = !links[0].empty();
			modsContextSecond.enabled = !links[1].empty();
			modsContextThird.enabled = !links[2].empty();
			modsContextFourth.enabled = !links[3].empty();
			modsContextClose.enabled = true;
		}
		modsLaunch.label = Localized(russian, "LAUNCH", "ЗАПУСТИТЬ");
		const LauncherProfile *modsProfile = FindProfile(profiles, modsEngine);
		EchelonLauncher::ModificationStack activeStack;
		std::string activeStackError;
		const bool canLaunchActiveStack = modsProfile && modsProfile->enabled &&
			EchelonLauncher::BuildSelectedModificationStack(
				modificationCatalog, currentModificationProfile(), activeStack, activeStackError);
		modsLaunch.enabled = !migrationRunning && !folderDialog.pending && !modificationFileDialog.pending &&
			!modificationOperationRunning && currentModItem && canLaunchActiveStack;
		if (!settingsOpen) {
			for (Button *button : settingsButtons) {
				button->hilited = false;
				button->selected = false;
			}
		}

		auto buttonAcceptsInput = [&](Button *button) {
			return ButtonAcceptsInput(*button, transitionEpoch);
		};
		auto updateHilites = [&]() {
			for (Button *button : mainButtons) {
				UpdateButtonHilite(*button, mouseX, mouseY, ButtonAcceptsInput(*button, transitionEpoch));
			}
		};
		auto buttonAt = [&](float x, float y) -> Button * {
			for (Button *button : mainButtons) {
				if (buttonAcceptsInput(button) && Contains(button->rect, x, y)) return button;
			}
			return nullptr;
		};
		auto setFocus = [&](Button *button) {
			for (Button *candidate : mainButtons) candidate->focused = false;
			focusedButton = button;
			if (focusedButton) focusedButton->focused = true;
		};
		auto moveFocus = [&](int direction) {
			const std::vector<Button *> &buttons = mainButtons;
			int currentIndex = -1;
			for (size_t index = 0; index < buttons.size(); ++index) {
				if (buttons[index] == focusedButton) currentIndex = static_cast<int>(index);
			}
			for (size_t step = 0; step < buttons.size(); ++step) {
				currentIndex = (currentIndex + direction + static_cast<int>(buttons.size())) % static_cast<int>(buttons.size());
				Button *candidate = buttons[static_cast<size_t>(currentIndex)];
				if (candidate->enabled && buttonAcceptsInput(candidate)) {
					setFocus(candidate);
					break;
				}
			}
		};
		auto activateButton = [&](Button *button) {
			if (!button || !button->enabled) return;
			if (button == &generalsButton && generals) {
				launchProfile(*generals, nullptr);
		} else if (button == &zeroHourButton && zeroHour) {
			launchProfile(*zeroHour, nullptr);
		} else if (button == &modsButton) {
			openMods();
		} else if (button == &settingsButton) {
			openSettings();
		} else if (button == &exitButton) {
				running = false;
			}
		};
		updateHilites();

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (renderer) SDL_ConvertEventToRenderCoordinates(renderer, &event);
			if (event.type == SDL_EVENT_QUIT) {
				if (!migrationRunning) running = false;
			} else if (modsOpen && !migrationRunning && !folderDialog.pending && !modificationFolderDialog.pending) {
				if (event.type == SDL_EVENT_MOUSE_MOTION) {
					mouseX = event.motion.x;
					mouseY = event.motion.y;
					if (capturedModificationCard >= 0 && std::fabs(mouseY - modificationCardPressY) >= 8.0f) {
						modificationCardDragging = true;
					}
					for (Button *button : modsButtons) {
						UpdateButtonHilite(*button, mouseX, mouseY, modsContextMode == ModsContextMode::None);
					}
					for (Button *button : modsContextButtons) {
						UpdateButtonHilite(*button, mouseX, mouseY, modsContextMode != ModsContextMode::None);
					}
				} else if (event.type == SDL_EVENT_MOUSE_WHEEL && modsContextMode == ModsContextMode::None) {
					int wheelDelta = event.wheel.integer_y;
					if (wheelDelta == 0) wheelDelta = event.wheel.y > 0.0f ? 1 : (event.wheel.y < 0.0f ? -1 : 0);
					modsSelectedItem -= wheelDelta;
					clampModsSelection();
				} else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
					if (modsContextMode != ModsContextMode::None) modsContextMode = ModsContextMode::None;
					else closeMods();
				} else if (modsContextMode == ModsContextMode::None && event.type == SDL_EVENT_KEY_DOWN &&
					(event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) && !event.key.repeat) {
					const int direction = event.key.key == SDLK_UP ? -1 : 1;
					bool reordered = false;
					if (modsPage == ModsPage::Addons && (event.key.mod & SDL_KMOD_SHIFT)) {
						const std::vector<ModificationViewItem> items = visibleModifications();
						if (!items.empty()) {
							const auto &selected = items[static_cast<size_t>(std::clamp(
								modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
							if (selected.installed) {
								reordered = EchelonLauncher::MoveSelectedAddon(currentModificationProfile(),
									EchelonLauncher::ModificationSelectionKey(*selected.installed), direction);
								if (reordered) saveModificationProfile(currentModificationProfile());
							}
						}
					}
					if (!reordered) modsSelectedItem += direction;
					clampModsSelection();
				} else if (modsContextMode == ModsContextMode::None && event.type == SDL_EVENT_KEY_DOWN &&
					(event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE) &&
					!event.key.repeat) {
					activateSelectedModificationCard();
				} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
					if (Button *button = modsButtonAt(event.button.x, event.button.y)) {
						capturedModificationCard = -1;
						modificationCardDragging = false;
						capturedButton = button;
						button->selected = true;
						uiAudio.play(LauncherUISound::Click);
					} else if (const int item = modsContextMode == ModsContextMode::None ?
						modsCardAt(event.button.x, event.button.y) : -1; item >= 0) {
						modsSelectedItem = item;
						capturedModificationCard = item;
						modificationCardPressY = event.button.y;
						modificationCardDragging = false;
						uiAudio.play(LauncherUISound::Click);
					}
				} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && capturedButton) {
					Button *activated = capturedButton;
					const bool shouldActivate = activated->enabled && Contains(activated->rect, event.button.x, event.button.y);
					activated->selected = false;
					capturedButton = nullptr;
					if (shouldActivate) activateModsButton(activated);
				} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT &&
					capturedModificationCard >= 0) {
					const int sourceIndex = capturedModificationCard;
					const int targetIndex = modsCardAt(event.button.x, event.button.y);
					if (modificationCardDragging && modsPage == ModsPage::Addons && targetIndex >= 0) {
						const std::vector<ModificationViewItem> items = visibleModifications();
						if (sourceIndex < static_cast<int>(items.size()) && targetIndex < static_cast<int>(items.size())) {
							const auto &source = items[static_cast<size_t>(sourceIndex)];
							const auto &target = items[static_cast<size_t>(targetIndex)];
							if (source.installed && target.installed &&
								EchelonLauncher::IsModificationSelected(currentModificationProfile(), *source.installed) &&
								EchelonLauncher::IsModificationSelected(currentModificationProfile(), *target.installed)) {
								const auto &addons = currentModificationProfile().addonSelections;
								const auto targetPosition = std::find(addons.begin(), addons.end(),
									EchelonLauncher::ModificationSelectionKey(*target.installed));
								if (targetPosition != addons.end() && EchelonLauncher::MoveSelectedAddonTo(
									currentModificationProfile(), EchelonLauncher::ModificationSelectionKey(*source.installed),
									static_cast<size_t>(std::distance(addons.begin(), targetPosition)))) {
									saveModificationProfile(currentModificationProfile());
								}
							}
						}
					} else if (!modificationCardDragging && targetIndex == sourceIndex) {
						activateSelectedModificationCard();
					}
					capturedModificationCard = -1;
					modificationCardDragging = false;
				}
			} else if (settingsOpen && !migrationRunning && !folderDialog.pending) {
				if (event.type == SDL_EVENT_MOUSE_MOTION) {
					mouseX = event.motion.x;
					mouseY = event.motion.y;
					for (Button *button : settingsButtons) UpdateButtonHilite(*button, mouseX, mouseY, true);
				} else if (event.type == SDL_EVENT_MOUSE_WHEEL && !settingsTextEditing) {
					int wheelDelta = event.wheel.integer_y;
					if (wheelDelta == 0) wheelDelta = event.wheel.y > 0.0f ? 1 : (event.wheel.y < 0.0f ? -1 : 0);
					settingsSelectedRow = std::clamp(settingsSelectedRow - wheelDelta, 0, settingsRowCount() - 1);
					clampSettingsSelection();
				} else if (event.type == SDL_EVENT_TEXT_INPUT && settingsTextEditing) {
					auto &arguments = currentLaunchSettings().additionalArguments;
					if (arguments.size() + std::strlen(event.text.text) <= 512) arguments += event.text.text;
				} else if (event.type == SDL_EVENT_KEY_DOWN && settingsTextEditing) {
					auto &arguments = currentLaunchSettings().additionalArguments;
					if (event.key.key == SDLK_BACKSPACE && !event.key.repeat) RemoveLastUtf8Codepoint(arguments);
					else if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) && !event.key.repeat) {
						SDL_StopTextInput(window);
						settingsTextEditing = false;
					}
				} else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
					closeSettings();
				} else if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) && !event.key.repeat) {
					settingsSelectedRow += event.key.key == SDLK_UP ? -1 : 1;
					settingsSelectedRow = std::clamp(settingsSelectedRow, 0, settingsRowCount() - 1);
					clampSettingsSelection();
				} else if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) && !event.key.repeat) {
					adjustSettingsRow(event.key.key == SDLK_LEFT ? -1 : 1);
				} else if (event.type == SDL_EVENT_KEY_DOWN &&
					(event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE) && !event.key.repeat) {
					adjustSettingsRow(1);
				} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
					if (settingsTextEditing) {
						SDL_StopTextInput(window);
						settingsTextEditing = false;
					}
					if (Button *button = settingsButtonAt(event.button.x, event.button.y)) {
						if (button->enabled) {
							capturedButton = button;
							button->selected = true;
							uiAudio.play(LauncherUISound::Click);
						} else {
							uiAudio.play(LauncherUISound::DisabledClick);
						}
					} else if (const int row = settingsRowAt(event.button.x, event.button.y); row >= 0) {
						settingsSelectedRow = row;
						adjustSettingsRow(1);
					}
				} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && capturedButton) {
					Button *activated = capturedButton;
					const bool shouldActivate = Contains(activated->rect, event.button.x, event.button.y);
					activated->selected = false;
					capturedButton = nullptr;
					if (shouldActivate) activateSettingsButton(activated);
				}
			} else if (event.type == SDL_EVENT_MOUSE_MOTION) {
				mouseX = event.motion.x;
				mouseY = event.motion.y;
				updateHilites();
			} else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
				if (!migrationRunning) running = false;
			} else if (event.type == SDL_EVENT_KEY_DOWN &&
				(event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT ||
				 event.key.key == SDLK_UP || event.key.key == SDLK_LEFT)) {
				if (!event.key.repeat) {
					const bool backwards = event.key.key == SDLK_UP || event.key.key == SDLK_LEFT ||
						(event.key.key == SDLK_TAB && (event.key.mod & SDL_KMOD_SHIFT));
					moveFocus(backwards ? -1 : 1);
				}
			} else if (event.type == SDL_EVENT_KEY_DOWN &&
				(event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE)) {
				if (!event.key.repeat && focusedButton && focusedButton->enabled && buttonAcceptsInput(focusedButton)) {
					focusedButton->selected = true;
					uiAudio.play(LauncherUISound::Click);
				}
			} else if (event.type == SDL_EVENT_KEY_UP &&
				(event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE)) {
				if (focusedButton && focusedButton->selected) {
					Button *activated = focusedButton;
					focusedButton->selected = false;
					activateButton(activated);
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && !migrationRunning) {
				Button *button = buttonAt(event.button.x, event.button.y);
				if (button && !button->enabled) {
					uiAudio.play(LauncherUISound::DisabledClick);
				} else if (button) {
					capturedButton = button;
					button->selected = true;
					uiAudio.play(LauncherUISound::Click);
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
				if (capturedButton) {
					Button *activated = capturedButton;
					const bool shouldActivate = capturedButton->enabled && capturedButton->selected &&
						Contains(capturedButton->rect, event.button.x, event.button.y);
					capturedButton->selected = false;
					capturedButton = nullptr;
					if (shouldActivate) activateButton(activated);
				}
			}
		}
		if (pendingModificationLaunch) {
			EchelonLauncher::ModificationStack stack = std::move(*pendingModificationLaunch);
			pendingModificationLaunch.reset();
			LauncherProfile *profile = FindProfile(profiles, stack.engine);
			if (!profile || !profile->enabled) {
				statusMessage = Localized(russian, "Required game data is unavailable", "Данные требуемой игры недоступны");
				statusError = true;
			} else {
				launchProfile(*profile, &stack);
			}
		}

		if (!running || !renderer) continue;
		DrawLauncherBackground(renderer, backdrop);
		if (modsOpen) {
			DrawFilledRect(renderer, kModsLayout.panel, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kModsLayout.panel, LauncherPalette::kFrame, 2);
			DrawGradientRect(renderer, SDL_FRect{kModsLayout.panel.x, kModsLayout.panel.y, kModsLayout.panel.w, 3.0f},
				LauncherPalette::kAccent, LauncherPalette::kAccentBright);
			font.draw(Localized(russian, "ECHELON MOD MANAGER", "МЕНЕДЖЕР МОДОВ ECHELON"), 800.0f, 108.0f, 28.0f,
				LauncherPalette::kTextBright, true);
			modsGeneralsTab.focused = modsEngine == kGeneralsProfileId;
			modsZeroHourTab.focused = modsEngine == kZeroHourProfileId;
			modsModsTab.focused = modsPage == ModsPage::Mods;
			modsPatchesTab.focused = modsPage == ModsPage::Patches;
			modsAddonsTab.focused = modsPage == ModsPage::Addons;
			DrawFilledRect(renderer, kModsLayout.catalog, LauncherPalette::kPanel);
			DrawOutline(renderer, kModsLayout.catalog, LauncherPalette::kFrameSoft, 1);
			DrawFilledRect(renderer, kModsLayout.details, LauncherPalette::kPanel);
			DrawOutline(renderer, kModsLayout.details, LauncherPalette::kFrameSoft, 1);
			const std::vector<ModificationViewItem> items = visibleModifications();
			for (int visible = 0; visible < 4; ++visible) {
				const int index = modsScroll + visible;
				if (index >= static_cast<int>(items.size())) break;
				const SDL_FRect card{kModsLayout.catalog.x + 15.0f, kModsLayout.catalog.y + 15.0f + visible * 137.0f,
					kModsLayout.catalog.w - 30.0f, 120.0f};
				const ModificationViewItem &item = items[static_cast<size_t>(index)];
				const bool active = item.installed && EchelonLauncher::IsModificationSelected(
						currentModificationProfile(), *item.installed);
				int activeOrder = 0;
				if (active && item.installed && item.installed->type == EchelonLauncher::ModificationType::Addon) {
					const auto &addons = currentModificationProfile().addonSelections;
					const auto position = std::find(addons.begin(), addons.end(),
						EchelonLauncher::ModificationSelectionKey(*item.installed));
					if (position != addons.end()) activeOrder = static_cast<int>(std::distance(addons.begin(), position)) + 1;
				}
				DrawModificationCard(renderer, font, modCoverCache, item, card,
					index == modsSelectedItem, active, activeOrder, russian);
			}
			if (items.empty()) {
				font.draw(Localized(russian, "NO MODIFICATIONS IN THIS SECTION", "В ЭТОМ РАЗДЕЛЕ НЕТ МОДИФИКАЦИЙ"),
					kModsLayout.catalog.x + kModsLayout.catalog.w * 0.5f,
					kModsLayout.catalog.y + kModsLayout.catalog.h * 0.5f, 21.0f, LauncherPalette::kDisabledText, true);
			} else {
				const auto &selected = items[static_cast<size_t>(std::clamp(modsSelectedItem, 0, static_cast<int>(items.size()) - 1))];
				font.draw(selected.name(), kModsLayout.details.x + 25.0f, kModsLayout.details.y + 48.0f, 28.0f,
					LauncherPalette::kTextBright);
				font.draw(Localized(russian, "VERSION", "ВЕРСИЯ") + std::string(": ") + selected.version(),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 94.0f, 20.0f, LauncherPalette::kValue);
				const bool selectedActive = selected.installed && EchelonLauncher::IsModificationSelected(
						currentModificationProfile(), *selected.installed);
				font.draw(selectedActive ? Localized(russian, "STATUS: ACTIVE", "СТАТУС: АКТИВЕН") :
					Localized(russian, "STATUS: INSTALLED", "СТАТУС: УСТАНОВЛЕН"),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 130.0f, 19.0f,
					selectedActive || selected.installed ?
						LauncherPalette::kAccentBright : LauncherPalette::kValue);
				font.draw(Localized(russian, "SOURCE", "ИСТОЧНИК") + std::string(": ") + selected.sourceId(),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 166.0f, 18.0f, LauncherPalette::kText);
				font.draw(Localized(russian, "CONTENT TYPE", "ТИП СОДЕРЖИМОГО") + std::string(": ") +
					EchelonLauncher::ModificationTypeName(selected.type()),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 198.0f, 18.0f, LauncherPalette::kText);
				font.draw(Localized(russian, "REQUIRES", "ТРЕБУЕТ") + std::string(": ") +
					std::to_string(selected.requirements().size()) + "    " +
					Localized(russian, "CONFLICTS", "КОНФЛИКТЫ") + ": " +
					std::to_string(selected.conflicts().size()),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 230.0f, 16.0f, LauncherPalette::kText);
				if (selected.installed && selected.installed->legacyManifest) {
					font.draw(Localized(russian, "V1 MANIFEST MIGRATED SAFELY", "МАНИФЕСТ V1 БЕЗОПАСНО ПЕРЕНЕСЁН"),
						kModsLayout.details.x + 25.0f, kModsLayout.details.y + 262.0f, 16.0f, LauncherPalette::kAccentSoft);
				}
				const auto &activeProfile = currentModificationProfile();
				const auto *activeMod = EchelonLauncher::FindModification(
					modificationCatalog, modsEngine, activeProfile.modSelection);
				const auto *activePatch = EchelonLauncher::FindModification(
					modificationCatalog, modsEngine, activeProfile.patchSelection);
				font.draw(Localized(russian, "ACTIVE STACK", "АКТИВНЫЙ СТЕК"),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 306.0f, 19.0f, LauncherPalette::kAccentBright);
				font.draw(Localized(russian, "MOD: ", "МОД: ") +
					(activeMod ? activeMod->name : Localized(russian, "NONE", "НЕТ")),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 340.0f, 17.0f, LauncherPalette::kText);
				font.draw(Localized(russian, "PATCH: ", "ПАТЧ: ") +
					(activePatch ? activePatch->name : Localized(russian, "NONE", "НЕТ")),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 372.0f, 17.0f, LauncherPalette::kText);
				font.draw(Localized(russian, "ADD-ONS: ", "АДДОНЫ: ") +
					std::to_string(activeProfile.addonSelections.size()),
					kModsLayout.details.x + 25.0f, kModsLayout.details.y + 404.0f, 17.0f, LauncherPalette::kText);
			}
			// Echelon @bugfix Codex 14/08/2026 Draw controls after their panel surfaces so detail actions remain visible and interactive.
			for (Button *button : modsButtons) DrawSageButton(renderer, font, *button, transitionEpoch, uiAudio, false);
			if (modsContextMode != ModsContextMode::None) {
				DrawFilledRect(renderer, kModsContextLayout.panel, SDL_Color{0, 8, 5, 255});
				DrawOutline(renderer, kModsContextLayout.panel, LauncherPalette::kAccentBright, 2);
				font.draw(modsContextMode == ModsContextMode::Folders ?
					Localized(russian, "OPEN FOLDER", "ОТКРЫТЬ ПАПКУ") :
					Localized(russian, "OPEN PROJECT LINK", "ОТКРЫТЬ ССЫЛКУ ПРОЕКТА"),
					1215.0f, 275.0f, 25.0f, LauncherPalette::kTextBright, true);
				for (Button *button : modsContextButtons) {
					DrawSageButton(renderer, font, *button, transitionEpoch, uiAudio, false);
				}
			}
			if (!statusMessage.empty()) {
				font.draw(statusMessage, 800.0f, 905.0f, 17.0f,
					statusError ? LauncherPalette::kError : LauncherPalette::kAccentSoft, true);
			}
		} else if (settingsOpen) {
			DrawFilledRect(renderer, kSettingsLayout.panel, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kSettingsLayout.panel, LauncherPalette::kFrame, 2);
			DrawGradientRect(renderer, SDL_FRect{kSettingsLayout.panel.x, kSettingsLayout.panel.y, kSettingsLayout.panel.w, 3.0f},
				LauncherPalette::kAccent, LauncherPalette::kAccentBright);
			font.draw(Localized(russian, "ECHELON SETTINGS", "НАСТРОЙКИ ECHELON"), 800.0f, 145.0f, 30.0f,
				LauncherPalette::kTextBright, true);
			settingsGeneralsTab.focused = settingsPage == SettingsPage::Generals;
			settingsZeroHourTab.focused = settingsPage == SettingsPage::ZeroHour;
			settingsLauncherTab.focused = settingsPage == SettingsPage::Launcher;
			for (Button *button : settingsButtons) {
				if ((button == &settingsScrollUp || button == &settingsScrollDown) && settingsRowCount() <= kVisibleSettingsRows) continue;
				DrawSageButton(renderer, font, *button, transitionEpoch, uiAudio, false);
			}
			if (settingsRowCount() > kVisibleSettingsRows) {
				DrawSettingsScrollArrow(renderer, settingsScrollUp, true);
				DrawSettingsScrollArrow(renderer, settingsScrollDown, false);
			}
			DrawFilledRect(renderer, kSettingsLayout.content, LauncherPalette::kPanel);
			DrawOutline(renderer, kSettingsLayout.content, LauncherPalette::kFrameSoft, 1);
			constexpr float rowHeight = 50.0f;
			constexpr float rowGap = 5.0f;
			for (int visible = 0; visible < kVisibleSettingsRows; ++visible) {
				const int row = settingsScroll + visible;
				if (row >= settingsRowCount()) break;
				const float rowWidth = kSettingsLayout.content.w -
					(settingsRowCount() > kVisibleSettingsRows ? 144.0f : 36.0f);
				const SDL_FRect rowRect{kSettingsLayout.content.x + 18.0f,
					kSettingsLayout.content.y + 18.0f + visible * (rowHeight + rowGap),
					rowWidth, rowHeight};
				const auto [label, value] = settingsRowLabelValue(row);
				DrawSettingsRow(renderer, font, rowRect, label, value, row == settingsSelectedRow);
			}
			if (settingsRowCount() > kVisibleSettingsRows) {
				const SDL_FRect track{1332.0f, 350.0f, 14.0f, 320.0f};
				DrawFilledRect(renderer, track, LauncherPalette::kControl);
				DrawOutline(renderer, track, LauncherPalette::kAccentMuted, 1);
				const float thumbHeight = std::max(42.0f, track.h * kVisibleSettingsRows / settingsRowCount());
				const int maximumScroll = settingsRowCount() - kVisibleSettingsRows;
				const float thumbY = track.y + (track.h - thumbHeight) * settingsScroll / maximumScroll;
				DrawFilledRect(renderer, SDL_FRect{track.x + 2.0f, thumbY + 2.0f, track.w - 4.0f, thumbHeight - 4.0f},
					LauncherPalette::kAccent);
			}
			if (settingsTextEditing && !(statusError && !statusMessage.empty())) {
				font.draw(Localized(russian, "TYPE ARGUMENTS; ENTER TO FINISH", "ВВЕДИТЕ ПАРАМЕТРЫ; ENTER — ГОТОВО"),
					800.0f, 798.0f, 20.0f, LauncherPalette::kAccentSoft, true);
			}
			if (!statusMessage.empty() && statusError) {
				font.draw(statusMessage, 800.0f, 798.0f, 19.0f, LauncherPalette::kError, true);
			}
		} else {
			DrawFilledRect(renderer, kLauncherLayout.mainPanel, LauncherPalette::kPanel);
			DrawOutline(renderer, kLauncherLayout.mainPanel, LauncherPalette::kFrameSoft, 1);
			DrawGradientRect(renderer,
				SDL_FRect{kLauncherLayout.mainPanel.x, kLauncherLayout.mainPanel.y, kLauncherLayout.mainPanel.w, 2.0f},
				LauncherPalette::kAccent, LauncherPalette::kAccentBright);
			DrawLauncherBranding(renderer, font, logo);
			DrawSageButton(renderer, font, generalsButton, transitionEpoch, uiAudio);
			DrawSageButton(renderer, font, zeroHourButton, transitionEpoch, uiAudio);
			DrawSageButton(renderer, font, modsButton, transitionEpoch, uiAudio);
			DrawSageButton(renderer, font, settingsButton, transitionEpoch, uiAudio);
			DrawSageButton(renderer, font, exitButton, transitionEpoch, uiAudio);
		}

		if (!settingsOpen && !modsOpen && !statusMessage.empty()) {
			font.draw(statusMessage, kLauncherLayout.mainCenterX(), 575.0f, 16,
				statusError ? LauncherPalette::kError : LauncherPalette::kAccentSoft, true);
		}
		if (folderDialog.pending) {
			DrawFilledRect(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kAccent, 2);
			font.draw(Localized(russian, "SELECT THE GAME DATA FOLDER", "ВЫБЕРИТЕ ПАПКУ С ДАННЫМИ ИГРЫ"), 800, 495, 24,
				LauncherPalette::kTextBright, true);
		}
		if (modificationFolderDialog.pending) {
			DrawFilledRect(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kAccent, 2);
			font.draw(Localized(russian, "SELECT THE MODIFICATION FOLDER", "ВЫБЕРИТЕ ПАПКУ МОДИФИКАЦИИ"),
				800, 495, 24, LauncherPalette::kTextBright, true);
		}
		if (migrationRunning) {
			DrawFilledRect(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kAccentBright, 2);
			font.draw(Localized(russian, "MIGRATING AND VERIFYING DATA...", "ПЕРЕНОС И ПРОВЕРКА ДАННЫХ..."), 800, 495, 24,
				LauncherPalette::kTextBright, true);
		}
		if (modificationFileDialog.pending) {
			DrawFilledRect(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, kLauncherLayout.progressOverlay, LauncherPalette::kAccent, 2);
			font.draw(modificationFileDialogPurpose == ModificationFileDialogPurpose::Cover ?
				Localized(russian, "SELECT A REPLACEMENT IMAGE", "ВЫБЕРИТЕ НОВУЮ КАРТИНКУ") :
				Localized(russian, "SELECT MOD ARCHIVES OR BIG FILES", "ВЫБЕРИТЕ АРХИВЫ ИЛИ BIG-ФАЙЛЫ"),
				800, 495, 24, LauncherPalette::kTextBright, true);
		}
		if (modificationOperationRunning && modificationOperationProgress) {
			const uint64_t completed = modificationOperationProgress->completedBytes.load(std::memory_order_relaxed);
			const uint64_t total = modificationOperationProgress->totalBytes.load(std::memory_order_relaxed);
			const float ratio = total > 0 ? std::clamp(static_cast<float>(completed) / static_cast<float>(total), 0.0f, 1.0f) : 0.0f;
			const SDL_FRect progressPanel = kLauncherLayout.progressOverlay;
			const SDL_FRect progressTrack{progressPanel.x + 70.0f, progressPanel.y + 118.0f,
				progressPanel.w - 140.0f, 18.0f};
			DrawFilledRect(renderer, progressPanel, LauncherPalette::kPanelStrong);
			DrawOutline(renderer, progressPanel, LauncherPalette::kAccentBright, 2);
			font.draw(Localized(russian, "INSTALLING MODIFICATION SAFELY...", "БЕЗОПАСНАЯ УСТАНОВКА МОДИФИКАЦИИ..."),
				800, 470, 23, LauncherPalette::kTextBright, true);
			DrawFilledRect(renderer, progressTrack, LauncherPalette::kControl);
			DrawFilledRect(renderer, SDL_FRect{progressTrack.x + 2.0f, progressTrack.y + 2.0f,
				(progressTrack.w - 4.0f) * ratio, progressTrack.h - 4.0f}, LauncherPalette::kAccentBright);
			DrawOutline(renderer, progressTrack, LauncherPalette::kAccentMuted, 1);
		}
		// Echelon @test Codex 13/08/2026 Capture the settled SAGE composition instead of its empty first transition frame.
		const bool screenshotCompositionReady = settingsOpen ? ButtonTransitionFrame(settingsApply, transitionEpoch) >= 17 :
			(modsOpen ? ButtonTransitionFrame(modsBack, transitionEpoch) >= 17 :
				ButtonTransitionFrame(exitButton, transitionEpoch) >= 17);
		if (!internalScreenshotWritten && !parsed.internalTestScreenshot.empty() && screenshotCompositionReady) {
			SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
			if (!surface || !IMG_SavePNG(surface, parsed.internalTestScreenshot.c_str())) {
				fprintf(stderr, "ERROR: Cannot save launcher test screenshot: %s\n", SDL_GetError());
				fflush(stderr);
				workerExitCode = 2;
			} else {
				fprintf(stderr, "INFO: Saved launcher test screenshot: %s\n", parsed.internalTestScreenshot.c_str());
				fflush(stderr);
			}
			SDL_DestroySurface(surface);
			internalScreenshotWritten = true;
			running = false;
		}
		if (!SDL_RenderPresent(renderer) || g_sdlVideoConnectionFailed.load(std::memory_order_acquire)) {
			fprintf(stderr, "ERROR: Launcher presentation failed: %s\n", SDL_GetError());
			fflush(stderr);
			ExitWithoutGlobalDestructors(kWorkerExitRecoverableGraphicsFailure);
		}
		SDL_Delay(8);
	}

	if (migrationRunning) {
		migration.wait();
	}
	if (modificationOperationRunning) {
		if (modificationOperationProgress) {
			modificationOperationProgress->cancelRequested.store(true, std::memory_order_relaxed);
		}
		modificationOperation.wait();
	}
	uiAudio.reset();
	modCoverCache.reset();
	backdrop.reset();
	logo.reset();
	font.reset();
	if (renderer) SDL_DestroyRenderer(renderer);
	for (auto &[id, module] : modules) {
		module.api->shutdown();
	}
	SDL_DestroyWindow(window);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
	ExitWithoutGlobalDestructors(workerExitCode);
}
