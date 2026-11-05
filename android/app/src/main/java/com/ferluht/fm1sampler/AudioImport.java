package com.ferluht.fm1sampler;

import android.content.ContentResolver;
import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import android.provider.OpenableColumns;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** A file picked from the disk -> a 16-bit PCM WAV in the app's data dir (sample_*.wav = the library). WAV is converted in place of decoding; mp3, aac, ogg, flac... go through MediaCodec. */
final class AudioImport {
    private static final int MAX_SECONDS = 180;
    private AudioImport() { }

    static String displayName(Context c, Uri uri) {
        try (android.database.Cursor q = c.getContentResolver().query(uri, null, null, null, null)) {
            if (q != null && q.moveToFirst()) { int i = q.getColumnIndex(OpenableColumns.DISPLAY_NAME); if (i >= 0) return q.getString(i); }
        } catch (Exception ignored) { }
        return "file".equals(uri.getScheme()) && uri.getLastPathSegment() != null
            ? new File(uri.getPath()).getName() : "import";
    }

    /** Returns the written file. */
    static File importTo(Context c, Uri uri, File dir) throws IOException {
        String base = displayName(c, uri);
        int dot = base.lastIndexOf('.'); if (dot > 0) base = base.substring(0, dot);
        base = base.replaceAll("[^A-Za-z0-9 _-]", "").trim(); if (base.isEmpty()) base = "import";
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create import folder");
        File out = new File(dir, base + ".wav");
        for (int i = 2; out.exists(); i++) out = new File(dir, base + " (" + i + ").wav");
        Pcm p = null;
        try (InputStream in = c.getContentResolver().openInputStream(uri)) { if (in != null) p = readWav(in); }
        if (p == null) p = decode(c, uri);
        if (p == null || p.data.length == 0) throw new IOException("no audio in the file");
        writeWav(out, p);
        return out;
    }

    static final class Pcm { short[] data; int channels, rate; }

    private static int le32(byte[] b, int o) { return (b[o] & 255) | (b[o + 1] & 255) << 8 | (b[o + 2] & 255) << 16 | (b[o + 3] & 255) << 24; }
    private static int le16(byte[] b, int o) { return (b[o] & 255) | (b[o + 1] & 255) << 8; }

    /** PCM 8/16/24/32-bit and float WAV; null when the file is not a WAV. */
    static Pcm readWav(InputStream in) throws IOException {
        byte[] all = readAll(in, 64 << 20);
        if (all.length < 44 || all[0] != 'R' || all[1] != 'I' || all[2] != 'F' || all[3] != 'F' || all[8] != 'W' || all[9] != 'A') return null;
        int fmt = 1, ch = 2, rate = 44100, bits = 16, pos = 12, dataOff = -1, dataLen = 0;
        while (pos + 8 <= all.length) {
            int len = le32(all, pos + 4);
            String id = new String(all, pos, 4, "US-ASCII");
            if (id.equals("fmt ")) { fmt = le16(all, pos + 8); ch = le16(all, pos + 10); rate = le32(all, pos + 12); bits = le16(all, pos + 22); if (fmt == 0xfffe && len >= 26) fmt = le16(all, pos + 32); }
            else if (id.equals("data")) { dataOff = pos + 8; dataLen = len < 0 || dataOff + len > all.length ? all.length - dataOff : len; break; }
            pos += 8 + len + (len & 1);
        }
        if (dataOff < 0 || ch < 1 || ch > 2 || !(fmt == 1 || fmt == 3)) return null;   // other layouts: let MediaCodec try
        int bytes = bits / 8, frames = dataLen / (bytes * ch);
        if (frames > MAX_SECONDS * rate) throw new IOException("Audio too long for sampler");
        short[] s = new short[frames * ch];
        for (int i = 0; i < s.length; i++) {
            int o = dataOff + i * bytes; float v;
            if (fmt == 3 && bits == 32) v = Float.intBitsToFloat(le32(all, o));
            else if (bits == 16) v = (short) le16(all, o) / 32768f;
            else if (bits == 8) v = ((all[o] & 255) - 128) / 128f;
            else if (bits == 24) v = ((all[o + 2] << 16) | ((all[o + 1] & 255) << 8) | (all[o] & 255)) / 8388608f;
            else if (bits == 32) v = le32(all, o) / 2147483648f;
            else return null;
            s[i] = (short) Math.max(-32768, Math.min(32767, Math.round(v * 32767f)));
        }
        Pcm p = new Pcm(); p.data = s; p.channels = ch; p.rate = rate; return p;
    }

