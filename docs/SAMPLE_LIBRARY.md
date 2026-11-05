# Sample packs, navigation, and safe saves

This feature branch is based on `feature/scene-launcher-song-mode`.
It preserves the FM-1 USB MIDI firmware behavior, CUE shortcuts, 12 scenes,
COMPOSE, SONG mode and the existing self-contained FMP2 project snapshots.

## Library folders

The app initializes the following paths in app-private storage:

```
files/samples/
  OFFICIAL/
    Bass/
    Drums/
    Keys/
    Pads/
  SAVED/
    Loops/
    Recordings/
    Imports/
  Your Imported Pack/
    ... all folders and files from its ZIP ...
```

- Legacy built-in WAVs are migrated into `OFFICIAL` without deleting them.
- Previously imported `sample_*.wav` files remain indexed as
  `SAVED/Recordings` even if they are still physically in the legacy data directory.
- New microphone recordings save to `SAVED/Recordings`.
- Saved loops, with a name selected in the Android dialog, go to
  `SAVED/Loops`. The exact loop audio is preserved (no trimming).
- Imported individual files go to `SAVED/Imports`. Each filename is unique.
- An imported ZIP becomes a new pack. All its subfolders and all entries are
  retained. An entry that is not a playable WAV still appears in the browser
  and remains on disk. Unsupported formats are never silently dropped.
- Identical ZIP entry names are automatically disambiguated with ` (2)`,
  ` (3)`, etc. before extraction. Existing packs are not overwritten.

## Touch controls

Open the library by tapping the current sample name.

- The archive glyph identifies the top level of packs.
- A folder glyph identifies a directory; a waveform glyph identifies a WAV.
- Tap a row to select it. **ENTER >** enters the selected folder at any
  depth, or loads a selected WAV into the current track.
- **< BACK** returns to the immediate parent directory. At the archive
  root it closes the browser.
- `+ IMPORT ZIP` opens Android's document picker. Importing a pack refreshes
  the visible browser immediately without restarting the APK.

The UI continues to use the FM1 Sampler paper/ink/orange palette, rectangular
line drawings, and the original icon primitives.

## Audio formats

- Playable library entries are PCM WAV.
- WAV, AIFF / AIFC PCM, and common Android-decoded audio formats can be imported.
  Compatible AIFF, FLAC, MP3, OGG, AAC etc. receive a new playable 16-bit WAV;
  the source file is retained inside ZIP packs.
- Longer or unsupported audio files remain present in the imported pack even
  when the sampler cannot play them. No duration, name or musical-phrase filter
  runs on a user-selected ZIP.
- ZIP safety validation may reject an unsafe or corrupt archive atomically:
  a rejected archive is not presented as a successfully imported pack.

## Saving loops and projects

- The deck or editor `SAVE` action and the PROJECT menu `SAVE LOOP` action
  request the loop name through Android's keyboard.
- `SAVE PROJECT` opens the eight FMP2 slots; select one and enter a name.
  The original self-contained FMP2 project archive still stores **all eight
  samples, recorded clips, COMPOSE, scenes, SONG, settings, and transport**.
- Custom project names are stored in sidecar `project_N.name` metadata and
  displayed on the project slot buttons. The FMP2 binary format itself is
  untouched; existing archived projects remain readable.
- A project label is committed only when the background save reports success.

## Private storage and updates

Use `+ IMPORT ZIP` from within the app instead of manually editing Android's
private data directory. Installing the new debug APK over a compatible prior
package should preserve private app data, but back up irreplaceable projects
before installing a preview build.
