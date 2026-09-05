# Echelon Launcher

## Overview

`Echelon` is one SDL3 application with two private engine modules:

- Command & Conquer: Generals
- Command & Conquer: Generals — Zero Hour

The launcher owns SDL, Vulkan, and the application window. It releases its renderer before starting an engine module, then restores the launcher UI in the same window when the game chooses **RETURN TO ECHELON**. The normal game exit command still terminates the application.

Retail game assets and third-party localization files are not included.

## Settings

The launcher's **OPTIONS** screen maintains independent settings for Generals and Zero Hour. Each game page contains:

- windowed launch, quick start, shell-map control, and additional engine arguments for both games;
- resolution, particles, texture detail, shadows, props, animation, water, heat, and mouse options from `Options.ini`;
- safe camera height, scroll speed, terrain distance, and frame-rate controls from `SagePatch.ini`.

The launcher never patches a retail `.big` archive to change camera height. It writes the engine-supported `SagePatch.ini` overlay in each profile's user-data directory and preserves unrecognized lines in both INI files. Additional arguments are tokenized directly without invoking a command shell; unmatched quotes are rejected before saving.

Each game page also contains **Russian BIG localization**. Turning localization off prevents the VFS from loading the exact optional `00Russian.big` archive for Generals and both the base and expansion Russian archives for Zero Hour. Files are never renamed, deleted, or edited. The main Generals and Zero Hour buttons always launch the clean games; managed mod stacks belong exclusively to **MODS**.

The **LAUNCHER** page contains only the interface language, persistent display mode, and window size. The launcher can start windowed or in desktop fullscreen mode. Its configured mode and dimensions are restored after every return from an engine. Retail data selection remains part of the automatic first-launch setup and is not exposed as an unrelated display preference.

Pressing **APPLY** publishes launcher, Generals, and Zero Hour settings as one recoverable transaction. If the process stops during publication, the next launcher start restores the previous complete set before reading any settings. **CANCEL** discards the in-memory draft, and **DEFAULTS** resets only the currently selected game or launcher page.

## Data layout

The application creates only the following root:

```text
~/.Echelon/
├── Generals/
├── GeneralsZH/
├── UserData/
│   ├── Generals/
│   └── GeneralsZH/
├── Profiles/
├── Mods/
└── Launcher/
```

`Profiles/generals.ini` and `Profiles/zerohour.ini` use the `LauncherProfileV1` schema. The current release accepts only the two built-in profile IDs.

## Local mod manager

The **MODS** screen manages installed mods, patches, and add-ons independently for Generals and Zero Hour. There is no online catalog, refresh button, update indicator, or catalog request at startup. The owner retired the catalog service; future mod distribution requires a separate design.

**ADD FROM FILES** accepts BIG, ZIP, 7z, and RAR packages. **ADD FOLDER** imports unpacked content. Installation is transactional:

```text
local archive/folder -> Mods/.staging -> validation -> safe extraction
                     -> files.sha256 -> atomic publication
```

Imports reject paths escaping staging, symbolic/hard links, device files, oversized expansion, and executable content. Retail archives are never renamed or overwritten. Failed imports leave existing versions intact. **REMOVE** moves content to `.trash`; **RESTORE** returns the newest valid removed version.

Click a mod or patch to activate it; toggle add-ons independently. Drag active add-ons or use `Shift+Up` / `Shift+Down` to change their order. **Original Game** clears the managed stack. **INSTALLED VERSION** switches installed versions, **VERIFY** checks SHA-256, **CHANGE IMAGE** sets a local cover, and **OPEN FOLDER** opens content or user-data folders. **OPEN LINK** is available only for an explicit HTTPS source in local metadata.

Profiles live in `Mods/Profiles/<engine>.ini`. Missing or incompatible dependencies are rejected before launch. Removing a primary mod clears its dependent selections while preserving compatible global add-ons. Metadata retains source-qualified IDs, parent relationships, `Requires`, and `Conflicts`; no hard-coded list of mods is used.

```text
~/.Echelon/Mods/Installed/<engine>/<type>/<id>/<version>/
├── manifest.ini
├── files.sha256
├── cover.image              # optional local override
└── content/
```

Content remains read-only during gameplay. ABI V2 passes the verified layer stack to each engine. Later layers override earlier layers; loose files and BIG/GIB archives share precedence. Legacy manifests remain readable for explicit local imports; old application directories are never discovered or migrated automatically.

The generic HTTPS, resume, S3, and legacy catalog code is retained for isolated fixtures in `echelon_settings_tests`. It is not linked into the launcher. `ECHELON_REPOSITORY_URL` is an explicit test-library input and has no effect on the product UI.

Launcher preferences are stored in `Launcher/Settings.ini`, including the launcher display mode and per-engine localization selection. Engine settings remain in their native locations:

```text
UserData/Generals/Options.ini
UserData/Generals/SagePatch.ini
UserData/GeneralsZH/Options.ini
UserData/GeneralsZH/SagePatch.ini
```

Echelon does not inspect, migrate, or modify `$HOME/.GeneralsX`, `$HOME/GeneralsX`, or `GENERALSX_*` environment variables. To reuse files from another installation, explicitly select its game-data directory in the launcher. Canceling the picker returns to the selector and leaves unavailable profiles disabled.

## Command line

Show the selector explicitly:

```bash
Echelon --launcher
```

Launch a built-in profile directly:

