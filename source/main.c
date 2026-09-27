#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include "audio.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include "video.h"
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

#include <stdbool.h>

so_module so_mod;
bool g_hide_virtual_buttons = true;

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
// Button positions RE-MEASURED with a pixel grid overlay on the real 960x544
// gameplay screenshot (screenshots/dc/2026-09-06/2026-09-06-033741.jpg,
// exploration scene) after hardware testing showed the previous guesses were
// wrong (TRIANGLE, bound to the old F_WPN spot, was firing the gun instead of
// switching weapons -- that spot was ~100px from the real weapon icon and
// inside the FIRE button's touch hit-radius). Converted to engine space
// (x*800/960, y*480/544):
//   R        -> FIRE (gun icon, bottom-right corner)          real (750,511)
//   L        -> AIM (small crosshair/reticle, bottom-right)   real (905,503)
//   CROSS    -> ACT: running-man icon (jump/run/climb)        real (870,374)
//   CIRCLE   -> GRAB: hand icon (interact/pick up objects)    real (870,172)
//   SQUARE   -> weapon switch (see below, NOT a plain tap on the icon)
//   SELECT   -> toggle showing the touch controls (debug/accessibility,
//               used to be CIRCLE before CIRCLE became GRAB)
//   Left stick / Dpad -> virtual joystick (down + drag around its center)
//   Right stick       -> free-look camera (continuous drag, see below)
//
// Weapon switch is NOT a simple tap-to-toggle button like the others: per
// out_ghidra.c:86760-86813 (the PlayerCtrl input handler), the weapon-icon
// touch region is read via TouchManager::FindTouch and then the code branches
// on the DRAG DELTA of that touch (`curX - startX`) -- if the drag is bigger
// than ~30 units, its SIGN alone picks Actor::SetNextWeapon() (dragged right)
// or Actor::SetPreviousWeapon() (dragged left); only a near-zero-delta TAP
// falls back to fixed X thresholds (365/450) whose coordinate space we could
// not confirm from static analysis alone (they don't line up with the icon's
// own on-screen position, so they're likely in some internal/reference space,
// not the 800x480 GameGLSurfaceView space every other button here uses).
// Rather than guess those thresholds and risk another wrong-button bug like
// the original TRIANGLE guess, SQUARE synthesizes a DOWN-then-MOVE-right-then-
// UP swipe starting at the confirmed weapon icon spot: the swipe distance
// (F_WPN_SWIPE_DX) only needs to clear the ~30-unit drag threshold, which it
// does by a wide margin regardless of which coordinate space that threshold
// turns out to be in, and the sign-based logic doesn't depend on the disputed
// 365/450 constants at all.
// ---------------------------------------------------------------------------
#define F_FIRE_X   625
#define F_FIRE_Y   451
#define F_AIM_X    754
#define F_AIM_Y    444
#define F_ACT_X    725
#define F_ACT_Y    330
#define F_GRAB_X   725
#define F_GRAB_Y   152
#define F_WPN_X    688
#define F_WPN_Y    30
// How far right the synthetic swipe travels from F_WPN_X. Clamped so
// F_WPN_X + F_WPN_SWIPE_DX stays inside the 800-wide engine viewport.
#define F_WPN_SWIPE_DX 100

#define F_JOY_CX   129
#define F_JOY_CY   406
#define F_JOY_R    50
// Deadzone (analog units, 0..128): 20 absorbs stick drift/noise without
// hurting response; output is rescaled so full deflection = full radius.
#define F_JOY_DZ   40

#define F_CAM_X       583
#define F_CAM_Y       240
#define F_CAM_WRAP_LIMIT 50000
#define LOOK_DZ       0.15f
// Velocidad maxima (nivel 10). Sensibilidad regulable en juego:
// SELECT + D-pad arriba/abajo, niveles 1..10 (default 4 = 40%),
// guardada en DATA_PATH"camera_sens.txt" con OSD de barra.
#define LOOK_SPEED_X  14.0f
#define LOOK_SPEED_Y  9.0f
#define CAM_LEVEL_DEFAULT 4
#define CAM_LEVEL_MAX 10
#define CAM_CFG       DATA_PATH "camera_sens.txt"

