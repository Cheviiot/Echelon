// Echelon @feature Codex 16/10/2026 core IniDocumentTests
/*
** Contract tests for the ordered INI document model.
**
** The suite is invoked from the shared echelon_core_tests main() because
** Launcher/Core/CMakeLists.txt globs every Tests/*.cpp into one binary.
*/

#include "Echelon/Core/IniDocument.h"
#include "Echelon/Core/OperationError.h"
#include "TestKit.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace echelon::core;

namespace
{

namespace fs = std::filesystem;

fs::path TestDir()
{
	return fs::temp_directory_path() / "echelon_core_inidocument_tests";
}

void ResetTestDir()
{
	std::error_code error;
	fs::remove_all(TestDir(), error);
	fs::create_directories(TestDir(), error);
}

void WriteFile(const fs::path &path, const std::string &contents)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << contents;
}

void TestParseSectionsCommentsAndSpacing()
{
	ResetTestDir();
	const fs::path path = TestDir() / "parse.ini";
	WriteFile(path,
		"; leading comment\n"
		"# hash comment\n"
		"\n"
		"[Launcher]\n"
		"SchemaVersion = 3\n"
		"Language = english\n"
		"\n"
		"[Generals]\n"
		"Windowed = yes\n"
		"Extra Args = -win -quickstart\n"
		"malformed line without equals\n"
		"UnknownFutureKey = keepme\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(path, error));
	CHECK_EQ(error.stage, ErrorStage::None);

	const std::vector<std::string> sections = document.Sections();
	CHECK_EQ(sections.size(), size_t{2});
	CHECK_EQ(sections[0], std::string("Launcher"));
	CHECK_EQ(sections[1], std::string("Generals"));

	CHECK_EQ(document.Get("Launcher", "SchemaVersion"), std::string("3"));
	CHECK_EQ(document.Get("Launcher", "Language"), std::string("english"));
	CHECK_EQ(document.Get("Generals", "Windowed"), std::string("yes"));
	CHECK_EQ(document.Get("Generals", "Extra Args"), std::string("-win -quickstart"));
	CHECK_EQ(document.Get("Generals", "Missing"), std::string(""));
	CHECK_EQ(document.Get("Generals", "Missing", "fallback"), std::string("fallback"));
	CHECK_EQ(document.Get("NoSuchSection", "Key", "fallback"), std::string("fallback"));

	// The malformed line is skipped; known keys keep their insertion order.
	const std::vector<std::string> keys = document.Keys("Generals");
	CHECK_EQ(keys.size(), size_t{3});
	CHECK_EQ(keys[0], std::string("windowed"));
	CHECK_EQ(keys[1], std::string("extra args"));
	CHECK_EQ(keys[2], std::string("unknownfuturekey"));
}

void TestKeyCaseInsensitivity()
{
	ResetTestDir();
	const fs::path path = TestDir() / "case.ini";
	WriteFile(path, "[Profile]\nMiXeD = first\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(path, error));

	CHECK(document.Has("Profile", "mixed"));
	CHECK(document.Has("PROFILE", "MIXED"));
	CHECK_EQ(document.Get("profile", "MIXED"), std::string("first"));

	// Setting with another case updates the same key, not a duplicate.
	document.Set("profile", "Mixed", "second");
	CHECK_EQ(document.Get("Profile", "mixed"), std::string("second"));
	const std::vector<std::string> keys = document.Keys("Profile");
	CHECK_EQ(keys.size(), size_t{1});
}

void TestSectionCaseInsensitiveFirstSeenSpelling()
{
	ResetTestDir();
	const fs::path path = TestDir() / "sections.ini";
	WriteFile(path, "[Launcher]\nA = 1\n[LAUNCHER]\nB = 2\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(path, error));

	// Same section merged despite header casing; first spelling wins.
	const std::vector<std::string> sections = document.Sections();
	CHECK_EQ(sections.size(), size_t{1});
	CHECK_EQ(sections[0], std::string("Launcher"));
	CHECK_EQ(document.Get("launcher", "a"), std::string("1"));
	CHECK_EQ(document.Get("LAUNCHER", "b"), std::string("2"));
}

void TestRoundTripPreservesOrderAndUnknownKeys()
{
	ResetTestDir();
	const fs::path source = TestDir() / "roundtrip-src.ini";
	const fs::path saved = TestDir() / "nested" / "roundtrip-out.ini";
	WriteFile(source,
		"[First]\n"
		"One = 1\n"
		"UnknownKey = survives\n"
		"[Second]\n"
		"Two = 2\n"
		"[Third]\n"
		"Three = 3\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(source, error));
	CHECK(document.Save(saved, error));

	IniDocument reloaded;
	CHECK(reloaded.Load(saved, error));

	CHECK_EQ(reloaded.Sections(), document.Sections());
	CHECK_EQ(reloaded.Get("First", "one"), std::string("1"));
	CHECK_EQ(reloaded.Get("First", "unknownkey"), std::string("survives"));
	CHECK_EQ(reloaded.Get("Second", "two"), std::string("2"));
	CHECK_EQ(reloaded.Get("Third", "three"), std::string("3"));
	CHECK_EQ(reloaded.Keys("First"), document.Keys("First"));

	// Mutating a loaded document keeps existing key positions stable.
	document.Set("First", "one", "updated");
	document.Set("First", "brand-new", "x");
	CHECK(document.Save(saved, error));
	CHECK(reloaded.Load(saved, error));
	CHECK_EQ(reloaded.Get("First", "one"), std::string("updated"));
	CHECK_EQ(reloaded.Get("First", "brand-new"), std::string("x"));
	const std::vector<std::string> keys = reloaded.Keys("First");
	CHECK_EQ(keys.size(), size_t{3});
	CHECK_EQ(keys[0], std::string("one"));
	CHECK_EQ(keys[1], std::string("unknownkey"));
	CHECK_EQ(keys[2], std::string("brand-new"));
}

void TestKeysBeforeAnySection()
{
	ResetTestDir();
	const fs::path path = TestDir() / "headerless.ini";
	WriteFile(path, "TopLevel = value\n[Real]\nKey = 1\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(path, error));
	CHECK_EQ(document.Get("", "toplevel"), std::string("value"));
	CHECK_EQ(document.Get("Real", "key"), std::string("1"));
}

void TestMissingFileReportsIo()
{
	IniDocument document;
	OperationError error;
	CHECK(!document.Load(TestDir() / "does-not-exist.ini", error));
	CHECK_EQ(error.stage, ErrorStage::IO);
	CHECK_EQ(error.code, ErrorCode::FilesystemFailure);
	CHECK(!error.detail.empty());
}

void TestErase()
{
	ResetTestDir();
	const fs::path path = TestDir() / "erase.ini";
	WriteFile(path, "[A]\nK = v\n[B]\nK = v\n");

	IniDocument document;
	OperationError error;
	CHECK(document.Load(path, error));

	CHECK(document.Erase("a", "k"));
	CHECK(!document.Has("A", "K"));
	CHECK(!document.Erase("A", "k"));
	CHECK(!document.Erase("Ghost", "k"));
	// The section survives even when its last key is removed.
	CHECK(document.Erase("B", "k"));
	const std::vector<std::string> sections = document.Sections();
	CHECK_EQ(sections.size(), size_t{2});
}

void TestEmptyDocumentRoundTrip()
{
	ResetTestDir();
	const fs::path path = TestDir() / "empty.ini";

	IniDocument document;
	OperationError error;
	CHECK(document.Save(path, error));

	IniDocument reloaded;
	CHECK(reloaded.Load(path, error));
	CHECK(reloaded.Sections().empty());
	CHECK_EQ(reloaded.Get("Any", "key", "dflt"), std::string("dflt"));
}

} // namespace

void RunIniDocumentTests()
{
	TestParseSectionsCommentsAndSpacing();
	TestKeyCaseInsensitivity();
	TestSectionCaseInsensitiveFirstSeenSpelling();
	TestRoundTripPreservesOrderAndUnknownKeys();
	TestKeysBeforeAnySection();
	TestMissingFileReportsIo();
	TestErase();
	TestEmptyDocumentRoundTrip();
}
