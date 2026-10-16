// Echelon @feature Codex 16/10/2026 core StringUtilsTests
/*
** Contract tests for the core string helpers and the stderr logger.
**
** The suite is invoked from the shared echelon_core_tests main() because
** Launcher/Core/CMakeLists.txt globs every Tests/*.cpp into one binary.
*/

#include "Echelon/Core/Logger.h"
#include "Echelon/Core/StringUtils.h"
#include "TestKit.h"

#include <string>
#include <vector>

using namespace echelon::core;

namespace
{

void TestTrim()
{
	std::string value = "  hello  ";
	Trim(value);
	CHECK_EQ(value, std::string("hello"));

	value = "\t\r\n mixed \t\r\n";
	Trim(value);
	CHECK_EQ(value, std::string("mixed"));

	value = "   ";
	Trim(value);
	CHECK(value.empty());

	CHECK_EQ(TrimCopy("  padded  "), std::string("padded"));
	CHECK_EQ(TrimCopy(""), std::string(""));
	CHECK_EQ(TrimCopy("ok"), std::string("ok"));
}

void TestToLower()
{
	CHECK_EQ(ToLowerCopy("HeLLo 123!"), std::string("hello 123!"));
	CHECK_EQ(ToLowerCopy(""), std::string(""));

	// Non-ASCII bytes must survive untouched (ASCII-only folding).
	const std::string cyrillic = "\xD0\x9C";
	CHECK_EQ(ToLowerCopy(cyrillic), cyrillic);

	std::string value = "ABCdef";
	ToLower(value);
	CHECK_EQ(value, std::string("abcdef"));
}

void TestEqualsNoCase()
{
	CHECK(EqualsNoCase("Hello", "hello"));
	CHECK(EqualsNoCase("WORLD", "world"));
	CHECK(EqualsNoCase("", ""));
	CHECK(!EqualsNoCase("hello", "world"));
	CHECK(!EqualsNoCase("ab", "abc"));
	CHECK(!EqualsNoCase("abc", ""));
}

void TestStartsWithEndsWith()
{
	CHECK(StartsWith("hello", "he"));
	CHECK(!StartsWith("hello", "lo"));
	CHECK(StartsWith("hello", ""));
	CHECK(StartsWith("hello", "hello"));
	CHECK(!StartsWith("he", "hello"));

	CHECK(EndsWith("hello", "lo"));
	CHECK(!EndsWith("hello", "he"));
	CHECK(EndsWith("hello", ""));
	CHECK(EndsWith("hello", "hello"));
	CHECK(!EndsWith("hell", "hello"));
}

void TestSplitPreservesEmptyFields()
{
	const std::vector<std::string> fields = Split("a;;b", ';');
	CHECK_EQ(fields.size(), size_t{3});
	CHECK_EQ(fields[0], std::string("a"));
	CHECK_EQ(fields[1], std::string(""));
	CHECK_EQ(fields[2], std::string("b"));

	const std::vector<std::string> trailing = Split("a;", ';');
	CHECK_EQ(trailing.size(), size_t{2});
	CHECK_EQ(trailing[0], std::string("a"));
	CHECK_EQ(trailing[1], std::string(""));

	const std::vector<std::string> leading = Split(";a", ';');
	CHECK_EQ(leading.size(), size_t{2});
	CHECK_EQ(leading[0], std::string(""));
	CHECK_EQ(leading[1], std::string("a"));

	const std::vector<std::string> only = Split(";", ';');
	CHECK_EQ(only.size(), size_t{2});
	CHECK_EQ(only[0], std::string(""));
	CHECK_EQ(only[1], std::string(""));

	const std::vector<std::string> none = Split("a,b,c", ';');
	CHECK_EQ(none.size(), size_t{1});
	CHECK_EQ(none[0], std::string("a,b,c"));

	const std::vector<std::string> empty = Split("", ';');
	CHECK_EQ(empty.size(), size_t{1});
	CHECK_EQ(empty[0], std::string(""));
}

void TestJoin()
{
	CHECK_EQ(Join({"a", "b", "c"}, ", "), std::string("a, b, c"));
	CHECK_EQ(Join({"solo"}, "-"), std::string("solo"));
	CHECK_EQ(Join({}, "-"), std::string(""));
	CHECK_EQ(Join({"", "", ""}, ":"), std::string("::"));
	CHECK_EQ(Join({"a", "b"}, ""), std::string("ab"));
}

void TestSlugifyAsciiMatchesInstaller()
{
	CHECK_EQ(Slugify("Hello, World!"), std::string("hello-world"));
	CHECK_EQ(Slugify("Mod  Name  v2"), std::string("mod-name-v2"));
	CHECK_EQ(Slugify("--Foo--Bar--"), std::string("foo-bar"));
	CHECK_EQ(Slugify("under_score"), std::string("under-score"));
	CHECK_EQ(Slugify("trailing-"), std::string("trailing"));
	CHECK_EQ(Slugify("-leading"), std::string("leading"));
	CHECK_EQ(Slugify("a;;b"), std::string("a-b"));
	CHECK_EQ(Slugify("123"), std::string("123"));

	// The canonical fallback for an empty or separator-only result.
	CHECK_EQ(Slugify(""), std::string("local-modification"));
	CHECK_EQ(Slugify("!!!"), std::string("local-modification"));
	CHECK_EQ(Slugify("   "), std::string("local-modification"));
}

void TestSlugifyKeepsCyrillic()
{
	CHECK_EQ(Slugify("МОД Тест"), std::string("мод-тест"));
	CHECK_EQ(Slugify("Русский"), std::string("русский"));
	CHECK_EQ(Slugify("Ёлка"), std::string("ёлка"));
	CHECK_EQ(Slugify("TestМОД"), std::string("testмод"));
	CHECK_EQ(Slugify("---МОД---"), std::string("мод"));
}

void TestLoggerSmoke()
{
	// No assertion on the output stream: the contract is "never crash and
	// never require a debug build".
	ECHELON_LOG_INFO("test", "info %d", 1);
	ECHELON_LOG_WARN("test", "warning %s", "two");
	ECHELON_LOG_ERROR("test", "error %d %d", 3, 4);
	Log(LogLevel::Info, "test", "direct %s", "call");
	Log(LogLevel::Warning, "test", "no-varargs");
	CHECK(true);
}

} // namespace

void RunStringUtilsTests()
{
	TestTrim();
	TestToLower();
	TestEqualsNoCase();
	TestStartsWithEndsWith();
	TestSplitPreservesEmptyFields();
	TestJoin();
	TestSlugifyAsciiMatchesInstaller();
	TestSlugifyKeepsCyrillic();
	TestLoggerSmoke();
}
