# FM1 Sampler: physical RESTART LOOP and NOTE REPEAT

This page describes the **FM1 Sampler** control-surface firmware at
`firmware/app/ctrl.c`, together with the matching FM1 Sampler Android APK.
It does not describe another firmware.

## Physical FM-1 controls

| Action | Behaviour |
| --- | --- |
| Tap OCT− by itself | Shift keyboard down one octave |
| Tap OCT+ by itself | Shift keyboard up one octave |
| **Hold OCT− first, then press PLAY/STOP** | **RESTART LOOP**, immediately on PLAY/STOP press |
| **Hold OCT+ first, then press and hold PLAY/STOP** | **NOTE REPEAT** from the current position at BPM-synced division |
| **Hold OCT+ and OCT− together** (either press order) | Immediately display the NOTE REPEAT subdivisions menu; no loop playback starts |
| Turn K1 on the rate page | Select 1/1, 1/1T, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, or 1/32T |
| Turn K1 while holding the NOTE REPEAT combination | Change the selected straight or triplet rate live |
| Release PLAY/STOP | End NOTE REPEAT; the original recorded loop is unchanged |
| Release OCT first, then press PLAY/STOP again | Normal play/stop; previous modifier cannot latch |

The firmware marks **both OCT buttons as consumed** after the two-key
menu gesture so releasing them does not shift the keyboard by accident.
Straight durations are 4/4-bar time divided by 1, 2, 4, 8, 16 or 32.
Each **T** variant is exactly **2/3** of the corresponding straight
duration at the current BPM, including 1/1T and 1/32T. Existing saved
projects (settings v2) retain their original straight division values.

The OCT key's MIDI Note On may arrive long before the PLAY/STOP Note On.
The Android software remembers whether an OCT modifier is held. No single
simultaneous MIDI event or minimum hold duration is necessary.

## MIDI wire contract

- Physical OCT− press/release: channel 2, note 0, Note On/Off.
- Physical OCT+ press/release: channel 2, note 1, Note On/Off.
- Physical PLAY/STOP press/release: channel 2, note 12, Note On/Off.
- Physical K1: channel 1, relative MIDI CC 18.

The FM1 Sampler firmware forwards these panel events exclusively over **USB MIDI**. It does not send these events over Bluetooth.
An unmodified FM1 Sampler firmware that handles OCT locally **without sending
these events** cannot activate physical combinations in the Android app.
The APK's INFO panel can now show whether each message has actually arrived.
The INFO panel is diagnostic only; it does not emulate physical buttons.

## Source, validation and building the firmware

- Firmware implementation: `firmware/app/ctrl.c`.
- Physical button and USB-only validation:
  `firmware/hosttest/ctrl_host.c` (now includes both OCT key orders).
- CI test: `.github/workflows/fm1-panel-midi-tests.yml`.

The test compiles and simulates the panel logic on Linux. Passing these
tests does **not** create a flashable firmware image or prove operation on
the physical device.

To build for FM-1 hardware, the firmware project requires the matching
JieLi Windows PI32v2 toolchain 2.5.3 and matching SDK as documented in
`firmware/README.md`. The supported firmware build command is:

```powershell
$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
.\sdk\Build.ps1 -FirmwareOnly -Toolchain $env:FM1_PI32_TOOLCHAIN
```

**Do not flash a previously committed prebuilt `.fwsc` expecting these
changes**: the source has been modified but the bundled firmware image
has not been regenerated. Before flashing a newly built firmware, verify
that it matches the FM-1 board and installer/updater and back up device
data. The matching FM1 Sampler image must be tested on hardware.
