package com.ferluht.fm1sampler;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Regenerable, royalty-free starter one-shots for the independent step sequencer.
 * Never replaces an existing sample; the user can load any WAV from the library.
 */
final class DefaultSeqSamples {
    static final String[] NAMES = {
        "Kick", "Snare", "Rim", "Tom 1", "Tom 2",
        "Hihat Open", "Hihat Closed", "Crash"
    };
    private static final int SR = 24000;

    private DefaultSeqSamples() { }

    static void ensure(File directory) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IOException("Cannot create sequencer defaults");
        for (int t = 0; t < NAMES.length; ++t) {
            File destination = new File(directory, NAMES[t] + ".wav");
            if (!destination.exists()) write(destination, t);
        }
    }

    private static void le16(FileOutputStream f, int n) throws IOException {
        f.write(n & 255); f.write((n >>> 8) & 255);
    }
    private static void le32(FileOutputStream f, int n) throws IOException {
        le16(f, n & 65535); le16(f, (n >>> 16) & 65535);
    }
    private static void write(File file, int voice) throws IOException {
        final float[] seconds = {.65f, .35f, .17f, .42f, .53f, .62f, .13f, 1.35f};
        final int count = Math.round(seconds[voice] * SR);
        final int bytes = count * 2; // mono PCM 16; readWav expands it to stereo
        final File temporary = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(new byte[]{'R','I','F','F'});
            le32(out, 36 + bytes);
            out.write(new byte[]{'W','A','V','E','f','m','t',' '});
            le32(out, 16); le16(out, 1); le16(out, 1);
            le32(out, SR); le32(out, SR * 2); le16(out, 2); le16(out, 16);
            out.write(new byte[]{'d','a','t','a'}); le32(out, bytes);
            int rng = 0x159F12D + voice * 32771;
            double phase = 0.0;
            double oldNoise = 0.0;
            for (int i = 0; i < count; ++i) {
                final double time = i / (double)SR;
                rng = rng * 1664525 + 1013904223;
                final double noise = ((rng >>> 8) / 8388607.5) - 1.0;
                final double filteredNoise = noise - oldNoise * .92;
                oldNoise = noise;
                double sample;
                if (voice == 0) {
                    double hz = 46 + 105 * Math.exp(-time * 38);
                    phase += 2 * Math.PI * hz / SR;
                    sample = Math.sin(phase) * Math.exp(-time * 10)
                           + .20 * noise * Math.exp(-time * 120);
                } else if (voice == 1) {
                    phase += 2 * Math.PI * 182 / SR;
                    sample = (.32 * Math.sin(phase) + .82 * filteredNoise)
                             * Math.exp(-time * 19);
                } else if (voice == 2) {
                    phase += 2 * Math.PI * 1020 / SR;
                    sample = (.58 * Math.sin(phase) + .42 * filteredNoise)
                             * Math.exp(-time * 48);
                } else if (voice == 3 || voice == 4) {
                    phase += 2 * Math.PI * (voice == 3 ? 170 : 108)
                           * (1 + .75 * Math.exp(-time * 35)) / SR;
                    sample = Math.sin(phase) * Math.exp(-time * (voice == 3 ? 12 : 8));
                } else if (voice == 5 || voice == 6) {
                    sample = filteredNoise * Math.exp(-time * (voice == 5 ? 7 : 48));
                } else {
                    sample = filteredNoise * (.75 * Math.exp(-time * 3.7)
                           + .22 * Math.exp(-time * 23));
                }
                sample *= Math.min(1.0, time * 3500) * .72;
                short pcm = (short)Math.max(-32767, Math.min(32767,
                        Math.round(sample * 32767)));
                le16(out, pcm & 65535);
            }
            out.getFD().sync();
        }
        if (!temporary.renameTo(file)) {
            temporary.delete();
            throw new IOException("Could not finish " + file.getName());
        }
    }
}
