# Shadow Guardian Vita v1.3

This is a wrapper/port of **Shadow Guardian HD** for the PS Vita.

## Changelog

- v1.3: Full controls rework based on hardware testing feedback:
  - **Wrong weapon-switch position fixed.** The old touch coordinate for the weapon icon was only measured by eye and turned out to sit inside the FIRE button's touch hit-radius, so pressing that button fired the gun instead of switching weapons. All button positions (fire, aim, contextual action, grab, weapon) were re-measured with a pixel grid over a real 960x544 gameplay screenshot.
  - **Weapon switch moved to Square and reworked as a swipe, not a tap.** Reading the engine's own input handler (`out_ghidra.c:86760-86813`) showed the real weapon selector responds to the *drag delta* of a touch (`Actor::SetNextWeapon()` / `SetPreviousWeapon()` picked by the sign of the movement, matching its actual "left/right arrow slider" look), not a fixed tap zone — so Square now sends a synthetic down → drag-right → release over the weapon icon instead of a plain tap.
  - **Circle reassigned from a debug visibility toggle to Grab/interact** (the hand icon never had a physical button before); **Select now toggles the touch controls** on/off, taking over the old debug-toggle role.
  - **Square no longer shares the Aim spot with L** — only L aims now.
  - **Virtual button hiding is now enforced every frame** instead of once at startup, fixing buttons (the aim reticle in particular) that the engine would silently re-show later in a session.
  - **Hooked `GS_GamePlay::HighlightButton`** so the button-highlight glow shown during aim feedback / "touch this button" tutorial prompts is suppressed while the touch controls are hidden, instead of glowing over an otherwise-invisible button.
  - **The weapon-selector graphics are hidden too now** (previously always shown by request), using the exact GUI item index range (`0xb`-`0x20`) the engine itself uses to fade them out.
  - **Free-look camera (right stick) rewritten** from an absolute stick-to-touch-position mapping — which hard-capped at a fixed offset and could never complete a full turn — to a continuous relative drag with invisible wraparound recentring, so holding the stick now keeps rotating indefinitely: slow and precise at a light tilt, fast at full tilt, a full 360° around the character is now possible.
  - **Fixed a blurry intro video.** A texture-downsampling optimization was quartering the effective resolution of `logo.m4v` before a 4x upscale to the screen; disabled, since the intro is only a few seconds and the blur wasn't worth the saved upload bandwidth.
