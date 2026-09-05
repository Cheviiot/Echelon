#!/usr/bin/env bash
# Compatibility wrapper; use test-echelon-supervisor-wayland.sh.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$script_dir/test-echelon-supervisor-wayland.sh" "$@"
