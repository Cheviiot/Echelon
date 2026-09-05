#!/usr/bin/env bash
# Echelon @build Codex 05/09/2026 Package both hosted engines in one application.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${script_dir}/bundle_macos_echelon.py" "${1:-macos-vulkan}"
