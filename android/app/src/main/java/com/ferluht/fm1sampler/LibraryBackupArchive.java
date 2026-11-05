package com.ferluht.fm1sampler;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.StringReader;
import java.net.URLDecoder;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Portable FM1-SAMPLER library backup. This class deliberately uses plain Java
 * APIs supported on Android 6+, so its archive format and restore safety are
 * also exercised by host Java tests in GitHub Actions.
 *
 * The archive restores SAVED (loops/recordings/imports), PACKS, OFFICIAL and
 * PROYECTOS (including self-contained FMP2 projects, scenes and SONG data).
 * No existing file is ever deleted on an overwrite: its original is moved
 * to a separate recovery folder outside the live library.
 */
final class LibraryBackupArchive {
    static final String PREFIX = "FM1-SAMPLER/";
    static final String MANIFEST = "FM1-BACKUP-V1.manifest";
    static final String HEADER = "FM1-SAMPLER-BACKUP\t1";
    static final int MAX_FILES = 50000;
    static final long MAX_BYTES = 8_000_000_000L;
    private static final int BUFFER = 65536;

    static final class Spec {
        final String path;
        final boolean directory;
        final long size;
        final String digest;
        Spec(String path, boolean directory, long size, String digest) {
            this.path = path; this.directory = directory;
            this.size = size; this.digest = digest;
        }
    }
    static final class Plan {
        final File zip;
        final LinkedHashMap<String, Spec> items;
        final long bytes;
        final int collisions;
        Plan(File zip, LinkedHashMap<String, Spec> items, long bytes, int collisions) {
            this.zip = zip; this.items = items; this.bytes = bytes;
            this.collisions = collisions;
        }
        int files() {
            int count = 0;
            for (Spec s : items.values()) if (!s.directory) count++;
            return count;
        }
    }
    static final class Result {
        final int imported, unchanged, protectedFiles;
        final File recovery;
        Result(int imported, int unchanged, int protectedFiles, File recovery) {
            this.imported = imported;
            this.unchanged = unchanged;
            this.protectedFiles = protectedFiles;
            this.recovery = recovery;
        }
    }
    private static final class Installed {
        final File target, prior;
        Installed(File target, File prior) { this.target = target; this.prior = prior; }
    }

    private LibraryBackupArchive() { }

