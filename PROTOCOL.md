# FM1-DAW protocol v1 (USB MIDI)

## FM-1 -> phone (plain MIDI)
| Event | Message |
|---|---|
| Note keys (27) | ch1 Note On/Off, the FINAL note: the firmware applies scale, root, octave and chord mode (see below) |
| Panel buttons | ch2 Note On/Off, note = button index (0 OCT-, 1 OCT+, 2 FX, 3 SCL, 4 ENV, 5 LFO, 6 EDIT, 7 GLO, 8 HOME, 9 SAVE, 10 ARP, 11 SEQ, 12 PLAY, 13 REC). OCT-/OCT+ and SCL are NOT forwarded: the firmware handles them |
| Encoders 0..6 | ch1 CC 16+i, relative: 64 + delta (-63..+63). Not forwarded while SCL is held |
| Knob (10-bit) | ch1 CC 7 (MSB = adc>>3) then CC 39 ((adc&7)<<4) |
| Battery | ch1 CC 24, adc>>3, every 2 s |
Velocity is fixed (100; chords 85). Failed sends (queue full) are retried; state only advances on success.

## Scales (firmware)
Tables CHROM MIX MAJ MIN HMIN PHRYG MIN+ plus OFF (plain chromatic keyboard from F3). Key index k (0..26 from the left) -> degree k of the
scale: note = 60 + root + 12*octave + 12*(k / n) + step[k % n]. KEY mode sends one note, CHORD mode the triad k, k+2, k+4.
Default MIN, root C, octave 0, KEY. Hold SCL: the firmware shows its own scale screen (K1 scale, K2 root, K3 octave, K4 mode) and ignores phone
drawing until SCL is released, then sends EV_REDRAW. OCT -/+ shift the octave (-3..+3).

## FM-1 -> phone SysEx `F0 7D 'D' 'A' 'W' 01 <ev> ... F7`
| ev | payload |
|---|---|
| 40 PONG | id(2) millis(5) rx_frames(5) rx_bytes(5) rx_dropped(5) |
| 41 SCALE | scale root octave+3 mode (also repeated every 2 s) |
| 42 REDRAW | (none) the phone must repaint the whole screen |

## Phone -> FM-1 SysEx `F0 7D 'D' 'A' 'W' 01 <cmd> <payload> F7`
All values 7-bit. n14 = 2 bytes (hi,lo). color = 3 bytes, RGB565 split 2/7/7 bits. Frames <= 300 B, queue depth 4.
| cmd | payload |
|---|---|
| 01 LED | 6 bytes, 7 bits each, little-endian bitmask of 41 LEDs (0..13 buttons, 14..40 keys) |
| 02 CLEAR | color |
| 03 RECT | x n14, y n14, w n14, h n14, color |
| 04 TEXT | x n14, y n14, scale (1..4), fg color, bg color, ASCII text (opaque cell 6*scale x 8*scale) |
| 05 PING | id n14 -> PONG |
| 06 SCALE | scale root octave+3 mode |
| 07 METRONOME | bpm×10 n14, numerator (1..64), swing percentage (0..100), volume percentage (0..100), mode (0..5), beat index (0..N−1), fractional beat n14 (0..16383), recording boolean (0/1) |
| 08 PALETTE | 16 x color: the palette of BLIT |
| 09 BLIT | x n14, y n14, w n14, h n14 (w*h <= 768), tokens: colour(4)<<3 \| (run-1); run field 7 = next byte adds 8.. |
| 0A BATCH | repeated { length n14, cmd, payload }: many small commands in one frame (the FM-1 queue counts frames, not bytes) |
| 7E NOP | ignored, counted (throughput test) |

## Audio
The looper uses **16 audio tracks**, plus eight independent sequencer sample lanes, plus one **control-only metronome track** (no looper clip or sample source).

Metronome mode values (K4, second click page): 0 = PLAY/REC (click to phone main mix), 1 = REC (click to phone mix only while recording/overdubbing), 2 = OFF, 3 = PLAY/REC MON, 4 = REC MON, 5 = OFF MON.

In MON modes the Android main mix is requested on the phone's built-in audio output; FM-1 firmware mutes all other signals going to its DAC and synthesizes the click exclusively onto the FM-1 physical 3.5 mm headphone output. The click is inserted **after** `fm1_usb_audio_write` and never enters FM-1 USB capture. In non-MON modes legacy phone-to-FM-1 USB audio routing is preserved. The firmware and phone both use 44.1 kHz; the FM-1 receives periodic beat phase updates from the phone audio clock and slew-corrects limited drift. **Without real-device loopback measurements, USB transfer latency and the two independent DAC clocks prevent claiming demonstrated sample-accurate inter-device alignment.** Do not publish a flashable package as verified until firmware packaging and a physical sync / no-leak test pass.
