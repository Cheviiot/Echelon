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

#include <cstdlib>
#include <cstring>

namespace EchelonArchivePolicy
{

// Echelon @bugfix Codex 05/09/2026 Share the native engine's BIG path capacity with import validation; exclude the terminator.
constexpr size_t kMaximumEntryPathBytes = 1023;

inline char LowerAscii(char character)
{
	return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

inline const char *BaseName(const char *path)
{
	const char *name = path ? path : "";
	for (const char *cursor = name; *cursor != '\0'; ++cursor) {
		if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
	}
	return name;
}

inline bool EqualsTokenIgnoreCase(const char *value, const char *token, size_t tokenLength)
{
	if (::strlen(value) != tokenLength) return false;
	for (size_t index = 0; index < tokenLength; ++index) {
		if (LowerAscii(value[index]) != LowerAscii(token[index])) return false;
	}
	return true;
}

// Echelon @feature Codex 13/08/2026 Skip only exact archive basenames selected by the trusted launcher host.
inline bool IsArchiveDisabled(const char *path)
{
#if defined(ECHELON_ENGINE_HOSTED)
	const char *policy = ::getenv("ECHELON_DISABLED_BIG_FILES");
	if (!policy || !policy[0]) return false;
	const char *name = BaseName(path);
	const char *cursor = policy;
	while (*cursor != '\0') {
		while (*cursor == ';' || *cursor == ' ' || *cursor == '\t') ++cursor;
		const char *begin = cursor;
		while (*cursor != '\0' && *cursor != ';') ++cursor;
		const char *end = cursor;
		while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
		if (end > begin && EqualsTokenIgnoreCase(name, begin, static_cast<size_t>(end - begin))) return true;
	}
#else
	(void)path;
#endif
	return false;
}

} // namespace EchelonArchivePolicy
