// Echelon @feature Codex 16/10/2026 core FileUtilsTests
/*
** Contract tests for the core filesystem helpers.
**
** The suite is invoked from the shared echelon_core_tests main() because
** Launcher/Core/CMakeLists.txt globs every Tests/*.cpp into one binary.
*/

#include "Echelon/Core/FileUtils.h"
#include "Echelon/Core/OperationError.h"
#include "TestKit.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace echelon::core;

namespace
{

namespace fs = std::filesystem;

fs::path TestDir()
{
	return fs::temp_directory_path() / "echelon_core_fileutils_tests";
}

void ResetTestDir()
{
	std::error_code error;
	fs::remove_all(TestDir(), error);
	fs::create_directories(TestDir(), error);
}

void WritePlainFile(const fs::path &path, const std::string &contents)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << contents;
}

size_t CountTempLeftovers(const fs::path &directory, const std::string &fileName)
{
	size_t count = 0;
	std::error_code error;
	for (const auto &entry : fs::directory_iterator(directory, error)) {
		if (error) break;
		const std::string name = entry.path().filename().string();
		if (name.rfind(fileName + ".tmp-", 0) == 0) ++count;
	}
	return count;
}

void TestIsInsideRootLexicalCases()
{
	const fs::path root = TestDir() / "root";

	CHECK(IsInsideRoot(root, root / "a" / "b"));
	CHECK(IsInsideRoot(root, root));
	CHECK(!IsInsideRoot(root, root / ".." / "escape"));
	CHECK(!IsInsideRoot(root, fs::temp_directory_path() / "echelon_core_fileutils_elsewhere" / "x"));

	// Weak string-prefix sibling ("root-evil") must not match root.
	const fs::path evilSibling =
		root.parent_path() / (root.filename().string() + "-evil") / "child";
	CHECK(!IsInsideRoot(root, evilSibling));

	// Relative paths behave the same way.
	CHECK(IsInsideRoot("root", "root/a/b"));
	CHECK(!IsInsideRoot("root", "root/../escape"));
}

void TestReadFileMissingIsIoError()
{
	ResetTestDir();
	const Result<std::string> missing = ReadFile(TestDir() / "does-not-exist.bin");
	CHECK(!missing.HasValue());
	CHECK_EQ(missing.Error().stage, ErrorStage::IO);
	CHECK_EQ(missing.Error().code, ErrorCode::FilesystemFailure);
}

void TestWriteThenReadRoundTrip()
{
	ResetTestDir();
	const fs::path path = TestDir() / "roundtrip.bin";
	const std::string payload = std::string("line one\nline two\n") + '\0' + "raw byte";
	const Status written = WriteFile(path, payload);
	CHECK(written.HasValue());
	const Result<std::string> read = ReadFile(path);
	CHECK(read.HasValue());
	CHECK_EQ(read.Value(), payload);
}

void TestAtomicWritePublishesContent()
{
	ResetTestDir();
	const fs::path path = TestDir() / "payload.bin";
	const std::string payload = "atomic payload";
	const Status written = AtomicWriteFile(path, payload);
	CHECK(written.HasValue());
	CHECK_EQ(ReadFile(path).Value(), payload);
	// The temp file must be gone after the rename.
	CHECK_EQ(CountTempLeftovers(TestDir(), "payload.bin"), size_t{0});
}

void TestAtomicWriteReapsCrashedTemp()
{
	ResetTestDir();
	const fs::path path = TestDir() / "payload.bin";
	// Simulate the crash residue of a previous process: a leftover temp
	// with a pid that cannot be alive (far above any pid_max).
	WritePlainFile(TestDir() / "payload.bin.tmp-999999999", "crashed half write");
	CHECK_EQ(CountTempLeftovers(TestDir(), "payload.bin"), size_t{1});

	const Status written = AtomicWriteFile(path, "recovered");
	CHECK(written.HasValue());
	CHECK_EQ(ReadFile(path).Value(), std::string("recovered"));
	CHECK_EQ(CountTempLeftovers(TestDir(), "payload.bin"), size_t{0});
}

void TestAtomicWriteKeepsLiveForeignTemp()
{
	ResetTestDir();
	const fs::path path = TestDir() / "payload.bin";
#if !defined(_WIN32)
	// The parent process is alive but is not this process, so its temp must
	// survive garbage collection (a live writer may still own it).
	const std::string liveTemp =
		"payload.bin.tmp-" + std::to_string(static_cast<unsigned long long>(getppid()));
	WritePlainFile(TestDir() / liveTemp, "precious in-flight data");

	const Status written = AtomicWriteFile(path, "ours");
	CHECK(written.HasValue());
	CHECK_EQ(ReadFile(path).Value(), std::string("ours"));
	CHECK_EQ(CountTempLeftovers(TestDir(), "payload.bin"), size_t{1});
	CHECK_EQ(ReadFile(TestDir() / liveTemp).Value(), std::string("precious in-flight data"));
#else
	// Windows has no getppid(); the liveness guard is covered by the reaped
	// case above, so just verify a plain atomic write stays temp-free.
	const Status written = AtomicWriteFile(path, "ours");
	CHECK(written.HasValue());
	CHECK_EQ(CountTempLeftovers(TestDir(), "payload.bin"), size_t{0});
#endif
}

void TestSha256HexFileKnownVectors()
{
	ResetTestDir();
	const fs::path path = TestDir() / "abc.bin";
	WritePlainFile(path, "abc");

	const Result<std::string> digest = Sha256HexFile(path);
	CHECK(digest.HasValue());
	CHECK_EQ(digest.Value(),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	// Empty-file digest of SHA-256.
	WritePlainFile(path, "");
	const Result<std::string> emptyDigest = Sha256HexFile(path);
	CHECK(emptyDigest.HasValue());
	CHECK_EQ(emptyDigest.Value(),
		std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void TestSha256HexFileMissingIsIoError()
{
	ResetTestDir();
	const Result<std::string> missing = Sha256HexFile(TestDir() / "missing.bin");
	CHECK(!missing.HasValue());
	CHECK_EQ(missing.Error().stage, ErrorStage::IO);
}

void TestEnsureDirCreatesNestedDirectories()
{
	ResetTestDir();
	const fs::path nested = TestDir() / "one" / "two" / "three";
	CHECK(!fs::exists(nested));
	CHECK(EnsureDir(nested));
	CHECK(fs::is_directory(nested));
	CHECK(EnsureDir(nested));
}

} // namespace

void RunFileUtilsTests()
{
	TestIsInsideRootLexicalCases();
	TestReadFileMissingIsIoError();
	TestWriteThenReadRoundTrip();
	TestAtomicWritePublishesContent();
	TestAtomicWriteReapsCrashedTemp();
	TestAtomicWriteKeepsLiveForeignTemp();
	TestSha256HexFileKnownVectors();
	TestSha256HexFileMissingIsIoError();
	TestEnsureDirCreatesNestedDirectories();
}