    private static byte[] readAll(InputStream in, int max) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream(1 << 20);
        byte[] b = new byte[65536]; int n, total = 0;
        while ((n = in.read(b)) > 0 && total < max) { o.write(b, 0, n); total += n; }
        return o.toByteArray();
    }

    /** Any other format the phone can decode. */
    static Pcm decode(Context c, Uri uri) throws IOException {
        MediaExtractor ex = new MediaExtractor();
        MediaCodec codec = null;
        try {
            ex.setDataSource(c, uri, null);
            int track = -1; MediaFormat f = null;
            for (int i = 0; i < ex.getTrackCount() && track < 0; i++) { MediaFormat t = ex.getTrackFormat(i); String m = t.getString(MediaFormat.KEY_MIME); if (m != null && m.startsWith("audio/")) { track = i; f = t; } }
            if (track < 0) return null;
            ex.selectTrack(track);
            codec = MediaCodec.createDecoderByType(f.getString(MediaFormat.KEY_MIME));
            codec.configure(f, null, null, 0); codec.start();
            int ch = f.containsKey(MediaFormat.KEY_CHANNEL_COUNT) ? f.getInteger(MediaFormat.KEY_CHANNEL_COUNT) : 2;
            int rate = f.containsKey(MediaFormat.KEY_SAMPLE_RATE) ? f.getInteger(MediaFormat.KEY_SAMPLE_RATE) : 44100;
            ByteArrayOutputStream pcm = new ByteArrayOutputStream(1 << 20);
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inDone = false, outDone = false;
            long limit = (long) MAX_SECONDS * rate * ch * 2;
            while (!outDone && pcm.size() < limit) {
                if (!inDone) {
                    int ii = codec.dequeueInputBuffer(10000);
                    if (ii >= 0) {
                        ByteBuffer b = codec.getInputBuffer(ii);
                        int n = ex.readSampleData(b, 0);
                        if (n < 0) { codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                        else { codec.queueInputBuffer(ii, 0, n, ex.getSampleTime(), 0); ex.advance(); }
                    }
                }
                int oi = codec.dequeueOutputBuffer(info, 10000);
                if (oi >= 0) {
                    ByteBuffer b = codec.getOutputBuffer(oi);
                    byte[] chunk = new byte[info.size]; b.position(info.offset); b.get(chunk, 0, info.size);
                    pcm.write(chunk, 0, chunk.length);
                    codec.releaseOutputBuffer(oi, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                } else if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat of = codec.getOutputFormat();
                    if (of.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                    if (of.containsKey(MediaFormat.KEY_SAMPLE_RATE)) rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                }
            }
            if (!outDone) throw new IOException("Audio exceeds the sampler length limit");
            byte[] raw = pcm.toByteArray();
            short[] s = new short[raw.length / 2];
            ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(s);
            if (ch > 2) {   // keep the first two channels
                short[] d = new short[s.length / ch * 2];
                for (int i = 0; i < d.length / 2; i++) { d[2 * i] = s[i * ch]; d[2 * i + 1] = s[i * ch + 1]; }
                s = d; ch = 2;
            }
            Pcm p = new Pcm(); p.data = s; p.channels = ch; p.rate = rate; return p;
        } catch (Exception e) {
            throw new IOException("cannot decode: " + e);
        } finally {
            try { if (codec != null) { codec.stop(); codec.release(); } } catch (Exception ignored) { }
            ex.release();
        }
    }

    static void writeWav(File f, Pcm p) throws IOException {
        int n = p.data.length * 2;
        ByteBuffer h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN);
        h.put("RIFF".getBytes()).putInt(36 + n).put("WAVEfmt ".getBytes()).putInt(16).putShort((short) 1).putShort((short) p.channels)
            .putInt(p.rate).putInt(p.rate * p.channels * 2).putShort((short) (p.channels * 2)).putShort((short) 16).put("data".getBytes()).putInt(n);
        ByteBuffer d = ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN);
        d.asShortBuffer().put(p.data);
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(h.array()); o.write(d.array()); }
    }
}
