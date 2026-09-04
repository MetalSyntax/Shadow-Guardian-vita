# Registro de Progreso — Shadow Guardian (PS Vita)

## Fase 1: Configuración y Preparación (Completada — 2026-09-01)
- Repo creado desde soloader-boilerplate, `.gitignore` anti-DMCA.
- APK `Shadow-Guardian-HD-v1-0-1-UK-RU.apk` copiado y extraído.
- ABI detectada: armeabi-v7a (elegida: armeabi-v7a).
- GLES detectado: valor no estándar en manifest: 0x20000 (declarado en AndroidManifest.xml)

## Fase 2: Decompilación (Completada — 2026-09-01)
- jadx: corrido, resultados en decompiled/apk_jadx/.
- Ghidra (.so): corrido para cada .so.

## Fase 3: Análisis del Motor Real (Completada — 2026-09-03)
- [x] Confirmar si comparte motor con algún port hermano:
  - **SÍ.** Motor 3D propietario de Gameloft (era 2010-2011, Xperia Play / Android 2.1-2.3).
  - Comparte arquitectura directa con Modern Combat 2: Black Pegasus (`GloftBPHP`), Dungeon Hunter 2 (`GloftD2SS`) y Sacred Odyssey (`GloftSOHP`).
  - Namespace C++ nativo: `pig::` (`pig::System`) para motor y ciclo de vida de aplicación.
  - Middleware de audio: **Vox / VoxN** (`vox::DriverAndroid`), mezcla por software en C++ a 44.1kHz estéreo y envía buffers PCM a Java vía `android.media.AudioTrack.write()`.
  - Análisis de `libStormGLOFT.so`: Se confirmó con Ghidra que es un hooker/patcher OpenGL de Gameloft para Android que intercepta llamadas en tiempo de ejecución en `libshadowguardian.so`. Es prescindible y omitido en PS Vita ya que vitaGL implementa el renderizado. El ejecutable del juego es `libshadowguardian.so`.
  - Rutas de datos requeridas: `/sdcard/gameloft/games/GloftSGHP` y `/data/data/com.gameloft.android.ANMP.GloftSGHP.ML`.
- [x] Leer decompiled/apk_jadx/sources/ y Ghidra para el ciclo de vida nativo real:
  - Secuencia de arranque: `JNI_OnLoad` -> `ShadowGuardian.nativeGetInfo` -> `GLResLoader.nativeInit` -> `Device.nativeInit` -> `GameRenderer.nativeInit` -> `ShadowGuardian.nativeInit` -> `GameRenderer.nativeResize`.
  - Main loop: `GameRenderer.nativeRender()` (render tick) + `gl_swap()`.
  - Input: `GameGLSurfaceView.nativeOnTouch(action, x, y, pointer_id)`, `ShadowGuardian.nativeOnKeyDown(keyCode)`, `ShadowGuardian.nativeOnKeyUp(keyCode)`.
  - Ciclo de suspensión: `GameGLSurfaceView.nativePause()`, `GameGLSurfaceView.nativeResume()`, `ShadowGuardian.nativeCanInterrupt()`.
- [x] Confirmar exports JNI reales y RegisterNatives:
  - Confirmado con `nm -D`: 19 funciones `Java_*` exportadas en `libshadowguardian.so` (GameRenderer, ShadowGuardian, GLResLoader, GameGLSurfaceView, Device).
  - Callbacks Java esperados por el .so: `AudioTrack` (`<init>`, `getMinBufferSize`, `write`, `play`, `pause`, `stop`, `release`), `GLResLoader` (`getResourceFull`, `getResourceBytes`, `getResourceLength`), `ShadowGuardian` (`sendAppToBackground`, `isWifiEnabled`, `Pause`, `GetPhoneLanguage`, etc.), y `Device`.
  - Símbolos dinámicos faltantes en `dynlib.c`: solo 7 símbolos (`_ZSt7nothrow`, `_ZnajRKSt9nothrow_t`, `__aeabi_d2iz`, `__dso_handle`, `cosh`, `frexpf`, `inet_addr`).

## Fase 4: Bootstrap del Loader y Enlace JNI (Completada — 2026-09-03)
- [x] Completar tabla de símbolos en `dynlib.c` con los 7 símbolos faltantes:
  - Añadidos `_ZSt7nothrow`, `_ZnajRKSt9nothrow_t`, `__aeabi_d2iz`, `__dso_handle`, `cosh`, `frexpf`, `inet_addr`.
  - Verificado por script automatizado: 279 de 279 símbolos dinámicos indefinidos de `libshadowguardian.so` están 100% resueltos.
- [x] Configurar redirección de rutas y parches de archivos en `patch.c` e `io.c`:
  - En `io.c`: implementación de `translate_path` mapeando rutas `/sdcard/gameloft/games/GloftSGHP`, `/mnt/sdcard/...`, `/data/data/...` y relativas a `DATA_PATH` (`ux0:data/shadowguardian/`).
  - En `patch.c`: hook de `_Z8initPathv` (`initPath`) para inicializar `m_gAppPath` hacia `DATA_PATH` y ejecutar `chdir(DATA_PATH)`. Hook de bypass para `ALicenseCheck_ValidateLicense`.
- [x] Implementar tabla FalsoJNI en `java.c`:
  - Mapeo de `android/media/AudioTrack` (`<init>`, `getMinBufferSize`, `write`) para el motor de audio Vox.
  - Implementación de `GLResLoader` (`getResourceFull`, `getResourceBytes`, `getResourceLength`) con soporte para cargar `res/raw/` y `res/drawable/` dinámicamente usando `JavaDynArray` (`jda_alloc`).
  - Mapeo de `ShadowGuardian` (`Exit`, `sendAppToBackground`, `isWifiEnabled`, `Pause`, `GetPhoneLanguage`, `launchGLLive`, `getManufacture`, `notifyTrophy`, `launchIGP`, `sendTrackingInfo`, `GetCurrentTime`).
  - Mapeo de `Device` (`InitDeviceValues`, `getPhoneModel`, `getUserAgent`, `getCarrier`, `getIMEI`, `IsWifiEnable`).
- [x] Configurar `CMakeLists.txt` y dependencias del loader:
  - `SO_PATH` corregido a `libshadowguardian.so`.
  - Eliminado `source/reimpl/egl.c` que colisionaba con los símbolos EGL nativos provistos por `libvitaGL.a`.
  - Añadido `lib/falso_jni/converter.c` para resolver `utf16_to_utf8` y `utf8_to_utf16`.
  - Implementado `port_trace` en `source/utils/logger.c`.
- [x] Implementar el ciclo de vida y render loop en `main.c`:
  - Secuencia de arranque respetando el orden real de `Activity`: `JNI_OnLoad` -> `nativeGetInfo` -> `nativeGLResLoaderInit` -> `nativeDeviceInit` -> `nativeGameRendererInit` -> `nativeShadowGuardianInit` -> `nativeGameRendererResize`.
  - Render loop continuo: `nativeGameRendererRender()` + `gl_swap()`.
  - Mapeo de pantalla táctil frontal a `nativeGameGLSurfaceViewOnTouch`.
- [x] Compilar primer binario ejecutable con VitaSDK:
  - Compilación y linkeo limpios sin errores.
  - `shadowguardian.vpk` (696 KB) y `eboot.bin` (614 KB) generados con éxito.

