[![Echelon CI](https://github.com/Cheviiot/Echelon/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Cheviiot/Echelon/actions/workflows/ci.yml)
[![GitHub Release](https://img.shields.io/github/v/release/Cheviiot/Echelon?include_prereleases&sort=date&display_name=tag&style=flat&label=Release)](https://github.com/Cheviiot/Echelon/releases)

# Echelon

Echelon combines **Command & Conquer: Generals** and **Zero Hour** in one cross-platform SDL3 launcher. The selected engine runs in the same process and window, and can return to the Echelon selector without restarting the application.

The local mod manager imports archives and folders, verifies installed content, and maintains independent mod, patch, and add-on profiles for both games. Online mod distribution is deferred: the retired catalog has no replacement endpoint and the launcher makes no catalog requests.

## Project status

- Linux packaging produces one Flatpak: `io.github.cheviiot.Echelon`.
- macOS packaging is prepared for one `Echelon.app` containing both engines; execution on macOS remains unverified.
- The public executable is `Echelon`.
- Generals and Zero Hour remain separate private engine modules.
- Retail game assets are not included. You must provide data from a legally owned copy.
- Linux and macOS share the SDL3, DXVK, OpenAL, and 64-bit platform stack inherited from GeneralsX.

See the [installation guide](docs/HOWTO/INSTALLATION.md), [launcher guide](docs/HOWTO/ECHELON_LAUNCHER.md), and [tested build environment](docs/WORKDIR/reports/ECHELON_FOUNDATION.md#build-environment) for details.

## Source layout

```text
Launcher/          Echelon UI, settings and local mod management
EngineIntegration/ Shared host ABI, engine entry and content layers
cmake/             Echelon identity and hosted build variants
assets/            Product artwork and redistributable font
scripts/           Product builds, packaging and QA
flatpak/           Unified Linux package
docs/              Our guides, decisions and worklog
GeneralsX/         Upstream engine, libraries, tools and historical docs
```

`GeneralsX/` is not a submodule. Engine fixes remain tracked in this repository; future upstream merges use the directory prefix. The historical DXVK reference inside it retains its existing submodule registration.

The two permanent branches are `main` for Echelon and [`upstream`](https://github.com/Cheviiot/Echelon/tree/upstream) for a locked, exact copy of the original GeneralsX history and layout. The clean branch has no Echelon modifications; see [the branch policy](UPSTREAM.md#permanent-branches).

## Command line

Show the selector:

```bash
Echelon --launcher
```

Launch a profile directly while forwarding the remaining game arguments:

```bash
Echelon --profile=generals -win
Echelon --profile=zerohour -headless -replay example.rep
```

Application data is stored only under `$HOME/.Echelon`. Echelon does not automatically read or migrate old GeneralsX directories.

## Building

On Linux, configure and build the unified launcher with both engine modules:

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher
```

The existing `g_generals` and `z_generals` targets remain available for upstream-compatible engine development and debugging.

## Relationship to GeneralsX

Echelon is a fork of [fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX). It preserves the complete Git history, GPL license, contributor attribution, and the upstream engine directory structure.

Our source lives at the repository root. GeneralsX is an ordinary tracked directory, with reviewed engine changes preserved inside it. The fork owns `Launcher/`, `EngineIntegration/`, brand configuration, and product packaging. The ownership map and validation record are maintained in [the foundation report](docs/WORKDIR/reports/ECHELON_FOUNDATION.md). Engine improvements continue to be integrated from GeneralsX through reviewed Git merges instead of source snapshots or history rewrites. See [Synchronizing GeneralsX upstream](docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md).

GeneralsX itself builds on major community efforts:

- [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) provides the upstream game-code foundation, preservation work, compatibility fixes, and long-term maintenance.
- [Fighter19/CnC_Generals_Zero_Hour](https://github.com/Fighter19/CnC_Generals_Zero_Hour), including major work by feliwir, pioneered much of the SDL3, DXVK, OpenAL, FFmpeg, and Linux portability stack used by GeneralsX.
- All original GeneralsX and community contributors retain their authorship in the repository history and source annotations.

## Contributing

Open issues and pull requests at [Cheviiot/Echelon](https://github.com/Cheviiot/Echelon). Changes to shared platform code and general bug fixes should remain applicable to both Generals and Zero Hour. Fork-specific branding belongs in the centralized brand configuration and Echelon launcher layer.

## License and trademarks

Echelon remains licensed under the GNU General Public License; see [LICENSE.md](LICENSE.md). Upstream copyright notices and attribution are preserved.

This project is not endorsed or supported by Electronic Arts. Command & Conquer, Generals, Zero Hour, and related marks belong to their respective owners. No retail artwork or game data is distributed by this repository or its packages.
