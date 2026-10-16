# GeneralsX integration baseline

- Upstream: https://github.com/fbraz3/GeneralsX
- Source remote: `upstream` (`fbraz3/GeneralsX`)
- Product branch: `origin/main`
- Pristine mirror branch: `origin/upstream` (original commits and original root layout)
- Integrated revision: `670b082cda303f8b84e31b75f11506ec9be6a8eb`
- Previous baseline: `3c2ed4589599beb631ea6da602c8ac95abf6bcdc`
- Incoming commits: 244 (reviewed on `codex/upstream-sync-2026-10-16`)
- Prefix: `GeneralsX/`, an ordinary directory tracked by the Echelon repository
- Development checkpoint: `984ab6c91`
- Upstream merge: `4b7395027`
- Product-layer checkpoint before relocation: `5300ceb85`

The engine directory includes reviewed Echelon changes; it is not claimed to be a pristine upstream snapshot. Product sources, engine integration, identity, packaging and QA live at the root. Full Git history and upstream contributor attribution are retained.

Use [the sync guide](docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md) for subsequent merges and [the foundation report](docs/WORKDIR/reports/ECHELON_FOUNDATION.md) for the ownership map and validation evidence.

## Permanent branches

`main` is the Echelon product branch. `upstream` is a locked mirror of the original GeneralsX history, initially at the exact integrated revision above. It contains no Echelon commits, relocated paths, product patches or extra mirror metadata. The Git remote named `upstream` is the external source; the branch `origin/upstream` is its clean copy in this repository.

GitHub protection locks the mirror against pushes, including administrator pushes, and disallows force pushes and deletion. Fork syncing is allowed; every refresh must retain an exact original commit SHA and fast-forward history. Never merge `main` or a product PR into this branch.

The owner approved this arrangement and authorized foundation PR #1 to merge into product `main`. The temporary `codex/echelon-foundation` branch is used only until the reviewed integration completes. The clean mirror is never a product merge target.
