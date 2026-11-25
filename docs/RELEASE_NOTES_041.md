# FM1 Sampler 0.41.0 — Full-screen LOOP / SONG workspace (Android preview)

**Signed Android preview; FM-1 physical firmware remains unverified on hardware. No screenshots or generated illustrations.**

## New in 0.41

- **PROJECT menu** is now its own full-screen page with large SAVE PROJECT, LOAD PROJECT, EXPORT ALL LOOPS and **LOOP / SONG MODE** buttons.
- **LOOP and SONG each get their own full-screen layout**, instead of sharing the tiny secondary PROJECT column. A single row of tabs switches COMPOSE / LOOP MODE / SONG MODE; touch targets remain distinct, with no overlaps.
- **LOOP mode:** 12 large C1–B1 pads; 16 looper track selection buttons (T1–T16); and nine direct **OFF / SC 1–SC 8** buttons for attaching exactly one **entire sequencer scene** to the selected pad alongside any chosen looper tracks. Save/Replace, Launch, Exit and Delete retain confirmation and existing playback behavior.
- **SONG mode:** 12 pads, spacious song-order strip (8 visible steps with selection/scroll), ADD/REMOVE/MOVE controls, PLAY/STOP and EXPORT SONG.
- **Project-only performance content:** on a fresh APK launch, LOOP pads and SONG order start **empty**. They are no longer silently restored from or written to a global `fm1_scenes.dat` startup sidecar. **SAVE PROJECT** stores them in the chosen numbered `.fm1p` project. **LOAD PROJECT** restores that project's exact saved pads and arrangement; loading another project with no pads/SONG clears the prior project's performance data.
- **Existing sample and parameter handling is unchanged** when opening the APK fresh. Live current sequencer pattern edits are included when saving a full project even without switching sequencer scenes.
- Previous HOME improvements retained: BPM at the left of the tape illustration, five central icons, PROJECT aligned with PLAY/STOP/REC, SEQ icon, MET-only tempo controls and compact page selectors.
- 16 looper tracks + separate MET and 8 sequencer tracks/scenes. Light and Dark themes are supported throughout.

## Safe update and migration

1. **Before updating from 0.40, open the previous version and save every LOOP/SONG arrangement you want to keep with SAVE PROJECT.** Unsaved global-scene sets will not automatically reappear after restarting 0.41.
2. From LIBRARY also export **SAVE BACKUP** and verify that its file exists. Do not uninstall the older app.
3. Install the **same-signature** `FM1-Sampler-LoopSong-Fullscreen-0.41-signed-preview.apk` over the existing compatible `com.ferluht.fm1sampler.scenes` APK. Android versionCode increases from 64 to **65**. CI checks the pinned signing certificate and package identity.
4. Use LOAD PROJECT to reopen a saved arrangement. A clean startup intentionally shows zero LOOP pads and an empty SONG until the project is loaded.
5. Older `fm1_scenes.dat` files are **not deleted** by the upgrade, but they are no longer automatically imported. Keep your backup or use 0.40 to move important legacy scenes into a full project before installing 0.41.

**Only the Android APK is included.** No physical FM-1 firmware is added or certified by this release. Separate FM-1 click monitoring, end-to-end synchronization, and firmware flashing still require device testing.

- [Repository](https://github.com/Koolkatze/fm1-sampler)
- [Branch](https://github.com/Koolkatze/fm1-sampler/tree/feature/16-looper-metronome-monitor)
- [Upgrade/data-loss prevention](https://github.com/Koolkatze/fm1-sampler/blob/feature/16-looper-metronome-monitor/docs/UPDATES_WITHOUT_DATA_LOSS.md)
