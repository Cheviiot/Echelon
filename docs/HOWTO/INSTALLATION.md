# How to Install Generals: Arsenal

## Prerequisites

1. You must own a legitimate copy of the game. We build and test against the [Steam version](https://store.steampowered.com/app/2732960/Command__Conquer_Generals_Zero_Hour/). Other retail releases may work, but are not officially supported.

   > **On macOS or Linux?** This title is Windows-only on Steam. On macOS, Steam usually does not show an install option. On Linux, installation may be available via Steam Play/Proton depending on your configuration. See [GETTING_THE_GAME_FILES.md](GETTING_THE_GAME_FILES.md) for all supported ways to obtain game files.

2. Keep the original Generals and Zero Hour game data available on the machine. On first launch, the launcher can import a selected source directory into `$HOME/.GeneralsArsenal/Generals` or `$HOME/.GeneralsArsenal/GeneralsZH`. It does not inspect or migrate GeneralsX directories automatically.

## Linux

1. Install Flatpak for your distribution by following the official setup guide:

   https://flatpak.org/setup/

   Each Linux distribution packages Flatpak differently, so rely on the upstream instructions for installing the Flatpak tool itself.

2. Download the unified Linux Flatpak release asset (`GeneralsArsenal-linux.flatpak`). It contains the launcher and both open-source engine modules, but no retail assets.

3. Install the Flatpak bundle:

   ```bash
   flatpak --user install -y ./GeneralsArsenal-linux.flatpak
   ```

4. Launch the game with Flatpak:

   ```bash
   flatpak run io.github.cheviiot.GeneralsArsenal
   ```

5. Choose Generals or Zero Hour in the launcher. Missing profiles remain disabled until their required `.big` files are imported. Use **Game Data Settings** to select another source directory.

6. The Flatpak bundle ships the required userspace runtime libraries (DXVK, SDL3, SDL3_image, OpenAL, FFmpeg, and related dependencies). You do not need to install those libraries manually on the host.

> **GPU Driver note**: Vulkan support must be provided by your host GPU driver. For NVIDIA use the proprietary driver, for AMD/Intel use Mesa 21+. The Flatpak bundle does not ship GPU drivers.

## macOS

1. Download the macOS `.zip` file from this release.
2. Extract the `.zip` and copy the app bundle into your `Applications` folder.
3. Make sure your game assets are placed in the following locations:
   - `$HOME/.GeneralsArsenal/Generals` for Generals
   - `$HOME/.GeneralsArsenal/GeneralsZH` for Zero Hour
4. Because the app is not code-signed, macOS Gatekeeper will initially block it. After the first launch attempt, go to **System Settings -> Privacy & Security** and allow the application to run.

## Requirements

Generals: Arsenal inherits the GeneralsX platform stack and is developed primarily on the following environments:

- Ubuntu 26.04 LTS (x86_64)
- macOS 26 "Tahoe" on Apple Silicon (M1 / ARM64)

Other Linux distributions or macOS versions may work, but you may need to install additional dependencies manually. On some systems, certain libraries might need to be built from source.

## Tested Platforms

Current development and test matrix:

| Platform           | Architecture           | Status  |
|--------------------|------------------------|---------|
| Ubuntu 26.04 LTS   | x86_64                 | Working |
| macOS 26 "Tahoe"   | ARM64 (Apple Silicon)  | Working |

Support for other platforms and configurations is possible but not yet officially tested.

## Multiplayes features

- LAN play is currently incomplete; follow the [Generals: Arsenal issues](https://github.com/Cheviiot/GeneralsArsenal/issues) for current status.
- Online features - not implemented and planned for the future.

## Known Issues & Limitations

For documented limitations and known bugs, check the [issues page](https://github.com/Cheviiot/GeneralsArsenal/issues).

If you encounter problems while running the game, please [open an issue](https://github.com/Cheviiot/GeneralsArsenal/issues/new/choose) and include as much detail as possible, such as:

- Operating system and version
- CPU architecture (x86_64 / ARM64)
- Steps to reproduce the issue
- Logs or terminal output (if available)

This information greatly helps us reproduce and fix issues.

## Contributing

Contributions of all sizes are welcome.

If you are interested in helping with development, bug fixes, testing on additional platforms, or improving compatibility, feel free to open a pull request or start a discussion in the repository.

Even small contributions, such as testing, documentation improvements, or well-documented bug reports, are very valuable.

## Credits

This project exists thanks to the Command & Conquer community and the many tools created around the game over the years.

Special thanks to everyone who has contributed time to testing, debugging, and development.
