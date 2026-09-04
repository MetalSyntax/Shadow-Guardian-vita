# Shadow Guardian Vita

This is a wrapper/port of **Shadow Guardian HD** for the PS Vita.

The port works by loading the official Android ARMv7 `libshadowguardian.so` executable in memory, resolving its imports with custom stubs, and patching it to properly run on the PS Vita using a fake JNI environment and translating OpenGL ES 2.0 calls to [vitaGL](https://github.com/Rinnegatamante/vitaGL).

## Current Status

**The game is fully playable at a smooth 60 FPS.**

Current features:
- The game is fully playable from start to finish.
- Audio and music work perfectly (Vox engine mapped to Vita's PCM audio).
- Touch controls and physical buttons are mapped and working.
- Gameloft DRM has been bypassed.
- Graphics render flawlessly using a natively built version of vitaGL.

## Setup Instructions (For End Users)

In order to properly install the game, you'll need to extract the data from the original Android game.

1. Install the `kubridge` and `fd_fix` plugins on your PS Vita.
2. Install `libshacccg.suprx`, if you don't have it already, by following [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
3. Obtain the original APK of Shadow Guardian (`Shadow-Guardian-HD-v1-0-1-UK-RU.apk`) and its data files.
4. Extract the `.apk` using any zip extractor.
5. Copy the `.so` executable `libshadowguardian.so` (from `lib/armeabi-v7a/` inside the APK) into `ux0:data/shadowguardian/`.
6. Copy the game data folders (`res`, `shaders`, `text`, `music`, `sounds`, `models`, `anims`, `collisions`, `textures`, etc.) to `ux0:data/shadowguardian/`. Ensure that 16:9 ratio assets like `sprites_1_7` and `gui_1_7` are present or copied from the `_1_6` fallback if missing.
7. Download and install `shadowguardian.vpk` on your PS Vita.

## Controls

- **Left Analog / D-Pad** - Movement
- **Right Analog** - Camera
- **Cross/Square/Triangle/Circle** - Action Buttons
- **L/R Triggers** - Aim/Shoot
- **Start / Circle** - Pause / Back
- **Select** - Menu
- **Touch Screen** - Original Touch Controls

## Build Instructions (For Developers)

You need [VitaSDK](https://vitasdk.org/) to build this project.

```bash
mkdir build && cd build
cmake ..
make
```

## Credits

- TheFloW for the `so_loader` wrapper and Android porting boilerplate.
- Rinnegatamante for `vitaGL` and porting tools.
- Gameloft for the original game.
