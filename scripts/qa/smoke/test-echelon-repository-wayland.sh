#!/usr/bin/env bash
# Echelon @test Codex 05/09/2026 The retired repository UI must stay local, even with cached catalogs.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ECHELON_REPOSITORY_URL="https://127.0.0.1:9/retired-catalog.yaml"
exec "${script_dir}/test-echelon-mod-manager-wayland.sh" "$@"
