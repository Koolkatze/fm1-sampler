# FM1 Sampler 0.42.0 — live pad editing (Android preview)

- MET: PLAY/REC follows PLAY and STOP, including the FM-1 monitor sync message. REC-only modes click only during recording with the transport running. Correct Tempo has K1 fine BPM calibration and a large Tap Tempo touch target, independent of clip speed and pitch.
- PROJECT → LOOP / SONG MODE: select a saved pad, then COMPOSE. The pad stays audible. Record on a track with no clip anywhere in the current project; other occupied tracks are protected. REC/STOP commits the new clip automatically to that pad. BACK TO PADS keeps playback on the pad; HOME restores the main composition, including newly added clips with matching source timeline.
- SEQUENCER edits a private copy of the selected pad's pattern live. Steps, notes and time signature update the pad without replacing shared scenes. The eight sequencer sample assignments remain project-wide and cannot be changed in the private pad editor. SONG playback pauses its arrangement to repeat the chosen pad when entering the editor.
- SAVE PROJECT persists the entire project, including private pad patterns and tempo correction. Existing projects are readable; projects newly saved with v8 application metadata require the new app. SAVE BACKUP before upgrading; do not uninstall. Package identity and pinned certificate are unchanged; versionCode 66.
- Android and host regression tests must pass before a signed APK is published. Actual FM-1 physical monitoring still requires device testing.

See the [README](../README.md) for controls and limitations.
