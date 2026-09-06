// Echelon @refactor Codex 05/09/2026 Shared hosted lifecycle included by each SDL entry point.
// GeneralsX @feature Codex 11/08/2026 Keep legacy memory pools alive across launcher sessions.
#include <new>
#include <string>

#include "Common/ReplaySimulation.h"

static bool s_echelonModuleInitialized = false;

static void EchelonModuleLog(const EchelonEngineHostV2 *host, const char *message)
{
	if (host && host->log_callback) {
		host->log_callback(host->log_user_data, message);
	}
	fprintf(stderr, "%s\n", message);
	fflush(stderr);
}

static bool EchelonModuleInitialize(const EchelonEngineHostV2 *host)
{
	if (s_echelonModuleInitialized) {
		return true;
	}

	TheAsciiStringCriticalSection = &critSec1;
	TheUnicodeStringCriticalSection = &critSec2;
	TheDmaCriticalSection = &critSec3;
	TheMemoryPoolCriticalSection = &critSec4;
	TheDebugLogCriticalSection = &critSec5;

	initMemoryManager();
	TheVersion = NEW Version;
	s_echelonModuleInitialized = true;
	EchelonModuleLog(host, "INFO: Engine module initialized");
	return true;
}

static void EchelonModulePhase(
	const EchelonEngineHostV2 *host, EchelonEnginePhaseV2 phase, const char *message)
{
	if (host && host->phase_callback) {
		host->phase_callback(host->phase_user_data, phase, message);
	}
}

static uint32_t EchelonCollectQuiescenceFlags()
{
	uint32_t flags = 0;
	if (!TheGameEngine) flags |= ECHELON_ENGINE_QUIESCENCE_GAME_ENGINE_RELEASED;
	if (!TheFramePacer) flags |= ECHELON_ENGINE_QUIESCENCE_FRAME_PACER_RELEASED;
	if (!WW3D::Is_Initted()) flags |= ECHELON_ENGINE_QUIESCENCE_WW3D_RELEASED;
	if (!DX8Wrapper::Is_Initted() && !DX8Wrapper::_Get_D3D_Device8() && !DX8Wrapper::_Get_D3D8() &&
		DX8Wrapper::Get_Last_Device_Release_Count() == 0) {
		flags |= ECHELON_ENGINE_QUIESCENCE_DX8_RELEASED;
	}
	if (!TheSDL3Window && !ApplicationHWnd) flags |= ECHELON_ENGINE_QUIESCENCE_WINDOW_DETACHED;
	if (EchelonAreEngineSubsystemSingletonsReleased()) {
		flags |= ECHELON_ENGINE_QUIESCENCE_SUBSYSTEM_SINGLETONS_RELEASED;
	}
	if (EchelonContentRuntime::IsClear()) flags |= ECHELON_ENGINE_QUIESCENCE_CONTENT_LAYERS_RELEASED;
	return flags;
}

static uint32_t EchelonModuleQueryQuiescence(EchelonEngineQuiescenceReportV2 *report)
{
	const uint32_t flags = EchelonCollectQuiescenceFlags();
	if (report && report->struct_size >= sizeof(EchelonEngineQuiescenceReportV2)) {
		report->flags = flags;
	}
	return flags;
}

// Echelon @feature Codex 14/08/2026 Bridge SAGE display changes to the launcher-owned SDL window coordinator.
static bool EchelonRequestWindowMode(void *userData, bool windowed, int renderWidth, int renderHeight)
{
	const EchelonEngineHostV2 *host = static_cast<const EchelonEngineHostV2 *>(userData);
	return host && host->window_mode_callback &&
		host->window_mode_callback(host->window_mode_user_data, windowed ? 1u : 0u,
			static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)) != 0;
}

