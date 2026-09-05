# Synchronizing GeneralsX Upstream

## Purpose

Echelon is maintained as a small branding, launcher, and packaging overlay on top of [fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX). Updates are merged through dated review branches. Never replace the repository with a source archive, rebase published Echelon history, or apply blanket `ours`/`theirs` conflict resolution.

## One-time repository setup

Verify the remotes and enable recorded conflict resolution:

```bash
git remote get-url origin
git remote get-url upstream
git config rerere.enabled true
git config rerere.autoupdate true
```

Expected remotes:

```text
origin    https://github.com/Cheviiot/Echelon.git
upstream  https://github.com/fbraz3/GeneralsX.git
```

## Prepare a synchronization branch

Start with a clean tree. Update the fork and inspect upstream before merging:

```bash
git switch main
git pull --ff-only origin main
git fetch --prune upstream
git log --oneline --no-merges main..upstream/main
git diff --stat main...upstream/main
git switch -c generalsx-sync-MM-DD-YYYY
git merge --no-ff upstream/main
```

Replace `MM-DD-YYYY` with the merge date. Record the upstream head SHA, commit count, touched subsystems, and expected risk areas in the pull request.

## Conflict policy

Resolve each conflict from its intent:

- Accept upstream engine, gameplay, common-library, platform, determinism, and safety fixes unless there is a documented Echelon incompatibility.
- Preserve the Echelon launcher, `Echelon` public executable, private Echelon ABI, `.Echelon` data root, application ID, logo, and packaging.
- If upstream changes its own launcher or packaging, port the functional change into the Echelon layer without restoring GeneralsX public identifiers.
- Keep `Generals/`, `GeneralsMD/`, `Core/`, `g_generals`, and `z_generals` aligned with upstream names.
- Preserve historical `GeneralsX @...` annotations and attribution. Use `Echelon @...` only for new fork-owned changes.
- Never edit generated dependency trees under `build/_deps`.

The highest-risk files are the root CMake configuration, engine entry points, SDL/DXVK window ownership, audio teardown, global memory managers, Flatpak manifests, and launcher return hooks in both game branches.

After resolving each file, inspect the combined result before staging it:

```bash
git diff --check
git diff --merge
git add path/to/resolved-file
git status --short
```

## Required validation

Build inside the `ubuntu-dev` Distrobox environment:

```bash
project_root="$(pwd -P)"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.generalsx/vcpkg}"
distrobox enter ubuntu-dev -- bash -lc '
  cd "$1" &&
  cmake --fresh --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON &&
  cmake --build build/linux64-deploy --target echelon_launcher g_generals z_generals -j4
' bash "$project_root"
```

Then verify:

1. Both engine modules export only `Echelon_GetEngineModuleV2`.
2. Generals and Zero Hour start, return to Echelon, and can be alternated ten times.
3. Normal game exit terminates the process.
4. Linux replay/CRC tests still pass for retail replays.
5. Headless runs bypass the selector.
6. A clean HOME creates only `.Echelon`; old paths remain untouched.
7. The unified Flatpak builds and runs as `io.github.cheviiot.Echelon`.
8. RU/EN, 4:3, 16:9, 16:10, HiDPI, windowed, and fullscreen layouts remain usable.

## Pull request report

The synchronization pull request must include:

- upstream range and head SHA;
- number of incoming commits;
- summary by subsystem;
- every manually resolved conflict and the chosen intent;
- build, replay, ABI, Flatpak, and runtime results;
- known follow-up work or explicitly deferred upstream changes.

Merge the synchronization branch through review. Do not merge upstream directly into `main` and do not enable automatic upstream merges.
