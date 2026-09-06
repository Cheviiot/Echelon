/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#pragma once

#include "LauncherIntegration/EngineModuleAPI.h"

#include <functional>
#include <string>

namespace EchelonLauncher
{

enum class EngineSessionState
{
	Created,
	Prepared,
	Running,
	Stopping,
	Quiescent,
	Failed
};

struct EngineSessionResult
{
	EchelonEngineResultV2 result = ECHELON_ENGINE_FATAL_ERROR;
	uint32_t quiescenceFlags = 0;
	std::string errorMessage;
};

// Echelon @refactor Codex 06/09/2026 Give the legacy blocking ABI an explicit host-owned session lifecycle.
class LegacyBlockingEngineSession
{
public:
	using PrepareFunction = std::function<bool(std::string &errorMessage)>;
	using RunFunction = std::function<EngineSessionResult()>;

	LegacyBlockingEngineSession(PrepareFunction prepare, RunFunction run);

	bool Prepare(std::string &errorMessage);
	EngineSessionResult Run();
	EngineSessionState state() const { return m_state; }

private:
	PrepareFunction m_prepare;
	RunFunction m_run;
	EngineSessionState m_state = EngineSessionState::Created;
};

} // namespace EchelonLauncher