static EchelonEngineResultV2 EchelonModuleRun(const EchelonEngineHostV2 *host)
{
	if (!host || host->struct_size < sizeof(EchelonEngineHostV2) ||
		(host->abi_version != ECHELON_ENGINE_ABI_VERSION && host->abi_version != ECHELON_ENGINE_ABI_VERSION_V3)) {
		return ECHELON_ENGINE_FATAL_ERROR;
	}

	try {
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_STARTING, "Engine session is starting");
		std::string contentError;
		if (!EchelonContentRuntime::Configure(host->content_layers, host->content_layer_count, contentError)) {
			EchelonModuleLog(host, contentError.c_str());
			EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine content stack validation failed");
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		if (!EchelonModuleInitialize(host)) {
			EchelonContentRuntime::Clear();
			EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine module initialization failed");
			return ECHELON_ENGINE_FATAL_ERROR;
		}

		__argc = host->argc;
		__argv = host->argv;
		TheSDL3Window = static_cast<SDL_Window *>(host->sdl_window);
		ApplicationHWnd = reinterpret_cast<HWND>(TheSDL3Window);
		s_echelonLauncherSession = host->headless == 0;
		// Echelon @feature Codex 14/08/2026 Honor the host-owned window contract before any WW3D/DXVK initialization can resize it.
		DX8Wrapper::Set_Window_Geometry_Externally_Owned(
			host->struct_size >= sizeof(EchelonEngineHostV2) &&
			host->window_policy == ECHELON_ENGINE_WINDOW_POLICY_HOST_OWNED);
		DX8Wrapper::Set_Window_Mode_Request_Callback(
			DX8Wrapper::Is_Window_Geometry_Externally_Owned() ? EchelonRequestWindowMode : nullptr,
			DX8Wrapper::Is_Window_Geometry_Externally_Owned() ? const_cast<EchelonEngineHostV2 *>(host) : nullptr);
		s_echelonReturnRequested = false;
		s_echelonTestReturnUpdates = host->internal_test_return_after_updates;
		s_echelonQuiescenceFlags = 0;

		if (host->asset_root && host->asset_root[0]) {
			setenv("CNC_GENERALS_INSTALLPATH", host->asset_root, 1);
			#if RTS_GENERALS
			setenv("CNC_GENERALS_PATH", host->asset_root, 1);
#else
			setenv("CNC_GENERALS_ZH_PATH", host->asset_root, 1);
#endif
		}
#if !RTS_GENERALS
		if (host->base_asset_root && host->base_asset_root[0]) {
			setenv("CNC_GENERALS_PATH", host->base_asset_root, 1);
		}
#endif
		if (host->user_data_root && host->user_data_root[0]) {
			setenv("ECHELON_USER_DATA_ROOT", host->user_data_root, 1);
		}

		CommandLine::parseCommandLineForStartup();
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_RUNNING, "Engine session is running");
		const Int exitCode = GameMain();
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_STOPPING, "Engine session is stopping");

		TheSDL3Window = nullptr;
		ApplicationHWnd = nullptr;
		s_echelonLauncherSession = false;
		DX8Wrapper::Set_Window_Mode_Request_Callback(nullptr, nullptr);
		DX8Wrapper::Set_Window_Geometry_Externally_Owned(false);
		s_echelonTestReturnUpdates = 0;
		EchelonContentRuntime::Clear();
		s_echelonQuiescenceFlags = EchelonCollectQuiescenceFlags();
		if ((s_echelonQuiescenceFlags & ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) !=
			ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) {
			EchelonModuleLog(host, "ERROR: Engine did not reach a quiescent state");
			EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine teardown is incomplete");
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_QUIESCENT, "Engine session is quiescent");

		if (exitCode != 0) {
			EchelonModuleLog(host, "ERROR: Engine session failed");
			return ECHELON_ENGINE_FATAL_ERROR;
		}
		return s_echelonReturnRequested ? ECHELON_ENGINE_RETURN_TO_LAUNCHER : ECHELON_ENGINE_EXIT_APPLICATION;
	} catch (const std::exception &error) {
		fprintf(stderr, "FATAL: Engine module exception: %s\n", error.what());
		fflush(stderr);
	} catch (...) {
		EchelonModuleLog(host, "FATAL: Unknown Engine module exception");
	}

	TheSDL3Window = nullptr;
	ApplicationHWnd = nullptr;
	s_echelonLauncherSession = false;
	DX8Wrapper::Set_Window_Mode_Request_Callback(nullptr, nullptr);
	DX8Wrapper::Set_Window_Geometry_Externally_Owned(false);
	s_echelonTestReturnUpdates = 0;
	EchelonContentRuntime::Clear();
	s_echelonQuiescenceFlags = EchelonCollectQuiescenceFlags();
	EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine session failed");
	return ECHELON_ENGINE_FATAL_ERROR;
}

