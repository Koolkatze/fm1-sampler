package com.ferluht.fm1sampler;

import android.content.Context;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** SAF document streaming; the archive engine itself remains platform-free. */
final class LibraryBackup {
    private LibraryBackup() { }

    /** The user's chosen destination is external to private app data and can
     * be moved between phones or kept after the app has been uninstalled. */
    static int save(Context context, Uri destination) throws IOException {
        try (OutputStream out = context.getContentResolver().openOutputStream(destination, "wt")) {
            if (out == null) throw new IOException("Cannot write the selected backup destination");
            return LibraryBackupArchive.write(StorageLayout.root(context), out);
        }
    }

    /** Copy an arbitrary document-provider URI to a private read-only snapshot
     * before validation, so restore never depends on provider seeking or timing. */
    static File snapshot(Context context, Uri source) throws IOException {
        File cached = File.createTempFile("fm1-import-backup-", ".zip", context.getCacheDir());
        boolean completed = false;
        try (InputStream in = context.getContentResolver().openInputStream(source);
             FileOutputStream out = new FileOutputStream(cached)) {
            if (in == null) throw new IOException("Cannot open selected backup");
            byte[] buffer = new byte[65536];
            long length = 0;
            int n;
            while ((n = in.read(buffer)) != -1) {
                length += n;
                if (length > LibraryBackupArchive.MAX_BYTES)
                    throw new IOException("Backup ZIP file exceeds safety limit");
                out.write(buffer, 0, n);
            }
            out.getFD().sync();
            completed = true;
            return cached;
        } finally {
            if (!completed) cached.delete();
        }
    }
}
