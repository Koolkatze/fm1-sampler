# FM1 Sampler — Android + M-VAVE FM-1

Sampler, editor de audio y **looper de 8 pistas** para Android. El teléfono ejecuta el motor de audio, la biblioteca y la interfaz; el **M-VAVE FM-1** puede actuar como superficie de control MIDI por **USB**, con botones, teclado y encoders. El motor y la interfaz se encuentran en `engine/`; Android integra ese motor mediante JNI.

> **Estado de esta rama:** `feature/oneshot-packs-on-scenes`, versión **FM1 Sampler 0.38.10**, que conserva la corrección de navegación de la raíz, añade utilidades de recuperación ADB y muestra el atajo NOTE REPEAT en INFO. No confundir este proyecto con Workbench, Urban-1 o Blackhole Remote. La compilación Android no instala firmware en el FM-1 físico.

## Descargas y compilación

- **APK de la versión 0.38.10:** [FM1 Sampler 0.38.10 — GitHub Releases](../../releases/tag/v0.38-library-preview.11). Comprueba que la publicación y la APK hayan terminado de compilar antes de descargar.
- **Código Android:** `android/`; **motor de audio/UI:** `engine/`; **protocolo de hardware:** `PROTOCOL.md`.
- **Utilidad ADB para backups TAR y proyectos `.fm1p`:** [`tools/FM1_ADB_Recuperacion_0389.py`](tools/FM1_ADB_Recuperacion_0389.py) y [guía de uso](docs/ADB_RECUPERACION_0389.md).
- **Documentación:** [biblioteca, importación, proyectos y backups](docs/SAMPLE_LIBRARY.md), [escenas y SONG](docs/SCENES_AND_SONG.md), [migración de versiones anteriores](docs/MIGRACION_SEGURA.md).

Las compilaciones debug de GitHub usan **`com.ferluht.fm1sampler.scenes`**; la variante original usa **`com.ferluht.fm1sampler`**. Son instalaciones con datos privados separados. Las firmas de debug de diferentes ejecuciones pueden no coincidir: **no desinstales una app con proyectos/loops sin haber exportado y verificado una copia de seguridad externa**.

## Funciones de la aplicación Android

### HOME, biblioteca y controles de reproducción

- Interfaz horizontal de cassette con tema **claro/oscuro** y monitor de audio. El indicador de pista activo es **solo T1…T8 en naranja**, grande, sin recuadro, fondo, raya lateral ni texto «TRACK», inmediatamente a la izquierda del visor de muestras. Tocarlo cambia la pista.
- **8 pistas** independientes con instrumento/sampler, parámetros, efectos, grabaciones y audio en el looper.
- **PLAY, STOP, REC/OVERDUB y CLEAR** en HOME. PLAY y STOP controlan la cinta; durante la primera toma, el transporte puede finalizar la grabación antes de detener la reproducción. REC arma la primera toma y después permite overdub por pista.
- **Visor de forma de onda** del sample, selección de modo, ventana START/END y voces; **timeline de los loops** con cabezal de reproducción y *scrub* táctil.
- Barra de herramientas: **tema**, **micrófono**, **LIBRARY**, **PROJECT**. El micrófono permite grabar un sample a partir de un umbral de señal ajustable.
- **BACK/ENTER** en HOME y en LIBRARY: navegar por directorios internos sin abrir el selector de Android. Desde `FM1-SAMPLER`, ENTER abre de nuevo la última carpeta elegida aunque BACK haya dejado la raíz sin elemento seleccionado (corrección 0.38.9).
- **PRESETS físico**: mueve el selector por carpetas y archivos, un elemento por paso. Al elegir un WAV compatible lo carga **inmediatamente** en la pista activa; ENTER es solo para carpetas y **no** para reproducir un WAV.
- **LIBRARY** abre la biblioteca interna; botones **+ IMPORT ZIP**, **SAVE BACKUP** e **IMPORT BACKUP**. Los ZIP de paquetes externos no son lo mismo que los ZIP de backup completos.

### Instrumentos, edición y síntesis