// Echelon @feature Codex 06/09/2026 Expose an explicit V3 session boundary while the legacy GameMain loop remains blocking.
struct EchelonEngineSessionV3
{
	EchelonEngineHostV3 host{};
	EchelonEngineSessionStateV3 state = ECHELON_ENGINE_SESSION_CREATED_V3;
	EchelonEngineResultV2 result = ECHELON_ENGINE_FATAL_ERROR;
	uint32_t quiescenceFlags = 0;
	uint32_t errorCode = ECHELON_ENGINE_SESSION_ERROR_NONE_V3;
	std::string errorMessage;
	bool nativeLifecycle = false;
	bool nativeFinished = false;
};

static void EchelonSessionWriteResult(const EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!result) return;
	result->struct_size = sizeof(EchelonEngineSessionResultV3);
	result->state = session ? static_cast<uint32_t>(session->state) :
		static_cast<uint32_t>(ECHELON_ENGINE_SESSION_FAILED_V3);
	result->result = session ? session->result : ECHELON_ENGINE_FATAL_ERROR;
	result->quiescence_flags = session ? session->quiescenceFlags : 0;
	result->error_code = session ? session->errorCode : ECHELON_ENGINE_SESSION_ERROR_INVALID_ARGUMENT_V3;
	result->error_message = session && !session->errorMessage.empty() ? session->errorMessage.c_str() : nullptr;
}

static EchelonEngineSessionV3 *EchelonModuleCreateSession(const EchelonEngineHostV3 *host)
{
	if (!host || host->struct_size < sizeof(EchelonEngineHostV2) ||
		host->abi_version != ECHELON_ENGINE_ABI_VERSION_V3) return nullptr;
	EchelonEngineSessionV3 *session = new (std::nothrow) EchelonEngineSessionV3();
	if (!session) return nullptr;
	session->host = *host;
	return session;
}

// Echelon @feature Codex 07/09/2026 Split the hosted engine bootstrap from GameMain so V3 can own one frame at a time.
static void EchelonConfigureHostedRuntime(const EchelonEngineHostV2 *host)
{
	__argc = host->argc;
	__argv = host->argv;
	TheSDL3Window = static_cast<SDL_Window *>(host->sdl_window);
	ApplicationHWnd = reinterpret_cast<HWND>(TheSDL3Window);
	s_echelonLauncherSession = host->headless == 0;
	DX8Wrapper::Set_Window_Geometry_Externally_Owned(
		host->window_policy == ECHELON_ENGINE_WINDOW_POLICY_HOST_OWNED);
	DX8Wrapper::Set_Window_Mode_Request_Callback(
		DX8Wrapper::Is_Window_Geometry_Externally_Owned() ? EchelonRequestWindowMode : nullptr,
		DX8Wrapper::Is_Window_Geometry_Externally_Owned() ? const_cast<EchelonEngineHostV2 *>(host) : nullptr);
	s_echelonReturnRequested = false;
	s_echelonTestReturnUpdates = host->internal_test_return_after_updates;
	s_echelonQuiescenceFlags = 0;

	if (host->asset_root && host->asset_root[0]) {
		setenv("CNC_GENERALS_INSTALLPATH", host->asset_root, 1);
#if RTS_GENERALS
		setenv("CNC_GENERALS_PATH", host->asset_root, 1);
#else
		setenv("CNC_GENERALS_ZH_PATH", host->asset_root, 1);
#endif
	}
#if !RTS_GENERALS
	if (host->base_asset_root && host->base_asset_root[0]) {
		setenv("CNC_GENERALS_PATH", host->base_asset_root, 1);
	}
#endif
	if (host->user_data_root && host->user_data_root[0]) {
		setenv("ECHELON_USER_DATA_ROOT", host->user_data_root, 1);
	}
	CommandLine::parseCommandLineForStartup();
}

