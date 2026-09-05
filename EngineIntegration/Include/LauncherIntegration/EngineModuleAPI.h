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

#include <stdint.h>

// Echelon @feature Codex 14/08/2026 Carry a complete immutable content stack through ABI V2.
#define ECHELON_ENGINE_ABI_VERSION 2u
#define ECHELON_MAX_CONTENT_LAYERS 64u

#if defined(_WIN32)
#if defined(ECHELON_ENGINE_BUILD)
#define ECHELON_ENGINE_EXPORT __declspec(dllexport)
#else
#define ECHELON_ENGINE_EXPORT __declspec(dllimport)
#endif
#else
#define ECHELON_ENGINE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum EchelonEngineResultV2
{
	ECHELON_ENGINE_RETURN_TO_LAUNCHER = 0,
	ECHELON_ENGINE_EXIT_APPLICATION = 1,
	ECHELON_ENGINE_FATAL_ERROR = 2
} EchelonEngineResultV2;

typedef void (*EchelonEngineLogCallbackV2)(void *user_data, const char *message);

typedef enum EchelonEnginePhaseV2
{
	ECHELON_ENGINE_PHASE_STARTING = 0,
	ECHELON_ENGINE_PHASE_RUNNING = 1,
	ECHELON_ENGINE_PHASE_STOPPING = 2,
	ECHELON_ENGINE_PHASE_QUIESCENT = 3,
	ECHELON_ENGINE_PHASE_FAILED = 4
} EchelonEnginePhaseV2;

typedef void (*EchelonEnginePhaseCallbackV2)(
	void *user_data, EchelonEnginePhaseV2 phase, const char *message);

typedef enum EchelonEngineQuiescenceFlagV2
{
	ECHELON_ENGINE_QUIESCENCE_GAME_ENGINE_RELEASED = 1u << 0,
	ECHELON_ENGINE_QUIESCENCE_FRAME_PACER_RELEASED = 1u << 1,
	ECHELON_ENGINE_QUIESCENCE_WW3D_RELEASED = 1u << 2,
	ECHELON_ENGINE_QUIESCENCE_DX8_RELEASED = 1u << 3,
	ECHELON_ENGINE_QUIESCENCE_WINDOW_DETACHED = 1u << 4,
	ECHELON_ENGINE_QUIESCENCE_SUBSYSTEM_SINGLETONS_RELEASED = 1u << 5,
	ECHELON_ENGINE_QUIESCENCE_CONTENT_LAYERS_RELEASED = 1u << 6
} EchelonEngineQuiescenceFlagV2;

#define ECHELON_ENGINE_REQUIRED_QUIESCENCE_FLAGS \
	(ECHELON_ENGINE_QUIESCENCE_GAME_ENGINE_RELEASED | \
		ECHELON_ENGINE_QUIESCENCE_FRAME_PACER_RELEASED | \
		ECHELON_ENGINE_QUIESCENCE_WW3D_RELEASED | ECHELON_ENGINE_QUIESCENCE_DX8_RELEASED | \
		ECHELON_ENGINE_QUIESCENCE_WINDOW_DETACHED | \
		ECHELON_ENGINE_QUIESCENCE_SUBSYSTEM_SINGLETONS_RELEASED | \
		ECHELON_ENGINE_QUIESCENCE_CONTENT_LAYERS_RELEASED)

typedef struct EchelonEngineQuiescenceReportV2
{
	uint32_t struct_size;
	uint32_t flags;
} EchelonEngineQuiescenceReportV2;

typedef enum EchelonEngineWindowPolicyV2
{
	ECHELON_ENGINE_WINDOW_POLICY_ENGINE_OWNED = 0,
	ECHELON_ENGINE_WINDOW_POLICY_HOST_OWNED = 1
} EchelonEngineWindowPolicyV2;

typedef uint32_t (*EchelonEngineWindowModeCallbackV2)(
	void *user_data, uint32_t windowed, uint32_t render_width, uint32_t render_height);

typedef enum EchelonContentLayerTypeV1
{
	ECHELON_CONTENT_LAYER_MOD = 0,
	ECHELON_CONTENT_LAYER_PATCH = 1,
	ECHELON_CONTENT_LAYER_ADDON = 2
} EchelonContentLayerTypeV1;

typedef struct EchelonContentLayerV1
{
	uint32_t struct_size;
	uint32_t layer_type;
	const char *id;
	const char *version;
	const char *root_path;
	uint32_t priority;
	const char *content_fingerprint;
} EchelonContentLayerV1;

typedef struct EchelonEngineHostV2
{
	uint32_t struct_size;
	uint32_t abi_version;
	void *sdl_window;
	int argc;
	char **argv;
	const char *profile_id;
	const char *asset_root;
	const char *base_asset_root;
	const char *user_data_root;
	uint32_t headless;
	void *log_user_data;
	EchelonEngineLogCallbackV2 log_callback;
	void *phase_user_data;
	EchelonEnginePhaseCallbackV2 phase_callback;
	uint32_t internal_test_return_after_updates;
	uint32_t window_policy;
	void *window_mode_user_data;
	EchelonEngineWindowModeCallbackV2 window_mode_callback;
	const EchelonContentLayerV1 *content_layers;
	uint32_t content_layer_count;
	const char *content_stack_fingerprint;
} EchelonEngineHostV2;

typedef struct EchelonEngineModuleV2
{
	uint32_t struct_size;
	uint32_t abi_version;
	const char *engine_id;
	const char *display_name;
	EchelonEngineResultV2 (*run)(const EchelonEngineHostV2 *host);
	uint32_t (*query_quiescence)(EchelonEngineQuiescenceReportV2 *report);
	void (*shutdown)(void);
} EchelonEngineModuleV2;

typedef const EchelonEngineModuleV2 *(*EchelonGetEngineModuleV2Fn)(void);

ECHELON_ENGINE_EXPORT const EchelonEngineModuleV2 *Echelon_GetEngineModuleV2(void);

#ifdef __cplusplus
}

void EchelonRequestReturnToLauncher();
bool EchelonIsLauncherSession();
bool EchelonConsumeTestReturnRequest();
#endif
