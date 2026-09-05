# Generals: Arsenal Launcher

## Overview

`GeneralsArsenal` is one SDL3 application with two private engine modules:

- Command & Conquer: Generals
- Command & Conquer: Generals — Zero Hour

The launcher owns SDL, Vulkan, and the application window. It releases its renderer before starting an engine module, then restores the launcher UI in the same window when the game chooses **RETURN TO ARSENAL**. The normal game exit command still terminates the application.

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
~/.GeneralsArsenal/
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

## Native mod manager

**MODS** opens the native full-window manager with independent Generals/Zero Hour and Mods/Patches/Addons filters. Normal launcher sessions read only the centrally administered Generals: Arsenal catalog. Installed content and the last valid catalog cache remain available offline, while a manual **REFRESH** bypasses the normal 24-hour cache lifetime. `Mods/Repositories.ini` is deliberately ignored so an old configuration cannot silently restore a foreign catalog.

The catalog is data-driven. Publishing, updating, or removing a mod, patch, or add-on does not require a launcher source change, rebuild, or Flatpak update. A running release discovers the new catalog snapshot on **REFRESH**, or automatically after the cached snapshot reaches its 24-hour lifetime. Parent IDs connect patches and add-ons to their primary mod. Optional `Requires` and `Conflicts` selectors express the rest of the compatibility graph without any hard-coded launcher list. A selector can name any source-qualified item or pin its exact version as `source-id:item-id@version`. Arsenal refuses incomplete and mutually exclusive stacks before VFS activation and repairs stale saved profiles deterministically.

Selecting a downloadable catalog card changes the lower action to **INSTALL**. Every archive and cover is downloaded from the same fork-owned GitHub Releases origin as the catalog. Content-addressed release assets provide stable byte ranges, strong ETags, exact sizes, and mandatory SHA-256 values for resumable staging. The launcher contains no repository credentials; maintainers publish through an authenticated GitHub CLI session.

**ADD FROM FILES** accepts multiple BIG, ZIP, 7z, and RAR packages. **ADD FOLDER** imports an already unpacked modification or legacy directory through the same installation API. Every operation follows the same transaction:

```text
download/import -> Mods/.staging -> validation -> safe extraction
                -> files.sha256 -> atomic publication
```

Archive paths cannot escape staging. Symlinks, hardlinks, devices, archive bombs, and executable components such as `.exe`, `.dll`, `.so`, `.dylib`, and `.asi` are rejected. A failed update leaves the prior version intact. **REMOVE** moves an installed version to `Mods/.trash` instead of deleting it immediately.

Click an installed mod or patch to make it active. Add-ons are independent toggles; their active order is the VFS precedence order. Drag one active add-on onto another to move it, or use `Shift+Up` and `Shift+Down` from the keyboard. Selecting **Original Game** clears the managed primary mod, patch, and dependent add-ons without affecting the top-level vanilla launch buttons.

The details panel provides the following maintenance actions:

- **INSTALLED VERSION** cycles side-by-side installed versions without deleting the old version;
- **VERIFY** rebuilds and checks the complete SHA-256 index before launch;
- **CHANGE IMAGE** stores a private cover override outside the read-only content tree;
- **OPEN FOLDER** opens a dedicated menu for the selected modification, game data, maps, and replay directories. Missing maps and replay directories are created before opening;
- **OPEN LINK** opens a dedicated menu for the trusted HTTPS ModDB, Discord, news, and support links published by the catalog. Unavailable links remain visibly disabled.

Cards with a genuinely newer catalog version flash ten times and expose **UPDATE**. Version components are compared numerically, so `2.10` is newer than `2.9`, while an older catalog entry is never presented as an update. **RESTORE** atomically returns the newest valid removed version from `.trash`; incomplete interrupted imports are retained for diagnosis but are never offered as restorable installations.

The selected stack is saved independently for Generals and Zero Hour in `Mods/Profiles/<engine>.ini`. Arsenal reconciles these profiles after installation, removal, or recovery: a missing primary mod clears its dependent patch and add-ons, while compatible global add-ons remain selected. The lower **LAUNCH** action always resolves and verifies the complete saved stack before entering an engine.

Installed V2 content uses this layout:

```text
~/.GeneralsArsenal/Mods/Installed/<engine>/<type>/<id>/<version>/
├── manifest.ini
├── files.sha256
├── cover.image              # optional
└── content/
```

