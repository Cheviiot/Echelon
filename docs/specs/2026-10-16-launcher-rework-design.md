# Echelon Launcher Rework — Design Spec

Date: 2026-10-16
Status: approved design (interactive session), pending implementation plan
Workflow: single product branch — all work lands on `main`; the pristine
`upstream` mirror stays the only separate branch (owner directive).

## Context and motivation

The current universal launcher is a single-pass monolith: `LauncherMain.cpp`
(4339 lines) contains the main loop, hand-rolled immediate-mode UI, INI
parsing, file dialogs, retail data migration, Vulkan ICD filtering, audio
synthesis, 15 `--internal-test-*` flags, the process supervisor and engine
session orchestration. Base utilities are duplicated across files
(`ToLower` x9, `Trim` x7, three diverging `IsInsideRoot` variants, six ad-hoc
INI parsers). ~2000 lines of networking (Downloads/S3/Repositories,
hand-rolled AWS SigV4) are dead in the product. Errors flow through two
channels that disagree; concurrency is `std::async` polled per frame.

The owner directed a full rewrite: keep what works, delete the rest, no
transitionary coexistence of two implementations in the tree.

Well-designed parts that must survive: content-pipeline crash-safety
(staging -> sha256 verify -> atomic publish -> trash -> recovery journals),
workspace isolation, engine ABI contracts (V2/V3, quiescence), supervisor
recovery, the `OperationDiagnostic` idea.

## Decisions (approved interactively)

| # | Decision | Choice | Rationale |
|---|---|---|---|
| A | UI stack | **Dear ImGui** (pinned via FetchContent, official SDL3 backend) | Rich widgets out of the box (tables, cards, progress); header-only, cheap in Flatpak; best agent-maintainability; SDL_Renderer3 backend fits the existing window handoff. Design-system layer gives the Echelon look. |
| B | Rewrite strategy | **Direct rewrite on `main`** | Owner directive: one branch, no long transitionary coexistence. New layers land with tests while the old `echelon_launcher` still builds; then the new app shell takes over the `echelon_launcher` target and the old `Launcher/*.cpp` files are deleted in the same final step. Until that step the old binary keeps shipping. |
| C | Architecture | **Six layers, strict downward deps** | See below. |
| D | Networking | **Deleted entirely** | Catalog service retired by owner; dead code with a hand-rolled crypto attack surface; recoverable from git history if online distribution ever returns. curl/yaml-cpp leave the launcher link line. Localization generation moves to local types. |

## Architecture

```
App/     main(), CLI contract, screen routing, supervisor wiring, markers
Ui/      ImGui design system, widgets, screens, localization, resources
Engine/  module loading, SessionHost (V2+V3), PresentationBridge, Supervisor
Content/ install/migrate/recover operations on a task queue
Domain/  settings, profiles, manifests, selection, workspace, paths, locks
Core/    Result/errors, Logger, strings, INI document, file utils, digests
```

Dependencies point strictly downward: `App -> Ui -> {Domain, Content,
Engine}`; `Content -> Domain -> Core`; `Ui` and `Engine` do not know each
other. Only `App` wires everything.

### Core (std + OpenSSL only; no SDL, no network)

- `Result<T>` + `OperationError` — the single error channel
  (stage/code/retryable; human message derived from the code).
- `Logger` — facade over `fprintf(stderr)` + `fflush` (release-build
  requirement), component tags, levels.
- `StringUtils` — one `ToLower`/`Trim`/`Split`/`Slugify`.
- `IniDocument` — the single INI parse/serialize engine for the whole
  launcher.
- `FileUtils` — one `IsInsideRoot`, atomic write (temp + rename + fsync),
  SHA-256 file digest, case-insensitive path helpers.
- `FsLock` — directory locks with owner token (ported from `LauncherLocks`).

### Domain (pure logic; no SDL, no threads)

- `Settings` + transactional publication (`Settings.v3.ini`,
  `SchemaVersion=3` — on-disk format unchanged).
