// Echelon @feature Codex 16/10/2026 core IniDocument
/*
** Ordered, section-based INI document model.
**
** Parsing semantics mirror the monolithic launcher's settings reader in
** Launcher/LauncherSettings.cpp: ';' and '#' start comments, blank lines are
** skipped, key/value pairs tolerate whitespace around '=', and values keep
** their original spelling (quotes are not stripped). Keys are matched
** case-insensitively and are stored in canonical lowercase form; section
** names are matched case-insensitively too, while their first-seen spelling
** is kept for saving. Duplicate keys keep the first occurrence, and lines
** without '=' are skipped with a WARN log instead of failing the load.
**
** Unlike the line-preserving serializers in LauncherSettings.cpp this class
** models sections and keys, so comments and blank lines do not survive a
** save. Sections and keys keep their insertion order.
*/

#pragma once

#include "Echelon/Core/OperationError.h"

#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace echelon::core
{

class IniDocument
{
public:
	// Parses the file into the document, replacing any previous content.
	// Returns false with error.stage == ErrorStage::IO when the file cannot be
	// opened or read to completion. Malformed lines are skipped with a WARN log.
	bool Load(const std::filesystem::path &path, OperationError &error);

	// Serializes the document, creating missing parent directories. Sections
	// and keys are written in insertion order. Returns false with
	// error.stage == ErrorStage::IO on any filesystem or stream failure.
	bool Save(const std::filesystem::path &path, OperationError &error) const;

	// Value for the key, or fallback when the section or key is missing.
	std::string Get(const std::string &section, const std::string &key,
		const std::string &fallback = "") const;

	// Inserts or updates the key, creating the section in insertion order
	// when it does not exist yet.
	void Set(const std::string &section, const std::string &key, const std::string &value);

	bool Has(const std::string &section, const std::string &key) const;

	// Section names (first-seen spelling) in insertion order.
	std::vector<std::string> Sections() const;

	// Canonical (lowercase) key names of the section in insertion order;
	// empty when the section does not exist.
	std::vector<std::string> Keys(const std::string &section) const;

	// Removes the key. The section itself is kept even when it becomes empty.
	// Returns false when the section or key does not exist.
	bool Erase(const std::string &section, const std::string &key);

private:
	struct KeyEntry
	{
		std::string key; // canonical lowercase
		std::string value; // original spelling
	};

	struct SectionEntry
	{
		std::string displayName; // first-seen spelling
		std::string canonicalName; // lowercase, used for matching
		std::deque<KeyEntry> keys; // insertion order
	};

	// Launcher-scale documents are tiny, so linear scans keep the model
	// simple and preserve insertion order without index bookkeeping.
	const SectionEntry *FindSection(const std::string &section) const;
	SectionEntry *FindSection(const std::string &section);

	std::deque<SectionEntry> m_sections; // insertion order
};

} // namespace echelon::core