Older `ModificationManifestV1` files are read and migrated by atomically publishing a sibling V2 manifest without overwriting the legacy source. Source-qualified IDs remain in the installation schema so local imports and historical manifests cannot collide with centrally published IDs.

The built-in catalog endpoint is `https://github.com/Cheviiot/GeneralsArsenalRepository/releases/latest/download/catalog.json`. Developers may temporarily replace it with the `GENERALS_ARSENAL_REPOSITORY_URL` environment variable, but only an HTTPS `RepositoryCatalogV1` endpoint is accepted. Catalog IDs are path-safe, packages and covers must share the `github.com` catalog origin, duplicate releases are rejected, and every package requires a non-zero size and full SHA-256 value. GitHub redirects release assets to its download service after validation; libcurl follows that HTTPS redirect while preserving resumable Range requests and strong `If-Match` protection. The old GenLauncher YAML reader is retained only for offline migration tests and is never contacted by a normal launcher session.

Installed modifications are launched through the ABI V2 read-only content overlay. Arsenal rebuilds every layer's SHA-256 content index immediately before launch, validates root BIG directories before the legacy parser sees them, and refuses modified, truncated, unsafe, or executable-bearing packages. Both `.big` and GenLauncher-style `.gib` files are mounted directly as BIG archives; no compatibility symlink or retail-file rename is required. Retail files are never renamed, replaced, or copied into a modification.

## Rise of the Reds onboarding

Rise of the Reds is modeled as a Zero Hour primary mod with explicit, independently versioned patch branches and add-ons. The repository keeps a non-published candidate graph for ROTR 1.87 Public Build 2.0, HanPatch 32.2, AntiThesis 0.7, ROTR Navy 2.0.3, and their known add-ons. Mutually exclusive patch branches are declared as conflicts, while patch-specific add-ons require the exact compatible patch version. This prevents combinations such as ROTR Navy plus HanPatch from reaching the engine.

Candidate metadata never appears in the public launcher catalog and contains no third-party package or download URL. A candidate becomes installable only after its author approves redistribution, the maintainer imports an approved archive, and `repoctl` verifies its size, SHA-256, safe contents, and compatibility graph. Add-ons that require executable runtime injection remain blocked because Arsenal does not execute third-party binaries.

Launcher preferences are stored in `Launcher/Settings.ini`, including the launcher display mode and per-engine localization selection. Engine settings remain in their native locations:

```text
UserData/Generals/Options.ini
UserData/Generals/SagePatch.ini
UserData/GeneralsZH/Options.ini
UserData/GeneralsZH/SagePatch.ini
```

Generals: Arsenal does not inspect, migrate, or modify `$HOME/.GeneralsX`, `$HOME/GeneralsX`, or `GENERALSX_*` environment variables. To reuse files from another installation, explicitly select its game-data directory in the launcher. Canceling the picker returns to the selector and leaves unavailable profiles disabled.

## Command line

Show the selector explicitly:

```bash
GeneralsArsenal --launcher
```

Launch a built-in profile directly:

```bash
GeneralsArsenal --profile=generals -win
GeneralsArsenal --profile=zerohour -win
```

Both engines support the shared quick-start switch, which skips startup movies, logos, and window animations while retaining the live shell map:

```bash
GeneralsArsenal --profile=generals -quickstart
GeneralsArsenal --profile=zerohour -quickstart
```

Add `-noshellmap` or enable **Disable shell map** for the selected profile only when a static menu background is preferred.

All unrecognized arguments are forwarded to the selected engine unchanged. Headless and replay runs should select a profile and bypass the graphical selector:

```bash
GeneralsArsenal --profile=zerohour -headless -replay example.rep
```

Launch an installed, verified stack explicitly:

```bash
GeneralsArsenal --profile=generals --mod=community:example@1.0 \
  --patch=community:example-patch@1.1 \
  --addon=community:music@2.0 --addon=local:maps@1.0
```

Layer order is always base game, one primary mod, at most one patch, then add-ons in command-line or saved UI order. A managed stack cannot be combined with the legacy engine `-mod` argument. `--no-mods` explicitly selects vanilla content. Headless and replay launches never inherit a stack selected in the graphical manager; they require explicit modification arguments.

There are no `GeneralsX` or `GeneralsXZH` launcher aliases.

## Private engine ABI

The host and both modules use the versioned C ABI declared in `GeneralsArsenalLauncher/EngineModuleAPI.h`. A module exports exactly one entry point:

