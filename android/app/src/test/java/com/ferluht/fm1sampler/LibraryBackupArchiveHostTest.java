package com.ferluht.fm1sampler;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Run with plain javac/java on Linux CI (no emulator or Android dependencies). */
public final class LibraryBackupArchiveHostTest {
    private static int checks;
    private static void check(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
        checks++;
        System.out.println("PASS " + description);
    }
    private static void mkdir(File dir) throws Exception {
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException(dir.toString());
    }
    private static void put(File file, String text) throws Exception {
        mkdir(file.getParentFile());
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
    private static String read(File file) throws Exception {
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[(int)file.length()];
            int n = in.read(bytes);
            if (n != bytes.length) throw new IOException("Incomplete read");
            return new String(bytes, StandardCharsets.UTF_8);
        }
    }
    private static void failInspect(File root, File archive, String message) throws Exception {
        boolean failed = false;
        try { LibraryBackupArchive.inspect(root, archive); }
        catch (IOException expected) { failed = true; }
        check(failed, message);
    }
    public static void main(String[] args) throws Exception {
        File base = new File(System.getProperty("java.io.tmpdir"), "fm1backup-" + System.nanoTime());
        File src = new File(base, "old/FM1-SAMPLER");
        File dest = new File(base, "new/FM1-SAMPLER");
        mkdir(src);
        put(new File(src, "PROYECTOS/project_1.fm1p"), "FMP2\\0 all 8 tracks, scenes, song, clips");
        put(new File(src, "PROYECTOS/fm1_scenes.dat"), "scene slots");
        put(new File(src, "SAVED/LOOPS/mi loop.wav"), "stereo wav bytes");
        put(new File(src, "SAVED/RECORDINGS/mic.wav"), "recording bytes");
        put(new File(src, "PACKS/Lo Fi/Drums/kick.wav"), "kick audio");
        put(new File(src, "PACKS/Lo Fi/other.xml"), "<arbitrary>file</arbitrary>");
        put(new File(src, "OFFICIAL/Bass/Fuzz.wav"), "bundled preset");
        mkdir(new File(src, "PACKS/Empty Subfolder"));
        File backup = new File(base, "full.zip");
        try (FileOutputStream out = new FileOutputStream(backup)) {
            check(LibraryBackupArchive.write(src, out) == 7,
                  "backup exports all project, scene, loop, recording, pack and official files");
        }
        LibraryBackupArchive.Plan plan = LibraryBackupArchive.inspect(dest, backup);
        check(plan.files() == 7 && plan.collisions == 0,
              "ZIP manifest and SHA-256 verify on a fresh install");
        LibraryBackupArchive.Result first = LibraryBackupArchive.restore(dest, plan, false);
        check(first.imported == 7 && first.unchanged == 0 && first.protectedFiles == 0,
              "new-device restore recovers complete library with no collisions");
        check(read(new File(dest, "PROYECTOS/project_1.fm1p")).startsWith("FMP2") &&
              read(new File(dest, "SAVED/LOOPS/mi loop.wav")).equals("stereo wav bytes") &&
              read(new File(dest, "PACKS/Lo Fi/other.xml")).contains("arbitrary") &&
              new File(dest, "PACKS/Empty Subfolder").isDirectory(),
              "project, loops, imported non-audio files and empty directories survive");

        put(new File(dest, "PROYECTOS/project_1.fm1p"), "newer local project");
        plan = LibraryBackupArchive.inspect(dest, backup);
        check(plan.collisions == 7, "restore detects files already present");
        LibraryBackupArchive.Result keep = LibraryBackupArchive.restore(dest, plan, false);
        check(keep.imported == 0 && keep.unchanged == 7 &&
              read(new File(dest, "PROYECTOS/project_1.fm1p")).equals("newer local project"),
              "KEEP EXISTING never overwrites any local project");

        LibraryBackupArchive.Result replace = LibraryBackupArchive.restore(dest, plan, true);
        check(replace.imported == 7 && replace.protectedFiles == 7 &&
              replace.recovery.isDirectory() &&
              read(new File(replace.recovery, "PROYECTOS/project_1.fm1p")).equals("newer local project") &&
              read(new File(dest, "PROYECTOS/project_1.fm1p")).startsWith("FMP2"),
              "REPLACE restores backup and retains originals in a separate recovery folder");

        File malicious = new File(base, "zip-slip.zip");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(malicious))) {
            out.putNextEntry(new ZipEntry("FM1-SAMPLER/../../outside-project.fm1p"));
            out.write("bad".getBytes(StandardCharsets.UTF_8));
            out.closeEntry();
            out.putNextEntry(new ZipEntry(LibraryBackupArchive.MANIFEST));
            out.write((LibraryBackupArchive.HEADER + "\\n").getBytes(StandardCharsets.UTF_8));
            out.closeEntry();
        }
        failInspect(dest, malicious, "path traversal ZIP is rejected before writing");
        check(!new File(base, "outside-project.fm1p").exists(), "ZIP slip never writes outside library");

        File randomZip = new File(base, "not-backup.zip");
        try (FileOutputStream out = new FileOutputStream(randomZip)) {
            out.write("not a zip".getBytes(StandardCharsets.UTF_8));
        }
        failInspect(dest, randomZip, "unrelated file rejected instead of importing as a library backup");
        System.out.println("ALL " + checks + " FM1-SAMPLER BACKUP CHECKS PASSED");
    }
}
