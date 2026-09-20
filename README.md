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

## What Happened With the Shader Crash

Across several rounds of testing, three different-looking bugs turned out to be the same root cause wearing different masks, all traced back to 3 of the engine's own shader files (`interactible_basic.xml`, `interactible_bump.xml`, `interactible_shadowmap.xml`):

1. **v1.2.1 (Bug #23):** vitaGL crashed while *reading back* a corrupted/0-byte shader cache file from a previous run. Fixed by making the cache reader tolerate bad cache files and recompile instead of crashing.
2. **v1.2.4 (Bug #26):** the same underlying bug's other half — a crash could still happen mid-level while loading the `interactible_bump` shader, this time on the *write* side: vitaGL tried to save a shader to the on-disk cache that had actually failed to compile, and crashed copying garbage into the cache file. Fixed by skipping the cache write whenever a shader fails to compile.
3. **v1.2.5 (Bug #28) — the actual bug:** neither fix above made the shader *compile*. They only stopped the game from crashing when it didn't. The 3 shader files above are written in a way Android's GLES driver quietly tolerates but the Vita's on-device Shark compiler does not (a `vec4(1)` constructor missing its float literal, and, in `_bump`, a uniform that's used but never declared) — so those shaders silently failed to compile on every boot, crash or no crash, and the interactable objects that use them rendered without their proper material/shading.

This was finally confirmed by diffing a tester's working `ux0:data/shadowguardian` folder against a clean, untouched data dump extracted straight from the original APK: the only real differences between the two were these exact 3 shader files, already hand-patched to work around the compile failure on that one console, but never shipped anywhere else. As of v1.2.5, the corrected shaders are bundled inside the VPK itself and get installed automatically on every boot, so the fix applies regardless of which data dump a tester is running — no manual file swapping needed.

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

- **Left Analog** - Movement (in-game only)
- **Right Analog** - Camera (free-look, continuous drag - hold to keep turning; light tilt for slow/precise aim, full tilt for a fast full 360°)
- **D-Pad** - Menu Navigation / Movement
- **R** - Fire
- **L** - Aim (hold)
- **Cross** - Contextual action (jump / run / climb)
- **Circle** - Grab / interact
- **Square** - Switch weapon
- **Start** - Pause / Back
- **Select** - Toggle Virtual Buttons (Hide/Show touch controls)
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
