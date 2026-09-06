/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "EngineSession.h"

#include <utility>

namespace EchelonLauncher
{

LegacyBlockingEngineSession::LegacyBlockingEngineSession(PrepareFunction prepare, RunFunction run)
	: m_prepare(std::move(prepare)), m_run(std::move(run))
{
}

bool LegacyBlockingEngineSession::Prepare(std::string &errorMessage)
{
	if (m_state != EngineSessionState::Created) {
		errorMessage = "Engine session was prepared more than once";
		m_state = EngineSessionState::Failed;
		return false;
	}
	if (!m_prepare) {
		errorMessage = "Engine session has no prepare operation";
		m_state = EngineSessionState::Failed;
		return false;
	}
	if (!m_prepare(errorMessage)) {
		if (errorMessage.empty()) errorMessage = "Engine session preparation failed";
		m_state = EngineSessionState::Failed;
		return false;
	}
	m_state = EngineSessionState::Prepared;
	return true;
}

EngineSessionResult LegacyBlockingEngineSession::Run()
{
	EngineSessionResult result;
	result.diagnostic.stage = OperationStage::Run;
	if (m_state != EngineSessionState::Prepared || !m_run) {
		result.diagnostic.stage = OperationStage::Prepare;
		result.diagnostic.code = OperationErrorCode::InvalidInput;
		result.errorMessage = "Engine session was not prepared";
		m_state = EngineSessionState::Failed;
		return result;
	}
	m_state = EngineSessionState::Running;
	result = m_run();
	result.diagnostic.stage = result.result == ECHELON_ENGINE_FATAL_ERROR ? OperationStage::Stop : OperationStage::Quiescent;
	if (result.result == ECHELON_ENGINE_FATAL_ERROR && result.diagnostic.code == OperationErrorCode::None) {
		result.diagnostic.code = OperationErrorCode::EngineFailure;
	}
	m_state = EngineSessionState::Stopping;
	if ((result.quiescenceFlags & ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) !=
		ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) {
		result.diagnostic.stage = OperationStage::Quiescent;
		result.diagnostic.code = OperationErrorCode::NotQuiescent;
		if (result.errorMessage.empty()) result.errorMessage = "Engine session did not reach quiescence";
		m_state = EngineSessionState::Failed;
		return result;
	}
	m_state = result.result == ECHELON_ENGINE_FATAL_ERROR ? EngineSessionState::Failed : EngineSessionState::Quiescent;
	return result;
}

} // namespace EchelonLauncher
