// Echelon @refactor Codex 05/09/2026 Shared hosted lifecycle included by each SDL entry point.
// GeneralsX @feature Codex 11/08/2026 Keep legacy memory pools alive across launcher sessions.
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
	if (!host || host->struct_size < sizeof(EchelonEngineHostV2) || host->abi_version != ECHELON_ENGINE_ABI_VERSION) {
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
