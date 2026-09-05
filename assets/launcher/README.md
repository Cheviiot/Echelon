# Echelon artwork

The editable SVG wordmark and icon are the production masters. They use original angular letter paths with gold faces and metal edges; PNG exports have a real alpha channel and contain only the Echelon identity. Regenerate them with `scripts/assets/render-brand.sh` inside `ubuntu-dev` (package `librsvg2-bin`).

The built-in image generator was used to explore a metallic ECHELON wordmark and an E icon in the earlier Arsenal style. Its wordmark outputs contained a painted transparency checkerboard, so they were not used as production textures. The final vector assets are maintained directly in this repository.

The existing unbranded launcher background is preserved from the development checkpoint. Retail game artwork is not added.

`fonts/DejaVuSans.ttf` is DejaVu Sans 2.37 from Ubuntu's `fonts-dejavu-core` package (2.37-8). The accompanying redistribution notices are preserved in `fonts/LICENSE`. The same font is packaged on Linux and macOS.
