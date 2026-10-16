// Echelon @feature Codex 16/10/2026 core FsLockTests
/*
** Contract tests for the advisory directory lock.
**
** The suite is invoked from the shared echelon_core_tests main() because
** Launcher/Core/CMakeLists.txt globs every Tests/*.cpp into one binary.
*/

#include "Echelon/Core/FsLock.h"
#include "Echelon/Core/OperationError.h"
#include "TestKit.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace echelon::core;

namespace
{

namespace fs = std::filesystem;

fs::path TestDir()
{
	return fs::temp_directory_path() / "echelon_core_fslock_tests";
}

fs::path LockFile(const fs::path &directory)
{
	return directory / ".echelon-lock";
}

void ResetTestDir()
{
	std::error_code error;
	fs::remove_all(TestDir(), error);
	fs::create_directories(TestDir(), error);
}

void WriteLockFile(const fs::path &directory, const std::string &pid, const std::string &token)
{
	fs::create_directories(directory);
	std::ofstream output(LockFile(directory), std::ios::binary | std::ios::trunc);
	output << "pid=" << pid << "\n"
		<< "created=1\n"
		<< "token=" << token << "\n";
}

std::string ReadLockFile(const fs::path &directory)
{
	std::ifstream input(LockFile(directory), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

unsigned long long OwnPid()
{
#if defined(_WIN32)
	return static_cast<unsigned long long>(_getpid());
#else
	return static_cast<unsigned long long>(getpid());
#endif
}

// An alive pid that is not this process when possible; any alive pid
// exercises the same "owner alive" path.
unsigned long long ForeignAlivePid()
{
#if defined(_WIN32)
	return OwnPid();
#else
	return static_cast<unsigned long long>(getppid());
#endif
}

void TestAcquireHoldAndRelease()
{
	ResetTestDir();
	const fs::path directory = TestDir() / "basic";
	FsLock lock;
	OperationError error;
	CHECK(lock.Acquire(directory, error));
	CHECK(lock.IsHeld());
	CHECK(fs::exists(LockFile(directory)));
	CHECK_EQ(error.code, ErrorCode::None);

	lock.Release();
	CHECK(!lock.IsHeld());
	CHECK(!fs::exists(LockFile(directory)));

	// Releasing twice is a safe no-op.
	lock.Release();
	CHECK(!lock.IsHeld());
}

void TestEmptyPathIsInvalidInput()
{
	ResetTestDir();
	FsLock lock;
	OperationError error;
	CHECK(!lock.Acquire("", error));
	CHECK_EQ(error.stage, ErrorStage::Validation);
	CHECK_EQ(error.code, ErrorCode::InvalidInput);
	CHECK(!lock.IsHeld());
}

void TestSecondAcquireIsLockBusy()
{
	ResetTestDir();
	const fs::path directory = TestDir() / "busy";
	FsLock first;
	OperationError error;
	CHECK(first.Acquire(directory, error));

	FsLock second;
	OperationError busyError;
	CHECK(!second.Acquire(directory, busyError));
	CHECK(!second.IsHeld());
	CHECK_EQ(busyError.stage, ErrorStage::Discovery);
	CHECK_EQ(busyError.code, ErrorCode::LockBusy);

	first.Release();
	OperationError retryError;
	CHECK(second.Acquire(directory, retryError));
	CHECK(second.IsHeld());
	second.Release();
}

void TestStaleLockIsReaped()
{
	ResetTestDir();
	const fs::path directory = TestDir() / "stale";
	// A pid far above any pid_max cannot be alive.
	WriteLockFile(directory, "999999999", "dead-owner");
	CHECK(FsLock::IsStaleLock(directory));

	FsLock lock;
	OperationError error;
	CHECK(lock.Acquire(directory, error));
	CHECK(lock.IsHeld());
	const std::string record = ReadLockFile(directory);
	CHECK(record.find("token=dead-owner") == std::string::npos);
	CHECK(record.find("pid=" + std::to_string(OwnPid())) != std::string::npos);
	lock.Release();
	CHECK(!fs::exists(LockFile(directory)));
}

void TestAliveForeignLockIsNotReaped()
{
	ResetTestDir();
	const fs::path directory = TestDir() / "foreign";
	const std::string foreignRecord =
		"pid=" + std::to_string(ForeignAlivePid()) + "\ncreated=1\ntoken=foreign-owner\n";
	WriteLockFile(directory, std::to_string(ForeignAlivePid()), "foreign-owner");
	CHECK_EQ(ReadLockFile(directory), foreignRecord);
	CHECK(!FsLock::IsStaleLock(directory));

	FsLock lock;
	OperationError error;
	CHECK(!lock.Acquire(directory, error));
	CHECK(!lock.IsHeld());
	CHECK_EQ(error.code, ErrorCode::LockBusy);

	// The live owner's record must be left untouched.
	CHECK(fs::exists(LockFile(directory)));
	CHECK_EQ(ReadLockFile(directory), foreignRecord);
}

void TestReleaseKeepsReclaimedLock()
{
	ResetTestDir();
	const fs::path directory = TestDir() / "reclaimed";
	FsLock lock;
	OperationError error;
	CHECK(lock.Acquire(directory, error));

	// Simulate another process reaping and retaking the lock: the owner
	// token on disk no longer matches our instance.
	WriteLockFile(directory, std::to_string(ForeignAlivePid()), "foreign-owner");
	lock.Release();
	CHECK(!lock.IsHeld());
	CHECK(fs::exists(LockFile(directory)));
}

void TestIsStaleLockOnMissingDirectory()
{
	ResetTestDir();
	CHECK(!FsLock::IsStaleLock(TestDir() / "never-locked"));
}

} // namespace

void RunFsLockTests()
{
	TestAcquireHoldAndRelease();
	TestEmptyPathIsInvalidInput();
	TestSecondAcquireIsLockBusy();
	TestStaleLockIsReaped();
	TestAliveForeignLockIsNotReaped();
	TestReleaseKeepsReclaimedLock();
	TestIsStaleLockOnMissingDirectory();
}
