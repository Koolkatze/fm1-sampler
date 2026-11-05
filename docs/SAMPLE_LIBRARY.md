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

## ZIP import

Tap `+ IMPORT ZIP` in the library and select any ZIP from Android's
system document picker. The archive may come from Downloads, removable USB,
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

## HOME navigator: read-only icons and BACK/ENTER (0.38.2)

In the top-left of HOME, the **BACK** and **ENTER** buttons are the only
navigation controls. Immediately to their right, ONE non-interactive symbol
indicates the selected item: archive drawers for the `FM1-SAMPLER` root,
a folder icon for a directory, or a waveform for a sample/audio file. Its
name is displayed directly to the right. Tap the name to open the full list.
The icons DO NOT act as filters or buttons.

Inside the full library, BACK and ENTER are in a narrow left column;
each entry has its own non-interactive folder/waveform icon immediately before
its name. The breadcrumb shows the current location. BACK goes to the parent
folder and ENTER opens the selected subfolder, or loads a playable WAV.
The imported ZIP's non-audio files are preserved but cannot be loaded as sound.

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
