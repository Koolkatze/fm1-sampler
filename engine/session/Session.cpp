#include "Session.h"
#include "Pitch.h"
#include "Arp.h"
#include "Common.h"
#include "Filter.h"
#include "Reverb.h"
#include "Sampler.h"
#include "Stretch.h"
#include "TapeDelay.h"
#include "TapeInsert.h"
#include "Wav.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <array>
#include <limits>
#include <functional>
#include <mutex>
#include <thread>

namespace {
constexpr int kBlock = 256;
std::string safeSampleName(const std::string& name) {
    std::string out;
    for (unsigned char ch : name) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch < 32 || ch == 127) out += '_';
        else out += (char)ch;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out == "." || out == "..") out.clear();
    if (out.size() > 100) out.resize(100);
    return out.empty() ? "Loop" : out;
}

// Write PCM in fixed-size blocks: a three-minute loop at 0.25x tape speed
// must not allocate a multi-hundred-megabyte WAV vector on the phone.
class LoopWavStream {
public:
    LoopWavStream(const std::string& path, uint64_t frames, int sampleRate)
        : out_(path, std::ios::binary), expected_(frames) {
        if (!out_ || !frames || sampleRate < 8000 ||
            frames > (UINT32_MAX - 36u) / 4u) return;
        auto u16 = [this](uint16_t v) {
            const char b[] = {char(v & 255), char((v >> 8) & 255)};
            out_.write(b, 2);
        };
        auto u32 = [this](uint32_t v) {
            const char b[] = {char(v & 255), char((v >> 8) & 255),
                              char((v >> 16) & 255), char((v >> 24) & 255)};
            out_.write(b, 4);
        };
        const uint32_t bytes = (uint32_t)(frames * 4);
        out_.write("RIFF", 4); u32(36u + bytes);
        out_.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
        u32((uint32_t)sampleRate); u32((uint32_t)sampleRate * 4u);
        u16(4); u16(16); out_.write("data", 4); u32(bytes);
        valid_ = !!out_;
    }
    bool write(const float* stereo, int frames) {
        if (!valid_ || frames < 0 || done_ + (uint64_t)frames > expected_) return false;
        std::array<char, kBlock * 4> bytes{};
        if (frames > kBlock) return false;
        for (int i = 0; i < frames * 2; ++i) {
            const int16_t q = (int16_t)std::lrintf(
                clampf(stereo[i], -1.f, 1.f) * 32767.f);
            bytes[(size_t)i * 2] = char((uint16_t)q & 255);
            bytes[(size_t)i * 2 + 1] = char(((uint16_t)q >> 8) & 255);
        }
        out_.write(bytes.data(), (std::streamsize)frames * 4);
        done_ += (uint64_t)frames;
        return !!out_;
    }
    bool finish() {
        out_.flush();
        return valid_ && done_ == expected_ && !!out_;
    }
private:
    std::ofstream out_;
    uint64_t expected_ = 0, done_ = 0;
    bool valid_ = false;
};

}

struct Session::Impl {
    struct Note { int note, vel; };
    struct Track {
        std::unique_ptr<Sampler> sampler;
        mutable std::mutex infoMtx; std::string label; std::vector<float> peaks; std::shared_ptr<const SampleBuf> orig;   // for the UI threads
        // sound shaping
        Arp arp; bool arpOn = false;
        Lowpass filt; bool filtOn = false;
        Session::Lfo lfo; float lfoPhase = 0, lfoSH = 0, lfoVal = 0;
        TapeInsert tape;
        float vol = 0.8f, pan = 0.f, send[2] = {0.f, 0.f};
        // sidechain compressor: this track is ducked by the level of track scSrc (-1 off); scEnv is this track's own level, read by the others
        int scSrc = -1; float scThrDb = -20.f, scRatio = 1.f, scEnv = 0.f, scGain = 1.f;
        std::vector<Note> pending;   // notes of this block (without the arpeggiator)
        // offline stretch / pitch
        float speed = 1.f, semis = 0.f, rangeA = 0.f, rangeB = 1.f; uint32_t gen = 0;
        float sliceThr = 0.22f, kitSemis = 0.f, grainScatter = 1.f, paulStretch = 8.f, paulWidth = 1.f; int grainN = 7; std::vector<float> slices;   // mode parameters; slices for the UI (under infoMtx)
        // the audio loop of this track
        std::shared_ptr<std::vector<float>> clip = std::make_shared<std::vector<float>>();  // COW: scene snapshots share loop audio
        bool clipHas = false, clipRec = false;
        bool recPrevOk = false; double recPrevCp = 0; float recPrevL = 0, recPrevR = 0;   // tape-speed recording: previous write position and input
        std::atomic<float> peak{0.f};
        explicit Track(int sr) : tape(sr) {}
    };

    struct Scene {
        bool filled = false;
        int32_t samples = 0;
        float tempo = 120.f;
        int bars = 4;
        std::array<std::shared_ptr<std::vector<float>>, kTracks> clips{};
        std::array<bool, kTracks> has{};
    };
    std::array<Scene, Session::kScenes> scenes;
    // Independent edit workspace. Scene launches swap the active clip pointers;
    // retain the complete composition using COW before the FIRST launch, and
    // restore those eight clip buffers when leaving either performance mode.
    // Samples assigned to tracks are held separately and never replaced.
    struct Composition {
        bool valid = false;
        std::array<std::shared_ptr<std::vector<float>>, kTracks> clips{};
        std::array<bool, kTracks> has{}, rec{};
        int32_t samples = 0;
        int bars = 4;
        float tempo = 120.f;
        double pos = 0., tapePos = 0., rate = 1.;
        bool playing = false, stopping = false;
    } composition;
    int activeScene = -1, pendingScene = -1;
    double pendingAt = 0;   // audio frame of the next whole-bar boundary
    int sceneFadeFrames = 0;  // short scene start fade, no click on clip changes
    std::vector<int> songOrder;
    // Serialized in a single standalone .fm1p file for each project.
    struct ProjectTimeline {
        int32_t samples = 0;
        int bars = 4; float tempo = 120.f;
        double pos = 0., tapePos = 0., rate = 1.;
        bool playing = false, stopping = false;
        std::array<std::shared_ptr<std::vector<float>>, kTracks> clips{};
        std::array<bool, kTracks> has{};
    };
    struct ProjectSnapshot {
        ProjectTimeline current, compose;
        bool hasCompose = false;
        std::array<Scene, Session::kScenes> savedScenes;
        std::vector<int> arrangement;
        int active = -1, queued = -1, songStep = -1;
        double pendingAt = 0.;
        bool songPlaying = false;
        std::vector<uint8_t> appState;
        std::array<std::string, kTracks> samplePaths;
    };
    // Only a few primitive settings and shared immutable clip references are
    // copied on the real-time thread. No DSP delay buffers or large WAVs.
    struct LoopExportSnapshot {
        struct TrackData {
            std::shared_ptr<std::vector<float>> clip;
            bool has = false;
            float vol = 0.f, pan = 0.f, send[2] = {0, 0};
            int scSrc = -1;
            float scThrDb = -20.f, scRatio = 1.f;
            Lfo lfo{};
            float lfoPhase = 0.f, lfoSH = 0.f;
            TapeInsert::Settings tape{};
        };
        std::array<TrackData, kTracks> track{};
        int sampleRate = 0, loopSamples = 0;
        float tapeSpeed = 1.f, masterVolume = 0.9f;
        float returns[2] = {0.35f, 0.7f};
        float reverbSize = 0.7f, delaySeconds = 0.4f, delayFeedback = 0.4f;
    };
    std::atomic<int> projectOp{0};
    std::atomic<int> loopExportOp{0};
    mutable std::mutex projectMtx;
    std::shared_ptr<Session::LoadedProject> projectReady;
    bool projectRestored = false;
    int songStep = -1;
    bool songPlaying = false;

    int sr;
    std::vector<std::unique_ptr<Track>> tracks;
    Reverb reverb; TapeDelay delay;
    float masterVol = 0.9f, ret[2] = {0.35f, 0.7f};
    float reverbSize = 0.7f, delaySeconds = 0.4f, delayFeedback = 0.4f;
    bool offlineRender = false; // bypass visual-only peak scans for offline bounces
    int loopSrc = 1;   // what the loop records: 0 microphone, 1 the track's sound

    bool playing = false;
    // tape start / stop: the loops speed up from standstill / slow down to it instead of cutting in and out
    double rate = 1.0; bool stopping = false; std::vector<float> rateArr; std::vector<double> cpArr;
    // scrubbing with a finger: the tape follows the target position with a smoothed speed (forward and backward), like a hand on the reel
    bool scrubActive = false, scrubTail = false; double scrubPos = 0, scrubV = 0; std::atomic<double> scrubTarget{0};
    float bpm = 120.f; int loopBars = 4;
    double pos = 0; int32_t loopSamples = 0;
    std::atomic<float> tapeSpeed{1.f}; double tapePos = 0;   // ONE tape pointer for every loop clip, so they never drift apart
    // CUE / beat repeat uses a separate read head. The original transport runs underneath
    // so releasing PLAY drops back on the original beat (slip-style return).
    bool cueActive = false;
    int cueDivision = 4;                         // interleaved straight/triplet: 1/1,1/1T,...1/32T (0..11)
    double cueStart = 0.0, cuePhase = 0.0;
    float cueMix = 0.f;
    std::vector<double> cueCpArr;
    std::vector<float> cueMixArr, cueEdgeArr;

    // free take
    bool freeRec = false, freeArmed = false; int freeTrack = 0; std::vector<float> freeBuf;

    // microphone sampler
    std::string dataDir, sampleDir;
    bool sampling = false, armedWait = false, waitForSound = true;
    std::vector<float> sampleBuf; std::string samplePath; bool pending = false;
    mutable std::mutex sampleMtx;
    std::atomic<float> micThr{0.04f};
    static constexpr int kLive = 2048;
    static constexpr int kFreeBins = 18500;   // 10 ms per bin, a little over 3 minutes
    std::atomic<float> freePk[kFreeBins]; std::atomic<int> freeN{0}; float freeAcc = 0; int freeCnt = 0;   // the first take, for the timeline while it is recorded
    std::atomic<float> livePk[kLive]; std::atomic<int> liveN{0}; size_t livePos = 0; float liveAcc = 0; int liveCnt = 0;

    // loop overview, refreshed in the background by the audio thread
    std::atomic<float> a_lpk[kLoopBins]; std::atomic<float> a_tpk[kTracks][kLoopBins]; int lpkBin = 0;

    // commands from other threads, run at the start of each block
    std::mutex cmdMtx; std::vector<std::function<void()>> cmds, run;
    // background worker: sample loading, offline stretch, file writes, freeing
    std::thread worker; std::mutex wMtx; std::condition_variable wCv; std::deque<std::function<void()>> jobs; bool wStop = false;

    // status mirror (the audio thread writes)
    std::atomic<double> a_pos{0};
    std::atomic<bool> a_playing{false}, a_free{false}, a_freeArmed{false}, a_pending{false};
    std::atomic<float> a_master{0.f}, a_freeSec{0.f}, a_sampleSec{0.f}, a_inPeak{0.f}, a_bpm{120.f};
    std::atomic<uint32_t> a_cpu{0}, a_clip_mask{0}, a_cliprec_mask{0};
    std::atomic<int> a_bars{4}, a_sampleState{0}, a_sampleTrack{0}, a_sampleVersion{0}, a_sampleFails{0};
    std::atomic<int32_t> a_loop{0};
    std::atomic<uint32_t> a_sceneMask{0};
    std::atomic<uint8_t> a_sceneTracks[Session::kScenes];
    std::atomic<int> a_activeScene{-1}, a_queuedScene{-1}, a_songStep{-1}, a_songSize{0};
    std::atomic<bool> a_songPlaying{false}, a_arrangementLoaded{false};
    std::atomic<int> a_arrangementSize{0};
    std::atomic<int> a_arrangement[64];
    std::atomic<float> a_clipPos{0.f}, a_lfo[kTracks], a_scGain[kTracks], a_scLevel[kTracks];
    std::atomic<int> a_voiceN[kTracks]; std::atomic<float> a_voicePos[kTracks][kVoiceMax], a_voiceLvl[kTracks][kVoiceMax];

    std::vector<float> bus, tmp, sendBus[2], wet;

