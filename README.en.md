# Harakiri (one-hit death)

[日本語](README.md)

> [!NOTE]
> This Mod is for the [Steam version of Way of the Samurai 4](https://store.steampowered.com/app/312780/4/?l=english) only.

Adds a "Harakiri" difficulty. Both the player and NPCs go down after a single hit.

## Features

### The Harakiri difficulty

- The difficulty menu gains a final entry named "Harakiri" ("一撃死" in Japanese). Hard and Very Hard keep their existing unlock requirements.
- A blade attack kills, and a blunt attack empties stamina and knocks out. This applies to both the player and NPCs.
- A constable's arrest kick deals its original health damage and empties your stamina in one kick, so you are arrested.
- Everything except one-hit death is the same as Very Hard (enemy strength, evaluation and rewards, weapon generation, and so on).

### Saves

- Save slots display "Harakiri" ("一撃死" in Japanese).
- Loading a save made in Harakiri requires this Mod.

### When one-hit death does not apply

- Attacks that deal no damage in the original game, and scenes where the game sets a minimum health, do not kill; the game continues as usual.
- Special invulnerability during events, and attacks that deal damage outside the usual way, may not be one-hit death.
- Torture damage is not changed.

### Verified in game

The following have been confirmed to play without problems in game.

- Combat
  - How health and stamina go down
  - DLC NPCs whose health behaves in special ways
- Arrests by constables, okappiki, and others
- Torture
- Night visits (yobai)

## Installation

### Automatic installation (recommended)

1. Close the game.
2. Download and run [WayOfTheSamurai4ModInstaller.exe](https://github.com/nagataaaas/wots4-mods/releases/latest/download/WayOfTheSamurai4ModInstaller.exe).
   - If "Windows protected your PC" appears, click "More info" → "Run anyway".
   - When asked whether to allow it to make changes as administrator, click "Yes" (it writes to the game folder).
3. Follow the screen and choose `harakiri` in the list of Mods. Move with Up and Down, choose with Space and confirm with Enter.

The ModLoader is installed along with it. The same steps update the Mod, and your settings (`config.ini`) are kept.

<details>
<summary>Manual installation</summary>

> [!WARNING]
> Install [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) first.

1. Close the game.
2. Download `wots4-mod-harakiri-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `mods` folder into the game's installation folder (the folder containing `WayOfTheSamurai4.exe`). If a `mods` folder already exists, merge the contents into it.
4. Confirm that `{game installation folder}/mods/harakiri/mod.dll` exists, then start the game.

To keep your current settings when updating, replace only `mod.dll` and do not overwrite `mods/harakiri/config.ini`.

</details>

## Uninstallation

1. Close the game.
2. Delete the `{game installation folder}/mods/harakiri/` folder. The settings in it (`config.ini`) are deleted too.

> [!WARNING]
> Saves made on the Harakiri difficulty can no longer be loaded once this Mod is removed.

## Settings

Close the game and edit `{game installation folder}/mods/harakiri/config.ini`. Save the file and start the game to apply the settings.

| Section    | Key       | Default | Description                                                                                                                                    |
| ---------- | --------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `harakiri` | `enabled` | `1`     | `0` disables the entire Mod, and the Harakiri difficulty no longer appears. Setting it to `1` does not make other difficulties one-hit death. |

## Building

> [!NOTE]
> You do not need this just to use the Mod. Refer to it only if you want to fix or develop it.

It is usually built inside [wots4-mods](https://github.com/nagataaaas/wots4-mods). To build only this Mod, run CMake with `-DWOTS4_SDK_DIR=<path to wots4-mods>`.
