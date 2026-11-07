package com.ferluht.fm1sampler;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Deletes only user-selected descendants of FM1-SAMPLER/PACKS.
 * No part of PROYECTOS, SAVED, OFFICIAL, backups or the PACKS root can
 * be reached by this API. The caller must first obtain user confirmation.
 * Preflight rejects path traversal and symbolic links before any deletion.
 */
final class PackDeletion {
    private static final int MAX_ENTRIES = 200000;

    private PackDeletion() { }

    static File resolveTarget(File packsRoot, String relative) throws IOException {
        if (relative == null || relative.isEmpty() || relative.startsWith("/")
                || relative.indexOf('\\') >= 0 || relative.indexOf('\u0000') >= 0)
            throw new IOException("Invalid pack selection");
        String[] components = relative.split("/", -1);
        File root = packsRoot.getCanonicalFile();
        if (!root.isDirectory()) throw new IOException("PACKS folder unavailable");
        File result = root;
        for (String c : components) {
            if (c.isEmpty() || c.equals(".") || c.equals(".."))
                throw new IOException("Unsafe file path");
            result = new File(result, c);
        }
        verifyPath(root, result);
        if (!result.isFile() && !result.isDirectory())
            throw new IOException("Selected pack file no longer exists");
        return result;
    }

    private static void verifyPath(File root, File candidate) throws IOException {
        String base = root.getCanonicalPath() + File.separator;
        File canonical = candidate.getCanonicalFile();
        if (!canonical.getPath().startsWith(base))
            throw new IOException("Refusing to delete outside PACKS");
        // A symlink may redirect a folder inside PACKS to unrelated
        // projects or files, even if it ultimately resolves within PACKS.
        if (!canonical.equals(candidate.getAbsoluteFile()))
            throw new IOException("Refusing to delete symbolic links");
    }

    private static void preflight(File root, File file, List<File> postorder)
            throws IOException {
        verifyPath(root, file);
        if (postorder.size() >= MAX_ENTRIES)
            throw new IOException("Selected pack is too large for safe deletion");
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot inspect selected folder");
            for (File child : children) preflight(root, child, postorder);
        } else if (!file.isFile()) {
            throw new IOException("Selected file is missing or unsupported");
        }
        postorder.add(file);
    }

    /** Called only after explicit confirmation. Never deletes a different folder. */
    static int delete(File packsRoot, String relative) throws IOException {
        File root = packsRoot.getCanonicalFile();
        File target = resolveTarget(root, relative);
        List<File> entries = new ArrayList<>();
        // Validate the full tree before deleting even the first file.
        preflight(root, target, entries);
        for (File file : entries) {
            verifyPath(root, file);
            if (!file.delete()) throw new IOException("Cannot delete " + file.getName());
        }
        return entries.size();
    }
}