    explicit Impl(int rate, bool offline = false)
        : sr(rate), reverb(rate), delay(rate), offlineRender(offline) {
        for (int i = 0; i < kTracks; i++) tracks.emplace_back(new Track(rate));
        bus.resize(kBlock * 2); tmp.resize(kBlock * 2); wet.resize(kBlock * 2); sendBus[0].resize(kBlock * 2); sendBus[1].resize(kBlock * 2);
        for (auto& a : a_lpk) a = 0.f;
        for (auto& r : a_tpk) for (auto& a : r) a = 0.f;
        for (auto& a : a_lfo) a = 0.f;
        for (auto& a : a_scGain) a = 1.f;
        for (auto& a : a_scLevel) a = 0.f;
        for (auto& a : a_voiceN) a = 0;
        for (auto& a : a_arrangement) a = -1;
        for (auto& a : a_sceneTracks) a = 0;
        if (!offlineRender) worker = std::thread([this] {
            for (;;) {
                std::function<void()> j;
                { std::unique_lock<std::mutex> l(wMtx); wCv.wait(l, [this] { return wStop || !jobs.empty(); }); if (wStop && jobs.empty()) return; j = std::move(jobs.front()); jobs.pop_front(); }
                j();
            }
        });
    }
    ~Impl() { { std::lock_guard<std::mutex> l(wMtx); wStop = true; } wCv.notify_all(); if (worker.joinable()) worker.join(); }
    void job(std::function<void()> f) { { std::lock_guard<std::mutex> l(wMtx); jobs.push_back(std::move(f)); } wCv.notify_one(); }
    void post(std::function<void()> f) { std::lock_guard<std::mutex> l(cmdMtx); cmds.push_back(std::move(f)); }
    template <class T> void bury(std::unique_ptr<T> p) { if (p) { auto sp = std::make_shared<std::unique_ptr<T>>(std::move(p)); job([sp] { sp->reset(); }); } }   // freed on the worker, not in the audio callback

    void updateSceneMask() {
        uint32_t mask = 0;
        for (int i = 0; i < Session::kScenes; ++i) {
            uint8_t tracks = 0;
            for (int t = 0; t < kTracks; ++t)
                if (scenes[(size_t)i].has[(size_t)t]) tracks |= (uint8_t)(1u << t);
            a_sceneTracks[(size_t)i].store(tracks);
            if (scenes[(size_t)i].filled) mask |= 1u << i;
        }
        a_sceneMask.store(mask);
    }

    void retainComposition() {
        if (composition.valid) return;  // LOOP <-> SONG must not overwrite original
        if (freeRec || freeArmed) return;
        for (const auto& t : tracks) if (t->clipRec) return;
        composition.valid = true;
        composition.samples = loopSamples;
        composition.bars = loopBars; composition.tempo = bpm;
        composition.pos = pos; composition.tapePos = tapePos;
        composition.rate = rate;
        composition.playing = playing; composition.stopping = stopping;
        for (int i = 0; i < kTracks; ++i) {
            const auto& t = *tracks[(size_t)i];
            composition.clips[(size_t)i] = t.clip;
            composition.has[(size_t)i] = t.clipHas;
            composition.rec[(size_t)i] = t.clipRec;
        }
    }
    void restoreComposition() {
        // Cancel even a scene which was only queued; prevent it from firing
        // after EXIT/HOME/COMPOSE during the next audio callback.
        pendingScene = -1; songPlaying = false; songStep = -1;
        a_queuedScene = -1; a_songPlaying = false; a_songStep = -1;
        if (!composition.valid) return;
        const Composition saved = std::move(composition);
        composition = Composition{};
        freeRec = freeArmed = false; a_free = a_freeArmed = false;
        for (int i = 0; i < kTracks; ++i) {
            auto& t = *tracks[(size_t)i];
            releaseAll(t);
            t.clip = saved.clips[(size_t)i] ? saved.clips[(size_t)i]
                                           : std::make_shared<std::vector<float>>();
            t.clipHas = saved.has[(size_t)i];
            t.clipRec = saved.rec[(size_t)i]; t.recPrevOk = false;
        }
        loopSamples = saved.samples; a_loop = loopSamples;
        loopBars = saved.bars; a_bars = loopBars;
        bpm = saved.tempo; a_bpm = bpm;
        pos = saved.pos; tapePos = saved.tapePos; rate = saved.rate;
        playing = saved.playing; stopping = saved.stopping;
        activeScene = -1; a_activeScene = -1;
        scrubActive = scrubTail = false;
        cueActive = false; cueMix = 0.f;
        sceneFadeFrames = std::max(1, sr / 180);
    }

    // Change all 8 loop tracks together. Copy-on-write clips make this constant-time
    // even when a scene contains long samples. No pointers into unloaded files.
    void activateScene(int slot) {
        if (slot < 0 || slot >= Session::kScenes || !scenes[(size_t)slot].filled) return;
        const Scene& sc = scenes[(size_t)slot];
        if (sc.samples <= 0) return;
        for (int i = 0; i < kTracks; i++) {
            auto& t = *tracks[(size_t)i];
            releaseAll(t);
            t.clip = sc.has[(size_t)i] ? sc.clips[(size_t)i] : std::make_shared<std::vector<float>>();
            t.clipHas = sc.has[(size_t)i]; t.clipRec = false; t.recPrevOk = false;
        }
        loopSamples = sc.samples; a_loop = loopSamples;
        loopBars = sc.bars; a_bars = loopBars;
        bpm = sc.tempo; a_bpm = bpm;
        activeScene = slot; pendingScene = -1; a_activeScene = slot; a_queuedScene = -1;
        pos = tapePos = 0.0;
        playing = true; stopping = false; rate = 1.0;
        scrubActive = scrubTail = false;
        cueActive = false; cueMix = 0.f;
        sceneFadeFrames = std::max(1, sr / 180);  // about 5.5 ms
    }

    ProjectSnapshot captureProject(std::vector<uint8_t> app,
                                   std::array<std::string, kTracks> paths) {
        ProjectSnapshot s;
        s.appState = std::move(app);
        s.samplePaths = std::move(paths);
        s.savedScenes = scenes; s.arrangement = songOrder;
        s.active = activeScene; s.queued = pendingScene;
        s.songPlaying = songPlaying; s.songStep = songStep; s.pendingAt = pendingAt;
        auto& c = s.current;
        c.samples = loopSamples; c.bars = loopBars; c.tempo = bpm;
        c.pos = pos; c.tapePos = tapePos; c.rate = rate;
        c.playing = playing; c.stopping = stopping;
        for (int t = 0; t < kTracks; ++t) {
            c.clips[(size_t)t] = tracks[(size_t)t]->clip;
            c.has[(size_t)t] = tracks[(size_t)t]->clipHas;
        }
        s.hasCompose = composition.valid;
        if (s.hasCompose) {
            auto& d = s.compose;
            d.samples = composition.samples; d.bars = composition.bars;
            d.tempo = composition.tempo; d.pos = composition.pos;
            d.tapePos = composition.tapePos; d.rate = composition.rate;
            d.playing = composition.playing; d.stopping = composition.stopping;
            d.clips = composition.clips; d.has = composition.has;
        }
        return s; // COW; subsequent loop overdubs cannot change this audio snapshot
    }
    void installProject(ProjectSnapshot&& s) {
        freeRec = freeArmed = sampling = armedWait = false;
        a_free = a_freeArmed = false; a_sampleState = 0;
        cueActive = false; cueMix = 0.f;
        scrubActive = scrubTail = false; freeBuf.clear();
        const auto& c = s.current;
        loopSamples = c.samples; loopBars = c.bars; bpm = c.tempo;
        pos = c.pos; tapePos = c.tapePos; rate = c.rate;
        playing = c.playing; stopping = c.stopping;
        for (int t = 0; t < kTracks; ++t) {
            auto& tr = *tracks[(size_t)t];
            releaseAll(tr);
            tr.clip = c.has[(size_t)t] && c.clips[(size_t)t]
                    ? c.clips[(size_t)t] : std::make_shared<std::vector<float>>();
            tr.clipHas = c.has[(size_t)t]; tr.clipRec = false;
            tr.recPrevOk = false;
        }
        composition = Composition{};
        if (s.hasCompose) {
            composition.valid = true;
            composition.samples = s.compose.samples;
            composition.bars = s.compose.bars;
            composition.tempo = s.compose.tempo;
            composition.pos = s.compose.pos;
            composition.tapePos = s.compose.tapePos;
            composition.rate = s.compose.rate;
            composition.playing = s.compose.playing;
            composition.stopping = s.compose.stopping;
            composition.clips = std::move(s.compose.clips);
            composition.has = s.compose.has;
        }
        scenes = std::move(s.savedScenes); songOrder = std::move(s.arrangement);
        activeScene = s.active; pendingScene = s.queued;
        pendingAt = s.pendingAt; songPlaying = s.songPlaying; songStep = s.songStep;
        sceneFadeFrames = std::max(1, sr / 180);
        updateSceneMask();
        a_loop = loopSamples; a_bars = loopBars; a_bpm = bpm;
        a_activeScene = activeScene; a_queuedScene = pendingScene;
        a_arrangementLoaded = true;
        projectRestored = true; // asynchronous startup scene restore must not overwrite it
    }

    // Save/load whole scenes off the audio thread. The shared buffers cannot
    // change while the worker reads them: live overdubs use copy-on-write.
    void persistScenes() {
        if (dataDir.empty()) return;
        const auto copy = scenes;
        const auto order = songOrder;
        const int sampleRate = sr;
        const std::string dir = dataDir;
        job([copy, order, sampleRate, dir] {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(dir, ec);
            const auto temp = dir + "/fm1_scenes.tmp";
            const auto path = dir + "/fm1_scenes.dat";
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            auto put = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
            const uint32_t magic = 0x314E4353; // SCN1
            const uint32_t rate = (uint32_t)sampleRate;
            put(magic); put(rate);
            for (const auto& sc : copy) {
                const uint8_t filled = sc.filled ? 1 : 0;
                put(filled);
                if (!filled) continue;
                put(sc.samples); put(sc.tempo); put(sc.bars);
                for (int t = 0; t < kTracks; ++t) {
                    const auto& ptr = sc.clips[(size_t)t];
                    const uint8_t has = sc.has[(size_t)t] && ptr &&
                        ptr->size() == (size_t)sc.samples * 2 ? 1 : 0;
                    put(has);
                    if (has) out.write(reinterpret_cast<const char*>(ptr->data()),
                                       (std::streamsize)(ptr->size() * sizeof(float)));
                }
            }
            const uint32_t len = (uint32_t)std::min<size_t>(64, order.size());
            put(len);
            for (uint32_t i = 0; i < len; ++i) {
                const uint8_t slot = (uint8_t)order[i];
                put(slot);
            }
            out.flush();
            if (!out.good()) { out.close(); fs::remove(temp, ec); return; }
            out.close();
            fs::rename(temp, path, ec);
            if (ec) { fs::remove(path, ec); ec.clear(); fs::rename(temp, path, ec); }
            if (ec) fs::remove(temp, ec);
        });
    }
    void restoreScenes() {
        if (dataDir.empty()) return;
        const auto path = dataDir + "/fm1_scenes.dat";
        const int sampleRate = sr;
        job([this, path, sampleRate] {
            std::array<Scene, Session::kScenes> loaded;
            std::vector<int> order;
            std::ifstream in(path, std::ios::binary);
            bool ok = (bool)in;
            auto get = [&](auto& value) {
                in.read(reinterpret_cast<char*>(&value), sizeof(value));
                if (!in) ok = false;
            };
            if (ok) {
                uint32_t magic = 0, rate = 0; get(magic); get(rate);
                if (magic != 0x314E4353 || rate != (uint32_t)sampleRate) ok = false;
            }
            size_t totalFloats = 0;
            for (int i = 0; ok && i < Session::kScenes; ++i) {
                auto& sc = loaded[(size_t)i];
                uint8_t filled = 0; get(filled);
                if (!filled) continue;
                get(sc.samples); get(sc.tempo); get(sc.bars);
                if (sc.samples <= 0 || sc.samples > sampleRate * 180 ||
                    sc.tempo < 30.f || sc.tempo > 300.f ||
                    sc.bars < 1 || sc.bars > 32) { ok = false; break; }
                for (int t = 0; ok && t < kTracks; ++t) {
                    uint8_t has = 0; get(has);
                    if (!has) continue;
                    const size_t n = (size_t)sc.samples * 2;
                    totalFloats += n;
                    if (totalFloats > 100000000) { ok = false; break; }
                    auto ptr = std::make_shared<std::vector<float>>(n);
                    in.read(reinterpret_cast<char*>(ptr->data()), (std::streamsize)(n * sizeof(float)));
                    if (!in) { ok = false; break; }
                    sc.clips[(size_t)t] = std::move(ptr); sc.has[(size_t)t] = true;
                    sc.filled = true;
                }
            }
            if (ok) {
                uint32_t len = 0; get(len);
                if (len > 64) ok = false;
                for (uint32_t i = 0; ok && i < len; ++i) {
                    uint8_t slot = 0; get(slot);
                    if (slot < Session::kScenes) order.push_back(slot);
                }
            }
            post([this, ok, loaded = std::move(loaded), order = std::move(order)]() mutable {
                // If a user has already captured something, their new work wins.
                if (!projectRestored && a_sceneMask.load() == 0 && ok) {
                    scenes = std::move(loaded);
                    songOrder = std::move(order);
                    updateSceneMask();
                }
                a_arrangementLoaded = true;
            });
        });
    }