- v1.2.5: Fixed broken rendering on interactable objects. `shaders/interactible_basic.xml`, `interactible_bump.xml`, and `interactible_shadowmap.xml` (the engine's own shader source, shipped inside the original game data) use GLSL that Android's GLES driver quietly accepts but the Vita's on-device Shark compiler (`shark_compile_shader_extended()`) rejects outright: a `vec4(1)` constructor without an explicit float literal in all three, and — in `_bump` only — an `OutlineSize` uniform that's read in the shader body but never declared. All three previously failed to compile on Vita; the v1.2.1 and v1.2.4 hotfixes only stopped the game from crashing when that happened (a symptom of the bug, not the cause), so the interactable objects using these shaders kept rendering without their proper material/shading even on a "successful" boot. Found by diffing a tester's working data folder against a clean APK data dump — the only differences were these 3 files, hand-patched locally but never shipped. Corrected copies are now bundled inside the VPK (`extras/shader_patches/`) and installed to `ux0:data/shadowguardian/shaders/` automatically on every boot (`apply_shader_patches()` in `source/utils/init.c`), so this is fixed for every tester regardless of which data dump they installed from — no manual file replacement needed.
- v1.2.4: Fixed a crash in vitaGL's shader cache (`lib/vitagl/source/custom_shaders.c`, `compile_shader`) that could happen mid-level (seen while loading the `interactible_bump` shader) when the on-device Shark compiler failed to compile a shader — the code tried to write that failed (NULL) shader to the on-disk cache anyway, crashing inside `sceClibMemcpy`. Caching is now skipped for any shader that fails to compile. Also added an in-memory cache for `sounds/*.wav` effects (`source/reimpl/io.c`) to help with FPS drops reported while shooting/rolling: the engine was doing a full storage round-trip (`fopen`+`fread`+`fclose`) on the main thread for every single sound trigger, which piles up when several gunshots/impacts/ricochets fire at once during combat; repeated sounds are now served from RAM after the first real read instead of hitting storage again.
- v1.2.3: The startup data check in `init.c` now also validates `gui_1_6`/`gui_1_7` (flat or under `GloftSGHP/`), the same way it already did for `sprites_1_6`/`sprites_1_7`. A tester's console crashed mid-gameplay with the same `SpriteMgr::LoadSprite` signature as v1.2.2 even though the startup check passed — the log showed `gui_1_6` was also missing from their data copy, which the old check never looked for. This doesn't fix the crash by itself (the data still needs to be there), but a missing `gui_1_6`/`gui_1_7` now shows a clear "missing game data files" dialog on boot instead of an opaque native crash later in gameplay.
- v1.2.2: Fixed a crash in `SpriteMgr::LoadSprite` (Data abort inside STLport locale code, `_Locale_long_d_fmt`) that could still happen after v1.2.1 despite the startup asset check passing. Root cause: the engine's asset requests reach `translate_path()` (`source/reimpl/io.c`) already as flat `ux0:` paths (via the `initPath` hook), and that code path had no fallback to `DATA_PATH "GloftSGHP/<file>"` for anything other than the `_1_7`→`_1_6` redirect — so a data folder that kept the original APK layout (`GloftSGHP/sprites_1_6` instead of a flattened `sprites_1_6`) passed the startup check but still failed to load the sprite Lib at runtime, corrupting engine state and crashing shortly after. The `GloftSGHP/` fallback is now applied to every asset request, not just `_1_7`.
- v1.2.1: Robustness hotfix — added validation guards in vitaGL's `unserialize_shader` and `glLinkProgram` to prevent crash on corrupted/0-byte shader cache files (falls back to recompilation cleanly); added early startup data validation in `init.c` with friendly error dialogues if required game assets (`sprites_1_6`/`sprites_1_7` or `res/`) are missing; improved asset fallback routing for `GloftSGHP/` subfolders.
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
8. Double-check that `gui_1_6` and `sprites_1_6` themselves are also present in `ux0:data/shadowguardian/` (not just the `_1_7` copies) — some assets are only ever requested by their `_1_6` name at runtime, and a copy missing just these two files can pass every other check and still crash later during gameplay.
9. Download and install `shadowguardian.vpk` on your PS Vita.

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

## Known Issues

- A blue highlight/glow effect shown specifically when switching weapons may still appear even with the touch controls hidden — the generic `HighlightButton` hook (v1.3) covers the button-prompt glow, but the weapon-change flash may come from a separate, not-yet-located effect. Please report it with a log/screenshot of the exact moment it happens.
- Square's weapon-switch swipe and the new continuous camera drag (v1.3) are newly implemented and pending confirmation on hardware across multiple weapons/play sessions.

## Troubleshooting

**Crash on the LOADING screen (`C2-12828-1` error):** this is usually a data setup problem, not a game bug. Check the following with VitaShell:

1. You installed the **latest** `shadowguardian.vpk` (older builds have known crashes that are already fixed).
2. `kubridge` and `fd_fix` plugins plus `libshacccg.suprx` are installed (see Setup above).
3. In `ux0:data/shadowguardian/`: `libshadowguardian.so`, the `res/` folder, and all asset folders are present, and **both** the `_1_6` and `_1_7` variants of `sprites` and `gui` exist (`sprites_1_6`, `sprites_1_7`, `gui_1_6`, `gui_1_7`) — copy whichever ones are missing from the variant you do have. As of v1.2.3, a missing `gui_1_6`/`gui_1_7` or `sprites_1_6`/`sprites_1_7` shows a clear "missing game data files" dialog on boot instead of crashing later — if you see that dialog, it's telling you exactly which one to fix.

**If it still crashes**, please report it with:

1. The newest `ux0:data/shadowguardian/logs/game_log_*.txt` (from the time of the crash — do not delete it).
2. The newest `ux0:data/psp2core-*-eboot.bin.psp2dmp` core dump.
3. A note on exactly when it happens (on boot, which level loading, do you ever reach the menu?).

## Credits

- TheFloW for the `so_loader` wrapper and Android porting boilerplate.
- Rinnegatamante for `vitaGL` and porting tools.
- Gameloft for the original game.
