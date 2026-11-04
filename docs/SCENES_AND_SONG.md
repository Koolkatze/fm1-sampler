# FM-1 Sampler — Scenes (Loop Mode) & Song Mode

The **Android phone app** plays and stores the audio. The physical FM-1 only sends MIDI buttons and notes. No new `.fwsc` is required to use the on-screen controls.

## In PROJECT

Open the three-line **PROJECT** button on the cassette deck. The original project operations are retained in the **left column**. The **right column** contains **LOOP MODE** and **SONG MODE**. Both use the app's existing light and dark palettes. On narrow screens, tap the column-switch button; controls never cover one another.

### LOOP MODE: 12 MIDI scenes

1. Create or overdub the audio loops for the tracks you want to use. All tracks share the loop clock, as in the existing app.
2. Choose a scene pad **C1, C#1, D1, D#1, E1, F1, F#1, G1, G#1, A1, A#1 or B1**.
3. Toggle tracks **1–8** to decide which currently recorded loops to include.
4. Tap **SAVE SCENE**. For an existing scene, confirm **REPLACE** with a second tap.
5. Select another pad and build another combination. One scene can contain kick+bass, another kick+bass+melody, another only percussion, etc.
6. Tap a saved pad, or press its key, to launch its loop set. The app queues the change for the **next whole bar**, with a short audio fade at the transition. Each scene loops until you launch another scene or stop.

The standard MIDI note numbers for C1..B1 are **24..35**. When LOOP MODE is enabled, the first 12 keys of the FM-1's native 53-based keyboard are interpreted as virtual C1..B1 (physical MIDI notes 53..64). This avoids needing a firmware update to retune the MIDI keys. Outside LOOP MODE the FM-1 keyboard behaves normally.

All the clip audio and settings remain on the phone. A scene captures the **recorded loop audio**, not a new instrument preset or its live unrecorded notes. Scenes retain up to eight loop tracks. Capturing a saved scene and subsequently editing a track uses copy-on-write: previously saved scenes are not changed until explicitly replaced.

### SONG MODE: ordered scenes

1. Choose **SONG MODE**.
2. Select a saved scene pad and tap **+ ADD**. Repeat as needed: for example **C1 → D1 → C1 → E1**. Repeats are allowed, up to 64 slots.
3. Select a numbered sequence step to edit it. Use **REMOVE**, **MOVE <** or **MOVE >**.
4. Tap **PLAY SONG**. It begins at the start of the first scene; each scene plays its complete loop duration and then the next scene starts in sync. The song **stops after the last scene**, rather than looping endlessly.
5. Use **STOP** or the physical PLAY transport to stop/restart in SONG MODE.

The project's scene packs and song order are written to `fm1_scenes.dat` in the app's private data directory. Scene saves and song edits persist between app launches, and the file is replaced atomically. The file stores floating-point audio at the app's sample rate; very long scenes consume more storage. Clearing live loops in PROJECT **does not delete saved scene pads**. Deleting a scene requires confirmation and removes its steps from the song order.

## Compatibility

This feature builds on the existing FM1 Sampler CUE branch. It keeps CUE, tape FX, sample library, recorder and editor unchanged. The debug Android application is packaged separately as **FM1 Sampler Scenes** (`com.ferluht.fm1sampler.scenes`) so it does not overwrite an existing FM1 Sampler or FM1 Sampler CUE app; each app has separate private projects.

Source: `engine/session/Session.cpp` (snapshot, tempo-quantized transitions, song transport, persistence), `engine/app/SamplerApp.cpp` (MIDI/key bindings and editing), `engine/ui/SamplerUI.cpp` (dual-column responsive sheet). Android JNI/CMake builds those same sources into the APK. Host tests cover scene switching, song order, project persistence and light/dark UI rendering.
