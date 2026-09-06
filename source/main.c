#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include "audio.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 32 * 1024 * 1024;
#endif

so_module so_mod;

#define SCREEN_W 800
#define SCREEN_H 480

// Native function pointer types
typedef void (*sg_get_info_fn)(JNIEnv *, jobject, jstring, jstring, jstring, jstring, jstring, jstring, jstring);
typedef void (*sg_void_fn)(JNIEnv *, jobject);
typedef void (*sg_renderer_init_fn)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*sg_resize_fn)(JNIEnv *, jobject, jint, jint);
typedef int  (*sg_render_fn)(void);
typedef void (*sg_touch_fn)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*sg_key_fn)(JNIEnv *, jobject, jint);
typedef int  (*sg_can_interrupt_fn)(void);

static sg_get_info_fn           nativeGetInfo = NULL;
static sg_void_fn               nativeGLResLoaderInit = NULL;
static sg_void_fn               nativeDeviceInit = NULL;
static sg_renderer_init_fn       nativeGameRendererInit = NULL;
static sg_void_fn               nativeShadowGuardianInit = NULL;
static sg_resize_fn             nativeGameRendererResize = NULL;
static sg_render_fn             nativeGameRendererRender = NULL;
static sg_void_fn               nativeGameRendererDone = NULL;
static sg_void_fn               nativeGameGLSurfaceViewPause = NULL;
static sg_void_fn               nativeGameGLSurfaceViewResume = NULL;
static sg_touch_fn              nativeGameGLSurfaceViewOnTouch = NULL;
static sg_key_fn                nativeOnKeyDown = NULL;
static sg_key_fn                nativeOnKeyUp = NULL;
static sg_can_interrupt_fn      nativeCanInterrupt = NULL;

static void *resolve_sym_or_die(const char *name) {
    void *sym = (void *)so_symbol(&so_mod, name);
    if (!sym) {
        fatal_error("Required symbol '%s' not found in libshadowguardian.so!", name);
    }
    l_info("Resolved %s -> %p", name, sym);
    return sym;
}

