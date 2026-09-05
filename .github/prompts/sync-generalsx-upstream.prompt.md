---
mode: agent
description: Merge fbraz3/GeneralsX into Echelon through a reviewed dated synchronization branch.
---

# Synchronize GeneralsX upstream

Follow `docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md` exactly.

1. Preserve existing development in a checkpoint before integrating upstream; do not discard dirty work.
2. Verify `origin` points to `Cheviiot/Echelon` and `upstream` points to `fbraz3/GeneralsX`.
3. Fetch both remotes. Verify the locked `origin/upstream` mirror equals the selected original GeneralsX commit and has no Echelon commits. If a mirror refresh is needed, follow the sync guide before proceeding. Create `codex/upstream-sync-YYYY-MM-DD` from current `origin/main`.
4. Report the incoming commit range and touched subsystems before merging.
5. Merge `refs/remotes/origin/upstream` with `--no-ff --no-commit -Xsubtree=GeneralsX`; verify that upstream changes remain inside `GeneralsX/`.
6. Resolve every conflict manually. Preserve Echelon branding, launcher, ABI, data root, and packaging while accepting applicable upstream engine fixes.
7. Never use repository-wide `ours` or `theirs`, never rebase published history, and never push directly to `main` or add product commits to the locked `upstream` branch.
8. Build `echelon_launcher`, `g_generals`, and `z_generals` in `ubuntu-dev`; run ABI, clean-HOME, replay/CRC, Flatpak, and runtime switching checks.
9. Update the monthly worklog and prepare a pull request report with the upstream SHA, commit count, subsystem summary, manual resolutions, test evidence, and deferrals.
