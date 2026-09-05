---
applyTo: 'cmake/**,CMakeLists.txt,CMakePresets.json,GeneralsX/**/CMakeLists.txt,GeneralsX/cmake/**'
---

## Build Presets

### Legacy (Maintenance Only)
- **`vc6`** — Visual Studio 6 (C++98), 32-bit, DirectX 8 + Miles
- **`win32`** — MSVC 2022 (C++20), experimental upstream path

### Cross-Platform (SDL3 + DXVK + OpenAL) — Active Targets
- **`linux64-deploy`** — GCC/Clang x86_64, Release — **PRIMARY LINUX**
- **`linux64-testing`** — Linux debug variant
- **`macos-vulkan`** — macOS ARM64, RelWithDebInfo — **PRIMARY MACOS**
- **`mingw-w64-i686`** — exploratory MinGW-w64 cross-compile for Windows
- **`windows64-deploy`** — planned MinGW-w64 x86_64 (issue #29, not active)

## Build Workflow

```bash
# Run from the Echelon root, inside the documented Distrobox on ALT.
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher echelon_settings_tests
ctest --test-dir build/linux64-deploy -R echelon_local_content --output-on-failure

# macOS product
cmake --preset macos-vulkan
cmake --build build/macos-vulkan --target echelon_launcher
./scripts/build/macos/bundle-macos-echelon.sh macos-vulkan

# Standalone engine validation: use a fresh build directory.
cmake --preset linux64-deploy -B build/standalone -DRTS_BUILD_UNIVERSAL_LAUNCHER=OFF
cmake --build build/standalone --target g_generals z_generals
```

## DXVK Source of Truth (macOS)

- DXVK fixes must live in `GeneralsX/references/fbraz3-dxvk` and be pushed to the fork branch `generalsx-macos-v2.6`.
- macOS build tracks that branch via CMake FetchContent (`UPDATE_DISCONNECTED FALSE`).
- Local mode: `-DSAGE_DXVK_USE_LOCAL_FORK=ON` (disables update/fetch).

**Rules**:
1. Never patch DXVK files inside `build/_deps/...`.
2. Do not rely on transient patch scripts.
3. Keep Linux/macOS aligned on DXVK 2.6 branch unless explicitly changed.
4. Validate fix locally → commit/push to fork → CMake consumes from tracked branch.

## Testing Strategy

1. Per-platform smoke tests: launch game, reach main menu, load skirmish map.
2. Replay compatibility: VC6 optimized builds with `RTS_BUILD_OPTION_DEBUG=OFF`.
3. Cross-platform validation: same replays valid across platforms.
