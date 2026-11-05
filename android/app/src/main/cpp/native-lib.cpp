// JNI glue: Session + Surface + Oboe output stream. Java feeds MIDI bytes from the FM-1 in and polls the SysEx bytes to send out.
#include <jni.h>
#include <oboe/Oboe.h>
#include <android/log.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <Session.h>
#include <SamplerApp.h>
#include <SamplerUI.h>
#include <cstring>
#include <Common.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fm1sampler", __VA_ARGS__)

namespace {
struct Engine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
    Session session{44100};
    std::mutex surfaceMtx;
    std::vector<uint8_t> out;           // SysEx bytes waiting for Java to send
    std::unique_ptr<SamplerApp> app;
    std::unique_ptr<SamplerUI> ui;
    std::vector<uint8_t> sx; bool inSx = false;
    uint8_t msg[3] = {0, 0, 0}; int msgLen = 0, msgNeed = 3;   // channel message being assembled
    std::shared_ptr<oboe::AudioStream> stream;
    std::atomic<int> xruns{0};
    std::mutex streamMtx;               // start() / stop() / startInput() never run at the same time
    std::atomic<bool> streamDead{false};   // the output stream was torn down by the system (USB plugged / unplugged): Java restarts it
    int deviceId = 0;
    std::shared_ptr<oboe::AudioStream> inStream;
    std::vector<float> inMono = std::vector<float>(8192), inStereo = std::vector<float>(16384);

    Engine(const std::string& dataDir, const std::string& sampleDir) {
        session.setDataDir(dataDir); session.setSampleDir(sampleDir);   // before the app: it reads the library and loads the first samples
        app = std::make_unique<SamplerApp>(session, [this](const uint8_t* m, size_t n) { out.insert(out.end(), m, m + n); });
        ui = std::make_unique<SamplerUI>(*app);
    }
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t frames) override {
        Denormals::enableOnThisThread();
        const float* in = nullptr;
        if (inStream && frames <= 8192) {
            auto r = inStream->read(inMono.data(), frames, 0);   // non-blocking: whatever the mic has delivered
            int got = r ? r.value() : 0;
            for (int i = 0; i < frames; i++) { float v = i < got ? inMono[i] : 0.f; inStereo[2 * i] = v; inStereo[2 * i + 1] = v; }
            in = inStereo.data();
        }
        session.process(static_cast<float*>(data), in, frames);
        return oboe::DataCallbackResult::Continue;
    }
    bool onError(oboe::AudioStream*, oboe::Result r) override { LOG("stream error %d", (int)r); return false; }
    // Runs on a thread of Oboe's, often while Java is already restarting the stream for the same USB event: only note it, Java restarts on its own thread.
    void onErrorAfterClose(oboe::AudioStream* s, oboe::Result r) override { LOG("stream closed: %s", oboe::convertToText(r)); if (s == streamRaw.load()) streamDead = true; }
    std::atomic<oboe::AudioStream*> streamRaw{nullptr};

    int start(int dev) {
        std::lock_guard<std::mutex> lk(streamMtx);
        deviceId = dev;
        streamDead = false; streamRaw = nullptr;
        if (stream) { stream->close(); stream.reset(); }
        oboe::Result r = oboe::Result::ErrorInternal;
        for (int attempt = 0; attempt < 3; attempt++) {   // exclusive low latency first, then shared, then without the device id
            oboe::AudioStreamBuilder b;
            b.setDirection(oboe::Direction::Output)->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(attempt == 0 ? oboe::SharingMode::Exclusive : oboe::SharingMode::Shared)->setFormat(oboe::AudioFormat::Float)->setChannelCount(2)
                ->setSampleRate(44100)->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
                ->setUsage(oboe::Usage::Media)->setDataCallback(this)->setErrorCallback(this);
            if (dev && attempt < 2) b.setDeviceId(dev);
            r = b.openStream(stream);
            if (r == oboe::Result::OK) break;
            LOG("open failed (attempt %d): %s", attempt, oboe::convertToText(r));
            stream.reset();
        }
        if (r != oboe::Result::OK) return -1;
        streamRaw = stream.get();
        stream->setBufferSizeInFrames(stream->getFramesPerBurst() * 2);
        r = stream->requestStart();
        LOG("stream started: rate %d burst %d buffer %d sharing %d perf %d dev %d", stream->getSampleRate(), stream->getFramesPerBurst(),
            stream->getBufferSizeInFrames(), (int)stream->getSharingMode(), (int)stream->getPerformanceMode(), stream->getDeviceId());
        return r == oboe::Result::OK ? 0 : -2;
    }
    int startInput(int dev) {
        std::lock_guard<std::mutex> lk(streamMtx);
        if (inStream) return 0;
        // Phones differ in what they accept: try the clean low-latency setup first, then relax step by step.
        struct Try { bool lowLatency, unprocessed, withDev, withRate; };
        const Try tries[] = {{true, true, true, true}, {true, false, true, true}, {false, false, true, true}, {false, false, false, true}, {false, false, false, false}};
        for (const Try& t : tries) {
            oboe::AudioStreamBuilder b;
            b.setDirection(oboe::Direction::Input)->setFormat(oboe::AudioFormat::Float)->setChannelCount(1)
                ->setPerformanceMode(t.lowLatency ? oboe::PerformanceMode::LowLatency : oboe::PerformanceMode::None)
                ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
            if (t.withRate) b.setSampleRate(44100);
            if (t.unprocessed) b.setInputPreset(oboe::InputPreset::Unprocessed);
            if (t.withDev && dev) b.setDeviceId(dev);
            std::shared_ptr<oboe::AudioStream> s;
            oboe::Result r = b.openStream(s);
            if (r != oboe::Result::OK) { LOG("input open failed (ll %d unproc %d dev %d rate %d): %s", t.lowLatency, t.unprocessed, t.withDev, t.withRate, oboe::convertToText(r)); continue; }
            r = s->requestStart();
            if (r != oboe::Result::OK) { LOG("input start failed: %s", oboe::convertToText(r)); s->close(); continue; }
            inStream = s;
            LOG("input started: rate %d dev %d ll %d unproc %d", s->getSampleRate(), s->getDeviceId(), t.lowLatency, t.unprocessed);
            return 0;
        }
        return -1;
    }
    void stop() {
        std::lock_guard<std::mutex> lk(streamMtx);
        streamRaw = nullptr;
        if (inStream) { inStream->stop(); inStream->close(); inStream.reset(); } if (stream) { stream->stop(); stream->close(); stream.reset(); } }
};
Engine* E(jlong h) { return reinterpret_cast<Engine*>(h); }
}  // namespace

