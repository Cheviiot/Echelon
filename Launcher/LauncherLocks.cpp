/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "LauncherLocks.h"

#include <chrono>
#include <cerrno>
#include <atomic>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

constexpr auto kStaleLockAge = std::chrono::hours(24);
std::atomic<uint64_t> s_lockSequence{0};

uint64_t CurrentProcessId()
{
#if defined(_WIN32)
	return static_cast<uint64_t>(_getpid());
#else
	return static_cast<uint64_t>(getpid());
#endif
}

std::string SafeLockComponent(const std::string &value)
{
	std::string result;
	result.reserve(value.size());
	for (const unsigned char character : value) {
		if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.') {
			result.push_back(static_cast<char>(character));
		} else {
			result.push_back('_');
		}
	}
	return result.empty() ? "default" : result;
}

bool OwnerProcessIsAlive(uint64_t processId)
{
	if (processId == 0) return false;
#if defined(_WIN32)
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
	if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
	DWORD exitCode = 0;
	const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
	CloseHandle(process);
	return alive;
#else
	if (kill(static_cast<pid_t>(processId), 0) == 0) return true;
	return errno == EPERM;
#endif
}

bool IsStale(const fs::path &path)
{
	std::error_code error;
	std::ifstream owner(path / "owner");
	std::string key;
	uint64_t processId = 0;
	while (owner >> key) {
		if (key.rfind("pid=", 0) == 0) processId = std::strtoull(key.c_str() + 4, nullptr, 10);
	}
	if (processId != 0 && !OwnerProcessIsAlive(processId)) return true;
	const fs::file_time_type modified = fs::last_write_time(path, error);
	if (error) return false;
	const fs::file_time_type now = fs::file_time_type::clock::now();
	return now > modified && now - modified > kStaleLockAge;
}

// Echelon @bugfix Codex 06/09/2026 Never report a lock as owned without a durable owner record.
std::string MakeOwnerToken()
{
	const uint64_t sequence = ++s_lockSequence;
	const uint64_t timestamp = static_cast<uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	return std::to_string(CurrentProcessId()) + "-" + std::to_string(timestamp) + "-" +
		std::to_string(sequence);
}

bool WriteOwner(const fs::path &path, const std::string &ownerToken)
{
	std::ofstream owner(path / "owner", std::ios::trunc);
	if (!owner) return false;
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	owner << "pid=" << CurrentProcessId() << "\ncreated=" << now << '\n';
	owner << "token=" << ownerToken << '\n';
	owner.flush();
	return static_cast<bool>(owner);
}

bool OwnerTokenMatches(const fs::path &path, const std::string &ownerToken)
{
	std::ifstream owner(path / "owner");
	std::string key;
	std::string token;
	while (owner >> key) {
		if (key.rfind("token=", 0) == 0) token = key.substr(6);
	}
	return !owner.bad() && !ownerToken.empty() && token == ownerToken;
}

} // namespace

ScopedDirectoryLock::ScopedDirectoryLock(ScopedDirectoryLock &&other) noexcept
	: m_path(std::move(other.m_path)), m_ownerToken(std::move(other.m_ownerToken)), m_acquired(other.m_acquired)
{
	other.m_acquired = false;
}

ScopedDirectoryLock &ScopedDirectoryLock::operator=(ScopedDirectoryLock &&other) noexcept
{
	if (this == &other) return *this;
	Release();
	m_path = std::move(other.m_path);
	m_ownerToken = std::move(other.m_ownerToken);
	m_acquired = other.m_acquired;
	other.m_acquired = false;
	return *this;
}

ScopedDirectoryLock::~ScopedDirectoryLock()
{
	Release();
}

ScopedDirectoryLock ScopedDirectoryLock::TryAcquire(const fs::path &lockPath, std::string &errorMessage)
{
	ScopedDirectoryLock result;
	errorMessage.clear();
	if (lockPath.empty()) {
		errorMessage = "Lock path is empty";
		return result;
	}

	std::error_code error;
	fs::create_directories(lockPath.parent_path(), error);
	if (error) {
		errorMessage = "Cannot create lock directory: " + error.message();
		return result;
	}
	if (!fs::create_directory(lockPath, error)) {
		if (error) {
			errorMessage = "Cannot create lock: " + error.message();
			return result;
		}
		if (IsStale(lockPath)) {
			std::error_code removeError;
			fs::remove_all(lockPath, removeError);
			if (!removeError && fs::create_directory(lockPath, error)) {
				const std::string ownerToken = MakeOwnerToken();
				if (!WriteOwner(lockPath, ownerToken)) {
					fs::remove_all(lockPath, removeError);
					errorMessage = "Cannot write lock owner: " + lockPath.string();
					return result;
				}
				result.m_path = lockPath;
				result.m_ownerToken = ownerToken;
				result.m_acquired = true;
				return result;
			}
		}
		errorMessage = "Another Echelon operation already owns lock: " + lockPath.string();
		return result;
	}
	const std::string ownerToken = MakeOwnerToken();
	if (!WriteOwner(lockPath, ownerToken)) {
		fs::remove_all(lockPath, error);
		errorMessage = "Cannot write lock owner: " + lockPath.string();
		return result;
	}
	result.m_path = lockPath;
	result.m_ownerToken = ownerToken;
	result.m_acquired = true;
	return result;
}

void ScopedDirectoryLock::Release()
{
	if (!m_acquired) return;
	std::error_code error;
	// Echelon @bugfix Codex 06/09/2026 Do not remove a lock reclaimed by another process.
	if (OwnerTokenMatches(m_path, m_ownerToken)) fs::remove_all(m_path, error);
	m_acquired = false;
	m_ownerToken.clear();
}

fs::path MakeLockPath(const fs::path &root, const std::string &scope, const std::string &key)
{
	return root / ".locks" / SafeLockComponent(scope) /
		(SafeLockComponent(key) + ".lock");
}

} // namespace EchelonLauncher
