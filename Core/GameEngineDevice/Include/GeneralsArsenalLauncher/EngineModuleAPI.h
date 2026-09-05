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

// GeneralsArsenal @feature Codex 14/08/2026 Carry a complete immutable content stack through ABI V2.
#define GENERALS_ARSENAL_ENGINE_ABI_VERSION 2u
#define GENERALS_ARSENAL_MAX_CONTENT_LAYERS 64u

#if defined(_WIN32)
#if defined(GENERALS_ARSENAL_ENGINE_BUILD)
#define GENERALS_ARSENAL_ENGINE_EXPORT __declspec(dllexport)
#else
#define GENERALS_ARSENAL_ENGINE_EXPORT __declspec(dllimport)
#endif
#else
#define GENERALS_ARSENAL_ENGINE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GeneralsArsenalEngineResultV2
{
	GENERALS_ARSENAL_ENGINE_RETURN_TO_LAUNCHER = 0,
	GENERALS_ARSENAL_ENGINE_EXIT_APPLICATION = 1,
	GENERALS_ARSENAL_ENGINE_FATAL_ERROR = 2
} GeneralsArsenalEngineResultV2;

typedef void (*GeneralsArsenalEngineLogCallbackV2)(void *user_data, const char *message);

typedef enum GeneralsArsenalEnginePhaseV2
{
	GENERALS_ARSENAL_ENGINE_PHASE_STARTING = 0,
	GENERALS_ARSENAL_ENGINE_PHASE_RUNNING = 1,
	GENERALS_ARSENAL_ENGINE_PHASE_STOPPING = 2,
	GENERALS_ARSENAL_ENGINE_PHASE_QUIESCENT = 3,
	GENERALS_ARSENAL_ENGINE_PHASE_FAILED = 4
} GeneralsArsenalEnginePhaseV2;

typedef void (*GeneralsArsenalEnginePhaseCallbackV2)(
	void *user_data, GeneralsArsenalEnginePhaseV2 phase, const char *message);

typedef enum GeneralsArsenalEngineQuiescenceFlagV2
{
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_GAME_ENGINE_RELEASED = 1u << 0,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_FRAME_PACER_RELEASED = 1u << 1,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_WW3D_RELEASED = 1u << 2,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_DX8_RELEASED = 1u << 3,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_WINDOW_DETACHED = 1u << 4,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_SUBSYSTEM_SINGLETONS_RELEASED = 1u << 5,
	GENERALS_ARSENAL_ENGINE_QUIESCENCE_CONTENT_LAYERS_RELEASED = 1u << 6
} GeneralsArsenalEngineQuiescenceFlagV2;

#define GENERALS_ARSENAL_ENGINE_REQUIRED_QUIESCENCE_FLAGS \
	(GENERALS_ARSENAL_ENGINE_QUIESCENCE_GAME_ENGINE_RELEASED | \
		GENERALS_ARSENAL_ENGINE_QUIESCENCE_FRAME_PACER_RELEASED | \
		GENERALS_ARSENAL_ENGINE_QUIESCENCE_WW3D_RELEASED | GENERALS_ARSENAL_ENGINE_QUIESCENCE_DX8_RELEASED | \
		GENERALS_ARSENAL_ENGINE_QUIESCENCE_WINDOW_DETACHED | \
		GENERALS_ARSENAL_ENGINE_QUIESCENCE_SUBSYSTEM_SINGLETONS_RELEASED | \
		GENERALS_ARSENAL_ENGINE_QUIESCENCE_CONTENT_LAYERS_RELEASED)

typedef struct GeneralsArsenalEngineQuiescenceReportV2
{
	uint32_t struct_size;
	uint32_t flags;
} GeneralsArsenalEngineQuiescenceReportV2;

typedef enum GeneralsArsenalEngineWindowPolicyV2
{
	GENERALS_ARSENAL_ENGINE_WINDOW_POLICY_ENGINE_OWNED = 0,
	GENERALS_ARSENAL_ENGINE_WINDOW_POLICY_HOST_OWNED = 1
} GeneralsArsenalEngineWindowPolicyV2;

typedef uint32_t (*GeneralsArsenalEngineWindowModeCallbackV2)(
	void *user_data, uint32_t windowed, uint32_t render_width, uint32_t render_height);

typedef enum GeneralsArsenalContentLayerTypeV1
{
	GENERALS_ARSENAL_CONTENT_LAYER_MOD = 0,
	GENERALS_ARSENAL_CONTENT_LAYER_PATCH = 1,
	GENERALS_ARSENAL_CONTENT_LAYER_ADDON = 2
} GeneralsArsenalContentLayerTypeV1;

typedef struct GeneralsArsenalContentLayerV1
{
	uint32_t struct_size;
	uint32_t layer_type;
	const char *id;
	const char *version;
	const char *root_path;
	uint32_t priority;
	const char *content_fingerprint;
} GeneralsArsenalContentLayerV1;

typedef struct GeneralsArsenalEngineHostV2
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
	GeneralsArsenalEngineLogCallbackV2 log_callback;
	void *phase_user_data;
	GeneralsArsenalEnginePhaseCallbackV2 phase_callback;
	uint32_t internal_test_return_after_updates;
	uint32_t window_policy;
	void *window_mode_user_data;
	GeneralsArsenalEngineWindowModeCallbackV2 window_mode_callback;
	const GeneralsArsenalContentLayerV1 *content_layers;
	uint32_t content_layer_count;
	const char *content_stack_fingerprint;
} GeneralsArsenalEngineHostV2;

typedef struct GeneralsArsenalEngineModuleV2
{
	uint32_t struct_size;
	uint32_t abi_version;
	const char *engine_id;
	const char *display_name;
	GeneralsArsenalEngineResultV2 (*run)(const GeneralsArsenalEngineHostV2 *host);
	uint32_t (*query_quiescence)(GeneralsArsenalEngineQuiescenceReportV2 *report);
	void (*shutdown)(void);
} GeneralsArsenalEngineModuleV2;

typedef const GeneralsArsenalEngineModuleV2 *(*GeneralsArsenalGetEngineModuleV2Fn)(void);

GENERALS_ARSENAL_ENGINE_EXPORT const GeneralsArsenalEngineModuleV2 *GeneralsArsenal_GetEngineModuleV2(void);

#ifdef __cplusplus
}

void GeneralsArsenalRequestReturnToLauncher();
bool GeneralsArsenalIsLauncherSession();
bool GeneralsArsenalConsumeTestReturnRequest();
#endif
