package com.ferluht.fm1sampler;

import android.content.Context;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** A pack is a ZIP with its original internal directory structure. No one-shot or loop filter.
 * Keep EVERY entry, including unsupported files, so an import never silently discards content.
 * For compressed audio, add a playable WAV next to the untouched original whenever decoding succeeds.
 */
final class PackImport {
    private PackImport() { }

    static String safe(String s) {
        s = s.replace('\\', '/');
        int p = s.lastIndexOf('/'); if (p >= 0) s = s.substring(p + 1);
        s = s.replaceAll("[\\p{Cntrl}/\\\\]", "_").trim();
        if (s.equals(".") || s.equals("..") || s.isEmpty()) s = "Pack";
        return s.length() > 90 ? s.substring(0, 90) : s;
    }

    static File importZip(Context ctx, Uri uri, File samples) throws IOException {
        String source = AudioImport.displayName(ctx, uri);
        source = source.replaceFirst("(?i)\\.zip$", "");
        String base = safe(source);
        if (base.equalsIgnoreCase("OFFICIAL") || base.equalsIgnoreCase("SAVED")) base = "PACK_" + base;
        File target = new File(samples, base);
        for (int i = 2; target.exists(); i++) target = new File(samples, base + " (" + i + ")");
        if (!target.mkdirs()) throw new IOException("Cannot create pack folder");
        boolean completed = false;
        try {
            int files = 0;
            long bytes = 0;
            String root = target.getCanonicalPath() + File.separator;
            try (InputStream raw = ctx.getContentResolver().openInputStream(uri);
                 ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                byte[] buf = new byte[65536];
                while ((entry = zip.getNextEntry()) != null) {
                    if (++files > 20000) throw new IOException("Pack contains too many entries");
                    String name = entry.getName().replace('\\', '/');
                    File original = new File(target, name);
                    String canon = original.getCanonicalPath();
                    if (!canon.startsWith(root) && !canon.equals(target.getCanonicalPath()))
                        throw new IOException("Unsafe ZIP file path");
                    File out = original;
                    if (entry.isDirectory()) {
                        if (!out.isDirectory() && !out.mkdirs()) throw new IOException("Cannot create folder");
                    } else {
                        // ZIP archives may contain duplicate entry names.
                        // Preserve every occurrence rather than silently overwriting an earlier sample.
                        if (out.exists()) {
                            String filename = out.getName();
                            int dot = filename.lastIndexOf('.');
                            String stem = dot > 0 ? filename.substring(0, dot) : filename;
                            String ext = dot > 0 ? filename.substring(dot) : "";
                            for (int serial = 2; out.exists(); serial++)
                                out = new File(original.getParentFile(), stem + " (" + serial + ")" + ext);
                        }
                        File parent = out.getParentFile();
                        if (!parent.exists() && !parent.mkdirs()) throw new IOException("Cannot create parent");
                        if (out.isDirectory()) throw new IOException("Invalid ZIP path");
                        try (OutputStream dst = new FileOutputStream(out)) {
                            int n;
                            while ((n = zip.read(buf)) != -1) {
                                bytes += n;
                                if (bytes > 2_000_000_000L) throw new IOException("Pack exceeds 2GB safety limit");
                                dst.write(buf, 0, n);
                            }
                        }
                    }
                    zip.closeEntry();
                }
            }
            // Conversion does not remove originals: non-audio assets, loops and melodic phrases survive intact.
            // WAVs play directly; only formats the Android decoder recognises gain an additional WAV copy.
            convertAudio(ctx, target);
            completed = true;
            return target;
        } finally {
            if (!completed) deleteTree(target); // treat an incomplete ZIP as a failed transaction
        }
    }

    static void convertAudio(Context ctx, File folder) {
        File[] children = folder.listFiles();
        if (children == null) return;
        for (File f : children) {
            if (f.isDirectory()) { convertAudio(ctx, f); continue; }
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (!(n.endsWith(".mp3") || n.endsWith(".flac") || n.endsWith(".ogg") ||
                  n.endsWith(".m4a") || n.endsWith(".aac") || n.endsWith(".aif") ||
                  n.endsWith(".aiff"))) continue;
            try { AudioImport.importTo(ctx, Uri.fromFile(f), f.getParentFile()); }
            catch (Exception ignored) { /* ORIGINAL remains, and nothing is discarded */ }
        }
    }

    static void deleteTree(File f) {
        if (f.isDirectory()) {
            File[] all = f.listFiles();
            if (all != null) for (File c : all) deleteTree(c);
        }
        f.delete();
    }
}
