// Echelon @feature Codex 16/10/2026 core Logger
/*
** stderr logger implementation.
*/

#include "Echelon/Core/Logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace echelon::core
{

void Log(LogLevel, std::string_view component, const char *format, ...)
{
	char message[2048];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);

	const std::time_t now = std::time(nullptr);
	std::tm localTime{};
#if defined(_WIN32)
	localtime_s(&localTime, &now);
#else
	localtime_r(&now, &localTime);
#endif
	char timestamp[16]{};
	std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &localTime);

	static std::mutex outputMutex;
	const std::lock_guard<std::mutex> lock(outputMutex);
	std::fprintf(stderr, "[%s][%.*s] %s\n", timestamp, static_cast<int>(component.size()),
		component.data(), message);
	std::fflush(stderr);
}

} // namespace echelon::core
