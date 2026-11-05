package com.ferluht.fm1sampler;

import android.content.Context;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * One logical library root, created when the app first starts:
 * FM1-SAMPLER/{PROYECTOS,OFFICIAL,SAVED/LOOPS,SAVED/RECORDINGS,PACKS}.
 *
 * App-private storage is intentional: it needs no broad filesystem permissions
 * on Android 17. The SAF document picker can import ZIPs from any provider.
 * Existing data is migrated without replacing an existing user file.
 */
final class StorageLayout {
    private static final String TAG = "fm1sampler";
    private StorageLayout() { }

    static File root(Context context) { return new File(context.getFilesDir(), "FM1-SAMPLER"); }
    static File projects(Context context) { return new File(root(context), "PROYECTOS"); }
    static File official(Context context) { return new File(root(context), "OFFICIAL"); }
    static File packs(Context context) { return new File(root(context), "PACKS"); }
    static File loops(Context context) { return new File(root(context), "SAVED/LOOPS"); }
    static File recordings(Context context) { return new File(root(context), "SAVED/RECORDINGS"); }
    static File imports(Context context) { return new File(root(context), "SAVED/Imports"); }

    static File initialize(Context context) throws IOException {
        File root = root(context);
        for (File dir : new File[]{root, projects(context), official(context), packs(context),
                                   loops(context), recordings(context), imports(context)})
            ensureDirectory(dir);
        // Former APKs used files/samples as the library root and files/ for scenes/projects.
        // Migration is repeatable: a failed move leaves the source available to retry.
        migrateLibrary(context);
        migrateProjects(context);
        for (String category : new String[]{"Bass", "Drums", "Keys", "Pads"}) {
            File dest = new File(official(context), category);
            if (!dest.exists()) Backend.copyAssets(context, "samples/" + category, dest);
        }
        return root;
    }

    private static void ensureDirectory(File dir) throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs())
            throw new IOException("Cannot create FM1-SAMPLER folder: " + dir);
    }

    private static void migrateLibrary(Context context) {
        File legacy = new File(context.getFilesDir(), "samples");
        File[] children = legacy.listFiles();
        if (children == null) return;
        for (File child : children) {
            String name = child.getName();
            File destination;
            if (name.equals("OFFICIAL")) destination = new File(official(context), name);
            else if (name.equals("SAVED")) destination = new File(root(context), name);
            else if (name.equals(".v2")) continue; // previous bundled asset marker
            else if (child.isDirectory()) destination = new File(packs(context), name);
            else destination = new File(imports(context), name);
            try { mergePreserving(child, destination); }
            catch (IOException e) { Log.w(TAG, "Keeping legacy sample at " + child, e); }
        }
    }

    private static void migrateProjects(Context context) {
        File previous = context.getFilesDir();
        File[] files = previous.listFiles();
        if (files == null) return;
        for (File f : files) {
            String n = f.getName();
            try {
                if (n.startsWith("sample_") && n.toLowerCase(java.util.Locale.ROOT).endsWith(".wav"))
                    mergePreserving(f, new File(recordings(context), n));
                else if (n.equals("project_assets") || n.equals("projects") ||
                         n.startsWith("project_") || n.startsWith("fm1_scenes"))
                    mergePreserving(f, new File(projects(context), n));
            } catch (IOException e) { Log.w(TAG, "Keeping legacy project at " + f, e); }
        }
    }

    /** Move whole trees where possible, merge on collision, never clobber existing files. */
    private static void mergePreserving(File from, File to) throws IOException {
        if (!from.exists()) return;
        if (from.isDirectory()) {
            if (!to.exists() && from.renameTo(to)) return;
            if (to.exists() && !to.isDirectory()) to = unique(to);
            ensureDirectory(to);
            File[] children = from.listFiles();
            if (children == null) throw new IOException("Cannot read " + from);
            for (File f : children) mergePreserving(f, new File(to, f.getName()));
            if (!from.delete()) Log.w(TAG, "Legacy folder retained: " + from);
            return;
        }
        ensureDirectory(to.getParentFile());
        if (to.exists()) to = unique(to);
        if (from.renameTo(to)) return;
        // Fallback if the filesystem cannot rename across volumes.
        File part = new File(to.getParentFile(), to.getName() + ".fm1part");
        if (part.exists()) part = unique(part);
        boolean copied = false;
        try (FileInputStream input = new FileInputStream(from);
             FileOutputStream output = new FileOutputStream(part)) {
            byte[] buffer = new byte[65536];
            int n;
            while ((n = input.read(buffer)) != -1) output.write(buffer, 0, n);
            output.getFD().sync();
            copied = true;
        } finally {
            if (!copied) part.delete();
        }
        if (!part.renameTo(to)) { part.delete(); throw new IOException("Cannot finish migration: " + to); }
        if (!from.delete()) Log.w(TAG, "Legacy original retained: " + from);
    }

    private static File unique(File wanted) {
        String n = wanted.getName();
        int dot = n.lastIndexOf('.');
        String base = dot > 0 ? n.substring(0, dot) : n;
        String ext = dot > 0 ? n.substring(dot) : "";
        File next = wanted;
        for (int i = 2; next.exists(); ++i)
            next = new File(wanted.getParentFile(), base + " (" + i + ")" + ext);
        return next;
    }
}