```text
GeneralsArsenal_GetEngineModuleV2
```

The ABI includes the shared SDL window, game arguments, game-data and user-data paths, selected profile, and an ordered array of `GeneralsArsenalContentLayerV1` records. Each record carries its type, source-qualified ID, version, canonical read-only root, priority, and content fingerprint. Loose files resolve from the highest layer first; root BIG files mount in ascending priority with verified overwrite precedence. The engine clears the overlay before reporting the content-layer quiescence bit and returning `ReturnToLauncher`, `ExitApplication`, or `FatalError`.

## Return and recovery model

The graphical application uses two process roles:

- a windowless supervisor, which owns no SDL or Vulkan objects;
- a UI worker, which owns SDL, the shared window, the launcher renderer, and both loaded engine modules.

Normal **RETURN TO ARSENAL** transitions remain in the UI worker and preserve the same `SDL_Window`. Before the launcher renderer is recreated, the engine module must report every required quiescence condition: the game engine and frame pacer are deleted, all registered subsystem singleton pointers are cleared, WW3D and DX8 are shut down, and the window is detached from the engine. The launcher refuses to reuse the window if any condition is missing.

A fatal Wayland or Vulkan error can invalidate an entire client connection and cannot be repaired safely in that process. In this case the UI worker exits with a recovery status and the supervisor creates a clean launcher worker. Repeated failures are limited to three recoveries per minute to prevent an endless crash loop. Headless and replay invocations bypass the supervisor and graphical launcher.

## Building

Configure both branches and the launcher target:

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target generals_arsenal_launcher
```

The output directory contains:

```text
build/linux64-deploy/GeneralsArsenal/
├── GeneralsArsenal
├── libGeneralsArsenalGeneralsEngine.so
└── libGeneralsArsenalZeroHourEngine.so
```

The upstream-compatible monolithic targets `g_generals` and `z_generals` remain available for engine debugging.

## Lifecycle QA

The graphical tests create a private headless Mutter/Wayland session and never connect to the developer's desktop display:

```bash
# Twenty alternating sessions, starting with each engine in turn.
./scripts/qa/smoke/test-generals-arsenal-lifecycle-wayland.sh linux64-deploy 20 30 generals
./scripts/qa/smoke/test-generals-arsenal-lifecycle-wayland.sh linux64-deploy 20 30 zerohour

# Abrupt UI-worker loss and automatic supervisor recovery.
./scripts/qa/smoke/test-generals-arsenal-supervisor-wayland.sh linux64-deploy

# Direct non-graphical dispatch for both profiles.
./scripts/qa/smoke/test-generals-arsenal-headless-dispatch.sh linux64-deploy 30

# Verified mod -> patch -> ordered add-ons, BIG precedence, tamper rejection, and VFS teardown.
./scripts/qa/smoke/test-generals-arsenal-content-stack.sh linux64-deploy 30

# The actually installed Flatpak, its packaged font, and ten handoffs.
./scripts/qa/smoke/test-generals-arsenal-flatpak-wayland.sh 10 20
```

The lifecycle test validates the complete quiescence mask after every session and samples resident memory, threads, and file descriptors. It fails on unbounded growth or a Wayland protocol error.

The settings persistence layer also has a standalone target that does not start a game or graphical session:

```bash
cmake --build build/linux64-deploy --target generals_arsenal_settings_tests
build/linux64-deploy/Core/GameEngineDevice/Source/GeneralsArsenalLauncher/generals_arsenal_settings_tests
```

## Flatpak

Build the single application bundle:

```bash
# One-time setup inside the dev-ubuntu Distrobox:
sudo apt-get install flatpak-builder

./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Arsenal
flatpak --user install -y build/GeneralsArsenal-linux64-deploy.flatpak
flatpak run io.github.cheviiot.GeneralsArsenal
```

After one successful online build has populated `.flatpak-builder`, the same pinned sources can be rebuilt without network access:

```bash
GENERALS_ARSENAL_FLATPAK_OFFLINE=1 \
  ./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Arsenal
```

The sandbox receives access to `$HOME/.GeneralsArsenal` and does not receive explicit access to old GeneralsX roots.

## Localization

The launcher selects Russian or English strings from the system locale and reports detected Russian `.big` localization files. Users can suppress those optional archives independently for Generals and Zero Hour without changing the files on disk. Generals: Arsenal does not distribute files from GeneralsRussianLoca or any retail installation.
