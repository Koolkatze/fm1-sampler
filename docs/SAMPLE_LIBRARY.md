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

## FIX: ENTER after returning to FM1-SAMPLER root (0.38.9)

If the navigator is at `FM1-SAMPLER` itself because the user pressed
**BACK** at the root, the in-app cursor is intentionally unselected. In
older builds **ENTER** did nothing in this state, looking frozen.

**ENTER now reopens the last selected root folder** (for example
`PACKS`), or the first root folder if none has been visited. This works
identically from the HOME inline ENTER and the LIBRARY overlay ENTER and
needs no intervening turn of the physical PRESETS encoder. BACK still
returns to the parent and PRESETS still selects one item per detent;
choosing a WAV still immediately loads that file without ENTER.
Repeated BACK → ENTER navigation at root remains possible.

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

## HOME track badge position (0.38.7)

The selected-track badge **T1–T8** is now large, orange and placed
immediately **to the left of the SAMPLE waveform viewer**, rather than
being a small label next to the dark/light theme icon. It stays visible even
when no sample is loaded and still acts as a touch control: tapping cycles
to the next track. Physical SELECT encoder behavior is unchanged.

## SAVE BACKUP / IMPORT BACKUP (0.38.6)

The HOME folder icon opens the **LIBRARY** page containing three independent
actions on its bottom bar: **+ IMPORT ZIP**, **SAVE BACKUP**, and
**IMPORT BACKUP**.

- **SAVE BACKUP:** opens Android's **Create document** picker and saves
  `FM1-SAMPLER-BACKUP-<date>.zip` wherever the user chooses (Downloads,
  Documents, USB, cloud document provider, etc.). The backup contains the
  entire *saved* app-private `FM1-SAMPLER/` tree: all eight project slots and
  their FMP2 archives and asset files, scene/SONG files, completed loop WAVs,
  microphone recordings, imported WAVs, OFFICIAL instruments, full PACKS,
  non-audio files and directory hierarchy.
- **IMPORT BACKUP:** opens Android's **Open document** picker. It only
  accepts a versioned FM1-SAMPLER backup archive. The app checks every ZIP
  entry against a manifest of file sizes and SHA-256 hashes, rejects
  path-traversal, duplicate entries, missing entries or corrupt data and
  validates the whole archive **before** making changes.
- **KEEP EXISTING (default):** restores only missing files; existing project
  slots, saved loops and imports are never overwritten.
- **REPLACE (explicit confirmation):** restores files from the backup whose
  names collide, first moving previous originals into an independent
  `files/FM1-SAMPLER-RECOVERY/<timestamp>/` directory outside the live
  library. The prior files are retained for recovery, not silently deleted.
- Both operations stream the bytes and execute off the UI thread. A status
  dialog reports how many files were saved, imported or protected. The user
  should restart the app after import to load the restored project files.
- Recording and in-progress project save/load operations block backup
  creation/import to avoid snapshotting partially written projects.

**Important:** the ZIP includes **files already saved**, not notes/loops
currently present only in RAM. **SAVE PROJECT first** (and finish any
recording) to capture the current COMPOSE, SONG, scenes, live loop clips,
samples and settings into a self-contained FMP2 project before saving the
library ZIP. The exported ZIP is user-owned and survives uninstalling the
APK when saved outside the app; this solves migrating between different
Android debug APK signing keys or package IDs (export from the old APK,
import into the new one).

Imported and exported ZIP documents are not deleted by the app. An ordinary
sample-pack ZIP selected through **+ IMPORT ZIP** is different from a full
backup; use **IMPORT BACKUP** only for ZIPs made with **SAVE BACKUP**.

## EXPORT ALL LOOPS: processed audio (0.38.11)

The **PROJECT** page has three separate actions: SAVE PROJECT,
LOAD PROJECT, and **EXPORT ALL LOOPS**. Export captures the current recorded
loop tracks (only populated tracks become WAVs) and creates 16-bit stereo
`TRACK_01.wav` ... `TRACK_08.wav`, plus `MASTER.wav`.

**As of 0.38.11, all exported audio is processed ("wet") instead of raw:**

- Each track follows the real looper playback DSP: current **TAPE SPEED**
  (varispeed and interpolation), cassette **EXCITER / saturation, LO-FI,
  WOBBLE, hiss and tone**, live **LFO volume/pan** modulation, track
  **volume/pan**, keyed **sidechain ducking**, and its configured reverb/delay
  **send/return** levels.
- Each stem is rendered in solo with other tracks still running silently
  as sidechain key sources. The result contains that track's own reverberation
  and echo as part of the WAV; the original recorded WAV buffer is unchanged.
- **MASTER.wav** runs all recorded tracks through the shared reverb/delay
  buses and final **master gain + tanh soft limiter**, not a sum of raw clips.
  Due to the nonlinear master limiter and tape-delay feedback, adding the
  individually rendered wet stems in a DAW need not equal MASTER bit-for-bit.
- The exporter primes the reverb/delay state with the end of the loop
  before writing so FX tails from the previous pass are present without
  adding unaligned extra seconds at the end of a loop. All WAVs retain the
  same duration after the active tape-speed conversion. Stems represent a
  cycle of the **currently active clips**, not an automatic render of
  every scene in a full SONG arrangement.
- Sample-engine synthesis, arpeggiation, pitch envelopes and other effects
  already recorded into each clip are preserved within its existing audio;
  they are **not applied again** during export. Unrecorded live key presses
  are not part of EXPORT ALL LOOPS.

The exporter snapshots shared audio-buffer references and current FX
settings at an audio block boundary and performs DSP and streaming WAV writes
on its background worker (no multi-hundred-MB PCM export vector and no
mutation of live clips). It still publishes files via Android MediaStore.

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
