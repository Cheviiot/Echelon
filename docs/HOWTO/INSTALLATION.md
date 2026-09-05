# Install Echelon

Echelon is one application containing the Generals and Zero Hour engine modules. Retail game data is not included; use files from your own copy of the games. For obtaining the files, see the [upstream guide](../../GeneralsX/docs/HOWTO/GETTING_THE_GAME_FILES.md).

## Linux

Install Flatpak using your distribution's supported method. Install the unified package from an Echelon release or your local build:

```bash
flatpak --user install ./Echelon-linux.flatpak
flatpak run io.github.cheviiot.Echelon
```

A local packaging build produces `build/Echelon-linux64-deploy.flatpak`; the release workflow names the downloadable asset `Echelon-linux.flatpak`.

## macOS

Unpack `Echelon-macos-arm64.zip`, move `Echelon.app` to Applications, and launch it. This build targets Apple Silicon and macOS 15 or newer. Local and CI bundles use ad-hoc signing; Developer ID signing and notarization are separate release work.

The application contains both engine modules, SDL3, DXVK, Vulkan/MoltenVK and their required libraries. No Homebrew installation is needed to run a complete bundle.

## First launch

Select the Generals and Zero Hour data directories when prompted. Echelon imports explicitly selected game assets into `$HOME/.Echelon/Generals` and `$HOME/.Echelon/GeneralsZH`. Canceling selection leaves the corresponding game disabled.

Settings and saves are separate for the two games under `.Echelon/UserData`. Existing Arsenal and GeneralsX application directories are not inspected or migrated automatically. This is a clean installation.

Use **MODS** to import local archives or folders. Online mod catalog services are retired; see the [launcher guide](ECHELON_LAUNCHER.md) for local content, settings and profiles.