extern "C" {
#define FN(ret, name) JNIEXPORT ret JNICALL Java_com_ferluht_fm1sampler_Native_##name

FN(jlong, create)(JNIEnv* env, jclass, jstring dataDir, jstring sampleDir) {
    const char* a = env->GetStringUTFChars(dataDir, nullptr); const char* b = env->GetStringUTFChars(sampleDir, nullptr);
    auto* e = new Engine(a, b);
    env->ReleaseStringUTFChars(dataDir, a); env->ReleaseStringUTFChars(sampleDir, b);
    return reinterpret_cast<jlong>(e);
}
FN(void, destroy)(JNIEnv*, jclass, jlong h) { E(h)->stop(); delete E(h); }
FN(jint, startAudio)(JNIEnv*, jclass, jlong h, jint dev) { return E(h)->start(dev); }
FN(jint, startInput)(JNIEnv*, jclass, jlong h, jint dev) { return E(h)->startInput(dev); }
FN(void, stopAudio)(JNIEnv*, jclass, jlong h) { E(h)->stop(); }
// 1 once when the system closed the output stream under us (the device list changed)
FN(jboolean, audioDead)(JNIEnv*, jclass, jlong h) { return E(h)->streamDead.exchange(false); }
FN(jint, xruns)(JNIEnv*, jclass, jlong h) { auto& e = *E(h); return e.stream ? e.stream->getXRunCount().value() : -1; }

FN(void, midiIn)(JNIEnv* env, jclass, jlong h, jbyteArray a, jint off, jint n) {
    if (!a || off < 0 || n <= 0 || off + n > env->GetArrayLength(a)) return;
    jbyte* d = env->GetByteArrayElements(a, nullptr);
    if (!d) return;
    auto& e = *E(h);
    std::lock_guard<std::mutex> l(e.surfaceMtx);
    for (int i = off; i < off + n; i++) {
        const uint8_t s = (uint8_t)d[i];
        if (s >= 0xf8) continue;                        // real time (clock, active sensing): may come anywhere, even inside a SysEx
        if (e.inSx) {                                   // SysEx from the FM-1 (scale state, redraw request), may span calls
            if (s >= 0x80 && s != 0xf7) { e.inSx = false; i--; continue; }   // anything else that starts a message ends a broken SysEx
            if (e.sx.size() < 256) e.sx.push_back(s);
            if (s == 0xf7) { e.inSx = false; e.app->onSysex(e.sx.data(), e.sx.size()); }
            continue;
        }
        if (s == 0xf0) { e.inSx = true; e.sx.assign(1, s); e.msgLen = 0; continue; }
        if (s >= 0xf0) { e.msgLen = 0; continue; }
        if (s >= 0x80) { e.msg[0] = s; e.msgLen = 1; e.msgNeed = ((s & 0xf0) == 0xc0 || (s & 0xf0) == 0xd0) ? 2 : 3; continue; }
        if (e.msgLen == 0) continue;                    // a data byte without a status
        e.msg[e.msgLen++] = s;
        if (e.msgLen == e.msgNeed) { if (e.msgNeed == 3) e.app->onMidi(e.msg, 3); e.msgLen = 1; }   // running status: the status stays
    }
    env->ReleaseByteArrayElements(a, d, JNI_ABORT);
}
FN(jbyteArray, tick)(JNIEnv* env, jclass, jlong h, jlong nowMs) {
    auto& e = *E(h);
    std::lock_guard<std::mutex> l(e.surfaceMtx);
    e.app->tick((uint32_t)nowMs);
    jbyteArray r = env->NewByteArray((jsize)e.out.size());
    if (!e.out.empty()) env->SetByteArrayRegion(r, 0, (jsize)e.out.size(), (const jbyte*)e.out.data());
    e.out.clear();
    return r;
}
// the phone UI asked for the disk picker (LOAD FILE FROM DISK)
FN(jboolean, wantFile)(JNIEnv*, jclass, jlong h) { auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx); return e.app->wantFile(); }
// a picked file, already converted to a 16-bit WAV in the data dir: load it on the selected track
FN(void, importFile)(JNIEnv* env, jclass, jlong h, jstring path) {
    auto& e = *E(h); const char* c = env->GetStringUTFChars(path, nullptr);
    { std::lock_guard<std::mutex> l(e.surfaceMtx); e.app->importFile(c); }
    env->ReleaseStringUTFChars(path, c);
}
FN(jboolean, wantPack)(JNIEnv*, jclass, jlong h) {
    auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx);
    return e.app->wantPack();
}
FN(jint, takeDialogRequest)(JNIEnv*, jclass, jlong h) {
    auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx);
    return e.app->takeDialogRequest();
}
FN(void, submitName)(JNIEnv* env, jclass, jlong h, jint kind, jstring name) {
    auto& e = *E(h); const char* c = env->GetStringUTFChars(name, nullptr);
    { std::lock_guard<std::mutex> l(e.surfaceMtx); e.app->submitName((int)kind, c); }
    env->ReleaseStringUTFChars(name, c);
}
FN(void, refreshLibrary)(JNIEnv*, jclass, jlong h) {
    auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx);
    e.app->refreshLibrary();
}
FN(void, say)(JNIEnv* env, jclass, jlong h, jstring msg) {
    auto& e = *E(h); const char* c = env->GetStringUTFChars(msg, nullptr);
    { std::lock_guard<std::mutex> l(e.surfaceMtx); e.app->say(c); }
    env->ReleaseStringUTFChars(msg, c);
}
FN(void, uiLink)(JNIEnv*, jclass, jlong h, jboolean midi, jboolean usb) { auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx); e.ui->setLink(midi, usb); }
// one UI frame: Object[]{ int[] ops (9 ints each: type, 6 float bit patterns, colour, text index), String texts joined by \u0001 }
FN(jobjectArray, uiFrame)(JNIEnv* env, jclass, jlong h, jfloat w, jfloat hh, jfloat top, jfloat bottom, jlong now) {
    auto& e = *E(h);
    std::lock_guard<std::mutex> l(e.surfaceMtx);
    e.ui->setSize(w, hh, top, bottom);
    e.ui->frame((uint32_t)now);
    const auto& ops = e.ui->ops();
    std::vector<jint> v; v.reserve(ops.size() * 9);
    std::string texts; int ti = 0;
    for (auto& o : ops) {
        v.push_back((jint)o.type);
        for (int k = 0; k < 6; k++) { jint bits; memcpy(&bits, &o.f[k], 4); v.push_back(bits); }
        v.push_back((jint)o.color);
        if (o.type == UiOp::TEXT) { if (ti) texts += '\x01'; texts += o.text; v.push_back(ti++); } else v.push_back(-1);
    }
    jintArray ia = env->NewIntArray((jsize)v.size()); env->SetIntArrayRegion(ia, 0, (jsize)v.size(), v.data());
    jclass oc = env->FindClass("java/lang/Object");
    jobjectArray r = env->NewObjectArray(2, oc, nullptr);
    env->SetObjectArrayElement(r, 0, ia);
    env->SetObjectArrayElement(r, 1, env->NewStringUTF(texts.c_str()));
    return r;
}
FN(void, uiTouch)(JNIEnv*, jclass, jlong h, jint action, jfloat x, jfloat y, jlong now) { auto& e = *E(h); std::lock_guard<std::mutex> l(e.surfaceMtx); e.ui->touch(action, x, y, (uint32_t)now); }
}