#define BTN_FIRE (SCE_CTRL_RTRIGGER | SCE_CTRL_R1)
#define BTN_AIM  (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)

#define FID_JOY   50
#define FID_CAM   51
#define FID_FIRE  60
#define FID_AIM   61
#define FID_ACT   62
#define FID_GRAB  63
#define FID_WPN   64

// Deadzone with rescaling for move joystick: values within dz snap to 0,
// the rest is stretched so max deflection still gives full radius.
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
        *active = 0;
        synth_touch(0, x, y, fid);
    }
}

// Camara fluida:
// - Arrastre continuo por velocidad: el touch se mantiene activo mientras el
//   stick este inclinado. No se suelta repetidamente para evitar micro-pausas
//   por reseteo de inercia del motor (StartFreeCamera).
// - Curva con deadzone + respuesta cuadratica para apuntar fino con poca inclinacion.
// - Acumulador float: inclinaciones leves avanzan <1px/frame sin perderse ni dar saltos.
// - Sensibilidad persistente en camera_sens.txt con barra OSD.
static float look_fx = F_CAM_X, look_fy = F_CAM_Y;
static int cam_active = 0;
static int cam_x = F_CAM_X;
static int cam_y = F_CAM_Y;
static int cam_level = CAM_LEVEL_DEFAULT;
static uint64_t cam_osd_until = 0;

static void cam_load(void) {
    SceUID fd = sceIoOpen(CAM_CFG, SCE_O_RDONLY, 0);
    if (fd < 0)
        return;
    char b[8] = { 0 };
    sceIoRead(fd, b, sizeof(b) - 1);
    sceIoClose(fd);
    int v = atoi(b);
    if (v >= 1 && v <= CAM_LEVEL_MAX)
        cam_level = v;
}

