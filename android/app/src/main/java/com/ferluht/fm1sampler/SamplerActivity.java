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
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import java.io.File;

/** The app: starts the backend (engine, FM-1 link, audio), shows the sampler and opens the system file picker when the UI asks for a file. */
public class SamplerActivity extends Activity {
    static final int PICK = 7;
    static final int PICK_PACK = 8;
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

    void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"audio/*", "application/ogg"});
        try { startActivityForResult(i, PICK); } catch (Exception e) { Native.say(backend.engine, "NO FILE PICKER ON THIS PHONE"); }
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
        if (code == PICK_PACK) {
            Native.say(backend.engine, "IMPORTING PACK...");
            new Thread(() -> {
                try {
                    File out = PackImport.importZip(this, uri, StorageLayout.root(this));
                    Native.refreshLibrary(backend.engine);
                    Native.say(backend.engine, "PACK: " + out.getName());
                } catch (Exception e) {
                    android.util.Log.w("fm1sampler", "pack import", e);
                    Native.say(backend.engine, "PACK IMPORT FAILED");
                }
            }, "pack-import").start();
            return;
        }
        if (code != PICK) return;
        Native.say(backend.engine, "READING THE FILE...");
        new Thread(() -> {
            try {
                File folder = StorageLayout.imports(this);
                folder.mkdirs();
                File out = AudioImport.importTo(this, uri, folder);
                Native.importFile(backend.engine, out.getAbsolutePath());
            } catch (Exception e) {
                android.util.Log.w("fm1sampler", "import: " + e);
                Native.say(backend.engine, "CANNOT READ THIS FILE");
            }
        }, "import").start();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) backend.startMic();
    }
}
