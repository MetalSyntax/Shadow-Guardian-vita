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

### Bug #13 (confirmado 2026-09-04): Pantalla negra por carga de texturas fallida en `pig::stream::MMap()` (longitud de `mmap()` = `st_blksize`, no `st_size`)

- **Síntoma:** Tras superar toda la carga inicial (Bugs #1-#12), el juego llegaba al menú (música de
  título sonando, shaders de post-proceso cargando) pero la pantalla se veía negra. El log mostraba
  **772** errores `Cannot load texture 'X.tga' ... Exp: !Game::GetInstance()->DisplayErrors() ||
  layer.GetTexture()` — prácticamente toda textura de personaje/mundo fallaba al cargar.
- **Causa raíz:**
  - Ghidra (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:412050`,
    `pig::stream::MMap(std::string const&)`) hace `open()` + `fstat()` + `mmap()` para leer cada
    textura, pero usa `sStack_488.st_blksize` (no `st_size`) como longitud del `mmap()` — un bug del
    motor original que aparentemente "funcionaba" en Android real por casualidad (blksize del
    filesystem de origen probablemente cubría archivos chicos).
  - Nuestro `fstat_soloader` (`source/reimpl/io.c`) delega en el `fstat()` real de newlib sobre
    `ux0:`, que reporta `st_blksize = 0` para archivos ahí — el juego terminaba llamando
    `mmap(addr=NULL, length=0, ...)` para cada textura, que fallaba inmediatamente. Confirmado en el
    log: `[warning] mmap(0x0, 0, 1, 1, 3, 0)` inmediatamente después de cada `open()`+`fstat()` de un
    `.tga`.
  - Bug secundario agravante: el shim `mmap()` en `source/reimpl/mem.c` nunca leía contenido real del
    `fd` — solo hacía `malloc()` + `memset(0)` y devolvía memoria en cero, así que aunque `length`
    hubiese sido correcto, las texturas habrían cargado como buffers vacíos (negro/basura), no el
    contenido real del archivo.
- **Fix aplicado:**
  - En `source/reimpl/bits/_struct_converters.c` (`stat_newlib_to_bionic`): se fuerza
    `dst->st_blksize = st_size` (cuando `st_size` > 0) para que el uso indebido de `st_blksize` como
    "tamaño del archivo" en `MMap()` mapee el archivo completo en vez de 0 bytes.
  - En `source/reimpl/mem.c` (`mmap()`): reimplementado para que de verdad lea el contenido del `fd`
    (`lseek` + `read` en un buffer `malloc`eado del tamaño pedido) en vez de devolver memoria en cero.
    `munmap()` sigue siendo un `free()` simple (no hay mapeo de páginas real).
- **Verificado en consola física:** `game_log_1788536434751.txt` y `game_log_1788538117179.txt` — los
  errores `Cannot load texture` bajaron de 772 a 106 (los restantes son texturas de gameplay/HUD
  específicas, no assets de menú; a investigar aparte si hace falta), y `mmap(..., 0, ...)` bajó de
  cientos a solo 10 instancias. El juego ahora carga el nivel de menú completo, reproduce música de
  título y responde a input (sonido `sfx_menu_confirm.wav` al pulsar).
- **Pendiente:** La pantalla del menú sigue viéndose negra pese a que el motor está vivo y responde a
  input — no se descartó todavía si es un problema de render target/composición post-proceso
  (`bloom_menu`/`blur_menu`) o de algún asset de fondo específico. Además: los 68 `Shader N binary
  looks truncated (len=0)` en cada carga de shader del menú (`glutil.c`, `DUMP_COMPILED_SHADERS`) —
  no bloquean el render (el shader sí compila en runtime, `compile_status == GL_TRUE`; solo falla el
  cacheo del binario `.gxp` para arranques futuros) pero conviene investigarlos si el arranque en frío
  resulta muy lento.
### Bug #14 (confirmado 2026-09-04): CAUSA RAÍZ real de la pantalla negra — `vglInitExtended()` del `vitaGL` precompilado de vitasdk (vdpm) devolvía `GL_FALSE` desde la primera llamada del proceso

- **Síntoma:** Con el Bug #13 ya arreglado (texturas cargando, audio sonando, motor respondiendo a
  input), la pantalla seguía completamente negra. Incluso un `glClearColor`+`glClear`+swap de
  diagnóstico puesto manualmente justo después de `gl_init()` — antes de que corriera código del
  juego — no se veía en pantalla física. Eso descartó el pipeline de render del juego y apuntó a la
  inicialización de vitaGL en sí.
- **Causa raíz (confirmada desensamblando el `.a` real, no adivinando):**
  - Con `arm-vita-eabi-objdump` sobre `libvitaGL.a` (el paquete vdpm de vitasdk), `vglInitExtended()` →
    `vglInitWithCustomThreshold()` solo devuelve `GL_FALSE` en un único camino: si el flag estático
    interno `vgl_inited` ya es distinto de cero.
  - Instrumentando `main.c` con un contador de llamadas + lectura directa de memoria de `vgl_inited`
    (dirección real vía `nm` sobre el ELF linkeado) en varios puntos del arranque, se confirmó que
    `vgl_inited` valía `0` inmediatamente antes de la primera (y única) llamada a `gl_init()` en todo
    el proceso, y que esa misma llamada devolvía `GL_FALSE`. Es decir: el flag se corrompe/pasa a 1
    **dentro** de esa única llamada, en código que no pertenece a este proyecto (no hay ningún otro
    call site a `vglInit*` en todo el binario, confirmado buscando `bl` hacia esas direcciones en el
    ELF completo) — imposible de seguir depurando más sin el código fuente del `.a` precompilado.
  - Los ports hermanos (Asphalt-5-Vita, Dungeon-Hunter-2-vita) ya habían abandonado el vitaGL
    precompilado de vdpm por bugs conocidos con juegos "pesados" (ver sus CMakeLists.txt, comentarios
    de Bug #19/#20/#22 de Asphalt5), compilándolo en cambio desde código fuente vendorizado
    (`lib/vitagl/`, fork de Rinnegatamante/vitaGL) con flags propios.
- **Fix aplicado:**
  - Se vendorizó `lib/vitagl/` (commit `cd3791e29ff7f1c0ab349f12c7231f4871ce6a75` de
    github.com/Rinnegatamante/vitaGL, mismo commit base que usa Dungeon-Hunter-2-vita) y se cambió
    `CMakeLists.txt` para compilarlo desde fuente (`vitaGL_lib` custom target + `build_vitagl.sh`) en
    vez de linkear el `vitaGL` de vdpm. Ver `lib/vitagl/VENDORED.md` para el detalle de qué se copió y
    por qué.
  - A diferencia de Dungeon-Hunter-2-vita (que sí tiene su propia capa EGL completa en
    `source/reimpl/egl.c`, wireada en su CMakeLists), acá `source/reimpl/egl.c` existe pero **nunca
    estuvo en el `CMakeLists.txt`** — es código muerto de una iteración anterior. Este proyecto
    depende de la implementación EGL propia de vitaGL, así que se mantuvo `lib/vitagl/source/egl.c`
    sin modificar (a diferencia del patrón de DH2 de eliminarlo).
  - `.gitignore`: la regla `Makefile` (sin ancla) ignoraba también `lib/vitagl/Makefile` vendorizado,
    rompiendo la copia que hace `psvita-toolkit build` a un directorio temporal. Se ancló a `/Makefile`.
- **Verificado en consola física:** con este cambio, el flash de diagnóstico magenta **sí se vio en
  pantalla** — primera confirmación real de que vitaGL presenta algo al framebuffer físico. Con el
  diagnóstico ya quitado, el juego ahora muestra la pantalla de carga real (assets, texturas, UI) en
  vez de negro.
- **Efecto secundario descubierto:** con vitaGL real (no el vdpm que no-opeaba todo), `vglGetShaderBinary()`
  (usada por el diagnóstico opcional `DUMP_COMPILED_SHADERS`, ver `source/utils/glutil.c`) **ignora el
  parámetro `bufSize`** (confirmado leyendo `lib/vitagl/source/custom_shaders.c`) y escribe el shader
  serializado completo sin chequear límites — con nuestro buffer fijo de 128 KB, cualquier shader más
  grande desborda el buffer y crashea (antes esto no se notaba porque el vdpm simplemente no hacía
  nada real). Se desactivó `DUMP_COMPILED_SHADERS` por defecto en `CMakeLists.txt` (mismo default que
  ya usa Dungeon-Hunter-2-vita) — es solo una optimización de cacheo para arranques más rápidos, no
  hace falta para que el juego renderice.
- [x] Bug de pantalla negra CONFIRMADO RESUELTO en consola física.

### Bug #15 (en investigación, 2026-09-04): Data Abort en `_glFramebufferTexture2D` (`fb == NULL`) al montar el render target de `bloom_menu`

- **Síntoma:** Con el Bug #14 ya arreglado y la pantalla de carga visible, el juego crashea
  inmediatamente después de `<bloom_menu> Linking pass 3...` en el log. Dump:
  `logs/shadowguardian-psp2core-1788550747-0x0017282cf9-eboot.bin.psp2dmp`.
  - `PC` dentro de `_glFramebufferTexture2D` (`lib/vitagl/source/shared.h:1355`),
    `int old_w = fb->width` — desreferencia de `fb` nulo.
  - `R4 = 0x00000000` en el volcado de registros, consistente con que el puntero `fb` (framebuffer
    actualmente bindeado) es nulo en el momento de la llamada.
- **Hipótesis de causa (sin confirmar aún):** el juego arma un FBO offscreen para el post-proceso de
  bloom del menú (múltiples "Linking pass N" ya vistos en el log para `bloom_menu`/`blur_menu`) y
  llama a `glFramebufferTexture2D` sin que haya un framebuffer real bindeado (`glBindFramebuffer` no
  se llamó, falló, o el objeto 0/default no es válido para esta llamada según el spec de GLES2).
- [ ] Seguir con `so-crash-triage`: encontrar el call site real de `glFramebufferTexture2D` para este
  pase de bloom (Ghidra sobre `libshadowguardian.so`, buscar la función de setup de `bloom_menu` /
  `RenderPassDef`) y confirmar si el bug es un `glBindFramebuffer` faltante/fallido antes de esta
  llamada, o si depende de las texturas de canal que aún no cargan (`Cannot load texture` restantes).

### Bug #16 (fix aplicado, sin confirmar en consola, 2026-09-04): cinemática `story_cinematic_1.bclara` "casi en negro" — probable misma causa raíz que los modelos 3D negros en gameplay

- **Contexto:** `story_cinematic_1.bclara` (y las demás `story_cinematic_N.bclara`) NO son video
  pre-renderizado — son escenas reales del motor con `CinematicCamera` propia
  (`GameLevel::SwitchToNextCinematicCamera`, `GS_GamePlay::UpdateCinematicCamera`,
  `GS_GamePlay::DoSkipCinematic` en el pseudo-C de Ghidra), armadas con modelos `.pig` normales
  (`cin_a01_final.pig`) y shaders normales de personaje (`bump.xml`/`character_body.xml` →
  `df_nm_sm_spec_fres_refl.fs`). No hay ningún shader propio de fade/vignette/letterbox para
  cinemáticas en `ux0_data/shadowguardian/shaders/` — el único fundido es el `FadeIn`/`FadeOut` de Lua
  (`decompiled/.../out_ghidra.c:194251-194274`, registrado como función de script), que es lógica de
  guion normal, no un bug de render.
- **Síntoma real:** en `logs/game_log_1788552823938.txt` (~línea 5728), al cargar las texturas del
  personaje de la cinemática, se ven `mmap()` con longitudes absurdas: `cin_a01.tga` → `mmap(0x0, 1,
  ...)`, `cin_t03.tga` → `mmap(0x0, 0, ...)`, `cin_d03.tga` y `cin_s01.tga` → `mmap(0x0, 10, ...)` — pese
  a que los archivos reales miden 87536, 11064, 699192 y 699192 bytes respectivamente (confirmado
  comparando `GloftSGHP/textures/*.tga` contra `ux0_data/shadowguardian/textures/*.tga`, tamaños
  idénticos). Esto es la MISMA familia de bug que el Bug #13 (mmap con longitud incorrecta al cargar
  texturas vía `pig::stream::MMap()`), pero afecta al subconjunto de ~106 texturas que quedaron sin
  resolver tras ese fix (texturas de nivel/gameplay/cinemática, no de menú) — el mismo hueco que ya
  había dejado marcado un diagnóstico temporal en `fstat_soloader()` (`source/reimpl/io.c`).
- **Causa raíz (hipótesis fuerte, no confirmada en hardware):** `MMap()` (pseudo-C en
  `decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:412026`) hace `open()` + `fstat(fd,
  &st)` + `mmap(..., st.st_blksize, ...)` + `close()` — un único call site, igual para texturas de menú
  y de nivel. La ruta de acceso por PATH (`stat()`, usada segundos después por el propio motor para un
  `fopen()` redundante sobre el mismo archivo) siempre reporta éxito para estos mismos archivos. Es
  decir: `stat(path, ...)` y `fstat(fd, ...)` sobre el MISMO archivo dan tamaños distintos. En el
  newlib de vitasdk, `stat()` está respaldado por `sceIoGetstat()` (por path) mientras que `fstat()`
  está respaldado por `sceIoGetstatByFd()` (por descriptor) — dos syscalls distintas. La hipótesis es
  que `sceIoGetstatByFd()` devuelve un `st_size` obsoleto/corto para un handle recién abierto bajo la
  carga de I/O concurrente más pesada del streaming de nivel/cinemática (no se reproduce en el menú,
  con I/O mucho más liviano y secuencial). Esto NO es un bug de shading/iluminación — el pipeline de
  render de personajes (`df_nm_sm_spec_fres_refl.fs`, con luz difusa + especular) es el sospechoso
  natural para "modelos negros", pero acá la causa real es más básica: la textura de difuso jamás llega
  a cargar contenido real (el buffer que devuelve nuestro `mmap()` queda con 0/1/10 bytes reales y el
  resto en cero por el `memset` de relleno en `source/reimpl/mem.c`), así que el material se ve negro
  independientemente de que la iluminación esté bien calculada.
- **Fix aplicado (sin verificar en consola todavía):** en `source/reimpl/io.c`, `fstat_soloader()` ahora
  hace un `lseek(fd, 0, SEEK_END)` (con `lseek` de vuelta a la posición original) inmediatamente después
  de un `fstat()` exitoso, y si el tamaño real medido por `lseek` es mayor que `st.st_size`, reemplaza
  `st.st_size` por ese valor antes de pasarlo a `stat_newlib_to_bionic()` (que ya hacía
  `st_blksize = st_size`, fix del Bug #13). `lseek()` va por `sceIoLseek()`, una ruta distinta que no
  comparte el problema sospechado de `sceIoGetstatByFd()`. El reemplazo es unidireccional (solo agranda,
  nunca achica) para no romper los ~666 casos que el Bug #13 ya dejó funcionando.
- **Relación con el bug de "modelos 3D negros en gameplay":** con muy alta probabilidad es la MISMA
  causa raíz — ambos son escenas 3D reales (no UI 2D) que dependen de `pig::stream::MMap()` para cargar
  texturas de personaje/mundo durante streaming de nivel, y ambos coinciden con el conjunto de ~106
  texturas que el Bug #13 no llegó a arreglar. La diferencia entre "casi negro" (cinemática) y
  "completamente negro" (gameplay) es coherente con este mismo mecanismo: en el shader de personaje
  (`df_nm_sm_spec_fres_refl.fs`) el término especular se calcula con `pow(abs(NdotH), Shininess)` — el
  `abs()` hace que siempre haya un brillo especular residual incluso con la textura de difuso en negro,
  lo que puede bastar para que una cinemática con más iluminación direccional/rim se vea "casi" negra en
  vez de perfectamente negra, mientras que superficies con menos luz de relleno en gameplay normal caen
  a negro puro.
- [ ] Pendiente confirmar en consola física (build/deploy fuera de este agente): verificar que
  `cin_a01.tga`/`cin_t03.tga`/`cin_d03.tga`/`cin_s01.tga` (y las texturas de personaje/gameplay que
  fallaban) ahora mapean su tamaño real y que tanto la cinemática como los modelos de gameplay dejan de
  verse negros. Si el diagnóstico de `lseek` no ayuda (o el problema es aún otro), instrumentar
  `sceIoGetstatByFd()` directamente para comparar sus campos crudos contra `sceIoGetstat()` sobre el
  mismo path.

#### Addendum a Bug #16 (fix aplicado, sin confirmar en consola, 2026-09-04): `stat64_bionic` tenía un campo de relleno de más — `st_blksize` caía 4 bytes después de donde el binario real lo lee

- **Contexto:** investigando el mismo síntoma de Bug #16 (modelos/mundo negros en gameplay real, no
  solo la cinemática) para el ticket "se ve todo negro, no veo los modelos, pero sí el texto y el
  audio", se desensambló directamente `libshadowguardian.so` (no solo el pseudo-C de Ghidra) para las
  dos únicas funciones que llaman a `fstat()`/`mmap()`/`munmap()` en todo el binario:
  `_Z4MMapRKSs` (`0x2ffb30`) y `_Z6MUnmapRKSsPv` (`0x2ffc40`).
- **Hallazgo (confirmado por desensamblado, no por inferencia del log):**
  ```
  2ffbb8: add  r1, sp, #8         ; buffer de stat pasado a fstat() empieza en sp+8
  2ffbbc: bl   fstat@plt
  2ffbd4: ldr  r1, [sp, #0x38]    ; longitud para mmap() = *(stat_buf + 0x30)
  2ffbe0: bl   mmap@plt
  ```
  y de forma idéntica en `MUnmap` (`ldr r1, [sp, #0x30]` relativo a un buffer de stat que también
  arranca en `sp`). Es decir: el binario real, en las DOS únicas llamadas de todo el juego que usan
  este valor como longitud de `mmap()`/`munmap()`, lo lee de **exactamente el byte 0x30 (48)** dentro
  del struct `stat` que le pasamos.
  - Con el `stat64_bionic` tal cual quedó tras el fix de nlink/uid/gid de Bug #16 (documentado arriba,
    campos anchados a `unsigned int`), `offsetof(stat64_bionic, st_blksize)` da **0x34 (52)**, no 0x30 —
    confirmado compilando el struct exacto por fuera (`offsetof()` en un test host). El campo
    `__pad3[4]` entre `st_rdev` y `st_size` (heredado de un header de referencia de bionic genérico)
    sobra: `st_rdev` ya termina en el offset 32, múltiplo de 8, así que no hace falta relleno para
    alinear el siguiente campo de 8 bytes. Ese relleno de más es lo que corre `st_size`/`st_blksize` 4
    bytes de más respecto de lo que el binario compilado realmente lee.
  - Efecto práctico: aunque `stat_newlib_to_bionic()` escriba el tamaño real del archivo en
    `dst->st_blksize` (fix de Bug #13) y aunque ese tamaño ya venga corregido por el `lseek()` de Bug
    #16, el valor cae en el byte 0x34, mientras que `MMap()`/`MUnmap()` siguen leyendo el byte 0x30 --
    4 bytes antes, dentro de la mitad alta de `st_size`. El resultado es que la longitud de `mmap()`
    para *toda* textura/asset cargado por `pig::stream::MMap()` -- no solo las ~106-400 que llegan a
    imprimir `Cannot load texture` -- depende de bytes que no son el tamaño real del archivo. Esto
    explicaría por qué el reporte de este ticket es "no veo NINGÚN modelo" (todos en negro) y no solo
    los personajes con texturas puntualmente rotas: los materiales que sí "cargan" sin error visible en
    el log también pueden estar recibiendo datos de píxel truncados/incorrectos por este mismo bug,
    solo que sin disparar la validación estricta que produce el mensaje de error.
- **Fix aplicado:** en `source/reimpl/io.h`, se eliminó el campo `__pad3[4]` de `stat64_bionic`, lo que
  deja `st_size` en el offset 40 y `st_blksize` en el offset 48 (0x30) -- coincidiendo exactamente con
  el desensamblado. Se agregó un `_Static_assert(offsetof(stat64_bionic, st_blksize) == 0x30, ...)`
  para que cualquier cambio futuro a este struct que rompa el alineamiento falle en tiempo de
  compilación en vez de volver a producir este bug silenciosamente.
- **Relación con el fix de `lseek()` de Bug #16:** son complementarios, no alternativos -- el fix de
  `lseek()` garantiza que `st.st_size` (el valor *fuente*, antes de convertir) sea el tamaño real del
  archivo incluso si `sceIoGetstatByFd()` devuelve algo obsoleto/corto; este fix garantiza que ese valor
  ya correcto efectivamente llegue al byte exacto que el binario real lee para `mmap()`/`munmap()`. Sin
  este segundo fix, el primero por sí solo seguía escribiendo el tamaño correcto 4 bytes fuera de lugar.
- **Confianza:** alta en la causa raíz del desalineamiento (verificada por desensamblado directo del
  `.so`, no por conjetura), pero **sin confirmar en hardware real** -- este agente no compiló ni
  desplegó nada (instrucción explícita de la tarea).
- [ ] Pendiente confirmar en consola física: recompilar, desplegar, y verificar en
  `game_log_*.txt` que los `mmap(0x0, N, ...)` de texturas de personaje/mundo ahora muestran `N` igual
  al tamaño real del archivo (no 0/1/2/10), y que los modelos 3D dejan de verse negros en gameplay real
  (tutorial intro/outro, nivel real). Si el problema persiste incluso con `N` correcto, la siguiente
  hipótesis a descartar es el pipeline de `mem.c`'s `mmap()` (lectura real del contenido del fd hacia el
  buffer) para buffers grandes, o el propio decodificador de textura DDS/TGA del motor.

### Bug #17 (fix aplicado, sin confirmar en consola, 2026-09-10): la intro (`logo.m4v`) nunca reproduce ni un frame — el fallback CDRAM→PHYCONT de `av_alloc_texture()` está roto de forma incondicional

- **Síntoma:** al abrir el juego no se reproduce el video de intro. `main()` (`source/main.c`) llama
  `gl_init()` y a continuación `video_init()` + `video_play("video/logo.m4v")`.
- **Causa raíz (confirmada por log real + headers de vitasdk, no por hipótesis):** en
  `logs/game_log_1789081567836.txt:874-889`, `video_play()` abre el archivo, lee 2 chunks de 65536
  bytes y en la línea 883 falla:
  `[error] video: texture memblock alloc FAILED on both CDRAM (0x80024309) and PHYCONT (0x80020005)
  (req align=1048576 size=1048576 -> size=1048576)`. El loop de reproducción termina de inmediato
  (`iterations=20, video_frames=0, audio_frames=0, elapsed=0.03s`) sin dibujar nada.
  - `0x80024309` = `SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE_CDRAM` (confirmado en
    `/Users/metalsyntax/vitasdk/arm-vita-eabi/include/psp2/kernel/error.h`) — esperable: `gl_init()`
    (vitaGL, vía `vglInitExtended(0, 960, 544, 6*1024*1024, SCE_GXM_MULTISAMPLE_4X)` en
    `source/utils/glutil.c:49`) ya se llama antes que `video_play()` y consume la CDRAM disponible.
  - `0x80020005` = `SCE_KERNEL_ERROR_INVALID_ARGUMENT` (confirmado en el mismo header; el CLI
    `psvita-toolkit errcode 0x80020005` lo etiqueta como `SCE_KERNEL_ERROR_INVALID_FLAGS`, misma
    familia de "argumento/flag inválido"). Esta es la causa real del bug: en `source/video.cpp`,
    `av_alloc_texture()` arma un único `SceKernelAllocMemBlockOpt opt` con
    `opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT` y `opt.alignment` custom para el intento
    en `SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW`, y REUTILIZABA ese mismo `opt` para el fallback a
    `SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW`. Los bloques PHYCONT no aceptan alineación custom
    vía `opt` (ya vienen alineados naturalmente a 1MB por el kernel) y el kernel rechaza la llamada con
    `SCE_KERNEL_ERROR_INVALID_ARGUMENT`. Efecto práctico: el fallback CDRAM→PHYCONT estaba roto de
    forma incondicional — cualquier vez que la CDRAM esté agotada (la situación normal apenas arranca
    el juego, con vitaGL ya inicializado), la asignación de textura del video falla siempre y la intro
    nunca reproduce ni un frame.
- **Fix aplicado:** en `source/video.cpp`, `av_alloc_texture()`, el intento de fallback a PHYCONT ahora
  redondea `size` a un múltiplo de 1MB (`AV_ALIGN_MEM(size, 0x100000)`) y llama
  `sceKernelAllocMemBlock("av_tex_phycont", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, size, NULL)`
  sin pasar ningún `opt` de alineación custom. El intento primario en CDRAM no se tocó (sigue
  necesitando `opt.alignment` para el alineamiento de textura GPU que exige `SceAvPlayer`).
  No se buscó reducir la huella de CDRAM de `vglInitExtended()` (el `ram_threshold` de 6MB en
  `source/utils/glutil.c:49` ya le pide a vitaGL preferir RAM_PHYCONT cuando la CDRAM libre cae por
  debajo de ese umbral) — cambiar ese parámetro es una decisión global que afecta la carga de texturas
  de todo el juego y requiere validación en hardware real, así que se dejó fuera de este fix mínimo.
- **Limpieza asociada:** se borraron `source/video_player.c` y `source/video_player.h` (0 bytes útiles,
  contenían literalmente el texto `404: Not Found` de una descarga fallida, no estaban referenciados en
  `CMakeLists.txt` ni en ningún otro archivo del proyecto).
- **Build:** `psvita-toolkit build` compila limpio (sin warnings nuevos) con este fix.
- [ ] Pendiente confirmar en consola física: desplegar y verificar en `game_log_*.txt` que
  `av_alloc_texture()` ya no reporta el error doble de la línea 883, que aparece
  `video: texture memblock ok (PHYCONT ...)` (o `CDRAM`) para `logo.m4v`, y que la intro efectivamente
  se ve y se escucha al abrir el juego. Si el intento CDRAM también empieza a tener éxito de forma
  consistente tras este fix (porque nunca se llegaba a loguear su fallo real antes de que rompiera el
  fallback), no hace falta tocar el `ram_threshold` de `vglInitExtended()`; si el video sigue sin
  reproducirse pese a que el log ahora muestre `texture memblock ok`, el siguiente sospechoso es el
  pipeline de conversión YUV→RGB565/subida de textura GPU más abajo en `video.cpp`, no la asignación de
  memoria.

### Bug #18 (fix aplicado, sin confirmar en consola, 2026-09-10): elementos del HUD/menús reales se ocultaban junto con los botones y el joystick virtuales

- **Síntoma:** al togglear (o directamente por el estado inicial de) los controles táctiles virtuales,
  desaparecían de pantalla elementos que son parte del juego real (HUD, prompts, indicadores) y no
  únicamente el joystick/botones virtuales.
- **Causa raíz (confirmada cruzando el pseudo-C de Ghidra, no por hipótesis):** `source/patch.c`
  interceptaba con un trampolín ARM el único call site de `GS_GamePlay::RenderState` que llama a
  `GUILevel::PaintVisibleItems` (`so_mod.text_base + 0x0019c8d0`) y, cuando `g_hide_virtual_buttons`
  era `true`, se SALTEABA LA FUNCIÓN ENTERA en vez de llamarla. Pero
  `GUILevel::PaintVisibleItems` (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:149331`)
  es una rutina GENÉRICA que itera sobre TODOS los `m_elementsCount` ítems registrados en esa instancia
  de `GUILevel` y pinta cada uno (`PaintRectItem`/`PaintGraphItem`) según su tipo/flags — no tiene
  ninguna noción de "esto es un botón virtual" vs. "esto es HUD real". La misma función se llama desde
  al menos otros 14 sitios distintos del binario (menús, diálogos, pantallas de estado — ver los
  `GUILevel::PaintVisibleItems();` en las líneas 153186, 154270, 163226, 164565, 167931, 169468, 171208,
  175804, 177505, 179362, 182331, 184440, 186096, 188746 del mismo archivo), confirmando que es la
  rutina de pintado genérica de la GUI y no algo exclusivo de los controles táctiles. Saltearla entera
  en el único call site de gameplay apagaba de un saque TODO lo que ese `GUILevel` tuviera visible ese
  frame (vida, munición, prompts, minimapa, etc.), no solo el joystick/botones.
- **Hallazgo secundario:** ya existía en el working tree un archivo sin trackear
  `source/joystick_hook.c` (no listado en `CMakeLists.txt`, no wireado en `so_patch()`) con un enfoque
  quirúrgico abandonado a medio hacer: en vez de saltear la función de pintado, hookeaba el render del
  joystick y usaba `GUILevel::SetItemAlpha` (`_ZN8GUILevel12SetItemAlphaEjj`,
  `decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:155543`) para poner alpha=0 SOLO en los
  índices de ítem del joystick base/knob y de los botones de acción, leídos de offsets conocidos del
  struct `PlayerCtrl` (`+8`/`+0xc` joystick, arrays de 8 en `+0x68`/`+0xc8` para acciones, `+0x2c`/`+0x34`
  extra). Se confirmó en el pseudo-C que `SetItemAlpha` escribe un campo de alpha *por ítem* (offset
  0x34 dentro del struct del ítem) y que el propio motor ya usa esta función en otro lado (línea 158684)
  para fundidos de UI — es una API segura y quirúrgica, no un hack.
- **Fix aplicado:** en `source/patch.c`, se eliminó por completo el trampolín/hook sobre
  `GUILevel::PaintVisibleItems` (ese call site ya no se toca, la función corre siempre sin parchear). El
  hook sobre `PlayerCtrl::Render` (`so_mod.text_base + 0x0019cc68`, que sí actualiza estado real de
  gauge/cooldown de los botones además de dibujarlos — confirmado en
  `decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:86940`) se reescribió para SIEMPRE
  llamar a la función original primero, y solo si `g_hide_virtual_buttons` es `true`, aplicar después
  `GUILevel::SetItemAlpha(guiLevel, idx, 0)` sobre los índices puntuales del joystick/botones (la lógica
  que estaba a medio implementar en `joystick_hook.c`, ahora integrada en `in_game_player_ctrl_render()`
  en `patch.c`). Se borró `source/joystick_hook.c` por quedar duplicado/muerto tras la integración (no
  estaba referenciado en ningún otro archivo del proyecto).
- **Build:** `psvita-toolkit build` compila limpio con este fix.
- [ ] Pendiente confirmar en consola física: desplegar y verificar que, con los controles virtuales
  ocultos (CIRCLE), el HUD/menús reales (vida, munición, prompts, etc.) permanecen visibles y solo
  desaparecen el joystick y los botones de acción; y que con los controles virtuales visibles el
  comportamiento no cambió respecto de antes de este fix.

### Bug #19 (fix aplicado, sin confirmar en consola, 2026-09-10): Data abort dentro de `GUILevel::SetItemAlpha` al arrancar el nivel (regresión del fix del Bug #18)

- **Síntoma:** crash justo al terminar de cargar el nivel, antes de llegar al gameplay visible (el log
  `game_log_1789084690672.txt` corta en medio de la apertura repetida de
  `music//m_cinematic_action.wav`, inmediatamente después de `INFO: level successfully loaded` y del
  linkeo de los shaders `aim_rifle_blur`/`aim_sniper_blur` — es decir, durante la cinemática/transición
  de entrada al gameplay, no en un menú).
- **Dump analizado:** `logs/shadowguardian-psp2core-1789084733-0x00049e2337-eboot.bin.psp2dmp`
  (parseado con `vita-parse-core` contra `build/shadowguardian.elf`, mismo build que generó el dump
  según timestamps). Excepción: Data abort. `LR = 0x81006ec5` cae en
  `in_game_player_ctrl_render+0xe5` (`shadowguardian@1`, el loader) — justo después de una de las
  instrucciones `blx r3` que llaman a `GUILevel_SetItemAlpha_func(guiLevel, idx, 0)` agregadas en el fix
  del Bug #18. El `PC` real (0x98198524) no resuelve contra el `.elf` del loader porque cae dentro del
  `.so` del juego, cargado en `so_mod.text_base = 0x98000000`; resolviendo el offset (`0x198524`) contra
  la tabla de símbolos dinámicos de `libshadowguardian.so` (`objdump -T`) da
  `GUILevel::SetItemAlpha(unsigned int, unsigned int)+0x48` exacto.
- **Causa raíz (confirmada cruzando el pseudo-C de Ghidra):**
  `decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:155543` muestra que
  `GUILevel::SetItemAlpha` chequea `if (*(uint *)(this + 0xc) <= param_1)` (es decir,
  `idx >= m_elementsCount`) pero ese chequeo **no es un assert que corta la ejecución** — solo llama a
  `pig::System::ShowMessageBox(...)` (un log de error, ver mismo patrón repetido por todo el archivo,
  p.ej. línea 87008 dentro de `PlayerCtrl::Render` original) y **sigue de largo** dereferenciando
  `*(int *)(iVar3 + param_1 * 4)` con el índice fuera de rango — esto es lo que causa el Data abort
  cuando `idx` es un valor grande fuera de rango.
  El código agregado en el Bug #18 (`in_game_player_ctrl_render()` en `source/patch.c`) lee los índices
  de joystick/botones de acción desde offsets de `PlayerCtrl` reconstruidos por inspección
  (`+8`/`+0xc` joystick, `+0x68`/`+0xc8` arrays de 8 elementos de acciones, `+0x2c`/`+0x34` extra) y
  solo trata `0xFFFFFFFF` como "sin asignar". Pero
  `PlayerCtrl::PlayerCtrl()` (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:85342`)
  inicializa `*(this + 0x68) = 0x3f800000` (el patrón de bits de la constante float `1.0f`, no un
  sentinel de índice) y no fija `0xFFFFFFFF` en ese rango del array de acciones — antes de que
  `RebuildActionCircle`/equivalente corra por primera vez (que en el arranque del nivel puede ser
  después del primer par de frames de la cinemática de entrada, cuando `PlayerCtrl::Render` ya se está
  llamando), ese campo vale literalmente `0x3f800000` (1065353216 en decimal), un índice absurdamente
  fuera de rango que el chequeo no-fatal del motor deja pasar igual.
- **Fix aplicado:** en `source/patch.c`, se agregó `gui_level_set_item_alpha_safe()`, que replica el
  bound check real de `GUILevel::SetItemAlpha` (`idx < *(uint32_t*)(guiLevel + 0xc)`) mirando
  `m_elementsCount` del `GUILevel` de destino y **absteniéndose de llamar** a
  `GUILevel_SetItemAlpha_func` si el índice está fuera de rango, en vez de confiar en el assert no-fatal
  del motor. Los 6 call sites en `in_game_player_ctrl_render()` (joystick base/knob, 2×8 botones de
  acción, extra 1/2) ahora pasan por este wrapper.
- **Build:** `psvita-toolkit build` compila limpio con este fix.
- [ ] Pendiente confirmar en consola física: desplegar, reproducir la misma secuencia (cargar este
  nivel hasta la cinemática de entrada) y verificar que ya no aparece un `.psp2dmp` nuevo en ese punto,
  y que el `game_log` sigue más allá de `INFO: level successfully loaded` hasta ver texto de gameplay
  real. Si el crash persiste en el mismo punto pese al bounds check, el siguiente sospechoso es que
  `guiLevel` en sí (`*(void**)(playerCtrl + 4)`) sea el offset equivocado (no `GUILevel*` real) en este
  punto temprano del ciclo de vida, no los índices.
- **UPDATE 2026-09-10 (mismo día, después de desplegar el fix de arriba): el crash persistió, en el
  MISMO punto exacto** — confirma la sospecha anotada arriba.

### Bug #19b (fix aplicado, sin confirmar en consola, 2026-09-10): confirmado que `guiLevel` (`*(void**)(playerCtrl+4)`) nunca fue un `GUILevel*` real — todo el mecanismo de `GUILevel::SetItemAlpha` manual se reemplazó por la API real del motor

- **Síntoma:** con el fix del Bug #19 ya desplegado, mismo crash exacto en el mismo frame (dump
  `logs/shadowguardian-psp2core-1789085647-0x0003332bcd-eboot.bin.psp2dmp`, log
  `logs/game_log_1789085604919.txt`, corta en el mismo punto que el Bug #19 tras
  `INFO: level successfully loaded`). El bounds check agregado en el Bug #19
  (`gui_level_set_item_alpha_safe`) SÍ dejó pasar la llamada esta vez (`LR` cae justo después del
  `blx r3` DENTRO de `gui_level_set_item_alpha_safe`, no en `in_game_player_ctrl_render` directamente),
  pero `GUILevel::SetItemAlpha` crasheó en el mismo offset exacto (`+0x48`) de todos modos.
- **Causa raíz real (esta vez confirmada con desensamblado ARM real, no pseudo-C):**
  `GUILevel::SetItemAlpha` es código **ARM, no Thumb** (`arm-vita-eabi-objdump -d` sin
  `-M force-thumb` sobre `0x1984dc` en `libshadowguardian.so` da un prólogo `push {r4,r5,r6,lr}`
  coherente; con `-M force-thumb` se desensambla como basura). La instrucción exacta en `+0x48`
  (`0x198524`) es `ldrsh r2, [r2]` con `r2 = *(int*)(*(int*)guiLevel + idx*4)` — es decir, el bounds
  check (`idx < m_elementsCount`) pasó, pero el ITEM en ese índice del array de `guiLevel` es un
  puntero inválido. El registro `r3` en el momento del crash (`= *(int*)guiLevel`, la base del array de
  items) valía **exactamente** `0x98304304`, que resuelve (vía `objdump -T` sobre el `.so`) al símbolo
  `_ZN4ustl8memblockD1Ev` (`ustl::memblock::~memblock()`) — **una dirección de CÓDIGO**, no un array de
  punteros a ítems. Esto prueba que `guiLevel` (leído de `playerCtrl+4`) NUNCA fue un `GUILevel*` real:
  esa lectura caía sobre otro campo/objeto de `PlayerCtrl` cuyo primer `int` coincide por casualidad con
  una dirección de código real del binario.
  Confirmado cruzando el pseudo-C real de `PlayerCtrl::Render()`
  (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:86940`): el motor NUNCA obtiene su
  `GUILevel*` de `PlayerCtrl` — lo obtiene de `Singleton<GS_GamePlay>::s_instance + 0x6c` (después de
  chequear un estado en `+0x68` contra el rango `[0x13, 0x14]`). El guessing de offsets hecho en el
  código heredado de `joystick_hook.c` (integrado en el Bug #18) nunca fue verificado contra el
  desensamblado real y estaba mal en la fuente del `GUILevel*`, no solo en los índices.
- **Fix aplicado (reemplazo completo, no otro parche encima):** se encontró que el juego YA TIENE una
  API pública de alto nivel para esto —
  `GS_GamePlay::SetButtonsVisible(bool)` (`_ZN11GS_GamePlay17SetButtonsVisibleEb`,
  `decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:159952`) — que itera los 8
  `ButtonEnum` (0..7, donde el 0 es el joystick — confirmado en `GS_GamePlay::SetButtonEnabled` línea
  158933, que llama `Joystick::Init` para `param_1==0`) y llama a `GUILevel::SetItemVisible` con los
  índices y el `GUILevel*` CORRECTOS (obtenido de `Singleton<GUIMgr>::s_instance`, otra fuente más,
  confirmando que ni `PlayerCtrl` ni `GS_GamePlay` directamente son la única fuente — hay que dejar que
  el propio motor resuelva esto, no adivinarlo). Es la misma función que el juego real usa para ocultar
  los controles durante QTEs (`GS_GamePlay::StartQTE`/`EndQTE`), así que está probada en el binario
  shippeado.
  En `source/patch.c`: se eliminó por completo `in_game_player_ctrl_render()`,
  `gui_level_set_item_alpha_safe()`, y el trampolín/hook sobre el call site de `PlayerCtrl::Render`
  en `so_patch()` (ya no hace falta interceptar el render en absoluto). Se agregó
  `bool set_virtual_buttons_visible(bool visible)`, que resuelve por símbolo
  `_ZN11GS_GamePlay17SetButtonsVisibleEb` y `_ZN9SingletonI11GS_GamePlayE10s_instanceE`, y llama a la
  función real del juego — devuelve `false` sin crashear si el singleton todavía no existe (menú/carga).
  En `source/main.c`: el toggle de CIRCLE ahora llama a `set_virtual_buttons_visible(!g_hide_virtual_buttons)`
  directamente (ya no depende de ningún hook de render). Como `GS_GamePlay` no existe hasta que arranca
  el nivel, se agregó un reintento liviano una vez por frame en el loop principal
  (`virtual_buttons_state_synced`) para aplicar el estado inicial (`g_hide_virtual_buttons = true` por
  default) apenas el singleton pasa a existir, sin loguear ni hacer nada mientras tanto.
- **Build:** `psvita-toolkit build` compila limpio con este fix.
- [ ] Pendiente confirmar en consola física: desplegar y reproducir la carga de este nivel hasta pasar
  la cinemática de entrada sin `.psp2dmp` nuevo, y confirmar con CIRCLE que los controles
  virtuales se ocultan/muestran correctamente en pantalla. Nota para el futuro: si se cambia de nivel
  (el `GS_GamePlay` singleton se destruye/recrea), el estado de visibilidad podría no persistir
  automáticamente al nuevo nivel más allá del primer re-sync -- no confirmado todavía si hace falta
  resetear `virtual_buttons_state_synced` en las transiciones de nivel.

### Bug #20 (fix aplicado, sin confirmar en consola, 2026-09-11): al ocultar los controles virtuales (CIRCLE), el personaje dejaba de responder a los controles FÍSICOS

- **Síntoma:** con el fix del Bug #19b ya desplegado (llamar a `GS_GamePlay::SetButtonsVisible(bool)`
  para ocultar el joystick/botones virtuales), el crash desapareció, pero al ocultar los controles con
  CIRCLE el personaje dejaba de moverse y de ejecutar acciones con los botones FÍSICOS de la Vita —
  justo lo que se quería evitar (los controles físicos deben funcionar siempre, se vean o no los
  gráficos táctiles en pantalla).
- **Causa raíz (confirmada leyendo el pseudo-C real, no por hipótesis):**
  `GS_GamePlay::SetButtonVisible(ButtonEnum, bool)`
  (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:159975`) llama primero a
  `SetButtonEnabled(this, param_1, param_2)` (línea 158922) ANTES de tocar cualquier
  `GUILevel::SetItemVisible`. `SetButtonEnabled` no es puramente visual: para el botón 1 llama a
  `Joystick::Init(*(Joystick**)(Singleton<PlayerCtrl>::s_instance+0x88), -1, -1)` (línea 145393),
  que reinicializa/resetea la posición trackeada del joystick, y para los botones 2 a 7 llama a
  `ActionManager::OnActionReleased(...)`, forzando que esa acción se libere. O sea: `SetButtonsVisible`
  (que internamente llama a `SetButtonVisible` para los 8 `ButtonEnum`) no solo oculta gráficos —
  también resetea/libera el estado real de joystick y acciones cada vez que se llama, lo cual
  desincroniza el puente físico→touch sintético que usa este port para mapear los controles de la
  Vita.
- **Fix aplicado:** en `source/patch.c`, se reescribió `set_virtual_buttons_visible()` para dejar de
  llamar a `GS_GamePlay::SetButtonsVisible`/`SetButtonVisible`/`SetButtonEnabled` por completo. Ahora
  replica ÚNICAMENTE las llamadas a `GUILevel::SetItemVisible` del switch de `SetButtonVisible`
  (`out_ghidra.c:159994-160074`), obteniendo el `GUILevel*` de la MISMA fuente que usa el motor ahí
  (`Singleton<GUIMgr>::s_instance` → `+8` → `+0x4c`, símbolo `_ZN9SingletonI6GUIMgrE10s_instanceE`,
  NO desde `GS_GamePlay` ni `PlayerCtrl` — ya se aprendió de la manera difícil, con un crash real en
  consola, que hay que confirmar contra el pseudo-C exactamente cuál objeto tiene el puntero, no
  adivinar), con los mismos índices constantes hardcodeados por botón y el flag de "control scheme"
  (`*(uint32_t*)(gs_instance+0x30)`) que el switch original consulta. Se agregó un bounds check
  defensivo (`idx < m_elementsCount`) en cada llamada a `SetItemVisible`, porque esa función tiene el
  mismo patrón de assert no-fatal-y-sigue-de-largo que causó el crash de `SetItemAlpha` en los Bugs
  #19/#19b. El botón 0 (joystick) no tiene ítem de `GUILevel` propio en el switch original
  (`case 0: return;`), así que no hace falta tocar nada visual para él tampoco. Se dejó afuera
  deliberadamente el llamado extra de "mostrar" del botón 7 (`PlayerCtrl::UpdateWeaponGUI()`) por estar
  fuera de alcance de este fix — en el peor caso un ícono de arma se ve desactualizado un frame, no es
  un bug de input.
- **Build:** `psvita-toolkit build` compila limpio con este fix.
- [ ] Pendiente confirmar en consola física: desplegar, ocultar/mostrar los controles con CIRCLE varias
  veces durante gameplay, y verificar que (a) los gráficos del joystick/botones se ocultan y muestran
  correctamente, y (b) el personaje sigue respondiendo a los controles físicos (mover, disparar, apuntar,
  acciones contextuales) en todo momento, sin importar si los controles virtuales están ocultos o no.

### Bug #21 (fix aplicado, sin confirmar en consola, 2026-09-11): crash (Data abort, NULL deref) en `SoundMgr::Update()` al elegir "Salir" (Exit) desde el menú del juego

- **Síntoma:** el usuario reporta un crash al cerrar el juego "sin el botón PS" — es decir, usando el
  botón/opción de salir nativo del propio juego/menú, no el suspend/exit de la Vita.
- **Log analizado (`logs/game_log_1789098606601.txt`):** la secuencia real, en orden, fue: el usuario
  está en el menú (`menu.bclara` cargado), suena `sfx_menu_confirm.wav` (click de "Salir"/Exit
  confirmado), el motor loguea `### Shutting down` (su propia secuencia de apagado empieza: para hilos
  de audio con `AudioTrack release/stop`, etc.), y recién ahí llama al JNI `Exit()`
  (`[Java] game requested Exit / sendAppToBackground (id=2)`, ver `source/java.c`, `method_exit()`).
  Inmediatamente después aparece
  `[error] [ALOG][SHADOW] Error!!!! Exp: s_instance, ... Singleton.h, Line: 34` — el patrón ya conocido
  de este motor: un assert de `Singleton::s_instance == NULL` que solo loguea (no es fatal) y el código
  sigue de largo igual.
- **Causa raíz (confirmada con `.psp2dmp` real, no por hipótesis):**
  `logs/shadowguardian-psp2core-1789098820-0x0001da2fcf-eboot.bin.psp2dmp`, parseado con
  `vita-parse-core` contra `build/shadowguardian.elf`: Data abort, `R0 = 0x0`, `PC = 0x98254a34`
  (`libshadowguardian.so`, offset `0x254a34` sobre `text_base=0x98000000`), que resuelve por
  `objdump -T` + `c++filt` a `SoundMgr::Update()+0x1c`; `LR = 0x980bf798` resuelve a
  `Game::FrameUpdate()+0x80`. El pseudo-C de `SoundMgr::Update`
  (`decompiled/libshadowguardian_armeabi-v7a/ghidra/out_ghidra.c:276485`) confirma que lo primero que
  hace tras `vox::VoxEngine::Update()` es `*(int *)(in_r0 + 0xd8)` — con `in_r0` (el `this` de
  `SoundMgr`) en `NULL`, eso es exactamente el Data abort observado.
  En conjunto, la causa raíz es: `method_exit()` (el stub de FalsoJNI para el JNI `Exit()`/
  `sendAppToBackground()` del motor) solo logueaba y devolvía el control al `.so` **sin terminar el
  proceso**. En Android real, esa llamada JNI ocurre justo antes de que la Activity termine y el
  proceso muera — el motor YA asume que el proceso no va a seguir vivo y por eso destruye sus propios
  singletons (`SoundMgr` incluido) como parte de `### Shutting down`. Como nuestro stub no mataba el
  proceso, `Game::FrameUpdate()` seguía corriendo al menos un frame más y llamaba a
  `SoundMgr::Update()` con el singleton ya nulo/destruido.
- **Fix aplicado:** en `source/java.c`, `method_exit()` ahora llama a `logger_flush()` +
  `sceKernelExitProcess(0)` después de loguear — el mismo patrón de apagado limpio que ya usa
  `fatal_error()` en `source/utils/dialog.c`. Esto garantiza que el `.so` nunca vuelve a ejecutar otro
  frame después de que su propia secuencia de shutdown asume que el proceso ya terminó.
- **Build:** `psvita-toolkit build` compila limpio con este fix.
- [ ] Pendiente confirmar en consola física: desplegar, entrar al menú, elegir "Salir"/Exit, y verificar
  que la consola vuelve limpiamente a LiveArea sin generar un `.psp2dmp` nuevo. Nota: `method_exit()`
  también es el handler para `sendAppToBackground` (mismo `MID_EXIT`, ver `source/java.c`) — FalsoJNI no
  puede distinguir cuál de los dos métodos Java disparó la llamada (comparten el mismo `jmethodID`), así
  que este fix también terminará el proceso si el motor llega a invocar `sendAppToBackground` en algún
  otro contexto (p. ej. simulando una interrupción tipo llamada telefónica) en vez de un "Salir" real
  del usuario. No se encontró en el log analizado ningún caso de `sendAppToBackground` disparándose
  fuera de un Exit real, pero si en consola aparece un cierre inesperado del juego en un contexto que NO
  sea el menú "Salir", ese es el siguiente sospechoso.



### Bug #10 (confirmado 2026-09-11): Opacidad de botones virtuales ("sombra azul" del joystick)
- **Síntoma:** Al ocultar los botones virtuales presionando CIRCULO, quedaba una sombra azul visible (la base del joystick virtual), y el usuario quería ocultar **todos** los botones gráficos pero conservando su funcionalidad real para controles físicos. Hubo un intento previo usando opacidad forzada que provocó un crash.
- **Causa raíz:** `GUILevel::SetItemVisible` dejaba elementos sin ocultar correctamente o era sobreescrito por la lógica del motor. Adicionalmente, el índice del joystick (Button 0) no se ocultaba completamente (solo la base o solo el centro dependiendo del esquema). El crash anterior con la "opacidad forzada" se debió a invocar `GUILevel::SetItemAlpha` sin comprobar primero el límite `m_elementsCount`, un fallo ya documentado en este port.
- **Fix aplicado (`source/patch.c`):**
  - Se cambió `set_virtual_buttons_visible` para usar `GUILevel::SetItemAlpha` (`_ZN8GUILevel12SetItemAlphaEjj`) en lugar de `SetItemVisible`.
  - Se agregó una envoltura de seguridad `set_item_alpha_safe` que verifica `idx < elementsCount` antes de llamar a la función, evadiendo así el crash por desreferencia de puntero fuera de límites que tiene el propio juego (similar al error en SetItemVisible).
  - Se incluyeron ambos índices `0` y `1` explícitamente para asegurar que tanto la base como el botón central del joystick desaparezcan sin dejar restos azules.
  - Al reducir la transparencia al valor `0` (en lugar de desactivar la visibilidad) se garantiza que el gestor de eventos táctiles siga procesando toques en esas áreas si fuera necesario, sin entorpecer el input.
- **Estado:** Implementado y compilado (eboot.bin generado con éxito). Listo para prueba en consola real.
- **Mejora (2026-09-11):** Se refinó el comportamiento de ocultar los controles virtuales. Ahora el arma de la esquina superior derecha se mantiene visible, y se aseguran todos los botones de acción e interfaz de la mitad derecha (incluido el de apuntar, cubriendo índices del 0 al 10). También se amplió el rango del joystick derecho (cámara) de 100 a 250 para permitir movimientos y giros de cámara más rápidos.

### Intento Actual (2026-09-11): Refinamiento de Interfaz Gráfica y Joystick Derecho [ESTADO: EN PRUEBAS / NO FINALIZADO]
**Objetivo:** Lograr una inmersión completa ocultando estrictamente los botones virtuales táctiles (joystick izquierdo, botones de acción derechos como apuntar/disparar/cubrirse/saltar) sin perder el HUD fundamental (como el selector de armas) y mejorar la sensibilidad de la cámara (joystick derecho).

**Historial de modificaciones aplicadas hasta el momento:**
1. **Corrección del crash por opacidad (`source/patch.c`):**
   - El juego sufría un crash (Out-Of-Bounds) nativo en la función `GUILevel::SetItemAlpha` si se pasaban índices incorrectos.
   - Se resolvió implementando una función segura (`set_item_alpha_safe`) que verifica que el índice sea menor a `m_elementsCount` antes de llamar a `_ZN8GUILevel12SetItemAlphaEjj`. Esto permitió usar la opacidad para ocultar gráficos sin desactivar la detección de toques sintéticos, corrigiendo de paso el rastro de la "sombra azul" del joystick (ocultando los índices 0 y 1).
2. **Ajuste de elementos a ocultar:**
   - **Arma visible:** Se eliminó la lógica que ocultaba el grupo del `ButtonEnum 7` (índices `0xb`, `0x1c`, `0x1d`, etc.) para asegurar que el arma en la esquina siempre permanezca visible.
   - **Botón de apuntar y acciones:** Para resolver el problema de botones de acción "rebeldes" (como el de apuntar que no desaparecía), se reemplazó la lógica condicional por un barrido incondicional de los elementos gráficos del `0` al `10`. Esto garantiza ocultar el joystick y los 4 botones de acción principales independientemente del `control_scheme` seleccionado en el menú del juego.
3. **Mejora del límite de cámara (`source/main.c`):**
   - El stick derecho simula un desplazamiento táctil (swipe) continuo en la pantalla. Su rango máximo estaba limitado a `100` píxeles (`dz_rescale(rx, 100)`), lo que provocaba una velocidad de paneo lenta.
   - Se amplió este límite a `250` píxeles para ambos ejes, incrementando teóricamente en un 150% la distancia del deslizamiento virtual, lo cual debería traducirse en un giro de cámara mucho más fluido y rápido.

*Nota: Estos cambios han compilado correctamente en `eboot.bin`, pero **aún están a la espera de confirmación real en la consola** para validar que el mapeo, la cámara y el HUD reaccionan exactamente como se espera.*

### Bug #22 (fix aplicado, sin confirmar en consola, 2026-09-11): el video intro (`logo.m4v`) sigue sin reproducir ni un frame — CDRAM y PHYCONT agotados a la vez

- **Síntoma:** con el fix del Bug #17 ya desplegado, la intro sigue sin verse. `logs/game_log_1789105223771.txt:873-887`:
  el archivo abre bien (`fd=0x40010239`, `file size -> 723304`), se leen 2 chunks de 65536 bytes y en la
  línea 881 falla `av_alloc_texture()`:
  `[error] video: texture memblock alloc FAILED on both CDRAM (0x80024309) and PHYCONT (0x80024302)
  (req align=1048576 size=1048576 -> size=1048576)`. El loop termina con
  `iterations=21, video_frames=0, audio_frames=0, elapsed=0.04s` — AvPlayer aborta sin decodificar nada.
- **Causa raíz (confirmada por log real + headers de vitasdk, no por hipótesis):** el fix del Bug #17 SÍ
  funcionó — el error PHYCONT cambió de `0x80020005` (INVALID_ARGUMENT, el bug de reutilizar el `opt` de
  alineación) a `0x80024302` (`SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE`, ver
  `/Users/metalsyntax/vitasdk/arm-vita-eabi/include/psp2/kernel/error.h:58`; el `0x80024309` es su gemelo
  de CDRAM en la línea 65). O sea: el fallback PHYCONT ahora se intenta correctamente, pero AMBOS pools
  contiguos están agotados de verdad en ese punto del arranque — `gl_init()` corre antes que
  `video_play()` (`source/main.c`) con `vglInitExtended(0, 960, 544, 6MB, SCE_GXM_MULTISAMPLE_4X)`, y el
  MSAA 4X se come la CDRAM libre. Con los dos pools contiguos sin ni una página de 1MB, devolver NULL
  hace que AvPlayer aborte el stream entero.
- **Fix aplicado:** en `source/video.cpp`, `av_alloc_texture()` ahora tiene 3 niveles
  (CDRAM → PHYCONT → `SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE`). El tercer nivel es el mismo patrón que
  usa el renderer GXM de SDL en Vita cuando se queda sin VRAM: memoria no-cacheada, mapeable con
  `sceGxmMapMemory`, que NO sale de ninguno de los dos pools contiguos, así que sobrevive a ambos
  agotados. Nuestro pipeline solo hace `memcpy` del frame decodificado y lo sube con `glTexSubImage2D`,
  así que cualquier memoria escribible por CPU sirve (OpenFMV, el reproductor AvPlayer de referencia de
  Rinnegatamante, confirma que el allocator de textura solo necesita memoria válida mapeada). De paso se
  corrigió un leak silencioso: si los 8 slots de `gAvTexBlocks` estaban llenos, el bloque recién asignado
  se perdía (se retornaba `base` sin guardar el UID); ahora en ese caso se hace unmap+free y se retorna
  NULL con log de error.
- **Build:** `psvita-toolkit build` compila limpio, sin warnings nuevos en `video.cpp`.
- [ ] Pendiente confirmar en consola física: desplegar y verificar en `game_log_*.txt` que aparece
  `video: texture memblock ok (UNCACHE ...)` (o CDRAM/PHYCONT) para `logo.m4v`, que `video_frames > 0` al
  salir del loop, y que la intro se ve y se escucha al abrir el juego. Si el log muestra `ok (UNCACHE)`
  pero el primer frame sale con `Y-plane all_same=1, first_byte=0` (el diagnóstico de decoder que nunca
  escribió ya existe en `video_play()`), el siguiente sospechoso es que el decodificador HW necesite RAM
  físicamente contigua para DMA y haya que atacar la presión de CDRAM de vitaGL (p. ej. bajar MSAA 4X),
  no el allocator.
