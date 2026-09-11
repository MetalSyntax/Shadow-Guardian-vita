#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "utils/logger.h"
#include "utils/utils.h"
#include "audio.h"

static int dummy_java_object = 0;

static void method_void_stub(jmethodID id, va_list args) {
    (void)args;
    l_debug("[Java] void stub called (id=%d)", (int)id);
}

static void method_exit(jmethodID id, va_list args) {
    (void)args;
    l_info("[Java] game requested Exit / sendAppToBackground (id=%d)", (int)id);

    // On Android this JNI call is followed by the Activity finishing / the process being
    // killed by the OS. Our stub used to just log and return, leaving control right back
    // inside the .so's own game loop -- but the engine's shutdown sequence (visible in the
    // log right before this call as "### Shutting down") already tears down its own
    // singletons (SoundMgr among them) on the assumption the process is about to die. With
    // no real process death, Game::FrameUpdate() runs at least one more frame and calls
    // SoundMgr::Update() with a NULL "this" -- confirmed via psp2dmp
    // (shadowguardian-psp2core-1789098820-0x0001da2fcf): Data abort, R0=0x0, PC resolves to
    // SoundMgr::Update()+0x1c (dereferences this+0xd8), LR to Game::FrameUpdate()+0x80.
    // Actually terminating here, like fatal_error() does in utils/dialog.c, matches what
    // the .so's shutdown code already assumes happened.
    logger_flush();
    sceKernelExitProcess(0);
}

static jobject object_dummy(jmethodID id, va_list args) {
    (void)args;
    return (jobject)&dummy_java_object;
}

static jobject string_empty(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "");
}

static jobject string_data_path(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, DATA_PATH);
}

static jobject string_package(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "com.gameloft.android.ANMP.GloftSGHP.ML");
}

static jobject string_country(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "US");
}

static jobject string_device_model(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "PlayStationVita");
}

static jobject string_device_carrier(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "Sony");
}

static jobject string_device_imei(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "358912345678901");
}

static jobject string_device_user_agent(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "Mozilla/5.0 (Linux; U; Android 2.3.4; en-us; PlayStationVita Build/GINGERBREAD)");
}

static jboolean boolean_false(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_FALSE;
}

static jboolean boolean_true(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_TRUE;
}

static jint integer_zero(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 0;
}

static jint integer_one(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 1;
}

static jint integer_width(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 960;
}

static jint integer_height(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 544;
}

static jint integer_manufacturer_sony(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 4; // Sony profile
}

static jlong current_time_millis(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jlong)(sceKernelGetProcessTimeWide() / 1000);
}

// ---- AudioTrack ----
static jobject audiotrack_init(jmethodID id, va_list args) {
    (void)args;
    l_debug("[Java] AudioTrack.<init> called");
    return (jobject)&dummy_java_object;
}

static jint audiotrack_get_min_buffer_size(jmethodID id, va_list args) {
    (void)args;
    l_debug("[Java] AudioTrack.getMinBufferSize called");
    return 16384;
}

static jint audiotrack_write(jmethodID id, va_list args) {
    (void)id;
    JavaDynArray *jda = (JavaDynArray *)va_arg(args, jobject);
    jint offset = va_arg(args, jint);
    jint size = va_arg(args, jint);

    if (!jda || !jda->array || size <= 0) return size;

    const char *pcm_data = (const char *)jda->array + offset;
    audio_write_pcm(pcm_data, (size_t)size);
    return size;
}

static void audiotrack_release(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    l_debug("[Java] AudioTrack release/stop called");
    audio_reset_buffer();
}

