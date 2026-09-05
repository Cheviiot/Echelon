/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherLocalization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace GeneralsArsenalLauncher
{
namespace
{

constexpr uint64_t kMaximumStringTableBytes = 64ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMaximumBigEntries = 1000000;
constexpr uint32_t kMaximumCsfLabels = 100000;

std::string Trim(const std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string ToLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string NormalizeBigPath(std::string value)
{
	std::replace(value.begin(), value.end(), '\\', '/');
	return ToLowerAscii(value);
}

uint32_t ReadBigEndian32(const unsigned char *bytes)
{
	return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
		(static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

uint32_t ReadLittleEndian32(const unsigned char *bytes)
{
	return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
		(static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool ReadBigEntry(const fs::path &archivePath, const std::string &wantedPath,
	std::vector<unsigned char> &contents, std::string &errorMessage)
{
	std::error_code fileError;
	const uint64_t archiveSize = fs::file_size(archivePath, fileError);
	if (fileError || archiveSize < 16) return false;
	std::ifstream input(archivePath, std::ios::binary);
	std::array<unsigned char, 16> header{};
	if (!input.read(reinterpret_cast<char *>(header.data()), header.size()) ||
		(std::string(reinterpret_cast<const char *>(header.data()), 4) != "BIGF" &&
			std::string(reinterpret_cast<const char *>(header.data()), 4) != "BIG4")) {
		return false;
	}
	const uint32_t entryCount = ReadBigEndian32(header.data() + 8);
	if (entryCount > kMaximumBigEntries) {
		errorMessage = "BIG archive has an unreasonable directory size: " + archivePath.string();
		return false;
	}
	const std::string normalizedWanted = NormalizeBigPath(wantedPath);
	for (uint32_t index = 0; index < entryCount; ++index) {
		std::array<unsigned char, 8> entryHeader{};
		if (!input.read(reinterpret_cast<char *>(entryHeader.data()), entryHeader.size())) {
			errorMessage = "BIG archive directory is truncated: " + archivePath.string();
			return false;
		}
		const uint64_t offset = ReadBigEndian32(entryHeader.data());
		const uint64_t size = ReadBigEndian32(entryHeader.data() + 4);
		std::string name;
		for (size_t length = 0; length < 4096; ++length) {
			char character = 0;
			if (!input.get(character)) {
				errorMessage = "BIG archive directory name is truncated: " + archivePath.string();
				return false;
			}
			if (character == '\0') break;
			name.push_back(character);
			if (length == 4095) {
				errorMessage = "BIG archive contains an oversized path: " + archivePath.string();
				return false;
			}
		}
		if (NormalizeBigPath(name) != normalizedWanted) continue;
		if (size > kMaximumStringTableBytes || offset > archiveSize || size > archiveSize - offset) {
			errorMessage = "BIG string table entry is outside its archive: " + archivePath.string();
			return false;
		}
		contents.resize(static_cast<size_t>(size));
		input.seekg(static_cast<std::streamoff>(offset));
		if (!input.read(reinterpret_cast<char *>(contents.data()), static_cast<std::streamsize>(size))) {
			errorMessage = "Cannot read BIG string table entry: " + archivePath.string();
			return false;
		}
		return true;
	}
	return false;
}

void AppendUtf8(std::string &output, uint32_t codePoint)
{
	if (codePoint <= 0x7f) {
		output.push_back(static_cast<char>(codePoint));
	} else if (codePoint <= 0x7ff) {
		output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	} else if (codePoint <= 0xffff) {
		output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	} else {
		output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	}
}

bool ParseRussianCsf(const std::vector<unsigned char> &bytes,
	std::unordered_map<std::string, std::string> &strings, std::string &errorMessage)
{
	if (bytes.size() < 24) {
		errorMessage = "Retail Russian CSF header is truncated";
		return false;
	}
	const uint32_t labelCount = ReadLittleEndian32(bytes.data() + 8);
	const uint32_t skip = ReadLittleEndian32(bytes.data() + 16);
	if (labelCount == 0 || labelCount > kMaximumCsfLabels || skip > bytes.size() - 24) {
		errorMessage = "Retail Russian CSF header is invalid";
		return false;
	}
	size_t position = 24 + skip;
	auto read32 = [&](uint32_t &value) {
		if (position > bytes.size() || bytes.size() - position < 4) return false;
		value = ReadLittleEndian32(bytes.data() + position);
		position += 4;
		return true;
	};
	for (uint32_t labelIndex = 0; labelIndex < labelCount; ++labelIndex) {
		uint32_t labelId = 0;
		uint32_t stringCount = 0;
		uint32_t labelLength = 0;
		if (!read32(labelId) || !read32(stringCount) || !read32(labelLength) ||
			labelLength > 4096 || position > bytes.size() || bytes.size() - position < labelLength) {
			errorMessage = "Retail Russian CSF label table is truncated";
			return false;
		}
		(void)labelId;
		std::string label(reinterpret_cast<const char *>(bytes.data() + position), labelLength);
		position += labelLength;
		for (uint32_t stringIndex = 0; stringIndex < stringCount; ++stringIndex) {
			uint32_t stringId = 0;
			uint32_t stringLength = 0;
			if (!read32(stringId) || !read32(stringLength) || stringLength > kMaximumStringTableBytes / 2 ||
				position > bytes.size() || bytes.size() - position < static_cast<uint64_t>(stringLength) * 2) {
				errorMessage = "Retail Russian CSF string table is truncated";
				return false;
			}
			std::string text;
			if (stringIndex == 0) {
				for (uint32_t characterIndex = 0; characterIndex < stringLength; ++characterIndex) {
					uint16_t value = static_cast<uint16_t>(bytes[position + characterIndex * 2] |
						(static_cast<uint16_t>(bytes[position + characterIndex * 2 + 1]) << 8));
					value = static_cast<uint16_t>(~value);
					if (value >= 0xd800 && value <= 0xdbff && characterIndex + 1 < stringLength) {
						uint16_t low = static_cast<uint16_t>(bytes[position + (characterIndex + 1) * 2] |
							(static_cast<uint16_t>(bytes[position + (characterIndex + 1) * 2 + 1]) << 8));
						low = static_cast<uint16_t>(~low);
						if (low >= 0xdc00 && low <= 0xdfff) {
							AppendUtf8(text, 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00));
							++characterIndex;
							continue;
						}
					}
					AppendUtf8(text, value);
				}
			}
			position += static_cast<size_t>(stringLength) * 2;
			// CSF string-with-wave chunks store a byte string after the translated text.
			if (stringId == 0x57525453) {
				uint32_t waveLength = 0;
				if (!read32(waveLength) || position > bytes.size() || bytes.size() - position < waveLength) {
					errorMessage = "Retail Russian CSF wave table is truncated";
					return false;
				}
				position += waveLength;
			}
			if (stringIndex == 0) strings[ToLowerAscii(label)] = std::move(text);
		}
	}
	return !strings.empty();
}

std::string EscapeStringText(const std::string &value)
{
	std::string escaped;
	escaped.reserve(value.size() + value.size() / 16);
	for (char character : value) {
		switch (character) {
			case '\\': escaped += "\\\\"; break;
			case '"': escaped += "\\\""; break;
			case '\n': escaped += "\\n"; break;
			case '\r': break;
			case '\t': escaped += "\\t"; break;
			default: escaped.push_back(character); break;
		}
	}
	return escaped;
}

size_t FindClosingQuote(const std::string &value, size_t begin)
{
	bool escaped = false;
	for (size_t position = begin; position < value.size(); ++position) {
		const char character = value[position];
		if (character == '"' && !escaped) return position;
		if (character == '\\') escaped = !escaped;
		else escaped = false;
	}
	return std::string::npos;
}

bool MergeStringTable(const std::vector<unsigned char> &sourceBytes,
	const std::unordered_map<std::string, std::string> &russianStrings, std::string &output,
	size_t &translatedCount, size_t &totalCount, std::string &errorMessage)
{
	std::string source(reinterpret_cast<const char *>(sourceBytes.data()), sourceBytes.size());
	if (source.find('\0') != std::string::npos) {
		errorMessage = "Source Generals.str contains binary data";
		return false;
	}
	std::string normalizedSource;
	normalizedSource.reserve(source.size());
	for (size_t position = 0; position < source.size(); ++position) {
		if (source[position] == '\r') {
			normalizedSource.push_back('\n');
			if (position + 1 < source.size() && source[position + 1] == '\n') ++position;
		} else {
			normalizedSource.push_back(source[position]);
		}
	}
	std::istringstream input(normalizedSource);
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(input, line)) lines.push_back(line);
	output = "// Generated locally by Generals: Arsenal from user-owned retail Russian resources.\r\n\r\n";
	for (size_t index = 0; index < lines.size();) {
		const std::string label = Trim(lines[index++]);
		if (label.empty() || label.rfind("//", 0) == 0) continue;
		std::string section;
		bool foundEnd = false;
		while (index < lines.size()) {
			const std::string current = Trim(lines[index++]);
			if (ToLowerAscii(current) == "end") {
				foundEnd = true;
				break;
			}
			if (!section.empty()) section.push_back('\n');
			section += current;
		}
		if (!foundEnd) {
			errorMessage = "Source Generals.str ends before END for label " + label;
			return false;
		}
		const auto translated = russianStrings.find(ToLowerAscii(label));
		const size_t openingQuote = section.find('"');
		const size_t closingQuote = openingQuote == std::string::npos ? std::string::npos :
			FindClosingQuote(section, openingQuote + 1);
		if (openingQuote == std::string::npos || closingQuote == std::string::npos) {
			// GeneralsArsenal @bugfix Codex 15/08/2026 Preserve intentionally empty STR entries used by HanPatch.
			if (!Trim(section).empty()) {
				errorMessage = "Source Generals.str has an invalid quoted value for label " + label;
				return false;
			}
			++totalCount;
			output += label + "\r\n";
			if (translated != russianStrings.end()) {
				output += "\"" + EscapeStringText(translated->second) + "\"\r\n";
				++translatedCount;
			} else {
				output += "\r\n";
			}
			output += "END\r\n\r\n";
			continue;
		}
		std::string encodedText = section.substr(openingQuote + 1, closingQuote - openingQuote - 1);
		std::replace(encodedText.begin(), encodedText.end(), '\n', ' ');
		if (translated != russianStrings.end()) {
			encodedText = EscapeStringText(translated->second);
			++translatedCount;
		}
		++totalCount;
		output += label + "\r\n\"" + encodedText + "\"" + section.substr(closingQuote + 1) + "\r\nEND\r\n\r\n";
	}
	if (totalCount == 0 || translatedCount < 1000) {
		errorMessage = "Retail Russian localization does not match the installed modification string table";
		return false;
	}
	return true;
}

bool FindRussianRetailArchive(const fs::path &assetRoot, fs::path &archivePath)
{
	static const std::array<std::string, 4> names = {
		"00russianzh.big", "00russiazh.big", "00russian.big", "00russia.big"
	};
	std::error_code error;
	for (const fs::directory_entry &entry : fs::directory_iterator(assetRoot, error)) {
		if (error) return false;
		if (!entry.is_regular_file(error) || error) continue;
		const std::string name = ToLowerAscii(entry.path().filename().string());
		if (std::find(names.begin(), names.end(), name) != names.end()) {
			archivePath = entry.path();
			return true;
		}
	}
	return false;
}

bool FindInstalledStringTable(const InstalledModification &source, std::vector<unsigned char> &contents,
	std::string &errorMessage)
{
	std::vector<fs::path> archives;
	std::error_code error;
	for (fs::recursive_directory_iterator iterator(source.launchPath,
		fs::directory_options::skip_permission_denied, error), end; iterator != end && !error; iterator.increment(error)) {
		if (iterator.depth() > 8) iterator.disable_recursion_pending();
		if (!iterator->is_regular_file(error) || error) continue;
		const std::string extension = ToLowerAscii(iterator->path().extension().string());
		if (extension == ".big" || extension == ".gib") archives.push_back(iterator->path());
		if (archives.size() > 4096) {
			errorMessage = "Installed modification contains too many archives";
			return false;
		}
	}
	if (error) {
		errorMessage = "Cannot inspect installed modification: " + error.message();
		return false;
	}
	std::sort(archives.begin(), archives.end(), [](const fs::path &left, const fs::path &right) {
		return ToLowerAscii(left.generic_string()) < ToLowerAscii(right.generic_string());
	});
	for (const fs::path &archive : archives) {
		std::string archiveError;
		if (ReadBigEntry(archive, "Data/Generals.str", contents, archiveError)) return true;
		if (!archiveError.empty()) errorMessage = archiveError;
	}
	if (errorMessage.empty()) errorMessage = "Required installed modification does not provide Data/Generals.str";
	return false;
}

const InstalledModification *FindLocalizationSource(const RepositoryModification &modification,
	const ModificationCatalog &catalog)
{
	for (const std::string &requirement : modification.requirements) {
		for (const InstalledModification &installed : catalog.modifications) {
			if (installed.engine == modification.engine && ModificationSelectorMatches(requirement, installed)) {
				return &installed;
			}
		}
	}
	return nullptr;
}

} // namespace

// GeneralsArsenal @feature Codex 15/08/2026 Build a redistribution-safe Russian overlay from the user's retail CSF and installed mod strings.
ModificationOperationResult GenerateRetailRussianLocalization(
	const RepositoryModification &modification, const fs::path &modsRoot,
	ModificationOperationProgress *progress)
{
	ModificationOperationResult result;
	if (modification.engine != "zerohour" || modification.type != ModificationType::Addon) {
		result.message = "Retail Russian localization generator only supports Zero Hour add-ons";
		return result;
	}
	const ModificationCatalog catalog = LoadModificationCatalog(modsRoot);
	const InstalledModification *source = FindLocalizationSource(modification, catalog);
	if (!source) {
		result.message = "Install the required mod or patch before generating its Russian localization";
		return result;
	}
	std::vector<unsigned char> sourceStringTable;
	if (!FindInstalledStringTable(*source, sourceStringTable, result.message)) return result;
	fs::path russianArchive;
	if (!FindRussianRetailArchive(modsRoot.parent_path() / "GeneralsZH", russianArchive)) {
		result.message = "Retail Russian localization was not found (00RussianZH.big or 00RussiaZH.big)";
		return result;
	}
	std::vector<unsigned char> csfBytes;
	if (!ReadBigEntry(russianArchive, "Data/English/generals.csf", csfBytes, result.message)) {
		if (result.message.empty()) result.message = "Retail Russian archive does not contain Data/English/generals.csf";
		return result;
	}
	std::unordered_map<std::string, std::string> russianStrings;
	if (!ParseRussianCsf(csfBytes, russianStrings, result.message)) return result;
	std::string mergedTable;
	size_t translatedCount = 0;
	size_t totalCount = 0;
	if (!MergeStringTable(sourceStringTable, russianStrings, mergedTable,
		translatedCount, totalCount, result.message)) return result;
	if (progress && progress->cancelRequested.load(std::memory_order_relaxed)) {
		result.cancelled = true;
		result.message = "Localization generation was cancelled";
		return result;
	}
	const fs::path stagingRoot = modsRoot / ".staging" / "generated" /
		"retail-russian-merge-v1" / modification.version;
	const fs::path contentRoot = stagingRoot / "content";
	std::error_code error;
	fs::remove_all(stagingRoot, error);
	error.clear();
	fs::create_directories(contentRoot / "Data", error);
	if (error) {
		result.message = "Cannot create localization staging directory: " + error.message();
		return result;
	}
	const fs::path temporary = contentRoot / "Data" / "Generals.str.writing";
	const fs::path published = contentRoot / "Data" / "Generals.str";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output.write(mergedTable.data(), static_cast<std::streamsize>(mergedTable.size()))) {
		result.message = "Cannot write generated Russian string table";
		return result;
	}
	output.close();
	fs::rename(temporary, published, error);
	if (error) {
		result.message = "Cannot publish generated Russian string table: " + error.message();
		return result;
	}
	if (progress) {
		progress->totalBytes.store(mergedTable.size(), std::memory_order_relaxed);
		progress->completedBytes.store(mergedTable.size(), std::memory_order_relaxed);
	}
	LocalImportRequest request;
	request.inputPath = contentRoot;
	request.modsRoot = modsRoot;
	request.engine = modification.engine;
	request.type = modification.type;
	request.parentId = modification.parentId;
	request.requirements = modification.requirements;
	request.conflicts = modification.conflicts;
	request.displayName = modification.name;
	request.version = modification.version;
	request.sourceId = modification.sourceId;
	request.modificationId = modification.id;
	request.source = "generated:retail-russian-merge-v1";
	request.coverImagePath = modification.coverCachePath;
	result = ImportLocalModification(request, progress);
	if (result.success) {
		result.message = "Russian localization generated: " + std::to_string(translatedCount) +
			" of " + std::to_string(totalCount) + " strings translated; remaining mod strings use English fallback";
		fs::remove_all(stagingRoot, error);
	}
	return result;
}

} // namespace GeneralsArsenalLauncher
