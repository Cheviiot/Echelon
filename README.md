<p align="center">
  <img src="assets/launcher/echelon-logo.svg" alt="Echelon" width="560">
</p>

<p align="center">
  <strong>Generals &amp; Zero Hour. One command center.</strong><br>
  An open-source launcher and engine fork for Linux and macOS.
</p>

<p align="center">
  <a href="docs/HOWTO/INSTALLATION.md">Getting started</a> ·
  <a href="docs/HOWTO/ECHELON_LAUNCHER.md">User guide</a> ·
  <a href="https://github.com/Cheviiot/Echelon/issues">Report an issue</a>
</p>

<p align="center">
  <a href="https://github.com/Cheviiot/Echelon/actions/workflows/ci.yml"><img src="https://github.com/Cheviiot/Echelon/actions/workflows/ci.yml/badge.svg?branch=main" alt="Build status"></a>
</p>

## Two games, one application

Echelon brings **Command & Conquer: Generals** and **Zero Hour** together, building on the cross-platform engine from [GeneralsX](https://github.com/fbraz3/GeneralsX).

- **Switch games** — return to the launcher without closing the application.
- **Make it yours** — import local mods, patches and add-ons; manage their order and installed versions.
- **Keep things separate** — independent settings, saves and mod profiles for each game.
- **Choose your language** — English and Russian launcher interfaces.

## Get started

Echelon is in active development: **Flatpak on Linux** and **Apple Silicon on macOS**. Start with the [installation guide](docs/HOWTO/INSTALLATION.md), then select your game-data folders on first launch. Original files are copied and preserved; settings and saves live under `~/.Echelon`.

**You need your own game data.** Retail assets are not included. See the [validation record](docs/WORKDIR/reports/ECHELON_FOUNDATION.md#executed-validation) for tested behavior and current limitations, including replay compatibility.

## Build from source

Set up the [build environment](docs/WORKDIR/reports/ECHELON_FOUNDATION.md#build-environment), then build the launcher and both engines:

```bash
git clone --recurse-submodules https://github.com/Cheviiot/Echelon.git
cd Echelon
cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON
cmake --build build/linux64-deploy --target echelon_launcher
```

The executable is `build/linux64-deploy/Echelon/Echelon`. For macOS, use the `macos-vulkan` preset and its corresponding build directory.

## Repository

| Branch | Purpose |
| :--- | :--- |
| `main` | Echelon: product sources at the root, integrated engine in `GeneralsX/`. |
| [`upstream`](https://github.com/Cheviiot/Echelon/tree/upstream) | Protected original GeneralsX history, without Echelon changes. |

Engine updates are integrated through reviewed merges. See the [upstream policy](UPSTREAM.md).

## Credits & license

Built on [GeneralsX](https://github.com/fbraz3/GeneralsX), [TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode), and the portability work of [Fighter19 and feliwir](https://github.com/Fighter19/CnC_Generals_Zero_Hour). Original authorship and history are preserved.

[GNU GPL v3 or later](LICENSE.md). An independent community project, not affiliated with Electronic Arts. Command & Conquer and related trademarks belong to their respective owners.