static void resolve_entrypoints(void) {
    nativeGetInfo = (sg_get_info_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeGetInfo");
    nativeGLResLoaderInit = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GLResLoader_nativeInit");
    nativeDeviceInit = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GLUtils_Device_nativeInit");
    nativeGameRendererInit = (sg_renderer_init_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeInit");
    nativeShadowGuardianInit = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeInit");
    nativeGameRendererResize = (sg_resize_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeResize");
    nativeGameRendererRender = (sg_render_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeRender");
    nativeGameRendererDone = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeDone");
    nativeGameGLSurfaceViewPause = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativePause");
    nativeGameGLSurfaceViewResume = (sg_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativeResume");
    nativeGameGLSurfaceViewOnTouch = (sg_touch_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativeOnTouch");
    nativeOnKeyDown = (sg_key_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnKeyDown");
    nativeOnKeyUp = (sg_key_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnKeyUp");
    nativeCanInterrupt = (sg_can_interrupt_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeCanInterrupt");
}

// ---------------------------------------------------------------------------
// Physical -> touch mapping (Shadow Guardian is 100% touch-driven).
//
// Findings (decompiled/ + GameGLSurfaceView.java + out_ghidra.c):
// - nativeOnTouch(action, x, y, fingerId): 1 = DOWN, 2 = MOVE, 0 = UP,
//   coordinates in ENGINE pixels (800x480, what the game believes the
//   screen is). The fullscreen viewport is stretched x1.2 / x1.1333 to
//   the real 960x544 by glViewport_soloader (direct scaling, no FBO),
//   so engine_x = screen_x * 800/960, engine_y = screen_y * 480/544.
// - nativeOnKeyDown/Up only feeds Game::OnKeyPressed/Released, which handles
//   KEYCODE_BACK (4) -> action 0x38, KEYCODE_MENU (82) -> action 0x39,
//   key 24 -> action 0x3a. There are NO keycodes for fire/jump/aim/cover:
//   all gameplay actions go through TouchManager touch areas + the virtual
//   joystick (left, touch radius ~70px around its GUI position) + free-camera
//   drag (right side). The in-game button layout is user-customizable
//   (options menu), so the coordinates below are the DEFAULT Gameloft layout
//   -- tune them if you moved the buttons.
// - Synthetic touches use fingerIds 50..65, outside the real front-touch id
//   range, so they never collide with real finger tracking below.
// Button positions measured on real 960x544 gameplay screenshot
// (screenshots/dc/2026-09-06/2026-09-06-033741.jpg, exploration scene),
// converted to engine space (x*800/960, y*480/544):
//   R                 -> FIRE (gun icon, bottom-center-right)
//   L / SQUARE        -> AIM (small crosshair, bottom-right corner, hold)
//   CROSS             -> JUMP/climb (runner icon, mid-right)
//   CIRCLE            -> HAND/interact top-right slot (contextual: hand,
//                        cover, etc. depending on scene)
//   TRIANGLE          -> RELOAD/weapon (GUESS: left of FIRE; only visible
//                        in combat -- verify on hardware, harmless if empty)
//   Left stick / Dpad -> virtual joystick (down + drag around its center)
//   Right stick       -> free-look camera (drag on right half)
// ---------------------------------------------------------------------------
#define F_FIRE_X   621
#define F_FIRE_Y   422
#define F_AIM_X    738
#define F_AIM_Y    432
#define F_ACT_X    738
#define F_ACT_Y    304
#define F_COVER_X  738
#define F_COVER_Y  176
#define F_WPN_X    542
#define F_WPN_Y    424

#define F_JOY_CX   129
#define F_JOY_CY   406
#define F_JOY_R    50
// Deadzone (analog units, 0..128): 20 absorbs stick drift/noise without
// hurting response; output is rescaled so full deflection = full radius.
#define F_JOY_DZ   20

#define F_CAM_X    583
#define F_CAM_Y    240

#define FID_JOY   50
#define FID_CAM   51
#define FID_FIRE  60
#define FID_AIM   61
#define FID_ACT   62
#define FID_COVER 63
#define FID_WPN   64

// Deadzone with rescaling: values within dz snap to 0, the rest is
// stretched so max deflection still gives full radius (no range loss,
// no jump at the threshold like a plain cutoff).
static inline int dz_rescale(int v, int radius) {
    int a = v < 0 ? -v : v;
    if (a <= F_JOY_DZ) return 0;
    int t = ((a - F_JOY_DZ) * radius) / (128 - F_JOY_DZ);
    return v < 0 ? -t : t;
}

static inline void synth_touch(int action, int x, int y, int fid) {
    if (nativeGameGLSurfaceViewOnTouch)
        nativeGameGLSurfaceViewOnTouch(&jni, NULL, action, x, y, fid);
}

// Edge-triggered tap/hold button: down on press, up on release.
// mask may combine several physical buttons sharing one touch spot.
static inline void synth_button(uint32_t pressed, uint32_t released, uint32_t mask,
                                int *active, int fid, int x, int y) {
    if ((pressed & mask) && !*active) {
        *active = 1;
        synth_touch(1, x, y, fid);
    }
    if ((released & mask) && *active) {
        // Only release when NONE of the sharing buttons is still held;
        // the caller passes `released` filtered for that (see loop).
        *active = 0;
        synth_touch(0, x, y, fid);
    }
}

int main(void) {
    // Maximize hardware clocks
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Dedicate main thread to core 0
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);

    // Set up incremental file logging (and UDP log streaming, if configured) before
    // anything else logs, so a crash always leaves a log file behind to triage from.
    logger_init();

    l_info("Starting Shadow Guardian loader initialization...");
    soloader_init_all();

    int (*JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (!JNI_OnLoad) {
        fatal_error("JNI_OnLoad not found in libshadowguardian.so!");
    }
    l_info("Calling JNI_OnLoad...");
    JNI_OnLoad(&jvm);

    resolve_entrypoints();

    gl_init();
    l_info("vitaGL initialized successfully.");

    audio_init();

    // Sequence corresponding to ShadowGuardian.onCreate & onSurfaceCreated
    l_info("Sending device info via nativeGetInfo...");
    // nativeGetInfo calls env->GetStringUTFChars() on each argument, which expects
    // a real jstring (a FalsoJNI JavaString*), not a raw C string literal -- passing
    // literals directly here made GetStringUTFChars misread the literal's bytes as a
    // JavaString struct and crash. Build proper jstrings via NewStringUTF instead.
    nativeGetInfo(&jni, NULL,
                  (jstring)jni->NewStringUTF(&jni, "US"),
                  (jstring)jni->NewStringUTF(&jni, "GL_00"),
                  (jstring)jni->NewStringUTF(&jni, "Sony_PlayStationVita"),
                  (jstring)jni->NewStringUTF(&jni, "2.3.4"),
                  (jstring)jni->NewStringUTF(&jni, "Sony"),
                  (jstring)jni->NewStringUTF(&jni, "PlayStationVita"),
                  (jstring)jni->NewStringUTF(&jni, "PlayStationVita"));

    l_info("Initializing GLResLoader...");
    nativeGLResLoaderInit(&jni, NULL);

    l_info("Initializing Device...");
    nativeDeviceInit(&jni, NULL);

    l_info("Initializing GameRenderer (manufacturer=4, %dx%d, lang=0)...", SCREEN_W, SCREEN_H);
    nativeGameRendererInit(&jni, NULL, 4, SCREEN_W, SCREEN_H, 0);

    l_info("Initializing ShadowGuardian...");
    nativeShadowGuardianInit(&jni, NULL);

    l_info("Calling GameRenderer.nativeResize(%dx%d)...", SCREEN_W, SCREEN_H);
    nativeGameRendererResize(&jni, NULL, SCREEN_W, SCREEN_H);

    uintptr_t *s_impl_got_ptr = (uintptr_t *)(0x98000000 + 0x3a0e7c);
    l_info("[DIAG] GOT entry 0x983a0e7c: points to %p", (void *)*s_impl_got_ptr);
    if (*s_impl_got_ptr) {
        uintptr_t s_impl_val = *(uintptr_t *)*s_impl_got_ptr;
        l_info("[DIAG] Value at *GOT (%p) = %p", (void *)*s_impl_got_ptr, (void *)s_impl_val);
        if (s_impl_val) {
            uintptr_t driver_val = *(uintptr_t *)(s_impl_val + 4);
            l_info("[DIAG] s_impl->driver = %p", (void *)driver_val);
        }
    }
    uintptr_t s_impl_sym = so_symbol(&so_mod, "_ZN3pig6System6s_implE");
    l_info("[DIAG] so_symbol(_ZN3pig6System6s_implE) = 0x%08X", (unsigned int)s_impl_sym);
    if (s_impl_sym) {
        l_info("[DIAG] *so_symbol = %p", (void *)*(uintptr_t *)s_impl_sym);
    }

    // Enable touch and controller sampling
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    SceTouchData touch;
    SceTouchData touch_old;
    memset(&touch, 0, sizeof(touch));
    memset(&touch_old, 0, sizeof(touch_old));

    uint32_t old_buttons = 0;
    uint32_t current_buttons = 0;

    int joy_active = 0, joy_x = F_JOY_CX, joy_y = F_JOY_CY;
    int cam_active = 0, cam_x = F_CAM_X, cam_y = F_CAM_Y;
    int fire_on = 0, aim_on = 0, act_on = 0, cover_on = 0, wpn_on = 0;

    l_info("Entering main render loop...");
    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

        // Physical controls -> native keys + synthetic touches.
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            old_buttons = current_buttons;
            current_buttons = pad.buttons;
            uint32_t pressed = current_buttons & ~old_buttons;
            uint32_t released = ~current_buttons & old_buttons;

            // START -> KEYCODE_BACK (4): pause / back in menus.
            // NOTE: CIRCLE no longer sends BACK (old behaviour); it is the
            // COVER/crouch touch button below, like Uncharted on Vita.
            if (pressed & SCE_CTRL_START) {
                nativeOnKeyDown(&jni, NULL, 4);
            }
            if (released & SCE_CTRL_START) {
                nativeOnKeyUp(&jni, NULL, 4);
            }

            // SELECT -> KEYCODE_MENU (82)
            if (pressed & SCE_CTRL_SELECT) {
                nativeOnKeyDown(&jni, NULL, 82);
            }
            if (released & SCE_CTRL_SELECT) {
                nativeOnKeyUp(&jni, NULL, 82);
            }

            // Fire: R trigger (hold). Synthetic DOWN once, UP on release.
            synth_button(pressed, released, SCE_CTRL_RTRIGGER,
                         &fire_on, FID_FIRE, F_FIRE_X, F_FIRE_Y);
            // Aim: L trigger (hold). Shares the spot with SQUARE below.
            if ((pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_SQUARE)) && !aim_on) {
                aim_on = 1;
                synth_touch(1, F_AIM_X, F_AIM_Y, FID_AIM);
            }
            if (!(current_buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_SQUARE)) && aim_on) {
                aim_on = 0;
                synth_touch(0, F_AIM_X, F_AIM_Y, FID_AIM);
            }
            // ACT contextual (jump/run/grab/climb): CROSS (hold/tap).
            synth_button(pressed, released, SCE_CTRL_CROSS,
                         &act_on, FID_ACT, F_ACT_X, F_ACT_Y);
            // Cover/crouch: CIRCLE (hold/tap).
            synth_button(pressed, released, SCE_CTRL_CIRCLE,
                         &cover_on, FID_COVER, F_COVER_X, F_COVER_Y);
            // Weapon switch: TRIANGLE (tap).
            synth_button(pressed, released, SCE_CTRL_TRIANGLE,
                         &wpn_on, FID_WPN, F_WPN_X, F_WPN_Y);

            // Left stick + Dpad -> virtual joystick (finger FID_JOY).
            // Dpad gives a digital fallback sharing the same touch point.
            int lx = (int)pad.lx - 128;
            int ly = (int)pad.ly - 128;
            if (current_buttons & SCE_CTRL_LEFT)  lx -= 128;
            if (current_buttons & SCE_CTRL_RIGHT) lx += 128;
            if (current_buttons & SCE_CTRL_UP)    ly -= 128;
            if (current_buttons & SCE_CTRL_DOWN)  ly += 128;
            if (lx < -128) lx = -128;
            if (lx >  127) lx =  127;
            if (ly < -128) ly = -128;
            if (ly >  127) ly =  127;
            int ox = dz_rescale(lx, F_JOY_R);
            int oy = dz_rescale(ly, F_JOY_R);
            if (ox != 0 || oy != 0) {
                joy_x = F_JOY_CX + ox;
                joy_y = F_JOY_CY + oy;
                if (!joy_active) {
                    joy_active = 1;
                    synth_touch(1, joy_x, joy_y, FID_JOY);
                } else {
                    synth_touch(2, joy_x, joy_y, FID_JOY);
                }
            } else if (joy_active) {
                joy_active = 0;
                joy_x = F_JOY_CX;
                joy_y = F_JOY_CY;
                synth_touch(0, joy_x, joy_y, FID_JOY);
            }

            // Right stick -> free-look camera drag (finger FID_CAM).
            // Anchor in the middle of the right half; deflect to drag.
            int rx = (int)pad.rx - 128;
            int ry = (int)pad.ry - 128;
            int cox = dz_rescale(rx, 100);
            int coy = dz_rescale(ry, 100);
            if (cox != 0 || coy != 0) {
                cam_x = F_CAM_X + cox;
                cam_y = F_CAM_Y + coy;
                if (cam_x < 0) cam_x = 0;
                if (cam_x >= SCREEN_W) cam_x = SCREEN_W - 1;
                if (cam_y < 0) cam_y = 0;
                if (cam_y >= SCREEN_H) cam_y = SCREEN_H - 1;
                if (!cam_active) {
                    cam_active = 1;
                    synth_touch(1, cam_x, cam_y, FID_CAM);
                } else {
                    synth_touch(2, cam_x, cam_y, FID_CAM);
                }
            } else if (cam_active) {
                cam_active = 0;
                synth_touch(0, cam_x, cam_y, FID_CAM);
                cam_x = F_CAM_X;
                cam_y = F_CAM_Y;
            }
        }

        // Multi-touch tracking
        int samples = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        if (samples > 0) {
            // Process current active touches
            for (int i = 0; i < touch.reportNum; i++) {
                int tx = (touch.report[i].x * SCREEN_W) / 1920;
                int ty = (touch.report[i].y * SCREEN_H) / 1088;

                int was_down = 0;
                for (int j = 0; j < touch_old.reportNum; j++) {
                    if (touch.report[i].id == touch_old.report[j].id) {
                        was_down = 1;
                        break;
                    }
                }

                // Native GameGLSurfaceView: 1 = ACTION_DOWN, 2 = ACTION_MOVE
                int action = was_down ? 2 : 1;
                nativeGameGLSurfaceViewOnTouch(&jni, NULL, action, tx, ty, touch.report[i].id);
            }

            // Process released touches
            for (int i = 0; i < touch_old.reportNum; i++) {
                int still_down = 0;
                for (int j = 0; j < touch.reportNum; j++) {
                    if (touch_old.report[i].id == touch.report[j].id) {
                        still_down = 1;
                        break;
                    }
                }
                if (!still_down) {
                    int tx = (touch_old.report[i].x * SCREEN_W) / 1920;
                    int ty = (touch_old.report[i].y * SCREEN_H) / 1088;
                    // Native GameGLSurfaceView: 0 = ACTION_UP
                    nativeGameGLSurfaceViewOnTouch(&jni, NULL, 0, tx, ty, touch_old.report[i].id);
                }
            }

            memcpy(&touch_old, &touch, sizeof(touch));
        }

        // Render tick
        nativeGameRendererRender();
        gl_swap();
    }

    audio_shutdown();
    sceKernelExitDeleteThread(0);
    return 0;
}
