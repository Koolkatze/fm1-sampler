# FM1 Sampler 0.42.7

- Increased polyphony per sampler: Melodic, One Shot, Looped 64; Kit 32; Granular 16; Paulstretch 8.
- Original allocation policy retained: first free voice, otherwise oldest voice. No prioritization by envelope, amplitude or gate. ADSR and playback behavior unchanged.
- Only eight PaulStream instances allocated per sampler. Rendering limits are mode-specific. Physical-device performance at full polyphony remains to be tested.
- Permanent signing identity and project format unchanged; versionCode 73.
