<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/branding/lichii_icon_with_logo.png" alt="Lichii" width="600">
</p>

This fork of [Lichii](https://github.com/zortos293/Lichii) adds fixes for The Duskbloods network test on Linux. The `nvidia-dusk` branch has all of them; each one is also sent to Lichii on its own:

- boot to the menus, and The Blood-Drenched Moon crash (pool memory): [#552](https://github.com/zortos293/Lichii/pull/552)
- online checks through a local server: [#560](https://github.com/zortos293/Lichii/pull/560), [#561](https://github.com/zortos293/Lichii/pull/561)
- an upscale in place of the game's DLSS network: [#637](https://github.com/zortos293/Lichii/pull/637)
- fog volumes cleared slice by slice: [#638](https://github.com/zortos293/Lichii/pull/638)
- audio crackle, shader forms, audio after a pause, the colour grading table copy: branches `fix/sysmix-host-callback-queue`, `fix/duskbloods-shader-forms`, `fix/sysmix-pump-after-pause`, `fix/blit-volume-slices`

To play, take the AppImage from [Releases](https://github.com/cerroverb/Lichii/releases) and start it through the [playDB-CNT](https://github.com/sikoGD/playDB-CNT/releases) launcher (1.1.0 or later), which runs the local servers the game checks. Keys, firmware and your game dump are set up as for Lichii.

Known issues:
- In Battle Remembrances the whole picture brightens and darkens every fraction of a second after a phase starts (seen on NVIDIA; not on a Steam Deck).
- The upscale stands in for DLSS, which the game expects: reflections flicker, shading fades back in after the camera stops, and fine detail shimmers on idle characters.
- Remembrances run well below full speed on some machines.
- If the emulator crashes while the game is saving, the save can be damaged; keep a copy of your save.
