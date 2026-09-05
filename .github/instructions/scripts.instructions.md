---
applyTo: 'scripts/**,GeneralsX/scripts/**'
---

# Product and upstream scripts

Root `scripts/` contains Echelon entry points. `GeneralsX/scripts/` preserves upstream tools and historical wrappers. The owner explicitly requested a clean source separation; do not recreate retired root wrappers or old repository layouts.

- `build/linux/` and `build/macos/`: unified product packaging.
- `assets/`: regenerate product artwork from editable masters.
- `qa/`: ownership, module-boundary and upstream-sync checks.
- `qa/smoke/`: isolated runtime and local-content checks, plus test fixtures.

Run commands from the repository root. Resolve paths from the script location, quote shell expansions and propagate failures (`set -euo pipefail`). Give scripts a usage header and meaningful exit codes. Python should use a main entry point when reusable; avoid empty stubs and broad exception suppression.

On ALT, use the documented Distrobox for development tools. Keep logs in ignored `logs/`, generated builds in `build/`, and QA fixtures in temporary directories. Do not alter the owner's original data. `ECHELON_QA_DATA_ROOT` selects retail fixtures explicitly.

When adding or moving a maintained entry point, update `scripts/README.md`, applicable VS Code tasks, active CI callers and instructions. Compatibility wrappers are optional when a caller still needs them; they are not required for the approved clean Echelon transition. Keep upstream reference tools under their existing prefix.

See [script inventory](../../scripts/README.md), [presets](../../CMakePresets.json) and [agent instructions](../../AGENTS.md).
