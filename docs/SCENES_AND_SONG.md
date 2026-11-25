# FM1 Sampler — LOOP/SONG workspace (Android v0.41)

The Android app runs the audio engine and stores full projects. The physical M-VAVE FM-1 can control its transport and scene-key bank over MIDI, but the on-screen project and performance editor does not need a new physical firmware.

## Navigation

From **HOME**, use the last central **PROJECT** icon. The full-screen PROJECT menu has **SAVE PROJECT**, **LOAD PROJECT**, **EXPORT ALL LOOPS** and **LOOP / SONG MODE**, plus its existing loop editing/clearing controls.

Touch **LOOP / SONG MODE** for the dedicated full-screen performance workspace. The header has **PROJECT**, **COMPOSE**, **LOOP MODE**, **SONG MODE** and **HOME**. LOOP and SONG are *separate screen layouts* rather than small panels squeezed beside the project menu. All controls use the app's existing Light/Dark theme colors.

## LOOP: 12 pads, 16 looper tracks, eight complete sequencer scenes

1. Record or overdub loops in the 16 independent audio tracks.
2. Open **LOOP MODE** and tap one of the 12 scene pads **C1–B1** to select it for editing. A tap only selects; **LAUNCH** plays it.
3. Tap **T1–T16** to choose which recorded audio clips belong to this scene. Both rows of eight are visible at once.
4. Select **OFF** (no sequencer) or one of **SC 1–SC 8** using the nine dedicated buttons. Each SC button includes the chosen *complete* sequencer pattern (all its eight steps lanes) in the current pad; no separate sequencer lanes can be added individually to a pad. A pad may contain both looper clips and one sequencer scene, or a sequencer scene alone.
5. Tap **SAVE PAD**. For an existing pad, **REPLACE PAD** requires a second confirmation tap. **DELETE PAD** also requires confirmation.
6. Tap **LAUNCH** to play the pad. Existing tempo-quantized scene transitions, fades, loop playback, and track contents are unchanged.
7. **EXIT LOOP** returns to COMPOSE, restoring the unchanged original loop workspace.

Under the established MIDI mapping, virtual scene pads C1–B1 correspond to MIDI 36–47 in Ableton's convention, or 24–35 in scientific octave naming. The native FM-1 keyboard notes 53–64 also select these pads when LOOP mode is enabled.

## SONG: full-screen arrangement editor

Tap **SONG MODE** in the performance header. The same 12 pads are available, plus a large arrangement strip. Select a saved pad and tap **+ ADD PAD** to append it after the selected position, up to 64 steps (repetitions allowed). Tap a step to select it; use **REMOVE**, **MOVE LEFT** and **MOVE RIGHT** to edit order. The strip displays eight large steps at a time and follows the selected step. **PLAY SONG** plays scenes in order once; **STOP SONG** stops. **EXPORT SONG** renders a processed audio file when the arrangement is nonempty.

## Project isolation and startup behavior

From v0.41, **opening the APK fresh without a project load displays zero saved LOOP pads and an empty SONG arrangement**, even if an older global scene file exists. Loading project A restores only the scene bank and song saved with A. Loading project B restores only B's bank and song; a B without performance content clears A's old pads and order. Source samples and current app settings continue using their existing startup behavior.

To preserve performance content after restarting the app, use **PROJECT → SAVE PROJECT**. The numbered self-contained `.fm1p` slots store COMPOSE loops, all scenes, entire SONG order, sequencer patterns and sound assignments, samples and FX. Loading one restores all of that state.

Older versions wrote `fm1_scenes.dat` in the app-private data directory. **v0.41 no longer automatically writes or restores that global file**, but intentionally does **not delete** an existing copy. Before upgrading, open v0.40, save every important scene/song in a full project slot, then make a full backup. Unsaved legacy global scenes are not automatically imported by 0.41.

## Compatibility and testing

The package remains `com.ferluht.fm1sampler.scenes`, signed with the same pinned certificate, now versionCode **65**. Light and Dark themes use one common responsive grid. Host regressions cover direct SC selectors, project menu navigation, separate LOOP/SONG layouts, project changes with and without saved scenes, safe project loading and clean restart. These tests do not constitute physical FM-1 firmware validation.

Source: `engine/ui/SamplerUI.cpp`, `engine/app/SamplerApp.cpp`, `engine/session/Session.cpp`, `engine/tools/test_app.cpp`.

No images or screenshots are included.
