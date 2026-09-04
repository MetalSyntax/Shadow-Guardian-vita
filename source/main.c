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

#define SCREEN_W 960
#define SCREEN_H 544

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
    SceTouchData touch;
    SceTouchData touch_old;
    memset(&touch, 0, sizeof(touch));
    memset(&touch_old, 0, sizeof(touch_old));

    uint32_t old_buttons = 0;
    uint32_t current_buttons = 0;

    l_info("Entering main render loop...");
    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

        // Controller buttons
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            old_buttons = current_buttons;
            current_buttons = pad.buttons;
            uint32_t pressed = current_buttons & ~old_buttons;
            uint32_t released = ~current_buttons & old_buttons;

            // START or CIRCLE -> KEYCODE_BACK (4)
            if (pressed & (SCE_CTRL_START | SCE_CTRL_CIRCLE)) {
                nativeOnKeyDown(&jni, NULL, 4);
            }
            if (released & (SCE_CTRL_START | SCE_CTRL_CIRCLE)) {
                nativeOnKeyUp(&jni, NULL, 4);
            }

            // SELECT -> KEYCODE_MENU (82)
            if (pressed & SCE_CTRL_SELECT) {
                nativeOnKeyDown(&jni, NULL, 82);
            }
            if (released & SCE_CTRL_SELECT) {
                nativeOnKeyUp(&jni, NULL, 82);
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
