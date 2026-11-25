# FM1 Sampler 0.40.0 — 16 looper tracks + private FM-1 metronome

**Android signed preview / hardware validation pending.**

### New in 0.40

- **HOME UI adjustment:** SEQUENCER opens from a compact step-grid icon in the central tools column. The five tool icons are evenly spaced so the bottom PROJECT icon aligns with PLAY/STOP/REC, without overlapping the bottom sample/timeline viewer. BPM now appears at the left of the HOME tape illustration instead of beside the sample knobs. The permanently visible TEMPO/PITCH button has been removed; MET uses the same two miniature page selectors as the other tracks.
- **16 independent looper tracks** (T1–T16) in COMPOSE and scenes, with a **dedicated 17th metronome control slot (MET)**. MET does not occupy an audio track.
- Existing **8 independent sequencer lanes** and **8 sequencer scenes** remain available alongside all 16 looper tracks.
- Two MET knob pages, switched by **SEL** on FM-1 or the Android screen:
  - Page 1: K1 **Tempo Flex**, K2 **Pitch Bend in semitones**, K3 **Pitch Bend in cents**, K4 **Pitched Tempo**.
  - Page 2: K1 **time signature 1/4–64/4**, K2 **Swing 0–100%**, K3 **click volume**, K4 **click mode**.
- Six click modes: **PLAY/REC**, **REC**, **OFF**, **PLAY/REC MON**, **REC MON**, **OFF MON**.
- In **MON** modes, Android's main mix targets the built-in phone speaker; a compatible *separate FM-1 firmware* sends the click only to the hardware 3.5 mm output. The click is not mixed into USB audio capture or recorded/exported looper WAVs.
- 16-track scenes and project persistence: **FMP2 v5**, **SCN3**, app settings **v6**; older projects remain readable. Original **Light/Dark** themes preserved.

### Installation and data safety

1. In your current app, **SAVE PROJECT**, then **SAVE BACKUP** from LIBRARY and check the exported backup exists.
2. Download `FM1-Sampler-16Track-Metronome-0.40-signed-test.apk` from this release.
3. Install **over the existing compatible signed Scenes installation; never uninstall first**. Android package is `com.ferluht.fm1sampler.scenes`, `versionCode 64`. CI verified the permanent pinned signing certificate, package name and incremented version.
4. Verify your older project, LOOP/SONG scenes, newly available T9–T16, and MET pages on your actual phone before relying on this version for performance.

**Do not install over an APK signed with a different certificate.** If that applies, refer to [the migration guide](https://github.com/Koolkatze/fm1-sampler/blob/feature/16-looper-metronome-monitor/docs/UPDATES_WITHOUT_DATA_LOSS.md).

### Firmware warning

**This release attaches the Android APK only.** The compatible physical FM-1 firmware and Workbench have been compiled as [unverified test artifacts](https://github.com/Koolkatze/fm1-sampler/actions/runs/45298048937), but have **not** been validated with a physical FM-1. Two independent playback clocks remain and physical end-to-end latency and drift have not been measured. Therefore do **not** assume the headphone metronome is sample-accurate, or flash the firmware for a live performance.

### Verification and documentation

- [Passing signed Android build, engine and MIDI tests](https://github.com/Koolkatze/fm1-sampler/actions/runs/45376916551)
- [Full 0.40 controls and modes](https://github.com/Koolkatze/fm1-sampler/blob/feature/16-looper-metronome-monitor/docs/FM1_16TRACK_METRONOME_040.md)
- [Technical status / remaining physical tests](https://github.com/Koolkatze/fm1-sampler/blob/feature/16-looper-metronome-monitor/docs/STATUS_040.md)
- [Draft implementation PR #7](https://github.com/Koolkatze/fm1-sampler/pull/7)

No screenshots or generated illustrations are included.
