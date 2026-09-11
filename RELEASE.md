# Shadow Guardian Vita v1.2

This is a wrapper/port of **Shadow Guardian HD** for the PS Vita.

## Changelog
- v1.2: Intro video playback (`logo.m4v`) via SceAvPlayer on boot; virtual touch buttons hide/show with CIRCLE via `SetItemAlpha` (indices 0-10, weapon selector stays visible); D-Pad sends native menu keys; left stick only drives the virtual joystick in-game; faster free-look camera (drag radius 100→250) and wider stick deadzone (20→40); fixed crash on in-game Exit (`SoundMgr::Update` NULL deref — process now exits cleanly to LiveArea).
- v1.1: Full physical-controls mapping verified on hardware (sticks, D-Pad, R/L, Cross/Circle/Square/Triangle with touch injection at the real on-screen button positions); direct 800x480 to 960x544 display scaling fix (no FBO, engine FBOs untouched); analog deadzone with rescaling to absorb stick drift.
- v1.0: Initial release.

## Setup Instructions

In order to properly install the game, you'll need to extract the data from the original Android game.

1. Install the `kubridge` and `fd_fix` plugins on your PS Vita.
2. Install `libshacccg.suprx`, if you don't have it already, by following [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
3. Obtain the original APK of Shadow Guardian (`Shadow-Guardian-HD-v1-0-1-UK-RU.apk`) and the required data files.
4. Extract the `.apk` using any zip extractor.
5. Place the executable `libshadowguardian.so` (from `lib/armeabi-v7a/` inside the APK) in `ux0:data/shadowguardian/`. (If the APK also contains `libStormGLOFT.so`, you can copy it too — it does no harm.)
6. Place all game asset folders (`res`, `shaders`, `textures`, `models`, `music`, `sounds`, etc.) in `ux0:data/shadowguardian/`.
7. Make sure the 16:9 assets `sprites_1_7` and `gui_1_7` are present. If the APK only ships the `sprites_1_6` / `gui_1_6` variants, duplicate them with VitaShell (Copy + Paste + Rename) so the `_1_7` names exist — missing 16:9 GUI assets can crash the game on the LOADING screen. Also confirm the `res/` folder was copied.
8. Download and install `shadowguardian.vpk` on your PS Vita.

## Controls

- **Left Analog** - Movement (in-game only)
- **Right Analog** - Camera (free-look)
- **D-Pad** - Menu Navigation / Movement
- **R** - Fire
- **L / Square** - Aim (hold)
- **Cross** - Contextual action (jump / run / grab / climb)
- **Triangle** - Reload / Switch weapon
- **Circle** - Toggle Virtual Buttons (Hide/Show HUD layout)
- **Start** - Pause / Back
- **Select** - Menu
- **Touch Screen** - Original Touch Controls

## Known Issues
- Reload/weapon touch position (Triangle) confirmed by layout only; verify in combat on hardware.
- [Update this section prior to release with any remaining unresolved bugs]

## Troubleshooting

**Crash on the LOADING screen (`C2-12828-1` error):** this is usually a data setup problem, not a game bug. Check the following with VitaShell:

1. You installed the **latest** `shadowguardian.vpk` (older builds have known crashes that are already fixed).
2. `kubridge` and `fd_fix` plugins plus `libshacccg.suprx` are installed (see Setup above).
3. In `ux0:data/shadowguardian/`: `libshadowguardian.so`, the `res/` folder, and all asset folders are present, and the 16:9 files `sprites_1_7` / `gui_1_7` exist (copy them from the `_1_6` variants if needed).

**If it still crashes**, please report it with:

1. The newest `ux0:data/shadowguardian/logs/game_log_*.txt` (from the time of the crash — do not delete it).
2. The newest `ux0:data/psp2core-*-eboot.bin.psp2dmp` core dump.
3. A note on exactly when it happens (on boot, which level loading, do you ever reach the menu?).

## Credits
- TheFloW for the `so_loader` wrapper and Android porting boilerplate.
- Rinnegatamante for `vitaGL` and porting tools.
- Gameloft for the original game.