    // ---- notes ----
    void releaseAll(Track& t) {
        std::vector<Arp::Ev> evs; t.arp.clear(evs);
        t.pending.clear();
        if (t.sampler) t.sampler->allOff();
    }
    void liveNote(int ti, int note, int vel) {
        Track& t = *tracks[ti];
        if (freeArmed && ti == freeTrack && loopSrc == 1 && vel > 0) { freeArmed = false; a_freeArmed = false; freeRec = true; playing = true; pos = 0; }   // the take starts with the first note
        if (t.arpOn) { if (vel) t.arp.noteOn(note); else t.arp.noteOff(note); }
        else t.pending.push_back({note, vel});
    }

    // ---- free take ----
    void freeStart(int track) {
        for (auto& t : tracks) { releaseAll(*t); t->clip = std::make_shared<std::vector<float>>(); t->clipHas = false; t->clipRec = false; }
        loopSamples = 0; a_loop = 0;
        freeArmed = true; a_freeArmed = true; freeRec = false; freeTrack = track;
        freeBuf.clear(); freeBuf.reserve((size_t)sr * 2 * 30);
        freeN = 0; freeAcc = 0; freeCnt = 0;
        pos = 0; playing = false;
    }
    void freeStop() {
        if (freeArmed) { freeArmed = false; a_freeArmed = false; return; }   // cancelled before any sound
        if (!freeRec) return;
        freeRec = false;
        const double len = pos;
        if (len < 0.25 * sr) { pos = 0; freeBuf.clear(); return; }   // too short: treat as cancelled
        // bars and tempo that fit the take (shown to the user; the loop length is exactly the take)
        const double barSec = 240.0 / bpm;
        int n = std::min(32, std::max(1, (int)std::lround(len / sr / barSec)));
        float nb = (float)(n * 240.0 * sr / len);
        while (nb > 300.f && n > 1) { n--; nb = (float)(n * 240.0 * sr / len); }
        while (nb < 30.f && n < 32) { n++; nb = (float)(n * 240.0 * sr / len); }
        bpm = std::min(300.f, std::max(30.f, nb)); a_bpm = bpm; loopBars = n; a_bars = n;
        loopSamples = (int32_t)std::lround(len); a_loop = loopSamples;
        Track& ft = *tracks[freeTrack];
        if (!freeBuf.empty()) { freeBuf.resize((size_t)loopSamples * 2, 0.f); ft.clip = std::make_shared<std::vector<float>>(std::move(freeBuf)); ft.clipHas = true; }
        freeBuf = {};
        pos = 0; tapePos = 0;
    }
    void clearIfEmpty() {
        for (auto& t : tracks) if (t->clipHas) return;
        loopSamples = 0; a_loop = 0; pos = 0; tapePos = 0; cueActive = false; cueMix = 0.f;
    }

    // ---- samples ----
    static std::string baseName(const std::string& p) { size_t s = p.find_last_of("/\\"); return p.substr(s == std::string::npos ? 0 : s + 1); }
    void installNew(const std::string& path, int track, bool kit, const std::string& displayName = "") {
        auto sm = std::make_unique<Sampler>(sr);
        if (!sm->load(path)) { a_sampleFails++; a_sampleState = 0; return; }
        auto holder = std::make_shared<std::unique_ptr<Sampler>>(std::move(sm));
        post([this, holder, track, kit, displayName] {
            Track& t = *tracks[track];
            releaseAll(t);
            std::swap(t.sampler, *holder);
            applyModeParams(t);
            { std::lock_guard<std::mutex> l(t.infoMtx); t.label = displayName.empty() ? t.sampler->label() : displayName; t.peaks = t.sampler->peaks(); t.orig = t.sampler->original(); }
            (void)kit;
            bury(std::move(*holder));
            a_sampleTrack = track; a_sampleVersion++; a_sampleState = 0;
            startProcess(track);   // the stretch / pitch that was set for this track applies to the new sample too
        });
    }
    // audio thread: hand the mode parameters of a track to its sampler and publish the slices for the UI
    void applyModeParams(Track& t) {
        if (!t.sampler) return;
        t.sampler->setSliceThreshold(t.sliceThr); t.sampler->setKitPitch(t.kitSemis); t.sampler->setGrain(t.grainN, t.grainScatter); t.sampler->setPaul(t.paulStretch, t.paulWidth);
        auto f = t.sampler->sliceFractions();
        std::lock_guard<std::mutex> l(t.infoMtx); t.slices = std::move(f);
    }
    // audio thread: build the stretched copy of the track's range on the worker, then swap it in
    void startProcess(int ti) {
        Track& t = *tracks[ti];
        if (!t.sampler) return;
        const uint32_t gen = ++t.gen;
        auto orig = t.sampler->original();
        const float a = t.rangeA, b = t.rangeB, speed = t.speed, semis = t.semis;
        if (std::fabs(speed - 1.f) < 0.005f && std::fabs(semis) < 0.005f) { t.sampler->setProcessed(nullptr, 0.f, 1.f); return; }
        job([this, ti, gen, orig, a, b, speed, semis] {
            const int f0 = (int)(a * orig->frames), f1 = std::max(f0 + 64, (int)(b * orig->frames));
            std::vector<float> seg(orig->d.begin() + 2 * (size_t)f0, orig->d.begin() + 2 * (size_t)std::min(f1, orig->frames));
            auto out = std::make_shared<SampleBuf>();
            out->d = stretchPitch(seg, orig->sr, speed, semis); out->frames = (int)(out->d.size() / 2); out->sr = orig->sr;
            post([this, ti, gen, orig, a, b, out] {
                Track& t = *tracks[ti];
                if (t.gen == gen && t.sampler && t.sampler->original() == orig) t.sampler->setProcessed(out, a, b);
            });
        });
    }

