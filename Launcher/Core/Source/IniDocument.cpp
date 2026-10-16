// Echelon @feature Codex 16/10/2026 core IniDocument
/*
** Implementation of the ordered INI document model.
*/

#include "Echelon/Core/IniDocument.h"

#include "Echelon/Core/Logger.h"
#include "Echelon/Core/StringUtils.h"

#include <fstream>
#include <system_error>

namespace echelon::core
{

bool IniDocument::Load(const std::filesystem::path &path, OperationError &error)
{
	error = OperationError{};
	*this = IniDocument{};

	std::ifstream input(path);
	if (!input) {
		error.stage = ErrorStage::IO;
		error.code = ErrorCode::FilesystemFailure;
		error.detail = "Cannot open INI file: " + path.string();
		return false;
	}

	std::string currentSection;
	std::string line;
	std::size_t lineNumber = 0;
	while (std::getline(input, line)) {
		++lineNumber;
		const std::string trimmed = TrimCopy(line);
		if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
			continue;
		}
		if (trimmed.front() == '[' && trimmed.back() == ']') {
			// Create eagerly so empty sections survive a round-trip.
			currentSection = TrimCopy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
			SectionEntry *entry = FindSection(currentSection);
			if (!entry) {
				m_sections.push_back(SectionEntry{currentSection, ToLowerCopy(currentSection), {}});
			}
			continue;
		}
		const size_t equals = trimmed.find('=');
		if (equals == std::string::npos) {
			ECHELON_LOG_WARN("ini", "Ignoring malformed line %zu in %s: %s", lineNumber,
				path.string().c_str(), trimmed.c_str());
			continue;
		}
		const std::string key = ToLowerCopy(TrimCopy(std::string_view(trimmed).substr(0, equals)));
		const std::string value = TrimCopy(std::string_view(trimmed).substr(equals + 1));
		if (key.empty()) {
			ECHELON_LOG_WARN("ini", "Ignoring keyless assignment on line %zu in %s", lineNumber,
				path.string().c_str());
			continue;
		}
		// First occurrence wins, matching ToValueMap() in LauncherSettings.cpp.
		if (!Has(currentSection, key)) {
			Set(currentSection, key, value);
		}
	}
	if (input.bad()) {
		error.stage = ErrorStage::IO;
		error.code = ErrorCode::FilesystemFailure;
		error.detail = "Cannot finish reading INI file: " + path.string();
		return false;
	}
	return true;
}

bool IniDocument::Save(const std::filesystem::path &path, OperationError &error) const
{
	error = OperationError{};

	if (!path.parent_path().empty()) {
		std::error_code fsError;
		std::filesystem::create_directories(path.parent_path(), fsError);
		if (fsError) {
			error.stage = ErrorStage::IO;
			error.code = ErrorCode::FilesystemFailure;
			error.detail = "Cannot create INI directory: " + fsError.message();
			return false;
		}
	}

	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		error.stage = ErrorStage::IO;
		error.code = ErrorCode::FilesystemFailure;
		error.detail = "Cannot open INI file for writing: " + path.string();
		return false;
	}
	bool firstSection = true;
	for (const SectionEntry &section : m_sections) {
		if (section.displayName.empty() && section.keys.empty()) {
			continue;
		}
		if (!firstSection) {
			output.put('\n');
		}
		firstSection = false;
		if (!section.displayName.empty()) {
			output << '[' << section.displayName << "]\n";
		}
		for (const KeyEntry &key : section.keys) {
			output << key.key << '=' << key.value << '\n';
		}
	}
	output.flush();
	if (!output) {
		error.stage = ErrorStage::IO;
		error.code = ErrorCode::FilesystemFailure;
		error.detail = "Cannot write INI file: " + path.string();
		return false;
	}
	return true;
}

std::string IniDocument::Get(const std::string &section, const std::string &key,
	const std::string &fallback) const
{
	const SectionEntry *entry = FindSection(section);
	if (!entry) {
		return fallback;
	}
	const std::string canonicalKey = ToLowerCopy(key);
	for (const KeyEntry &candidate : entry->keys) {
		if (candidate.key == canonicalKey) {
			return candidate.value;
		}
	}
	return fallback;
}

void IniDocument::Set(const std::string &section, const std::string &key, const std::string &value)
{
	const std::string canonicalKey = ToLowerCopy(key);
	SectionEntry *entry = FindSection(section);
	if (!entry) {
		m_sections.push_back(SectionEntry{section, ToLowerCopy(section), {}});
		entry = &m_sections.back();
	}
	for (KeyEntry &candidate : entry->keys) {
		if (candidate.key == canonicalKey) {
			candidate.value = value;
			return;
		}
	}
	entry->keys.push_back(KeyEntry{canonicalKey, value});
}

bool IniDocument::Has(const std::string &section, const std::string &key) const
{
	const SectionEntry *entry = FindSection(section);
	if (!entry) {
		return false;
	}
	const std::string canonicalKey = ToLowerCopy(key);
	for (const KeyEntry &candidate : entry->keys) {
		if (candidate.key == canonicalKey) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> IniDocument::Sections() const
{
	std::vector<std::string> names;
	names.reserve(m_sections.size());
	for (const SectionEntry &section : m_sections) {
		names.push_back(section.displayName);
	}
	return names;
}

std::vector<std::string> IniDocument::Keys(const std::string &section) const
{
	std::vector<std::string> names;
	const SectionEntry *entry = FindSection(section);
	if (!entry) {
		return names;
	}
	names.reserve(entry->keys.size());
	for (const KeyEntry &key : entry->keys) {
		names.push_back(key.key);
	}
	return names;
}

bool IniDocument::Erase(const std::string &section, const std::string &key)
{
	SectionEntry *entry = FindSection(section);
	if (!entry) {
		return false;
	}
	const std::string canonicalKey = ToLowerCopy(key);
	for (auto candidate = entry->keys.begin(); candidate != entry->keys.end(); ++candidate) {
		if (candidate->key == canonicalKey) {
			entry->keys.erase(candidate);
			return true;
		}
	}
	return false;
}

const IniDocument::SectionEntry *IniDocument::FindSection(const std::string &section) const
{
	const std::string canonical = ToLowerCopy(section);
	for (const SectionEntry &candidate : m_sections) {
		if (candidate.canonicalName == canonical) {
			return &candidate;
		}
	}
	return nullptr;
}

IniDocument::SectionEntry *IniDocument::FindSection(const std::string &section)
{
	const std::string canonical = ToLowerCopy(section);
	for (SectionEntry &candidate : m_sections) {
		if (candidate.canonicalName == canonical) {
			return &candidate;
		}
	}
	return nullptr;
}

} // namespace echelon::core
