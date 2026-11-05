# FM1-SAMPLER: library root, imports, navigation and saved projects

This change continues from `feature/oneshot-packs-on-scenes`. It does not
change the FM-1 USB firmware, audio synthesis, COMPOSE, SONG, scenes, CUE,
or the self-contained FMP2 project file format.

## Directory tree created on first launch

The application creates a **single FM1-SAMPLER root** inside its Android
**app-private files directory** (not the public `Documents` folder):

```
FM1-SAMPLER/
  PROYECTOS/
    project_1.fm1p
    project_1.name
    fm1_scenes.dat
    project_assets/
    ... (up to eight complete FMP2 project slots)
  OFFICIAL/
    Bass/
    Drums/
    Keys/
    Pads/
  SAVED/
    LOOPS/
    RECORDINGS/
    IMPORTS/
  PACKS/
    Lofi Dust/
      ... complete original ZIP folder hierarchy ...
    Vaporwave/
      ... complete original ZIP folder hierarchy ...
```

All four top-level folders are visible **even while empty**. Existing
`FM1-SAMPLER` content is not cleared on an app update. Folders are created
on first launch (Android does not execute application code at install time).

## Exploración interna de archivos (0.38.4)

El icono **LIBRARY** de HOME abre la raíz **FM1-SAMPLER** dentro del propio
sampler: **no lanza** el explorador de archivos de Android.

El control físico **PRESETS** mueve la selección por carpetas y archivos
del directorio actual, exactamente un elemento por giro.
Un **WAV seleccionado se carga automáticamente como preset en la pista
activa**: no es necesario pulsar ENTER. El botón **ENTER** sirve
exclusivamente para entrar en carpetas, y **BACK** regresa un nivel.

La importación desde Descargas/USB/Drive continúa existiendo como acción
explícita **LOAD FILE FROM DISK** en EDIT. Un ZIP importado se descomprime
en FM1-SAMPLER/PACKS con sus carpetas y contenidos completos. El archivo
externo original permanece sin modificaciones.

En PROYECTO, **EXPORT ALL LOOPS** publica las pistas grabadas y su mezcla
maestra en `Downloads/FM1-SAMPLER/EXPORTED LOOPS/`, accesible desde Files.

## ZIP import

Use the explicit external **LOAD FILE FROM DISK** control in EDIT and select
any ZIP from Android's system document picker. The archive may come from Downloads, removable USB,
Google Drive, or another compatible document provider.

The app unpacks the complete archive into
`FM1-SAMPLER/PACKS/<original ZIP filename>/`. It preserves **every archive
entry**, subdirectory, non-audio asset, loop, and original audio file.
Compatible compressed audio files may gain an additional WAV copy, without
removing their originals. A pack name already in `PACKS` gets a safe
` (2)` suffix instead of overwriting content.

The *imported/extracted pack folder* moves logically into the `PACKS`
library. The source ZIP selected through the Android document picker
**remains in its original provider/location**: deleting a user file from a
third-party provider cannot be assumed or performed without that provider's
permission. No attempt is made to delete the source.

Unsafe ZIP paths, corrupt archives or packs exceeding the safety limits
fail without exposing a partially imported pack.

## Loop, recording and project destinations

- Complete named loop WAVs: `FM1-SAMPLER/SAVED/LOOPS/`
- FM-1 Sampler microphone WAVs: `FM1-SAMPLER/SAVED/RECORDINGS/`
- Individually imported WAVs: `FM1-SAMPLER/SAVED/IMPORTS/`
- Complete project archives: `FM1-SAMPLER/PROYECTOS/project_N.fm1p`
- Their human-readable names: `FM1-SAMPLER/PROYECTOS/project_N.name`
- Official instruments: `FM1-SAMPLER/OFFICIAL/`
- Each extracted imported pack: `FM1-SAMPLER/PACKS/<pack name>/`

FMP2 projects remain self-contained: eight tracks, sample audio, clips,
COMPOSE, twelve scenes, SONG order, effects, settings and transport.

## HOME navigator: PRESETS auto-load; ENTER folders; BACK parent (0.38.4)