static void cam_save(void) {
    SceUID fd = sceIoOpen(CAM_CFG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return;
    char b[8];
    int n = sceClibSnprintf(b, sizeof(b), "%d\n", cam_level);
    sceIoWrite(fd, b, n);
    sceIoClose(fd);
}

static void cam_adjust(int d) {
    int v = cam_level + d;
    if (v < 1) v = 1;
    if (v > CAM_LEVEL_MAX) v = CAM_LEVEL_MAX;
    cam_level = v;
    cam_save();
    cam_osd_until = sceKernelGetProcessTimeWide() + 1500000;
    l_info("[cam] sensibilidad %d/%d", cam_level, CAM_LEVEL_MAX);
}

// Barra de sensibilidad con glScissor+glClear (sin shaders).
// Guarda y restaura el estado GL previo.
static void cam_osd_draw(void) {
    if (!cam_osd_until || sceKernelGetProcessTimeWide() > cam_osd_until)
        return;
    GLint fbo, box[4];
    GLfloat cc[4];
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_SCISSOR_BOX, box);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    if (fbo)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    const int bw = 22, bh = 14, gap = 4;
    int x0 = (960 - (CAM_LEVEL_MAX * (bw + gap) - gap)) / 2;
    int y0 = 544 - 20 - bh;   // GL: origen abajo -> 20 px desde arriba
    glScissor(x0 - 4, y0 - 4, CAM_LEVEL_MAX * (bw + gap) - gap + 8, bh + 8);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int i = 0; i < CAM_LEVEL_MAX; i++) {
        glScissor(x0 + i * (bw + gap), y0, bw, bh);
        if (i < cam_level)
            glClearColor(0.2f, 0.8f, 1.0f, 1.0f);
        else
            glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glScissor(box[0], box[1], box[2], box[3]);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (!sc)
        glDisable(GL_SCISSOR_TEST);
    if (fbo)
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

static float look_curve(float v) {
    float a = v < 0 ? -v : v;
    if (a <= LOOK_DZ)
        return 0.0f;
    float t = (a - LOOK_DZ) / (1.0f - LOOK_DZ);
    if (t > 1.0f)
        t = 1.0f;
    t = 0.35f * t + 0.65f * t * t;
    return v < 0 ? -t : t;
}

static void syn_look(float dx, float dy, bool in_game) {
    if (!in_game) {
        if (cam_active) {
            cam_active = 0;
            synth_touch(0, cam_x, cam_y, FID_CAM);
            cam_x = F_CAM_X;
            cam_y = F_CAM_Y;
            look_fx = F_CAM_X;
            look_fy = F_CAM_Y;
        }
        return;
    }
    float k = (float)cam_level / (float)CAM_LEVEL_MAX;
    float vx = look_curve(dx) * LOOK_SPEED_X * k;
    float vy = look_curve(dy) * LOOK_SPEED_Y * k;
    if (vx == 0.0f && vy == 0.0f) {
        if (cam_active) {
            synth_touch(0, cam_x, cam_y, FID_CAM);
            cam_active = 0;
            cam_x = F_CAM_X;
            cam_y = F_CAM_Y;
            look_fx = F_CAM_X;
            look_fy = F_CAM_Y;
        }
        return;
    }
    if (!cam_active) {
        cam_active = 1;
        look_fx = F_CAM_X;
        look_fy = F_CAM_Y;
        cam_x = F_CAM_X;
        cam_y = F_CAM_Y;
        synth_touch(1, F_CAM_X, F_CAM_Y, FID_CAM);
    }
    look_fx += vx;
    look_fy += vy;
    int nx = (int)look_fx, ny = (int)look_fy;
    if (nx != cam_x || ny != cam_y) {
        cam_x = nx;
        cam_y = ny;
        synth_touch(2, nx, ny, FID_CAM);
    }
    // Failsafe amplio para evitar overflow en caso improbable de mantener
    // el stick inclinado por minutos seguidos sin soltar. En uso normal
    // (giros 360), el touch permanece activo de forma continua y fluida.
    if (nx < F_CAM_X - F_CAM_WRAP_LIMIT || nx > F_CAM_X + F_CAM_WRAP_LIMIT ||
        ny < F_CAM_Y - F_CAM_WRAP_LIMIT || ny > F_CAM_Y + F_CAM_WRAP_LIMIT) {
        synth_touch(0, nx, ny, FID_CAM);
        look_fx = F_CAM_X;
        look_fy = F_CAM_Y;
        cam_x = F_CAM_X;
        cam_y = F_CAM_Y;
        synth_touch(1, F_CAM_X, F_CAM_Y, FID_CAM);
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
    
    video_init();
    video_play("video/logo.m4v");

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
    int fire_on = 0, aim_on = 0, act_on = 0, grab_on = 0;

    cam_load();
    l_info("BOOT: camera sensitivity %d/%d (SELECT + D-pad arriba/abajo)", cam_level, CAM_LEVEL_MAX);

    l_info("Entering main render loop...");
    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

        // GS_GamePlay (and its GUILevel) only exists once a level is loaded, so this is a
        // no-op (returns false) until gameplay actually begins. Called every frame (not just
        // once) because the engine itself re-shows some of these items on its own during
        // normal gameplay (e.g. contextual aim feedback) -- a one-shot apply at startup got
        // silently overridden later and left the aim button stuck visible on hardware, so we
        // now keep stomping the alpha back to hidden every frame instead of latching.
        extern bool set_virtual_buttons_visible(bool visible);
        set_virtual_buttons_visible(!g_hide_virtual_buttons);

        // Physical controls -> native keys + synthetic touches.
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            old_buttons = current_buttons;
            current_buttons = pad.buttons;
            uint32_t pressed = current_buttons & ~old_buttons;
            uint32_t released = ~current_buttons & old_buttons;

            // START -> KEYCODE_BACK (4): pause / back in menus.
            if (pressed & SCE_CTRL_START) {
                nativeOnKeyDown(&jni, NULL, 4);
            }
            if (released & SCE_CTRL_START) {
                nativeOnKeyUp(&jni, NULL, 4);
            }

            // SELECT + D-Pad UP/DOWN: sensibilidad de camara (1..10, persistente).
            // SELECT solo: alterna visibilidad de controles virtuales al soltar.
            static int select_combo_used = 0;
            if (pressed & SCE_CTRL_SELECT) {
                select_combo_used = 0;
            }
            if (current_buttons & SCE_CTRL_SELECT) {
                if (pressed & SCE_CTRL_UP) {
                    cam_adjust(+1);
                    select_combo_used = 1;
                    pressed &= ~SCE_CTRL_UP;
                } else if (pressed & SCE_CTRL_DOWN) {
                    cam_adjust(-1);
                    select_combo_used = 1;
                    pressed &= ~SCE_CTRL_DOWN;
                }
            }
            if (released & SCE_CTRL_SELECT) {
                if (!select_combo_used) {
                    g_hide_virtual_buttons = !g_hide_virtual_buttons;
                }
            }
            if (select_combo_used) {
                released &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN);
            }

            // Fire: R trigger / R1 (hold). Synthetic DOWN once, UP on release.
            synth_button(pressed, released, BTN_FIRE,
                         &fire_on, FID_FIRE, F_FIRE_X, F_FIRE_Y);
            // Aim: L trigger / L1 only (hold).
            synth_button(pressed, released, BTN_AIM,
                         &aim_on, FID_AIM, F_AIM_X, F_AIM_Y);
            // ACT contextual (jump/run/climb, runner icon): CROSS (hold/tap).
            synth_button(pressed, released, SCE_CTRL_CROSS,
                         &act_on, FID_ACT, F_ACT_X, F_ACT_Y);
            // Grab/interact (hand icon): CIRCLE (hold/tap).
            synth_button(pressed, released, SCE_CTRL_CIRCLE,
                         &grab_on, FID_GRAB, F_GRAB_X, F_GRAB_Y);
            // D-Pad -> Native DPAD Keys for Menu Navigation
            if (pressed & SCE_CTRL_UP) nativeOnKeyDown(&jni, NULL, 19);
            if (released & SCE_CTRL_UP) nativeOnKeyUp(&jni, NULL, 19);

            if (pressed & SCE_CTRL_DOWN) nativeOnKeyDown(&jni, NULL, 20);
            if (released & SCE_CTRL_DOWN) nativeOnKeyUp(&jni, NULL, 20);

            if (pressed & SCE_CTRL_LEFT) nativeOnKeyDown(&jni, NULL, 21);
            if (released & SCE_CTRL_LEFT) nativeOnKeyUp(&jni, NULL, 21);

            if (pressed & SCE_CTRL_RIGHT) nativeOnKeyDown(&jni, NULL, 22);
            if (released & SCE_CTRL_RIGHT) nativeOnKeyUp(&jni, NULL, 22);

            // Weapon switch: SQUARE (tap = synthetic swipe-right over the weapon
            // icon; see the big comment above F_FIRE_X for why a swipe and not a
            // plain tap). No hold state needed -- the whole down/move/up sequence
            // fires once per press.
            if (pressed & SCE_CTRL_SQUARE) {
                synth_touch(1, F_WPN_X, F_WPN_Y, FID_WPN);
                synth_touch(2, F_WPN_X + F_WPN_SWIPE_DX, F_WPN_Y, FID_WPN);
                synth_touch(0, F_WPN_X + F_WPN_SWIPE_DX, F_WPN_Y, FID_WPN);
            }
            // Left stick -> virtual joystick (finger FID_JOY).
            // Only generate touch events if we are in-game (GUI is visible),
            // otherwise stick drift can click things in the main menu.
            extern bool *s_isGUIVisible_ptr;
            bool in_game = s_isGUIVisible_ptr ? *s_isGUIVisible_ptr : true;
            
            if (in_game) {
                int lx = (int)pad.lx - 128;
                int ly = (int)pad.ly - 128;
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
            } else if (joy_active) {
                // If we entered menu while holding stick, release it
                joy_active = 0;
                synth_touch(0, joy_x, joy_y, FID_JOY);
            }

            // Right stick -> free-look camera (NOVA 2 style fluid continuous drag).
            // Curva cuadratica, acumulador float y wrap instantaneo.
            float ldx = ((float)pad.rx - 128.0f) / 128.0f;
            float ldy = ((float)pad.ry - 128.0f) / 128.0f;
            syn_look(ldx, ldy, in_game);
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
        cam_osd_draw();
        gl_swap();
    }

    audio_shutdown();
    sceKernelExitDeleteThread(0);
    return 0;
}
