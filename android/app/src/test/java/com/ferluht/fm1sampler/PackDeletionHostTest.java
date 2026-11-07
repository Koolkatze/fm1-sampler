package com.ferluht.fm1sampler;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

public final class PackDeletionHostTest {
    static void check(boolean good, String message) {
        if (!good) throw new AssertionError(message);
    }

    static void forbidden(File root, String name) throws Exception {
        boolean rejected = false;
        try { PackDeletion.delete(root, name); }
        catch (IOException expected) { rejected = true; }
        check(rejected, "Must reject " + name);
    }

    public static void main(String[] args) throws Exception {
        Path temp = Files.createTempDirectory("fm1-pack-delete-");
        File root = temp.resolve("FM1-SAMPLER").toFile();
        File packs = new File(root, "PACKS");
        File project = new File(root, "PROYECTOS/project_1.fm1p");
        File saved = new File(root, "SAVED/RECORDINGS/take.wav");
        File zip = new File(root, "PACKS/OtherPack/original.zip");
        check(project.getParentFile().mkdirs(), "project dir");
        check(saved.getParentFile().mkdirs(), "recording dir");
        check(zip.getParentFile().mkdirs(), "other pack dir");
        Files.write(project.toPath(), new byte[]{1,2,3});
        Files.write(saved.toPath(), new byte[]{4,5,6});
        Files.write(zip.toPath(), new byte[]{7,8,9});
        File sample = new File(packs, "MarsPack/drums/kick.wav");
        check(sample.getParentFile().mkdirs(), "sample dirs");
        Files.write(sample.toPath(), new byte[]{10,11});
        File pack = new File(packs, "MarsPack");
        check(PackDeletion.delete(packs, "MarsPack") == 3, "pack files and directories");
        check(!pack.exists(), "selected pack removed");
        check(project.isFile() && saved.isFile() && zip.isFile(), "all other data preserved");

        forbidden(packs, "");
        forbidden(packs, ".");
        forbidden(packs, "..");
        forbidden(packs, "../PROYECTOS");
        forbidden(packs, "/PROYECTOS");
        forbidden(packs, "OtherPack/../../PROYECTOS");
        forbidden(packs, "OtherPack/../OtherPack");
        forbidden(packs, "OtherPack\\original.zip");
        check(zip.isFile() && project.isFile(), "traversal cannot erase anything");

        File wav = new File(packs, "OtherPack/hi hat.wav");
        Files.write(wav.toPath(), new byte[]{42});
        check(PackDeletion.delete(packs, "OtherPack/hi hat.wav") == 1, "one selected file");
        check(!wav.exists() && zip.exists(), "other pack files untouched");

        Path link = packs.toPath().resolve("OtherPack/project_link");
        try {
            Files.createSymbolicLink(link, project.toPath());
            forbidden(packs, "OtherPack/project_link");
            check(project.exists(), "symlink target preserved");
        } catch (UnsupportedOperationException | SecurityException | IOException unsupported) {
            // Some host filesystems disallow symlink creation.
        }
        System.out.println("PackDeletionHostTest OK");
    }
}
