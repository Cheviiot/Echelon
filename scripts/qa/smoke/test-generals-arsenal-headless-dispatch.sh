#!/usr/bin/env bash
# Compatibility wrapper; use test-echelon-headless-dispatch.sh.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$script_dir/test-echelon-headless-dispatch.sh" "$@"