## Fase 5: Pruebas en Hardware Real y Depuración Iterativa (En Curso)
- [x] Conectar la salida de audio PCM de `audiotrack_write` a `sceAudioOut` para habilitar el sonido real del motor Vox:
  - Implementado `source/audio.h` y `source/audio.c` con mezclador de salida PCM de doble buffer a 44100Hz estéreo.
  - Hilo mezclador `sg_audio_mixer` anclado al núcleo 1 (`SCE_KERNEL_CPU_MASK_USER_1`) para no interferir con la lógica de render en el núcleo 0.
  - Conectado `audiotrack_write` en `source/java.c` consumiendo el `JavaDynArray` y enviándolo al ring buffer de audio.
  - Manejo de vaciado y reseteo en `AudioTrack.stop` y `AudioTrack.release`.
- [x] Corregir y perfeccionar sistema de entrada y controles:
  - Corregido el mapeo de acciones táctiles de Android según la decompilación de `GameGLSurfaceView`: `ACTION_DOWN` = 1, `ACTION_MOVE` = 2, `ACTION_UP` = 0.
  - Implementado seguimiento multi-touch completo de pantalla táctil capacitiva de PS Vita.
  - Mapeo de botones físicos: START y CÍRCULO mapeados a `KEYCODE_BACK` (4) para pausa y retroceso en menús; SELECT a `KEYCODE_MENU` (82).
- [x] Estructurar paquete de datos completo:
  - Contenido completo de assets de `GloftSGHP` copiado a `ux0_data/shadowguardian/` junto a `libshadowguardian.so` y `res/`.
  - Carpeta lista para ser copiada directamente a `ux0:data/shadowguardian/`.
- [x] Compilar VPK actualizado con subsistema de audio y controles:
  - Binarios `shadowguardian.vpk` (698 KB) y `eboot.bin` (616 KB) generados y actualizados.
- [x] Transferir `shadowguardian.vpk` y carpeta de datos `ux0_data/shadowguardian/` a la consola PS Vita (vía USB o FTP con VitaShell).
- [x] Ejecutar el juego en hardware real y capturar el log de arranque.

### Bug #1 (confirmado 2026-09-03): crash inmediato en el primer `nativeGetInfo`

- **Síntoma:** crash en el arranque, antes de cualquier log de "Sending device info". Dump:
  `logs/shadowguardian-psp2core-1788489281-0x00160a2cb7-eboot.bin.psp2dmp`.
- **Diagnóstico automático (triage_summary.md / analysis.txt) era engañoso:** el auto-detector de
  base del `.so` calculó `0x80d4a000` (heurística, escaneo de patrón), que resultó **incorrecto** —
  la base real y fija es `LOAD_ADDRESS = 0x98000000` (`source/utils/init.c:30`, pasado a
  `so_file_load` en `source/utils/init.c:70`). Con la base incorrecta, el reporte marcó `LR` como
  "fuera de rango" y hasta encontró una coincidencia casual de símbolos `vox::` para el `PC` que no
  tenía nada que ver con el crash real.
- **Con la base correcta (`0x98000000`):**
  - `LR = 0x980af1fc` → offset `0xaf1fc` → dentro de
    `Java_com_gameloft_android_ANMP_GloftSGHP_ML_ShadowGuardian_nativeGetInfo` (símbolo en
    `0xaf1c8`, tamaño `0x180`), justo después de la primera llamada a `GetStringUTFChars`.
  - `PC = 0x8100d310` cae dentro del módulo `shadowguardian` (el loader/eboot.bin mismo, no el
    `.so`) — consistente con el crash ocurriendo dentro de la implementación de FalsoJNI
    (`lib/falso_jni/FalsoJNI.c`), que está compilada estáticamente en el loader.
