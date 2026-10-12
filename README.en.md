# Harakiri (one-hit death)

A one-hit death mode for Way of the Samurai 4 on Steam. Attack damage is greatly increased for both the player and NPCs. Blade attacks use the native death handling; blunt attacks leave vitality at zero and use the native knockout handling.

This is a development version. In-game verification is still required. Special event invulnerability and attacks outside the normal damage path may remain unaffected.

Install `mods/harakiri/mod.dll` and `config.ini` alongside the ModLoader. Set `[harakiri] enabled=1` in `config.ini` to enable the mode, or `0` to disable it. The Mod verifies the supported game version and code before installing hooks in memory. Game files are not modified.

From the ModLoader repository, run `cmake -S . -B build -A Win32` and `cmake --build build --config Release`. The output is `build/game/mods/harakiri/mod.dll`. For a standalone build, pass `-DWOTS4_SDK_DIR=<path to ModLoader>`.

Run tests with `ctest --test-dir build -C Release -R harakiri --output-on-failure`. Tests do not start the game.
