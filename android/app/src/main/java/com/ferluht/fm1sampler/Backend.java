package com.ferluht.fm1sampler;

import android.content.Context;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

/** Owns the native engine and the link to the FM-1: finds it on USB (MIDI + audio), routes the audio to it, runs the 30 Hz surface tick. */
public class Backend {
    static final String TAG = "fm1sampler";
    public static Backend instance;
    public final long engine;
    final Context ctx;
    final Handler main = new Handler(Looper.getMainLooper());
    final MidiManager midi;
    final AudioManager am;
    MidiDevice device;
    volatile MidiInputPort toFm1;
    MidiOutputPort fromFm1;
    int loggedIncomingEvents = 0;
    volatile boolean running;
    Thread ticker;
    boolean audioStarted, micStarted, micWanted;
    int usbAudioId = 0;
    /** Set by the activity: opens the system file picker (the phone UI asked for LOAD FILE FROM DISK). */
    public volatile Runnable onFileRequest;
    public volatile Runnable onPackRequest;
    public volatile java.util.function.IntConsumer onNameRequest;

    public static synchronized Backend get(Context c) {
        if (instance == null) instance = new Backend(c.getApplicationContext());
        return instance;
    }

    Backend(Context c) {
        ctx = c;
        try {
            // One root for the UI, packs, official sounds, recordings and FMP2 projects.
            java.io.File root = StorageLayout.initialize(c);
            engine = Native.create(StorageLayout.projects(c).getAbsolutePath(), root.getAbsolutePath());
        } catch (java.io.IOException e) {
            throw new IllegalStateException("Cannot initialize FM1-SAMPLER library", e);
        }
        midi = (MidiManager) c.getSystemService(Context.MIDI_SERVICE);
        am = (AudioManager) c.getSystemService(Context.AUDIO_SERVICE);
    }

