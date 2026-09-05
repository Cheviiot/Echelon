[![Generals: Arsenal CI](https://github.com/Cheviiot/GeneralsArsenal/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Cheviiot/GeneralsArsenal/actions/workflows/ci.yml)
[![GitHub Release](https://img.shields.io/github/v/release/Cheviiot/GeneralsArsenal?include_prereleases&sort=date&display_name=tag&style=flat&label=Release)](https://github.com/Cheviiot/GeneralsArsenal/releases)

# Generals: Arsenal

Generals: Arsenal combines **Command & Conquer: Generals** and **Zero Hour** in one cross-platform SDL3 launcher. The selected engine runs in the same process and window, and can return to the Arsenal selector without restarting the application.

The project is designed to become a common home for both engines, community mods, and patches. Mod installation and patch management are not part of the first release, but their profile and directory interfaces are reserved.

## Project status

- Linux is packaged as one Flatpak: `io.github.cheviiot.GeneralsArsenal`.
- The public executable is `GeneralsArsenal`.
- Generals and Zero Hour remain separate private engine modules.
- Retail game assets are not included. You must provide data from a legally owned copy.
- Linux and macOS share the SDL3, DXVK, OpenAL, and 64-bit platform stack inherited from GeneralsX.

See the [installation guide](docs/HOWTO/INSTALLATION.md), [launcher guide](docs/HOWTO/GENERALS_ARSENAL_LAUNCHER.md), and [build guides](docs/BUILD/) for details.

## Command line

Show the selector:

```bash
GeneralsArsenal --launcher
```

Launch a profile directly while forwarding the remaining game arguments:

```bash
GeneralsArsenal --profile=generals -win
GeneralsArsenal --profile=zerohour -headless -replay example.rep
```

Application data is stored only under `$HOME/.GeneralsArsenal`. Generals: Arsenal does not automatically read or migrate old GeneralsX directories.

## Building

On Linux, configure and build the unified launcher with both engine modules:

```bash
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target generals_arsenal_launcher
```

The existing `g_generals` and `z_generals` targets remain available for upstream-compatible engine development and debugging.

## Relationship to GeneralsX

Generals: Arsenal is a fork of [fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX). It preserves the complete Git history, GPL license, contributor attribution, and the upstream engine directory structure.

The fork adds a branded universal launcher and fork-owned packaging as a deliberately small overlay. Engine improvements continue to be integrated from GeneralsX through reviewed Git merges instead of source snapshots or history rewrites. See [Synchronizing GeneralsX upstream](docs/HOWTO/SYNC_GENERALSX_UPSTREAM.md).

GeneralsX itself builds on major community efforts:

- [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) provides the upstream game-code foundation, preservation work, compatibility fixes, and long-term maintenance.
- [Fighter19/CnC_Generals_Zero_Hour](https://github.com/Fighter19/CnC_Generals_Zero_Hour), including major work by feliwir, pioneered much of the SDL3, DXVK, OpenAL, FFmpeg, and Linux portability stack used by GeneralsX.
- All original GeneralsX and community contributors retain their authorship in the repository history and source annotations.

## Contributing

Open issues and pull requests at [Cheviiot/GeneralsArsenal](https://github.com/Cheviiot/GeneralsArsenal). Changes to shared platform code and general bug fixes should remain applicable to both Generals and Zero Hour. Fork-specific branding belongs in the centralized brand configuration and Arsenal launcher layer.

## License and trademarks

Generals: Arsenal remains licensed under the GNU General Public License; see [LICENSE.md](LICENSE.md). Upstream copyright notices and attribution are preserved.

This project is not endorsed or supported by Electronic Arts. Command & Conquer, Generals, Zero Hour, and related marks belong to their respective owners. No retail artwork or game data is distributed by this repository or its packages.