    // ---- microphone sampler ----
    void liveReset() { liveN = 0; livePos = 0; liveAcc = 0; liveCnt = 0; }
    void liveFeed() {
        const int bin = sr / 20;
        for (; livePos < sampleBuf.size(); livePos++) {
            liveAcc = std::max(liveAcc, std::fabs(sampleBuf[livePos]));
            if (++liveCnt >= bin && liveN < kLive) { livePk[liveN.load()].store(liveAcc); liveN++; liveAcc = 0; liveCnt = 0; }
        }
    }
    // mono capture; while waiting for sound only a short look-ahead is kept, and the take starts when the level crosses the threshold
    void captureMono(const float* x, int n) {
        float pk = 0;
        for (int i = 0; i < n; i++) { const float m = 0.5f * (x[2 * i] + x[2 * i + 1]); sampleBuf.push_back(m); pk = std::max(pk, std::fabs(m)); }
        if (armedWait) {
            if (pk > micThr.load()) { armedWait = false; sampling = true; a_sampleState = 1; }
            else if (sampleBuf.size() > 2048) sampleBuf.erase(sampleBuf.begin(), sampleBuf.end() - 2048);
        }
        if (sampling) liveFeed();
    }
    // worker thread: trim, normalise, write the WAV; the result becomes the pending sample
    void finishSample(std::vector<float> st /*interleaved stereo*/) {
        const size_t frames = st.size() / 2;
        size_t a = 0, b = frames;
        while (a < frames && std::fabs(st[2 * a]) < 0.02f && std::fabs(st[2 * a + 1]) < 0.02f) a++;
        while (b > a && std::fabs(st[2 * (b - 1)]) < 0.01f && std::fabs(st[2 * (b - 1) + 1]) < 0.01f) b--;
        a = a > (size_t)(sr / 200) ? a - sr / 200 : 0; b = std::min(frames, b + (size_t)sr / 50);
        if (b <= a + (size_t)(sr / 50)) { a_sampleState = 0; return; }
        std::vector<float> o(st.begin() + 2 * a, st.begin() + 2 * b);
        float pk = 0; for (float v : o) pk = std::max(pk, std::fabs(v));
        if (pk > 1e-4f) { const float g = 0.9f / pk; for (float& v : o) v *= g; }
        const size_t fade = std::min<size_t>(o.size() / 4, (size_t)sr / 200 * 2);
        for (size_t i = 0; i < fade; i += 2) { const float k = (float)i / fade; o[i] *= k; o[i + 1] *= k; o[o.size() - 2 - i] *= k; o[o.size() - 1 - i] *= k; }
        char name[64]; snprintf(name, sizeof name, "/sample_%lld.wav", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const std::string folder = sampleDir.empty() ? dataDir : sampleDir + "/SAVED/RECORDINGS";
        std::error_code ec; std::filesystem::create_directories(folder, ec);
        const std::string path = folder + name;
        writeWav16(path, o, sr);
        { std::lock_guard<std::mutex> l(sampleMtx); samplePath = path; pending = true; a_pending = true; }
        a_sampleState = 0;
    }

    // ---- loop overview ----
    void updateLoopPeaks() {
        const int32_t L = loopSamples;
        for (int r = 0; r < 8; r++) {
            const int b = lpkBin; lpkBin = (lpkBin + 1) % kLoopBins;
            if (L <= 0) { a_lpk[b].store(0.f); for (int t = 0; t < kTracks; t++) a_tpk[t][b].store(0.f); continue; }
            const int32_t a = (int32_t)((int64_t)b * L / kLoopBins), e = (int32_t)((int64_t)(b + 1) * L / kLoopBins), step = std::max<int32_t>(1, (e - a) / 24);
            float mix = 0, tp[kTracks] = {};
            for (int32_t f = a; f < e; f += step) {
                float sum = 0;
                for (int t = 0; t < kTracks; t++) {
                    const auto& c = *tracks[(size_t)t]->clip;
                    if (!tracks[(size_t)t]->clipHas || c.size() != (size_t)L * 2) continue;
                    const float v = 0.5f * (c[(size_t)2 * f] + c[(size_t)2 * f + 1]); sum += v; tp[t] = std::max(tp[t], std::fabs(v));
                }
                mix = std::max(mix, std::fabs(sum));
            }
            a_lpk[b].store(mix); for (int t = 0; t < kTracks; t++) a_tpk[t][b].store(tp[t]);
        }
    }

    // ---- one block of `n` frames ----
    void block(float* out, const float* in, int n) {
        std::fill(out, out + n * 2, 0.f);
        for (auto& s : sendBus) std::fill(s.begin(), s.begin() + n * 2, 0.f);
        const size_t freeBase = freeBuf.size();
        if (freeRec && loopSrc == 1) freeBuf.resize(freeBase + (size_t)n * 2, 0.f);   // the track's sound is added below
        if (in) {
            float ip = 0;
            for (int i = 0; i < n * 2; i++) ip = std::max(ip, std::fabs(in[i]));
            a_inPeak.store(std::max(ip, a_inPeak.load() * 0.9f));
            if (freeArmed && loopSrc == 0 && ip > micThr.load()) { freeArmed = false; a_freeArmed = false; freeRec = true; playing = true; pos = 0; }   // first sound: the take starts
            if (freeRec && loopSrc == 0) freeBuf.insert(freeBuf.end(), in, in + n * 2);
            if (sampling || armedWait) captureMono(in, n);
        }
        const int32_t p0 = (int32_t)pos;
        const bool tl = playing && !freeRec && loopSamples > 0;
        if (!offlineRender) updateLoopPeaks();
        const double spd = tapeSpeed.load();
        if (!playing) { rate = 1.0; stopping = false; }
        bool ramp = false; double rateSum = n, tapeEnd = tapePos;
        const bool scrubbing = (scrubActive || scrubTail) && loopSamples > 0 && !freeRec;
        if (scrubActive && (loopSamples <= 0 || freeRec)) scrubActive = false;
        if (scrubbing) {   // the read position is driven by the finger: speed follows (target - position), smoothed, so there are no jumps and no clicks
            ramp = true; rateSum = 0;
            if ((int)rateArr.size() < n) { rateArr.resize((size_t)n); cpArr.resize((size_t)n); }
            const double L = (double)loopSamples, follow = 1.0 / (0.07 * sr), smooth = 1.0 - std::exp(-1.0 / (0.012 * sr));
            const double run = (playing && !stopping) ? spd : 0.0;   // after the finger lifts the tape goes back to the transport speed (or stands still)
            for (int i = 0; i < n; i++) {
                double want = scrubActive ? std::max(-6.0, std::min(6.0, (scrubTarget.load() - scrubPos) * follow)) : run;
                if (scrubActive && std::fabs(scrubTarget.load() - scrubPos) < 2.0) want = 0.0;
                scrubV += (want - scrubV) * smooth;
                cpArr[(size_t)i] = scrubPos;
                rateArr[(size_t)i] = (float)std::min(1.0, std::sqrt(std::fabs(scrubV)));   // standing tape is silent, a slow one quiet
                scrubPos += scrubV;
                if (scrubActive) scrubPos = std::max(0.0, std::min(L - 1.0, scrubPos)); else { scrubPos = std::fmod(scrubPos, L); if (scrubPos < 0) scrubPos += L; }
            }
            tapeEnd = scrubPos;
            if (!scrubActive && std::fabs(scrubV - run) < 0.01) { scrubTail = false; scrubV = 0; rate = 1.0; if (!playing) { rateArr[(size_t)n - 1] = 0.f; } }
        } else if (tl && (stopping || rate < 1.0)) {   // the tape runs up to speed / down to standstill: a per-sample rate and read position
            ramp = true; rateSum = 0;
            if ((int)rateArr.size() < n) { rateArr.resize((size_t)n); cpArr.resize((size_t)n); }
            const double step = stopping ? 1.0 / (0.7 * sr) : 1.0 / (0.45 * sr);
            double acc = tapePos;
            for (int i = 0; i < n; i++) {
                rate = stopping ? std::max(0.0, rate - step) : std::min(1.0, rate + step);
                rateArr[(size_t)i] = (float)rate; rateSum += rate; cpArr[(size_t)i] = acc;
                acc = std::fmod(acc + spd * rate, (double)loopSamples);
            }
            tapeEnd = acc;
        }
        std::array<float, kBlock> sceneGain{};
        const int fadeTotal = std::max(1, sr / 180);
        for (int i = 0; i < n; ++i) {
            float gain = sceneFadeFrames > 0 ? 1.f - sceneFadeFrames / (float)fadeTotal : 1.f;
            if (sceneFadeFrames > 0) --sceneFadeFrames;
            // Fade out a few milliseconds before a quantised switch too.
            double until = -1;
            if (songPlaying) until = loopSamples - (pos + i);
            else if (pendingScene >= 0) until = pendingAt - (pos + i);
            if (until >= 0 && until < fadeTotal) gain = std::min(gain, (float)(until / fadeTotal));
            sceneGain[(size_t)i] = std::max(0.f, std::min(1.f, gain));
        }
        const bool locked = !ramp && std::fabs(spd - 1.0) < 0.01;
        // All eight clips read from the same CUE head, generated only once per
        // audio frame (not once per track). 1/1 is one 4/4 bar at the session BPM.
        const bool cueAudible = (cueActive || cueMix > 0.f) && tl && !scrubbing;
        if (cueAudible) {
            if ((int)cueCpArr.size() < n) {
                cueCpArr.resize((size_t)n); cueMixArr.resize((size_t)n); cueEdgeArr.resize((size_t)n);
            }
            // Every binary duration has a triplet alternative (two thirds
            // of the straight note): 1/4T is 1/6 of a 4/4 bar.
            const double straight = (240.0 * sr / std::max(1.f, bpm))
                                  / (double)(1 << (cueDivision / 2));
            const double length = std::max(1.0, std::min((double)loopSamples,
                straight * ((cueDivision & 1) ? 2.0 / 3.0 : 1.0)));
            const double fade = std::max(1.0, std::min(0.004 * sr, length * 0.12));
            cuePhase = std::fmod(cuePhase, length);
            const float blendStep = 1.f / std::max(1, (int)(0.006f * sr));
            for (int i = 0; i < n; ++i) {
                cueMix = clampf(cueMix + (cueActive ? blendStep : -blendStep), 0.f, 1.f);
                cueCpArr[(size_t)i] = std::fmod(cueStart + cuePhase, (double)loopSamples);
                cueMixArr[(size_t)i] = cueMix;
                // A 4 ms wrap crossfade removes discontinuities on sharp beat rolls.
                cueEdgeArr[(size_t)i] = (float)std::max(0.0, (cuePhase - (length - fade)) / fade);
                if (cueActive) {
                    cuePhase += 1.0;
                    if (cuePhase >= length) cuePhase -= length;
                }
            }
        }
        const double tb = loopSamples > 0 ? (locked ? (double)(p0 % loopSamples) : tapePos) : 0.0;
        if (loopSamples > 0) tapePos = ramp ? tapeEnd : !tl ? (double)(p0 % loopSamples) : locked ? (double)((p0 + n) % loopSamples) : std::fmod(tb + n * spd, (double)loopSamples);
        bool anyEvents;
        std::vector<Arp::Ev> evs;
        for (int ti = 0; ti < kTracks; ti++) {
            Track& t = *tracks[(size_t)ti];
            if (!t.sampler && !t.clipHas && !t.clipRec && t.pending.empty()) { t.peak.store(t.peak.load() * 0.9f); t.scEnv *= 0.8f; t.scGain = 1.f; a_voiceN[ti].store(0); continue; }
            std::fill(bus.begin(), bus.begin() + n * 2, 0.f);
            // LFO: one value per block (about 170 Hz)
            float lfoGain = 1.f, lfoPan = 0.f, bend = 0.f; bool filterOn = false;
            if (t.lfo.depth > 0.001f) {
                t.lfoPhase += t.lfo.rate * n / sr; if (t.lfoPhase >= 1.f) { t.lfoPhase -= std::floor(t.lfoPhase); t.lfoSH = (float)(rand() % 2001 - 1000) / 1000.f; }
                const float ph = t.lfoPhase;
                const float v = t.lfo.shape == 0 ? std::sin(2.f * kPi * ph) : t.lfo.shape == 1 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : t.lfo.shape == 2 ? (ph < 0.5f ? 1.f : -1.f) : t.lfoSH;
                t.lfoVal = v;
                if (t.lfo.target == 0) bend = v * t.lfo.depth * t.lfo.depth * 12.f;   // +-12 semitones at full depth
                else if (t.lfo.target == 1) { filterOn = true; t.filt.set(20.f * std::pow(1000.f, clampf(t.lfo.base + 0.5f * v * t.lfo.depth, 0.f, 1.f)), (float)sr); }
                else if (t.lfo.target == 2) lfoGain = 1.f - t.lfo.depth * (0.5f - 0.5f * v);
                else lfoPan = v * t.lfo.depth;
            } else t.lfoVal = 0.f;
            if (filterOn != t.filtOn) { t.filtOn = filterOn; t.filt.reset(); }
            a_lfo[ti].store(t.lfoVal);
            a_scGain[ti].store(t.scGain); a_scLevel[ti].store(t.scEnv);
            // notes of the block: live ones at the start, the arpeggiator's with sample accurate offsets
            evs.clear();
            for (auto& nt : t.pending) evs.push_back({0, nt.note, nt.vel});
            t.pending.clear();
            if (t.arpOn) t.arp.run(n, bpm, sr, evs);
            std::stable_sort(evs.begin(), evs.end(), [](const Arp::Ev& a, const Arp::Ev& b) { return a.offset < b.offset; });
            if (t.sampler) {
                t.sampler->setBend(bend);
                int cur = 0;
                for (auto& e : evs) {
                    const int off = std::min(n, std::max(cur, e.offset));
                    if (off > cur) { t.sampler->render(bus.data() + 2 * cur, off - cur); cur = off; }
                    if (e.vel) t.sampler->noteOn(e.note, e.vel); else t.sampler->noteOff(e.note);
                }
                if (cur < n) t.sampler->render(bus.data() + 2 * cur, n - cur);
                float vp[kVoiceMax], vl[kVoiceMax]; const int vn = t.sampler->voicePositions(vp, kVoiceMax, vl);
                for (int i = 0; i < vn; i++) { a_voicePos[ti][i].store(vp[i]); a_voiceLvl[ti][i].store(vl[i]); }
                a_voiceN[ti].store(vn);
            } else a_voiceN[ti].store(0);
            if (t.filtOn) for (int i = 0; i < n; i++) { bus[2 * i] = t.filt.process(0, bus[2 * i]); bus[2 * i + 1] = t.filt.process(1, bus[2 * i + 1]); }
            // the sound of the track itself (before the loop is added), for the free take
            if (freeRec && loopSrc == 1 && ti == freeTrack) for (int i = 0; i < n * 2; i++) freeBuf[freeBase + (size_t)i] += bus[(size_t)i];
            if (tl || scrubbing) {
                for (int i = 0; i < n; i++) {
                    const int32_t k0 = (p0 + i) % loopSamples;
                    const double cp = ramp ? cpArr[(size_t)i] : locked ? (double)k0 : std::fmod(tb + i * spd, (double)loopSamples);
                    const float lg = ramp ? rateArr[(size_t)i] : 1.f;   // while the tape starts / stops its level follows the speed
                    const float src0 = bus[2 * i], src1 = bus[2 * i + 1];
                    const int32_t k = (int32_t)cp % loopSamples;
                    if (t.clipHas) {
                        auto& c = *t.clip;
                        // Linear interpolation permits a fractional CUE start and avoids
                        // the octave/pitch artifacts of rounded read pointers.
                        auto sampleAt = [&](double at, int ch) {
                            const int32_t j = (int32_t)at % loopSamples;
                            const int32_t j1 = (j + 1) % loopSamples;
                            const float frac = (float)(at - std::floor(at));
                            const float a = c[(size_t)2 * j + ch], b = c[(size_t)2 * j1 + ch];
                            return a + (b - a) * frac;
                        };
                        float baseL = sampleAt(cp, 0), baseR = sampleAt(cp, 1);
                        if (cueAudible && cueMixArr[(size_t)i] > 0.f) {
                            const float edge = cueEdgeArr[(size_t)i];
                            const double q = cueCpArr[(size_t)i], anchor = std::fmod(cueStart, (double)loopSamples);
                            const float qL = sampleAt(q, 0) * (1.f - edge) + sampleAt(anchor, 0) * edge;
                            const float qR = sampleAt(q, 1) * (1.f - edge) + sampleAt(anchor, 1) * edge;
                            const float mix = cueMixArr[(size_t)i];
                            baseL += (qL - baseL) * mix;
                            baseR += (qR - baseR) * mix;
                        }
                        bus[2 * i] += baseL * lg * sceneGain[(size_t)i];
                        bus[2 * i + 1] += baseR * lg * sceneGain[(size_t)i];
                    }
                    // Never overdub stuttered audio into the original clip.
                    if (t.clipRec && !ramp && !cueAudible) {
                        if (!t.clip.unique()) t.clip = std::make_shared<std::vector<float>>(*t.clip);
                        if (t.clip->size() != (size_t)loopSamples * 2) t.clip->assign((size_t)loopSamples * 2, 0.f);
                        auto& c = *t.clip;
                        // recording works at any tape speed, as the head of a tape machine: the input lands on the moving tape position.
                        // Slower than 1x the input is spread over the two neighbouring cells weighted by the speed (no pile-up);
                        // faster than 1x the cells the tape skipped since the last sample are filled by interpolating the input (no holes).
                        const float il = loopSrc == 0 && in ? in[2 * i] : loopSrc == 1 ? src0 : 0.f, ir = loopSrc == 0 && in ? in[2 * i + 1] : loopSrc == 1 ? src1 : 0.f;
                        if (locked) { c[(size_t)2 * k] += il; c[(size_t)2 * k + 1] += ir; t.recPrevOk = false; }
                        else if (spd >= 1.0) {
                            if (t.recPrevOk) {
                                const double p = t.recPrevCp; double cur = cp; if (cur < p) cur += (double)loopSamples;
                                for (int64_t cell = (int64_t)std::floor(p) + 1; cell <= (int64_t)std::floor(cur); cell++) {
                                    const float u = (float)((cell - p) / (cur - p)); const size_t idx = (size_t)(cell % loopSamples);
                                    c[2 * idx] += t.recPrevL + (il - t.recPrevL) * u; c[2 * idx + 1] += t.recPrevR + (ir - t.recPrevR) * u;
                                }
                            } else { c[(size_t)2 * k] += il; c[(size_t)2 * k + 1] += ir; }
                            t.recPrevCp = cp; t.recPrevL = il; t.recPrevR = ir; t.recPrevOk = true;
                        } else {
                            const float fr = (float)(cp - std::floor(cp)), w = (float)spd; const size_t k1 = (size_t)((k + 1) % loopSamples);
                            c[(size_t)2 * k] += il * w * (1.f - fr); c[(size_t)2 * k + 1] += ir * w * (1.f - fr);
                            c[2 * k1] += il * w * fr; c[2 * k1 + 1] += ir * w * fr;
                            t.recPrevOk = false;
                        }
                        t.clipHas = true;
                    } else t.recPrevOk = false;
                }
            }
            t.tape.process(bus.data(), n);
            {   // this track's level for the others (peak with a ~120 ms release) ...
                float bp = 0.f; for (int i = 0; i < n * 2; i++) bp = std::max(bp, std::fabs(bus[(size_t)i]));
                t.scEnv = std::max(bp, t.scEnv * std::exp(-(float)n / (0.12f * sr)));
            }
            // ... and the gain the sidechain asks for: over the threshold the level is pulled down by (over)(1 - 1/ratio) dB; fast attack, slow release
            float scTarget = 1.f;
            if (t.scSrc >= 0 && t.scSrc != ti && t.scRatio > 1.01f) {
                const float lvDb = 20.f * std::log10(std::max(1e-5f, tracks[(size_t)t.scSrc]->scEnv)), over = lvDb - t.scThrDb;
                if (over > 0.f) scTarget = std::pow(10.f, -over * (1.f - 1.f / t.scRatio) / 20.f);
            }
            const float scFrom = t.scGain; t.scGain += (scTarget - t.scGain) * (scTarget < t.scGain ? 0.7f : 0.08f);
            const float g = t.vol * lfoGain;
            const float ang = (clampf(t.pan + lfoPan, -1.f, 1.f) * 0.5f + 0.5f) * 1.5707963f;
            const float gl = g * std::cos(ang) * 1.4142f, gr = g * std::sin(ang) * 1.4142f;
            float pk = 0.f;
            for (int i = 0; i < n; i++) {
                const float sc = scFrom + (t.scGain - scFrom) * (float)(i + 1) / n;
                const float l = bus[2 * i] * gl * sc, r = bus[2 * i + 1] * gr * sc;
                out[2 * i] += l; out[2 * i + 1] += r;
                for (int sd = 0; sd < 2; sd++) if (t.send[sd] > 0.f) { sendBus[sd][2 * i] += l * t.send[sd]; sendBus[sd][2 * i + 1] += r * t.send[sd]; }
                pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
            }
            t.peak.store(std::max(pk, t.peak.load() * 0.92f));
        }
        (void)anyEvents;
        if (freeRec && freeBuf.size() >= freeBase + (size_t)n * 2) {
            const int bin = std::max(1, sr / 100);
            for (int i = 0; i < n; i++) {
                freeAcc = std::max(freeAcc, std::max(std::fabs(freeBuf[freeBase + 2 * (size_t)i]), std::fabs(freeBuf[freeBase + 2 * (size_t)i + 1])));
                if (++freeCnt >= bin && freeN < kFreeBins) { freePk[freeN.load()].store(freeAcc); freeN++; freeAcc = 0; freeCnt = 0; }
            }
        }
        a_clipPos.store(loopSamples > 0 ? (float)((cueAudible && cueActive ? cueCpArr[(size_t)n - 1] : tapePos) / loopSamples) : 0.f);
        reverb.process(sendBus[0].data(), wet.data(), n);
        for (int i = 0; i < n * 2; i++) out[i] += wet[(size_t)i] * ret[0];
        delay.process(sendBus[1].data(), wet.data(), n);
        for (int i = 0; i < n * 2; i++) out[i] += wet[(size_t)i] * ret[1];
        float mp = 0.f;
        for (int i = 0; i < n * 2; i++) { const float v = std::tanh(out[i] * masterVol); out[i] = v; mp = std::max(mp, std::fabs(v)); }
        a_master.store(std::max(mp, a_master.load() * 0.92f));
        if (playing) {
            pos += rateSum;
            if (freeRec) a_freeSec = (float)(pos / sr);
            else if (loopSamples > 0 && pos >= loopSamples) pos -= loopSamples;
        }
        if (scrubbing) { pos = tapeEnd; }
        if (stopping && !scrubbing && (rate <= 0.0 || !tl)) { playing = false; stopping = false; rate = 1.0; for (auto& t : tracks) releaseAll(*t); }
        if (sampling) a_sampleSec = (float)sampleBuf.size() / sr;
    }
};

Session::Session(int sampleRate) : d(new Impl(sampleRate)) {}
Session::~Session() = default;

void Session::process(float* out, const float* in, int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> l(d->cmdMtx, std::try_to_lock);
        if (l.owns_lock() && !d->cmds.empty()) d->run.swap(d->cmds);
    }
    for (auto& f : d->run) f();
    d->run.clear();
    for (int done = 0; done < frames;) {
        int n = std::min(kBlock, frames - done);
        const bool timeline = d->playing && !d->stopping && !d->freeRec && d->loopSamples > 0;
        if (timeline) {
            // Split precisely on a bar/scene boundary instead of waiting for a 256-frame block.
            const double next = d->songPlaying ? (double)d->loopSamples :
                d->pendingScene >= 0 ? d->pendingAt : (double)d->loopSamples;
            if (d->songPlaying || d->pendingScene >= 0)
                n = std::max(1, std::min(n, (int)std::ceil(std::max(1.0, next - d->pos - 1e-7))));
        }
        const double oldPos = d->pos;
        d->block(out + 2 * done, in ? in + 2 * done : nullptr, n);
        done += n;
        if (timeline && d->songPlaying && d->loopSamples > 0 &&
            oldPos + n >= d->loopSamples - 0.51) {
            if (d->songStep + 1 < (int)d->songOrder.size()) {
                ++d->songStep;
                d->activateScene(d->songOrder[(size_t)d->songStep]);
            } else {
                d->songPlaying = false; d->playing = false; d->stopping = false;
                d->pos = d->tapePos = 0;
                for (auto& track : d->tracks) d->releaseAll(*track);
            }
        } else if (timeline && !d->songPlaying && d->pendingScene >= 0 &&
                   oldPos + n >= d->pendingAt - 0.51) {
            d->activateScene(d->pendingScene);
        }
    }
    d->a_pos = d->pos; d->a_playing = d->playing && !d->stopping; d->a_free = d->freeRec;
    uint32_t cm = 0, rm = 0;
    for (int i = 0; i < kTracks; i++) { cm |= (uint32_t)d->tracks[(size_t)i]->clipHas << i; rm |= (uint32_t)d->tracks[(size_t)i]->clipRec << i; }
    d->a_clip_mask = cm; d->a_cliprec_mask = rm;
    d->a_activeScene = d->activeScene; d->a_queuedScene = d->pendingScene;
    d->a_songPlaying = d->songPlaying; d->a_songStep = d->songStep;
    d->a_songSize = (int)d->songOrder.size();
    d->a_arrangementSize = (int)std::min<size_t>(64, d->songOrder.size());
    for (int i = 0; i < d->a_arrangementSize.load(); ++i)
        d->a_arrangement[(size_t)i] = d->songOrder[(size_t)i];
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(), budget = frames * 1e6 / d->sr;
    d->a_cpu = (d->a_cpu.load() * 7 + (uint32_t)(1000.0 * us / budget)) / 8;
}