    private static IOException fail(String message) { return new IOException(message); }
    private static MessageDigest sha256() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
    }
    private static String hex(byte[] hash) {
        final char[] digits = "0123456789abcdef".toCharArray();
        char[] out = new char[hash.length * 2];
        for (int i = 0; i < hash.length; i++) {
            out[2 * i] = digits[(hash[i] & 255) >>> 4];
            out[2 * i + 1] = digits[hash[i] & 15];
        }
        return new String(out);
    }
    private static String encode(String path) throws IOException {
        return URLEncoder.encode(path, "UTF-8");
    }
    private static String decode(String value) throws IOException {
        try { return URLDecoder.decode(value, "UTF-8"); }
        catch (IllegalArgumentException e) { throw fail("Malformed backup manifest path"); }
    }
    private static String checkedPath(String relative) throws IOException {
        if (relative == null || relative.length() == 0 || relative.length() > 1024
                || relative.startsWith("/") || relative.endsWith("/")
                || relative.indexOf('\\') >= 0 || relative.indexOf(':') >= 0
                || relative.indexOf('\u0000') >= 0)
            throw fail("Unsafe backup path");
        for (String part : relative.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals(".."))
                throw fail("Unsafe backup path component");
            for (int i = 0; i < part.length(); i++)
                if (part.charAt(i) < 0x20) throw fail("Control character in backup name");
        }
        return relative;
    }
    private static File destination(File root, String relative) throws IOException {
        checkedPath(relative);
        File result = new File(root, relative);
        final String base = root.getCanonicalPath() + File.separator;
        if (!result.getCanonicalPath().startsWith(base))
            throw fail("Path escapes FM1-SAMPLER library");
        return result;
    }
    private static void mkdir(File dir) throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs())
            throw fail("Cannot create directory: " + dir.getName());
    }
    private static void scan(File root, File dir, String relative, List<Spec> found)
            throws IOException {
        File[] children = dir.listFiles();
        if (children == null) throw fail("Cannot read library folder");
        List<File> sorted = new ArrayList<>();
        Collections.addAll(sorted, children);
        Collections.sort(sorted, (a, b) -> a.getName().compareTo(b.getName()));
        for (File file : sorted) {
            String path = checkedPath(relative.isEmpty() ? file.getName() : relative + "/" + file.getName());
            if (found.size() >= MAX_FILES) throw fail("Too many library entries");
            if (!destination(root, path).getCanonicalPath().equals(file.getAbsolutePath()))
                throw fail("Library has a symbolic link or an unsafe path");
            if (file.isDirectory()) {
                found.add(new Spec(path, true, 0, ""));
                scan(root, file, path, found);
            } else if (file.isFile()) {
                found.add(new Spec(path, false, file.length(), ""));
            } else throw fail("Cannot include an unknown library file type");
        }
    }

    /** Write a self-contained ZIP to a user-selected Android document stream. */
    static int write(File root, OutputStream output) throws IOException {
        if (!root.isDirectory()) throw fail("FM1-SAMPLER library missing");
        List<Spec> files = new ArrayList<>();
        scan(root, root, "", files);
        StringBuilder manifest = new StringBuilder(HEADER).append('\n');
        long total = 0;
        int count = 0;
        byte[] buffer = new byte[BUFFER];
        try (ZipOutputStream zip = new ZipOutputStream(output)) {
            zip.setLevel(1);
            zip.putNextEntry(new ZipEntry(PREFIX));
            zip.closeEntry();
            for (Spec file : files) {
                if (file.directory) {
                    zip.putNextEntry(new ZipEntry(PREFIX + file.path + "/"));
                    zip.closeEntry();
                    manifest.append("D\t").append(encode(file.path)).append('\n');
                    continue;
                }
                if (++count > MAX_FILES || file.size > MAX_BYTES ||
                        total > MAX_BYTES - file.size) throw fail("Backup exceeds safety limit");
                File source = destination(root, file.path);
                MessageDigest hash = sha256();
                long written = 0;
                zip.putNextEntry(new ZipEntry(PREFIX + file.path));
                try (InputStream in = new FileInputStream(source)) {
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        written += n;
                        if (written > file.size || written > MAX_BYTES)
                            throw fail("A file changed while creating the backup");
                        hash.update(buffer, 0, n);
                        zip.write(buffer, 0, n);
                    }
                }
                zip.closeEntry();
                if (written != file.size) throw fail("A file changed while creating the backup");
                total += written;
                manifest.append("F\t").append(written).append('\t')
                        .append(hex(hash.digest())).append('\t')
                        .append(encode(file.path)).append('\n');
            }
            byte[] body = manifest.toString().getBytes(StandardCharsets.UTF_8);
            zip.putNextEntry(new ZipEntry(MANIFEST));
            zip.write(body);
            zip.closeEntry();
        }
        return count;
    }

    /** Parse and authenticate ALL entries before permitting a single restore write. */
    static Plan inspect(File root, File archive) throws IOException {
        if (!archive.isFile()) throw fail("Backup ZIP not found");
        LinkedHashMap<String, Spec> actual = new LinkedHashMap<>();
        byte[] manifestData = null;
        long total = 0;
        int entries = 0;
        byte[] buffer = new byte[BUFFER];
        try (ZipFile zip = new ZipFile(archive)) {
            Enumeration<? extends ZipEntry> iterator = zip.entries();
            while (iterator.hasMoreElements()) {
                ZipEntry item = iterator.nextElement();
                String name = item.getName();
                if (++entries > MAX_FILES + 2) throw fail("Too many ZIP entries");
                if (name.equals(PREFIX)) {
                    if (!item.isDirectory()) throw fail("Invalid backup root");
                    continue;
                }
                if (name.equals(MANIFEST)) {
                    if (manifestData != null || item.getSize() > 4_000_000L)
                        throw fail("Duplicate or oversized backup manifest");
                    try (InputStream in = zip.getInputStream(item);
                         ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                        int n;
                        while ((n = in.read(buffer)) != -1) {
                            if (out.size() + n > 4_000_000) throw fail("Backup manifest too large");
                            out.write(buffer, 0, n);
                        }
                        manifestData = out.toByteArray();
                    }
                    continue;
                }
                if (!name.startsWith(PREFIX)) throw fail("Not an FM1-SAMPLER backup");
                String relative = name.substring(PREFIX.length());
                if (item.isDirectory() && relative.endsWith("/"))
                    relative = relative.substring(0, relative.length() - 1);
                checkedPath(relative);
                destination(root, relative);
                if (actual.containsKey(relative)) throw fail("Duplicate file or folder in ZIP");
                if (item.isDirectory()) {
                    actual.put(relative, new Spec(relative, true, 0, ""));
                    continue;
                }
                if (item.getSize() > MAX_BYTES) throw fail("Oversized file in backup");
                MessageDigest hash = sha256();
                long read = 0;
                try (InputStream in = zip.getInputStream(item)) {
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        read += n;
                        if (read > MAX_BYTES || total > MAX_BYTES - read)
                            throw fail("Backup expands beyond safety limit");
                        hash.update(buffer, 0, n);
                    }
                }
                total += read;
                actual.put(relative, new Spec(relative, false, read, hex(hash.digest())));
            }
        }
        if (manifestData == null) throw fail("Not a recognised FM1-SAMPLER backup");
        LinkedHashMap<String, Spec> declared = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(
                new StringReader(new String(manifestData, StandardCharsets.UTF_8)))) {
            if (!HEADER.equals(reader.readLine()))
                throw fail("Unsupported FM1-SAMPLER backup version");
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] data = line.split("\t", -1);
                if (data.length == 2 && data[0].equals("D")) {
                    String path = checkedPath(decode(data[1]));
                    if (declared.put(path, new Spec(path, true, 0, "")) != null)
                        throw fail("Duplicate manifest directory");
                } else if (data.length == 4 && data[0].equals("F")) {
                    long size;
                    try { size = Long.parseLong(data[1]); }
                    catch (NumberFormatException e) { throw fail("Invalid file size in manifest"); }
                    if (size < 0 || size > MAX_BYTES ||
                            !data[2].matches("[0-9a-f]{64}"))
                        throw fail("Invalid file checksum");
                    String path = checkedPath(decode(data[3]));
                    if (declared.put(path, new Spec(path, false, size, data[2])) != null)
                        throw fail("Duplicate manifest file");
                } else throw fail("Malformed backup manifest");
            }
        }
        if (declared.size() != actual.size())
            throw fail("Backup ZIP entries do not match manifest");
        for (Spec expected : declared.values()) {
            Spec existing = actual.get(expected.path);
            if (existing == null || expected.directory != existing.directory ||
                    expected.size != existing.size || !expected.digest.equals(existing.digest))
                throw fail("Backup integrity check failed: " + expected.path);
        }
        int collisions = 0;
        for (Spec spec : declared.values()) {
            if (!spec.directory && destination(root, spec.path).exists()) collisions++;
        }
        return new Plan(archive, declared, total, collisions);
    }

    /**
     * Merge by default; replace only with explicit user confirmation. Existing
     * bytes moved to FM1-SAMPLER-RECOVERY outside the live library are NEVER
     * deleted, even when replacement succeeds.
     */
    static Result restore(File root, Plan plan, boolean replace) throws IOException {
        mkdir(root);
        File parent = root.getParentFile();
        File stage = new File(parent, ".FM1-BACKUP-STAGING-" + System.nanoTime());
        if (!stage.mkdir()) throw fail("Cannot create restore staging folder");
        String stamp = Long.toString(System.currentTimeMillis()) + "-" + System.nanoTime();
        File recovery = new File(parent, "FM1-SAMPLER-RECOVERY/" + stamp);
        List<Installed> installed = new ArrayList<>();
        int restored = 0, skipped = 0, preserved = 0;
        byte[] buffer = new byte[BUFFER];
        boolean committed = false;
        try {
            // ZIP bytes are first unpacked into a separate staging tree.
            // Every checksum was checked by inspect(); verify again during copy.
            try (ZipFile zip = new ZipFile(plan.zip)) {
                for (Spec spec : plan.items.values()) {
                    if (spec.directory) continue;
                    File dest = destination(root, spec.path);
                    if (dest.exists() && dest.isDirectory())
                        throw fail("File collides with an existing folder");
                    if (dest.exists() && !replace) { skipped++; continue; }
                    // No restore may traverse a pre-existing symlink or file parent.
                    File work = destination(stage, spec.path);
                    mkdir(work.getParentFile());
                    ZipEntry entry = zip.getEntry(PREFIX + spec.path);
                    if (entry == null) throw fail("Backup file missing after validation");
                    MessageDigest hash = sha256();
                    long read = 0;
                    try (InputStream in = zip.getInputStream(entry);
                         FileOutputStream out = new FileOutputStream(work)) {
                        int n;
                        while ((n = in.read(buffer)) != -1) {
                            read += n;
                            if (read > spec.size) throw fail("File changed during restoration");
                            out.write(buffer, 0, n);
                            hash.update(buffer, 0, n);
                        }
                        out.getFD().sync();
                    }
                    if (read != spec.size || !spec.digest.equals(hex(hash.digest())))
                        throw fail("Imported backup file failed checksum");
                }
            }
            // Preflight every destination before the first file is changed.
            for (Spec spec : plan.items.values()) {
                File dest = destination(root, spec.path);
                if (spec.directory && dest.exists() && !dest.isDirectory())
                    throw fail("Folder collides with an existing file");
                if (dest.exists() && !spec.directory && dest.isDirectory())
                    throw fail("File collides with an existing folder");
            }
            for (Spec spec : plan.items.values()) {
                File dest = destination(root, spec.path);
                if (spec.directory) { mkdir(dest); continue; }
                if (dest.exists() && !replace) continue;
                File src = destination(stage, spec.path);
                mkdir(dest.getParentFile());
                File previous = null;
                if (dest.exists()) {
                    previous = destination(recovery, spec.path);
                    mkdir(previous.getParentFile());
                    if (!dest.renameTo(previous))
                        throw fail("Could not preserve original before replacing");
                    preserved++;
                }
                if (!src.renameTo(dest)) {
                    if (previous != null && !previous.renameTo(dest))
                        throw fail("Restore failed; original retained in recovery folder");
                    throw fail("Unable to install verified backup file");
                }
                installed.add(new Installed(dest, previous));
                restored++;
            }
            committed = true;
            return new Result(restored, skipped, preserved, preserved == 0 ? null : recovery);
        } finally {
            if (!committed) {
                // Restore pre-existing content on failure; files that were
                // newly added by this import are removed again.
                for (int i = installed.size() - 1; i >= 0; --i) {
                    Installed file = installed.get(i);
                    if (!file.target.delete()) continue;
                    if (file.prior != null) file.prior.renameTo(file.target);
                }
            }
            delete(stage);
        }
    }

    private static void delete(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) delete(child);
        }
        file.delete();
    }
}
