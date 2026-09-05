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
#include "LauncherDownloads.h"
#include "LauncherInstaller.h"
#include "LauncherDataImport.h"
#include "LauncherLocalization.h"
#include "LauncherModProfiles.h"
#include "LauncherMods.h"
#include "LauncherRepositories.h"
#include "LauncherS3.h"
#include "LauncherIntegration/ArchiveLoadPolicy.h"
#include "LauncherIntegration/ContentLayerRuntime.h"

#include <SDL3/SDL.h>
#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace EchelonLauncher;

namespace
{

int g_failures = 0;

void Check(bool condition, const char *message)
{
	if (condition) return;
	std::fprintf(stderr, "FAIL: %s\n", message);
	++g_failures;
}

bool CreateZipFixture(const fs::path &path, const std::string &entryName, const std::string &contents)
{
	archive *writer = archive_write_new();
	archive_write_set_format_zip(writer);
	if (archive_write_open_filename(writer, path.string().c_str()) != ARCHIVE_OK) {
		archive_write_free(writer);
		return false;
	}
	archive_entry *entry = archive_entry_new();
	archive_entry_set_pathname(entry, entryName.c_str());
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0644);
	archive_entry_set_size(entry, static_cast<la_int64_t>(contents.size()));
	const bool success = archive_write_header(writer, entry) == ARCHIVE_OK &&
		archive_write_data(writer, contents.data(), contents.size()) == static_cast<la_ssize_t>(contents.size());
	archive_entry_free(entry);
	archive_write_close(writer);
	archive_write_free(writer);
	return success;
}

