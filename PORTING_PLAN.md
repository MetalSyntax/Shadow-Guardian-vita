# Plan de Port — Shadow Guardian (PS Vita)

> Generado por psvita-port-toolkit el 2026-09-01. Punto de partida con lo detectado automáticamente --
confirmar todo con objdump/Ghidra/jadx a mano antes de asumirlo como cierto.

## 0. Contexto

- **Juego:** Shadow Guardian
- **Paquete Java:** com.gameloft.android.ANMP.GloftSGHP.ML
- **APK original:** `Shadow-Guardian-HD-v1-0-1-UK-RU.apk`
- **TITLEID asignado:** `PSVSGHD01`

**¿Motor conocido?** Revisar si algún port hermano (bajo la misma BASE_DIR) comparte motor antes de
reusar su código -- confirmar con símbolos JNI reales, no por analogía superficial.

## 1. Detección automática

- **ABI(s):** armeabi-v7a
- **ABI elegida:** armeabi-v7a
- **Nota de arquitectura:** armeabi-v7a presente -> ARMv7 (hard-float/NEON disponible). El CPU de Vita (Cortex-A9) corre esto sin traducción.
- **Versión de GLES:** valor no estándar en manifest: 0x20000 (declarado en AndroidManifest.xml)

## 2. .so encontrados (ABI armeabi-v7a)

- `shadowguardian_extract/lib/armeabi-v7a/._libStormGLOFT.so` (4 KB)
- `shadowguardian_extract/lib/armeabi-v7a/._libshadowguardian.so` (4 KB)
- `shadowguardian_extract/lib/armeabi-v7a/libStormGLOFT.so` (807 KB)
- `shadowguardian_extract/lib/armeabi-v7a/libshadowguardian.so` (3732 KB)


## 3. Exports JNI (convención `Java_*`)

Confirmados con `nm -D` en `libshadowguardian.so` (19 funciones dinámicas exportadas):
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeGetInfo`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOrientation`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeAccelerator`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeCanInterrupt`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeSetCamIntterupt`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnKeyDown`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnKeyUp`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnExitIGP`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeOnExitGLLive`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GLResLoader_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GLUtils_Device_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeInit`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeResize`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeRender`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameRenderer_nativeDone`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativePause`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativeResume`
- `Java_com_gameloft_android_ANMP_GloftSGHP_ML_GameGLSurfaceView_nativeOnTouch`
- `JNI_OnLoad`

## 4. Checklist

- [x] Repo creado desde soloader-boilerplate, git init, .gitignore anti-DMCA.
- [x] APK decompilado (jadx) y .so decompilado(s) (Ghidra) -- ver sección 2/3.
- [x] Análisis del motor real (ciclo de vida nativo, reuso de otro port o boilerplate genérico).
- [x] Bootstrap del loader: so_file_load/so_relocate/so_resolve, primer build.
- [x] Tabla JNI (FalsoJNI): registrar exports + callbacks hacia "Java".
- [x] Gráficos (wrappers GL según versión detectada -- vitaGL GLES2).
- [x] Input, Audio, Assets, LiveArea/VPK (sceAudioOut PCM mixer en Core 1, multi-touch real 960x544, botones físicos).
- [x] Primer arranque y pruebas en hardware real (Fase 5 — Superados 12 bugs de arranque, juego llegando al menú principal con música activa).

## 5. Herramientas

Este port se gestiona con **psvita-port-toolkit** (standalone, fuera de este repo). Desde el
toolkit: `Continuar con un port existente` → elegí esta carpeta (ya tiene `.psvita-toolkit.json`).

## Auto-detected lifecycle methods (psvita-toolkit)

Native methods whose name matches a well-known Android/GL app lifecycle hook --
these are the ones `main.c`/the loader most likely needs to call directly to
drive the game (there's no real Android `Activity`/`GLSurfaceView` calling them
for you).

- `com.gameloft.android.ANMP.GloftSGHP.ML.GLMediaPlayer.nativeInit(int)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.GLResLoader.nativeInit(void)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.GLUtils.Device.nativeInit(void)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.GameRenderer.nativeInit(int, int, int, int)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.GameRenderer.nativeRender(void)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.GameRenderer.nativeResize(int, int)`
- `com.gameloft.android.ANMP.GloftSGHP.ML.ShadowGuardian.nativeInit(void)`