    public synchronized void start() {
        if (running) return;
        running = true;
        startAudio();
        if (midi != null) {
            midi.registerDeviceCallback(new MidiManager.DeviceCallback() {
                @Override public void onDeviceAdded(MidiDeviceInfo i) { try { connectMidi(); } catch (Throwable t) { Log.w(TAG, "midi add: " + t); } }
                @Override public void onDeviceRemoved(MidiDeviceInfo i) { try { disconnectMidi(); connectMidi(); } catch (Throwable t) { Log.w(TAG, "midi remove: " + t); } }
            }, main);
            connectMidi();
        }
        am.registerAudioDeviceCallback(new AudioDeviceCallback() {
            @Override public void onAudioDevicesAdded(AudioDeviceInfo[] a) { try { restartAudioIfUsbChanged(); if (micWanted && !micStarted) startMic(); } catch (Throwable t) { Log.w(TAG, "audio add: " + t); } }
            @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] a) { try { restartAudioIfUsbChanged(); } catch (Throwable t) { Log.w(TAG, "audio remove: " + t); } }
        }, main);
        ticker = new Thread(() -> {
            long t0 = SystemClock.uptimeMillis();
            long lastRestart = 0;
            while (running) {
                try {
                    byte[] out = Native.tick(engine, SystemClock.uptimeMillis() - t0);
                    send(out);
                    Native.uiLink(engine, fromFm1 != null, usbAudioId != 0);
                    if (Native.wantFile(engine)) { Runnable r = onFileRequest; if (r != null) main.post(r); }
                    if (Native.wantPack(engine)) { Runnable r = onPackRequest; if (r != null) main.post(r); }
                    int request = Native.takeDialogRequest(engine);
                    if (request != 0) { java.util.function.IntConsumer handler = onNameRequest; if (handler != null) main.post(() -> handler.accept(request)); }
                    if (Native.audioDead(engine) && SystemClock.uptimeMillis() - lastRestart > 700) {   // the system closed the stream (USB plugged / unplugged)
                        lastRestart = SystemClock.uptimeMillis();
                        main.postDelayed(() -> { if (running) startAudio(); }, 300);
                    }
                } catch (Throwable t) { Log.w(TAG, "tick: " + t); }
                try { Thread.sleep(16); } catch (InterruptedException ignored) { }
            }
        }, "fm1-surface");
        ticker.start();
    }

    public synchronized void stop() {
        running = false;
        try { if (ticker != null) ticker.join(300); } catch (InterruptedException ignored) { }
        Native.stopAudio(engine);
        audioStarted = micStarted = false;
        disconnectMidi();
    }

    int findUsbAudio() {
        int id = 0;
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_OUTPUTS))
            if (d.getType() == AudioDeviceInfo.TYPE_USB_DEVICE || d.getType() == AudioDeviceInfo.TYPE_USB_HEADSET) id = d.getId();
        return id;
    }
    void startAudio() {
        usbAudioId = findUsbAudio();
        int r = Native.startAudio(engine, usbAudioId);
        audioStarted = r == 0;
        Log.i(TAG, "audio start " + r + " usb device " + usbAudioId);
    }
    void restartAudioIfUsbChanged() {
        if (!running) return;
        int id = findUsbAudio();
        if (id != usbAudioId) { Log.i(TAG, "usb audio device changed " + usbAudioId + " -> " + id); startAudio(); }
    }
    /** Call after RECORD_AUDIO was granted. The phone's own microphone, never the USB device. */
    public synchronized void startMic() {
        if (micStarted) return;
        int mic = 0;
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_INPUTS)) if (d.getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC) mic = d.getId();
        micWanted = true;
        micStarted = Native.startInput(engine, mic) == 0;
        Log.i(TAG, "mic start " + micStarted + " device " + mic);
    }

    // ---- MIDI ----
    // Android calls the device's OUTPUT port "output", because it emits
    // MIDI FROM the FM-1. That is the port required to receive physical
    // OCT+/OCT-/PLAY messages. The device INPUT (phone -> FM-1) is optional.
    boolean connecting;
    synchronized void connectMidi() {
        if (device != null || midi == null || connecting || !running) return;
        MidiDeviceInfo pick = null;
        int bestScore = -1, candidates = 0;
        for (MidiDeviceInfo i : midi.getDevices()) {
            if (i.getOutputPortCount() <= 0) {
                Log.d(TAG, "MIDI device without output (cannot receive panel buttons): " +
                           i.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME));
                continue;
            }
            candidates++;
            android.os.Bundle props = i.getProperties();
            String label = String.valueOf(props.getString(MidiDeviceInfo.PROPERTY_NAME)) + " " +
                           String.valueOf(props.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER)) + " " +
                           String.valueOf(props.getString(MidiDeviceInfo.PROPERTY_PRODUCT));
            String name = label.toLowerCase(java.util.Locale.ROOT);
            int score = 0;
            if (name.contains("fm-1") || name.contains("fm1") ||
                name.contains("m-vave") || name.contains("mvave")) score = 100;
            else if (name.contains("jieli") || name.contains("blackhole")) score = 60;
            if (score > bestScore) { pick = i; bestScore = score; }
        }
        if (pick == null || (bestScore == 0 && candidates != 1)) {
            if (candidates > 1 && bestScore == 0)
                Log.w(TAG, "Several unidentified MIDI outputs; cannot safely select FM-1");
            return;
        }
        final MidiDeviceInfo selected = pick;
        connecting = true;
        midi.openDevice(selected, d -> {
            synchronized (Backend.this) {
                connecting = false;
                if (d == null || device != null || !running) {
                    if (d != null) try { d.close(); } catch (Exception ignored) { }
                    return;
                }
                try {
                    MidiOutputPort rx = d.openOutputPort(0);
                    if (rx == null) {
                        Log.w(TAG, "FM-1 MIDI output port unavailable: OCT/PLAY cannot be detected");
                        d.close();
                        return;
                    }
                    MidiInputPort tx = d.openInputPort(0); // SysEx/LED updates optional
                    device = d;
                    fromFm1 = rx; toFm1 = tx;
                    loggedIncomingEvents = 0;
                    rx.connect(new MidiReceiver() {
                        @Override public void onSend(byte[] data, int off, int count, long ts) {
                            try {
                                // A bounded MIDI input trace lets us diagnose firmware
                                // that does not forward button note-on/off events.
                                if (loggedIncomingEvents < 24 && count > 0) {
                                    StringBuilder sb = new StringBuilder();
                                    int until = Math.min(off + count, off + 12);
                                    for (int k = off; k < until; ++k)
                                        sb.append(String.format(java.util.Locale.ROOT, "%02X ", data[k] & 255));
                                    Log.i(TAG, "FM-1 MIDI RX: " + sb.toString());
                                    loggedIncomingEvents++;
                                }
                                Native.midiIn(engine, data, off, count);
                            } catch (Throwable t) { Log.w(TAG, "midiIn: " + t); }
                        }
                    });
                    Log.i(TAG, "FM-1 MIDI RX connected; inputPortCount=" +
                               selected.getInputPortCount() + " outputPortCount=" +
                               selected.getOutputPortCount() +
                               "; commands require channel 2 notes 0/1/12 on this firmware");
                } catch (Throwable t) {
                    Log.w(TAG, "open ports: " + t);
                    try { d.close(); } catch (Exception ignored) { }
                    device = null; fromFm1 = null; toFm1 = null;
                }
            }
        }, main);
    }
    synchronized void disconnectMidi() {
        try { if (fromFm1 != null) fromFm1.close(); } catch (Exception ignored) { }
        try { if (toFm1 != null) toFm1.close(); } catch (Exception ignored) { }
        try { if (device != null) device.close(); } catch (Exception ignored) { }
        fromFm1 = null; toFm1 = null; device = null;
    }
    void send(byte[] b) {
        MidiInputPort port = toFm1;
        if (port == null || b.length == 0) return;
        int s = 0;
        for (int i = 0; i < b.length; i++) if ((b[i] & 0xff) == 0xf7) {
            try { port.send(b, s, i - s + 1); } catch (Exception e) { Log.w(TAG, "send: " + e); return; }
            s = i + 1;
        }
    }

    static void copyAssets(Context c, String from, java.io.File to) throws java.io.IOException {
        String[] list = c.getAssets().list(from);
        if (list == null || list.length == 0) {   // a file
            to.getParentFile().mkdirs();
            try (java.io.InputStream in = c.getAssets().open(from); java.io.OutputStream out = new java.io.FileOutputStream(to)) {
                byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n);
            }
            return;
        }
        to.mkdirs();
        for (String f : list) copyAssets(c, from + "/" + f, new java.io.File(to, f));
    }
}