HOME shows BACK and ENTER, followed by **one read-only file-type icon** and
the **name of the currently selected item**. The symbol identifies the root,
a directory, a playable WAV or another preserved file. There is **no
separate modal file browser or Android DocumentsUI** when navigating the
FM1-SAMPLER library.

- Rotate physical **PRESETS**: move **one file or folder** within the current
  directory. A selected WAV loads directly into the active sampler track.
- **ENTER**: enter a selected folder. ENTER on a WAV is intentionally a no-op:
  the selected WAV is already loaded.
- **BACK**: return one folder level, highlighting the folder just left.
- **LIBRARY** icon: return to the in-app FM1-SAMPLER root.

Non-audio files imported as part of ZIP packs remain visible without being
mistaken for playable WAV samples. External file imports remain available
from the separate EDIT action, not from directory navigation.

## HOME LIBRARY overlay restored (0.38.5)

The **LIBRARY** folder icon on the cassette HOME page opens the in-app
**FM1-SAMPLER** library panel again. That panel displays the root folders,
current path, file-type icons, file names, **ENTER**, **BACK**, and the
**+ IMPORT ZIP** button. Tapping **+ IMPORT ZIP** launches Android's document
picker *only for choosing an external ZIP*, then extracts the pack under
`FM1-SAMPLER/PACKS/`. It never launches the external picker simply to
navigate existing library folders.

The same cursor is used by physical **PRESETS** and taps in the library list.
Any WAV selected by knob or touch is immediately loaded into the current track,
with no ENTER required; ENTER is reserved for entering folders and BACK for
the parent directory. Closing LIBRARY returns to HOME, retaining navigation
state. No second independent folder list or selection state is used.

## EXPORT ALL LOOPS (0.38.2)

The **PROJECT** page has three separate actions: SAVE PROJECT,
LOAD PROJECT, and **EXPORT ALL LOOPS**. Export captures all eight current
recorded loop tracks (only populated tracks become WAVs) and creates a
16-bit stereo `TRACK_01.wav` ... `TRACK_08.wav` per recorded track,
plus a `MASTER.wav` sum of those clips. Recorded stems are exported at
their original amplitude; if the summed master clips, only that master
is peak-limited.

Android 10+ publishes these copies to the **public** location
`Downloads/FM1-SAMPLER/EXPORTED LOOPS/`, visible in the phone's Files
application or via USB. Exported filenames have unique timestamp prefixes
so re-exporting never replaces previous files. The source loops, saved
projects, private WAVs and backups are never removed or modified.
This public export does not make the main private library public.

Exports are asynchronous. The command may reject an active recording, a
session with no recorded loops, or a duplicate request. Check that a success
message appears and the WAV files are visible before relying on the export
as your sole backup.

## Updating from earlier preview builds

Migration runs before the engine opens its files:

- `files/samples/OFFICIAL` -> `FM1-SAMPLER/OFFICIAL`
- `files/samples/SAVED/{Loops,Recordings,Imports}` ->
  `FM1-SAMPLER/SAVED/{LOOPS,RECORDINGS,IMPORTS}`
- `files/samples/<other pack folder>` ->
  `FM1-SAMPLER/PACKS/<pack folder>`
- `files/project_*.fm1p`, sidecars, scenes, project assets ->
  `FM1-SAMPLER/PROYECTOS`
- `files/sample_*.wav` -> `FM1-SAMPLER/SAVED/RECORDINGS`

Existing target files are never overwritten; conflicting filenames gain
numbered suffixes. Source files remain available if migration fails.

**Android package separation:** the `.scenes` preview APK can be installed
beside a production APK. Because they have different Android package IDs,
its app-private `FM1-SAMPLER` cannot automatically read the production
app's private recordings and projects. This migration applies to data owned
by the **same package** being upgraded, not to another app.

**External file manager visibility:** these folders are app-private and
normally inaccessible from Android's regular file manager on recent Android
versions. Making a public `Documents/FM1-SAMPLER` folder and allowing
external edits requires a separate Storage Access Framework export/sync
implementation and user-granted directory access.
