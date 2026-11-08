package com.ferluht.fm1sampler;

/** JNI bindings of native-lib.cpp (Session + SamplerApp + SamplerUI + Oboe). */
public final class Native {
    static { System.loadLibrary("fm1sampler"); }
    public static native long create(String dataDir, String sampleDir);
    public static native void destroy(long h);
    public static native int startAudio(long h, int deviceId);
    public static native int startInput(long h, int deviceId);
    public static native void stopAudio(long h);
    public static native boolean audioDead(long h);
    public static native int xruns(long h);
    public static native void midiIn(long h, byte[] data, int off, int n);
    public static native void midiDisconnected(long h);
    public static native byte[] tick(long h, long nowMs);
    public static native boolean wantFile(long h);
    public static native boolean wantPack(long h);
    public static native boolean wantLoopExport(long h);
    public static native boolean wantSongExport(long h);
    public static native boolean exportLoops(long h, String temporaryDirectory);
    public static native boolean exportSong(long h, String temporaryDirectory);
    public static native int loopExportStatus(long h);
    public static native int takeDialogRequest(long h);
    public static native String takePackDeletePath(long h);
    public static native void submitName(long h, int kind, String name);
    public static native boolean backupReady(long h); // no recording or full-project write/read underway
    public static native void refreshLibrary(long h);
    public static native void importFile(long h, String path);
    public static native void say(long h, String message);
    public static native void uiLink(long h, boolean midi, boolean usbAudio);
    public static native Object[] uiFrame(long h, float w, float hh, float top, float bottom, long nowMs, boolean pictureInPicture);
    public static native void uiTouch(long h, int action, float x, float y, long nowMs);
    private Native() { }
}
