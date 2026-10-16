// Echelon @feature Codex 16/10/2026 core OperationError
/*
** Structured error payload shared by every launcher layer.
**
** Echelon @refactor Codex 16/10/2026 Numeric ErrorCode values are carried over
** verbatim from Launcher/LauncherDiagnostics.h (OperationErrorCode) so existing
** logs and QA tooling keep matching.
*/

#pragma once

#include <cstdint>
#include <string>

namespace echelon::core
{

enum class ErrorStage
{
	None,
	Discovery,
	Validation,
	IO,
	Archive,
	Hash,
	Engine,
	Internal
};

enum class ErrorCode : std::uint32_t
{
	None = 0,
	InvalidInput = 100,
	LockBusy = 101,
	IntegrityFailure = 102,
	FilesystemFailure = 103,
	Conflict = 104,
	EngineFailure = 105,
	NotQuiescent = 106
};

struct OperationError
{
	ErrorStage stage = ErrorStage::None;
	ErrorCode code = ErrorCode::None;
	bool retryable = false;
	std::string detail;

	// Human-readable single line: "Stage::Code: detail" (detail omitted when empty).
	std::string Message() const;
};

// Lowercase symbolic names for both enums, used by Message() and QA logs.
const char *ErrorStageName(ErrorStage stage);
const char *ErrorCodeName(ErrorCode code);

} // namespace echelon::core
