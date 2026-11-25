# FM1 Sampler — Android + M-VAVE FM-1

Sampler, editor de audio y **looper de 16 pistas** para Android. El teléfono ejecuta el motor de audio, la biblioteca y la interfaz; el **M-VAVE FM-1** puede actuar como superficie de control MIDI por **USB**, con botones, teclado y encoders. El motor y la interfaz se encuentran en `engine/`; Android integra ese motor mediante JNI.

> **Versión 0.42.8 (en validación):** cada una de las ocho pistas del secuenciador tiene un fader independiente de 0 a 150 %, guardado con el proyecto y aplicado también al exportar SONG.

> **Versión 0.42.7 ([release estable](https://github.com/Koolkatze/fm1-sampler/releases/tag/v0.42.7)):** polifonía ampliada por sampler: Melodic, One Shot y Looped 64 voces; Kit 32; Granular 16; Paulstretch 8. Conserva el método de sustituir la voz más antigua cuando se llena el límite.

> **Versión 0.42.6 ([release estable](https://github.com/Koolkatze/fm1-sampler/releases/tag/v0.42.6)):** guardado automático de la sesión y recuperación al abrir Android, con botón NEW PROJECT en PROJECT. Conserva los 16 proyectos manuales.

> **Versión 0.42.5 ([release estable](https://github.com/Koolkatze/fm1-sampler/releases/tag/v0.42.5)):** los seis iconos centrales de HOME se muestran sin etiquetas; se conserva la separación y el acceso directo al metrónomo.

> **Versión 0.42.4 ([release estable](https://github.com/Koolkatze/fm1-sampler/releases/tag/v0.42.4)):** icono MET directo en HOME; seis iconos centrales con separación uniforme y alineación inferior de transporte, PROJECT y K1–K4.

> **Versión 0.42.3 — APK firmada y validada; promoción estable pendiente:** EXPORT SONG indica el estado de exportación en naranja hasta confirmar que el WAV se ha guardado en Downloads. [Notas 0.42.3](docs/RELEASE_NOTES_0423.md).

> **Versión 0.42.2 (preview anterior):** Correct Tempo añade DECIMALS en K4 a la derecha de TAP TEMPO, con precisión de 0,01 BPM. [Notas 0.42.2](docs/RELEASE_NOTES_0422.md).

> **Versión 0.42.1 (preview anterior):** LIBRARY vuelve a estar disponible durante la edición del secuenciador de un pad. [Notas 0.42.1](docs/RELEASE_NOTES_0421.md).

> **Versión 0.42.0 (preview anterior):** edición de pads en directo, MET sincronizado con PLAY/STOP y Correct Tempo; [notas 0.42](docs/RELEASE_NOTES_042.md). Las versiones actuales pasan las verificaciones automatizadas antes de publicarse.

> **Versión 0.41.0 (preview anterior):** rama `feature/16-looper-metronome-monitor`. **PROJECT, LOOP y SONG ahora tienen pantallas independientes a pantalla completa**, 12 pads con 16 pistas de looper y botones directos para escoger cualquiera de las 8 escenas del secuenciador. Los pads y el orden SONG se guardan exclusivamente en el proyecto `.fm1p` elegido; una apertura nueva sin sesión automática muestra LOOP/SONG vacíos. Se mantienen **16 pistas + MET** y el tema Light/Dark. La monitorización del metrónomo por FM-1 es experimental y **no se ha validado físicamente**; la APK no flashea el firmware.

## Descargas y compilación

- **Última APK estable:** [Descargar FM1 Sampler 0.42.7 firmada](https://github.com/Koolkatze/fm1-sampler/releases/download/v0.42.7/FM1-Sampler-0.42.7-signed.apk) · [Releases](https://github.com/Koolkatze/fm1-sampler/releases). Se mantiene el certificado permanente y el ID de paquete; no desinstales la APK anterior.
- **Código Android:** `android/`; **motor de audio/UI:** `engine/`; **protocolo de hardware:** `PROTOCOL.md`.
- **Utilidad ADB para backups TAR y proyectos `.fm1p`:** [`tools/FM1_ADB_Recuperacion_0389.py`](tools/FM1_ADB_Recuperacion_0389.py) y [guía de uso](docs/ADB_RECUPERACION_0389.md).
- **Documentación:** [16 pistas + MET v0.40](docs/FM1_16TRACK_METRONOME_040.md), [resumen técnico y pendientes](docs/STATUS_040.md), [secuenciador + Tempo Flex v0.39](docs/FM1_SEQUENCER_039.md), [biblioteca, proyectos y backups](docs/SAMPLE_LIBRARY.md), [escenas y SONG](docs/SCENES_AND_SONG.md), [migración](docs/MIGRACION_SEGURA.md).

Las compilaciones debug de GitHub usan **`com.ferluht.fm1sampler.scenes`**; la variante original usa **`com.ferluht.fm1sampler`**. Son instalaciones con datos privados separados. Las versiones Scenes firmadas con el certificado permanente son compatibles entre sí; las antiguas APK de otras firmas no lo son: **no desinstales una app con proyectos/loops sin haber exportado y verificado una copia de seguridad externa**.

## Volumen del secuenciador (0.42.8)

- En SEQUENCER, desliza el control situado junto a cada nombre de pista: 0 % silencia, 100 % conserva el volumen original y 150 % aumenta la ganancia. El valor se muestra a la derecha del deslizador.
- Los ocho niveles son independientes de las 16 pistas COMPOSE y de las velocidades de cada paso. Se aplican a las escenas y pads del mismo proyecto, incluida la exportación SONG. Los cambios se escuchan en directo sin reiniciar las notas.
- SAVE PROJECT y recuperación automática conservan los niveles; NEW PROJECT y archivos anteriores a 0.42.8 usan 100 %. Se mantienen las asignaciones físicas K1–K4.

## Polifonía (0.42.7)

| Modo | Voces por sampler |
|---|---:|
| Melodic / One Shot / Looped | 64 |
| Kit | 32 |
| Granular | 16 |
| Paulstretch | 8 |

Las voces conservan su envolvente, incluyendo release. Cuando se alcanza el máximo, se mantiene la política original: ocupar la primera voz libre o sustituir la más antigua si no queda ninguna. No se introduce prioridad por volumen o tecla pulsada. Los límites de Granular y Paulstretch acotan el coste DSP; se reservan ocho streams Paulstretch, no 64. El rendimiento real con alta polifonía depende del móvil.

## Recuperación automática y NEW PROJECT (0.42.6)

- Android conserva la sesión de trabajo en `PROYECTOS/CURRENT_SESSION.fm1p`, separada de los 16 slots manuales. Se comprueban cambios cada 3 segundos y se solicita un checkpoint al salir de la app; durante PLAY se actualiza periódicamente la posición. La captura y escritura se realizan en segundo plano y el archivo anterior se conserva hasta el reemplazo atómico del nuevo.
- Al abrir de nuevo se recupera la última sesión completada: clips, muestras, efectos, tempo, pads, arreglo, patrones, pista y página activa. También se recupera el modo de reproducción almacenado. En la primera apertura tras instalar la actualización no existe todavía una sesión automática.
- **PROJECT → NEW PROJECT → CONFIRM NEW PROJECT** crea un espacio vacío. Conserva todos los slots guardados, WAV, packs y biblioteca; para conservar una copia de la sesión anterior usa SAVE PROJECT antes de confirmar. LOAD PROJECT sustituye la sesión activa por el slot elegido.
- Una grabación que sigue en curso, un cierre brusco entre checkpoints, un fallo de almacenamiento o un archivo superior al límite de 512 MiB pueden impedir recuperar los cambios más recientes; no es una garantía de pérdida cero. SAVE PROJECT y SAVE BACKUP siguen siendo recomendables. La restauración de un archivo dañado no lo sobrescribe automáticamente.

## Edición de pads en directo (nueva actualización)

1. En **PROJECT → LOOP / SONG MODE**, selecciona un pad guardado. **COMPOSE** abre el deck y mantiene ese pad sonando; si otro pad estaba activo, inicia el seleccionado. SELECT y el selector de pista saltan a pistas libres de todo el proyecto. Las pistas usadas en COMPOSE u otros pads quedan bloqueadas aunque no suenen. Graba con REC y termina la toma con REC/STOP: el clip se añade automáticamente al pad. BACK TO PADS vuelve a la pantalla LOOP sin restaurar la composición.
2. **SEQUENCER** abre el patrón del pad en reproducción. La primera edición usa una copia privada de la escena vinculada (o un patrón vacío si estaba OFF). Los pasos, notas y compás se guardan directamente en esa copia; no se modifican los otros pads ni las ocho escenas compartidas. Desde 0.42.1, toca LIBRARY, elige una pista del secuenciador y selecciona un sample para cambiarlo sin detener el pad; también funciona PRESETS en el FM-1. Los instrumentos del secuenciador siguen siendo compartidos entre los patrones del proyecto: cambiar un sample cambia esa pista en todos los patrones, sin alterar sus pasos ni el audio ya grabado. SAVE PROJECT conserva la asignación. BACK TO PADS conserva el pad activo.
3. **HOME** (físico o táctil) sale del editor, restaura las pistas de COMPOSE y su secuenciador. Las nuevas pistas se incorporan también a COMPOSE cuando comparten longitud y tempo de origen; si no, permanecen en su pad para evitar alterar audio. Al editar un pad desde SONG se congela el arreglo y se repite el pad seleccionado.
4. **MET:** PLAY/REC sigue el transporte; STOP silencia el clic. REC/REC MON son de grabación; OFF permanece mudo. CORRECT TEMPO calibra la referencia musical sin cambiar el audio ni el pitch: K1 corrige BPM (pasos de 0,1), TAP TEMPO estima el tempo de las pulsaciones y K4 DECIMALS, a su derecha, afina en pasos de 0,01 BPM. En pantalla arrastra K4 para fijar la parte decimal de .00 a .99; el encoder físico K4 permite subir o bajar centésimas incluso al cruzar un BPM entero. El valor completo se muestra con dos decimales en K1. El reloj interno conserva esa precisión; el monitor físico FM-1 sigue usando el protocolo MIDI de 0,1 BPM. El cambio de tempo y los patrones de pad se guardan con el proyecto.
5. El auto guardado actualiza el pad en memoria; **SAVE PROJECT** conserva el trabajo en el archivo `.fm1p`, y **SAVE BACKUP** crea una copia externa. No se crea un proyecto global al salir. Los efectos globales y fuentes comunes siguen siendo parámetros del proyecto, no una copia independiente por pad. Durante la edición se bloquean los cambios de longitud, borrado global y los controles globales de tempo/cinta.

## Novedades 0.41: PROJECT, LOOP y SONG a pantalla completa

- En HOME, toca el **último icono central PROJECT**. En el menú se muestran botones grandes **SAVE PROJECT**, **LOAD PROJECT**, **EXPORT ALL LOOPS** y **LOOP / SONG MODE**.
- **LOOP / SONG MODE** abre otra pantalla a ancho completo. Desde allí elige **COMPOSE**, **LOOP MODE**, **SONG MODE**, **PROJECT** o **HOME**. No hay columnas superpuestas.
- **LOOP:** 12 pads grandes C1–B1, 16 botones T1–T16 en dos filas, botones **OFF + SC 1–SC 8** para enlazar **una escena completa del secuenciador** (sus 8 pistas y pasos) al pad seleccionado, además de las pistas de audio elegidas. **SAVE PAD/REPLACE PAD**, **LAUNCH**, **EXIT LOOP** y **DELETE PAD** mantienen el comportamiento original y las confirmaciones.
- **SONG:** pads, orden del arreglo con hasta 8 pasos grandes visibles por página, **+ ADD PAD**, **REMOVE**, **MOVE LEFT/RIGHT**, **PLAY SONG**, **STOP SONG** y **EXPORT SONG**. Al pulsar EXPORT SONG, el botón se vuelve naranja y dice **EXPORTING** durante el renderizado y la publicación en Downloads; no admite pulsaciones duplicadas. Cuando el WAV se publica correctamente, muestra **SONG EXPORTED** en naranja hasta una nueva exportación o la carga de otro proyecto. Si falla, muestra **EXPORT FAILED** y permite reintentar.
- **Al abrir la APK (desde 0.42.6):** se recupera la sesión automática si existe; NEW PROJECT inicia los pads de LOOP y SONG vacíos. **Al cargar un proyecto distinto:** se restauran exclusivamente los pads y el arreglo contenidos en ese archivo, o siguen vacíos si no existen. Las fuentes de muestras y los parámetros de la app conservan su comportamiento de inicio.
- **Guardado:** usa **SAVE PROJECT** en la ranura deseada para conservar escenas, canción, muestras, loops, secuenciador y ajustes. El antiguo archivo global \`fm1_scenes.dat\` ya no se carga ni se sobreescribe automáticamente; si existía, se conserva intacto como posible copia de recuperación.

**Actualización segura:** la versión 0.41 mantiene el ID de la app Scenes y la misma firma de los anteriores paquetes GitHub, con **versionCode 65**. Para evitar perder acceso a escenas antiguas guardadas solamente en \`fm1_scenes.dat\`, **convierte ese trabajo en un SAVE PROJECT usando 0.40 ANTES de actualizar**. No desinstales la app y exporta siempre SAVE BACKUP. Esta actualización es de Android; no certifica el firmware físico del FM-1.

## Guía rápida 0.40: 16 pistas + metrónomo

**Primeros pasos:** los seis iconos centrales (tema, micrófono, SEQ, LIBRARY, MET y PROJECT) no llevan etiquetas de texto y tienen separación vertical uniforme; PROJECT y PLAY/STOP/REC/CLEAR se alinean con la parte inferior de K1–K4, sin invadir la línea temporal. Toca MET para seleccionar directamente el metrónomo. El BPM se muestra en el lado izquierdo del dibujo de la cinta, no junto a los knobs. En **HOME / COMPOSE**, selecciona una de las pistas **T1–T16**; después de T16 aparece **MET**, que es un controlador independiente y no guarda audio. La vista de pistas tiene dos columnas de ocho. El secuenciador mantiene sus **ocho pistas propias** y sus **ocho escenas**.

**Control de MET:** abre MET con el selector de pista o el SELECT físico; pulsa **SEL** para cambiar entre sus tres páginas. En el móvil, usa **los mismos pequeños indicadores de página de las pistas normales**, a la derecha del nombre de página; los botones grandes TEMPO / PITCH y METER / CLICK ya no aparecen:

| Página | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **TEMPO / PITCH** | Tempo Flex, BPM | Pitch Bend, semitonos | Pitch Bend, cents | Pitched Tempo |
| **METER / CLICK** | Compás 1/4–64/4 | Swing, 0–100 % | Volumen del clic, 0–100 % | Modo de clic |
| **CORRECT TEMPO** | BPM 30–300, 0,1 BPM | TAP TEMPO (botón grande) | TAP TEMPO | DECIMALS, 0,01 BPM |

Los modos son **PLAY/REC** (clic durante PLAY y REC), **REC** (solo grabación), **OFF**, **PLAY/REC MON**, **REC MON** y **OFF MON**. Los modos *MON* requieren firmware específico en el FM-1: la mezcla principal se reproduce en el móvil y el clic de monitor va **solo al minijack del FM-1**. La salida y sincronía exactas **no están aún comprobadas en hardware real**. La APK por sí sola no modifica el firmware del FM-1.

**Seguridad de datos:** la APK 0.40 mantiene el ID `com.ferluht.fm1sampler.scenes`, el certificado de firma permanente y sube el versionCode a **64**; permite actualizar la misma instalación firmada sin desinstalar. Crea un **SAVE PROJECT** y **SAVE BACKUP** comprobable antes de actualizar. El proyecto nuevo usa FMP2 v5, las escenas SCN3 y el estado de app v6; conserva la lectura de proyectos antiguos.

**Manual de controles, compatibilidad y limitaciones:** [FM1_16TRACK_METRONOME_040.md](docs/FM1_16TRACK_METRONOME_040.md). **Resumen de pruebas:** [STATUS_040.md](docs/STATUS_040.md).

## Funciones de la aplicación Android

### HOME, biblioteca y controles de reproducción

- Interfaz horizontal de cassette con tema **claro/oscuro** y monitor de audio. El indicador de pista activo es **T1…T16 o MET en naranja**, grande, sin recuadro, fondo, raya lateral ni texto «TRACK», inmediatamente a la izquierda del visor de muestras. Tocarlo cambia la pista.
- **16 pistas** independientes (T1–T16) con instrumento/sampler, parámetros, efectos, grabaciones y audio en el looper; **MET** es una ranura adicional de control, sin ocupar pista grabable.
- **SEQUENCER (0.39):** botón en HOME que abre **8 pistas adicionales**, con 16 pasos por página, patrones 1/4–64/4, samples independientes seleccionados desde LIBRARY, transporte sincronizado y pantalla Light/Dark. [Controles, tempo y firma](docs/FM1_SEQUENCER_039.md).
- **PLAY, STOP, REC/OVERDUB y CLEAR** en HOME. PLAY y STOP controlan la cinta; durante la primera toma, el transporte puede finalizar la grabación antes de detener la reproducción. REC arma la primera toma y después permite overdub por pista.
- **Visor de forma de onda** del sample, selección de modo, ventana START/END y voces; **timeline de los loops** con cabezal de reproducción y *scrub* táctil.
- Barra de herramientas: **tema**, **micrófono**, **SEQ**, **LIBRARY**, **MET**, **PROJECT**. MET selecciona la pista de metrónomo sin tener que recorrer T1–T16. El micrófono permite grabar un sample a partir de un umbral de señal ajustable.
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
| **SEQ / SEQUENCER (0.39)** | Entra y sale del secuenciador independiente de 8 pistas. Los parámetros anteriores de TAPE permanecen en la interfaz de páginas del teléfono. |
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

- **COMPOSE:** tocar instrumentos y grabar loops en las dieciséis pistas, con transporte común, overdub, control de longitud (x2, /2), fuente SOUND/MIC y *tape scrub*.
- **PROJECT:** `SAVE PROJECT`, `LOAD PROJECT`, `EXPORT ALL LOOPS`, `SAVE LOOP OF TRACK`, `CLEAR LOOP OF TRACK`, `CLEAR ALL LOOPS` (confirmación), `OPEN THE EDITOR`, `LOOP LENGTH x2`, `LOOP LENGTH /2` y `LOOP SOURCE`.
- **16 slots de proyecto**, en **dos páginas de 8** (`project_1.fm1p`…`project_16.fm1p`) en formato **FMP2**. Cada archivo contiene el proyecto completo guardado: estado COMPOSE, muestras originales incrustadas, configuración de pistas/FX, audio de los loops, escenas y orden SONG. Guardar con un nombre crea además un archivo `.name`. Cargar y reemplazar requieren confirmación según el estado.
- **LOOP MODE:** doce escenas reutilizables, **C1…B1**, cada una con una selección de hasta dieciséis clips de loop. Guardar, reemplazar y eliminar escena con confirmación; lanzar una escena conmuta en el siguiente compás con fundido breve. Los clips guardados en escenas se conservan independientemente del borrado de los loops activos.
- **SONG MODE:** composición con una **secuencia de hasta 64 escenas**, admitiendo repeticiones; `+ ADD`, `REMOVE`, `MOVE <`, `MOVE >`, `PLAY SONG` y `STOP`. Las escenas se encadenan sincronizadas; al terminar la última, la canción **se detiene**.
- **EXPORT SONG (0.38.12):** botón de la pantalla SONG independiente, accesible desde PROJECT → LOOP / SONG MODE. Exporta **SONG.wav** desde la primera hasta la última escena del arrangement SONG (hasta 64 pasos, incluidas repeticiones), con audio de cada escena, DSP/FX de las pistas, reverb, delay, saturación y volumen/limitador master, más una cola final de efectos. Se publica en `Downloads/FM1-SAMPLER/EXPORTED LOOPS/` mediante Android MediaStore. **Si SONG no contiene escenas, muestra `NO SCENES IN SONG` y no genera archivo.** No modifica las escenas, la composición ni los proyectos.
- **SAVE / LOAD PROJECT (0.38.12):** ahora se muestran **PAGE 1 / 2** y **PAGE 2 / 2**, cada una con ocho slots grandes. PREV/NEXT cambia de página; los slots 1–8 mantienen sus nombres y bytes, y 9–16 añaden capacidad sin renumerar proyectos antiguos.
- **Actualización segura:** Android conserva datos privados al instalar una APK nueva sobre la anterior únicamente con el **mismo package ID, firma digital y versión superior**. La publicación requiere keystore permanente y huella de certificado fijada en GitHub; **si no están configurados no se publica una APK incompatible**. Las antiguas previews con firma temporal pueden necesitar migración de respaldo una sola vez; [instrucciones detalladas](docs/UPDATES_WITHOUT_DATA_LOSS.md).
- **EXPORT ALL LOOPS (0.38.11):** exporta WAV estéreo 16-bit **con todo el procesamiento del looper**, no los buffers crudos. Cada `TRACK_01.wav`…`TRACK_16.wav` contiene su **tape speed / varispeed**, **saturación, LO-FI, WOBBLE, hiss y tono de cinta**, modulación LFO de volumen/pan, nivel/panorama de mezcla, **sidechain** y sus **reverb/delay sends y retornos**. `MASTER.wav` reproduce la mezcla completa con **reverb/delay global y el limitador/ganancia de master**. Los loops suenan como en la APK y conservan su duración relativa al tempo/velocidad del proyecto, con cola de FX de ciclos anteriores.
  Se guardan en `Downloads/FM1-SAMPLER/EXPORTED LOOPS/` (Android 10+) y nunca alteran los clips originales. **Los stems procesados pueden no sumar exactamente el MASTER** por la no linealidad del delay y del limitador; lo ya grabado con efectos antes del loop no se vuelve a procesar dos veces. Solo se exportan **las pistas del loop actual**, no automáticamente toda una secuencia de SONG.

### Almacenamiento, packs y backups

La biblioteca privada de la app actual vive bajo `files/FM1-SAMPLER/`:

```text
FM1-SAMPLER/
├── PROYECTOS/         # hasta 16 archivos .fm1p, nombres, escenas y assets
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
| **`.fm1p`** | Un proyecto FMP2 autónomo (slots 1–16). | `tools/FM1_ADB_Recuperacion_0389.py importar-fm1p ...` |
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

- **SELECT:** cambiar pista 1–16 y MET; en MET, **SEL** alterna las tres páginas del metrónomo; **PRESETS:** navegación y carga inmediata de samples; **ALGORITHM:** cambiar el modo de sample; **K1…K4:** parámetros de la página.
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

## Android Picture-in-Picture (Scenes APK, Android 8+)

Press Home or switch apps while the FM1 Sampler Scenes APK is open. Android shows a floating 16:9 mini-player with the original animated HOME cassette and current track/playback status. The audio and MIDI engine continues running; tap and expand the window to return to your unchanged editor and project. The document picker does not activate PiP. Android settings may require enabling Picture-in-Picture for this app. Closing the floating player finishes the session.

## FM1 knob disconnect recovery (0.38.14)

A USB reconnection after a lost OCT+ or OCT- Note Off no longer traps the physical K1 in the NOTE REPEAT menu. The Android app resets held button/key states and the MIDI packet parser without unloading projects or stopping the loop. A separate physical-firmware source fix also preserves quadrature encoder motion across idle periods; it is not installed by the phone APK and must be built and flashed independently after hardware validation.

## Replacing existing scenes (0.38.15)

In PROJECT > LOOP / SONG MODE, tap a pad to **select it without launching it**. Choose the tracks to include (1–16), then tap REPLACE and CONFIRM SAVE within three seconds. The slot is overwritten from the **COMPOSE** loops, not the previously playing scene, so newly selected tracks replace the previous configuration. LAUNCH (or the physical MIDI scene key) plays the new version; the original COMPOSE workspace and the other scene slots are untouched. Invalid empty selections cannot wipe a saved scene. The existing SONG arrangement continues to reference the same slot and will play its new content.
