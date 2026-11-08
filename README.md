<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/branding/lichii_icon_with_logo.png" alt="Lichii" width="600">
</p>

Lichii with fixes for The Duskbloods network test on Linux: boot, online checks, shaders, fog, an upscale in place of the game's DLSS network, and audio. Tested on an RTX 5080 laptop and a Steam Deck.

What you need:
- the AppImage from [Releases](https://github.com/cerroverb/Lichii/releases)
- your own `prod.keys` and Switch 2 firmware, installed in the emulator
- your own dump of the network test (the `.nup` or the extracted folder)
- the [playDB-CNT](https://github.com/sikoGD/playDB-CNT/releases) launcher (1.1.0 or later), which runs the local servers the game checks and then starts the emulator

Setup:
1. `chmod +x Lichii-Duskbloods-x86_64.AppImage` and put it in `~/Applications`.
2. Unpack playDB-CNT and run `./play-duskbloods.sh`.
3. Pick the AppImage and the game in the launcher, then start.
4. Press B to enter the game; press Start twice to skip the intro video.

Known issues: reflections flicker, shading fades in after the camera stops, and the fog lighting can flash in Battle Remembrances.

Source for each release is under its tag; fixes go upstream to [Lichii](https://github.com/zortos293/Lichii) as pull requests. Licensed GPL-3.0-or-later, see [LICENSE.txt](LICENSE.txt).