- `LaunchProfiles` — exactly `generals`/`zerohour` (`LauncherProfileV1`).
- `ModCatalog` — manifests, semver comparison, layer fingerprints.
- `Selection` — mod/patch/addon stack resolution.
- `Workspace` — layer materialization (hardlink/copy), generations,
  fingerprint re-verification.
- `Paths` — the entire `~/.Echelon` layout in one place.
- `Localizer` — EN/RU string table outside code.

### Content (operations)

- `TaskQueue` — one worker thread; explicit states
  `queued/running/cancelling/done/failed`; progress struct; cancellation
  token. No `std::async` polling in the frame loop; shutdown joins with a
  timeout.
- `InstallOp` / `MigrationOp` / `Recovery` — crash-safety semantics ported
  unchanged: staging -> `files.sha256` -> verify -> atomic rename ->
  `.trash` -> `operation.ini` journals -> recovery at startup.
  libarchive + OpenSSL via Core.

### Engine (hosting)

- Module loading via `SDL_LoadObject`.
- `SessionHost` — V2 and V3 behind one interface; V3 is frame-pumped; env/cwd
  mutation only inside the scoped V2 compatibility bridge.
- `PresentationBridge` — no per-frame logging.
- `Supervisor` — worker process, 3 recoveries/min limit, restart markers.
- ABI header stays in `EngineIntegration/` (ownership boundary).

### Ui (ImGui + SDL3)

- `DesignSystem` — fonts, Echelon palette, ImGui style setup.
- `Widgets` — mod cards, setting rows, lists with stable `##id` discipline.
- `Screens` — Main / Settings / Mods / Install+Progress / Dialogs; each a
  class with own state and `handleEvent`/`render`.
- `Resources` — cover texture cache, sounds.

### App

- CLI contract unchanged: `--launcher`, `--profile=`, `--mod/--patch/--addon`,
  `--no-mods`, `-headless`, `-quickstart`, `-noshellmap`, engine argument
  pass-through, and all `--internal-test-*` flags used by smoke tests.
- `Markers` — every stderr marker parsed by QA (`[CONTENT-STACK]`,
  `[CONTENT-LAYER]`, `[WINDOW-HANDOFF]`, `[WINDOW-MODE]`, `[WORKER-RESOURCES]`,
  `[PRESENTATION-BRIDGE]`, quiescence line, supervisor lines) as `constexpr`
  in one file.

## Hard contracts (unchanged by this rework)

- On-disk data layout under `~/.Echelon` and all file formats.
- CLI flags and stderr markers (see `Markers` above).
- Engine ABI V2 + V3, quiescence mask `0x0000007f`, exactly two module
  exports.
- Retired mod catalog stays retired: no repository/download/S3 code, no
  network calls from the mod UI.
- Product identity, packaging presets, output/install layout.
- Determinism rules, FP flags, WWMath wrappers untouched.

## Testing

- Per-layer ctest targets for Core/Domain/Content (headless, no SDL where
  possible). The ctest name `echelon_local_content` is preserved for
  smoke-script compatibility.
- UI/engine parity validated with the existing Wayland smoke suite
  (screenshots, supervisor recovery, content stack, headless dispatch,
  Flatpak).
- Old `LauncherSettingsTests.cpp` fixtures serve as the behavioral
  specification for the Domain ports.

## Explicitly out of scope

- The MiniAudio-vs-OpenAL question raised by upstream #358 (the sync is now
  merged; the conflict must be resolved separately on `main` — macOS CI is
  red until then).
- Any change to engine gameplay code, determinism, or the ABI.
- Online mod distribution (retired); update-checker UI (decide later).

## Risks

- Smoke tests depend on exact stderr strings and `--internal-test-*` flags;
  the `Markers` file and unchanged CLI keep that contract stable.
- No local build environment on the maintainer workstation right now
  (missing `ubuntu-dev` Distrobox); validation relies on CI until it is
  restored. Note: macOS CI jobs are failing on `main` after the upstream
  sync (MiniAudio unification / Core restructure overlap) and must be fixed
  alongside the rework.
- ImGui look needs deliberate design-system work to avoid a "debug tool"
  appearance.