// ---- transport and notes ----
void Session::play(bool on) {
    d->post([this, on] {
        if (!on) {
            if (d->freeRec || d->freeArmed) d->freeStop();
            d->songPlaying = false; d->pendingScene = -1;
            if (d->playing) d->stopping = true;   // the tape slows down to standstill (block() ends it: playing = false, notes released)
        } else {
            d->stopping = false;
            if (!d->playing) { d->playing = true; d->rate = d->loopSamples > 0 ? 0.0 : 1.0; }   // the tape runs up from standstill
        }
    });
}
void Session::noteOn(int t, int note, int vel) { if (t >= 0 && t < kTracks) d->post([=] { d->liveNote(t, note, std::max(1, vel)); }); }
void Session::noteOff(int t, int note) { if (t >= 0 && t < kTracks) d->post([=] { d->liveNote(t, note, 0); }); }

// ---- sampler ----
void Session::loadSamplePath(const std::string& path, bool kit, int track,
                             const std::string& displayName) {
    if (track < 0 || track >= kTracks || !std::filesystem::exists(path)) return;
    d->a_sampleState = 2;
    d->job([this, path, track, kit, displayName] {
        d->installNew(path, track, kit, displayName);
    });
}
void Session::setMode(int t, int m) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setMode(m); }); }
void Session::setEnv(int t, float a, float dc, float s, float r) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setEnv(a, dc, s, r); }); }
void Session::setRoot(int t, float note) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setRoot(note); }); }
float Session::detectRoot(int t) const {
    if (t < 0 || t >= kTracks) return -1.f;
    std::shared_ptr<const SampleBuf> o; { std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx); o = d->tracks[(size_t)t]->orig; }
    if (!o) return -1.f;
    const float hz = detectPitchHz(*o);
    return hz > 20.f ? 69.f + 12.f * std::log2(hz / 440.f) : -1.f;
}
void Session::setSampleRange(int t, float a, float b) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.rangeA = clampf(a, 0.f, 1.f); tr.rangeB = clampf(b, 0.f, 1.f); if (tr.sampler) { tr.sampler->setRange(a, b); d->applyModeParams(tr); } });
}
void Session::setSliceThreshold(int t, float v) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.sliceThr = clampf(v, 0.f, 1.f); d->applyModeParams(tr); }); }
void Session::setKitPitch(int t, float s) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.kitSemis = s; d->applyModeParams(tr); }); }
void Session::setGrain(int t, int n, float sc) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.grainN = n; tr.grainScatter = sc; d->applyModeParams(tr); }); }
void Session::setPaul(int t, float st, float w) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.paulStretch = st; tr.paulWidth = w; d->applyModeParams(tr); }); }
std::vector<float> Session::sliceFractions(int t) const {
    if (t < 0 || t >= kTracks) return {};
    const auto& tr = *d->tracks[(size_t)t];
    std::lock_guard<std::mutex> l(tr.infoMtx);
    return tr.slices;
}
void Session::setStretch(int t, float speed, float semis, bool commit) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.speed = speed; tr.semis = semis; if (commit) d->startProcess(t); });
}
void Session::setLfo(int t, const Lfo& l) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->lfo = l; }); }
void Session::setArp(int t, bool on, int rate, int pattern, int octaves, float gate) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] {
        auto& tr = *d->tracks[(size_t)t];
        tr.arp.setup(rate, pattern, octaves, gate);
        if (on != tr.arpOn) { d->releaseAll(tr); tr.arpOn = on; }
    });
}
void Session::setTape(int t, float sat, float tone, float wobble, float hiss, float mix, float bits) {
    if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->tape.setParams(sat, tone, wobble, hiss, mix, bits); });
}

// ---- mixer ----
void Session::setVolume(int t, float v) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->vol = clampf(v, 0.f, 1.5f); }); }
void Session::setPan(int t, float p) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->pan = clampf(p, -1.f, 1.f); }); }
void Session::setSidechain(int t, int src, float thresholdDb, float ratio) {
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.scSrc = (src >= 0 && src < kTracks) ? src : -1; tr.scThrDb = clampf(thresholdDb, -60.f, 0.f); tr.scRatio = clampf(ratio, 1.f, 40.f); if (tr.scSrc < 0) tr.scGain = 1.f; });
}
void Session::setSend(int t, int w, float l) { if (t >= 0 && t < kTracks && (w == 0 || w == 1)) d->post([=] { d->tracks[(size_t)t]->send[w] = clampf(l, 0.f, 1.f); }); }
void Session::setMasterVolume(float v) { d->post([=] { d->masterVol = clampf(v, 0.f, 1.5f); }); }
void Session::setReverb(float size) { d->post([=] {
    d->reverbSize = clampf(size, 0.f, 1.f);
    d->reverb.setSize(d->reverbSize);
}); }
void Session::setDelay(float sec, float fb) { d->post([=] {
    d->delaySeconds = clampf(sec, 0.02f, 3.f);
    d->delayFeedback = clampf(fb, 0.f, 0.95f);
    d->delay.setTime(d->delaySeconds);
    d->delay.setFeedback(d->delayFeedback);
}); }
void Session::setRetLevel(int w, float v) { d->post([=] { d->ret[w & 1] = clampf(v, 0.f, 1.5f); }); }