static void EchelonResetHostedRuntime()
{
	TheSDL3Window = nullptr;
	ApplicationHWnd = nullptr;
	s_echelonLauncherSession = false;
	DX8Wrapper::Set_Window_Mode_Request_Callback(nullptr, nullptr);
	DX8Wrapper::Set_Window_Geometry_Externally_Owned(false);
	s_echelonTestReturnUpdates = 0;
}

static bool EchelonStartNativeGame(const EchelonEngineHostV3 *host, std::string &errorMessage)
{
	try {
		std::string contentError;
		if (!EchelonContentRuntime::Configure(host->content_layers, host->content_layer_count, contentError)) {
			errorMessage = contentError.empty() ? "Engine content stack validation failed" : contentError;
			return false;
		}
		if (!EchelonModuleInitialize(host)) {
			errorMessage = "Engine module initialization failed";
			return false;
		}
		EchelonConfigureHostedRuntime(host);
		TheFramePacer = NEW FramePacer();
		TheFramePacer->enableFramesPerSecondLimit(TRUE);
		TheGameEngine = CreateGameEngine();
		if (!TheGameEngine) {
			errorMessage = "Engine factory returned no game engine";
			return false;
		}
		TheGameEngine->init();
		if (!TheGlobalData) {
			errorMessage = "Engine did not initialize global data";
			return false;
		}
		return true;
	} catch (const std::exception &error) {
		errorMessage = error.what();
	} catch (...) {
		errorMessage = "Unknown exception while starting the engine";
	}
	return false;
}

static EchelonEngineResultV2 EchelonFinishNativeGame(const EchelonEngineHostV2 *host, Int exitCode,
	uint32_t &quiescenceFlags, std::string &errorMessage)
{
	EchelonModulePhase(host, ECHELON_ENGINE_PHASE_STOPPING, "Engine session is stopping");
	try {
		if (TheFramePacer) {
			delete TheFramePacer;
			TheFramePacer = nullptr;
		}
		if (TheGameEngine) {
			delete TheGameEngine;
			TheGameEngine = nullptr;
		}
	} catch (const std::exception &error) {
		errorMessage = error.what();
	} catch (...) {
		errorMessage = "Unknown exception while stopping the engine";
	}
	EchelonResetHostedRuntime();
	EchelonContentRuntime::Clear();
	s_echelonQuiescenceFlags = EchelonCollectQuiescenceFlags();
	quiescenceFlags = s_echelonQuiescenceFlags;
	if ((quiescenceFlags & ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) !=
		ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) {
		if (errorMessage.empty()) errorMessage = "Engine did not reach quiescence";
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine teardown is incomplete");
		return ECHELON_ENGINE_FATAL_ERROR;
	}
	if (exitCode != 0 || !errorMessage.empty()) {
		EchelonModulePhase(host, ECHELON_ENGINE_PHASE_FAILED, "Engine session failed");
		return ECHELON_ENGINE_FATAL_ERROR;
	}
	EchelonModulePhase(host, ECHELON_ENGINE_PHASE_QUIESCENT, "Engine session is quiescent");
	return s_echelonReturnRequested ? ECHELON_ENGINE_RETURN_TO_LAUNCHER : ECHELON_ENGINE_EXIT_APPLICATION;
}

