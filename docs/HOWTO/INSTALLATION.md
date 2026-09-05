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
