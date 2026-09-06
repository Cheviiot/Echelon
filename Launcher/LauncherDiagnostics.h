/*
** Echelon launcher operation diagnostics.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace EchelonLauncher
{

// Echelon @feature Codex 07/09/2026 Give content and engine operations one structured diagnostic payload.
enum class OperationStage : uint32_t
{
	Validate = 0,
	Lock = 1,
	Resolve = 2,
	Verify = 3,
	Staging = 4,
	Publish = 5,
	Prepare = 6,
	Start = 7,
	Run = 8,
	Stop = 9,
	Quiescent = 10,
	Destroy = 11,
	Recover = 12
};

enum class OperationErrorCode : uint32_t
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

struct OperationDiagnostic
{
	OperationStage stage = OperationStage::Validate;
	OperationErrorCode code = OperationErrorCode::None;
	std::string profileId;
	std::filesystem::path workspace;
	std::filesystem::path affectedPath;
	bool retryable = false;
};

inline const char *OperationStageName(OperationStage stage)
{
	switch (stage) {
		case OperationStage::Validate: return "validate";
		case OperationStage::Lock: return "lock";
		case OperationStage::Resolve: return "resolve";
		case OperationStage::Verify: return "verify";
		case OperationStage::Staging: return "staging";
		case OperationStage::Publish: return "publish";
		case OperationStage::Prepare: return "prepare";
		case OperationStage::Start: return "start";
		case OperationStage::Run: return "run";
		case OperationStage::Stop: return "stop";
		case OperationStage::Quiescent: return "quiescent";
		case OperationStage::Destroy: return "destroy";
		case OperationStage::Recover: return "recover";
	}
	return "unknown";
}

} // namespace EchelonLauncher
