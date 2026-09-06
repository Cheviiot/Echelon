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

#include <filesystem>
#include <string>

namespace EchelonLauncher
{

// Echelon @feature Codex 06/09/2026 Serialize launcher operations with an atomic directory lock.
class ScopedDirectoryLock
{
public:
	ScopedDirectoryLock() = default;
	ScopedDirectoryLock(const ScopedDirectoryLock &) = delete;
	ScopedDirectoryLock &operator=(const ScopedDirectoryLock &) = delete;
	ScopedDirectoryLock(ScopedDirectoryLock &&other) noexcept;
	ScopedDirectoryLock &operator=(ScopedDirectoryLock &&other) noexcept;
	~ScopedDirectoryLock();

	static ScopedDirectoryLock TryAcquire(const std::filesystem::path &lockPath,
		std::string &errorMessage);

	bool acquired() const { return m_acquired; }
	const std::filesystem::path &path() const { return m_path; }

private:
	std::filesystem::path m_path;
	std::string m_ownerToken;
	bool m_acquired = false;

	void Release();
};

std::filesystem::path MakeLockPath(const std::filesystem::path &root, const std::string &scope,
	const std::string &key);

} // namespace EchelonLauncher
