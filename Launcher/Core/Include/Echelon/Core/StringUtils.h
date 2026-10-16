// Echelon @feature Codex 16/10/2026 core StringUtils
/*
** ASCII string helpers shared by every launcher layer.
**
** Slugify mirrors EchelonLauncher::Slugify from Launcher/LauncherInstaller.cpp
** for ASCII input. The monolithic version routes every byte through
** std::isalnum/std::tolower, which under the default "C" locale collapses
** non-ASCII bytes (UTF-8 Cyrillic mod names) into separators and drops them;
** the core variant treats bytes >= 0x80 as letters so they pass through,
** folding Cyrillic uppercase to lowercase per the launcher rework plan.
*/

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace echelon::core
{

// Trims leading and trailing " \t\r\n" in place.
void Trim(std::string &value);

// Returns a copy with leading and trailing " \t\r\n" removed.
std::string TrimCopy(std::string_view value);

// Lowercases ASCII 'A'-'Z' in place; other bytes are left untouched.
void ToLower(std::string &value);

// Returns a copy with ASCII 'A'-'Z' lowercased.
std::string ToLowerCopy(std::string_view value);

// ASCII case-insensitive equality ('A'-'Z' vs 'a'-'z').
bool EqualsNoCase(std::string_view left, std::string_view right);

bool StartsWith(std::string_view value, std::string_view prefix);
bool EndsWith(std::string_view value, std::string_view suffix);

// Splits on every delimiter occurrence; empty fields are preserved
// ("a;;b" -> {"a", "", "b"}, "" -> {""}). Follows the same field
// semantics as splitting on whitespace-free delimiters in the monolithic
// launcher tooling.
std::vector<std::string> Split(std::string_view value, char delimiter);

// Concatenates fields with the delimiter placed between them.
std::string Join(const std::vector<std::string> &fields, std::string_view delimiter);

// Converts a free-form display name into a hyphen-separated lowercase slug:
// ASCII alnum is kept (lowercased), runs of any other separator collapse into
// a single '-', leading and trailing separators are dropped, non-ASCII bytes
// (e.g. UTF-8 Cyrillic) pass through as letters with Cyrillic uppercase
// folded to lowercase. An empty result becomes "local-modification".
std::string Slugify(std::string_view value);

} // namespace echelon::core
