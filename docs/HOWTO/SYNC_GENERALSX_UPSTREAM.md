# Synchronize GeneralsX upstream

The product is maintained in the root of the Echelon repository. The integrated engine is an ordinary tracked `GeneralsX/` directory. Do not create a GeneralsX submodule, clone over this directory, or rewrite published history.

## Review a pinned update

Start from a clean working tree with all existing development preserved in commits. Verify the remotes, fetch the upstream history, and record the exact incoming SHA and range:

```bash
git remote -v
git fetch upstream main
git switch -c codex/upstream-sync-YYYY-MM-DD
git log --oneline HEAD..upstream/main
git merge --no-ff --no-commit -Xsubtree=GeneralsX upstream/main
```

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
