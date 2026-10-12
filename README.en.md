# Harakiri (one-hit death)

A one-hit death mode for Way of the Samurai 4 on Steam. Attack damage is greatly increased for both the player and NPCs. Blade attacks use the native death handling; blunt attacks leave vitality at zero and use the native knockout handling.

This is a development version. In-game verification is still required. Special event invulnerability and attacks outside the normal damage path may remain unaffected.

Install `mods/harakiri/mod.dll` and `config.ini` alongside the ModLoader. The difficulty menu gains a final entry named "Harakiri" ("一撃死" in Japanese). Selecting it enables the mode; selecting a native difficulty disables it. Hard and Very Hard keep their existing unlock requirements.

The native highest difficulty is displayed as VeryHard in English. The new final difficulty is displayed as Harakiri.

Everything except one-hit death uses Very Hard behavior, including enemy strength, evaluation, rewards, and weapon generation. The Mod and saves use a separate `Harakiri = 4` difficulty, translated to `VeryHard = 3` for native gameplay. Selecting native Normal or Very Hard does not enable one-hit death.

Saving, loading, and retrying after death preserve Harakiri. Save slots display "Harakiri" ("一撃死" in Japanese). Loading a save made in Harakiri requires this Mod. `[harakiri] enabled=0` in `config.ini` disables the entire Mod. The default is 1; enabling the Mod does not make native difficulties lethal.

Torture uses a separate gauge calculation. This Mod does not change torture damage.

The Mod verifies the supported game version and code before installing hooks in memory. Game files are not modified.

From the ModLoader repository, run `cmake -S . -B build -A Win32` and `cmake --build build --config Release`. The output is `build/game/mods/harakiri/mod.dll`. For a standalone build, pass `-DWOTS4_SDK_DIR=<path to ModLoader>`.

Run tests with `ctest --test-dir build -C Release -R harakiri --output-on-failure`. Tests do not start the game.