// ---- GLResLoader ----
static bool resolve_res_path(const char *name, char *out_path, size_t max_len) {
    if (!name || !name[0]) return false;

    // Clean name
    const char *p = name;
    while (*p == '.' || *p == '/') p++;

    const char *slash = strrchr(p, '/');
    const char *base = slash ? slash + 1 : p;

    // Search in raw
    snprintf(out_path, max_len, "%sres/raw/%s", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    snprintf(out_path, max_len, "%sres/raw/%s.bin", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    snprintf(out_path, max_len, "%sres/raw/%s.txt", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    // Search in drawable
    snprintf(out_path, max_len, "%sres/drawable/%s", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    snprintf(out_path, max_len, "%sres/drawable/%s.png", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    // Search in res/
    snprintf(out_path, max_len, "%sres/%s", DATA_PATH, base);
    if (file_exists(out_path)) return true;

    return false;
}

static jint res_get_length(jmethodID id, va_list args) {
    (void)id;
    const char *name = (const char *)va_arg(args, jstring);
    char path[512];
    if (!resolve_res_path(name, path, sizeof(path))) {
        l_warn("[Java] GLResLoader.getResourceLength(%s): not found", name ? name : "(null)");
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    l_debug("[Java] GLResLoader.getResourceLength(%s) = %ld", name, (long)st.st_size);
    return (jint)st.st_size;
}

static jobject res_get_bytes(jmethodID id, va_list args) {
    (void)id;
    const char *name = (const char *)va_arg(args, jstring);
    jint offset = va_arg(args, jint);
    jint length = va_arg(args, jint);
    char path[512];

    if (length <= 0 || !resolve_res_path(name, path, sizeof(path))) {
        l_warn("[Java] GLResLoader.getResourceBytes(%s, %d, %d): not found", name ? name : "(null)", (int)offset, (int)length);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (offset > 0) fseek(f, offset, SEEK_SET);

    JavaDynArray *jda = jda_alloc(length, FIELD_TYPE_BYTE);
    if (!jda) {
        fclose(f);
        return NULL;
    }
    fread(jda->array, 1, (size_t)length, f);
    fclose(f);

    l_debug("[Java] GLResLoader.getResourceBytes(%s, off=%d, len=%d) -> loaded", name, (int)offset, (int)length);
    return (jobject)jda;
}

static jobject res_get_full(jmethodID id, va_list args) {
    (void)id;
    const char *name = (const char *)va_arg(args, jstring);
    char path[512];

    if (!resolve_res_path(name, path, sizeof(path))) {
        l_warn("[Java] GLResLoader.getResourceFull(%s): not found", name ? name : "(null)");
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    JavaDynArray *jda = jda_alloc((jsize)size, FIELD_TYPE_BYTE);
    if (!jda) {
        fclose(f);
        return NULL;
    }
    fread(jda->array, 1, (size_t)size, f);
    fclose(f);

    l_debug("[Java] GLResLoader.getResourceFull(%s) -> loaded %ld bytes", name, size);
    return (jobject)jda;
}

enum {
    MID_GENERIC_VOID = 1,
    MID_EXIT,
    MID_GET_CONTEXT,
    MID_GET_INSTANCE,
    MID_GET_PATH,
    MID_GET_PACKAGE_NAME,
    MID_GET_COUNTRY,
    MID_STRING_EMPTY,
    MID_STRING_DEVICE_MODEL,
    MID_STRING_DEVICE_CARRIER,
    MID_STRING_DEVICE_IMEI,
    MID_STRING_DEVICE_USER_AGENT,
    MID_BOOLEAN_FALSE,
    MID_BOOLEAN_TRUE,
    MID_INTEGER_ZERO,
    MID_INTEGER_ONE,
    MID_GET_WIDTH,
    MID_GET_HEIGHT,
    MID_GET_MANUFACTURE,
    MID_CURRENT_TIME,

    MID_AUDIOTRACK_INIT,
    MID_AUDIOTRACK_GET_MIN_BUFFER_SIZE,
    MID_AUDIOTRACK_WRITE,
    MID_AUDIOTRACK_RELEASE,

    MID_RES_GET_LENGTH,
    MID_RES_GET_BYTES,
    MID_RES_GET_FULL,
};

NameToMethodID nameToMethodId[] = {
    // Context & system
    { MID_GET_CONTEXT, "getContext", METHOD_TYPE_OBJECT },
    { MID_GET_CONTEXT, "getResources", METHOD_TYPE_OBJECT },
    { MID_GET_CONTEXT, "getDefault", METHOD_TYPE_OBJECT },
    { MID_GET_CONTEXT, "getAssets", METHOD_TYPE_OBJECT },
    { MID_GET_INSTANCE, "getInstance", METHOD_TYPE_OBJECT },
    { MID_GET_PATH, "getPath", METHOD_TYPE_OBJECT },
    { MID_GET_PATH, "getAbsolutePath", METHOD_TYPE_OBJECT },
    { MID_GET_PACKAGE_NAME, "getPackageName", METHOD_TYPE_OBJECT },
    { MID_GET_COUNTRY, "getCountry", METHOD_TYPE_OBJECT },
    { MID_STRING_EMPTY, "getString", METHOD_TYPE_OBJECT },
    { MID_STRING_EMPTY, "toString", METHOD_TYPE_OBJECT },
    { MID_STRING_DEVICE_MODEL, "getPhoneModel", METHOD_TYPE_OBJECT },
    { MID_STRING_DEVICE_MODEL, "getDevice", METHOD_TYPE_OBJECT },
    { MID_STRING_DEVICE_CARRIER, "getCarrier", METHOD_TYPE_OBJECT },
    { MID_STRING_DEVICE_IMEI, "getIMEI", METHOD_TYPE_OBJECT },
    { MID_STRING_DEVICE_USER_AGENT, "getUserAgent", METHOD_TYPE_OBJECT },
    { MID_STRING_EMPTY, "getLanguage", METHOD_TYPE_OBJECT },
    { MID_STRING_EMPTY, "getHostName", METHOD_TYPE_OBJECT },

    // Booleans
    { MID_BOOLEAN_TRUE, "IsWifiEnable", METHOD_TYPE_BOOLEAN },
    { MID_BOOLEAN_TRUE, "isNeedGcOnFrame", METHOD_TYPE_BOOLEAN },
    { MID_BOOLEAN_FALSE, "isRoaming", METHOD_TYPE_BOOLEAN },

    // Integers
    { MID_INTEGER_ZERO, "GetPhoneLanguage", METHOD_TYPE_INT },
    { MID_INTEGER_ONE, "isWifiEnabled", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "getUniqueCode", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "createUniqueCode", METHOD_TYPE_INT },
    { MID_GET_WIDTH, "getWidth", METHOD_TYPE_INT },
    { MID_GET_HEIGHT, "getHeight", METHOD_TYPE_INT },
    { MID_GET_MANUFACTURE, "getManufacture", METHOD_TYPE_INT },

    // Audio / Media Player stubs
    { MID_INTEGER_ZERO, "getMusicDuration", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "getSoundDuration", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "getSoundStatus", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "isMediaPlaying", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "isMusicPlaying", METHOD_TYPE_INT },
    { MID_INTEGER_ZERO, "isSoundLoaded", METHOD_TYPE_INT },

    // Long
    { MID_CURRENT_TIME, "GetCurrentTime", METHOD_TYPE_LONG },

    // AudioTrack
    { MID_AUDIOTRACK_INIT, "android/media/AudioTrack/<init>", METHOD_TYPE_OBJECT },
    { MID_AUDIOTRACK_GET_MIN_BUFFER_SIZE, "getMinBufferSize", METHOD_TYPE_INT },
    { MID_AUDIOTRACK_WRITE, "write", METHOD_TYPE_INT },

    // GLResLoader
    { MID_RES_GET_LENGTH, "getResourceLength", METHOD_TYPE_INT },
    { MID_RES_GET_BYTES, "getResourceBytes", METHOD_TYPE_OBJECT },
    { MID_RES_GET_FULL, "getResourceFull", METHOD_TYPE_OBJECT },
    { MID_RES_GET_FULL, "getRawResource", METHOD_TYPE_OBJECT },

    // Activity lifecycle & actions
    { MID_EXIT, "Exit", METHOD_TYPE_VOID },
    { MID_EXIT, "sendAppToBackground", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "Pause", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "openBrowser", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "enableWifi", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "launchGLLive", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "launchIGP", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "notifyTrophy", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "sendTrackingInfo", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "InitDeviceValues", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "init", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "destroy", METHOD_TYPE_VOID },

    // Audio / AudioTrack void methods
    { MID_GENERIC_VOID, "play", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "pause", METHOD_TYPE_VOID },
    { MID_AUDIOTRACK_RELEASE, "stop", METHOD_TYPE_VOID },
    { MID_AUDIOTRACK_RELEASE, "release", METHOD_TYPE_VOID },
    { MID_AUDIOTRACK_RELEASE, "android/media/AudioTrack/stop", METHOD_TYPE_VOID },
    { MID_AUDIOTRACK_RELEASE, "android/media/AudioTrack/release", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "loadMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "loadSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "pauseAllMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "pauseAllSounds", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "pauseMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "pauseSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "playMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "playSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "resetSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "resumeMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "resumeSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "setVolume", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "stopAllMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "stopAllSounds", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "stopMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "stopSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "unloadMusic", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "unloadSound", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "updateSound", METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { MID_BOOLEAN_FALSE, boolean_false },
    { MID_BOOLEAN_TRUE, boolean_true },
};

MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};

MethodsInt methodsInt[] = {
    { MID_INTEGER_ZERO, integer_zero },
    { MID_INTEGER_ONE, integer_one },
    { MID_GET_WIDTH, integer_width },
    { MID_GET_HEIGHT, integer_height },
    { MID_GET_MANUFACTURE, integer_manufacturer_sony },
    { MID_AUDIOTRACK_GET_MIN_BUFFER_SIZE, audiotrack_get_min_buffer_size },
    { MID_AUDIOTRACK_WRITE, audiotrack_write },
    { MID_RES_GET_LENGTH, res_get_length },
};

MethodsLong methodsLong[] = {
    { MID_CURRENT_TIME, current_time_millis },
};

MethodsObject methodsObject[] = {
    { MID_GET_CONTEXT, object_dummy },
    { MID_GET_INSTANCE, object_dummy },
    { MID_GET_PATH, string_data_path },
    { MID_GET_PACKAGE_NAME, string_package },
    { MID_GET_COUNTRY, string_country },
    { MID_STRING_EMPTY, string_empty },
    { MID_STRING_DEVICE_MODEL, string_device_model },
    { MID_STRING_DEVICE_CARRIER, string_device_carrier },
    { MID_STRING_DEVICE_IMEI, string_device_imei },
    { MID_STRING_DEVICE_USER_AGENT, string_device_user_agent },
    { MID_AUDIOTRACK_INIT, audiotrack_init },
    { MID_RES_GET_BYTES, res_get_bytes },
    { MID_RES_GET_FULL, res_get_full },
};

MethodsShort methodsShort[] = {};

MethodsVoid methodsVoid[] = {
    { MID_GENERIC_VOID, method_void_stub },
    { MID_EXIT, method_exit },
    { MID_AUDIOTRACK_RELEASE, audiotrack_release },
};

const int SDK_INT = 10; // Android 2.3.4 Gingerbread
char WINDOW_SERVICE[] = "window";

NameToFieldID nameToFieldId[] = {
    { 1, "SDK_INT", FIELD_TYPE_INT },
    { 2, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
    { 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
    { 2, (jobject)WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
