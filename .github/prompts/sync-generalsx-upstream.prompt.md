---
mode: agent
description: Merge fbraz3/GeneralsX into Generals: Arsenal through a reviewed dated synchronization branch.
---

# Synchronize GeneralsX upstream

Follow `docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md` exactly.

1. Refuse to start if the worktree is dirty.
2. Verify `origin` points to `Cheviiot/GeneralsArsenal` and `upstream` points to `fbraz3/GeneralsX`.
3. Fetch both remotes and create `generalsx-sync-MM-DD-YYYY` from current `origin/main`.
4. Report the incoming commit range and touched subsystems before merging.
5. Merge `upstream/main` with `--no-ff`.
6. Resolve every conflict manually. Preserve Generals: Arsenal branding, launcher, ABI, data root, and packaging while accepting applicable upstream engine fixes.
7. Never use repository-wide `ours` or `theirs`, never rebase published history, and never push directly to `main`.
8. Build `generals_arsenal_launcher`, `g_generals`, and `z_generals` in `dev-ubuntu`; run ABI, clean-HOME, replay/CRC, Flatpak, and runtime switching checks.
9. Update the monthly worklog and prepare a pull request report with the upstream SHA, commit count, subsystem summary, manual resolutions, test evidence, and deferrals.