```bash
Echelon --profile=generals -win
Echelon --profile=zerohour -win
```

Both engines support the shared quick-start switch, which skips startup movies, logos, and window animations while retaining the live shell map:

```bash
Echelon --profile=generals -quickstart
Echelon --profile=zerohour -quickstart
```

Add `-noshellmap` or enable **Disable shell map** for the selected profile only when a static menu background is preferred.

All unrecognized arguments are forwarded to the selected engine unchanged. Headless and replay runs should select a profile and bypass the graphical selector:

```bash
Echelon --profile=zerohour -headless -replay example.rep
```

Launch an installed, verified stack explicitly:

```bash
Echelon --profile=generals --mod=community:example@1.0 \
  --patch=community:example-patch@1.1 \
  --addon=community:music@2.0 --addon=local:maps@1.0
```

Layer order is always base game, one primary mod, at most one patch, then add-ons in command-line or saved UI order. A managed stack cannot be combined with the legacy engine `-mod` argument. `--no-mods` explicitly selects vanilla content. Headless and replay launches never inherit a stack selected in the graphical manager; they require explicit modification arguments.

There are no `GeneralsX` or `GeneralsXZH` launcher aliases.

## Private engine ABI

The host and both modules use the versioned C ABI declared in `LauncherIntegration/EngineModuleAPI.h`. A module exports exactly one entry point:

```text
Echelon_GetEngineModuleV2
```

The ABI includes the shared SDL window, game arguments, game-data and user-data paths, selected profile, and an ordered array of `EchelonContentLayerV1` records. Each record carries its type, source-qualified ID, version, canonical read-only root, priority, and content fingerprint. Loose files resolve from the highest layer first; root BIG files mount in ascending priority with verified overwrite precedence. The engine clears the overlay before reporting the content-layer quiescence bit and returning `ReturnToLauncher`, `ExitApplication`, or `FatalError`.

## Return and recovery model

The graphical application uses two process roles:

- a windowless supervisor, which owns no SDL or Vulkan objects;
- a UI worker, which owns SDL, the shared window, the launcher renderer, and both loaded engine modules.

Normal **RETURN TO ECHELON** transitions remain in the UI worker and preserve the same `SDL_Window`. Before the launcher renderer is recreated, the engine module must report every required quiescence condition: the game engine and frame pacer are deleted, all registered subsystem singleton pointers are cleared, WW3D and DX8 are shut down, and the window is detached from the engine. The launcher refuses to reuse the window if any condition is missing.

A fatal Wayland or Vulkan error can invalidate an entire client connection and cannot be repaired safely in that process. In this case the UI worker exits with a recovery status and the supervisor creates a clean launcher worker. Repeated failures are limited to three recoveries per minute to prevent an endless crash loop. Headless and replay invocations bypass the supervisor and graphical launcher.

## Building

Configure both branches and the launcher target:

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher
```

The output directory contains:

```text
build/linux64-deploy/Echelon/
├── Echelon
├── libEchelonGeneralsEngine.so
└── libEchelonZeroHourEngine.so
```

The upstream-compatible monolithic targets `g_generals` and `z_generals` remain available for engine debugging.

## Lifecycle QA

The graphical tests create a private headless Mutter/Wayland session and never connect to the developer's desktop display:

```bash
# Twenty alternating sessions, starting with each engine in turn.
./scripts/qa/smoke/test-echelon-lifecycle-wayland.sh linux64-deploy 20 30 generals
./scripts/qa/smoke/test-echelon-lifecycle-wayland.sh linux64-deploy 20 30 zerohour

# Abrupt UI-worker loss and automatic supervisor recovery.
./scripts/qa/smoke/test-echelon-supervisor-wayland.sh linux64-deploy

# Direct non-graphical dispatch for both profiles.
./scripts/qa/smoke/test-echelon-headless-dispatch.sh linux64-deploy 30

# Verified mod -> patch -> ordered add-ons, BIG precedence, tamper rejection, and VFS teardown.
./scripts/qa/smoke/test-echelon-content-stack.sh linux64-deploy 30

# The actually installed Flatpak, its packaged font, and ten handoffs.
./scripts/qa/smoke/test-echelon-flatpak-wayland.sh 10 20
```

The lifecycle test validates the complete quiescence mask after every session and samples resident memory, threads, and file descriptors. It fails on unbounded growth or a Wayland protocol error.

The settings persistence layer also has a standalone target that does not start a game or graphical session:

```bash
cmake --build build/linux64-deploy --target echelon_settings_tests
build/linux64-deploy/Launcher/echelon_settings_tests
```

## Flatpak

Build the single application bundle:

```bash
# One-time setup inside the ubuntu-dev Distrobox:
sudo apt-get install flatpak-builder

./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Echelon
flatpak --user install -y build/Echelon-linux64-deploy.flatpak
flatpak run io.github.cheviiot.Echelon
```

After one successful online build has populated `.flatpak-builder`, the same pinned sources can be rebuilt without network access:

```bash
ECHELON_FLATPAK_OFFLINE=1 \
  ./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Echelon
```

The sandbox receives access to `$HOME/.Echelon` and does not receive explicit access to old GeneralsX roots.

## Localization

The launcher selects Russian or English strings from the system locale and reports detected Russian `.big` localization files. Users can suppress those optional archives independently for Generals and Zero Hour without changing the files on disk. Echelon does not distribute files from GeneralsRussianLoca or any retail installation.
