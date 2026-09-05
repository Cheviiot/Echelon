---
applyTo: 'scripts/build/linux/**,GeneralsX/scripts/build/linux/**,GeneralsX/scripts/env/docker/**,GeneralsX/GeneralsMD/Code/CompatLib/**'
---

## Linux Build Environment

On ALT, use `ubuntu-dev` Distrobox for development packages and commands. The tested setup is recorded in `docs/WORKDIR/reports/ECHELON_FOUNDATION.md`. Do not install the project toolchain on the host.

From the repository root inside that environment:

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher echelon_settings_tests
ctest --test-dir build/linux64-deploy -R echelon_local_content --output-on-failure
./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Echelon
```

## Run & Test

Use `scripts/qa/smoke/test-echelon-*.sh` with explicitly selected retail fixtures. Native executables live under `build/<preset>/Echelon`; standalone binaries live under `build/<preset>/GeneralsX/{Generals,GeneralsMD}`. Capture logs under `logs/`.

## Linux-Specific Notes

- **Case-sensitive filesystem**: Include paths must match exact case. Use `GeneralsX/scripts/tooling/cpp/maintenance/fixIncludesCase.sh`.
- **DXVK requires Vulkan**: `vulkan-tools`, `mesa-vulkan-drivers`, or proprietary GPU drivers.
- **SDL3**: fetched via CMake FetchContent — no system package needed.
- **DXVK source policy**: fixes go in `GeneralsX/references/fbraz3-dxvk`, never in `build/_deps/...`.
- **CompatLib**: `GeneralsX/GeneralsMD/Code/CompatLib/` provides Win32 API compatibility shims (`windows_compat.h`).
- **No native POSIX calls**: use SDL3 abstractions for timers, threads, file I/O. No raw `pthread_*`, `open()`.
- **`-logToCon`**: only available in debug builds (`RTS_BUILD_OPTION_DEBUG=ON`).
- **Diagnostics**: prefer `fprintf(stderr, ...)` probes; capture stderr and grep targeted markers.

