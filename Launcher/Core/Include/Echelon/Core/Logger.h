// Echelon @feature Codex 16/10/2026 core Logger
/*
** Console diagnostics that stay available in release builds.
**
** DEBUG_LOG is compiled out outside debug builds, so the core layer routes
** its diagnostics through fprintf(stderr, ...) + fflush (see AGENTS.md).
** Output line format: "[HH:MM:SS][component] message".
*/

#pragma once

#include <string_view>

namespace echelon::core
{

enum class LogLevel
{
	Info,
	Warning,
	Error
};

// Formats the message printf-style and writes
// "[HH:MM:SS][component] <message>\n" to stderr, then flushes. Output longer
// than 2047 bytes is truncated. The component argument is expected to be a
// string literal naming the emitting layer (e.g. "ini", "content").
void Log(LogLevel level, std::string_view component, const char *format, ...);

} // namespace echelon::core

#define ECHELON_LOG_INFO(component, format, ...) \
	::echelon::core::Log(::echelon::core::LogLevel::Info, component, format, ##__VA_ARGS__)

#define ECHELON_LOG_WARN(component, format, ...) \
	::echelon::core::Log(::echelon::core::LogLevel::Warning, component, format, ##__VA_ARGS__)

#define ECHELON_LOG_ERROR(component, format, ...) \
	::echelon::core::Log(::echelon::core::LogLevel::Error, component, format, ##__VA_ARGS__)
