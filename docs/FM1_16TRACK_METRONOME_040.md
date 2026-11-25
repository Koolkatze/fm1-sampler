# FM1 Sampler 0.40 — 16 pistas y metrónomo del FM-1

**Estado: vista previa de pruebas (24 de noviembre de 2026).** Se ha compilado una APK firmada para Android, con pruebas automatizadas superadas. La sincronía del clic por la salida analógica del FM-1 y la latencia entre los dos dispositivos **todavía requieren pruebas físicas**. Las ilustraciones no son capturas reales; por decisión editorial, esta guía es exclusivamente textual.

## Lo nuevo

- **16 pistas de audio independientes** en el looper de COMPOSE (T1–T16). Los listados usan dos columnas de ocho y mantienen los modos claro/oscuro originales.
- **Metrónomo independiente (MET)**: ranura de control número 17. No consume una pista de grabación de audio ni se graba en los WAV del looper.
- **8 pistas de secuenciador adicionales y 8 escenas de secuenciador**: siguen siendo independientes de las 16 pistas de audio; no se sustituyen ni se mezclan sus muestras.
- Las escenas del looper admiten selecciones de las 16 pistas; LOOP y SONG mantienen el transporte sincronizado.
- Los proyectos `.fm1p` guardan las 16 pistas y los parámetros nuevos. Hay compatibilidad de lectura con los formatos anteriores (FMP2 v2–v4 y SCN1/SCN2), y los archivos nuevos se guardan con FMP2 v5/SCN3 y estado de app v6.

## Acceso al metrónomo

1. En HOME, toca el **icono de pasos SEQ en la columna central** para abrir SEQUENCER. Los cinco iconos centrales (tema, micrófono, SEQ, LIBRARY, PROJECT) están espaciados para que PROJECT quede al mismo nivel que los controles PLAY/STOP/REC, dejando libre el visor inferior. El BPM se lee a la izquierda del dibujo de la cinta de HOME, tanto con INFO visible como oculto.
2. Abre **HOME / COMPOSE** y selecciona T1–T16 o MET con el indicador de pista (pasa de T16 a MET). El botón **SELECT** físico también permite cambiar la pista y entrar en MET.
3. En MET se muestran las mismas dos páginas de cuatro knobs, **TEMPO / PITCH** y **METER / CLICK**, pero se eligen con los **dos pequeños marcadores de página en la posición habitual de las demás pistas**, no con botones grandes. **SEL** físico también alterna las páginas.
4. Los cuatro encoders K1–K4 controlan únicamente los parámetros de la página visible cuando MET está seleccionado.

| Página | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 — TEMPO / PITCH | Tempo Flex (BPM, sin alterar el tono) | Pitch Bend (semitonos) | Pitch Bend (centésimas de semitono) | Pitched Tempo (tempo y tono acoplados) |
| 2 — METER / CLICK | Compás 1/4–64/4 | Swing 0–100 % | Volumen del clic 0–100 % | Modo del metrónomo |

### Modos del clic

| Modo | ¿Cuándo suena? | Salida prevista |
|---|---|---|
| PLAY/REC | Durante reproducción y grabación | Mezcla principal del móvil |
| REC | Solamente durante REC/overdub | Mezcla principal del móvil |
| OFF | Nunca | Sin clic |
| PLAY/REC MON | Durante reproducción y grabación | **Solo** minijack del FM-1 |
| REC MON | Solamente durante REC/overdub | **Solo** minijack del FM-1 |
| OFF MON | Nunca | Sin clic; ruta de monitorización privada seleccionada |

En modos **MON**, la mezcla musical de Android está prevista para salir por el altavoz integrado del teléfono; el firmware modificado reproduce el clic por el DAC/minijack del FM-1, sin incluirlo en la captura de audio por USB. Si Android no puede seleccionar el altavoz interno, el sistema no debe redirigir inadvertidamente la mezcla principal al FM-1. Para obtener la salida MON, **no basta instalar la APK: hace falta el firmware FM-1 compatible**, aún sin validar en el equipo físico.

## Actualización de Android

1. Desde la versión instalada, guarda el proyecto actual con **SAVE PROJECT** y crea un **SAVE BACKUP** de toda la biblioteca; verifica que puedes acceder al ZIP externo.
2. Descarga la APK desde la [pre-release 0.40](../../releases/tag/v0.40-metronome-preview.1) o desde el [artefacto firmado de pruebas](https://github.com/Koolkatze/fm1-sampler/actions/runs/45376916551/artifacts/14910989314).
3. Instálala **encima de la anterior, sin desinstalarla**. El package ID es `com.ferluht.fm1sampler.scenes`, versión `0.40.0`, versionCode **64**. La comprobación CI coteja la firma con el certificado permanente de la rama de previews.
4. Carga un proyecto antiguo, verifica escenas/SONG y prueba la edición de pistas 9–16. Conserva la copia hasta acabar la comprobación.

Si tu APK antigua tiene **otro package ID o firma**, Android no permite la actualización conservando su almacenamiento privado. Consulta [UPDATES_WITHOUT_DATA_LOSS.md](UPDATES_WITHOUT_DATA_LOSS.md) antes de realizar ninguna migración.

## Firmware físico FM-1

El [build de firmware y Workbench](https://github.com/Koolkatze/fm1-sampler/actions/runs/45298048937) contiene archivos de **evaluación, no validados físicamente**. La compilación con toolchain JieLi, los tests MIDI, el aislamiento de audio en host y las verificaciones del paquete han pasado, pero **esto no constituye autorización para flashearlo sin riesgo**. No lo flashees para una actuación en directo ni asumas sincronía perfecta hasta contrastar la placa, el cargador de actualización, el DAC y la latencia real.

La aplicación envía a firmware un mensaje SysEx de fase/tempo (comando 07) y el firmware ajusta la fase de su reloj de 44,1 kHz. Existen dos relojes de audio independientes y posibles fluctuaciones de USB: la precisión final debe medirse con un loopback físico.

## Verificación y enlaces

- [Compilación Android 0.40 firmada y tests: éxito](https://github.com/Koolkatze/fm1-sampler/actions/runs/45376916551).
- [Firmware y Workbench de evaluación](https://github.com/Koolkatze/fm1-sampler/actions/runs/45298048937).
- [Cambios en la PR #7 (sigue en borrador)](https://github.com/Koolkatze/fm1-sampler/pull/7).
- [Actualizaciones sin pérdida de datos](UPDATES_WITHOUT_DATA_LOSS.md).
