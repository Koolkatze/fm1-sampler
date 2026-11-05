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

**The APK alone is insufficient for physical OCT commands.** The actual firmware installed on the FM-1 must transmit the following channel 2 note messages: OCT− Note 0 On/Off, OCT+ Note 1 On/Off, PLAY/STOP Note 12 On/Off. The updated `firmware/app/ctrl.c` transmits these independently over supported USB/BLE MIDI outputs. It deliberately retains the existing octave-change behaviour for a standalone tap while preventing an OCT modifier from changing the octave after a combo.

**Important:** The checked-in `.fwsc` file is a *previous prebuilt image*, **not** an artifact of these changes. Do not flash it expecting octave forwarding. Rebuild the exact source using the matching JieLi Windows SDK/toolchain; verify its firmware package and updater before flashing. Flashing will replace the currently installed firmware (including Blackhole, if present) and may affect its sounds/projects. Back up first.

The host test in `engine/tools/test_app.cpp` covers restart, long-hold page access, all-speed selection, live changes, release, and octave preservation. The CI host test compiles this engine; a separate Windows JieLi SDK toolchain is needed to package the device firmware.
