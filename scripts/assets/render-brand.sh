#!/usr/bin/env bash
# Render the editable vector masters; requires librsvg2-bin in the development container.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
for asset in logo icon; do
    rsvg-convert "${repo_root}/assets/launcher/echelon-${asset}.svg" \
        -o "${repo_root}/assets/launcher/echelon-${asset}.png"
done
cp "${repo_root}/assets/launcher/echelon-icon.png" "${repo_root}/flatpak/io.github.cheviiot.Echelon.png"