// ---- looper ----
void Session::freeRecord(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { if (on) d->freeStart(t); else d->freeStop(); }); }
void Session::clipRecord(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (on && !tr.clip.unique()) tr.clip = std::make_shared<std::vector<float>>(*tr.clip); tr.clipRec = on; }); }
void Session::clipClear(int t) {
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.clipRec = false; tr.clip = std::make_shared<std::vector<float>>(); tr.clipHas = false; d->clearIfEmpty(); });
}
void Session::clipClearAll() {
    d->post([this] { for (auto& t : d->tracks) { t->clipRec = false; t->clip = std::make_shared<std::vector<float>>(); t->clipHas = false; } d->clearIfEmpty(); });
}
void Session::loopDouble() {
    d->post([this] {
        const int32_t L = d->loopSamples;
        if (L <= 0 || (int64_t)L * 2 > (int64_t)d->sr * 180) return;
        for (auto& t : d->tracks) {
            if (t->clipHas && t->clip->size() == (size_t)L * 2) {
                if (!t->clip.unique()) t->clip = std::make_shared<std::vector<float>>(*t->clip);
                auto& c = *t->clip; c.resize((size_t)L * 4); std::copy(c.begin(), c.begin() + (size_t)L * 2, c.begin() + (size_t)L * 2);
            } else { t->clip = std::make_shared<std::vector<float>>(); t->clipHas = false; }
        }
        d->loopSamples = L * 2; d->a_loop = d->loopSamples;
        d->loopBars = std::min(32, d->loopBars * 2); d->a_bars = d->loopBars;
    });
}
void Session::loopHalve() {
    d->post([this] {
        const int32_t L = d->loopSamples, h = L / 2;
        if (L <= 0 || h < d->sr / 4) return;
        for (auto& t : d->tracks) if (t->clipHas && t->clip->size() == (size_t)L * 2) {
            if (!t->clip.unique()) t->clip = std::make_shared<std::vector<float>>(*t->clip);
            t->clip->resize((size_t)h * 2);
        }
        d->loopSamples = h; d->a_loop = h;
        d->loopBars = std::max(1, d->loopBars / 2); d->a_bars = d->loopBars;
        if (d->pos >= h) d->pos = std::fmod(d->pos, (double)h);
        if (d->tapePos >= h) d->tapePos = std::fmod(d->tapePos, (double)h);
    });
}
void Session::scrubBegin(float f) {
    d->post([this, f] {
        if (d->loopSamples <= 0 || d->freeRec) return;
        if (!d->scrubActive && !d->scrubTail) { d->scrubPos = d->tapePos; d->scrubV = d->playing && !d->stopping ? d->rate * d->tapeSpeed.load() : 0.0; }
        if (d->stopping) { d->playing = false; d->stopping = false; }
        d->scrubActive = true; d->scrubTail = false; d->scrubTarget.store(clampf(f, 0.f, 0.9999f) * d->loopSamples);
    });
}
void Session::scrubMove(float f) { d->scrubTarget.store((double)clampf(f, 0.f, 0.9999f) * d->a_loop.load()); }
void Session::scrubEnd() { d->post([this] { if (d->scrubActive) { d->scrubActive = false; d->scrubTail = true; } }); }
void Session::seek(float f) {
    d->post([this, f] {
        if (d->loopSamples <= 0 || d->freeRec) return;
        const double p = clampf(f, 0.f, 0.9999f) * d->loopSamples;
        d->pos = p; d->tapePos = p;
    });
}
void Session::setCue(bool on, int division) {
    d->post([this, on, division] {
        d->cueDivision = std::min(11, std::max(0, division));
        if (on && d->loopSamples > 0 && !d->freeRec && !d->freeArmed) {
            if (!d->cueActive) {
                d->cueStart = d->tapePos;
                d->cuePhase = 0.0;
                d->cueActive = true;
            }
            if (!d->playing || d->stopping) { d->playing = true; d->stopping = false; d->rate = 1.0; }
        } else {
            d->cueActive = false;   // the audio thread blends back to the running transport
        }
    });
}
void Session::captureScene(int slot, uint8_t tracks) {
    if (slot < 0 || slot >= kScenes) return;
    d->post([this, slot, tracks] {
        if (d->loopSamples <= 0 || !tracks || d->freeArmed || d->freeRec) return;
        for (const auto& t : d->tracks) if (t->clipRec) return;
        auto& sc = d->scenes[(size_t)slot];
        sc = Impl::Scene{};
        sc.samples = d->loopSamples; sc.tempo = d->bpm; sc.bars = d->loopBars;
        for (int i = 0; i < kTracks; ++i) {
            const auto& t = *d->tracks[(size_t)i];
            sc.has[(size_t)i] = !!(tracks & (1u << i)) && t.clipHas &&
                t.clip && t.clip->size() == (size_t)d->loopSamples * 2;
            if (sc.has[(size_t)i]) {
                sc.clips[(size_t)i] = t.clip;
                sc.filled = true;
            }
        }
        d->updateSceneMask();
        d->persistScenes();
    });
}
void Session::clearScene(int slot) {
    if (slot < 0 || slot >= kScenes) return;
    d->post([this, slot] {
        d->scenes[(size_t)slot] = Impl::Scene{};
        if (d->pendingScene == slot) d->pendingScene = -1;
        if (d->activeScene == slot) d->activeScene = -1;
        if (d->songPlaying) { d->songPlaying = false; d->songStep = -1; }
        d->updateSceneMask();
        d->persistScenes();
    });
}
void Session::launchScene(int slot) {
    if (slot < 0 || slot >= kScenes) return;
    d->post([this, slot] {
        if (!d->scenes[(size_t)slot].filled || d->freeRec || d->freeArmed) return;
        for (const auto& t : d->tracks) if (t->clipRec) return;
        d->retainComposition();
        d->songPlaying = false;
        d->songStep = -1;
        if (!d->playing || d->stopping || d->loopSamples <= 0) {
            d->activateScene(slot);
        } else {
            d->pendingScene = slot;
            const double bar = std::max(1.0, d->loopSamples / (double)std::max(1, d->loopBars));
            const double next = (std::floor(d->pos / bar) + 1.0) * bar;
            d->pendingAt = std::min((double)d->loopSamples, next);
        }
    });
}
void Session::startSong(const std::vector<int>& order) {
    d->post([this, order] {
        if (d->freeRec || d->freeArmed) return;
        for (const auto& t : d->tracks) if (t->clipRec) return;
        std::vector<int> valid;
        for (int slot : order) if (slot >= 0 && slot < kScenes &&
               d->scenes[(size_t)slot].filled && valid.size() < 64) valid.push_back(slot);
        if (valid.empty()) return;
        d->retainComposition();
        d->songOrder = std::move(valid); d->songStep = 0; d->songPlaying = true;
        d->activateScene(d->songOrder[0]);
    });
}
void Session::beginPerformanceMode() {
    d->post([this] { d->retainComposition(); });
}
void Session::restoreComposition() {
    d->post([this] { d->restoreComposition(); });
}
void Session::cancelSceneLaunch() {
    d->post([this] {
        d->pendingScene = -1;
        d->a_queuedScene = -1;
        d->songPlaying = false;
    });
}
void Session::stopSong() {
    d->post([this] {
        d->songPlaying = false; d->songStep = -1; d->pendingScene = -1;
        if (d->playing) d->stopping = true;
    });
}
void Session::saveArrangement(const std::vector<int>& order) {
    d->post([this, order] {
        if (d->songPlaying) return; // do not overwrite a currently playing song
        d->songOrder.clear();
        for (int i : order) if (i >= 0 && i < kScenes && d->songOrder.size() < 64)
            d->songOrder.push_back(i);
        d->persistScenes();
    });
}

void Session::setLoopSpeed(float v) { d->tapeSpeed.store(clampf(v, 0.25f, 2.f)); }
void Session::setLoopSource(int src) { d->post([=] { d->loopSrc = src ? 1 : 0; }); }
bool Session::saveClip(int t, const std::string& name) {
    if (t < 0 || t >= kTracks || !(d->a_clip_mask.load() & (1u << t))) return false;
    const std::string label = safeSampleName(name);
    const std::string folder = d->sampleDir + "/SAVED/LOOPS";
    // Capture a shared immutable clip on the audio thread; disk I/O stays on the worker.
    d->post([this, t, label, folder] {
        const auto& track = *d->tracks[(size_t)t];
        if (!track.clipHas || !track.clip || track.clip->empty()) return;
        auto clip = track.clip;
        d->job([this, clip, folder, label] {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(folder, ec);
            if (ec) return;
            std::string file = folder + "/" + label + ".wav";
            for (int n = 2; fs::exists(file, ec) && n < 10000; ++n)
                file = folder + "/" + label + " (" + std::to_string(n) + ").wav";
            writeWav16(file, *clip, d->sr); // preserve entire loop length, including silence
        });
    });
    return true;
}
bool Session::exportLoops(const std::string& directory) {
    // Export the ACTUAL post-loop DSP, not unprocessed recording buffers.
    // Capture only a small, consistent audio-thread snapshot; all expensive
    // processing, 16-bit PCM conversion and disk I/O happen on the worker.
    if (directory.empty()) return false;
    int prior = d->loopExportOp.load();
    do {
        if (prior == 1) return false;
    } while (!d->loopExportOp.compare_exchange_weak(prior, 1));
    d->post([this, directory] {
        if (d->freeRec || d->freeArmed || d->sampling || d->armedWait ||
            d->loopSamples <= 0) {
            d->loopExportOp = -1; return;
        }
        for (const auto& t : d->tracks)
            if (t->clipRec) { d->loopExportOp = -1; return; }
        Impl::LoopExportSnapshot snap;
        snap.sampleRate = d->sr;
        snap.loopSamples = d->loopSamples;
        snap.tapeSpeed = d->tapeSpeed.load();
        snap.masterVolume = d->masterVol;
        snap.returns[0] = d->ret[0]; snap.returns[1] = d->ret[1];
        snap.reverbSize = d->reverbSize;
        snap.delaySeconds = d->delaySeconds;
        snap.delayFeedback = d->delayFeedback;
        int populated = 0;
        for (int i = 0; i < kTracks; ++i) {
            const auto& t = *d->tracks[(size_t)i];
            auto& dst = snap.track[(size_t)i];
            if (t.clipHas && t.clip &&
                t.clip->size() == (size_t)snap.loopSamples * 2) {
                dst.clip = t.clip;  // COW: overdubs never mutate the snapshot
                dst.has = true; ++populated;
            } else if (t.clipHas) {
                d->loopExportOp = -2; return; // invalid/incomplete clip
            }
            dst.vol = t.vol; dst.pan = t.pan;
            dst.send[0] = t.send[0]; dst.send[1] = t.send[1];
            dst.scSrc = t.scSrc; dst.scThrDb = t.scThrDb; dst.scRatio = t.scRatio;
            dst.lfo = t.lfo; dst.lfoPhase = t.lfoPhase; dst.lfoSH = t.lfoSH;
            dst.tape = t.tape.settings();
        }
        if (!populated) { d->loopExportOp = -1; return; }

        d->job([this, snap = std::move(snap), directory] {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(directory, ec);
            if (ec) { d->loopExportOp = -2; return; }
            // The real engine uses its integer head when speed is near 1x.
            const double speed = std::fabs(snap.tapeSpeed - 1.f) < 0.01f
                ? 1.0 : std::max(0.25, (double)snap.tapeSpeed);
            const uint64_t frames = (uint64_t)std::ceil(snap.loopSamples / speed);
            bool ok = frames > 0 && frames <= (UINT32_MAX - 36u) / 4u;
            // The MASTER pass includes the sum before shared FX returns and
            // tanh limiter. Each isolated stem contains its own tape insert,
            // mixer, keyed sidechain, FX sends/returns and master gain.
            // Since delay has nonlinear feedback and the master has tanh,
            // processed stems need not sum bit-identically to MASTER.
            for (int pass = 0; ok && pass <= kTracks; ++pass) {
                const int solo = pass - 1; // pass 0 = MASTER; 1..8 = stems
                if (solo >= 0 && !snap.track[(size_t)solo].has) continue;
                Impl bounce(snap.sampleRate, true); // NO background thread
                bounce.loopSamples = snap.loopSamples;
                bounce.a_loop = snap.loopSamples;
                bounce.playing = true; bounce.stopping = false; bounce.rate = 1.0;
                bounce.tapeSpeed = (float)speed;
                bounce.masterVol = snap.masterVolume;
                bounce.ret[0] = snap.returns[0];
                bounce.ret[1] = snap.returns[1];
                bounce.reverb.setSize(snap.reverbSize);
                bounce.delay.setTime(snap.delaySeconds);
                bounce.delay.setFeedback(snap.delayFeedback);
                for (int t = 0; t < kTracks; ++t) {
                    const auto& src = snap.track[(size_t)t];
                    auto& dst = *bounce.tracks[(size_t)t];
                    dst.clipHas = src.has; dst.clip = src.clip ? src.clip
                        : std::make_shared<std::vector<float>>();
                    dst.vol = (solo < 0 || t == solo) ? src.vol : 0.f;
                    dst.pan = src.pan;
                    dst.send[0] = src.send[0]; dst.send[1] = src.send[1];
                    dst.scSrc = src.scSrc; dst.scThrDb = src.scThrDb;
                    dst.scRatio = src.scRatio; dst.scEnv = 0.f; dst.scGain = 1.f;
                    dst.lfo = src.lfo; dst.lfoPhase = src.lfoPhase;
                    dst.lfoSH = src.lfoSH;
                    const auto& p = src.tape;
                    dst.tape.setParams(p.sat, p.tone, p.wobble,
                                       p.hiss, p.mix, p.bits);
                }
                std::array<float, kBlock * 2> audio{};
                // Prime effect tails with the end of the repeated loop. This
                // includes previous-cycle reverb/delay without lengthening
                // exported stems and breaking DAW loop alignment.
                const uint64_t prime = std::min<uint64_t>(frames,
                    (uint64_t)std::max(1, snap.sampleRate) * 8);
                if (speed == 1.0) {
                    bounce.pos = (double)(snap.loopSamples - (int)prime);
                    bounce.tapePos = bounce.pos;
                } else {
                    bounce.pos = 0;
                    const double prior = std::fmod((double)prime * speed,
                                                   (double)snap.loopSamples);
                    bounce.tapePos = std::fmod((double)snap.loopSamples - prior,
                                                (double)snap.loopSamples);
                }
                for (uint64_t done = 0; done < prime;) {
                    const int n = (int)std::min<uint64_t>(kBlock, prime - done);
                    bounce.block(audio.data(), nullptr, n);
                    done += (uint64_t)n;
                }
                bounce.pos = bounce.tapePos = 0;
                bounce.playing = true; bounce.stopping = false; bounce.rate = 1.0;
                char name[40];
                if (solo < 0) std::snprintf(name, sizeof name, "MASTER.wav");
                else std::snprintf(name, sizeof name, "TRACK_%02d.wav", solo + 1);
                const std::string path = directory + "/" + name;
                LoopWavStream wav(path, frames, snap.sampleRate);
                for (uint64_t done = 0; ok && done < frames;) {
                    const int n = (int)std::min<uint64_t>(kBlock, frames - done);
                    bounce.block(audio.data(), nullptr, n);
                    ok = wav.write(audio.data(), n);
                    done += (uint64_t)n;
                }
                if (ok) ok = wav.finish();
                if (!ok) fs::remove(path, ec);
            }
            d->loopExportOp = ok ? 2 : -2;
        });
    });
    return true;
}
int Session::loopExportStatus() const { return d->loopExportOp.load(); }