static uint32_t EchelonModulePrepareSession(EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!session) {
		EchelonSessionWriteResult(nullptr, result);
		return 0;
	}
	session->errorMessage.clear();
	session->errorCode = ECHELON_ENGINE_SESSION_ERROR_NONE_V3;
	if (session->state != ECHELON_ENGINE_SESSION_CREATED_V3) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_INVALID_STATE_V3;
		session->errorMessage = "Engine session was prepared more than once";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	std::string contentError;
	if (!EchelonContentRuntime::Configure(session->host.content_layers, session->host.content_layer_count, contentError)) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_CONTENT_V3;
		session->errorMessage = contentError;
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	EchelonContentRuntime::Clear();
	session->state = ECHELON_ENGINE_SESSION_PREPARED_V3;
	session->result = ECHELON_ENGINE_RETURN_TO_LAUNCHER;
	EchelonSessionWriteResult(session, result);
	return 1;
}

static uint32_t EchelonModuleStartSession(EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!session) {
		EchelonSessionWriteResult(nullptr, result);
		return 0;
	}
	session->errorMessage.clear();
	session->errorCode = ECHELON_ENGINE_SESSION_ERROR_NONE_V3;
	if (session->state != ECHELON_ENGINE_SESSION_PREPARED_V3) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_INVALID_STATE_V3;
		session->errorMessage = "Engine session was not prepared";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	std::string startupError;
	if (!EchelonStartNativeGame(&session->host, startupError)) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_LEGACY_RUN_V3;
		session->errorMessage = startupError.empty() ? "Engine session startup failed" : startupError;
		uint32_t ignoredFlags = 0;
		std::string ignoredError;
		EchelonFinishNativeGame(&session->host, ECHELON_ENGINE_FATAL_ERROR, ignoredFlags, ignoredError);
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	session->nativeLifecycle = true;
	session->state = ECHELON_ENGINE_SESSION_RUNNING_V3;
	session->result = ECHELON_ENGINE_RETURN_TO_LAUNCHER;
	EchelonModulePhase(&session->host, ECHELON_ENGINE_PHASE_RUNNING, "Engine session is running");
	if (TheGlobalData && !TheGlobalData->m_simulateReplays.empty()) {
		const Int replayResult = ReplaySimulation::simulateReplays(
			TheGlobalData->m_simulateReplays, TheGlobalData->m_simulateReplayJobs);
		session->result = replayResult == 0 ? ECHELON_ENGINE_EXIT_APPLICATION : ECHELON_ENGINE_FATAL_ERROR;
		session->state = ECHELON_ENGINE_SESSION_STOPPING_V3;
		if (session->result == ECHELON_ENGINE_FATAL_ERROR) {
			session->errorCode = ECHELON_ENGINE_SESSION_ERROR_LEGACY_RUN_V3;
			session->errorMessage = "Replay simulation failed";
		}
	}
	EchelonSessionWriteResult(session, result);
	return 1;
}

static uint32_t EchelonModuleStepSession(EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!session) {
		EchelonSessionWriteResult(nullptr, result);
		return 0;
	}
	if (session->state != ECHELON_ENGINE_SESSION_RUNNING_V3) {
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_INVALID_STATE_V3;
		session->errorMessage = "Engine session is not running";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	if (!TheGameEngine || !TheFramePacer) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_INVALID_STATE_V3;
		session->errorMessage = "Engine session has no active frame loop";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	try {
		TheGameEngine->update();
		TheFramePacer->update();
		if (TheGameEngine->getQuitting()) {
			session->result = s_echelonReturnRequested ? ECHELON_ENGINE_RETURN_TO_LAUNCHER :
				ECHELON_ENGINE_EXIT_APPLICATION;
			session->state = ECHELON_ENGINE_SESSION_STOPPING_V3;
		}
	} catch (const std::exception &error) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_LEGACY_RUN_V3;
		session->errorMessage = error.what();
	} catch (...) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_LEGACY_RUN_V3;
		session->errorMessage = "Unknown exception while stepping the engine";
	}
	EchelonSessionWriteResult(session, result);
	return session->state != ECHELON_ENGINE_SESSION_FAILED_V3;
}

