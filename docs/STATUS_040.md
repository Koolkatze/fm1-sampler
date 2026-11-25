# FM1 Sampler — resumen técnico de la versión 0.40

Fecha: 2026-11-24. Rama: `feature/16-looper-metronome-monitor`.

## Implementado

- Looper: 16 pistas de audio T1–T16 y una pista de control MET que no utiliza audio de looper.
- Secuenciador: se conservan las ocho pistas propias y sus ocho escenas; integración con las escenas del looper y SONG.
- MET: dos páginas de controles K1–K4, seleccionables mediante SEL; tempo flexible, afinación en semitonos y cents, pitched tempo, compás 1/4–64/4, swing, volumen y seis modos normales/MON.
- Persistencia: FMP2 v5, SCN3 y estado de la aplicación v6; lectura retrocompatible.
- Audio privado: síntesis de clic en DAC del FM-1 para modos MON, fuera de la grabación USB; música en altavoz interno del teléfono en modo monitor.
- Conservación de temas Light/Dark existentes.

## Verificaciones que han pasado

- CI del motor y regresión MIDI, incluido acceso 16+MET.
- Compilación de Android con firma permanente y comprobación de certificado y `com.ferluht.fm1sampler.scenes`, `versionCode=64`.
- Generación del archivo de firmware/Workbench por el toolchain JieLi verificado y comprobación de empaquetado.
- Pruebas de firmware en host de click/DAC y separación USB.

APK CI: https://github.com/Koolkatze/fm1-sampler/actions/runs/45376916551

Firmware CI: https://github.com/Koolkatze/fm1-sampler/actions/runs/45298048937

## Limitaciones abiertas

1. No hay medición en hardware real de la diferencia de latencia entre el audio del móvil y el clic del FM-1; **no se ha demostrado sincronización sample-accurate entre dispositivos**.
2. No se ha probado físicamente la compatibilidad del cargador ni la salida jack de este firmware. El archivo de firmware es de evaluación y no debe presentarse como producción.
3. Deben revisarse en el teléfono las actualizaciones in-place, importación de proyectos antiguos, autonomía, sesiones largas y carga DSP con 16 pistas.
4. La PR #7 permanece en borrador hasta superar las pruebas anteriores.

Guía de controles: [FM1_16TRACK_METRONOME_040.md](FM1_16TRACK_METRONOME_040.md).
