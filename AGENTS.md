# Echelon — Instructions for AI Coding Agents

## What I Am
Echelon is a fork of GeneralsX that combines the Command & Conquer: Generals and Zero Hour engine branches behind one launcher for **Linux and macOS**. It preserves the GeneralsX cross-platform stack (SDL3 + DXVK + OpenAL + 64-bit) and keeps the engine in the tracked `GeneralsX/` directory close to upstream for regular reviewed merges.

## Key Entry Points
- `GeneralsX/GeneralsMD/Code/Main/WinMain.cpp`
- `GeneralsX/Generals/Code/Main/WinMain.cpp`
- `GeneralsX/Core/GameEngineDevice/Source/`

## Platform Focus
- **Active**: Linux (`linux64-deploy`), macOS (`macos-vulkan`)
- **Future/Exploratory**: Windows (MinGW path, issue #29)
- **Legacy**: VC6 + DirectX 8 + Miles (reference only)

## Architecture
| Layer   | Technology          | Replaces                     |
|---------|---------------------|------------------------------|
| Graphics| DXVK                | DirectX 8 (d3d8.dll)         |
| Windowing| SDL3              | Win32 API                    |
| Audio   | OpenAL (MiniAudio WIP)| Miles Sound System           |
| Video   | FFmpeg              | Bink Video (intro/videos)    |
| Platform| SDL3 + libc         | Win32 POSIX calls            |

**CRITICAL**: Engine platform code must be isolated to `GeneralsX/Core/GameEngineDevice/` and `GeneralsX/Core/Libraries/Source/Platform/`. Product-specific platform adapters belong in `EngineIntegration/Source/`. No native Win32/Cocoa/X11 calls in game logic.

## Golden Rules
1. **Single codebase** – Linux and macOS build from same source
2. **SDL3 everywhere** – No native platform calls in game code
3. **DXVK everywhere** – DX8 → Vulkan translation on all platforms
4. **OpenAL / MiniAudio Parity** – Cross-platform audio stack. Implementations and bug fixes in one MUST be replicated in the other to maintain strict feature parity.
5. **64-bit native** – Linux x86_64 and macOS ARM64 (32-bit VC6 is upstream reference only)
6. **Retail compatibility** – Original replays and mods must work
7. **Determinism** – Rendering/audio changes must not affect gameplay logic
8. **No band-aids** – Fix underlying issues, not symptoms
9. **Update worklog** – Update `docs/WORKLOG/YYYY-MM-DIARY.md` before committing (see [.github/instructions/docs.instructions.md](.github/instructions/docs.instructions.md) for details)
10. **Reference repos** – Study patterns, don't copy-paste
11. **Backport to Generals** – Bugfixes and improvements must be backported to the Generals base game.

## Cross-Platform Determinism (Mac vs Linux)
To guarantee cross-play between macOS ARM64 and Linux x86_64 without SyncCrash desyncs, you must obey the following rules:

1. **Deterministic Math** – The native math functions were replaced by deterministic multi-platform WWMath equivalents to maintain determinism. This pattern must be preserved. When syncing upstream, replace these functions:
    - `sqrt` / `sqrtf` -> `WWMath::SqrtOrigin` / `WWMath::SqrtfOrigin` or `WWMath::Sqrt`
    - `sin` / `sinf` -> `WWMath::SinTrig` or `WWMath::Sin`
    - `cos` / `cosf` -> `WWMath::CosTrig` or `WWMath::Cos`
    - `tan` / `tanf` -> `WWMath::TanTrig`
    - `acos` / `acosf` -> `WWMath::ACosTrig` or `WWMath::Acos`
    - `asin` / `asinf` -> `WWMath::ASinTrig` or `WWMath::Asin`
    - `atan` / `atanf` -> `WWMath::Atan`
    - `atan2` / `atan2f` -> `WWMath::Atan2Origin` or `WWMath::Atan2`
    - `ceil` / `ceilf` -> `WWMath::Ceil`
    - `floor` / `floorf` -> `WWMath::Floor`
    - `pow` -> `WWMath::PowOrigin`
2. **NaN/Inf Integer Casting** – ARM64 casts `(Int)NaN` to `0`, while x86 casts it to `INT_MIN`. Any division by potentially zero (e.g. `val / maxHealth`) whose result might be cast to an integer or affect simulation flow MUST be guarded (`if (maxHealth > 0)`) or use `WWMath::Div_FixNaN` to avoid immediate CRC desyncs.
3. **Double-Precision Isolation** – Do not allow implicit `double` promotion in transcendental/power math evaluations (e.g. `gm_sqrt`, `gm_pow`, `gm_atan`). x87 (24-bit mantissa) and NEON (53-bit mantissa) diverge by 1-ULP on double-precision. Ensure all `WWMath` wrappers explicitly accept/return `float` or internally downcast `double` to `float` prior to `gm_*f` execution to guarantee cross-platform bit-identical outputs.
4. **FPU Environment State Leaks** – Audio drivers (OpenAL/MiniAudio) and OS callbacks aggressively alter hardware FPU registers (e.g., Flush-To-Zero, Rounding mode) for DSP performance. If this state leaks into the main thread, the simulation math diverges. **Always inject `ScopedFPUGuard`** at the boundaries of `GameLogic::update()`, audio thread callbacks, and mouse-picking logic (e.g., `View::pickDrawable()`).
5. **FMA Contraction** – Fused Multiply-Add combines instructions with infinite intermediate precision, yielding different results on ARM64 vs x86_64. We strictly enforce `-ffp-contract=off` globally (and `/fp:precise` for MSVC). Never bypass this with `-ffast-math` or `/fp:fast`.
6. **Deep CRC Memory Buffer Logging** – When a sync crash occurs and the root cause isn't obvious, the game automatically dumps a `Debug/deep_crc_YYYY-MM-DD-HH-MM-SS.bin` file containing a binary snapshot of the last 64 frames of state transfers. Use `GeneralsX/scripts/qa/parse_deep_crc.py` to inspect these dumps and identify the exact object ID and state data that first diverged between players. Note: This requires the `RTS_BUILD_OPTION_DEEP_CRC=ON` CMake flag (which is enabled by default).

## Reference Repositories
- **fighter19-dxvk-port** – Primary graphics/platform reference (DXVK + SDL3 on Linux)
- **jmarshall-win64-modern** – Audio reference (OpenAL implementation, Generals-only)
- **thesuperhackers-main** – Upstream baseline for regression checks

## Build Commands

Run product builds from the repository root. On the maintainer's ALT workstation, use the existing `ubuntu-dev` Distrobox; do not install development dependencies on the host. See [the foundation report](docs/WORKDIR/reports/ECHELON_FOUNDATION.md) for the tested environment and fresh build directory.

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher echelon_settings_tests
ctest --test-dir build/linux64-deploy -R echelon_local_content --output-on-failure
./scripts/build/linux/build-linux-flatpak.sh linux64-deploy Echelon
```

For macOS, use `macos-vulkan` and `./scripts/build/macos/bundle-macos-echelon.sh macos-vulkan`. The bundle contains both engines. Standalone targets are `g_generals` and `z_generals`; configure `RTS_BUILD_UNIVERSAL_LAUNCHER=OFF` to verify independent engine compilation. Their output directories are `build/<preset>/GeneralsX/Generals` and `build/<preset>/GeneralsX/GeneralsMD`.

Scripts and build guides inside `GeneralsX/` retain upstream assumptions and serve as references. Echelon's maintained entry points are the root CMake presets and root product scripts.

## Target Priority
1. **GeneralsXZH** (Zero Hour) – Primary target, most feature-complete
2. **GeneralsX** (Base game) – Stable and functional. Bugfixes and improvements must be backported.

## Backport Rules
**Backport to Generals when:**
- Change is platform/backend code (SDL3, DXVK, OpenAL/MiniAudio) or a general bugfix/improvement
- Change is in shared Core libraries
- Change is low-risk and clearly applicable

**Do NOT backport:**
- Zero Hour-specific gameplay/logic
- Expansion-specific features
- High-risk changes to Zero Hour

## DXVK Source of Truth (macOS)
- Default: GitHub fork branch `generalsx-macos-v2.6` (auto-update enabled)
- Local mode: `-DSAGE_DXVK_USE_LOCAL_FORK=ON`
- **Rule**: Never edit files in `build/_deps/...` directly. Always commit fixes in fork repo first.

## Common Pitfalls
- **Linux case sensitivity**: Include paths must match exact case. Use `GeneralsX/scripts/tooling/cpp/maintenance/fixIncludesCase.sh`.
- **DXVK needs Vulkan**: Install `vulkan-tools`, `mesa-vulkan-drivers` or GPU drivers.
- **-logToCon only in debug**: Available only with `RTS_BUILD_OPTION_DEBUG=ON`.
- **SDL3 from source**: Fetched via CMake FetchContent. No system package needed.
- **Manual memory**: Always delete/delete[]. Use STLPort for VC6 legacy builds.
- **Debug options break replays**: Use `RTS_BUILD_OPTION_DEBUG=OFF` for replay tests.
- **Windows executable icons (.ico)**: Windows builds embed `GeneralsX/Generals/Code/Main/Generals.ico` and `GeneralsX/GeneralsMD/Code/Main/Generals.ico` via `RTS.RC`. If source PNG icon assets (`GeneralsX/assets/generalsx_icon.png` or `GeneralsX/assets/generalsx-zh_icon.png`) are updated, regenerate the multi-resolution `.ico` files (sizes 16, 24, 32, 48, 64, 128, 256) to keep Windows executables in sync.

## Testing & Validation

Run `python3 scripts/qa/check-echelon-boundaries.py --build build/<preset>` and the product scripts in `scripts/qa/smoke/`. See their headers for arguments. Runtime tests need explicit retail data fixtures. Never write into the owner's original game directories.

Test both profiles, local content precedence and verification, repeated engine switching, supervisor recovery, and packaged launch. Record unavailable checks as skipped and CRC failures as failures. Do not suppress replay mismatches or loosen deterministic math to make tests pass.

## Branching & Sync
### GeneralsX upstream sync

The active product upstream is `fbraz3/GeneralsX` (remote `upstream`). The two permanent repository branches are `main` (Echelon product) and `upstream` (locked, exact original history). Never commit product changes to `origin/upstream`; it retains the original root layout, while the product branch integrates the engine under `GeneralsX/`. Preserve history and integrate pinned commits on a `codex/` review branch. Follow [the sync guide](docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md) and [ownership map](docs/WORKDIR/reports/ECHELON_FOUNDATION.md).

- Review conflicts by behavior; do not apply directory-wide ours/theirs rules.
- Keep the shared engine layout close to upstream. Product code lives in `Launcher/`, the host bridge in `EngineIntegration/`.
- Validate hosted and standalone builds for both games. Only hosted variants receive `ECHELON_BRAND`, `ECHELON_ENGINE_HOSTED`, and `ECHELON_ENGINE_MODULE_ALLOCATOR`.
- The mod catalog service was retired by the owner. Do not recreate it or wire a replacement URL. Preserve local imports and isolated transport fixtures.
- Product identity is Echelon, application ID `io.github.cheviiot.Echelon`, data root `$HOME/.Echelon`. Do not migrate or discover old app roots automatically.
- TheSuperHackers remains a reference baseline; its changes normally arrive through GeneralsX.

## Code Conventions
- **Annotate new fork changes**: `// Echelon @keyword author DD/MM/YYYY Description`. Preserve historical `// GeneralsX @keyword ...` annotations.
- **Keywords**: `@bugfix` / `@feature` / `@performance` / `@refactor` / `@tweak` / `@build`
- **Attribution**: Add upstream PR references with author and GitHub URL
- **English only**: All code, comments, documentation
- **No lazy code**: No empty stubs, empty catch blocks, or commented-out code
- **Console Debug Logging**: For diagnostic/troubleshooting logs that must remain visible in release/non-debug builds (since `DEBUG_LOG` is disabled and compiled out in release builds), use `fprintf(stderr, ...)` paired with `fflush(stderr)` instead of `DEBUG_LOG` to output directly to the console.
  - ✗ *Wrong (compiled out in release builds)*:
    ```cpp
    DEBUG_LOG(("LanLobbyMenuInit - SetLocalIP ok %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(IP)));
    ```
  - ✓ *Right (will print to console in release/testing environments)*:
    ```cpp
    fprintf(stderr, "[LAN86] LanLobbyMenuInit SetLocalIP ok %d.%d.%d.%d\n", PRINTF_IP_AS_4_INTS(IP));
    fflush(stderr);
    ```

## GitHub PR/Issue Formatting
- Use `--body-file` with real Markdown file instead of `--body`
- Avoid literal `\n` sequences; prefer actual newlines in multi-line strings

## Git Commit Standards
- **Conventional Commits**: Format must be `<type>(optional scope): <description>`. (e.g. `fix(audio): restart sound groups in reset`)
- **DO NOT use `@`**: Neither commit subjects nor bodies may contain `@`. Inline source annotations are separate from commit messages.
- **Imperative mood**: Use "add", "fix", "change" (not "added" or "fixes").
- **Read the Docs**: For full details, valid types, and PR standards, you **MUST** read `.github/instructions/git-commit.instructions.md`.

## VS Code Tasks
- Prefer task-first execution for build/test/debug
- Logs captured to `logs/` directory
- Primary labels: `Echelon: Configure`, `Echelon: Build`, `Echelon: Test`

## Docs Workflow
1. Monthly diary in `docs/WORKLOG/YYYY-MM-DIARY.md` (YYYY=year, MM=month only, e.g., `2026-05-DIARY.md`), always including the standard AI-generated content disclosure note at the top
2. Active work notes in `docs/WORKDIR/` (phases/planning/reports/support/audit/lessons)
3. Step-by-step tutorials in `docs/HOWTO/` (user-facing guides for common tasks)
4. Never drop working docs directly under `docs/` root

## GitHub CLI Examples

> [!IMPORTANT]
> When running `gh` commands within the agent sandbox environment, if `GITHUB_TOKEN=github_pat_antigravitydummytoken` is present, it will override local credentials and cause `HTTP 401: Bad credentials`. Unset the dummy token by prepending `env -u GITHUB_TOKEN -u GH_TOKEN` to your `gh` commands.
> 
> Example: `env -u GITHUB_TOKEN -u GH_TOKEN gh pr create ...`

**Create issues:**
```bash
gh issue create \
  --title "Brief, actionable title" \
  --body "## Context\n...\n## Goal\n...\n## Acceptance Criteria\n..." \
  --label bug --label Linux
```

**Create PRs (use temp file for body):**
```bash
cat > /tmp/pr-body.md << 'EOF'
## Description
Fixes #123

## Changes
- Platform isolation
EOF
gh pr create --title "Description" --body-file /tmp/pr-body.md
```

**Verify PR body (check for literal \n):**
```bash
body=$(gh pr view <number> --json body --jq .body)
printf "%s" "$body" | rg '\\n' && echo "HAS_LITERAL_BACKSLASH_N=YES" || echo "HAS_LITERAL_BACKSLASH_N=NO"
```

## Build Presets Reference
- **linux64-deploy** – GCC/Clang x86_64, Release (PRIMARY LINUX)
- **linux64-testing** – Debug variant
- **macos-vulkan** – macOS ARM64, RelWithDebInfo (PRIMARY MACOS)
- **mingw-w64-i686** – MinGW cross-compile (exploratory)
- **vc6** – Visual Studio 6, 32-bit (legacy)
- **win32** – MSVC 2022, experimental

## Directories
- `GeneralsX/GeneralsMD/`: Zero Hour.
- `GeneralsX/Generals/`: base game.
- `GeneralsX/Core/`: shared libraries.
- `GeneralsX/references/`: fbraz3-dxvk
- `docs/WORKDIR/`: current work docs.
- `docs/HOWTO/`: user-facing step-by-step tutorials (SagePatch config, etc.)
- `logs/`: build/run/debug logs.

## Instruction Context Loading

The `.github/instructions/` files are scoped VS Code hints — they load only when the file path matches.

You MUST load the files below to your context when the file/dir you are working on matches the applyTo column pattern.

The `**` at applyTo means all files, you MUST load it everytime.

| Instruction File | applyTo | Purpose |
|---|---|---|
| [.github/instructions/git-commit.instructions.md](.github/instructions/git-commit.instructions.md) | `**` | Commit/PR message standards |
| [.github/instructions/cpp-conventions.instructions.md](.github/instructions/cpp-conventions.instructions.md) | `**/*.{cpp,h,hpp,c}` | Code style, annotations, platform isolation |
| [.github/instructions/build.instructions.md](.github/instructions/build.instructions.md) | `cmake/**,CMakeLists.txt,CMakePresets.json,GeneralsX/**/CMakeLists.txt,GeneralsX/cmake/**` | Build presets, DXVK source of truth |
| [.github/instructions/platform-linux.instructions.md](.github/instructions/platform-linux.instructions.md) | `scripts/build/linux/**,GeneralsX/scripts/build/linux/**` | Linux build notes |
| [.github/instructions/platform-macos.instructions.md](.github/instructions/platform-macos.instructions.md) | `scripts/build/macos/**,GeneralsX/scripts/build/macos/**,GeneralsX/references/fbraz3-dxvk/**` | macOS/DXVK build notes |
| [.github/instructions/docs.instructions.md](.github/instructions/docs.instructions.md) | `**/*.md` | Documentation structure and workflow |
| [.github/instructions/scripts.instructions.md](.github/instructions/scripts.instructions.md) | `scripts/**,GeneralsX/scripts/**` | Script organization and naming |

Update this table when instruction files are added, removed, or renamed.
