# Echelon foundation and source ownership

## Result

Echelon keeps its product sources at the repository root and the integrated engine in an ordinary tracked `GeneralsX/` directory. No GeneralsX submodule or mod catalog backend is introduced. The existing repository was renamed to [Cheviiot/Echelon](https://github.com/Cheviiot/Echelon), preserving history and issues. The owner authorized integration into product `main`, with a separate locked `upstream` branch retaining exact original history. No release publication is requested.

The original dirty development tree was preserved in checkpoint `984ab6c91`. Merge `4b7395027` integrates 84 upstream commits, from `70cccbdc34985bf90305c7aaf1ab2e962f3cde2b` through `3c2ed4589599beb631ea6da602c8ac95abf6bcdc` (Beta 18 and its following documentation update). Product extraction was checkpointed in `5300ceb85` before relocating source. Complete original-file and Git-history backups are retained in the owner's hidden workspace management directory.

## Ownership map

| Location | Responsibility |
|---|---|
| `Launcher/` | SDL UI, game discovery/import, settings, profiles, local mods and test-only transport/parsers |
| `EngineIntegration/Include/LauncherIntegration/` | Private V2 host ABI, shared engine entry, archive and content-layer interfaces |
| `EngineIntegration/Source/` | Content runtime, process allocator and exported-symbol policy |
| `cmake/` | Generated product identity, hosted library variants and launcher dependencies |
| `assets/launcher/` | Editable Echelon SVG masters, rendered PNGs, redistributable font and licenses |
| `flatpak/`, `scripts/build/` | Unified product packaging |
| `scripts/qa/` | Local-content, lifecycle, recovery, source-boundary and sync checks |
| `docs/`, `.github/`, `AGENTS.md`, root CMake files | Product guidance, worklog, build configuration and automation |
| `GeneralsX/` | Original engine structure, reviewed engine modifications, upstream tools, resources and historical documentation |

The engine directory is **not a pristine snapshot**. Its [54 modified upstream files](ECHELON_ENGINE_DELTA.json) cover build-root adaptation, engine hosting and shutdown, window/render handoff, filesystem layers, localization and retained existing fixes. Product files moved out of the engine tree are not counted as engine modifications. Upstream README, license and historical attribution are preserved. The prior DXVK reference submodule remains registered at `GeneralsX/references/fbraz3-dxvk`.

The main retained integration points are the two SDL entry files, game lifecycle/shutdown, shared subsystem cleanup, archive/local filesystem adapters, display ownership and main-menu return. Their behavior is shared or mirrored for both games. Engine game rules and deterministic math are inherited from the reviewed merge; this restructuring does not intentionally change simulation rules.

`GENERALSX_SOURCE_DIR` / `GENERALSX_BINARY_DIR` identify engine paths. `ECHELON_SOURCE_DIR` / `ECHELON_BINARY_DIR` identify product paths. This explicit split avoids shadowing CMake's global source directory, which vcpkg can reset during configuration.

## Product identity and local content

The executable is `Echelon`, target `echelon_launcher`, app ID `io.github.cheviiot.Echelon`, private ABI entry `Echelon_GetEngineModuleV2`, and data root `$HOME/.Echelon`. The V2 ABI layout remains unchanged. Public paths and environment options use `ECHELON_*`; generated C++/JSON identity comes from `cmake/brand.cmake`.

This is a clean installation. The application does not discover or migrate old Arsenal/GeneralsX roots. Existing directories were used only as explicitly selected, read-only QA inputs in temporary homes.

Local archive/folder imports, installed versions, compatibility/dependency validation, profiles, layers, integrity verification and trash/restore remain available. Catalog refresh, remote installs and startup catalog requests are removed from the product UI. Generic HTTPS/S3 transport and legacy catalog parsing remain isolated in tests; the legacy parser's schema names intentionally retain compatibility. The retired repository plan is archived, with no replacement endpoint or deployment.

Hosted engine libraries are compiled separately from standalone libraries. Only hosted variants receive `ECHELON_BRAND`, `ECHELON_ENGINE_HOSTED` and `ECHELON_ENGINE_MODULE_ALLOCATOR`. Standalone defaults and allocator behavior remain independent. Linux module exports are restricted to the one V2 entry.

## Build environment

Validated on the owner's ALT workstation using Ubuntu 24.04.4 Distrobox **`ubuntu-dev`**, GCC 13, CMake 3.28 and Ninja. Existing vcpkg: `$HOME/.generalsx/vcpkg`. Development packages were installed inside the container; no project toolchain packages were installed on ALT.

Representative container setup (Ubuntu packages):

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git pkg-config meson \
  autoconf automake autoconf-archive libtool curl zip unzip bison \
  libssl-dev libopenal-dev libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
  libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
  wayland-protocols libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev \
  libudev-dev libdbus-1-dev libibus-1.0-dev libpng-dev libfribidi-dev \
  mutter dbus-x11 strace ripgrep flatpak flatpak-builder elfutils \
  librsvg2-bin python3-yaml
```

Use a fresh build directory after relocation. The tested commands from the repository root are:

```bash
distrobox enter ubuntu-dev -- env VCPKG_ROOT="$HOME/.generalsx/vcpkg" \
  cmake --preset linux64-deploy -B build/echelon-linux -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
distrobox enter ubuntu-dev -- cmake --build build/echelon-linux \
  --target echelon_launcher echelon_settings_tests g_generals z_generals -j 8
distrobox enter ubuntu-dev -- ctest --test-dir build/echelon-linux \
  -R echelon_local_content --output-on-failure
python3 scripts/qa/check-echelon-boundaries.py --build build/echelon-linux
```

Product outputs are in `build/echelon-linux/Echelon/`. Standalone binaries are in `build/echelon-linux/GeneralsX/{Generals,GeneralsMD}/`. FetchContent dependencies generally remain under the build root's `_deps/`.

For packaging, the per-user Freedesktop Platform and SDK 25.08 are required. Run the root Flatpak build script inside `ubuntu-dev`; it produces `build/Echelon-linux64-deploy.flatpak`. `elfutils` is required for `eu-strip` / `eu-elfcompress` during finalization. The local build reused cached dependency sources with `ECHELON_FLATPAK_OFFLINE=1`; normal CI performs network fetches.

## Executed validation

All log names below refer to the ignored local `logs/` directory. No retail assets or private replay files are committed.

| Check | Result / evidence |
|---|---|
| Fresh product + both standalone builds after relocation | PASS, 3,939 Ninja steps; `echelon-layout-build.log` |
| Launcher OFF configuration and both standalone targets | PASS, hosted/launcher compile targets absent; `echelon-layout-off-*.log` |
| Restored ON build and CTest local content suite | PASS, 1/1 CTest suite; `echelon-layout-ctest.log` |
| Identity, compile flags, deterministic FP flags, exact module exports | PASS, `check-echelon-boundaries.py` |
| Settings and local mod UI after relocation | PASS, five pages inspected; network syscall trace found no AF_INET calls or old catalog-cache reads; `echelon-layout-mod-ui.log`, `echelon-layout-settings.log` |
| Native alternating game sessions after relocation | PASS, 20 sessions, quiescence and window restoration; `echelon-layout-lifecycle.log` |
| Native resource trend | RSS growth after warmup 17,540 KiB, maximum 501,152 KiB; final threads/FDs 15/21. This is an observation, not proof of no leaks. |
| Unified Flatpak build and per-user install | PASS, approximately 14 MiB bundle; `echelon-flatpak-build.log`, `echelon-flatpak-install.log` |
| Installed Flatpak alternating sessions | PASS, 20 handoffs, bundled font, window restoration, 800x600 windowed / 1024x768 fullscreen; `echelon-flatpak-lifecycle.log` |
| Headless dispatch, both-game content precedence/tamper handling, supervisor crash recovery | PASS before directory relocation; `echelon-headless.log`, `echelon-content-stack.log`, `echelon-supervisor.log` |
| HTTPS resume, ETag restart, ignored range and multi-file fixtures | PASS before directory relocation; `echelon-download-resume.log` |
| Wayland explicit-sync protocol lifetime | SKIP: the test compositor does not advertise this protocol. The check still fails if the protocol is advertised but unused or its surfaces are unbalanced. |
| Two pre-existing local replays | FAIL: Generals exits 1; Zero Hour reports frame-0 CRC mismatch. Standalone binaries reproduce the same outcomes and the same ZH CRC. |
| Six public upstream Zero Hour captures | FAIL: all report frame-0 CRC mismatches with the available local retail data; `echelon-upstream-replay-*.log` |
| macOS product/standalone compilation and local-content tests | PASS in CI 33970289484. |
| macOS bundle closure after dylib-identity correction | PENDING: the preceding run rejected conflicting SDL copies; the owner deferred further validation and authorized immediate merge. |
| macOS gameplay and cross-platform CRC | NOT RUN: no macOS gameplay host available. |
| Synthetic next-upstream merge | PASS on relocation commit `a3b91d86f`: edits to upstream README/Core, a new root file and a deletion all map into `GeneralsX/`; product tree unchanged. `echelon-upstream-sync-probe.log` |
| Workspace registry and saved Codex paths | PASS, 20 projects, no errors |

Public replay fixtures came from `fbraz3/GeneralsXReplays` at `d00070157d1e8cd911927eca7499aed29dda0118`. They include Linux/macOS captures and their custom maps. The results do not establish whether differences originate in game data, capture version or shared engine code; the identical local standalone failure only narrows the host-specific hypothesis. **Replay compatibility and cross-platform determinism remain unverified.** A matched asset manifest and capture-engine baseline are needed before declaring those gates passed. No CRC check has been weakened.

## Packaging, CI and subsequent updates

Linux uses the tested unified Flatpak. macOS packaging assembles one app, resolves and rewrites dylib dependencies, includes Vulkan/MoltenVK and redistributable fonts, and verifies ad-hoc signing before writing the ZIP. It still needs execution on macOS; Developer ID signing and notarization are not part of this draft.

CI now uses the relocated paths and unified product artifacts, while standalone targets remain buildable. The optional standalone Linux workflow keeps gzip packaging; old AppImage/deploy scripts remain inside the upstream reference tree. Replay jobs are explicitly skipped when `ASSETS_KEY` is unavailable, with a summary that CRC is unverified. The repository had no configured Actions secrets during this work.

Future updates use [the subtree sync guide](../../HOWTO/SYNC_GENERALSX_UPSTREAM.md). `scripts/qa/check-echelon-upstream-sync.py` checks upstream edit/add/delete mapping against synthetic commits without changing refs, the index or the checkout. Root product files must remain outside incoming upstream changes. Review conflicts by behavior and preserve the ordinary directory arrangement.

The owner resumed the product merge after approving the two permanent branches. Known replay and macOS validation limits remain recorded and do not imply release readiness. No release publication is requested.

## Pre-merge review corrections

The automated review on `0f8858a79` identified three valid file-handling problems. The retail import path has been changed from moves to verified copies, preserving source installations, existing destination content and copied conflicts. Copy behavior is shared by both games and covered by source-retention, repeat-import, conflict, symlink and overlapping-path regressions.

The shared content-layer runtime now enumerates logical names and resolves directory components case-insensitively. This preserves the INI loader's root-before-subdirectory order and makes lowercase Windows-mod directories visible on Linux. The incremental product build, CTest regressions, module boundaries and both-engine content-stack smoke check pass. Runtime fixtures now come explicitly from `~/.Echelon`; no old directory is recreated or migrated by QA.

The first macOS CI run exposed a stale SagePatch SDL include path after relocation. The target now consumes `SDL3::Headers`, and configured SagePatch builds are a dependency of the launcher. The local SagePatch target passes; macOS CI must validate its platform-specific sources and packaging.

The focused review identified oversized BIG entry paths and missing nested archive discovery. Import validation and the native engine now share a 1023-byte path limit; the engine also bounds reads and rejects truncated names independently. Managed layers recursively discover BIG/GIB files without following directory symlinks. Boundary import tests and both-engine nested-archive/precedence smoke tests pass. These corrections apply to the shared native backend used by Generals and Zero Hour.

CI 33967112215 passed Linux Flatpak, both Windows targets and both standalone macOS targets. The unified macOS target then exposed an independent C++ standard inheritance problem: its product targets compiled as C++14. Both now explicitly require C++17, including the local-content test target. The raw legacy `-mod` path also passes both-engine oversized/unterminated BIG rejection tests without relying on the installer.

CI 33968601862 passed all compilation targets and macOS local-content tests. Application packaging then rejected a second SDL3 from Homebrew, which DXVK had discovered separately. DXVK now receives a private generated pkg-config description of the exact CMake SDL3 target and waits for that library before configuring; stale Meson dependency caches are cleared. The conflict guard remains enabled. A local CMake/Meson fixture with a conflicting system package confirms selection and runtime use of the intended SDL library (`echelon-sdl-bridge-check.log`).

The macOS packager also distinguishes `LC_ID_DYLIB` (the current library identity) from load dependencies. It no longer searches system paths for a dylib itself. Portable recorded-output regressions cover both dylib identities and executable/module dependencies; actual dependency traversal and conflicting-library rejection remain enforced.

On 2026-09-06 the owner explicitly authorized immediate merge without additional checks. The final dylib-identity correction has two passing portable regressions; macOS application packaging must be revalidated in later work. No release is being published.
