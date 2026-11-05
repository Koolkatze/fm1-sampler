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

## Navigation

Opening the library **always starts at `FM1-SAMPLER /`**. It shows
`OFFICIAL`, `PACKS`, `PROYECTOS` and `SAVED`.

Tap a row to select it; `ENTER >` opens the selected folder, at any depth,
or loads a selected WAV. `< BACK` exits one directory level. From the root,
`< BACK` closes the browser. ZIP contents (including non-playable files)
remain visible and selectable but only playable WAVs may be loaded.

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
