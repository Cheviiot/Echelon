#!/usr/bin/env bash
# Echelon @build Codex 12/08/2026 Flatpak entry point for the unified launcher.
set -euo pipefail

exec /app/bin/run.sh "$@"
