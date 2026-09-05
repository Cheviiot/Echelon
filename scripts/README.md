# Echelon scripts

Run product commands from the repository root. Original engine tools are preserved under `GeneralsX/scripts/`.

- `build/linux/build-linux-flatpak.sh`: build the single Echelon Flatpak inside the Freedesktop SDK.
- `build/macos/bundle-macos-echelon.sh`: package and ad-hoc sign one Echelon.app, checking the complete dylib dependency set.
- `assets/render-brand.sh`: regenerate PNGs from the editable SVG identity masters.
- `qa/check-echelon-upstream-sync.py`: verify incoming edit/add/delete mapping with synthetic Git trees.
- `qa/check-echelon-boundaries.py`: verify identity, source layout, compile flags and module exports.
- `qa/smoke/test-echelon-*.sh`: isolated local-content, transport, headless, Wayland lifecycle and recovery checks.

On the maintainer's ALT workstation, use the existing Ubuntu 24.04 `ubuntu-dev` Distrobox for development dependencies. Build/test scripts run inside that container; they do not install packages on the host. The Flatpak runtime and SDK are already installed per user.

Set `ECHELON_QA_DATA_ROOT` explicitly when tests need existing retail files. The selected root must contain `Generals`, `GeneralsZH` and `Profiles`. Tests use temporary homes and never migrate old application settings.
