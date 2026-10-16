// Echelon @feature Codex 16/10/2026 core ResultTests
/*
** Contract tests for Result<T>, Status and OperationError.
*/

#include "Echelon/Core/OperationError.h"
#include "Echelon/Core/Result.h"
#include "TestKit.h"

#include <cstdint>
#include <string>

using namespace echelon::core;

// Suites from the sibling test files; Launcher/Core/CMakeLists.txt globs all
// Tests/*.cpp into this single echelon_core_tests binary, so their entry
// points are invoked from this main().
void RunStringUtilsTests();
void RunIniDocumentTests();

namespace
{

void TestOkStoresValue()
{
	Result<int> result = Result<int>::Ok(42);
	CHECK(result.HasValue());
	CHECK(static_cast<bool>(result));
	CHECK_EQ(result.Value(), 42);

	Result<std::string> text = Result<std::string>::Ok(std::string("payload"));
	CHECK_EQ(text.Value(), std::string("payload"));
}

void TestErrPreservesFields()
{
	OperationError error;
	error.stage = ErrorStage::Validation;
	error.code = ErrorCode::InvalidInput;
	error.retryable = false;
	error.detail = "missing required field";

	Result<int> result = Result<int>::Err(error);
	CHECK(!result.HasValue());
	CHECK(!static_cast<bool>(result));
	CHECK_EQ(result.Error().stage, ErrorStage::Validation);
	CHECK_EQ(result.Error().code, ErrorCode::InvalidInput);
	CHECK(!result.Error().retryable);
	CHECK_EQ(result.Error().detail, std::string("missing required field"));
}

void TestValueAndErrorGuards()
{
	Result<int> failed = Result<int>::Err({ErrorStage::IO, ErrorCode::FilesystemFailure, true, "disk full"});
	CHECK_THROWS(failed.Value());
	CHECK_THROWS(Result<int>::Ok(7).Error());

	Status ok = Status::Ok();
	CHECK_THROWS(ok.Error());
}

void TestMapAppliesOnSuccess()
{
	Result<int> result = Result<int>::Ok(21);
	Result<std::string> mapped = result.Map<std::string>([](int &value) {
		return std::string("value=") + std::to_string(value * 2);
	});
	CHECK(mapped.HasValue());
	CHECK_EQ(mapped.Value(), std::string("value=42"));
}

void TestMapSkippedOnError()
{
	bool invoked = false;
	OperationError error{ErrorStage::Hash, ErrorCode::IntegrityFailure, false, "checksum mismatch"};
	Result<int> failed = Result<int>::Err(error);

	Result<std::string> mapped = failed.Map<std::string>([&invoked](int &value) {
		invoked = true;
		return std::to_string(value);
	});

	CHECK(!invoked);
	CHECK(!mapped.HasValue());
	CHECK_EQ(mapped.Error().stage, ErrorStage::Hash);
	CHECK_EQ(mapped.Error().code, ErrorCode::IntegrityFailure);
	CHECK_EQ(mapped.Error().detail, std::string("checksum mismatch"));
}

void TestStatusOkAndErr()
{
	Status ok = Status::Ok();
	CHECK(ok.HasValue());
	CHECK(static_cast<bool>(ok));

	Status failed = Status::Err({ErrorStage::Engine, ErrorCode::EngineFailure, true, "module exited"});
	CHECK(!failed.HasValue());
	CHECK_EQ(failed.Error().code, ErrorCode::EngineFailure);
	CHECK(failed.Error().retryable);
	CHECK_EQ(failed.Error().detail, std::string("module exited"));
}

void TestErrorCodeValuesMatchLegacyDiagnostics()
{
	// Values must stay identical to Launcher/LauncherDiagnostics.h (OperationErrorCode)
	// because QA scripts and stored logs pattern-match on them.
	CHECK_EQ(static_cast<std::uint32_t>(ErrorCode::None), static_cast<std::uint32_t>(0));
	CHECK_EQ(static_cast<std::uint32_t>(ErrorCode::InvalidInput), static_cast<std::uint32_t>(100));
	CHECK_EQ(static_cast<std::uint32_t>(ErrorCode::IntegrityFailure), static_cast<std::uint32_t>(102));
	CHECK_EQ(static_cast<std::uint32_t>(ErrorCode::NotQuiescent), static_cast<std::uint32_t>(106));
}

void TestMessageFormat()
{
	OperationError error{ErrorStage::Archive, ErrorCode::FilesystemFailure, true, "cannot write staging file"};
	CHECK_EQ(error.Message(), std::string("archive::filesystem-failure: cannot write staging file"));

	OperationError bare{ErrorStage::Discovery, ErrorCode::LockBusy, false, ""};
	CHECK_EQ(bare.Message(), std::string("discovery::lock-busy"));
}

} // namespace

int main()
{
	TestOkStoresValue();
	TestErrPreservesFields();
	TestValueAndErrorGuards();
	TestMapAppliesOnSuccess();
	TestMapSkippedOnError();
	TestStatusOkAndErr();
	TestErrorCodeValuesMatchLegacyDiagnostics();
	TestMessageFormat();
	RunStringUtilsTests();
	RunIniDocumentTests();
	return TestKitReport();
}
