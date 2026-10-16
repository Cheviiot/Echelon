// Echelon @feature Codex 16/10/2026 testkit TestKit
/*
** Minimal shared test harness for all layered launcher test binaries.
**
** Each test binary provides its own main() that runs test functions and
** returns TestKitReport(). Every failed check prints to stderr with the
** exact file and line, so CI logs point straight at the regression.
*/

#pragma once

#include <cstdio>

namespace testkit_detail
{

inline int &TotalCount()
{
	static int count = 0;
	return count;
}

inline int &FailureCount()
{
	static int count = 0;
	return count;
}

inline void Record(bool ok, const char *expression, const char *file, int line)
{
	++TotalCount();
	if (ok) {
		return;
	}
	++FailureCount();
	std::fprintf(stderr, "FAIL: %s (%s:%d)\n", expression, file, line);
	std::fflush(stderr);
}

template <typename A, typename B>
inline void RecordEq(const A &actual, const B &expected, const char *actualExpr, const char *expectedExpr,
	const char *file, int line)
{
	++TotalCount();
	if (actual == expected) {
		return;
	}
	++FailureCount();
	std::fprintf(stderr, "FAIL: %s == %s (%s:%d)\n", actualExpr, expectedExpr, file, line);
	std::fflush(stderr);
}

} // namespace testkit_detail

#define CHECK(condition) ::testkit_detail::Record(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected) \
	::testkit_detail::RecordEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_THROWS(expression) \
	do { \
		++::testkit_detail::TotalCount(); \
		bool testKitCaught = false; \
		try { \
			expression; \
		} catch (...) { \
			testKitCaught = true; \
		} \
		if (!testKitCaught) { \
			++::testkit_detail::FailureCount(); \
			std::fprintf(stderr, "FAIL: expected exception: %s (%s:%d)\n", #expression, __FILE__, __LINE__); \
			std::fflush(stderr); \
		} \
	} while (0)

// Prints the "[TESTKIT] N passed, M failed" summary to stderr and returns 0/1.
inline int TestKitReport()
{
	const int failed = testkit_detail::FailureCount();
	const int total = testkit_detail::TotalCount();
	std::fprintf(stderr, "[TESTKIT] %d passed, %d failed\n", total - failed, failed);
	std::fflush(stderr);
	return failed == 0 ? 0 : 1;
}