| Modo de sample | Función |
|---|---|
| **MELODIC** | Sample cromático con ajustes START/END, velocidad y tono; autoafinación YIN opcional. |
| **ONE-SHOT** | Disparo de muestra de una sola vez; controles de rango, velocidad y tono. |
| **LOOPED** | Repetición del sample con región de reproducción ajustable. |
| **KIT / SLICE** | 16 cortes por transitorios (o repartidos regularmente); umbral de corte y tono. |
| **GRANULAR** | Reproducción mediante granos con controles de densidad/cantidad y dispersión. |
| **PAULSTRETCH** | Estiramiento espectral del sample y ajuste de anchura estéreo. |

El **editor** permite seleccionar muestras, ajustar región START/END y sus parámetros, editar FX, ENV, LFO, ARP, cinta, mezcla y teclado/escala. Los sonidos se normalizan al cargarlos y pasan por un limitador suave. Para MELODIC y ONE-SHOT existe detección de tono para afinar el instrumento.

### Páginas y parámetros accesibles desde la app o los K1–K4 del FM-1

| Página / botón | Parámetros principales |
|---|---|
| **HOME** | START, END y dos parámetros específicos del modo (SPEED/PITCH, KIT, GRANULAR o PAUL). |
| **FX** | Envíos de reverb y delay por pista y controles globales de espacio/tiempo. |
| **ENV** | Envelope ADSR: attack, decay, sustain y release por pista. |
| **LFO** | Rate, depth, destino (pitch/filter/volume/pan) y forma de onda. |
| **ARP** | Rate (incluidas divisiones ternarias), patrón, octavas y duración. |
| **SEQ / TAPE** | Velocidad global del loop, EXCITER, LO-FI y WOBBLE. |
| **EDIT / MIXER** | Volumen de pista y compresión sidechain: origen, threshold y ratio. |
| **GLO / KEY** | Escala, tónica, octava y modo KEY/CHORD. |
| **SAVE / LOOP** | Entrada de grabación de micrófono o sonido, umbral y fuente. |
| **SEL / LENGTH** | Duplicar o dividir longitud del loop; visualización de barras/BPM. |
| **CUE** | Selección de subdivisiones de NOTE REPEAT, manejada con K1; también accesible desde la interfaz. |

El editor y los controles del móvil funcionan sin necesidad de modificar el firmware físico; la disponibilidad de determinados eventos MIDI desde el FM-1 depende del firmware instalado en el dispositivo.

### Atajos físicos del FM-1: NOTE REPEAT y transporte

| Pulsación física | Comportamiento |
|---|---|
| **OCT− + PLAY/STOP** | Reinicia el loop al momento si se detecta OCT− previamente pulsado. |
| **OCT+ + PLAY** | **NOTE REPEAT momentáneo** mientras se mantiene la combinación; al soltarla deja de repetir. |
| **OCT+ + OCT−** | **NOTE REPEAT MENU**; entra en la selección de subdivisión, con K1, independientemente del orden de llegada de ambos eventos MIDI. |
| **PLAY/STOP sin OCT pulsado** | Transporte normal PLAY/STOP. |

Las doce subdivisiones son: **1/1, 1/1T, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 y 1/32T**. La caja desplegable **INFO** de HOME enseña expresamente **`OCT+ + OCT- = NOTE REPEAT MENU (K1)`**, el reinicio, NOTE REPEAT y los eventos MIDI recibidos. **INFO/HIDE** solo muestra u oculta esa ayuda; no simula botones OCT físicos.

### Grabación, proyectos y modos de composición