- **Causa raíz confirmada** (Ghidra, `decompiled/.../out_ghidra.c:3204`): `nativeGetInfo` recibe 7
  parámetros `jstring` (country, deviceID, deviceType, versionRelease, manufacturer, model,
  product) y para cada uno llama `env->GetStringUTFChars(param, 0)` seguido de `strdup(...)` **sin
  chequear NULL**. `source/main.c` (antes del fix) invocaba `nativeGetInfo` pasando literales C
  crudos casteados directamente a `jstring` (`(jstring)"US"`, etc.) en vez de crear un `jstring`
  real vía `jni->NewStringUTF`. `GetStringUTFChars` (FalsoJNI) interpreta su argumento como un
  `JavaString*` (`struct { JavaDynArray *utf16, *utf8; }`, ver `FalsoJNI_ImplBridge.h:143`) —
  al recibir un `char*` crudo, lee basura donde espera un puntero a `JavaDynArray`, y el
  `strdup`/acceso posterior revienta con un puntero inválido/NULL.
  - **Fix aplicado** (`source/main.c`): reemplazar cada `(jstring)"literal"` por
    `(jstring)jni->NewStringUTF(&jni, "literal")` antes de llamar a `nativeGetInfo`, igual que ya
    se hacía correctamente en los stubs de `source/java.c` (`string_country`, `string_device_model`,
    etc.). Verificado que no hay otras ocurrencias del mismo patrón (`grep '(jstring)"'` en
    `source/` no arroja más resultados).
  - **Confirmado resuelto:** la siguiente corrida en consola ya no crashea en `nativeGetInfo` —
    avanzó hasta la carga/compilación de shaders (ver Bug #2 abajo).

### Bug #2 (confirmado 2026-09-03): crash en `glCompileShader_soloader` al volcar el binario del shader

- **Síntoma:** nuevo crash más adelante en el arranque, durante la primera compilación de un
  shader. Dump: `logs/shadowguardian-psp2core-1788490314-0x00099e34b7-eboot.bin.psp2dmp`. Esta vez
  el analizador tenía el `.elf` del loader (`build/shadowguardian.elf`) y resolvió símbolos reales
  del loader correctamente (a diferencia del Bug #1, acá no hubo que corregir la base a mano).
- **Backtrace:** `PC` cae dentro de `SceLibKernel` (módulo real de Sony) → por la guía de la skill,
  el frame que importa es `LR = shadowguardian + 0x2777d` → símbolo `serialize_shader`, una función
  **interna de vitaGL** (prebuilt, `libvitaGL.a` de VitaSDK — no hay fuente vendorizada en este repo
  para editarla directamente). Más abajo en la pila: `vglGetShaderBinary`, `vglMalloc`, y
  `glCompileShader_soloader` en `source/utils/glutil.c:105`.
  R1 = 0x0 en el momento del crash, consistente con `serialize_shader` recibiendo/copiando desde un
  puntero de binario de shader inexistente.
- **Causa raíz confirmada** (`source/utils/glutil.c`, `glCompileShader_soloader`, antes del fix):
  llamaba `glCompileShader(shader)` y **sin chequear si la compilación tuvo éxito**, pasaba
  directo a `vglGetShaderBinary(shader, 32*1024, &len, bin)` para volcar el binario compilado a
  disco (`DUMP_COMPILED_SHADERS` cache, activado por defecto en `CMakeLists.txt`). Si el shader
  fallaba al compilar (GLSL no traducible/no soportado por el shader compiler `libshacccg.suprx`),
  vitaGL no tiene un binario que serializar y `serialize_shader` revienta con un puntero inválido/
  NULL en vez de fallar con gracia — un bug propio del loader (falta un guard), no de vitaGL en sí.
  - **Fix aplicado** (`source/utils/glutil.c:93-115`): agregado chequeo de `GL_COMPILE_STATUS` vía
    `glGetShaderiv` después de `glCompileShader`. Solo se llama a `vglGetShaderBinary`/`file_save`
    si la compilación tuvo éxito; si falló, se loguea el shader info log (`glGetShaderInfoLog`) sin
    tocar `vglGetShaderBinary` y se sigue sin crashear.
  - **Pendiente:** recompilar, redesplegar, y en el próximo log ver **cuál shader específico** falla
    al compilar (el `l_error` nuevo lo va a mostrar) — eso puede ser un bug nuevo (traducción GLSL
    incompleta) a diagnosticar por separado si el juego no renderiza correctamente después de este
    fix.
  - **Confirmado resuelto:** la siguiente corrida avanzó más allá de la compilación de shaders,
    hasta `Game::Init()` (ver Bug #3 abajo).

### Bug #3 (confirmado 2026-09-03): `Undefined Instruction` por uso de `SWP` (instrucción ARM obsoleta)

- **Síntoma:** crash en `Game::Init()`, cargando el pack de recursos de shaders. Dump:
  `logs/shadowguardian-psp2core-1788491311-0x000eea2e43-eboot.bin.psp2dmp`. Tipo de excepción:
  **Undefined Instruction** (no Data Abort) — la primera vez en este port que no es un null deref.
- **El analizador volvió a auto-detectar mal la base del `.so`** (`0x83c0f000` en vez de la real y
  fija `LOAD_ADDRESS=0x98000000`), marcando `PC`/`LR`/la pila entera como "fuera de rango". Recalculando
  con la base real, TODA la pila resuelve a símbolos reales y coherentes: `Game::Init` → (llamada
  virtual a través de `RenderTechniqueLoader`) → `pig::res::ResourceLoader::AddPack(string,string)` →
  `_AddPack` → `std::vector<boost::shared_ptr<IStreamLoader>>::push_back` → **`PC` cae exactamente en
  `boost::detail::shared_count::shared_count(shared_count const&)+0x54`** (el copy-ctor del control
  block de `boost::shared_ptr`, invocado al reallocar el vector y copiar un elemento ya existente).
- **Causa raíz confirmada** (desensamblado ARM real vía `arm-vita-eabi-objdump`, sin `-M force-thumb`:
  el símbolo tiene el bit de Thumb en 0, o sea que es código ARM puro, no Thumb — desensamblarlo como
  Thumb daba basura total, que fue lo que confundió el primer intento de análisis): la instrucción
  exacta en el punto del crash es **`swp r3, r6, [r5]`** — la implementación de
  `boost::detail::spinlock_pool` (usada para el refcounting de `boost::shared_ptr`) usa la instrucción
  atómica `SWP`, **obsoleta desde ARMv6 y deshabilitada por defecto en ARMv7** (como el Cortex-A9 de
  la Vita) salvo que el kernel active el bit `SCTLR.SW` — cosa que el kernel de Sony no hace. Ejecutarla
  genera exactamente una excepción de instrucción indefinida. La misma función tiene el patrón
  `swp+cmp` inlineado **3 veces** (intento inicial + 2 cuerpos del loop de reintento con backoff vía
  `sched_yield`/`nanosleep`), en los offsets `+0x54`, `+0x70`, `+0x9c` de
  `_ZN5boost6detail12shared_countC1ERKS1_`.
  - **Fix aplicado** (`source/patch.c`, `patch_swp_with_ldrex_strex()`): en `so_patch()`, cada una de
    las 3 instrucciones `SWP` confirmadas se reemplaza (patcheo binario de 4 bytes, un solo `B`
    relativo, mismo mecanismo que `so_util`'s `trampoline_ldm()` ya usa para parches de `LDM`) por un
    salto a un trampolín generado en runtime en el code cave de `so_util` que hace el intercambio
    atómico equivalente vía **`LDREX`/`STREX`** (soportadas nativamente en Cortex-A9), y vuelve
    exactamente a la instrucción siguiente a la `SWP` original (la `cmp rt,#0` colateral queda intacta
    en su lugar, sin tocar). `r12` se usa como registro de scratch para el status de `STREX`: no
    aparece en ningún lado del desensamblado de esta función (tampoco en el prólogo
    `push {r4-r9,sl,fp,lr}`), así que es seguro de pisar en los 3 sitios.
  - **Verificado:** compila limpio con el toolchain real de VitaSDK (build de prueba completo, linkeo
    y generación de `eboot.bin` exitosos) — no se pudo probar en consola física desde acá.
  - **Alcance del fix:** intencionalmente acotado a estas 3 instancias confirmadas por el crash real,
    no un escaneo ciego de todo el `.so` buscando más `SWP` (demasiado riesgo de falsos positivos en
    un binario de 3.8MB sin poder testear en hardware). Si aparece OTRO crash "Undefined Instruction"
    en cualquier otro punto, es casi seguro el mismo patrón (`spinlock_pool` inlineado en otra
    instanciación de `shared_ptr`/`weak_ptr`) — aplicar el mismo `patch_swp_with_ldrex_strex()` en el
    nuevo offset confirmado.
  - **Confirmado resuelto:** la siguiente corrida ya no crashea en los 3 sitios de `shared_count`
    (el log muestra los 3 "Patched SWP at ..." y el arranque avanza mucho más lejos, hasta
    `Game::Init` de nuevo pero esta vez pasando por el punto anterior — ver Bug #4 abajo).
- [x] Recompilar con los 3 fixes, redesplegar y volver a probar en hardware real.

### Bug #4 (confirmado 2026-09-03): mismo patrón de `SWP`, ahora en `sp_counted_base::release()`

- **Síntoma:** otro crash "Undefined Instruction", de nuevo en `Game::Init` → `ResourceLoader::AddPack`
  → `_AddPack`. Dump: `logs/shadowguardian-psp2core-1788493055-0x0009613d2f-eboot.bin.psp2dmp`. Exactamente
  la confirmación que se anticipaba al cerrar el Bug #3: el analizador auto-detectó de nuevo una base
  incorrecta (`0x83c0f000`) y encima esta vez "resolvió" varios valores de la pila (basura de stack, no
  relacionada) contra símbolos reales tipo `SkinnedMeshInstance::Render`/`SkinnedSubMesh::Skin` usando
  ESA base incorrecta — coincidencias falsas, se descartaron. Recalculando `PC`/`LR` con la base real
  (`0x98000000`, la misma confirmada en el Bug #3 y ahora también verificable directo en
  `game_log_1788493050809.txt`, línea 26: el loader logueó `nativeGetInfo -> 0x980af1c8`, que coincide
  exacto con `text_base(0x98000000) + st_value(0xaf1c8)`), `PC` cae en
  `boost::detail::sp_counted_base::release()+0x40` y `LR` en `_AddPack+0x50` (el
  `sp_counted_base::release()` que `_AddPack` llama al final para soltar la referencia extra del
  temporal local, después de que `push_back` ya copió el elemento al vector).
- **Causa raíz:** el mismo patrón de spinlock con `SWP` (Bug #3), pero en OTRA función —
  `sp_counted_base::release()` tiene su propio acquire-loop inlineado con `swp r3,r6,[r5]` en los
  offsets `+0x40` y `+0x5c` (confirmado con `arm-vita-eabi-objdump` en modo ARM real). Como ya se
  anticipó en el cierre del Bug #3: cada instanciación de `boost::shared_ptr<T>`/`weak_ptr<T>` para un
  tipo `T` distinto compila su propia copia de estas funciones, cada una con su propio `SWP` inlineado
  — este bug va a seguir reapareciendo en nuevas funciones a medida que el juego llegue a código que
  toca un `shared_ptr` de un tipo que todavía no pasó por acá.
  - **Fix aplicado** (`source/patch.c`): mismo `patch_swp_with_ldrex_strex()`, dos llamadas nuevas sobre
    `_ZN5boost6detail15sp_counted_base7releaseEv + 0x40` y `+ 0x5c`.
  - **Nota sobre el alcance:** al revisar el mecanismo de nuevo para este bug se confirmó que el fix no
    depende de que la instrucción siguiente al `SWP` sea `cmp Rt,#0` (el trampolín reproduce el efecto
    exacto de `SWP` -- intercambio atómico en `[Rn]` -- y salta de vuelta a `addr+4` para que continúe
    lo que sea que haya ahí realmente), así que es agnóstico a qué sigue. El riesgo real de un escaneo
    ciego de todo el `.so` sigue siendo los FALSOS POSITIVOS (datos embebidos en `.text`, como los
    jump-tables/literal-pools ya vistos en este port, coincidiendo por azar con el patrón de bits de
    `SWP`) — se mantiene la política de parchear solo instancias confirmadas por un crash real.
- [x] Recompilar con los 5 fixes (3 del Bug #3 + 2 de este), redesplegar y volver a probar en hardware.

### Bug menor confirmado: `file_save` no creaba el directorio destino

- **Síntoma:** en el mismo log (`game_log_1788493050809.txt`), varios `file_save: Could not open the
  specified target path "ux0:data/shadowguardian/gxp/<hash>.gxp"` al compilar shaders por primera vez
  (parte del cache `DUMP_COMPILED_SHADERS`) — la carpeta `gxp/` nunca se creaba porque `file_save()`
  (`source/utils/utils.c`) hacía `fopen(path,"wb")` directo, sin llamar antes a `file_mkpath()` (a
  diferencia de `file_load`, que si falla el `fopen` da un error más claro, y de `file_copy`, que sí
  llamaba `file_mkpath` manualmente antes de su propio `file_save` interno). No crashea, pero significa
  que el cache de shaders compilados nunca se estaba escribiendo a disco.
- **Fix aplicado**: `file_save()` ahora llama `file_mkpath(path, 0755)` al principio (creando la carpeta
  destino si hace falta), igual que ya hacía `file_load` internamente para la fuente. Se sacó el
  `file_mkpath` redundante de `file_copy` (ahora lo cubre `file_save` internamente).
- Nota: no se tocó `libvitaGL` -- el soporte de GLSL ya viene dado por la librería (ver
  `README VITAGL.md`, flags `USE_GLSL_SHADERS`/`HAVE_SHADER_CACHE`, etc.); este bug era pura y
  simplemente nuestro propio `file_save` sin crear el directorio.
- [x] Verificado que compila limpio con el toolchain real (build de prueba completo, `eboot.bin`
  generado sin errores) — no se pudo probar en consola física desde acá.

## Logging: log incremental a archivo + streaming por UDP (2026-09-03)

- **CONFIRMADO FUNCIONANDO:** esta sesión ya se benefició del log incremental para diagnosticar el
  Bug #4 y el bug de `file_save` -- sin `game_log_1788493050809.txt` no se hubiera visto ninguno de
  los dos (ambos crashean/fallan en un punto sin ningún log de por sí, pero el log de TODO lo que pasó
  ANTES fue justamente lo que permitió confirmar que los 3 parches de SWP del Bug #3 sí funcionaron y
  que el arranque llegó mucho más lejos antes del nuevo crash). El log de esa corrida además confirma
  que se compiló con `-DCMAKE_BUILD_TYPE=Debug` (aparecen líneas `[debug]`), así que el detalle
  completo de `l_info`/`l_debug` ya está disponible siempre que se siga compilando así.
- **Por qué se agregó:** el Bug #3 (y en menor medida los anteriores) se habían diagnosticado solo con
  el `.psp2dmp` — no había ningún log de la corrida real, porque `source/utils/logger.c` solo escribía
  a `sceClibPrintf` (ninguna persistencia). Además `l_info`/`l_debug`/`l_warn`/`l_success`/`l_wait` se
  compilan a NADA salvo que `CMAKE_BUILD_TYPE=Debug` (ver `CMakeLists.txt`) — solo `l_error`/`l_fatal`
  y `port_trace` (usado por FalsoJNI) sobreviven en un build release.
- **Implementado** (`source/utils/logger.c`, `source/utils/logger.h`, `source/main.c`,
  `CMakeLists.txt`):
  - `logger_init()` (llamado al principio de `main()`, antes de `soloader_init_all()`): abre
    `DATA_PATH"logs/game_log_<timestamp_ms>.txt"` (creando el directorio si hace falta) y escribe cada
    línea con `sceIoWrite` crudo (sin buffering de libc que perder en un crash — ya está en disco
    apenas retorna la llamada).
  - Streaming UDP opcional: si se configura `-DUDP_LOG_HOST=<ip de la PC>` al correr `cmake` (además
    `-DUDP_LOG_PORT=<puerto>`, default `9998`), cada línea también se manda por
    `sceNetSendto` best-effort (no bloquea ni falla el arranque si no hay red/listener). Con
    `UDP_LOG_HOST` vacío (default) el feature queda completamente deshabilitado, sin overhead de red.
    Nuevo target de conveniencia: `make listen_logs` (corre `nc -lu <puerto>` en la PC).
  - Ambos sinks (archivo + UDP) reciben la MISMA línea que ya se manda a consola, sin los códigos de
    color ANSI, con un tag de nivel al principio (`[info] ...`, `[error] ...`, etc.) — incluye también
    `port_trace()` (los mensajes de FalsoJNI), que antes no se guardaban en ningún lado.
  - Requiere linkear `SceNet_stub`/`SceNetCtl_stub` (agregado a `target_link_libraries`).
- **A tener en cuenta:** con un build SIN `-DCMAKE_BUILD_TYPE=Debug` los `l_info`/`l_debug`/`l_warn`
  vuelven a compilarse a nada (no se tocó ese gating). Para triage seguir compilando en Debug.
- [x] Recompilar, redesplegar y confirmar en consola real que aparece
  `ux0:data/shadowguardian/logs/game_log_*.txt` con contenido — confirmado con
  `game_log_1788493050809.txt`.
- [ ] Probar el streaming por UDP (`-DUDP_LOG_HOST=<ip de la PC>` + `make listen_logs`) — todavía no
  se confirmó en esta sesión, solo el log a archivo.
- [ ] Si aparece un nuevo crash, triage con `so-crash-triage` cruzando el `.psp2dmp` **con el
  `game_log_*.txt` de esa misma corrida** — recordar siempre: usar `LOAD_ADDRESS=0x98000000` como base
  real del `.so` si el analizador no trae ya el `.elf` del loader para resolver símbolos por su cuenta.

### Bug de rendimiento (auto-infligido, confirmado 2026-09-03): el log incremental volvía sincrónico cada `sceIoWrite`, y eso es lo que se vio como un "cuelgue" de >1 minuto en el splash de LiveArea

- **Síntoma:** sin crash, el juego se quedó en el splash de LiveArea más de un minuto. Log:
  `logs/game_log_1788493940781.txt` (3758 líneas).
- **Diagnóstico:** el log muestra que sí estaba avanzando -- escaneó `shaders/`, `text/`, `music/`,
  `sounds/`, **`models/` (3610 llamadas a `readdir` solo en esa carpeta)**, `anims/`, `collisions/`,
  `textures/`, y terminó abriendo `ux0:data/shadowguardian/states` (el arranque sí seguía progresando,
  no era un deadlock real). El problema: la implementación de `logger_init()`/`_log_print()` de la
  sesión anterior hacía un `sceIoWrite` **síncrono e individual por cada línea de log**, incluso las de
  nivel `debug` -- con miles de llamadas a `readdir` en el loop de escaneo de `models/`, eso se traduce
  en miles de syscalls de escritura a la memory card (mucho más lentas que RAM), suficiente para que un
  escaneo que debería tardar bien poco pareciera un cuelgue de más de un minuto. Un bug auto-infligido
  por el propio agregado del logging incremental de esta sesión.
- **Fix aplicado** (`source/utils/logger.c`, `source/utils/logger.h`, `source/utils/dialog.c`): el sink
  de archivo ahora bufferiza en RAM (4KB, `_log_file_buffer`) y solo hace `sceIoWrite` cuando el buffer
  se llena, en vez de por cada línea. Los niveles `warning`/`error`/`fatal` y `port_trace()` (los
  mensajes de FalsoJNI, siempre WARN/ERROR) siguen forzando un flush inmediato -- son los que más
  importan para diagnosticar un crash real y son poco frecuentes, así que no hay costo de rendimiento
  relevante ahí. Se agregó `logger_flush()` (declarada en `logger.h`), llamada también desde
  `fatal_error()` (`dialog.c`) como red de seguridad antes de mostrar el diálogo de error. Esto acota
  la pérdida de líneas en un cuelgue/crash real a como mucho ~4KB de log `debug`/`info` sin escribir,
  a cambio de evitar miles de syscalls en loops de alta frecuencia como este.
- **Verificado:** compila limpio con el toolchain real (build de prueba completo, `eboot.bin` generado
  sin errores) — no se pudo confirmar en consola física que el escaneo de `models/` ahora sea rápido.
- [ ] Recompilar, redesplegar, y confirmar que el splash de LiveArea ya no tarda más de lo esperado.

### Bug #5 (confirmado 2026-09-04): crash cargando un shader cacheado (`.gxp`) — y regresión propia en el buffer del log

- **Síntoma:** crash sin relación aparente a los bugs anteriores. Dump:
  `logs/shadowguardian-psp2core-1788494769-0x00150e24bd-eboot.bin.psp2dmp`. `PC` cae en `SceLibKernel`
  (módulo real de Sony) con **`LR = 0x0`**. La pila trae una cadena ASCII legible:
  `ux0:data/shadowguardian/gxp/AC1D1CF63D70C300367B0C98906BAE6DEAD1....gxp` — el MISMO hash de shader
  que en el Bug #2 no se había podido guardar por el bug de `file_save`/directorio faltante. El
  backtrace resuelto (símbolos reales del `.elf` del loader) muestra `load_shader` (`glutil.c:138`,
  nuestro código) → `unserialize_shader` (interno de **vitaGL**, `custom_shaders.c:602` — vitaGL en
  este toolchain está compilado con símbolos de depuración de OTRO port, "Sacred-Odyssey-vita", que
  comparte la misma instalación de VitaSDK; los símbolos en sí son reales y confiables).
- **Efecto colateral descubierto:** este log (`game_log_1788494762836.txt`) **no tiene ninguna línea de
  debug de la carga de shaders**, a pesar de que el crash ocurre ahí — confirma en la práctica el
  riesgo que ya se había anotado al bufferizar el log (fix del bug de rendimiento, arriba): un crash de
  hardware real nunca pasa por `fatal_error()`, así que nunca llama a `logger_flush()`, y lo que
  hubiera en el buffer de 4KB en ese momento se pierde. Justo la información más valiosa (el contexto
  inmediato antes del crash) fue lo que se perdió.
  - **Fix aplicado** (`source/utils/logger.c`): además del flush por tamaño (4KB), ahora también se
    fuerza un flush cada `LOG_FILE_FLUSH_EVERY = 8` líneas sin importar el nivel. Acota la pérdida
    máxima en un crash real a ~8 líneas en vez de hasta ~130 (con el buffer lleno), a costa de un poco
    más de syscalls (segue siendo un ahorro de ~8x contra el bug de rendimiento original, no de ~500x).
- **Causa raíz — dos bugs reales encontrados por revisión de código** (no se pudo confirmar cuál es
  el que efectivamente disparó ESTE crash puntual, porque el log no tenía el contexto; ambos son
  defectos genuinos independientemente):
  1. `load_shader()` (`glutil.c`) llamaba `file_load(gxp_path, &buffer, &size)` **sin chequear el
     valor de retorno** — si fallaba, `buffer`/`size` quedaban sin inicializar y se pasaban igual a
     `glShaderBinary(1, &shader, 0, buffer, size)`, con un puntero/tamaño potencialmente basura.
  2. `glCompileShader_soloader()` pedía el binario compilado con `vglGetShaderBinary(shader, 32*1024,
     &len, bin)` — si el binario real superaba los 32KB, `len` queda truncado al tope del buffer sin
     ninguna señal de error, y ese `.gxp` truncado/corrupto se guardaba en el cache igual, para
     crashear la próxima vez que se cargara.
  - **Fix aplicado** (`source/utils/glutil.c`):
    - `load_shader()`: ahora solo usa el `.gxp` cacheado si `file_exists` Y `file_load` (que ahora SÍ
      se chequea) tienen éxito Y el tamaño es `> 0`; si no, cae al camino normal de compilar el GLSL
      de nuevo (trata un cache corrupto/faltante como cache-miss en vez de crashear).
    - `glCompileShader_soloader()`: buffer de `vglGetShaderBinary` subido a 128KB, y si `len` llega a
      igualar el tamaño del buffer (señal de truncamiento), NO se guarda el `.gxp` (se loguea error
      en vez de cachear un binario que sabemos que probablemente está corrupto).
- **Acción pendiente en la consola (no puedo hacerla desde acá):** borrar
  `ux0:data/shadowguardian/gxp/` en la Vita antes de la próxima prueba, para eliminar cualquier `.gxp`
  ya corrupto/truncado que haya quedado de sesiones anteriores (antes de que existiera este chequeo).
  Con el fix de logging ya no se debería perder el contexto si este crash puntual vuelve a pasar.
- **Verificado:** compila limpio con el toolchain real (build de prueba completo, `eboot.bin` generado
  sin errores) — no se pudo probar en consola física desde acá.
- [x] Borrar `ux0:data/shadowguardian/gxp/` en la consola, recompilar con los fixes, redesplegar y
  volver a probar. El crash de cargar un `.gxp` corrupto NO volvió a pasar — confirmado con
  `game_log_1788497378082.txt` (ver Bug #6 abajo, distinto crash, mucho más adelante en el arranque).
  El log SÍ mostró más contexto esta vez gracias al fix de flush, incluyendo un hallazgo nuevo:
  **el guard de truncamiento se dispara para los 6 shaders, pero con `len=0`, no por pasarse del
  buffer** (`[error] Shader 1 binary looks truncated (len=0, bufsize=131072), not caching ...`) —
  o sea, `vglGetShaderBinary()` no está devolviendo NINGÚN byte para shaders recién compilados (quizás
  vitaGL solo expone el binario después de linkear el shader en un programa, no inmediatamente tras
  compilarlo). El guard está funcionando como red de seguridad (ya no crashea, ya no cachea basura),
  pero el cache de shaders compilados sigue sin funcionar en absoluto — se recompila desde GLSL en
  cada corrida, sin beneficio de velocidad, pero sin crashear. No es urgente (no es un crash), pero
  vale la pena investigar la semántica real de `vglGetShaderBinary` en algún momento si el tiempo de
  carga de shaders llega a ser un problema.

### Bug #6 (confirmado 2026-09-04): mismo patrón de `SWP`, ahora en `spinlock_pool<1>::scoped_lock`

- **Síntoma:** crash "Undefined Instruction", mucho más adelante en el arranque (cargando fuentes).
  Dump: `logs/shadowguardian-psp2core-1788497388-0x0012612dd3-eboot.bin.psp2dmp`. De nuevo el
  analizador auto-detectó una base incorrecta (`0x83f24000`); recalculando con la base real
  (`0x98000000`), `PC` cae en `boost::detail::spinlock_pool<1>::scoped_lock::scoped_lock(void
  const*)+0x44`, llamado desde `LR` en `FontMgr::LoadFonts(Lib*, StringMgr::LanguageEnum)+0x174`.
- **Causa raíz:** el mismo patrón exacto de `SWP` (Bug #3/#4), esta vez a través del wrapper RAII
  `scoped_lock` de boost (en vez de acceder al pool directamente como hacían `shared_count` y
  `sp_counted_base::release`) — mismas 3 instancias inlineadas (intento inicial + 2 cuerpos de retry
  con `sched_yield`/`nanosleep`), confirmadas con `arm-vita-eabi-objdump` en modo ARM real, en los
  offsets `+0x44`, `+0x5c`, `+0x88` de `_ZN5boost6detail13spinlock_poolILi1EE11scoped_lockC1EPKv`.
  Como ya se esperaba: cada lugar del motor que toca un `boost::shared_ptr`/`weak_ptr` (acá,
  `FontMgr` cargando fuentes) puede traer su propia copia inlineada de este patrón roto.
  - **Fix aplicado** (`source/patch.c`): mismo `patch_swp_with_ldrex_strex()`, tres llamadas nuevas
    sobre los 3 offsets confirmados. El destructor de `scoped_lock` no necesitó parche — libera el
    lock con un `str` simple, no con `SWP` (no hace falta atomicidad para soltar el lock).
  - **Contexto adicional del log** (gracias al fix de flush del Bug #5): justo antes del crash,
    `FontMgr::LoadFonts` abre `ux0:data/shadowguardian/fonts` y dispara un assert interno del motor
    (`pig::stream::BufferStream::Seek`, `Ghidra out_ghidra.c:411938`, mensaje `Exp: false, File:
    .../BufferStream.cpp, Line: 224`) — un `Seek()` más allá del tamaño real del buffer en un stream
    que no puede crecer. Anotado para investigar aparte si después de este fix el juego arranca pero
    con fuentes/texto rotos: podría indicar un archivo `fonts` con tamaño/formato distinto al
    esperado. También se vieron 57 warnings de "encountered garbage value (missing(EN,NNN))" al
    parsear `text/texts_en.lang` con IDs no correlativos (479, 481, 78, 79, 80, 56...) — probablemente
    huecos normales/pre-existentes en la tabla de strings original del juego, no algo introducido por
    el port (es un WARNING que el motor tolera con gracia, no un error).
- **Verificado:** compila limpio con el toolchain real (build de prueba completo, `eboot.bin`
  generado sin errores) — no se pudo probar en consola física desde acá.
- [x] Recompilar con el fix, redesplegar y volver a probar. Se llegó más lejos (crash pasó de la
  construcción del lock a su destrucción) pero apareció una 4ta función con el mismo patrón — ver
  Bug #7 abajo.

### Bug #7 (confirmado 2026-09-04): mismo patrón de `SWP`, ahora en el DESTRUCTOR de `shared_count`

- **Síntoma:** mismo tipo de crash "Undefined Instruction", mismo lugar general (`FontMgr::LoadFonts`,
  cargando `ux0:data/shadowguardian/fonts` — el mismo assert de `BufferStream::Seek` del Bug #6 se
  repite en el log, sin cambios). Dump:
  `logs/shadowguardian-psp2core-1788497809-0x000a533bf1-eboot.bin.psp2dmp`. Recalculando `PC`/`LR`
  con la base real: `PC` cae en **`boost::detail::shared_count::~shared_count()+0x50`** (el
  destructor esta vez, no el copy-ctor del Bug #3), llamado desde `FontMgr::LoadFonts+0x1c8` — más
  adelante que el `+0x174` del Bug #6, confirmando que ESE fix sirvió y el arranque avanzó un poco
  más antes de pegar con la siguiente instancia del mismo patrón.
- **Causa raíz:** mismas 3 instancias de `SWP` inlineadas (confirmado con `arm-vita-eabi-objdump`
  ARM real), en offsets `+0x50`, `+0x6c`, `+0x98` de `_ZN5boost6detail12shared_countD1Ev`. Esta
  función también llama internamente a `spinlock_pool<1>::scoped_lock` (ya parcheada en el Bug #6),
  así que esa parte no necesitó nada nuevo.
  - **Fix aplicado** (`source/patch.c`): mismo `patch_swp_with_ldrex_strex()` en los 3 offsets.
- **Van 4 funciones distintas con el mismo patrón** (`shared_count` copy-ctor, `sp_counted_base::
  release`, `spinlock_pool<1>::scoped_lock` ctor, y ahora `shared_count` destructor) — todas usan el
  MISMO array global `spinlock_pool<1>::pool_`, así que es literalmente cualquier construcción/copia/
  destrucción de CUALQUIER `boost::shared_ptr<T>` en todo el motor. Dado que ya son 4/4 confirmaciones
  reales sin ningún falso positivo, y que el fix en sí no depende de qué instrucción sigue al `SWP`
  (ver nota en el Bug #3 sobre por qué es semánticamente seguro), **vale la pena considerar un escaneo
  automático de todo el `.so` en `so_patch()`** (buscando el patrón de bits exacto de `SWP`/`SWPB`, con
  un chequeo extra de que la instrucción siguiente sea `cmp Rt,#0` como filtro anti-falsos-positivos)
  en vez de seguir parcheando función por función a medida que aparecen nuevos crashes — pendiente de
  decisión del usuario, no se implementó todavía porque cambia el perfil de riesgo (de "parchear solo
  lo que un crash real confirmó" a "parchear todo lo que coincida con un patrón, sin poder probar cada
  caso en hardware").
- **Verificado:** compila limpio con el toolchain real (build de prueba completo, `eboot.bin`
  generado sin errores) — no se pudo probar en consola física desde acá.
- [x] Recompilar con el fix, redesplegar y volver a probar — **decisión: pasar al escaneo automático**
  (ver más abajo) en vez de seguir parcheando uno por uno.

## Decisión (2026-09-04): escaneo automático de todo el `.so` para el bug de `SWP`, en vez de parchear función por función

- Consultado explícitamente: dado que ya eran 4/4 funciones distintas con el mismo patrón exacto (y
  el fix no depende de qué instrucción sigue al `SWP`, ver Bug #3), se optó por implementar el escaneo
  automático de todo el `.text` cargado en `so_patch()`, en vez de seguir esperando un crash nuevo por
  cada instanciación de `boost::shared_ptr<T>`/`weak_ptr<T>` todavía no ejercitada.
- **Implementado** (`source/patch.c`, `patch_all_swp_spinlocks()`): recorre `so_mod.text_base` ..
  `so_mod.text_base + so_mod.text_size` en pasos de 4 bytes (lectura directa por puntero, sin
  syscalls — la región ya es legible, solo no escribible), buscando el patrón de bits exacto de
  `SWP`/`SWPB` (`(instr & 0x0FB00FF0) == 0x01000090`) **y**, como filtro anti-falso-positivo contra
  datos embebidos en `.text` (jump tables/literal pools, que este binario sí tiene en otros lugares,
  ver investigación del Bug #1), que la palabra siguiente sea `cmp Rt,#0` (con `Rt` exactamente el
  registro destino de la `SWP` encontrada) antes de parchear. Reemplazó los 4 bloques de símbolos
  puntuales de los Bugs #3/#4/#6/#7 — `so_patch()` ahora llama una sola vez a
  `patch_all_swp_spinlocks()`.
- **Verificado offline contra el `.so` real** (script Python replicando la misma lógica de bits, antes
  de tocar la consola): sobre el segmento ejecutable completo (`0x398034` bytes, confirmado con
  `readelf -l`) hay **830 palabras** que matchean el patrón crudo de `SWP`, de las cuales **785**
  además pasan el filtro de `cmp Rt,#0` (esas son las que el scanner real va a parchear; el resto,
  ~45, se descartan como probable dato embebido). Los 11 offsets ya confirmados por crashes reales
  (Bugs #3/#4/#6/#7) están **los 11** dentro de esas 785 — cero falsos negativos contra lo ya
  confirmado en hardware. 785 instancias es mucho más de lo que se hubiera cubierto parcheando
  función por función a este ritmo (4 funciones en 4 crashes), lo que confirma que la estrategia
  anterior iba a necesitar muchas más rondas de crash+build+deploy para cubrir todo el motor.
- **Riesgo aceptado:** un escaneo ciego no permite confirmar cada caso individual en hardware real
  (a diferencia del enfoque anterior). El filtro `cmp Rt,#0` reduce bastante el riesgo de falso
  positivo, y el fix en sí es local/autocontenido por diseño (no depende de qué hay después del
  `SWP` para ser correcto — ver nota en el código), pero **si aparece un crash NUEVO y distinto**
  después de este cambio, hay que reconsiderar si el escaneo mismo introdujo un problema (parcheo de
  un falso positivo) antes de asumir que es un bug no relacionado.
- **Verificado:** compila limpio con el toolchain real (build de prueba completo, `eboot.bin`
  generado sin errores) — no se pudo probar en consola física desde acá.
- [x] Recompilar con el escaneo automático, redesplegar y probar.
  - **CONFIRMADO ÉXITO:** En `game_log_1788498371002.txt` (línea 838), el escáner parcheó con éxito
    las **785 instancias de SWP** en el arranque. El juego pasó sin problemas toda la carga de shaders,
    idiomas, modelos, sonidos, texturas y fuentes donde antes crasheaba repetidamente.

### Bug #8 (confirmado 2026-09-04): Data Abort en `ALicenseCheck` (stack overflow + llamadas JNI de DRM sin stub)

- **Síntoma:** Crash en `nativeShadowGuardianInit`, inmediatamente después de inicializar audio y cargar
  assets. Dump: `logs/shadowguardian-psp2core-1788498383-0x000a7b3539-eboot.bin.psp2dmp`.
  - El analizador automático (`vita-parse-core`) reportó incorrectamente una base `0x97fe4000`
    (confundido por los trampolines de SWP en el arena), sugiriendo falsamente un crash en
    `_Locale_get_time_hint` / `_Locale_mon_grouping`.
  - Con la base real fija `LOAD_ADDRESS = 0x98000000`:
    - `PC = 0x9833bf28` → offset `0x33bf28` → primera instrucción (`push {r4-r8, lr}`) de
      `ALicenseCheck::CallJNIFuncChar`.
    - `LR = 0x9833c014` → offset `0x33c014` → dentro de `ALicenseCheck::LoadConfig()`.
- **Causa raíz:**
  1. `Java_..._ShadowGuardian_nativeInit` (offset `0xaee98`) invoca `ALicenseCheck_InitLicense` (offset
     `0x33c248`), que a su vez llama a `ALicenseCheck::Init` (`0x33c08c`).
  2. En `source/patch.c` se había intentado hookear `_ZN13ALicenseCheck28ALicenseCheck_ValidateLicenseEb`,
     pero ese símbolo no existía (era un nombre erróneo), por lo que el hook falló silenciosamente y
     toda la infraestructura de DRM de Gameloft se ejecutó sin interceptar.
  3. `ALicenseCheck::Init` intenta obtener los MethodIDs de `"d"`, `"db"`, `"dc"`, `"da"` (DRM ofuscado en
     el APK) resultando en errores de FalsoJNI en el log.
  4. Seguidamente llama a `ALicenseCheck::LoadConfig()`, función que reserva localmente en la pila
     `char acStack_4001c[262152]` (¡más de 256 KB en una sola función!). Como el hilo principal de la Vita
     tenía el tamaño de pila por defecto (256 KB), la resta del `SP` rebasó el límite de la pila y la
     primera instrucción `push` en `CallJNIFuncChar` causó un **Data Abort Exception (0x30004)** por stack overflow.
- **Fix aplicado:**
  - En `source/patch.c`: se implementaron stubs e interceptores para todos los símbolos reales del DRM:
    - `ALicenseCheck_InitLicense` y `_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass`
    - `ALicenseCheck_ValidateLicense` y `_ZN13ALicenseCheck14ValidateServerEb`
    - `_ZN13ALicenseCheck10LoadConfigEv`
    - `_ZN13ALicenseCheck7LoadRMSEv` y `_ZN13ALicenseCheck7SaveRMSEb`
  - En `source/main.c`: se definió `unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;` (2 MB) para
    garantizar suficiente margen de pila para el motor C++.
- **Verificado:** Compilación limpia de `eboot.bin` con VitaSDK sin errores.
- [x] Recompilar con el fix de ALicenseCheck, redesplegar y probar.
  - **CONFIRMADO ÉXITO:** En `game_log_1788499731193.txt`, el log muestra:
    `[info] ALicenseCheck_InitLicense bypassed`
    `[info] Calling GameRenderer.nativeResize(960x544)...`
    `[info] Entering main render loop...`
    `[info] ALicenseCheck_ValidateLicense bypassed`
    ¡El motor superó toda la inicialización y entró con éxito al bucle de renderizado principal!

### Bug #9 (confirmado 2026-09-04): Crash en `GS_Loading::LoadSpritesMenu` por asset faltante `sprites_1_7` (NULL dereference)

- **Síntoma:** Crash durante la carga del menú en el primer ciclo de render (`Entering main render loop...`).
  Dump: `logs/shadowguardian-psp2core-1788499744-0x0009b634c7-eboot.bin.psp2dmp`.
  - `PC = 0x981b40fc` → offset `0x1b40fc` → dentro de `GS_Loading::LoadSpritesMenu()` (`0x1b4088 + 0x74`).
  - `LR = 0x981b40e8` → offset `0x1b40e8`.
  - Registro `R7 = 0x00000000`.
  - Instrucción causante: `vstr s15, [r7, #292]` intentando escribir en `[0x0 + 292]` (Data Abort / Null dereference).
- **Causa raíz:**
  1. Al calcular la relación de aspecto de pantalla (`fVar19 = 960.0 / 544.0 = 1.7647 > 1.7`), el motor detecta
     una pantalla 16:9 (`m_isAspect1706 = 0`, flag `0xbd = 0`) y selecciona las variantes de assets para ratio 1.7:
     `sprites_1_7` y `gui_1_7`.
  2. En los datos del juego (`ux0_data/shadowguardian/`), el APK original (destinado a pantallas WVGA 800x480 de ratio ~1.66)
     solo incluía `sprites_1_6` (66 MB) y `gui_1_6` (24 KB).
  3. `Lib::Open("sprites_1_7")` falló (`stat` retornó `-1`), dejando `m_chunkCount = 0` y emitiendo el log:
     `Exp: 0 <= index && index < m_chunkCount, File: .../Lib.cpp, Line: 162`.
  4. Como la carga del sprite 27 falló, el SpriteMgr no contenía el sprite y fijó el puntero `r7 = 0` (`movle r7, #0`).
     Inmediatamente después, el código original desreferencia `r7` sin comprobar NULL: `vstr s15, [r7, #292]`.
- **Fix aplicado:**
  - En `source/reimpl/io.c`: se implementó `try_fallback_1_7` en `translate_path`. Si el juego solicita un archivo
    con `_1_7` (como `sprites_1_7` o `gui_1_7`) y no existe físicamente en `ux0:`, se redirige automáticamente al
    asset existente `_1_6` de forma transparente.
  - En `ux0_data/shadowguardian/`: se generaron también copias físicas `gui_1_7` y `sprites_1_7` a partir de los
    archivos `_1_6` para compatibilidad directa.
- **Verificado:** Compilación limpia de `eboot.bin` con VitaSDK.

### Bug #10 (confirmado 2026-09-04): Error de enlazado de shaders en `interactible_bump.xml` (Uniform no declarado `OutlineSize`)

- **Síntoma:** Error de linkeo en vitaGL durante la carga de shaders de elementos interactivos del menú.
- **Causa raíz:** En `ux0_data/shadowguardian/shaders/interactible_bump.xml`, el fragment shader utilizaba `OutlineSize` pero no estaba declarado como `uniform highp float OutlineSize;`.
- **Fix aplicado:**
  - Se declaró `uniform highp float OutlineSize;` y se ajustaron los tipos en `interactible_bump.xml`, `interactible_basic.xml` e `interactible_shadowmap.xml`.
  - En `source/utils/glutil.c`, se implementó `glLinkProgram_soloader` con lectura de `glGetProgramInfoLog` para emitir al log cualquier fallo de compilación o linkeo de shaders en tiempo real.

### Bug #11 (confirmado 2026-09-04): Undefined Instruction en `clara::PSTemplate::LoadEmitter` (`0x98277824`) por instrucción SWP no parcheada

- **Síntoma:** Crash por instrucción no definida (`0x30002`) en `PC = 0x98277824` al cargar emisores de partículas del menú. Dump: `logs/shadowguardian-psp2core-1788500926-0x000a1b3249-eboot.bin.psp2dmp`.
- **Causa raíz:**
  - La instrucción en `0x98277824` era `swp r3, fp, [r9]` seguida de `cmp r3, sl` (`cmp r3, r10`).
  - El escáner automático previo solo buscaba `cmp rt, #0` y saltaba las instrucciones SWP que comparaban contra registros (`cmp rt, reg`), además de omitir aquellas donde `r12` era uno de los operandos.
- **Fix aplicado:**
  - En `source/patch.c`, se amplió `patch_all_swp_spinlocks` para reconocer ambos patrones: `cmp rt, #0` y `cmp rt, reg`.
  - Para instrucciones SWP con operandos en `r12`, se implementó un trampolín extendido que preserva un registro de scratch (`r0`/`r1`) en la pila con alineación de 8 bytes (`str scratch, [sp, #-8]!` / `ldr scratch, [sp], #8`).
  - Total de instrucciones SWP parcheadas con éxito en todo el binario: **826 de 826**.

### Bug #12 (confirmado 2026-09-04): Data Abort en `Game::FrameRender()` (`0x980bf8d8`) por desreferencia nula de driver gráfico

- **Síntoma:** El juego reprodujo música de fondo (`music//m_title.wav`), cargó los shaders de postprocesado (`bloom_menu`, `blur_menu`), pero crasheó con pantalla negra justo al ingresar al bucle de renderizado. Dump: `logs/shadowguardian-psp2core-1788501517-0x000a273c27-eboot.bin.psp2dmp`.
- **Causa raíz:**
  - Desensamblado en `0x000bf8b0` (`Game::FrameRender()`):
    ```arm
    bf8c4: ldr   r3, [r4, r5]     ; r3 = &s_impl
    bf8c8: ldr   r3, [r3]         ; r3 = s_impl
    bf8cc: cmp   r3, #0
    bf8d0: moveq r2, r3           ; r2 = 0 si s_impl == 0
    bf8d4: ldrne r2, [r3, #4]     ; r2 = s_impl->m_pDriver
    bf8d8: ldr   r3, [r2, #16]    ; ¡CRASH! Desreferencia incondicional [r2 + 16]
    ```
  - Al llamarse `FrameRender()` en el primer frame tras terminar la carga de `menu.bclara`, el puntero `r2` era `0x0`. La instrucción `ldr r3, [r2, #16]` intentó leer de la dirección `0x10`, provocando un Data Abort Exception.
- **Fix aplicado:**
  - En `source/patch.c`: se implementó el trampolín `patch_game_framerender` en la arena de código (`0x000bf8c4`). Comprueba si `s_impl == NULL` o `driver == NULL`; si alguno no está listo, limpia la pila (`add sp, sp, #16; pop {r4-r8, pc}`) y retorna limpiamente sin crashear. Cuando ambos punteros son válidos, reanuda el renderizado normalmente.
  - En `source/main.c`: se añadieron logs de diagnóstico `[DIAG]` para verificar el puntero `s_impl` y `driver` en la entrada al bucle de render.
  - `eboot.bin` recompilado y desplegado exitosamente vía FTP a la consola.



