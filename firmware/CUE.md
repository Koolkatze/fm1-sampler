# FM-1 Sampler: loop restart and tempo-synchronised CUE

These controls apply to the FM-1 Sampler phone audio engine and its matching **FM-1 control-surface firmware**. They do not modify Urban-1.

| Physical controls | Action |
|---|---|
| OCT− (tap alone) | Shift keyboard down one octave (unchanged) |
| OCT+ (tap alone) | Shift keyboard up one octave (unchanged) |
| Hold OCT−, then press PLAY | Start playback of the loop at its beginning, even if playback was already running |
| Hold OCT+ for 2 seconds | Open the CUE division page; no octave shift |
| Hold OCT+, press and **hold** PLAY | Repeat the section from the current playhead at the selected division |
| While holding OCT+ and PLAY, turn K1 | Change the repeat rate live |
| Release PLAY | End CUE and return to the uninterrupted underlying loop transport |
| Release OCT+ | Close the division selector and restore the previous page |

## Android app (no FM-1 firmware change required)

The Android APK contains the native C++ sampler engine and CUE. Open the **CUE** page from the phone's editor or from the cassette deck's page notches, select a division with K1, then **touch and hold the CUE transport button** in the cassette deck to repeat. Release the touch to return to the main tape transport. This works entirely on the phone, with no physical FM-1 connected. The GitHub Actions debug APK is a separately installable application (`com.ferluht.fm1sampler.cue`) to avoid replacing an existing installed sampler.

The K1 divisions are **1/1**, **1/2**, **1/4**, **1/8**, **1/16**, and **1/32**. The default is **1/4** (one quarter note). Length is calculated from the session BPM in common time: 1/1 = four beats, 1/2 = two beats, 1/4 = one beat, etc. The repeated audio begins where the playhead was when PLAY was pressed; no global loop content is altered. The original transport keeps running in the background, so releasing CUE resumes at the position it would have reached without the effect (slip-style).

The repeat is non-destructive; it does not write repeated material into a clip. Audio transitions use short crossfades. K1 changes the rate without rearming PLAY.

### Compatibility and build

Both the **phone SamplerApp/Session engine** and the updated **firmware/app/ctrl.c** must be running for the modifier combinations to work on the physical device. The committed prebuilt `.fwsc` file is **not** rebuilt by this change. Do **not** flash the prebuilt firmware expecting these controls.

The host test in `engine/tools/test_app.cpp` covers restart, long-hold page access, all-speed selection, live changes, release, and octave preservation. The CI host test compiles this engine; a separate Windows JieLi SDK toolchain is needed to package the device firmware.
