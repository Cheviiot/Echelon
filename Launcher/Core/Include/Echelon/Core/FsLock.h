// Echelon @feature Codex 16/10/2026 core FsLock
/*
** Advisory directory lock serializing launcher operations.
**
** Behavior is carried over line-by-line from Launcher/LauncherLocks.cpp
** (ScopedDirectoryLock): the owner record is a "pid=<pid>\ncreated=<unix>
** \ntoken=<pid>-<steady>-<seq>" payload, stale detection asks whether the
** recorded pid is still alive (kill(pid, 0); EPERM counts as alive, and
** OpenProcess/ExitCodeProcess is used on Windows) with a 24-hour mtime
** fallback, stale locks are reaped and retried once, and Release never
** removes a lock whose owner token was reclaimed by someone else.
**
** Two deliberate differences against the monolithic canon:
** - Representation: instead of a lock directory with an "owner" file
**   inside (mkdir as the atomic primitive), the core lock is a single
**   ".echelon-lock" file in the guarded directory, created with O_EXCL
**   semantics (CreateFile(CREATE_NEW) on Windows). There is no
**   scope/key hierarchy in the core layer, so MakeLockPath and
**   SafeLockComponent stay behind in the monolith.
** - A failure to re-create a lock after reaping a stale one is reported
**   as ErrorCode::FilesystemFailure instead of being masked as LockBusy.
*/

#pragma once

#include "Echelon/Core/OperationError.h"

#include <filesystem>
#include <string>

namespace echelon::core
{

// Advisory lock over a directory, backed by "<directory>/.echelon-lock".
// Not thread-safe; one instance guards one directory at a time.
class FsLock
{
public:
	FsLock() = default;
	FsLock(const FsLock &) = delete;
	FsLock &operator=(const FsLock &) = delete;

	// Creates the directory when missing, then takes the lock exclusively.
	// Returns true when this instance holds the lock. On false, error is
	// ErrorCode::LockBusy when another live operation owns the directory,
	// ErrorCode::InvalidInput for an empty (or already held) target, and
	// ErrorCode::FilesystemFailure for IO failures. Stale locks whose
	// owner pid is dead are reaped and retried once, mirroring the canon.
	bool Acquire(const std::filesystem::path &directory, OperationError &error);

	bool IsHeld() const { return m_held; }

	// Drops the lock, but only while the owner token on disk still matches
	// this instance, so a lock reclaimed by another process is not removed.
	// Safe to call when not held.
	void Release();

	// True when "<directory>/.echelon-lock" exists and its recorded owner
	// pid is dead, or the lock file is older than 24 hours.
	static bool IsStaleLock(const std::filesystem::path &directory);

private:
	static std::filesystem::path LockFileFor(const std::filesystem::path &directory);

	std::filesystem::path m_lockFile;
	std::string m_ownerToken;
	bool m_held = false;
};

} // namespace echelon::core
