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
| Turn K1 while holding the NOTE REPEAT combination | Choose 1/1, 1/2, 1/4, 1/8, 1/16, or 1/32 in real time |
| Release PLAY/STOP | End NOTE REPEAT; the original recorded loop is unchanged |
| Release OCT first, then press PLAY/STOP again | Normal play/stop; previous modifier cannot latch |

The OCT key's MIDI Note On may arrive long before the PLAY/STOP Note On.
The Android software remembers whether an OCT modifier is held. No single
simultaneous MIDI event or minimum hold duration is necessary.

## MIDI wire contract

- Physical OCT− press/release: channel 2, note 0, Note On/Off.
- Physical OCT+ press/release: channel 2, note 1, Note On/Off.
- Physical PLAY/STOP press/release: channel 2, note 12, Note On/Off.
- Physical K1: channel 1, relative MIDI CC 18.

The panel firmware forwards events over whichever of USB or BLE MIDI is ready.
An unmodified FM1 Sampler firmware that handles OCT locally **without sending
these events** cannot activate physical combinations in the Android app.
The APK's INFO panel can now show whether each message has actually arrived.
The INFO panel is diagnostic only; it does not emulate physical buttons.

## Source, validation and building the firmware

- Firmware implementation: `firmware/app/ctrl.c`.
- Physical button and USB/BLE validation:
  `firmware/hosttest/ctrl_host.c`.
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