static uint32_t EchelonModuleStopSession(EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!session) {
		EchelonSessionWriteResult(nullptr, result);
		return 0;
	}
	if (session->state != ECHELON_ENGINE_SESSION_STOPPING_V3 &&
		session->state != ECHELON_ENGINE_SESSION_FAILED_V3) {
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_INVALID_STATE_V3;
		session->errorMessage = "Engine session is not waiting for Stop";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	if (session->nativeLifecycle && !session->nativeFinished) {
		std::string finishError;
		session->result = EchelonFinishNativeGame(&session->host, session->result == ECHELON_ENGINE_FATAL_ERROR ? 1 : 0,
			session->quiescenceFlags, finishError);
		session->nativeFinished = true;
		if (!finishError.empty() && session->errorMessage.empty()) session->errorMessage = finishError;
	} else {
		session->quiescenceFlags = EchelonModuleQueryQuiescence(nullptr);
	}
	if ((session->quiescenceFlags & ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) !=
		ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS) {
		session->state = ECHELON_ENGINE_SESSION_FAILED_V3;
		session->errorCode = ECHELON_ENGINE_SESSION_ERROR_NOT_QUIESCENT_V3;
		session->errorMessage = "Engine session did not reach quiescence";
		EchelonSessionWriteResult(session, result);
		return 0;
	}
	session->state = session->result == ECHELON_ENGINE_FATAL_ERROR ? ECHELON_ENGINE_SESSION_FAILED_V3 :
		ECHELON_ENGINE_SESSION_QUIESCENT_V3;
	EchelonSessionWriteResult(session, result);
	return 1;
}

static uint32_t EchelonModuleQuerySession(const EchelonEngineSessionV3 *session,
	EchelonEngineSessionResultV3 *result)
{
	if (!session) {
		EchelonSessionWriteResult(nullptr, result);
		return 0;
	}
	EchelonSessionWriteResult(session, result);
	return 1;
}

static void EchelonModuleDestroySession(EchelonEngineSessionV3 *session)
{
	delete session;
}

static void EchelonModuleShutdown()
{
	if (!s_echelonModuleInitialized) {
		return;
	}
	if (TheVersion) {
		delete TheVersion;
		TheVersion = nullptr;
	}
	shutdownMemoryManager();
	TheAsciiStringCriticalSection = nullptr;
	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;
	TheDebugLogCriticalSection = nullptr;
	s_echelonModuleInitialized = false;
}

ECHELON_ENGINE_EXPORT const EchelonEngineModuleV2 *Echelon_GetEngineModuleV2(void)
{
	static const EchelonEngineModuleV2 module = {
		sizeof(EchelonEngineModuleV2),
		ECHELON_ENGINE_ABI_VERSION,
#if RTS_GENERALS
		"generals",
		"Command & Conquer: Generals",
#else
		"zerohour",
		"Command & Conquer: Generals - Zero Hour",
#endif
		&EchelonModuleRun,
		&EchelonModuleQueryQuiescence,
		&EchelonModuleShutdown
	};
	return &module;
}

ECHELON_ENGINE_EXPORT const EchelonEngineModuleV3 *Echelon_GetEngineModuleV3(void)
{
	static const EchelonEngineModuleV3 module = {
		sizeof(EchelonEngineModuleV3),
		ECHELON_ENGINE_ABI_VERSION_V3,
#if RTS_GENERALS
		"generals",
		"Command & Conquer: Generals",
#else
		"zerohour",
		"Command & Conquer: Generals - Zero Hour",
#endif
		&EchelonModuleCreateSession,
		&EchelonModulePrepareSession,
		&EchelonModuleStartSession,
		&EchelonModuleStepSession,
		&EchelonModuleStopSession,
		&EchelonModuleQuerySession,
		&EchelonModuleDestroySession
	};
	return &module;
}
