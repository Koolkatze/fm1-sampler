# FM1 Sampler 0.42.2 — Decimal Tempo (Android preview)

- MET → CORRECT TEMPO: K1 shows full BPM to two decimals and keeps its 0.1 BPM adjustment. The large TAP TEMPO button remains in K2/K3. K4 DECIMALS is on the right; drag it to set the fractional part (.00–.99), or turn the physical K4 encoder to increment/decrement 0.01 BPM, including integer boundaries.
- Correction remains non-destructive; no sample pitch, speed or clip duration changes. The internal clock uses fractional BPM. Existing FM-1 monitor MIDI protocol still transmits 0.1 BPM.
- SAVE PROJECT stores the existing float tempo correction; no project format change. Light/Dark and compact editor layouts supported.
- Android versionCode 68; package ID and permanent signing configuration unchanged. Save a project and backup before updating; do not uninstall.
- See [README](../README.md) and [0.42.1 notes](RELEASE_NOTES_0421.md).