int Session::loopPeaks(int track, float* out, int maxBins) const {
    const int n = std::min(maxBins, kLoopBins);
    for (int i = 0; i < n; i++) out[i] = track < 0 ? d->a_lpk[i].load() : d->a_tpk[std::min(track, kTracks - 1)][i].load();
    return d->a_loop.load() > 0 ? n : 0;
}


// ---- complete, self-contained project archive (FMP2) ----
// Stored per slot under app-private storage. Source WAVs are embedded so
// recordings/imports and the original composition survive deleted libraries.
// Snapshots are captured at one audio callback and written asynchronously.
namespace {
constexpr uint32_t kProjectMagic = 0x32504d46;  // FMP2
constexpr uint32_t kProjectVersion = 2;
constexpr uint64_t kProjectMaxBytes = 536870912ULL; // 512 MiB decompressed
constexpr uint32_t kSampleMax = 67108864U; // 64 MiB per WAV
std::string projectPath(const std::string& dir, int slot) {
    return dir + "/project_" + std::to_string(slot + 1) + ".fm1p";
}
struct ProjectWrite {
    std::ofstream out;
    uint64_t hash = 14695981039346656037ULL, bytes = 0;
    bool good = true;
    explicit ProjectWrite(const std::string& path) : out(path, std::ios::binary | std::ios::trunc) {}
    bool put(const void* data, size_t n) {
        if (!out || !good || n > kProjectMaxBytes - std::min(bytes, kProjectMaxBytes)) return good = false;
        out.write((const char*)data, (std::streamsize)n);
        if (!out) return good = false;
        const auto* b = (const uint8_t*)data;
        for (size_t i = 0; i < n; ++i) { hash ^= b[i]; hash *= 1099511628211ULL; }
        bytes += n;
        return true;
    }
    template<class T> bool value(const T& v) { return put(&v, sizeof(v)); }
};
struct ProjectRead {
    std::ifstream in;
    uint64_t hash = 14695981039346656037ULL, bytes = 0;
    bool good = true;
    explicit ProjectRead(const std::string& path) : in(path, std::ios::binary) {}
    bool get(void* data, size_t n) {
        if (!in || !good || n > kProjectMaxBytes - std::min(bytes, kProjectMaxBytes)) return good = false;
        in.read((char*)data, (std::streamsize)n);
        if (!in) return good = false;
        const auto* b = (const uint8_t*)data;
        for (size_t i = 0; i < n; ++i) { hash ^= b[i]; hash *= 1099511628211ULL; }
        bytes += n;
        return true;
    }
    template<class T> bool value(T& v) { return get(&v, sizeof(v)); }
};
bool validTimeline(int32_t samples, int bars, float tempo, double pos, double tapePos, double rate, int sr) {
    return samples >= 0 && samples <= sr * 180 && bars >= 1 && bars <= 32 &&
           std::isfinite(tempo) && tempo >= 30.f && tempo <= 300.f &&
           std::isfinite(pos) && std::isfinite(tapePos) && std::isfinite(rate) &&
           rate >= -6. && rate <= 6. &&
           (samples == 0 ? pos == 0. && tapePos == 0. :
            pos >= 0. && pos <= samples && tapePos >= 0. && tapePos <= samples);
}
bool writeTimeline(ProjectWrite& w, const Session::Impl::ProjectTimeline& t) {
    const uint8_t playing = t.playing, stopping = t.stopping;
    if (!w.value(t.samples) || !w.value(t.bars) || !w.value(t.tempo) ||
        !w.value(t.pos) || !w.value(t.tapePos) || !w.value(t.rate) ||
        !w.value(playing) || !w.value(stopping)) return false;
    for (int i = 0; i < Session::kTracks; ++i) {
        const auto& ptr = t.clips[(size_t)i];
        const uint8_t has = t.has[(size_t)i] ? 1 : 0;
        if (has && (!ptr || t.samples <= 0 || ptr->size() != (size_t)t.samples * 2)) return false;
        if (!w.value(has) || (has && !w.put(ptr->data(), ptr->size() * sizeof(float)))) return false;
    }
    return true;
}
bool readTimeline(ProjectRead& r, Session::Impl::ProjectTimeline& t, int sr) {
    uint8_t playing = 0, stopping = 0;
    if (!r.value(t.samples) || !r.value(t.bars) || !r.value(t.tempo) ||
        !r.value(t.pos) || !r.value(t.tapePos) || !r.value(t.rate) ||
        !r.value(playing) || !r.value(stopping) ||
        !validTimeline(t.samples, t.bars, t.tempo, t.pos, t.tapePos, t.rate, sr) ||
        playing > 1 || stopping > 1) return false;
    t.playing = playing; t.stopping = stopping;
    for (int i = 0; i < Session::kTracks; ++i) {
        uint8_t has = 0;
        if (!r.value(has) || has > 1 || (has && t.samples <= 0)) return false;
        t.has[(size_t)i] = has != 0;
        if (!has) continue;
        auto ptr = std::make_shared<std::vector<float>>((size_t)t.samples * 2);
        if (!r.get(ptr->data(), ptr->size() * sizeof(float))) return false;
        t.clips[(size_t)i] = std::move(ptr);
    }
    return true;
}
bool writeProjectFile(const std::string& dir, int slot, const Session::Impl::ProjectSnapshot& s, int sr) {
    namespace fs = std::filesystem;
    if (dir.empty() || s.appState.empty() || s.appState.size() > 65536) return false;
    std::error_code ec; fs::create_directories(dir, ec);
    if (ec) return false;
    const auto path = projectPath(dir, slot), tmp = path + ".tmp", backup = path + ".bak";
    ProjectWrite w(tmp);
    const uint32_t rate = (uint32_t)sr, len = (uint32_t)s.appState.size();
    bool ok = w.value(kProjectMagic) && w.value(kProjectVersion) && w.value(rate) &&
              w.value(len) && w.put(s.appState.data(), len);
    std::array<uint8_t, 65536> chunk{};
    for (int i = 0; i < Session::kTracks && ok; ++i) {
        const auto& src = s.samplePaths[(size_t)i];
        uint32_t size = 0;
        if (!src.empty()) {
            const auto n = fs::file_size(src, ec);
            if (ec || n == 0 || n > kSampleMax) { ok = false; break; }
            size = (uint32_t)n;
        }
        ok = w.value(size);
        if (!ok || !size) continue;
        std::ifstream input(src, std::ios::binary);
        if (!input) { ok = false; break; }
        for (uint32_t left = size; left && ok;) {
            const auto n = (size_t)std::min<uint32_t>(left, (uint32_t)chunk.size());
            input.read((char*)chunk.data(), (std::streamsize)n);
            ok = !!input && w.put(chunk.data(), n);
            left -= (uint32_t)n;
        }
    }
    ok = ok && validTimeline(s.current.samples, s.current.bars, s.current.tempo,
                             s.current.pos, s.current.tapePos, s.current.rate, sr)
          && writeTimeline(w, s.current);
    const uint8_t compose = s.hasCompose ? 1 : 0;
    ok = ok && w.value(compose);
    if (ok && compose) ok = validTimeline(s.compose.samples, s.compose.bars, s.compose.tempo,
                                         s.compose.pos, s.compose.tapePos, s.compose.rate, sr)
                           && writeTimeline(w, s.compose);
    for (const auto& scene : s.savedScenes) {
        if (!ok) break;
        const uint8_t filled = scene.filled;
        ok = w.value(filled);
        if (!filled || !ok) continue;
        ok = scene.samples > 0 && scene.samples <= sr * 180 &&
             scene.bars > 0 && scene.bars <= 32 &&
             std::isfinite(scene.tempo) && scene.tempo >= 30.f && scene.tempo <= 300.f &&
             w.value(scene.samples) && w.value(scene.bars) && w.value(scene.tempo);
        for (int t = 0; t < Session::kTracks && ok; ++t) {
            const auto& ptr = scene.clips[(size_t)t];
            const uint8_t has = scene.has[(size_t)t];
            if (has && (!ptr || ptr->size() != (size_t)scene.samples * 2)) { ok = false; break; }
            ok = w.value(has) && (!has || w.put(ptr->data(), ptr->size() * sizeof(float)));
        }
    }
    const uint32_t n = (uint32_t)s.arrangement.size();
    ok = ok && n <= 64 && w.value(n);
    for (int slotIndex : s.arrangement) {
        const uint8_t index = (uint8_t)slotIndex;
        ok = ok && slotIndex >= 0 && slotIndex < Session::kScenes && w.value(index);
    }
    const int32_t active = s.active, queued = s.queued, step = s.songStep;
    const uint8_t song = s.songPlaying;
    ok = ok && w.value(active) && w.value(queued) && w.value(step) &&
         w.value(s.pendingAt) && w.value(song);
    if (ok) {
        const uint64_t checksum = w.hash;
        w.out.write((const char*)&checksum, sizeof(checksum));
        w.out.flush(); ok = !!w.out;
    }
    w.out.close();
    if (!ok) { fs::remove(tmp, ec); return false; }
    // On a replaced slot, preserve the old archive until the new one is committed.
    fs::remove(backup, ec);
    const bool hadOld = fs::exists(path, ec);
    if (hadOld) { fs::rename(path, backup, ec); if (ec) { fs::remove(tmp, ec); return false; } }
    fs::rename(tmp, path, ec);
    if (ec) { if (hadOld) { std::error_code ignored; fs::rename(backup, path, ignored); }
              fs::remove(tmp, ec); return false; }
    if (hadOld) fs::remove(backup, ec);
    return true;
}
bool readProjectFile(const std::string& file, Session::Impl::ProjectSnapshot& snap,
                     std::array<std::vector<uint8_t>, Session::kTracks>& assets, int sr) {
    ProjectRead r(file);
    uint32_t magic = 0, ver = 0, rate = 0, metaLen = 0;
    if (!r.value(magic) || !r.value(ver) || !r.value(rate) || !r.value(metaLen) ||
        magic != kProjectMagic || ver != kProjectVersion ||
        rate != (uint32_t)sr || metaLen == 0 || metaLen > 65536) return false;
    snap.appState.resize(metaLen);
    if (!r.get(snap.appState.data(), metaLen)) return false;
    for (auto& asset : assets) {
        uint32_t len = 0;
        if (!r.value(len) || len > kSampleMax) return false;
        asset.resize(len);
        if (len && !r.get(asset.data(), len)) return false;
    }
    if (!readTimeline(r, snap.current, sr)) return false;
    uint8_t compose = 0;
    if (!r.value(compose) || compose > 1) return false;
    snap.hasCompose = compose;
    if (compose && !readTimeline(r, snap.compose, sr)) return false;
    for (auto& scene : snap.savedScenes) {
        uint8_t filled = 0;
        if (!r.value(filled) || filled > 1) return false;
        scene.filled = filled;
        if (!filled) continue;
        if (!r.value(scene.samples) || !r.value(scene.bars) || !r.value(scene.tempo) ||
            scene.samples <= 0 || scene.samples > sr * 180 || scene.bars < 1 ||
            scene.bars > 32 || !std::isfinite(scene.tempo) ||
            scene.tempo < 30.f || scene.tempo > 300.f) return false;
        for (int t = 0; t < Session::kTracks; ++t) {
            uint8_t has = 0;
            if (!r.value(has) || has > 1) return false;
            scene.has[(size_t)t] = has;
            if (!has) continue;
            auto ptr = std::make_shared<std::vector<float>>((size_t)scene.samples * 2);
            if (!r.get(ptr->data(), ptr->size() * sizeof(float))) return false;
            scene.clips[(size_t)t] = std::move(ptr);
        }
    }
    uint32_t n = 0;
    if (!r.value(n) || n > 64) return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t idx = 0; if (!r.value(idx) || idx >= Session::kScenes ||
             !snap.savedScenes[(size_t)idx].filled) return false;
        snap.arrangement.push_back(idx);
    }
    int32_t active = 0, queued = 0, step = 0; uint8_t song = 0;
    if (!r.value(active) || !r.value(queued) || !r.value(step) ||
        !r.value(snap.pendingAt) || !r.value(song) ||
        active < -1 || active >= Session::kScenes || queued < -1 ||
        queued >= Session::kScenes || step < -1 || step >= (int)n ||
        song > 1 || !std::isfinite(snap.pendingAt) ||
        (active >= 0 && !snap.savedScenes[(size_t)active].filled) ||
        (queued >= 0 && !snap.savedScenes[(size_t)queued].filled) ||
        (song && (n == 0 || step < 0))) return false;
    snap.active = active; snap.queued = queued; snap.songStep = step; snap.songPlaying = song;
    const uint64_t computed = r.hash;
    uint64_t stored = 0;
    r.in.read((char*)&stored, sizeof(stored));
    if (!r.in || stored != computed) return false;
    char extra = 0; r.in.read(&extra, 1);
    return r.in.eof(); // reject incomplete, corrupt and trailing data
}
}  // namespace

