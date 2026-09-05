# Synchronize GeneralsX upstream

The product is maintained in the root of the Echelon repository. The integrated engine is an ordinary tracked `GeneralsX/` directory. Do not create a GeneralsX submodule, clone over this directory, or rewrite published history.

## Two permanent branches

- `origin/main`: Echelon, with our sources at the root and the integrated engine under `GeneralsX/`.
- `origin/upstream`: exact original GeneralsX history and layout, without any Echelon commits. This branch is locked against pushes, force pushes and deletion; GitHub fork syncing is enabled.

The remote named `upstream` points to `fbraz3/GeneralsX`; it is distinct from the branch `origin/upstream`. Use full remote-tracking ref names when comparing them.

Refresh the clean branch only from original GeneralsX commits. Before an update, verify that its current tip is an ancestor of the intended original revision. Afterward, verify SHA equality with the selected original commit; do not create a merge commit, patch, metadata commit or force push in the clean branch. If GitHub fork syncing cannot map the original source branch, stop and inspect the mapping instead of unlocking the branch or merging product code into it.

## Review a pinned update

Start from a clean working tree with all existing development preserved in commits. Verify the remotes, fetch the upstream history, and record the exact incoming SHA and range:

```bash
git remote -v
git fetch upstream main
git fetch origin main upstream
# Verify the clean mirror matches the chosen original revision.
git rev-parse refs/remotes/upstream/main refs/remotes/origin/upstream
# Start only after the Echelon foundation has been merged into origin/main.
git switch -c codex/upstream-sync-YYYY-MM-DD origin/main
git log --oneline HEAD..refs/remotes/origin/upstream
git merge --no-ff --no-commit -Xsubtree=GeneralsX refs/remotes/origin/upstream
```

If the displayed SHAs differ, first review and complete the clean mirror refresh. The product integration must name the verified mirror revision.

The synthetic edit/add/delete check passed on the relocated history. Run `python3 scripts/qa/check-echelon-upstream-sync.py` after structural changes to revalidate the mapping without changing the working tree.

The subtree merge option maps the upstream root into `GeneralsX/`. Review `git status` and the entire staged diff before committing. All upstream engine paths must stay inside that prefix; Echelon's root CMake, launcher, integration, brand, packaging and workflow files must remain intact. If Git cannot map the historical base correctly, abort the merge and investigate the tree mapping rather than accepting misplaced files.

## Ownership and conflict resolution

- Root `Launcher/`, `EngineIntegration/`, `cmake/`, `assets/launcher/`, `flatpak/`, product scripts and docs belong to Echelon.
- `GeneralsX/` retains upstream structure plus reviewed engine changes. General bug fixes apply to both games; platform behavior stays inside the platform layer.
- `GENERALSX_SOURCE_DIR` and `GENERALSX_BINARY_DIR` contain upstream build paths. `ECHELON_SOURCE_DIR` and `ECHELON_BINARY_DIR` identify the parent product.
- Hosted and standalone libraries compile separately. Do not apply hosted allocator or brand flags to `g_generals` or `z_generals`.
- Review each conflict by behavior. Never use directory-wide ours/theirs decisions for platform or game logic.
- Preserve upstream attribution and deterministic math/audio behavior.
- The retired mod catalog must not return through a merge or replacement URL.

Update the pinned revision in `UPSTREAM.md`, the source ownership report and the monthly worklog. Then build both engines with `RTS_BUILD_UNIVERSAL_LAUNCHER=ON` and `OFF`, validate module exports, local content, headless dispatch, 20 alternating sessions, supervisor recovery, replay CRC and platform packaging. Report environment-limited checks explicitly.

Use the existing `ubuntu-dev` Distrobox on the maintainer's ALT workstation. New source layouts require a fresh build directory; preserve old caches for comparison. See [the foundation report](../WORKDIR/reports/ECHELON_FOUNDATION.md) for commands and results.

Create a reviewable PR in `Cheviiot/Echelon`. Do not merge or publish a release automatically.