void WriteBigEndian32(std::ofstream &output, uint32_t value)
{
	const unsigned char bytes[] = {static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
		static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
	output.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

bool CreateBigFixture(const fs::path &path, const std::string &entryName, const std::string &contents);

void AppendLittleEndian32(std::string &output, uint32_t value)
{
	output.push_back(static_cast<char>(value));
	output.push_back(static_cast<char>(value >> 8));
	output.push_back(static_cast<char>(value >> 16));
	output.push_back(static_cast<char>(value >> 24));
}

bool CreateRussianCsfFixture(const fs::path &path, uint32_t labelCount)
{
	std::string csf;
	AppendLittleEndian32(csf, 0x20465343);
	AppendLittleEndian32(csf, 3);
	AppendLittleEndian32(csf, labelCount);
	AppendLittleEndian32(csf, labelCount);
	AppendLittleEndian32(csf, 0);
	AppendLittleEndian32(csf, 8);
	for (uint32_t index = 0; index < labelCount; ++index) {
		const std::string label = "TEST:Label" + std::to_string(index);
		AppendLittleEndian32(csf, 0x204c424c);
		AppendLittleEndian32(csf, 1);
		AppendLittleEndian32(csf, static_cast<uint32_t>(label.size()));
		csf += label;
		AppendLittleEndian32(csf, 0x20525453);
		const std::u16string text = u"Русский текст";
		AppendLittleEndian32(csf, static_cast<uint32_t>(text.size()));
		for (char16_t character : text) {
			const uint16_t encoded = static_cast<uint16_t>(~character);
			csf.push_back(static_cast<char>(encoded));
			csf.push_back(static_cast<char>(encoded >> 8));
		}
	}
	return CreateBigFixture(path, "Data\\English\\generals.csf", csf);
}

bool CreateBigFixture(const fs::path &path, const std::string &entryName, const std::string &contents)
{
	const uint32_t directoryEnd = static_cast<uint32_t>(16 + 8 + entryName.size() + 1);
	const uint32_t archiveSize = directoryEnd + static_cast<uint32_t>(contents.size());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write("BIGF", 4);
	output.write(reinterpret_cast<const char *>(&archiveSize), sizeof(archiveSize));
	WriteBigEndian32(output, 1);
	WriteBigEndian32(output, directoryEnd);
	WriteBigEndian32(output, directoryEnd);
	WriteBigEndian32(output, static_cast<uint32_t>(contents.size()));
	output.write(entryName.c_str(), static_cast<std::streamsize>(entryName.size() + 1));
	output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return static_cast<bool>(output);
}

bool CreatePaddedBigFixture(const fs::path &path, const std::string &entryName,
	const std::string &contents, unsigned char paddingByte)
{
	const uint32_t directoryEnd = static_cast<uint32_t>(16 + 8 + entryName.size() + 1 + 8);
	const uint32_t archiveSize = directoryEnd + static_cast<uint32_t>(contents.size());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write("BIGF", 4);
	output.write(reinterpret_cast<const char *>(&archiveSize), sizeof(archiveSize));
	WriteBigEndian32(output, 1);
	WriteBigEndian32(output, directoryEnd);
	WriteBigEndian32(output, directoryEnd);
	WriteBigEndian32(output, static_cast<uint32_t>(contents.size()));
	output.write(entryName.c_str(), static_cast<std::streamsize>(entryName.size() + 1));
	const unsigned char padding[8] = {
		paddingByte, paddingByte, paddingByte, paddingByte, paddingByte, paddingByte, paddingByte, paddingByte};
	output.write(reinterpret_cast<const char *>(padding), sizeof(padding));
	output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return static_cast<bool>(output);
}

bool CreateEmptyVersionedBigFixture(const fs::path &path)
{
	constexpr uint32_t archiveSize = 23;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write("BIGF", 4);
	output.write(reinterpret_cast<const char *>(&archiveSize), sizeof(archiveSize));
	WriteBigEndian32(output, 0);
	WriteBigEndian32(output, archiveSize);
	const char trailer[7] = {'L', '2', '2', '5', 0, 0, 0};
	output.write(trailer, sizeof(trailer));
	return static_cast<bool>(output);
}

std::string ReadFile(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

// Echelon @feature Codex 13/08/2026 Exercise settings persistence independently from the graphical launcher.
int main()
{
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path testRoot = fs::temp_directory_path() / ("echelon-settings-" + std::to_string(unique));
	const fs::path optionsPath = testRoot / "UserData" / "Generals" / "Options.ini";
	const fs::path launcherPath = testRoot / "Launcher" / "Settings.ini";
	const fs::path sagePatchPath = testRoot / "UserData" / "Generals" / "SagePatch.ini";
	std::error_code error;
	fs::create_directories(optionsPath.parent_path(), error);
	Check(!error, "temporary directory must be created");
	{
		std::ofstream output(optionsPath);
		output << "MusicVolume = 55\n"
			<< "Resolution = 1024 768\n"
			<< "MaxParticleCount = invalid\n"
			<< "TextureReduction = 2\n"
			<< "UseShadowDecals = no\n"
			<< "Resolution = 800 600\n";
	}

	std::string message;
	// Echelon @bugfix Codex 05/09/2026 Retail imports preserve original files, duplicates, conflicts and links.
	const fs::path retailSource = testRoot / "RetailSource";
	const fs::path retailDestination = testRoot / "RetailCopy";
	const fs::path retailBackup = testRoot / "RetailBackup";
	fs::create_directories(retailSource / "Data", error);
	fs::create_directories(retailDestination / "Data", error);
	std::ofstream(retailSource / "INI.big") << "original ini";
	std::ofstream(retailSource / "Fresh.big") << "original fresh";
	std::ofstream(retailSource / "Data/Conflict.ini") << "incoming content";
	std::ofstream(retailDestination / "INI.big") << "original ini";
	std::ofstream(retailDestination / "Data/Conflict.ini") << "existing content";
	fs::create_symlink("Fresh.big", retailSource / "Alias.big", error);
	Check(!error, "retail file symlink fixture must be created");
	Check(CopyRetailDataTree(retailSource, retailDestination, retailBackup, testRoot / "import.log", message),
		"retail data must copy into the product tree");
	Check(ReadFile(retailSource / "INI.big") == "original ini" &&
		ReadFile(retailSource / "Fresh.big") == "original fresh" &&
		ReadFile(retailSource / "Data/Conflict.ini") == "incoming content" &&
		fs::is_symlink(retailSource / "Alias.big"),
		"import must leave new, identical, conflicting and linked source files untouched");
	Check(ReadFile(retailDestination / "Fresh.big") == "original fresh" &&
		ReadFile(retailDestination / "Alias.big") == "original fresh" &&
		!fs::is_symlink(retailDestination / "Alias.big"),
		"import must create independent file copies, including file symlink contents");
	Check(ReadFile(retailDestination / "Data/Conflict.ini") == "existing content" &&
		ReadFile(retailBackup / "Data/Conflict.ini") == "incoming content",
		"import must preserve existing destination content and copy conflicts to backup");
	Check(CopyRetailDataTree(retailSource, retailDestination, retailBackup, testRoot / "import.log", message),
		"repeating the same import must preserve files without duplicate backup errors");
	Check(!CopyRetailDataTree(retailSource, retailSource / "NestedCopy", retailBackup,
		testRoot / "import.log", message) && !fs::exists(retailSource / "NestedCopy"),
		"an overlapping import destination must be rejected before writing into the source");
	const fs::path redirectedDestination = testRoot / "RedirectedCopy";
	fs::create_directories(redirectedDestination, error);
	fs::create_directory_symlink(retailSource / "Data", redirectedDestination / "Data", error);
	Check(!CopyRetailDataTree(retailSource, redirectedDestination, retailBackup,
		testRoot / "import.log", message) && ReadFile(retailSource / "Data/Conflict.ini") == "incoming content",
		"a destination directory symlink must not redirect writes into the original installation");
	message.clear();
	GameOptions options = LoadGameOptions(optionsPath, message);
	Check(message.empty(), "valid Options.ini must load without an error");
	Check(options.resolutionWidth == 1024 && options.resolutionHeight == 768,
		"the first duplicate value must define the loaded option");
	Check(options.maxParticleCount == 2500, "invalid particle count must fall back safely");
	Check(options.textureReduction == 2, "texture quality must be loaded");
	Check(!options.useShadowDecals, "boolean game option must be loaded");

	options.resolutionWidth = 1920;
	options.resolutionHeight = 1080;
	options.maxParticleCount = 4200;
	options.useShadowDecals = true;
	Check(SaveGameOptions(optionsPath, options, message), "game options must save atomically");
	const std::string savedOptions = ReadFile(optionsPath);
	Check(savedOptions.find("MusicVolume = 55") != std::string::npos,
		"unknown Options.ini values must be preserved");
	Check(savedOptions.find("Resolution = 1024 768") == std::string::npos,
		"stale duplicate values must not survive a save");
	Check(savedOptions.find("Resolution = 1920 1080") != std::string::npos,
		"updated resolution must be written");
	Check(savedOptions.find("MaxParticleCount = 4200") != std::string::npos,
		"updated particle count must be written");

	{
		std::ofstream output(sagePatchPath);
		output << "; custom line\nGameData\n  CustomCameraOption = 17\n  MaxCameraHeight = 425.0\nEnd\n";
	}
	SagePatchOptions sagePatch = LoadSagePatchOptions(sagePatchPath, message);
	Check(message.empty() && sagePatch.maxCameraHeight == 425.0f,
		"safe camera overrides must load from SagePatch.ini");
	sagePatch.maxCameraHeight = 550.0f;
	sagePatch.framesPerSecondLimit = 144;
	Check(SaveSagePatchOptions(sagePatchPath, sagePatch, message), "SagePatch options must save atomically");
	const std::string savedSagePatch = ReadFile(sagePatchPath);
	Check(savedSagePatch.find("CustomCameraOption = 17") != std::string::npos,
		"unknown SagePatch.ini values must be preserved");
	Check(savedSagePatch.find("MaxCameraHeight = 550.0") != std::string::npos &&
		savedSagePatch.find("FramesPerSecondLimit = 144") != std::string::npos,
		"camera and frame-rate options must be updated without patching retail data");

	LauncherSettings launcher;
	launcher.language = LanguageMode::Russian;
	launcher.windowMode = LauncherWindowMode::Fullscreen;
	launcher.windowWidth = 1600;
	launcher.windowHeight = 900;
	launcher.generals.windowed = true;
	launcher.generals.quickStart = true;
	launcher.generals.noShellMap = true;
	launcher.generals.russianLocalization = false;
	launcher.generals.additionalArguments = "-xres 1920 \"two words\"";
	launcher.zeroHour.quickStart = true;
	Check(SaveLauncherSettings(launcherPath, launcher, message), "launcher settings must save atomically");
	const LauncherSettings reloaded = LoadLauncherSettings(launcherPath, message);
	Check(message.empty(), "launcher settings must reload without an error");
	Check(reloaded.language == LanguageMode::Russian, "launcher language must round-trip");
	Check(reloaded.windowMode == LauncherWindowMode::Fullscreen &&
		reloaded.windowWidth == 1600 && reloaded.windowHeight == 900,
		"launcher display mode and size must round-trip");
	Check(reloaded.generals.windowed && reloaded.generals.quickStart && reloaded.generals.noShellMap,
		"profile switches must round-trip");
	Check(!reloaded.generals.russianLocalization,
		"localization selection must round-trip");
	Check(ReadFile(launcherPath).find("ActiveModification") == std::string::npos,
		"launcher settings must not bind modifications to vanilla profile buttons");
	Check(reloaded.generals.additionalArguments == launcher.generals.additionalArguments,
		"additional arguments must round-trip");

	std::vector<std::string> arguments{"-win"};
	Check(AppendProfileArguments(reloaded.generals, arguments, message),
		"valid additional arguments must be accepted");
	Check(arguments.size() == 6, "Generals quick start must append without duplicating -win");
	Check(arguments[1] == "-quickstart" && arguments[2] == "-noshellmap" && arguments[3] == "-xres" &&
		arguments[4] == "1920" && arguments[5] == "two words",
		"Generals launch switches and quoted arguments must be tokenized without a shell");
	ProfileLaunchSettings liveShellQuickStart;
	liveShellQuickStart.quickStart = true;
	arguments.clear();
	Check(AppendProfileArguments(liveShellQuickStart, arguments, message) && arguments.size() == 1 &&
		arguments[0] == "-quickstart",
		"quick start must retain the live shell map unless no-shell-map is enabled separately");
	ProfileLaunchSettings explicitQuickStart;
	explicitQuickStart.quickStart = true;
	arguments.assign({"-QUICKSTART"});
	Check(AppendProfileArguments(explicitQuickStart, arguments, message) && arguments.size() == 1,
		"an explicit Generals quick-start argument must not be duplicated");
	ProfileLaunchSettings pathArguments;
	pathArguments.additionalArguments = R"(-mod C:\Games\Arsenal\Example.big)";
	arguments.clear();
	Check(AppendProfileArguments(pathArguments, arguments, message) && arguments.size() == 2 &&
		arguments[1] == R"(C:\Games\Arsenal\Example.big)",
		"Windows path separators must survive argument tokenization");

	ProfileLaunchSettings invalid;
	invalid.additionalArguments = "-foo \"unfinished";
	arguments.clear();
	Check(!AppendProfileArguments(invalid, arguments, message),
		"unmatched quotes must be rejected before settings are published");

	// Echelon @feature Codex 13/08/2026 Validate safe manifest discovery and exact archive filtering.
	const fs::path modsRoot = testRoot / "Mods";
	fs::create_directories(modsRoot / "Example" / "1.0", error);
	{
		std::ofstream archive(modsRoot / "Example" / "1.0" / "Example.big");
		archive << "test";
		std::ofstream manifest(modsRoot / "Example" / "1.0" / "mod.ini");
		manifest << "[Modification]\nSchemaVersion=1\nId=example-mod\nName=Example Mod\n"
			<< "Version=1.0\nEngine=generals\nLaunchPath=Example.big\n";
	}
	const ModificationCatalog catalog = LoadModificationCatalog(modsRoot);
	const InstalledModification *installed = FindModification(catalog, "generals", "EXAMPLE-MOD@1.0");
	Check(catalog.warnings.empty() && installed && installed->name == "Example Mod",
		"a valid installed modification manifest must be discovered case-insensitively");
	Check(installed && installed->id == "local:example-mod" && installed->legacyManifest &&
		fs::is_regular_file(modsRoot / "Example" / "1.0" / "manifest.ini"),
		"a V1 manifest must migrate atomically to a source-qualified V2 manifest");
	Check(ModificationsForEngine(catalog, "zerohour").empty(),
		"modification manifests must remain isolated to their declared engine");
	const fs::path patchRoot = modsRoot / "Installed" / "zerohour" / "patch" / "arsenal:balance" / "2.1";
	fs::create_directories(patchRoot, error);
	{
		std::ofstream manifest(patchRoot / "manifest.ini");
		manifest << "[Modification]\nSchemaVersion=2\nId=arsenal:balance\nSourceId=arsenal\n"
			<< "Name=Shared Name\nVersion=2.1\nEngine=zerohour\nType=patch\n"
			<< "ParentId=arsenal:example\nSource=https://example.invalid/catalog\nRootPath=.\n"
			<< "CoverImage=\nSHA256=\nContentFingerprint=\n";
	}
	const fs::path secondSource = modsRoot / "Installed" / "zerohour" / "patch" / "community:balance" / "2.1";
	fs::create_directories(secondSource, error);
	{
		std::ofstream manifest(secondSource / "manifest.ini");
		manifest << "[Modification]\nSchemaVersion=2\nId=community:balance\nSourceId=community\n"
			<< "Name=Shared Name\nVersion=2.1\nEngine=zerohour\nType=patch\n"
			<< "ParentId=community:example\nSource=https://example.invalid/other\nRootPath=.\n"
			<< "CoverImage=\nSHA256=\nContentFingerprint=\n";
	}
	const ModificationCatalog v2Catalog = LoadModificationCatalog(modsRoot);
	Check(ModificationsForEngineAndType(v2Catalog, "zerohour", ModificationType::Patch).size() == 2,
		"source-qualified IDs must keep same-name modifications independent");
	Check(CompareModificationVersions("2.10", "2.9") > 0 &&
		CompareModificationVersions("1.0", "1.0.0") == 0 &&
		CompareModificationVersions("1.0", "1.0-rc1") > 0 &&
		CompareModificationVersions("10.0", "2.0") > 0,
		"modification versions must compare naturally without offering downgrades as updates");

	// Echelon @test Codex 14/08/2026 Lock both external YAML compatibility and the stricter Arsenal schema.
	const std::string genLauncherIndex =
		"modDatas:\n"
		"- ModName: Example Mod\n"
		"  ModLink: https://example.invalid/mod.yaml\n"
		"  ModPatches:\n"
		"  - https://example.invalid/patch.yaml\n"
		"  ModAddons:\n"
		"  - https://example.invalid/addon.yaml\n"
		"globalAddonsData: []\noriginalGameAddons: []\noriginalGamePatches: []\n";
	const RepositoryIndex repositoryIndex = ParseGenLauncherRepositoryIndex(
		genLauncherIndex, "fixture", "zerohour");
	Check(repositoryIndex.warnings.empty() && repositoryIndex.descriptors.size() == 3,
		"GenLauncher indexes must preserve mod, patch, and addon relationships");
	const std::string genLauncherItem =
		"ModificationType: Patch\nName: Example Balance\nVersion: 1.2.3\n"
		"SimpleDownloadLink: https://example.invalid/archive.zip\n"
		"UIImageSourceLink: https://example.invalid/cover.png\n"
		"DependenceName: Example Mod\nS3HostLink: ''\nS3BucketName: ''\nS3FolderName: ''\n";
	RepositoryModification repositoryModification;
	Check(ParseGenLauncherModification(genLauncherItem, repositoryIndex.descriptors[1],
		repositoryModification, message) && repositoryModification.id == "fixture:example-balance" &&
		repositoryModification.parentId == "fixture:example-mod" &&
		repositoryModification.type == ModificationType::Patch,
		"GenLauncher item metadata must normalize into stable source-qualified records");
	// Echelon @test Codex 15/08/2026 Prove that one catalog can add a complete content stack without launcher changes.
	const std::string arsenalCatalog =
		"SchemaVersion: 1\nSourceId: arsenal-test\nItems:\n"
		"- Id: shared-name\n  Engine: generals\n  Type: Mod\n  Name: Shared Name\n  Version: 1.0\n"
		"  DownloadUrl: https://example.invalid/shared.zip\n  CoverUrl: https://example.invalid/shared.png\n"
		"  ModDBLink: https://www.moddb.com/mods/shared\n  SupportLink: https://support.example.invalid/shared\n"
		"  SHA256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n  Size: 1024\n"
		"- Id: shared-patch\n  Engine: generals\n  Type: Patch\n  ParentId: shared-name\n"
		"  Requires: [shared-name@1.0]\n"
		"  Name: Shared Patch\n  Version: 1.1\n  DownloadUrl: https://example.invalid/patch.zip\n"
		"  SHA256: 1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n  Size: 512\n"
		"- Id: shared-addon\n  Engine: generals\n  Type: Addon\n  ParentId: shared-patch\n"
		"  Requires: [shared-patch@1.1]\n  Conflicts: [shared-name]\n"
		"  Name: Shared Addon\n  Version: 1.2\n  DownloadUrl: https://example.invalid/addon.zip\n"
		"  SHA256: 2123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n  Size: 256\n"
		"- Id: author-file-set\n  Engine: zerohour\n  Type: Mod\n  Name: Author File Set\n  Version: 2.0\n"
		"  Files:\n"
		"  - Path: '!Core.gib'\n    DownloadUrl: https://files.example.invalid/core.gib\n"
		"    SHA256: 3123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n    Size: 4096\n"
		"  - Path: Data/Maps.big\n    DownloadUrl: https://files.example.invalid/maps.big\n"
		"    SHA256: 4123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n    Size: 8192\n";
	const RepositoryCatalog arsenalRepository = ParseArsenalRepositoryCatalogV1(arsenalCatalog, "fallback");
	Check(arsenalRepository.warnings.empty() && arsenalRepository.modifications.size() == 4 &&
		arsenalRepository.modifications[0].id == "arsenal-test:shared-name" &&
		arsenalRepository.modifications[0].size == 1024 &&
		arsenalRepository.modifications[0].modDbUrl == "https://www.moddb.com/mods/shared" &&
		arsenalRepository.modifications[0].supportUrl == "https://support.example.invalid/shared" &&
		arsenalRepository.modifications[1].type == ModificationType::Patch &&
		arsenalRepository.modifications[1].parentId == "arsenal-test:shared-name" &&
		arsenalRepository.modifications[1].requirements == std::vector<std::string>{"arsenal-test:shared-name@1.0"} &&
		arsenalRepository.modifications[2].type == ModificationType::Addon &&
		arsenalRepository.modifications[2].parentId == "arsenal-test:shared-patch" &&
		arsenalRepository.modifications[2].requirements == std::vector<std::string>{"arsenal-test:shared-patch@1.1"} &&
		arsenalRepository.modifications[2].conflicts == std::vector<std::string>{"arsenal-test:shared-name"} &&
		arsenalRepository.modifications[3].contentFiles.size() == 2 &&
		arsenalRepository.modifications[3].contentFiles[0].path == fs::path("!Core.gib") &&
		arsenalRepository.modifications[3].contentFiles[1].size == 8192,
		"RepositoryCatalogV1 must retain dynamic relationships, exact requirements, conflicts, and integrity metadata");
	const RepositoryCatalog crossOriginArsenalRepository = ParseArsenalRepositoryCatalogV1(
		arsenalCatalog, "fallback", "https://catalog.example.invalid/v1/catalog.json");
	Check(crossOriginArsenalRepository.modifications.size() == 1 &&
		crossOriginArsenalRepository.modifications[0].id == "arsenal-test:author-file-set" &&
		!crossOriginArsenalRepository.warnings.empty(),
		"centralized Arsenal catalogs must not load mutable covers from another origin");
	const RepositoryCatalog authorHostedRepository = ParseArsenalRepositoryCatalogV1(
		"SchemaVersion: 1\nSourceId: author\nItems:\n- Id: hosted\n  Engine: zerohour\n  Type: Mod\n"
		"  Name: Hosted\n  Version: 1\n  DownloadUrl: https://author.example.invalid/hosted.zip\n"
		"  SHA256: 5123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n  Size: 99\n",
		"fallback", "https://catalog.example.invalid/catalog.json");
	Check(authorHostedRepository.warnings.empty() && authorHostedRepository.modifications.size() == 1,
		"hash-pinned author-hosted packages must remain installable from the centralized catalog");
	const RepositoryCatalog unsafeFileSetRepository = ParseArsenalRepositoryCatalogV1(
		"SchemaVersion: 1\nSourceId: unsafe-files\nItems:\n- Id: bad\n  Engine: zerohour\n  Type: Mod\n"
		"  Name: Bad\n  Version: 1\n  Files:\n  - Path: ../escape.gib\n"
		"    DownloadUrl: https://example.invalid/escape.gib\n"
		"    SHA256: 6123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n    Size: 10\n",
		"fallback");
	Check(unsafeFileSetRepository.modifications.empty() && !unsafeFileSetRepository.warnings.empty(),
		"multi-file catalogs must reject unsafe paths before any download starts");
	const RepositoryCatalog unsafeArsenalRepository = ParseArsenalRepositoryCatalogV1(
		"SchemaVersion: 1\nSourceId: unsafe\nItems:\n- Id: bad\n  Engine: generals\n  Type: Mod\n"
		"  Name: Bad\n  Version: 1\n  DownloadUrl: http://example.invalid/bad.zip\n  SHA256: bad\n  Size: 0\n",
		"fallback");
	Check(unsafeArsenalRepository.modifications.empty() && !unsafeArsenalRepository.warnings.empty(),
		"native Arsenal catalogs must require HTTPS, a full SHA-256, and a non-zero size");
	const fs::path repositoryConfigRoot = testRoot / "RepositoryConfig";
	fs::create_directories(repositoryConfigRoot, error);
	{
		std::ofstream sources(repositoryConfigRoot / "Repositories.ini");
		sources << "[Source.arsenal]\nId=arsenal-community\nEngine=generals\n"
			"URL=https://example.invalid/catalog.yaml\nFormat=arsenal-v1\n";
	}
	std::vector<std::string> sourceWarnings;
	const std::vector<RepositorySource> configuredSources =
		LoadConfiguredRepositorySources(repositoryConfigRoot, sourceWarnings);
	Check(sourceWarnings.empty() && configuredSources.size() == 1 &&
		configuredSources[0].id == "arsenal-community" &&
		configuredSources[0].format == RepositoryFormat::ArsenalV1,
		"legacy repository configuration must remain parseable for offline migration tools");
	unsetenv("ECHELON_REPOSITORY_URL");
	Check(BuiltInRepositorySources().empty(), "Echelon must not configure a production mod catalog");
	setenv("ECHELON_REPOSITORY_URL", "http://example.invalid/catalog.yaml", 1);
	Check(BuiltInRepositorySources().empty(), "transport fixtures must require HTTPS");
	setenv("ECHELON_REPOSITORY_URL", "https://example.invalid/catalog.yaml", 1);
	const auto fixtureSources = BuiltInRepositorySources();
	Check(fixtureSources.size() == 1 && fixtureSources[0].id == "fixture" &&
		fixtureSources[0].indexUrl == "https://example.invalid/catalog.yaml",
		"transport fixtures must require explicit configuration");
	unsetenv("ECHELON_REPOSITORY_URL");
	RepositoryModification insecureS3;
	insecureS3.id = "fixture:s3";
	insecureS3.sourceId = "fixture";
	insecureS3.engine = "generals";
	insecureS3.name = "S3 Fixture";
	insecureS3.version = "1.0";
	insecureS3.s3Host = "http://storage.example.invalid:9000";
	insecureS3.s3Bucket = "mods";
	insecureS3.s3Folder = "fixture";
#if defined(_WIN32)
	_putenv_s("ECHELON_S3_ACCESS_KEY", "test-access");
	_putenv_s("ECHELON_S3_SECRET_KEY", "test-secret");
#else
	setenv("ECHELON_S3_ACCESS_KEY", "test-access", 1);
	setenv("ECHELON_S3_SECRET_KEY", "test-secret", 1);
#endif
	fs::path unusedS3Root;
	Check(!DownloadS3Modification(insecureS3, testRoot / "S3Mods", unusedS3Root, message) &&
		message.find("HTTPS") != std::string::npos,
		"S3 credentials must never be sent to a non-TLS endpoint");
#if defined(_WIN32)
	_putenv_s("ECHELON_S3_ACCESS_KEY", "");
	_putenv_s("ECHELON_S3_SECRET_KEY", "");
#else
	unsetenv("ECHELON_S3_ACCESS_KEY");
	unsetenv("ECHELON_S3_SECRET_KEY");
#endif
	if (const char *networkUrl = std::getenv("ECHELON_QA_HTTPS_URL"); networkUrl && networkUrl[0]) {
		const char *packagePathValue = std::getenv("ECHELON_QA_PACKAGE_PATH");
		const char *packageSha256 = std::getenv("ECHELON_QA_PACKAGE_SHA256");
		const char *packageSizeValue = std::getenv("ECHELON_QA_PACKAGE_SIZE");
		Check(packagePathValue && packageSha256 && packageSizeValue,
			"network resume fixture requires package path, SHA-256, and size");
		if (packagePathValue && packageSha256 && packageSizeValue) {
			const fs::path packagePath(packagePathValue);
			const uint64_t packageSize = std::stoull(packageSizeValue);
			auto preparePartial = [&](const fs::path &mods, const std::string &id, const std::string &version,
				const std::string &etag) {
				std::string slug = id;
				std::replace(slug.begin(), slug.end(), ':', '-');
				std::replace(slug.begin(), slug.end(), '.', '-');
				std::string versionSlug = version;
				std::replace(versionSlug.begin(), versionSlug.end(), '.', '-');
				const fs::path root = mods / ".staging" / "downloads" / slug / versionSlug;
				fs::create_directories(root, error);
				std::ifstream input(packagePath, std::ios::binary);
				std::ofstream output(root / "package.partial", std::ios::binary | std::ios::trunc);
				std::vector<char> prefix(static_cast<size_t>(std::max<uint64_t>(1, packageSize / 3)));
				input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
				output.write(prefix.data(), input.gcount());
				std::ofstream(root / "download.ini") << "Size=" << packageSize << "\nETag=" << etag << '\n';
				return root / "package.partial";
			};
			auto networkModification = [&](const std::string &id, const std::string &version, const std::string &endpoint) {
				RepositoryModification modification;
				modification.id = id;
				modification.sourceId = "network";
				modification.engine = "generals";
				modification.type = ModificationType::Mod;
				modification.name = "Network Resume Fixture";
				modification.version = version;
				modification.downloadUrl = std::string(networkUrl) + endpoint;
				modification.sha256 = packageSha256;
				modification.size = packageSize;
				return modification;
			};
			const fs::path networkMods = testRoot / "NetworkMods";
			preparePartial(networkMods, "network:resume", "1.0", "test-etag");
			ModificationOperationResult networkResult = DownloadAndInstallModification(
				networkModification("network:resume", "1.0", "/package.zip"), networkMods);
			if (!networkResult.success) std::fprintf(stderr, "NETWORK RESUME ERROR: %s\n", networkResult.message.c_str());
			Check(networkResult.success, "HTTP Range resume must complete and install a verified package");
			preparePartial(networkMods, "network:changed", "1.0", "old-etag");
			networkResult = DownloadAndInstallModification(
				networkModification("network:changed", "1.0", "/changed.zip"), networkMods);
			if (!networkResult.success) std::fprintf(stderr, "NETWORK ETAG ERROR: %s\n", networkResult.message.c_str());
			Check(networkResult.success, "an ETag change must discard stale bytes and restart from zero");
			const fs::path ignoredPartial = preparePartial(networkMods, "network:ignored", "1.0", "test-etag");
			networkResult = DownloadAndInstallModification(
				networkModification("network:ignored", "1.0", "/ignore-range.zip"), networkMods);
			if (!networkResult.success) {
				std::fprintf(stderr, "NETWORK RANGE ERROR: %s\n", networkResult.message.c_str());
			}
			Check(networkResult.success && !fs::exists(ignoredPartial) &&
				fs::is_regular_file(networkResult.installedRoot / "content/Data/INI/NetworkResume.ini"),
				"a server that ignores Range must restart from zero and publish only verified content");
			RepositoryModification multiFileModification;
			multiFileModification.id = "network:multi-file";
			multiFileModification.sourceId = "network";
			multiFileModification.engine = "zerohour";
			multiFileModification.type = ModificationType::Mod;
			multiFileModification.name = "Parallel File Set Fixture";
			multiFileModification.version = "1.0";
			multiFileModification.contentFiles = {
				{fs::path("Data/First.dat"), std::string(networkUrl) + "/package.zip", packageSha256, packageSize},
				{fs::path("Data/Second.dat"), std::string(networkUrl) + "/package.zip", packageSha256, packageSize}
			};
			networkResult = DownloadAndInstallModification(multiFileModification, networkMods);
			if (!networkResult.success) std::fprintf(stderr, "NETWORK MULTI-FILE ERROR: %s\n", networkResult.message.c_str());
			Check(networkResult.success &&
				fs::is_regular_file(networkResult.installedRoot / "content/Data/First.dat") &&
				fs::is_regular_file(networkResult.installedRoot / "content/Data/Second.dat"),
				"multi-file releases must download, verify, and publish every content file as one modification");
		}
	}

	const fs::path importRoot = testRoot / "ImportMods";
	const fs::path safeFolder = testRoot / "SafeImport";
	fs::create_directories(safeFolder / "Data", error);
	Check(CreateBigFixture(safeFolder / "Data" / "Example.big", "Data/ArsenalQA/Example.txt", "safe-content"),
		"valid BIG test fixture must be created");
	Check(CreatePaddedBigFixture(safeFolder / "Data" / "FinalBigPadded.big",
		"Data/ArsenalQA/Padded.txt", "finalbig-content", 0),
		"zero-padded FinalBIG test fixture must be created");
	Check(CreateEmptyVersionedBigFixture(safeFolder / "Data" / "EmptyOverride.gib"),
		"versioned empty BIG override fixture must be created");
	LocalImportRequest importRequest;
	importRequest.inputPath = safeFolder;
	importRequest.modsRoot = importRoot;
	importRequest.engine = "generals";
	importRequest.type = ModificationType::Mod;
	importRequest.displayName = "Safe Example";
	importRequest.version = "1.0";
	importRequest.requirements = {"fixture:required@1.0"};
	importRequest.conflicts = {"fixture:conflicting"};
	ModificationOperationProgress importProgress;
	const ModificationOperationResult importResult = ImportLocalModification(importRequest, &importProgress);
	Check(importResult.success && fs::is_regular_file(importResult.installedRoot / "manifest.ini"),
		"local folders must publish atomically with a V2 manifest");
	const ModificationCatalog importedCatalog = LoadModificationCatalog(importRoot);
	const InstalledModification *imported = FindModification(importedCatalog, "generals", "local:safe-example@1.0");
	Check(imported && imported->requirements == importRequest.requirements &&
		imported->conflicts == importRequest.conflicts && VerifyInstalledModification(*imported, message),
		"installed local content must preserve compatibility metadata and pass deterministic SHA-256 verification");
	if (imported) {
		{
			std::ofstream tampered(imported->launchPath / "Data" / "Example.big", std::ios::binary | std::ios::app);
			tampered << "tampered";
		}
		message.clear();
		Check(!VerifyInstalledModification(*imported, message) && !message.empty(),
			"launch-time verification must reject content changed after installation");
		const ModificationOperationResult removed = MoveInstalledModificationToTrash(importRoot, *imported);
		Check(removed.success && fs::is_directory(removed.installedRoot) &&
			!fs::exists(importResult.installedRoot),
			"removal must move a version to recoverable trash instead of deleting it");
		const std::vector<RecoverableModification> recoverable = ListRecoverableModifications(importRoot);
		Check(!recoverable.empty() && recoverable.front().path == removed.installedRoot,
			"recoverable trash must expose removed versions without scanning them as installed");
		const ModificationOperationResult restored = RestoreModificationFromTrash(importRoot, removed.installedRoot);
		const ModificationCatalog restoredCatalog = LoadModificationCatalog(importRoot);
		Check(restored.success && FindModification(restoredCatalog, "generals", restored.selectionKey),
			"a removed version must restore atomically to its validated installed location");
		const InstalledModification *restoredModification =
			FindModification(restoredCatalog, "generals", restored.selectionKey);
		const fs::path customCover = testRoot / "custom-cover.png";
		std::ofstream(customCover, std::ios::binary) << "safe-test-image";
		const ModificationOperationResult coverResult = restoredModification ?
			ReplaceModificationCover(importRoot, *restoredModification, customCover) : ModificationOperationResult{};
		const ModificationCatalog coveredCatalog = LoadModificationCatalog(importRoot);
		const InstalledModification *coveredModification =
			FindModification(coveredCatalog, "generals", restored.selectionKey);
		if (!coverResult.success) std::fprintf(stderr, "COVER ERROR: %s\n", coverResult.message.c_str());
		if (!coveredModification) {
			for (const std::string &warning : coveredCatalog.warnings) std::fprintf(stderr, "COVER WARNING: %s\n", warning.c_str());
		}
		Check(coverResult.success && coveredModification &&
			coveredModification->coverImagePath.filename().string().rfind("cover.custom-", 0) == 0,
			"manual cover replacement must publish through the manifest without changing content files");
	}
	const fs::path unsafeFolder = testRoot / "UnsafeImport";
	fs::create_directories(unsafeFolder, error);
	{
		std::ofstream binary(unsafeFolder / "payload.dll", std::ios::binary);
		binary << "not-executable-in-tests";
	}
	importRequest.inputPath = unsafeFolder;
	importRequest.displayName = "Unsafe Binary";
	Check(!ImportLocalModification(importRequest).success,
		"local import must reject executable components before publication");
	const fs::path malformedBigFolder = testRoot / "MalformedBigImport";
	fs::create_directories(malformedBigFolder, error);
	{
		std::ofstream malformed(malformedBigFolder / "Broken.big", std::ios::binary);
		malformed << "BIGF-truncated";
	}
	importRequest.inputPath = malformedBigFolder;
	importRequest.displayName = "Malformed BIG";
	Check(!ImportLocalModification(importRequest).success,
		"local import must reject malformed BIG files before the legacy engine parser can see them");
	const fs::path invalidTrailerFolder = testRoot / "InvalidTrailerImport";
	fs::create_directories(invalidTrailerFolder, error);
	Check(CreatePaddedBigFixture(invalidTrailerFolder / "InvalidTrailer.big",
		"Data/ArsenalQA/Invalid.txt", "invalid-trailer", 1),
		"invalid padded BIG test fixture must be created");
	importRequest.inputPath = invalidTrailerFolder;
	importRequest.displayName = "Invalid BIG Trailer";
	Check(!ImportLocalModification(importRequest).success,
		"BIG validation must reject non-zero FinalBIG directory padding");
	const fs::path standaloneGib = testRoot / "Standalone.gib";
	Check(CreateBigFixture(standaloneGib, "Data/ArsenalQA/Standalone.txt", "standalone-gib"),
		"valid standalone GIB test fixture must be created");
	importRequest.inputPath = standaloneGib;
	importRequest.displayName = "Standalone GIB";
	importRequest.version = "1.0";
	importRequest.requirements.clear();
	importRequest.conflicts.clear();
	const ModificationOperationResult gibImport = ImportLocalModification(importRequest);
	Check(gibImport.success && fs::is_regular_file(gibImport.installedRoot / "content" / "Standalone.gib"),
		"standalone GenLauncher GIB files must install as validated BIG content without archive extraction");
	const fs::path symlinkFolder = testRoot / "SymlinkImport";
	fs::create_directories(symlinkFolder, error);
	fs::create_symlink(safeFolder / "Data" / "Example.big", symlinkFolder / "linked.big", error);
	if (!error) {
		importRequest.inputPath = symlinkFolder;
		importRequest.displayName = "Unsafe Symlink";
		Check(!ImportLocalModification(importRequest).success,
			"local import must reject symbolic links instead of following them");
	}
	error.clear();
	importRequest.inputPath = safeFolder;
	importRequest.displayName = "Cancelled Import";
	ModificationOperationProgress cancelledProgress;
	cancelledProgress.cancelRequested.store(true, std::memory_order_relaxed);
	const ModificationOperationResult cancelledImport = ImportLocalModification(importRequest, &cancelledProgress);
	Check(!cancelledImport.success && cancelledImport.cancelled,
		"cancelled imports must stop before publication and retain recoverable staging");
	importRequest.displayName = "Invalid Metadata";
	importRequest.version = "1.0\nInjected=true";
	Check(!ImportLocalModification(importRequest).success,
		"untrusted catalog metadata must not inject manifest fields");
	importRequest.version = "1.0";
	importRequest.displayName = "Hash Mismatch";
	importRequest.inputPath = safeFolder / "Data" / "Example.big";
	importRequest.expectedSha256 = std::string(64, '0');
	Check(!ImportLocalModification(importRequest).success,
		"a package with an unexpected SHA-256 must fail before extraction");
	importRequest.expectedSha256.clear();
	const fs::path traversalArchive = testRoot / "traversal.zip";
	Check(CreateZipFixture(traversalArchive, "../escape.big", "escape"),
		"path traversal fixture must be created");
	importRequest.inputPath = traversalArchive;
	importRequest.displayName = "Traversal";
	Check(!ImportLocalModification(importRequest).success && !fs::exists(testRoot / "escape.big"),
		"archive extraction must reject paths outside staging");
	fs::create_directories(importRoot / ".staging" / "downloads" / "fixture" / "1.0", error);
	std::ofstream(importRoot / ".staging" / "downloads" / "fixture" / "1.0" / "download.ini") << "ETag=test\n";
	fs::create_directories(importRoot / ".staging" / "s3" / "fixture" / "1.0" / "content", error);
	const ModificationRecoverySummary recovery = RecoverInterruptedModificationOperations(importRoot);
	Check(recovery.resumableDownloads == 1 && recovery.resumableS3Transfers == 1 &&
		recovery.preservedInterruptedImports > 0 && recovery.recoverableTrashEntries == 0 &&
		ListRecoverableModifications(importRoot).empty(),
		"startup recovery must preserve resumable transfers without presenting incomplete imports as restorable mods");
#if defined(_WIN32)
	_putenv_s("ECHELON_DISABLED_BIG_FILES", "00Russian.big; 00RussianZH.big");
#else
	setenv("ECHELON_DISABLED_BIG_FILES", "00Russian.big; 00RussianZH.big", 1);
#endif
	Check(EchelonArchivePolicy::IsArchiveDisabled("/game/00RUSSIAN.big") &&
		EchelonArchivePolicy::IsArchiveDisabled("C:\\game\\00RussianZH.big") &&
		!EchelonArchivePolicy::IsArchiveDisabled("00RussianPatch.big"),
		"archive policy must match exact basenames and never prefixes");
#if defined(_WIN32)
	_putenv_s("ECHELON_DISABLED_BIG_FILES", "");
#else
	unsetenv("ECHELON_DISABLED_BIG_FILES");
#endif

	launcher.generals.additionalArguments.clear();
	GameOptions zeroHourOptions;
	SagePatchOptions zeroHourSagePatch;
	Check(SaveSettingsBundle(testRoot, launcher, options, zeroHourOptions, sagePatch, zeroHourSagePatch, message),
		"all launcher and engine settings must publish as one transaction");
	Check(fs::is_regular_file(testRoot / "UserData" / "GeneralsZH" / "Options.ini") &&
		fs::is_regular_file(testRoot / "UserData" / "GeneralsZH" / "SagePatch.ini"),
		"the settings bundle must publish both engine profiles");
	Check(!fs::exists(testRoot / "Launcher" / "Settings.transaction"),
		"a committed settings transaction must remove its journal");

	const fs::path interruptedTarget = testRoot / "UserData" / "Generals" / "Options.ini";
	const std::string transaction = "recovery-test";
	fs::rename(interruptedTarget, interruptedTarget.string() + ".backup-" + transaction, error);
	{
		std::ofstream output(interruptedTarget);
		output << "Resolution = 640 480\n";
	}
	{
		std::ofstream output(testRoot / "Launcher" / "Settings.transaction");
		output << transaction << "\n1\n1\n1\n1\n1\n";
	}
	Check(RecoverInterruptedSettingsBundle(testRoot, message),
		"an interrupted settings transaction must be recovered on startup");
	Check(ReadFile(interruptedTarget).find("Resolution = 1920 1080") != std::string::npos,
		"transaction recovery must restore the previous settings file");

	const fs::path newlyCreatedTarget = testRoot / "UserData" / "GeneralsZH" / "Options.ini";
	fs::remove(newlyCreatedTarget, error);
	{
		std::ofstream output(newlyCreatedTarget);
		output << "Resolution = 640 480\n";
	}
	{
		std::ofstream output(testRoot / "Launcher" / "Settings.transaction");
		output << "new-file-recovery\n1\n0\n1\n1\n1\n";
	}
	Check(RecoverInterruptedSettingsBundle(testRoot, message),
		"an interrupted transaction containing a new file must be recovered");
	Check(!fs::exists(newlyCreatedTarget), "transaction recovery must remove a partially published new file");

	const fs::path modLayerRoot = testRoot / "LayerMod";
	const fs::path patchLayerRoot = testRoot / "LayerPatch";
	fs::create_directories(modLayerRoot / "Data" / "INI", error);
	fs::create_directories(patchLayerRoot / "data" / "ini" / "A", error);
	{
		std::ofstream(modLayerRoot / "Data" / "INI" / "Layer.ini") << "layer=mod\n";
		std::ofstream(patchLayerRoot / "data" / "ini" / "Layer.ini") << "layer=patch\n";
	}
	const std::string modRootString = modLayerRoot.string();
	const std::string patchRootString = patchLayerRoot.string();
	const std::string modFingerprint(64, 'a');
	const std::string patchFingerprint(64, 'b');
	const EchelonContentLayerV1 runtimeLayers[] = {
		{sizeof(EchelonContentLayerV1), ECHELON_CONTENT_LAYER_MOD,
			"fixture:mod", "1.0", modRootString.c_str(), 100, modFingerprint.c_str()},
		{sizeof(EchelonContentLayerV1), ECHELON_CONTENT_LAYER_PATCH,
			"fixture:patch", "1.0", patchRootString.c_str(), 200, patchFingerprint.c_str()}
	};
	message.clear();
	Check(EchelonContentRuntime::Configure(runtimeLayers, 2, message),
		"ABI content layers must validate before activation");
	fs::path resolvedLayerFile;
	Check(EchelonContentRuntime::ResolveReadPath("Data\\INI\\Layer.ini", resolvedLayerFile) &&
		ReadFile(resolvedLayerFile) == "layer=patch\n",
		"a later loose-file layer must shadow an earlier layer");
	const std::vector<fs::path> layeredFiles =
		EchelonContentRuntime::ListFiles("Data/INI", "*.ini", true);
	Check(layeredFiles.size() == 1 && layeredFiles.front() == fs::path("data/ini/layer.ini") &&
		EchelonContentRuntime::ResolveReadPath(layeredFiles.front().string().c_str(), resolvedLayerFile) &&
		ReadFile(resolvedLayerFile) == "layer=patch\n",
		"enumeration must return a logical path that opens the highest-priority file despite directory casing");
	std::ofstream(patchLayerRoot / "data/ini/Z.ini") << "root ini";
	std::ofstream(patchLayerRoot / "data/ini/A/Sub.INI") << "nested ini";
	const auto orderedLayerFiles = EchelonContentRuntime::ListFiles("Data\\INI\\", "*.ini", true);
	Check(orderedLayerFiles == std::vector<fs::path>{"data/ini/a/sub.ini", "data/ini/layer.ini", "data/ini/z.ini"},
		"nested enumeration must preserve directory-relative names for the INI root-before-subdirectory rule");
	Check(EchelonContentRuntime::ListFiles("DATA/INI", "*.ini", false) ==
		std::vector<fs::path>{"data/ini/layer.ini", "data/ini/z.ini"},
		"nonrecursive enumeration must find mixed-case layer directories and exclude nested INIs");
	Check(EchelonContentRuntime::ListFiles("../", "*.ini", true).empty(),
		"layer directory enumeration must reject traversal outside the layer root");
	EchelonContentRuntime::Clear();
	Check(EchelonContentRuntime::IsClear(), "content layer runtime must be empty at session quiescence");

	ModificationCatalog stackCatalog;
	InstalledModification stackMod;
	stackMod.id = "fixture:mod";
	stackMod.sourceId = "fixture";
	stackMod.name = "Fixture Mod";
	stackMod.version = "1.0";
	stackMod.engine = "generals";
	stackMod.type = ModificationType::Mod;
	stackMod.contentFingerprint = modFingerprint;
	stackMod.launchPath = modLayerRoot;
	InstalledModification stackPatch = stackMod;
	stackPatch.id = "fixture:patch";
	stackPatch.name = "Fixture Patch";
	stackPatch.type = ModificationType::Patch;
	stackPatch.parentId = stackMod.id;
	stackPatch.requirements = {"fixture:mod@1.0"};
	stackPatch.contentFingerprint = patchFingerprint;
	stackPatch.launchPath = patchLayerRoot;
	InstalledModification stackAddon = stackMod;
	stackAddon.id = "fixture:addon";
	stackAddon.name = "Fixture Addon";
	stackAddon.type = ModificationType::Addon;
	stackAddon.parentId = stackPatch.id;
	stackAddon.requirements = {"fixture:patch@1.0"};
	stackAddon.contentFingerprint = std::string(64, 'c');
	InstalledModification stackBaseAddon = stackAddon;
	stackBaseAddon.id = "fixture:base-addon";
	stackBaseAddon.name = "Fixture Base Addon";
	stackBaseAddon.parentId = stackMod.id;
	stackBaseAddon.requirements = {"fixture:mod@1.0"};
	stackBaseAddon.contentFingerprint = std::string(64, 'f');
	stackCatalog.modifications = {stackMod, stackPatch, stackAddon, stackBaseAddon};
	ModificationStack stack;
	message.clear();
	Check(ResolveModificationStack(stackCatalog, "generals", "fixture:mod@1.0",
		{"fixture:patch@1.0"}, {"fixture:addon@1.0"}, stack, message) &&
		stack.layers.size() == 3 && stack.layers[0]->type == ModificationType::Mod &&
		stack.layers[1]->type == ModificationType::Patch && stack.layers[2]->type == ModificationType::Addon &&
		stack.fingerprint.size() == 64,
		"managed stacks must resolve deterministically in mod, patch, addon order");
	message.clear();
	Check(!ResolveModificationStack(stackCatalog, "generals", "", {"fixture:patch@1.0"}, {}, stack, message),
		"a dependent patch must not launch without its parent mod");
	stackCatalog.modifications[1].requirements = {"fixture:mod@2.0"};
	message.clear();
	Check(!ResolveModificationStack(stackCatalog, "generals", "fixture:mod@1.0",
		{"fixture:patch@1.0"}, {}, stack, message),
		"an exact-version requirement must reject a different installed parent version");
	stackCatalog.modifications[1].requirements = {"fixture:mod@1.0"};

	ModificationSelectionProfileV1 patchTransitionProfile;
	patchTransitionProfile.engine = "generals";
	message.clear();
	Check(SelectCompatibleModification(stackCatalog, patchTransitionProfile, &stackMod, message) &&
		SelectCompatibleModification(stackCatalog, patchTransitionProfile, &stackBaseAddon, message) &&
		SelectCompatibleModification(stackCatalog, patchTransitionProfile, &stackPatch, message) &&
		patchTransitionProfile.patchSelection == ModificationSelectionKey(stackPatch) &&
		patchTransitionProfile.addonSelections.size() == 1 &&
		patchTransitionProfile.addonSelections.front() == ModificationSelectionKey(stackBaseAddon),
		"selecting a patch must preserve add-ons that belong to the active base mod");
	Check(SelectCompatibleModification(stackCatalog, patchTransitionProfile, &stackAddon, message) &&
		patchTransitionProfile.addonSelections.size() == 2 &&
		SelectCompatibleModification(stackCatalog, patchTransitionProfile, &stackPatch, message) &&
		patchTransitionProfile.patchSelection.empty() &&
		patchTransitionProfile.addonSelections.size() == 1 &&
		patchTransitionProfile.addonSelections.front() == ModificationSelectionKey(stackBaseAddon),
		"disabling a patch must remove only add-ons that depend on that patch");

	InstalledModification stackAddonSecond = stackAddon;
	stackAddonSecond.id = "fixture:addon-second";
	stackAddonSecond.name = "Fixture Addon Second";
	stackAddonSecond.parentId.clear();
	stackAddonSecond.requirements.clear();
	stackAddonSecond.conflicts = {"fixture:patch"};
	stackAddonSecond.contentFingerprint = std::string(64, 'd');
	stackCatalog.modifications.push_back(stackAddonSecond);
	message.clear();
	Check(!ResolveModificationStack(stackCatalog, "generals", "fixture:mod@1.0",
		{"fixture:patch@1.0"}, {"fixture:addon-second@1.0"}, stack, message),
		"a conflicting add-on must be rejected before launch");
	stackCatalog.modifications.back().conflicts.clear();
	ModificationSelectionProfileV1 selectionProfile;
	selectionProfile.engine = "generals";
	message.clear();
	Check(SelectModification(selectionProfile, &stackMod, message) &&
		SelectModification(selectionProfile, &stackPatch, message) &&
		SelectModification(selectionProfile, &stackAddon, message) &&
		SelectModification(selectionProfile, &stackAddonSecond, message),
		"installed mod, patch, and ordered add-ons must be selectable independently");
	Check(BuildSelectedModificationStack(stackCatalog, selectionProfile, stack, message) &&
		stack.layers.size() == 4 && stack.layers[0]->id == stackMod.id &&
		stack.layers[1]->id == stackPatch.id && stack.layers[2]->id == stackAddon.id &&
		stack.layers[3]->id == stackAddonSecond.id,
		"saved selections must resolve in exact base, mod, patch, add-on order");
	Check(MoveSelectedAddon(selectionProfile, ModificationSelectionKey(stackAddonSecond), -1) &&
		selectionProfile.addonSelections.front() == ModificationSelectionKey(stackAddonSecond),
		"selected add-ons must support persistent user ordering");
	InstalledModification stackAddonUpdate = stackAddonSecond;
	stackAddonUpdate.version = "2.0";
	stackAddonUpdate.contentFingerprint = std::string(64, 'e');
	stackCatalog.modifications.push_back(stackAddonUpdate);
	const size_t addonCountBeforeUpdate = selectionProfile.addonSelections.size();
	Check(SelectModification(selectionProfile, &stackAddonUpdate, message) &&
		selectionProfile.addonSelections.size() == addonCountBeforeUpdate &&
		selectionProfile.addonSelections.front() == ModificationSelectionKey(stackAddonUpdate),
		"switching an add-on version must replace it in place without changing layer order");
	Check(MoveSelectedAddonTo(selectionProfile, ModificationSelectionKey(stackAddonUpdate), 1) &&
		selectionProfile.addonSelections.back() == ModificationSelectionKey(stackAddonUpdate),
		"drag reorder must move an active add-on directly to the requested layer index");
	message.clear();
	Check(SaveModificationSelectionProfile(testRoot / "ProfileMods", selectionProfile, message),
		"modification stack profile must publish atomically");
	message.clear();
	ModificationSelectionProfileV1 loadedSelection =
		LoadModificationSelectionProfile(testRoot / "ProfileMods", "GENERALS", message);
	Check(message.empty() && loadedSelection.modSelection == selectionProfile.modSelection &&
		loadedSelection.patchSelection == selectionProfile.patchSelection &&
		loadedSelection.addonSelections == selectionProfile.addonSelections,
		"modification stack profile must preserve all selections and add-on order");
	stackCatalog.modifications.erase(std::remove_if(stackCatalog.modifications.begin(), stackCatalog.modifications.end(),
		[&](const InstalledModification &entry) { return entry.id == stackPatch.id; }), stackCatalog.modifications.end());
	message.clear();
	Check(ReconcileModificationSelectionProfile(stackCatalog, loadedSelection, message) &&
		loadedSelection.patchSelection.empty() && loadedSelection.addonSelections.size() == 1 &&
		loadedSelection.addonSelections.front() == ModificationSelectionKey(stackAddonUpdate),
		"profile reconciliation must remove missing patches and only their dependent add-ons");
	selectionProfile.modSelection = "unsafe\nInjected=true";
	message.clear();
	Check(!SaveModificationSelectionProfile(testRoot / "ProfileMods", selectionProfile, message),
		"profile persistence must reject injected metadata values");

	// Echelon @test Codex 15/08/2026 Generate a Russian overlay from retail CSF data without redistributing it.
	const fs::path localizationRoot = testRoot / "LocalizationRoot";
	const fs::path localizationMods = localizationRoot / "Mods";
	const fs::path localizationSource = testRoot / "LocalizationPatch";
	fs::create_directories(localizationRoot / "GeneralsZH", error);
	fs::create_directories(localizationSource, error);
	std::string englishStrings;
	for (uint32_t index = 0; index < 1001; ++index) {
		englishStrings += "TEST:Label" + std::to_string(index) + "\r\n\"English text\"\r\nEND\r\n\r\n";
	}
	englishStrings += "TOOLTIP:InvalidGameVersion\r\n\r\nEND\r\n\r\n";
	Check(!error && CreateBigFixture(localizationSource / "Language.big", "Data\\Generals.str", englishStrings) &&
		CreateRussianCsfFixture(localizationRoot / "GeneralsZH" / "00RussianZH.big", 1001),
		"localization fixtures must contain valid BIG, STR, and CSF data");
	LocalImportRequest localizationSourceRequest;
	localizationSourceRequest.inputPath = localizationSource;
	localizationSourceRequest.modsRoot = localizationMods;
	localizationSourceRequest.engine = "zerohour";
	localizationSourceRequest.type = ModificationType::Patch;
	localizationSourceRequest.displayName = "Fixture Language Patch";
	localizationSourceRequest.version = "1.0";
	localizationSourceRequest.sourceId = "fixture";
	localizationSourceRequest.modificationId = "fixture:language-patch";
	const ModificationOperationResult localizationSourceResult = ImportLocalModification(localizationSourceRequest);
	Check(localizationSourceResult.success, "the localization source patch must install before generation");
	RepositoryModification localizationGenerator;
	localizationGenerator.id = "fixture:russian-bridge";
	localizationGenerator.sourceId = "fixture";
	localizationGenerator.engine = "zerohour";
	localizationGenerator.type = ModificationType::Addon;
	localizationGenerator.parentId = "fixture:language-patch";
	localizationGenerator.requirements = {"fixture:language-patch@1.0"};
	localizationGenerator.name = "Fixture Russian Bridge";
	localizationGenerator.version = "1.0";
	localizationGenerator.generator = "retail-russian-merge-v1";
	const ModificationOperationResult localizationResult =
		GenerateRetailRussianLocalization(localizationGenerator, localizationMods);
	std::ifstream generatedInput(localizationResult.installedRoot / "content/Data/Generals.str", std::ios::binary);
	const std::string generatedStrings((std::istreambuf_iterator<char>(generatedInput)),
		std::istreambuf_iterator<char>());
	Check(localizationResult.success && generatedStrings.find("Русский текст") != std::string::npos &&
		generatedStrings.find("English text") == std::string::npos &&
		generatedStrings.find("TOOLTIP:InvalidGameVersion\r\n\r\nEND") != std::string::npos,
		"retail Russian generation must translate matching labels and preserve valid empty STR entries");

	// Echelon @test Codex 15/08/2026 Allow an opt-in audit of the complete official ROTR file set before catalog publication.
	if (const char *rotrPath = std::getenv("ECHELON_QA_ROTR_PATH"); rotrPath && rotrPath[0]) {
		LocalImportRequest rotrRequest;
		rotrRequest.inputPath = fs::path(rotrPath);
		rotrRequest.modsRoot = testRoot / "RotrAuditMods";
		rotrRequest.engine = "zerohour";
		rotrRequest.type = ModificationType::Mod;
		rotrRequest.displayName = "Rise of the Reds";
		rotrRequest.version = "1.87-pb2.0";
		rotrRequest.sourceId = "arsenal";
		rotrRequest.modificationId = "arsenal:rise-of-the-reds";
		const ModificationOperationResult rotrResult = ImportLocalModification(rotrRequest);
		if (!rotrResult.success) std::fprintf(stderr, "ROTR CONTENT AUDIT ERROR: %s\n", rotrResult.message.c_str());
		Check(rotrResult.success &&
			fs::is_regular_file(rotrResult.installedRoot / "content/!!!Rotr_Intrnl_Main.gib") &&
			fs::is_regular_file(rotrResult.installedRoot / "content/!Rotr_W3D.gib"),
			"the complete official ROTR file set must pass installation and BIG validation");
	}

	fs::remove_all(testRoot, error);
	if (error) {
		std::fprintf(stderr, "WARNING: Cannot remove test directory: %s\n", error.message().c_str());
	}
	if (g_failures != 0) {
		std::fprintf(stderr, "FAILED: %d settings checks failed\n", g_failures);
		return 1;
	}
	std::puts("PASS: Echelon settings persistence");
	return 0;
}
