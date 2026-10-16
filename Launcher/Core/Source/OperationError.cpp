// Echelon @feature Codex 16/10/2026 core OperationError
/*
** OperationError payload and its Message() rendering.
*/

#include "Echelon/Core/OperationError.h"

namespace echelon::core
{

const char *ErrorStageName(ErrorStage stage)
{
	switch (stage) {
		case ErrorStage::None: return "none";
		case ErrorStage::Discovery: return "discovery";
		case ErrorStage::Validation: return "validation";
		case ErrorStage::IO: return "io";
		case ErrorStage::Archive: return "archive";
		case ErrorStage::Hash: return "hash";
		case ErrorStage::Engine: return "engine";
		case ErrorStage::Internal: return "internal";
	}
	return "unknown";
}

const char *ErrorCodeName(ErrorCode code)
{
	switch (code) {
		case ErrorCode::None: return "none";
		case ErrorCode::InvalidInput: return "invalid-input";
		case ErrorCode::LockBusy: return "lock-busy";
		case ErrorCode::IntegrityFailure: return "integrity-failure";
		case ErrorCode::FilesystemFailure: return "filesystem-failure";
		case ErrorCode::Conflict: return "conflict";
		case ErrorCode::EngineFailure: return "engine-failure";
		case ErrorCode::NotQuiescent: return "not-quiescent";
	}
	return "unknown";
}

std::string OperationError::Message() const
{
	std::string message = ErrorStageName(stage);
	message += "::";
	message += ErrorCodeName(code);
	if (!detail.empty()) {
		message += ": ";
		message += detail;
	}
	return message;
}

} // namespace echelon::core
