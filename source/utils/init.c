/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <stdio.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

static const char *SHADER_PATCH_FILES[] = {
    "interactible_basic.xml",
    "interactible_bump.xml",
    "interactible_shadowmap.xml",
};

// Bug #28 (2026-09-16): interactible_basic/_bump/_shadowmap.xml as shipped
// in GloftSGHP's original Android data use `vec4(1)` and (_bump only) an
// undeclared `OutlineSize` uniform read further down in the shader body.
// Android's GLES driver accepts that, but vitaGL's Shark shader compiler
// doesn't -- it fails all three shaders outright (`s->prog` stays NULL in
// compile_shader()). That's the actual root cause behind the interactible_
// bump shader-cache crash fixed in f577aa8/3c9d3fb: those fixes only made
// the failed compile stop crashing, they never made the shaders compile,
// so testers on a stock data dump still got broken/missing rendering. A
// corrected copy of these 3 files (explicit `1.0` literals + the missing
// uniform declared) ships inside the VPK and is written over whatever is
// on the data card on every boot, so this is fixed regardless of which
// GloftSGHP dump a tester installed from.
static void apply_shader_patches() {
    const char *shaders_dir = DATA_PATH "shaders/";
    if (!is_dir(DATA_PATH "shaders") && is_dir(DATA_PATH "GloftSGHP/shaders")) {
        shaders_dir = DATA_PATH "GloftSGHP/shaders/";
    }

    for (int i = 0; i < sizeof(SHADER_PATCH_FILES) / sizeof(SHADER_PATCH_FILES[0]); i++) {
        char src[128];
        char dst[256];
        snprintf(src, sizeof(src), "app0:shader_patches/%s", SHADER_PATCH_FILES[i]);
        snprintf(dst, sizeof(dst), "%s%s", shaders_dir, SHADER_PATCH_FILES[i]);

        if (file_copy(src, dst)) {
            l_info("Shader patch applied: %s", dst);
        } else {
            l_error("Could not apply shader patch to %s", dst);
        }
    }
}

void soloader_init_all() {
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }

    if (!file_exists(DATA_PATH "sprites_1_7") &&
        !file_exists(DATA_PATH "sprites_1_6") &&
        !file_exists(DATA_PATH "GloftSGHP/sprites_1_6") &&
        !file_exists(DATA_PATH "GloftSGHP/sprites_1_7")) {
        fatal_error("Missing game data files in %s.\n\n"
                    "Please make sure you copied all game asset files (including 'sprites_1_6' or 'sprites_1_7') "
                    "directly into %s.", DATA_PATH, DATA_PATH);
    }

    // Bug #25 (2026-09-12): a tester's console crashed deep in gameplay
    // (SpriteMgr::LoadSprite, same PC/LR as Bug #23/#24) after the log showed
    // BOTH "gui_1_6" and "sprites_1_6" failing stat() (flat and GloftSGHP/
    // fallback), even though the sprites_1_7/sprites_1_6 check above passed.
    // gui_1_6 is a separate, independently-required Lib chunk file that the
    // check above never validated -- a data copy missing only gui_1_6 (or
    // only sprites_1_6, with a stale/partial sprites_1_7 satisfying the check
    // above) still passed startup and crashed later with an opaque native
    // STLport locale abort instead of a clear message. Validate it the same
    // way sprites_1_6/1_7 already are.
    if (!file_exists(DATA_PATH "gui_1_7") &&
        !file_exists(DATA_PATH "gui_1_6") &&
        !file_exists(DATA_PATH "GloftSGHP/gui_1_6") &&
        !file_exists(DATA_PATH "GloftSGHP/gui_1_7")) {
        fatal_error("Missing game data files in %s.\n\n"
                    "Please make sure you copied all game asset files (including 'gui_1_6' or 'gui_1_7') "
                    "directly into %s.", DATA_PATH, DATA_PATH);
    }

    if (!file_exists(DATA_PATH "res") &&
        !file_exists(DATA_PATH "GloftSGHP/res")) {
        fatal_error("Missing 'res' folder in %s.\n\n"
                    "Please make sure you copied all game asset folders (res, shaders, textures, models, etc.) "
                    "directly into %s.", DATA_PATH, DATA_PATH);
    }

    apply_shader_patches();

    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }

    settings_load();
    l_success("Settings loaded.");

    so_relocate(&so_mod);
    l_success("SO relocated.");

    resolve_imports(&so_mod);
    l_success("SO imports resolved.");

    so_patch();
    l_success("SO patched.");

    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    so_initialize(&so_mod);
    l_success("SO initialized.");

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");
}
