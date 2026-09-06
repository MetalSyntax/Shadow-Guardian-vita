# Shadow Guardian Vita v1.1

This is a wrapper/port of **Shadow Guardian HD** for the PS Vita.

## Changelog
- v1.1: Full physical-controls mapping verified on hardware (sticks, D-Pad, R/L, Cross/Circle/Square/Triangle with touch injection at the real on-screen button positions); direct 800x480 to 960x544 display scaling fix (no FBO, engine FBOs untouched); analog deadzone with rescaling to absorb stick drift.
- v1.0: Initial release.

## Setup Instructions

In order to properly install the game, you'll need to extract the data from the original Android game.

1. Install the `kubridge` and `fd_fix` plugins on your PS Vita.
2. Install `libshacccg.suprx`, if you don't have it already, by following [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
3. Obtain the original APK of Shadow Guardian (`Shadow-Guardian-HD-v1-0-1-UK-RU.apk`) and the required data files.
4. Extract the `.apk` using any zip extractor.
5. Place the executable `libshadowguardian.so` (from `lib/armeabi-v7a/` inside the APK) in `ux0:data/shadowguardian/`.
6. Place all game asset folders (`res`, `shaders`, `textures`, `models`, `music`, `sounds`, etc.) in `ux0:data/shadowguardian/`.
7. Download and install `shadowguardian.vpk` on your PS Vita.

## Controls

- **Left Analog / D-Pad** - Movement (virtual joystick)
- **Right Analog** - Camera (free-look)
- **R** - Fire
- **L / Square** - Aim (hold)
- **Cross** - Contextual action (jump / run / grab / climb)
- **Circle** - Interact / Take cover (contextual)
- **Triangle** - Reload / Switch weapon (combat)
- **Start** - Pause / Back
- **Select** - Menu
- **Touch Screen** - Original Touch Controls

## Known Issues
- Reload/weapon touch position (Triangle) confirmed by layout only; verify in combat on hardware.
- [Update this section prior to release with any remaining unresolved bugs]

## Credits
- TheFloW for the `so_loader` wrapper and Android porting boilerplate.
- Rinnegatamante for `vitaGL` and porting tools.
- Gameloft for the original game.