- **COMPOSE:** tocar instrumentos y grabar loops en las ocho pistas, con transporte común, overdub, control de longitud (x2, /2), fuente SOUND/MIC y *tape scrub*.
- **PROJECT:** `SAVE PROJECT`, `LOAD PROJECT`, `EXPORT ALL LOOPS`, `SAVE LOOP OF TRACK`, `CLEAR LOOP OF TRACK`, `CLEAR ALL LOOPS` (confirmación), `OPEN THE EDITOR`, `LOOP LENGTH x2`, `LOOP LENGTH /2` y `LOOP SOURCE`.
- **8 slots de proyecto** (`project_1.fm1p`…`project_8.fm1p`) en formato **FMP2**. Cada archivo contiene el proyecto completo guardado: estado COMPOSE, muestras originales incrustadas, configuración de pistas/FX, audio de los loops, escenas y orden SONG. Guardar con un nombre crea además un archivo `.name`. Cargar y reemplazar requieren confirmación según el estado.
- **LOOP MODE:** doce escenas reutilizables, **C1…B1**, cada una con una selección de hasta ocho clips de loop. Guardar, reemplazar y eliminar escena con confirmación; lanzar una escena conmuta en el siguiente compás con fundido breve. Los clips guardados en escenas se conservan independientemente del borrado de los loops activos.
- **SONG MODE:** composición con una **secuencia de hasta 64 escenas**, admitiendo repeticiones; `+ ADD`, `REMOVE`, `MOVE <`, `MOVE >`, `PLAY SONG` y `STOP`. Las escenas se encadenan sincronizadas; al terminar la última, la canción **se detiene**.
- **EXPORT ALL LOOPS:** exporta copias WAV de pistas grabadas y mezcla `MASTER.wav` a `Downloads/FM1-SAMPLER/EXPORTED LOOPS/` en Android 10+; no borra originales.

### Almacenamiento, packs y backups

La biblioteca privada de la app actual vive bajo `files/FM1-SAMPLER/`:

```text
FM1-SAMPLER/
├── PROYECTOS/         # 8 archivos .fm1p, nombres, escenas y assets
├── OFFICIAL/          # Bass, Drums, Keys, Pads
├── SAVED/
│   ├── LOOPS/         # WAV de loops guardados
│   ├── RECORDINGS/    # grabaciones
│   └── IMPORTS/       # audio importado
└── PACKS/             # packs ZIP extraídos, con directorios originales
```

- **+ IMPORT ZIP:** selecciona un pack mediante el selector de Android y lo extrae en `PACKS/<nombre del ZIP>/`; mantiene subcarpetas y otros archivos. La fuente original se conserva.
- **Importar audio:** carga WAV y otros tipos compatibles desde el selector de Android, convirtiendo a WAV cuando hace falta.
- **SAVE BACKUP (interfaz):** exporta un **ZIP completo** de la biblioteca ya guardada mediante el selector de Android; incluye proyectos, escenas, WAV, packs y archivos auxiliares. **Primero usa SAVE PROJECT** para conservar los cambios que todavía estén solo en RAM.
- **IMPORT BACKUP (interfaz):** comprueba manifiesto, tamaños y **SHA-256** antes de restaurar. Por defecto **KEEP EXISTING** no sobrescribe archivos; **REPLACE** exige confirmación y protege las copias anteriores.
- **Migración local:** al actualizar una misma instalación, se migran rutas antiguas `files/samples`, `project_*.fm1p`, `sample_*.wav` al árbol `FM1-SAMPLER` sin sobreescribir los existentes.

**No confundir los formatos:**

| Formato | Para qué sirve | Cómo se importa |
|---|---|---|
| **`.fm1p`** | Un proyecto FMP2 autónomo (slot 1–8). | `tools/FM1_ADB_Recuperacion_0389.py importar-fm1p ...` |
| **`.tar`** | Copia ADB del árbol privado `files/`, con proyectos, WAV, escenas y demás archivos. | `tools/FM1_ADB_Recuperacion_0389.py restaurar ...` |
| **`.zip` de SAVE BACKUP** | Backup completo creado desde LIBRARY dentro de Android. | **LIBRARY → IMPORT BACKUP**, no el script ADB TAR |
| **`.zip` de pack** | Samples y recursos de un paquete musical. | **LIBRARY → + IMPORT ZIP**, no IMPORT BACKUP |

### Recuperación por ADB — Windows / PowerShell

Se utiliza para restaurar datos antiguos incluso cuando un APK tiene otra firma; **no modifica el firmware del FM-1**. Requiere `adb`, depuración USB autorizada y una APK **debuggable** que permita `run-as`.

