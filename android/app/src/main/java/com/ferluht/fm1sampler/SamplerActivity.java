package com.ferluht.fm1sampler;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.text.InputType;
import android.widget.EditText;
import java.util.Arrays;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import java.io.File;

/** The app: starts the backend (engine, FM-1 link, audio), shows the sampler and opens the system file picker when the UI asks for a file. */
public class SamplerActivity extends Activity {
    static final int PICK = 7;
    static final int PICK_PACK = 8;
    static final int EXPORT_BACKUP = 9;
    static final int RESTORE_BACKUP = 10;
    private volatile boolean backupBusy;
    Backend backend;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        installCrashLog();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().setStatusBarColor(0xff1a1a1f);
        getWindow().setNavigationBarColor(0xff1a1a1f);
        backend = Backend.get(this);
        backend.onFileRequest = this::pickFile;
        backend.onPackRequest = this::pickPack;
        backend.onLoopExportRequest = this::exportLoops;
        backend.onNameRequest = this::nameDialog;
        backend.start();
        SamplerView v = new SamplerView(this, backend.engine, 0, 0);
        setContentView(v);
        hideSystemBars();
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) backend.startMic();
        else requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 1);
    }

    /** Full screen: no status or navigation bar (a swipe from the edge shows them for a moment). */
    void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (focus) hideSystemBars(); }
    @Override protected void onResume() { super.onResume(); hideSystemBars(); }

    /** Java crashes end up in files/crash.log (the last one), so a crash can be read without adb. */
    void installCrashLog() {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        final File f = new File(getFilesDir(), "crash.log");
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try (java.io.PrintWriter w = new java.io.PrintWriter(new java.io.FileWriter(f))) { w.println(new java.util.Date() + " thread " + t.getName()); e.printStackTrace(w); } catch (Exception ignored) { }
            if (prev != null) prev.uncaughtException(t, e);
        });
    }

    /** Explicit external import from EDIT only. HOME LIBRARY uses the
     * in-app directory navigator and never launches Android DocumentsUI.
     * Providers may label valid ZIP/audio files as application/octet-stream.
     */
    void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try { startActivityForResult(i, PICK); }
        catch (Exception e) { Native.say(backend.engine, "NO FILE PICKER ON THIS PHONE"); }
    }

    void pickPack() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES,
            new String[]{"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        try { startActivityForResult(i, PICK_PACK); }
        catch (Exception e) { Native.say(backend.engine, "NO ZIP PICKER"); }
    }

    /** SAVE BACKUP produces an ordinary, portable ZIP under a document URI,
     * not an app-private file that would disappear after uninstalling. */
    void pickBackupExport() {
        if (backupBusy) { Native.say(backend.engine, "BACKUP ALREADY RUNNING"); return; }
        if (!Native.backupReady(backend.engine)) {
            Native.say(backend.engine, "FINISH RECORDING OR PROJECT SAVE FIRST");
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        String stamp = new java.text.SimpleDateFormat("yyyyMMdd-HHmmss",
                         Locale.ROOT).format(new java.util.Date());
        intent.putExtra(Intent.EXTRA_TITLE, "FM1-SAMPLER-BACKUP-" + stamp + ".zip");
        try { startActivityForResult(intent, EXPORT_BACKUP); }
        catch (Exception e) { Native.say(backend.engine, "BACKUP SAVE PICKER UNAVAILABLE"); }
    }

    void pickBackupImport() {
        if (backupBusy) { Native.say(backend.engine, "BACKUP ALREADY RUNNING"); return; }
        if (!Native.backupReady(backend.engine)) {
            Native.say(backend.engine, "FINISH RECORDING OR PROJECT SAVE FIRST");
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*"); // also accept ZIP labelled octet-stream by a provider
        try { startActivityForResult(intent, RESTORE_BACKUP); }
        catch (Exception e) { Native.say(backend.engine, "BACKUP IMPORT PICKER UNAVAILABLE"); }
    }

    private void finishBackupImport(LibraryBackupArchive.Plan plan) {
        if (!Native.backupReady(backend.engine)) {
            plan.zip.delete(); backupBusy = false;
            Native.say(backend.engine, "FINISH RECORDING FIRST");
            return;
        }
        String message = plan.files() + " files checked (SHA-256).\\n"
            + plan.collisions + " existing names.\\n\\n"
            + "Choose KEEP to import only missing files; nothing is overwritten. "
            + "Choose REPLACE to restore the backup versions; existing originals "
            + "are preserved in a separate safety folder.";
        AlertDialog.Builder dialog = new AlertDialog.Builder(this)
            .setTitle("IMPORT BACKUP")
            .setMessage(message)
            .setPositiveButton("KEEP EXISTING", (d, which) -> restoreBackup(plan, false))
            .setNegativeButton("CANCEL", (d, which) -> discardBackup(plan))
            .setOnCancelListener(d -> discardBackup(plan));
        if (plan.collisions > 0) {
            dialog.setNeutralButton("REPLACE", (d, which) ->
                new AlertDialog.Builder(this)
                    .setTitle("REPLACE CURRENT FILES?")
                    .setMessage("Files with matching names will be replaced by "
                        + "their backup copies. The previous files will be kept "
                        + "in a recovery folder outside FM1-SAMPLER. "
                        + "Save a separate backup of your current library first.")
                    .setPositiveButton("REPLACE", (confirm, button) -> restoreBackup(plan, true))
                    .setNegativeButton("CANCEL", (confirm, button) -> discardBackup(plan))
                    .setOnCancelListener(confirm -> discardBackup(plan))
                    .show());
        }
        dialog.show();
    }

    private void discardBackup(LibraryBackupArchive.Plan plan) {
        plan.zip.delete();
        backupBusy = false;
    }

    private void restoreBackup(LibraryBackupArchive.Plan plan, boolean replace) {
        if (!Native.backupReady(backend.engine)) {
            discardBackup(plan);
            Native.say(backend.engine, "FINISH RECORDING FIRST");
            return;
        }
        Native.say(backend.engine, "IMPORTING VERIFIED BACKUP...");
        new Thread(() -> {
            try {
                LibraryBackupArchive.Result result =
                    LibraryBackupArchive.restore(StorageLayout.root(this), plan, replace);
                Native.refreshLibrary(backend.engine);
                Native.say(backend.engine, "RESTORED " + result.imported + " FILES");
                runOnUiThread(() -> {
                    if (isFinishing()) return;
                    new AlertDialog.Builder(this)
                        .setTitle("BACKUP IMPORTED")
                        .setMessage(result.imported + " files imported. "
                            + result.unchanged + " existing files kept. "
                            + result.protectedFiles + " previous files protected."
                            + "\\nReopen the app to load restored projects and settings.")
                        .setPositiveButton("OK", null).show();
                });
            } catch (Exception error) {
                android.util.Log.e("fm1sampler", "backup restore", error);
                Native.say(backend.engine, "BACKUP IMPORT FAILED");
                runOnUiThread(() -> {
                    if (!isFinishing())
                        new AlertDialog.Builder(this).setTitle("BACKUP IMPORT FAILED")
                            .setMessage(error.getMessage()).setPositiveButton("OK", null).show();
                });
            } finally {
                discardBackup(plan);
            }
        }, "fm1-library-restore").start();
    }

    private void importBackup(Uri uri) {
        if (backupBusy) { Native.say(backend.engine, "BACKUP ALREADY RUNNING"); return; }
        backupBusy = true;
        Native.say(backend.engine, "VERIFYING BACKUP...");
        new Thread(() -> {
            File snapshot = null;
            try {
                snapshot = LibraryBackup.snapshot(this, uri);
                LibraryBackupArchive.Plan plan =
                    LibraryBackupArchive.inspect(StorageLayout.root(this), snapshot);
                runOnUiThread(() -> {
                    if (isFinishing()) discardBackup(plan);
                    else finishBackupImport(plan);
                });
            } catch (Exception error) {
                if (snapshot != null) snapshot.delete();
                backupBusy = false;
                android.util.Log.e("fm1sampler", "backup inspect", error);
                Native.say(backend.engine, "INVALID BACKUP - NO FILES CHANGED");
                runOnUiThread(() -> {
                    if (!isFinishing())
                        new AlertDialog.Builder(this).setTitle("INVALID BACKUP")
                            .setMessage(error.getMessage()).setPositiveButton("OK", null).show();
                });
            }
        }, "fm1-library-inspect").start();
    }

    /** Detect ZIPs by filename, MIME or PK header, without trusting the
     * provider's MIME type. All reads use the granted document URI. */
    static boolean isZipDocument(android.content.Context ctx, Uri uri) throws IOException {
        String name = AudioImport.displayName(ctx, uri);
        if (name != null && name.toLowerCase(Locale.ROOT).endsWith(".zip")) return true;
        String mime = ctx.getContentResolver().getType(uri);
        if (mime != null) {
            String type = mime.toLowerCase(Locale.ROOT);
            if (type.equals("application/zip") ||
                type.equals("application/x-zip-compressed") ||
                type.equals("application/x-zip")) return true;
        }
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException("Cannot open selected document");
            byte[] sig = new byte[4];
            int n = 0;
            while (n < sig.length) {
                int m = in.read(sig, n, sig.length - n);
                if (m < 0) break;
                if (m == 0) continue;
                n += m;
            }
            return n == 4 && sig[0] == 0x50 && sig[1] == 0x4b &&
                   ((sig[2] == 3 && sig[3] == 4) ||
                    (sig[2] == 5 && sig[3] == 6) ||
                    (sig[2] == 7 && sig[3] == 8));
        }
    }

    /** Export a consistent audio-thread snapshot; publish completed WAVs
     * into the public Downloads directory through Android MediaStore. */
    void exportLoops() {
        if (android.os.Build.VERSION.SDK_INT < 29) {
            Native.say(backend.engine, "EXPORT NEEDS ANDROID 10+");
            return;
        }
        final File temp = new File(getCacheDir(), "fm1-loop-export-" +
                                   System.currentTimeMillis() + "-" + System.nanoTime());
        if (!temp.mkdirs()) {
            Native.say(backend.engine, "CANNOT PREPARE LOOP EXPORT");
            return;
        }
        if (!Native.exportLoops(backend.engine, temp.getAbsolutePath())) {
            temp.delete();
            Native.say(backend.engine, "EXPORT ALREADY RUNNING");
            return;
        }
        Native.say(backend.engine, "PREPARING WAV EXPORT...");
        new Thread(() -> {
            try {
                long until = android.os.SystemClock.elapsedRealtime() + 600000L;
                int state;
                do {
                    state = Native.loopExportStatus(backend.engine);
                    if (state != 1) break;
                    Thread.sleep(100);
                } while (android.os.SystemClock.elapsedRealtime() < until);
                if (state != 2)
                    throw new java.io.IOException(state == -1
                         ? "No finished loops or recording still active"
                         : state == -2 ? "Writing loop WAV files failed"
                         : "Loop export timed out");
                int files = LoopExport.publish(this, temp);
                LoopExport.deleteTemporaryFiles(temp);
                Native.say(backend.engine, "EXPORTED " + files +
                    " WAVS TO DOWNLOAD/FM1-SAMPLER");
            } catch (Exception e) {
                android.util.Log.w("fm1sampler", "loop export", e);
                Native.say(backend.engine, "EXPORT FAILED - RECORDINGS KEPT");
                // Keep any finished temporary WAVs for recovery after errors.
            }
        }, "fm1-export-loops").start();
    }

    /** Native FM-1 graphics keep their aesthetic; use Android's actual keyboard to name files. */
    void nameDialog(int kind) {
        if (kind == 4) { pickBackupExport(); return; }
        if (kind == 5) { pickBackupImport(); return; }
        if (kind == 3) {
            File dir = new File(StorageLayout.projects(this), "projects");
            File[] all = dir.listFiles(f -> f.isDirectory() && new File(f, "project.state").isFile());
            if (all == null || all.length == 0) {
                Native.say(backend.engine, "NO SAVED PROJECTS");
                return;
            }
            Arrays.sort(all, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            String[] names = new String[all.length];
            for (int i = 0; i < all.length; i++) names[i] = all[i].getName();
            new AlertDialog.Builder(this).setTitle("OPEN PROJECT")
                .setItems(names, (dialog, which) -> Native.submitName(backend.engine, 3, names[which]))
                .setNegativeButton("CANCEL", null).show();
            return;
        }
        EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        name.setText(kind == 1 ? "My Loop" : "My Project");
        name.selectAll();
        new AlertDialog.Builder(this)
            .setTitle(kind == 1 ? "SAVE LOOP AS" : "SAVE PROJECT AS")
            .setView(name)
            .setPositiveButton("SAVE", (dialog, which) -> {
                String value = name.getText().toString().trim();
                if (value.isEmpty()) Native.say(backend.engine, "NAME REQUIRED");
                else Native.submitName(backend.engine, kind, value);
            })
            .setNegativeButton("CANCEL", null).show();
    }

    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        if (code == EXPORT_BACKUP) {
            if (backupBusy || !Native.backupReady(backend.engine)) {
                Native.say(backend.engine, "FINISH RECORDING FIRST"); return;
            }
            backupBusy = true;
            Native.say(backend.engine, "WRITING FULL LIBRARY BACKUP...");
            new Thread(() -> {
                try {
                    int count = LibraryBackup.save(this, uri);
                    Native.say(backend.engine, "BACKUP SAVED: " + count + " FILES");
                    runOnUiThread(() -> {
                        if (!isFinishing())
                            new AlertDialog.Builder(this).setTitle("BACKUP SAVED")
                                .setMessage(count + " saved library files copied into the "
                                    + "selected ZIP. Keep this ZIP outside the app. "
                                    + "Save your currently playing project separately if "
                                    + "it has unsaved changes.")
                                .setPositiveButton("OK", null).show();
                    });
                } catch (Exception error) {
                    android.util.Log.e("fm1sampler", "backup write", error);
                    Native.say(backend.engine, "BACKUP SAVE FAILED");
                    runOnUiThread(() -> {
                        if (!isFinishing())
                            new AlertDialog.Builder(this).setTitle("BACKUP SAVE FAILED")
                                .setMessage(error.getMessage()).setPositiveButton("OK", null).show();
                    });
                } finally { backupBusy = false; }
            }, "fm1-library-backup").start();
            return;
        }
        if (code == RESTORE_BACKUP) { importBackup(uri); return; }
        if (code != PICK && code != PICK_PACK) return;
        // Both explicit external import actions route through the same importer. Selecting a ZIP always extracts
        // its complete original directory tree under FM1-SAMPLER/PACKS.
        new Thread(() -> {
            try {
                boolean zip = isZipDocument(this, uri);
                if (code == PICK_PACK && !zip) {
                    Native.say(backend.engine, "PLEASE SELECT A ZIP FILE");
                    return;
                }
                if (zip) {
                    Native.say(backend.engine, "UNPACKING ZIP TO PACKS...");
                    File pack = PackImport.importZip(this, uri, StorageLayout.root(this));
                    Native.refreshLibrary(backend.engine);
                    Native.say(backend.engine, "PACK IMPORTED: " + pack.getName());
                    return;
                }
                Native.say(backend.engine, "IMPORTING AUDIO...");
                File audioFolder = StorageLayout.imports(this);
                if (!audioFolder.isDirectory() && !audioFolder.mkdirs())
                    throw new IOException("Cannot create sample imports folder");
                File out = AudioImport.importTo(this, uri, audioFolder);
                Native.importFile(backend.engine, out.getAbsolutePath());
                Native.say(backend.engine, "SAMPLE IMPORTED: " + out.getName());
            } catch (Exception e) {
                android.util.Log.w("fm1sampler", "HOME file import", e);
                Native.say(backend.engine, "IMPORT FAILED - SOURCE UNCHANGED");
            }
        }, "fm1-home-file-import").start();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) backend.startMic();
    }
}
