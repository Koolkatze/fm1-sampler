package com.ferluht.fm1sampler;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * Publish complete WAV loop stems to the phone's real, shared Downloads folder
 * using MediaStore (scoped storage). This is visible from Files and over USB
 * without broad storage permission, and never changes private source projects.
 */
final class LoopExport {
    static final String PUBLIC_FOLDER = "Download/FM1-SAMPLER/EXPORTED LOOPS";
    private LoopExport() {}

    static int publish(Context context, File temporaryDirectory) throws IOException {
        if (Build.VERSION.SDK_INT < 29)
            throw new IOException("Public Downloads export requires Android 10 or newer");
        File[] files = temporaryDirectory.listFiles(file ->
            file.isFile() && file.getName().toLowerCase(Locale.ROOT).endsWith(".wav"));
        if (files == null || files.length == 0)
            throw new IOException("Export folder contains no WAV recordings");
        Arrays.sort(files, (a, b) -> a.getName().compareTo(b.getName()));

        final ContentResolver resolver = context.getContentResolver();
        final String batch = new SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(new Date());
        int published = 0;
        for (File file : files) {
            // Unique per-export prefixes prevent accidental replacement of any
            // previous file, even when its track number is the same.
            ContentValues meta = new ContentValues();
            meta.put(MediaStore.MediaColumns.DISPLAY_NAME, "FM1_" + batch + "_" + file.getName());
            meta.put(MediaStore.MediaColumns.MIME_TYPE, "audio/wav");
            meta.put(MediaStore.MediaColumns.RELATIVE_PATH,
                     Environment.DIRECTORY_DOWNLOADS + "/FM1-SAMPLER/EXPORTED LOOPS/");
            meta.put(MediaStore.MediaColumns.IS_PENDING, 1);
            Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, meta);
            if (uri == null) throw new IOException("Cannot create shared Downloads WAV");
            boolean committed = false;
            try {
                try (FileInputStream in = new FileInputStream(file);
                     OutputStream out = resolver.openOutputStream(uri, "w")) {
                    if (out == null) throw new IOException("Cannot write exported WAV");
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                    out.flush();
                }
                meta.clear();
                meta.put(MediaStore.MediaColumns.IS_PENDING, 0);
                if (resolver.update(uri, meta, null, null) < 1)
                    throw new IOException("Cannot finish media export");
                committed = true;
                published++;
            } finally {
                // On partial failure remove ONLY the newly created target, not
                // any original project, recorded loop or previous successful WAV.
                if (!committed) resolver.delete(uri, null, null);
            }
        }
        return published;
    }

    static void deleteTemporaryFiles(File folder) {
        File[] list = folder.listFiles();
        if (list != null) for (File f : list) if (f.isFile()) f.delete();
        folder.delete();
    }
}
