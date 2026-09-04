He avanzado en la ejecución del plan de port según lo estipulado en
PORTING_PLAN.md y he registrado todos los avances detalladamente en
port_progress.md.  
 ──────

### Resumen de Avances y Trabajo Realizado

#### 1. Implementación del Subsistema de Audio Nativo (sceAudioOut)

• Arquitectura de Audio Confirmada:  
 • El motor nativo de Gameloft utiliza el middleware Vox  
 (vox::DriverAndroid, \_ZN3vox...).  
 • Vox decodifica y mezcla internamente todo el audio (BGM  
 ambiental en IMA-ADPCM/VoxN y SFX en PCM) en C++ a 44100 Hz  
 estéreo PCM de 16-bit con signo.  
 • Este flujo se envía en ráfagas a android.media.AudioTrack.  
 write() como sumidero final.  
 • Implementación en C:  
 • Creados audio.h y audio.c.  
 • Configurado un ring buffer / FIFO thread-safe de 128 KB.  
 • Hilo de mezcla continuo sg_audio_mixer anclado al Núcleo 1  
 (SCE_KERNEL_CPU_MASK_USER_1) con prioridad 0x10000100 y puerto  
 SCE_AUDIO_OUT_PORT_TYPE_BGM (MIX_GRAIN = 2048 muestras). De esta
forma, el procesamiento y temporización de audio no compiten por
ciclos con el renderizado ni la lógica en el Núcleo 0.  
 • Enlazado audiotrack_write en java.c extrayendo el buffer del  
 JavaDynArray hacia audio_write_pcm.  
 • Mapeadas llamadas de parada y limpieza en AudioTrack.stop y  
 AudioTrack.release mediante audio_reset_buffer().  
 • Añadido source/audio.c al CMakeLists.txt y enlazado con  
 SceAudio_stub.

#### 2. Calibración del Sistema de Entrada y Controles

• Corrección de Eventos Táctiles:  
 • Al contrastar con el bytecode decompilado de  
 GameGLSurfaceView.java, se identificó que el motor nativo  
 espera:  
 • 1 = ACTION_DOWN  
 • 2 = ACTION_MOVE  
 • 0 = ACTION_UP  
 • Se corrigió en main.c, sustituyendo la suposición inicial  
 invertida.  
 • Soporte Multi-Touch Capacitivo:  
 • Implementado seguimiento simultáneo diferencial (touch vs  
 touch_old) en resolución nativa de 960x544, detectando toques  
 nuevos, en movimiento y liberados de forma independiente.  
 • Mapeo de Botones Físicos:  
 • Detección de flancos (pressed y released) con  
 sceCtrlPeekBufferPositive:  
 • START y CÍRCULO → KEYCODE_BACK (4) (Pausa / Regresar en  
 menús del juego).  
 • SELECT → KEYCODE_MENU (82) (Menú de opciones).

#### 3. Compilación y Paquetes Generados

• Compilación y enlazado ejecutados con VitaSDK (arm-vita-eabi-gcc  
 10.3.0):  
 • shadowguardian.vpk (698 KB)  
 • eboot.bin (616 KB)  
 • Preparación de Datos: Se combinaron los recursos originales de  
 GloftSGHP/ con libshadowguardian.so y res/ dentro de  
 ux0_data/shadowguardian/, dejando una carpeta única lista para  
 transferir a ux0:data/shadowguardian/.  
 ──────

### Próximos Pasos (Fase 5: Pruebas en Hardware Real)

1. Transferencia a la consola PS Vita:  
   • Copiar el contenido de ux0_data/shadowguardian/ a  
   ux0:data/shadowguardian/ en la Vita (vía USB o FTP con  
   VitaShell).  
   • Instalar shadowguardian.vpk.
2. Ejecución y Captura de Logs:  
   • Iniciar el juego desde el LiveArea y monitorizar la salida por
   consola / archivo de log en ux0:data/shadowguardian/.
3. Triage de Crashes (si se produce alguno):  
   • En caso de crash o salida inesperada, aplicar la metodología  
   so-crash-triage: analizar el coredump .psp2dmp resultante con  
   vita-parse-core y cruzar la dirección con out_ghidra.c para  
   corregir el punto exacto.  


● Bash(find "ux0*data/shadowguardian" -name ".*\*" -delete)
● Bash(ls -la "ux0_data/shadowguardian/")
⎿ <output +22 lines>
-rwx------ 1 metalsyntax staff 221825 Sep 3 21:43
response_SP.txt
-rwx------ 1 metalsyntax staff 51 Sep 3 21:43 settings
drwx------ 1 metalsyntax staff 131072 Sep 3 19:05 saves
drwx------ 1 metalsyntax staff 131072 Sep 3 21:44 shaders
drwx------ 1 metalsyntax staff 131072 Sep 3 21:44 sounds
-rwx------ 1 metalsyntax staff 66148190 Sep 3 21:44
sprites_1_6
-rwx------ 1 metalsyntax staff 1107890 Sep 3 21:44 states
drwx------ 1 metalsyntax staff 131072 Sep 3 21:44 text
drwx------ 1 metalsyntax staff 131072 Sep 3 21:44 textures
drwx------ 1 metalsyntax staff 131072 Sep 3 21:44 video
(ctrl+o to collapse)

La copia y preparación completa de los archivos de datos ha  
 finalizado con éxito. Además, se limpiaron todos los archivos  
 residuales de metadatos de macOS (.\_\*) para evitar cualquier  
 conflicto con el sistema de archivos de la PS Vita.

### Estado del Entorno de Despliegue

La carpeta contiene ahora el árbol completo de ejecución:

Archivo / Directorio │ Contenido
─────────────────────────────┼──────────────────────────────────────
libshadowguardian.so │ Binario nativo ARMv7 original
res/ │ Recursos Java (raw, drawable) para
│ GLResLoader
anims/, models/, textures/ │ Mallas 3D, esqueletos y texturas del
│ motor
sounds/, music/ │ Bancos de audio PCM / IMA-ADPCM /
│ VoxN
levels/, collisions/ │ Mapas y geometría de colisiones
shaders/ │ Shaders GLSL para vitaGL
fonts, gui*1_6, sprites_1_6 │ Elementos de interfaz y tipografías
text/, response*\*.txt │ Textos y localización multilenguaje
video/ │ Cinemáticas del juego
saves/, logs/ │ Directorios de runtime para partidas
│ y depuración
──────

### Instrucciones para Probar en Hardware Real (Fase 5)

1. Instalar el VPK:
   • Transferir shadowguardian.vpk a la PS Vita e instalarlo con  
   VitaShell.
2. Copiar la Carpeta de Datos:
   • Copiar la carpeta completa a ux0:data/shadowguardian/ en la  
   consola.
3. Iniciar el Juego:
   • Abrir Shadow Guardian desde el LiveArea.
   • En caso de presentarse algún error o cierre inesperado, el  
   archivo de log en ux0:data/shadowguardian/logs/ o el coredump .
   psp2dmp en ux0:data/ nos dará el punto exacto para aplicar so-  
   crash-triage.