```powershell
# 1. Verificar la conexión y el paquete
adb devices
python tools/FM1_ADB_Recuperacion_0389.py comprobar --app scenes

# 2. Validar TAR, simular su restauración e importar solo los archivos ausentes
python tools/FM1_ADB_Recuperacion_0389.py revisar "FM1_BACKUP.tar"
python tools/FM1_ADB_Recuperacion_0389.py restaurar "FM1_BACKUP.tar" --app scenes --simular
python tools/FM1_ADB_Recuperacion_0389.py restaurar "FM1_BACKUP.tar" --app scenes

# 3. Importar un proyecto FMP2 aislado al slot 2 (si el slot está libre)
python tools/FM1_ADB_Recuperacion_0389.py importar-fm1p "project_2.fm1p" --slot 2
python tools/FM1_ADB_Recuperacion_0389.py importar-fm1p "project_2.fm1p" --slot 2 --aplicar
```

Para **reemplazar** deliberadamente un slot, existe `--reemplazar`, que obliga a guardar antes sus originales en el ordenador. La restauración TAR **omite los archivos existentes**, incluso si tienen nombres iguales a archivos del respaldo. La transferencia utiliza `adb push` con staging en el teléfono y **SHA-256** antes de publicar el fichero: evita el fallo de Windows/Android `tar: EOF: Illegal seek`. **Nunca desinstala, borra datos ni elimina el TAR de respaldo.** Guía detallada: [docs/ADB_RECUPERACION_0389.md](docs/ADB_RECUPERACION_0389.md).

## FM-1: controladores, pantalla y alcance

- **SELECT:** cambiar pista 1–8; **PRESETS:** navegación y carga inmediata de samples; **ALGORITHM:** cambiar el modo de sample; **K1…K4:** parámetros de la página.
- Botones físicos **HOME, FX, ENV, LFO, ARP, SEQ, EDIT, GLO, SAVE, SEL, PLAY, REC** según el mapeo MIDI del firmware usado.
- El motor incluye código para pantalla secundaria **240×240 y LEDs** del FM-1 mediante mensajes SysEx. La visualización y algunos eventos de teclas y botones dependen de lo que envíe efectivamente el firmware físico. La APK por sí sola **no reprograma** el FM-1.
- El teclado puede funcionar cromáticamente o en escalas/acordes; en LOOP MODE se asigna al banco de escenas C1…B1.

## Arquitectura y pruebas

- `engine/dsp/`: motores sampler, normalización, filtros, granular, Paulstretch, time-stretch, ADSR, efectos, arpegiador y mezcla.
- `engine/session/`: audio, loops, overdub, escenas, canciones, grabación, persistencia FMP2 y exportación.
- `engine/app/`: control MIDI, páginas, controles por pista, biblioteca y proyectos.
- `engine/ui/`: interfaz táctil HOME/EDITOR/LIBRARY/PROJECT y diagnósticos.
- `android/`: Oboe, JNI, conexión MIDI/USB, selector de archivos y almacenamiento SAF.
- `firmware/`: fuente de firmware relacionado y pruebas de host; compilar el proyecto Android **no** lo instala en el dispositivo.

Para pruebas locales:

```bash
make -C engine -j2 all
make -C engine "$(pwd)/engine/build/test_app"
./engine/build/test_app
python3 -m py_compile tools/FM1_ADB_Recuperacion_0389.py
cd android && gradle :app:assembleDebug --no-daemon
```

Las pruebas automatizadas y compilaciones de GitHub no sustituyen una prueba real en Android con un FM-1 conectado. **No se han verificado en hardware todas las operaciones de ADB ni todos los firmwares físicos.**

## Imágenes de referencia (de versiones anteriores)

![Pantallas del FM-1](shots/readme_fm1_v37.png)

![Cassette deck](shots/readme_deck_v37.png)

![Grabación y micrófono](shots/readme_firstrec_v37.png) ![Micrófono](shots/readme_mic_v37.png)

Las capturas son ilustrativas y pueden no mostrar los cambios recientes de HOME, LIBRARY o PROJECT.