bool Session::projectExists(int slot) const {
    if (slot < 0 || slot >= kProjectSlots || d->dataDir.empty()) return false;
    std::error_code ec; return std::filesystem::is_regular_file(projectPath(d->dataDir, slot), ec);
}
int Session::projectOperation() const { return d->projectOp.load(); }
bool Session::saveProject(int slot, std::vector<uint8_t> meta,
                          const std::array<std::string, kTracks>& paths) {
    if (slot < 0 || slot >= kProjectSlots || d->dataDir.empty() ||
        meta.empty() || meta.size() > 65536) return false;
    int state = d->projectOp.load();
    while (state != 1 && state != 3) {
        if (d->projectOp.compare_exchange_weak(state, 1)) break;
    }
    if (d->projectOp.load() != 1 || state == 1 || state == 3) return false;
    d->post([this, slot, meta = std::move(meta), paths]() mutable {
        if (d->freeRec || d->freeArmed || d->sampling || d->armedWait) {
            d->projectOp = -1; return;
        }
        for (const auto& t : d->tracks) if (t->clipRec) { d->projectOp = -1; return; }
        auto snapshot = d->captureProject(std::move(meta), paths);
        const auto folder = d->dataDir;
        const int rate = d->sr;
        d->job([this, slot, folder, rate, snapshot = std::move(snapshot)]() mutable {
            d->projectOp = writeProjectFile(folder, slot, snapshot, rate) ? 2 : -1;
        });
    });
    return true;
}
bool Session::loadProject(int slot) {
    if (slot < 0 || slot >= kProjectSlots || !projectExists(slot)) return false;
    int state = d->projectOp.load();
    while (state != 1 && state != 3) {
        if (d->projectOp.compare_exchange_weak(state, 3)) break;
    }
    if (d->projectOp.load() != 3 || state == 1 || state == 3) return false;
    const auto folder = d->dataDir;
    d->job([this, slot, folder] {
        Impl::ProjectSnapshot snap;
        std::array<std::vector<uint8_t>, kTracks> assets;
        const bool valid = readProjectFile(projectPath(folder, slot), snap, assets, d->sr);
        if (!valid) { d->projectOp = -2; return; }
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string target = folder + "/project_assets/slot_" + std::to_string(slot + 1);
        fs::create_directories(target, ec);
        if (ec) { d->projectOp = -2; return; }
        std::array<std::string, kTracks> paths{};
        for (int t = 0; t < kTracks; ++t) {
            const auto& raw = assets[(size_t)t];
            if (raw.empty()) continue;
            const auto asset = target + "/track_" + std::to_string(t + 1) + ".wav";
            const auto temp = asset + ".tmp";
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) { d->projectOp = -2; return; }
            out.write((const char*)raw.data(), (std::streamsize)raw.size());
            out.flush();
            const bool good = !!out; out.close();
            if (!good) { d->projectOp = -2; return; }
            // A loaded sample may still be in use; replace the original
            // atomically and let the worker load it after the project callback.
            fs::rename(temp, asset, ec);
            if (ec) {
                fs::remove(asset, ec); ec.clear();
                fs::rename(temp, asset, ec);
            }
            if (ec) { d->projectOp = -2; return; }
            paths[(size_t)t] = asset;
        }
        auto result = std::make_shared<LoadedProject>();
        result->slot = slot; result->appState = std::move(snap.appState);
        result->samplePaths = std::move(paths);
        d->post([this, snap = std::move(snap), result]() mutable {
            d->installProject(std::move(snap));
            { std::lock_guard<std::mutex> lock(d->projectMtx); d->projectReady = result; }
            d->projectOp = 4;
        });
    });
    return true;
}
bool Session::takeLoadedProject(LoadedProject& output) {
    std::lock_guard<std::mutex> lock(d->projectMtx);
    if (!d->projectReady) return false;
    output = std::move(*d->projectReady);
    d->projectReady.reset();
    return true;
}
void Session::clearSample(int t) {
    if (t < 0 || t >= kTracks) return;
    d->post([this,t] {
        auto& track = *d->tracks[(size_t)t];
        d->releaseAll(track);
        d->bury(std::move(track.sampler));
        { std::lock_guard<std::mutex> lock(track.infoMtx);
          track.label.clear(); track.peaks.clear(); track.orig.reset(); track.slices.clear(); }
    });
}

// ---- microphone sampling ----
void Session::setMicThreshold(float v) { d->micThr.store(clampf(v, 0.005f, 0.5f)); }
void Session::recorderWait(bool w) { d->post([this, w] { if (!d->sampling && !d->armedWait) d->waitForSound = w; }); }
void Session::recorderStart() {
    d->post([this] {
        if (d->sampling || d->armedWait) return;
        d->sampleBuf.clear(); d->sampleBuf.reserve((size_t)d->sr * 30); d->a_sampleSec = 0; d->liveReset();
        if (d->waitForSound) { d->armedWait = true; d->a_sampleState = 3; } else { d->sampling = true; d->a_sampleState = 1; }
    });
}
void Session::recorderStop() {
    d->post([this] {
        if (d->armedWait) { d->armedWait = false; d->sampleBuf.clear(); d->a_sampleState = 0; return; }
        if (!d->sampling) return;
        d->sampling = false; d->a_sampleState = 2;
        std::vector<float> st(d->sampleBuf.size() * 2);
        for (size_t i = 0; i < d->sampleBuf.size(); i++) st[2 * i] = st[2 * i + 1] = d->sampleBuf[i];
        d->sampleBuf.clear();
        d->job([this, st = std::move(st)]() mutable { d->finishSample(std::move(st)); });
    });
}
void Session::recorderDismiss() { std::lock_guard<std::mutex> l(d->sampleMtx); d->pending = false; d->a_pending = false; }
std::string Session::lastSamplePath() const { std::lock_guard<std::mutex> l(d->sampleMtx); return d->samplePath; }
int Session::freePeaks(float* out, int maxBins) const {
    const int n = std::min(maxBins, d->freeN.load());
    for (int i = 0; i < n; i++) out[i] = d->freePk[i].load();
    return n;
}
int Session::livePeaks(float* out, int maxBins) const {
    const int n = std::min(maxBins, d->liveN.load());
    for (int i = 0; i < n; i++) out[i] = d->livePk[i].load();
    return n;
}

// ---- library ----
void Session::setDataDir(const std::string& dir) { d->dataDir = dir; d->restoreScenes(); }
void Session::setSampleDir(const std::string& dir) { d->sampleDir = dir; }
std::string Session::dataDir() const { return d->dataDir; }
std::string Session::sampleDir() const { return d->sampleDir; }
std::vector<std::string> Session::sampleFolders() const {
    std::vector<std::string> v; std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(d->sampleDir, ec)) if (e.is_directory()) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end()); return v;
}
std::vector<std::string> Session::sampleFiles(const std::string& folder) const {
    std::vector<std::string> v; std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(d->sampleDir + "/" + folder, ec)) if (e.path().extension() == ".wav") v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end()); return v;
}
std::vector<std::string> Session::sampleLibrary() const {
    namespace fs = std::filesystem;
    std::vector<std::pair<fs::file_time_type, std::string>> v; std::error_code ec;
    if (d->dataDir.empty()) return {};
    for (auto& e : fs::directory_iterator(d->dataDir, ec)) {
        const auto n = e.path().filename().string();
        if (n.rfind("sample_", 0) == 0 && e.path().extension() == ".wav") v.push_back({e.last_write_time(ec), e.path().string()});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out; for (auto& p : v) out.push_back(p.second);
    return out;
}

// ---- views ----
std::vector<float> Session::samplePeaksOf(int t, int bins) const {
    std::vector<float> out;
    if (t < 0 || t >= kTracks || bins <= 0) return out;
    const auto& tr = *d->tracks[(size_t)t];
    std::lock_guard<std::mutex> l(tr.infoMtx);
    if (tr.peaks.empty()) return out;
    out.assign((size_t)bins, 0.f);
    for (size_t i = 0; i < tr.peaks.size(); i++) { const size_t b = i * (size_t)bins / tr.peaks.size(); out[b] = std::max(out[b], tr.peaks[i]); }
    return out;
}
std::string Session::trackLabel(int t) const {
    if (t < 0 || t >= kTracks) return "";
    std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx);
    return d->tracks[(size_t)t]->label;
}
int Session::sampleRate() const { return d->sr; }
Session::Status Session::status() const {
    Status s{};
    s.playing = d->a_playing; s.pos = d->a_pos; s.loopSamples = d->a_loop;
    s.bpm = d->a_bpm; s.loopBars = d->a_bars; s.master_peak = d->a_master; s.cpu_permille = d->a_cpu;
    const uint32_t cm = d->a_clip_mask, rm = d->a_cliprec_mask;
    for (int i = 0; i < kTracks; i++) {
        s.track_peak[i] = d->tracks[(size_t)i]->peak;
        s.track_has_clip[i] = cm >> i & 1; s.track_clip_rec[i] = rm >> i & 1;
        s.clipPos[i] = d->a_clipPos; s.lfoValue[i] = d->a_lfo[i]; s.scGain[i] = d->a_scGain[i]; s.scLevel[i] = d->a_scLevel[i];
        s.voiceN[i] = std::min(kVoiceMax, d->a_voiceN[i].load()); for (int j = 0; j < s.voiceN[i]; j++) { s.voicePos[i][j] = d->a_voicePos[i][j]; s.voiceLvl[i][j] = d->a_voiceLvl[i][j]; }
    }
    s.freeRecording = d->a_free; s.freeArmed = d->a_freeArmed; s.freeSeconds = d->a_freeSec;
    s.sampleState = d->a_sampleState; s.sampleTrack = d->a_sampleTrack; s.sampleSeconds = d->a_sampleSec; s.inputPeak = d->a_inPeak;
    s.sampleVersion = d->a_sampleVersion; s.sampleFails = d->a_sampleFails; s.samplePending = d->a_pending;
    s.sceneMask = d->a_sceneMask; s.activeScene = d->a_activeScene;
    for (int i = 0; i < kScenes; ++i) s.sceneTracks[i] = d->a_sceneTracks[(size_t)i];
    s.queuedScene = d->a_queuedScene; s.songPlaying = d->a_songPlaying;
    s.songStep = d->a_songStep; s.songSize = d->a_songSize;
    s.arrangementLoaded = d->a_arrangementLoaded;
    s.arrangementSize = std::max(0, std::min(64, d->a_arrangementSize.load()));
    for (int i = 0; i < s.arrangementSize; ++i) s.arrangement[i] = d->a_arrangement[i].load();
    return s;
}
