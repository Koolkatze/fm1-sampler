# FM1 Sampler 0.39 — Sequencer / Tempo Flex

This preview adds **eight independent step-sequencer tracks** alongside the existing eight looper tracks. The sequencer's individual lanes are not listed in LOOP. **LOOP Mode selects any combination of looper tracks plus optionally ONE entire sequencer scene (1–8).** The looper's tracks do not appear inside the SEQUENCER editor. They share **PLAY/STOP and the master project clock**; each uses its own sample assignment.

## Getting started

From **HOME**, tap **SEQUENCER**. The grid has eight lanes with these initial one-shot sounds:

| Lane | Initial sound |
|---|---|
| 1 | Kick |
| 2 | Snare |
| 3 | Rim |
| 4 | Tom 1 |
| 5 | Tom 2 |
| 6 | Hihat Open |
| 7 | Hihat Closed |
| 8 | Crash |

These are generated only if missing in `FM1-SAMPLER/OFFICIAL/SEQUENCER`; existing user WAVs are never replaced. Tap **LIBRARY** while SEQUENCER is open, then select a WAV to assign it **only to the selected sequencer lane**. The old looper sample remains unchanged. The existing library browser, sample packs and backup/import workflow are reused.

## Physical FM-1 controls

| Physical control | Sequencer action |
|---|---|
| **SEQ** | Enter/leave SEQUENCER |
| **HOME** | Return to HOME |
| **SELECT encoder** | Select one of the eight sequencer lanes |
| **First 8 black piano keys** | Directly select lane 1–8 |
| **16 white piano keys** | Toggle the corresponding step in the visible 16-step window |
| **K1** | Move the visible 16-step window along the timeline |
| **K2** | Choose the MIDI note of the most recently edited step (useful for bass) |
| **K3** | Set pattern length, 1/4 through 64/4 |
| **K4** | Select sequencer scene 1–8 (whole pattern) |
| **PLAY/STOP** | Shared transport for looper and sequencer |

Each 16-step window represents four quarter-note beats, with 1/16-note step resolution. **64/4** spans **256 steps** or sixteen windows. The selected track and active steps also appear on the FM-1 LCD, with the original light/dark colour theme.

The physical buttons must actually transmit the supported USB-MIDI messages. The Android APK does **not** install or flash the FM-1 firmware.

## Independent sequencer scenes and LOOP Mode

The step sequencer has **eight separate scenes, numbered 1–8**. K4 on the SEQUENCER page selects a scene, and the phone interface has previous/next scene controls. Each scene remembers the patterns for all eight sequencer lanes, their step velocities and notes, and its own 1/4–64/4 length. Only the currently selected scene plays in normal SEQUENCER mode; the seven others remain saved and silent. Switching scenes does not erase previously entered steps. The eight sample slots are shared across the sequencer scene bank; editing a pattern never replaces the original looper audio or its sample assignments.

In **PROJECT → LOOP MODE**, pick a normal looper scene pad (C1–B1), select any of the **eight original looper track buttons**, and choose **SEQ OFF** or **SEQ SCENE 1–8**. When you save or replace this LOOP pad, it remembers the chosen looper clips and a reference to **one whole sequencer scene**. LOOP does not offer the sequencer's eight individual tracks as selectable looper tracks. A pad may also contain only the selected sequencer scene, with no recorded looper tracks.

Launching a LOOP pad switches its looper clips and starts **only its chosen sequencer scene**. If **SEQ OFF** is saved, all sequencer scenes are silent for that pad. Other sequencer patterns remain intact. The original SONG order can use these LOOP pads as parts, including the chosen complete sequencer scene; SONG.wav export renders that selected sequencer audio along with the looper clips.

A saved sequencer scene is referenced by number in LOOP: editing that sequencer scene later changes what that LOOP pad plays. This is not a separate copy of its audio for each LOOP pad. The pattern bank and references are included in new complete project saves, while previous app-state v4, archive v2/v3 and standalone SCN1 scene banks remain readable.

## Global TEMPO / PITCH

Tap **TEMPO / PITCH** on HOME or SEQUENCER to use the four knobs:

| Control | Range | Intended action |
|---|---|---|
| **Tempo Flex** | 30–300 BPM | Change playback and sequencer clock together, preserving the pitch of previously recorded clips with worker-generated time-stretching |
| **Semitones** | −24…+24 | Change pitch without changing BPM |
| **Cents** | −100…+100 | Fine-tune pitch without changing BPM |
| **Pitched Tempo** | 0.25×…2× | Tape-style coupling: changing speed also changes pitch |

The effective BPM is shown on both HOME/LOOPER and SEQUENCER. Tempo Flex processing may temporarily show **FLEX...** on the sequencer while the new processed loop audio is prepared. The original recorded PCM remains in the project and can be recovered unchanged.

Tempo Flex targets the project-wide tempo; it does not permanently rewrite the source samples. Because time stretching has finite limits and can produce artefacts on unusual material, judge the sound of long clips and percussion transients on the actual phone before relying on it for a live performance.

## Saving, loading and updating

**SAVE PROJECT** stores all eight complete sequencer scene patterns, their step notes/velocities and lengths, the shared sequencer sample assignments and embedded source WAV data, and the LOOP scene-to-sequencer references alongside COMPOSE and SONG. Existing v2/v3/v4 app settings and project archives v2/v3 remain readable. This development branch uses app-state v5 and project archive v4.

The previously published 0.39 preview used Android versionCode **63**. The new scene-selection changes in this branch have not yet been published as an upgrade APK. A safe in-place upgrade **also requires the exact same signing certificate** as the APK already installed on the phone. GitHub's signed-release workflow blocks publication if the permanent keystore or pinned SHA-256 does not match. **Do not uninstall a previous APK** before making and verifying a complete backup of projects and samples, because an uninstall can remove app-private files.

## Limitations to validate on real hardware

- Latency, USB MIDI key and knob mapping, and key LED feedback on the physical FM-1
- Tempo Flex CPU load and time-stretching quality on long recordings
- Looper/sequencer phase accuracy after STOP/PLAY, scene changes and Song Mode transitions
- Whether exported SONG and per-track WAVs sound identical to live playback in demanding projects
- Upgrade/restore behavior on the specific Android device and currently installed signing identity

Scene-selection development review: https://github.com/Koolkatze/fm1-sampler/pull/6

Original 0.39 preview: https://github.com/Koolkatze/fm1-sampler/pull/5
