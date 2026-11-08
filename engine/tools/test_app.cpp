// Host tests of the sampler app: every algorithm sounds, LFO targets, stretch/pitch, tape knobs, arp, loop speed, library, import. Exit code 1 on failure.
#include <Session.h>
#include <SamplerApp.h>
#include <Common.h>
#include <Stretch.h>
#include <Wav.h>
#include <Slicer.h>
#include <SamplerUI.h>
#include <Theme.h>
#include <Reverb.h>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s: ", #c); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main() {
    Denormals::enableOnThisThread();
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_t2; mkdir -p /tmp/fm1sampler_t2");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t2"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    uint64_t ledMask = 0; int ledFrames = 0;   // the last LED frame the FM-1 got (SysEx 01)
    SamplerApp app(s, [&](const uint8_t* m, size_t n) {   // frames come packed: F0 7D D A W 1 10 { len(2x7bit) cmd payload }... F7
        if (n < 9 || m[6] != 10) return;
        for (size_t i = 7; i + 3 < n;) { const size_t sub = (size_t)m[i] << 7 | m[i + 1]; if (sub < 1 || i + 2 + sub > n) break;
            if (m[i + 2] == 1 && sub == 7) { ledMask = 0; for (int k = 0; k < 6; k++) ledMask |= (uint64_t)(m[i + 3 + k] & 127) << (7 * k); ledFrames++; }
            i += 2 + sub; }
    });
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(200)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto rmsFrom = [&](size_t from) { double a = 0; size_t n = 0; for (size_t i = from; i < all.size(); i++) { a += all[i] * all[i]; n++; } return n ? std::sqrt(a / n) : 0.0; };
    auto enc = [&](int i, int d) { uint8_t m[3] = {0xb0, (uint8_t)(16 + i), (uint8_t)(64 + d)}; app.onMidi(m, 3); };
    auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
    auto pageLed = [&](int b) { return (ledMask >> b & 1) != 0; };
    auto play = [&](int note, double sec) { size_t m = all.size(); app.playNote(note, true); run(sec); app.playNote(note, false); run(0.3); return rmsFrom(m); };
    run(2.5);
    CHECK(app.hasSample(0) && app.hasSample(1) && app.hasSample(2) && app.hasSample(3), "four default tracks load (%s, %s, %s, %s)", app.sampleLabel(0).c_str(), app.sampleLabel(1).c_str(), app.sampleLabel(2).c_str(), app.sampleLabel(3).c_str());
    CHECK(!app.hasSample(4), "track 5 is empty");
    // Empty SONG must not cause a file picker, or publish any audio at all.
    app.requestSongExport();
    CHECK(!app.wantSongExport() && app.toast() == "NO SCENES IN SONG",
          "EXPORT SONG refuses an arrangement with no scenes");
    const std::string emptySongDir = "/tmp/fm1sampler_t2/empty_song_export";
    CHECK(s.exportSong(emptySongDir), "empty song request resolves asynchronously");
    for (int k = 0; k < 50 && s.loopExportStatus() == 1; ++k) run(0.01);
    CHECK(s.loopExportStatus() == -3 &&
          !std::filesystem::exists(emptySongDir + "/SONG.wav"),
          "empty SONG export reports -3 and creates no WAV file");
    // USB disconnect after OCT+ press without Note Off previously froze K1 in CUE.
    {
        btn(SamplerApp::ENV);
        const float before = app.knobs()[0].norm;
        const int rate = app.cueDivision();
        uint8_t oct[3] = {0x91, (uint8_t)SamplerApp::OCT_UP, 100};
        app.onMidi(oct, 3);
        enc(2, 1);
        CHECK(app.cueModifierHeld() && app.cueDivision() != rate &&
              std::fabs(app.knobs()[0].norm - before) < 0.0001f,
              "OCT+ intentionally changes K1 to CUE rate");
        app.onMidiDisconnected();
        CHECK(!app.cueModifierHeld() && !app.octDownModifierHeld() &&
              app.page() == SamplerApp::P_ENV,
              "USB disconnect clears stale modifiers without losing page");
        enc(2, 1);
        CHECK(app.knobs()[0].norm > before + 0.001f,
              "K1 responds again after reconnect");
        btn(SamplerApp::HOME);
    }
    // algorithms on track 1
    for (int a = 0; a < 6; a++) { app.setAlgo(a); run(0.3); double r = play(60, 1.0); CHECK(r > 0.003, "algorithm %s sounds (rms %.4f)", SamplerApp::algoName(a), r); }
    app.setAlgo(0);
    // LFO on each target
    btn(SamplerApp::LFO);
    for (int tg = 0; tg < 4; tg++) { auto k = app.knobs(); while (app.state(0).lfoTarget < tg) k[2].step(1); enc(3, 40); run(0.2); double r = play(60, 1.2); CHECK(r > 0.002, "LFO target %d sounds (rms %.4f), lfo value %.2f", tg, r, app.status().lfoValue[0]); }
    enc(3, -80); run(0.3);
    // stretch + pitch (offline processing, applied when the knob rests)
    btn(SamplerApp::HOME); enc(4, 20); enc(5, 12); run(2.0); { double r = play(60, 1.0); CHECK(r > 0.002, "stretch + pitch sound (rms %.4f)", r); }
    enc(4, -20); enc(5, -12); run(2.0);
    // tape page
    btn(SamplerApp::SEQ); enc(3, 30); enc(4, 30); enc(5, 20); run(0.3); { double r = play(60, 1.0); CHECK(r > 0.002 && std::isfinite(r), "exciter + lofi + wobble sound (rms %.4f)", r); }
    // arp
    btn(SamplerApp::ARP); enc(2, 1); for (int i = 0; i < 6; i++) enc(2, 1); run(0.2);
    { size_t m = all.size(); app.playNote(60, true); app.playNote(64, true); app.playNote(67, true); run(1.5); app.playNote(60, false); app.playNote(64, false); app.playNote(67, false); run(0.3); CHECK(rmsFrom(m) > 0.002, "arp sounds (rms %.4f)", rmsFrom(m)); }
    for (int i = 0; i < 12; i++) enc(2, -1); run(0.2);
    // loop: free take, speed, overdub, save
    btn(SamplerApp::REC); run(0.1);
    int seq[] = {60, 64, 67, 72}; for (int n : seq) { app.playNote(n, true); run(0.3); app.playNote(n, false); run(0.1); }
    btn(SamplerApp::REC); run(0.5);
    CHECK(app.status().track_has_clip[0] && app.loopExists(), "first take became the loop (%.0f bpm, %d bars)", app.status().bpm, app.status().loopBars);
    size_t m = all.size(); run(2.0); CHECK(rmsFrom(m) > 0.003, "loop plays back (rms %.4f)", rmsFrom(m));
    btn(SamplerApp::SEQ); enc(2, -10); run(0.1); m = all.size(); run(2.0); CHECK(rmsFrom(m) > 0.001 && app.status().clipPos[0] >= 0.f, "loop at half speed plays (pos %.2f)", app.status().clipPos[0]);
    enc(2, 10); run(0.2);
    btn(SamplerApp::REC); run(0.1); app.playNote(55, true); run(0.3); app.playNote(55, false); run(2.0); btn(SamplerApp::REC); run(0.2);
    CHECK(!app.status().track_clip_rec[0], "overdub ended");
    { app.selectTrack(0); app.setAlgo(4); run(0.3); app.playNote(60, true); run(0.8); CHECK(app.status().voiceN[0] > 3, "granular: every grain is reported (%d)", app.status().voiceN[0]); app.playNote(60, false); run(0.5); app.setAlgo(0); run(0.2); }
    { const double L = app.status().loopSamples; btn(SamplerApp::SEL); enc(2, 1); run(0.3);
      CHECK(app.status().loopSamples == 2 * L, "LENGTH page: loop doubled (%.0f -> %.0f)", L, app.status().loopSamples);
      m = all.size(); run(2.5); CHECK(rmsFrom(m) > 0.003, "doubled loop plays (rms %.4f)", rmsFrom(m));
      enc(2, -1); run(0.3); CHECK(app.status().loopSamples == L, "LENGTH page: loop halved back (%.0f)", app.status().loopSamples); }
    app.playNote(60, true); run(0.3); CHECK(app.status().voiceN[0] > 0 && app.status().voicePos[0][0] > 0.f, "active sampler voices reported (%d, pos %.3f)", app.status().voiceN[0], app.status().voicePos[0][0]); app.playNote(60, false); run(0.5);
    { app.selectTrack(3); app.setAlgo(3); run(0.3); size_t km = all.size(); uint8_t kd[3] = {0x90, (uint8_t)(53 + 26), 100}, ku[3] = {0x80, (uint8_t)(53 + 26), 0}; app.onMidi(kd, 3); run(0.6); app.onMidi(ku, 3); run(0.3);
      CHECK(rmsFrom(km) > 0.002, "KIT: the last of the 27 keys plays a pad (rms %.4f)", rmsFrom(km)); app.selectTrack(0); }
    app.seek(0.5f); run(0.05); { const float cp = app.status().clipPos[0]; CHECK(cp > 0.49f && cp < 0.65f, "seek: playhead jumps to the middle of the loop (%.3f)", cp); }
    uint8_t sd[3] = {0x91, (uint8_t)SamplerApp::SAVE, 100}, su[3] = {0x81, (uint8_t)SamplerApp::SAVE, 100};
    app.onMidi(sd, 3); run(0.9); app.onMidi(su, 3); run(0.3);   // the FM-1 SAVE button neither saves nor clears any more
    CHECK(app.loopExists() && app.page() == SamplerApp::P_SAVE, "SAVE button only opens the LOOP page (held for 0.9 s: loop stays)");
    app.loopSave(); run(1.0);
    int wavs = 0;
    {
        const std::filesystem::path saved = std::filesystem::path(s.sampleDir()) / "SAVED" / "LOOPS";
        std::error_code ec;
        for (const auto& file : std::filesystem::directory_iterator(saved, ec))
            if (file.path().extension() == ".wav") ++wavs;
    }
    CHECK(wavs >= 1, "SAVE wrote the complete named loop into SAVED/LOOPS (%d files)", wavs);
    // Export is a frozen audio-thread snapshot written asynchronously;
    // track stems and the mix must exist independently of current state.
    const std::string exportDir = "/tmp/fm1sampler_t2/public_export_staging";
    CHECK(s.exportLoops(exportDir), "export recorded loop to staging starts");
    for (int k = 0; k < 80 && s.loopExportStatus() == 1; ++k) run(0.05);
    CHECK(s.loopExportStatus() == 2, "snapshot export finishes successfully");
    {
        std::vector<float> wav; int rate = 0;
        CHECK(readWav(exportDir + "/TRACK_01.wav", wav, rate) &&
              rate == sr && !wav.empty(),
              "export WAV stem is playable and retains the original sample rate");
        CHECK(std::filesystem::exists(exportDir + "/MASTER.wav"),
              "export also writes the full-length master WAV");

        // The exported stem must reflect exactly the live track gain, tape
        // processing and shared FX bus, not the untouched recorded clip.
        // Silence after volume=0 verifies there is no dry buffer bypass.
        std::vector<float> baseline = wav;
        s.setVolume(0, 0.f); run(0.12);
        const std::string muted = "/tmp/fm1sampler_t2/export_mute_dsp";
        CHECK(s.exportLoops(muted), "offline export renders zero-gain live mixer snapshot");
        for (int k = 0; k < 120 && s.loopExportStatus() == 1; ++k) run(0.05);
        std::vector<float> zero; int mutedRate = 0;
        const bool mutedOk = readWav(muted + "/TRACK_01.wav", zero, mutedRate);
        float mutedPeak = 0.f;
        for (float v : zero) mutedPeak = std::max(mutedPeak, std::fabs(v));
        CHECK(s.loopExportStatus() == 2 && mutedOk && zero.size() == baseline.size() &&
              mutedPeak < 1e-4f,
              "exported WAV follows the real post-tape mixer gain (mute = digital silence)");
        s.setVolume(0, app.state(0).vol);
        // Increase shared reverb/delay sends and ensure that the bounced
        // stem is different from its previously exported PCM audio.
        s.setSend(0, 0, 1.f); s.setSend(0, 1, 1.f);
        s.setRetLevel(0, 1.f); s.setRetLevel(1, 1.f);
        s.setReverb(0.98f); s.setDelay(0.12f, 0.75f);
        run(0.15);
        const std::string fx = "/tmp/fm1sampler_t2/export_fx_dsp";
        CHECK(s.exportLoops(fx), "offline export accepts new reverb and tape-delay settings");
        for (int k = 0; k < 120 && s.loopExportStatus() == 1; ++k) run(0.05);
        std::vector<float> wet; int fxRate = 0;
        const bool wetOk = readWav(fx + "/TRACK_01.wav", wet, fxRate);
        double difference = 0;
        if (wetOk && wet.size() == baseline.size())
            for (size_t i = 0; i < wet.size(); ++i)
                difference += std::fabs(wet[i] - baseline[i]);
        CHECK(s.loopExportStatus() == 2 && wetOk && wet.size() == baseline.size() &&
              difference / std::max<size_t>(1, wet.size()) > 0.001,
              "exported stem includes shared reverb and delay returns (mean delta %.5f)",
              difference / std::max<size_t>(1, wet.size()));
        CHECK(std::filesystem::exists(fx + "/MASTER.wav"),
              "wet export contains the independently rendered full master mix");
        s.setSend(0, 0, 0.f); s.setSend(0, 1, 0.f);
        s.setRetLevel(0, 0.35f); s.setRetLevel(1, 0.7f);
    }
    app.loopClearTrack(); run(0.3);
    CHECK(!app.status().track_has_clip[0], "clearing one track's loop");
    app.loopClear(); run(0.3);
    CHECK(!app.loopExists(), "PROJECT menu: clear all loops");
    { bool any = false; for (int t = 0; t < 8; t++) any |= app.status().track_has_clip[t]; CHECK(!any, "...and every other track's loop"); }
    { constexpr int FXB = SamplerApp::FX, ENVB = SamplerApp::ENV;
      btn(FXB); run(0.6); const bool fx1 = pageLed(FXB) && !pageLed(SamplerApp::HOME);
      btn(ENVB); run(0.6); CHECK(fx1 && pageLed(ENVB) && !pageLed(FXB), "FM-1 LEDs: pressing another page button moves the light (ENV lit, FX dark)");
      const int before = ledFrames; ledMask = 0; run(1.0); CHECK(ledFrames > before && pageLed(ENVB), "FM-1 LEDs are resent periodically (the firmware may clear them)"); }
    {   // PLAY / STOP: while recording it only ends the recording, the loop plays on; the next press stops the tape (it slows down, not a cut); PLAY runs it up again
      app.selectTrack(0); app.loopClear(); run(0.3);
      app.loopRecord(); run(0.1); app.playNote(60, true); run(0.15); app.playNote(60, false); app.playNote(64, true); run(0.5); app.playNote(64, false); run(0.3);
      CHECK(app.status().freeRecording, "a take is recording");
      app.transportPlay(); run(0.3);
      CHECK(!app.status().freeRecording && app.loopExists() && app.status().playing, "PLAY/STOP while recording ends the recording and keeps playing");
      size_t m1 = all.size(); run(1.2); const double playing1 = rmsFrom(m1);
      CHECK(playing1 > 0.003, "the loop plays on (rms %.4f)", playing1);
      app.transportPlay(); run(0.05);
      CHECK(!app.status().playing, "second press: stopped");
      size_t m2 = all.size(); run(0.15); const double slowing = rmsFrom(m2);
      CHECK(slowing > 0.2 * playing1, "...but the tape slows down instead of cutting (rms %.4f vs %.4f)", slowing, playing1);
      run(1.2); size_t m3 = all.size(); run(0.5); CHECK(rmsFrom(m3) < 0.15 * playing1, "...and stands still afterwards (only effect tails: rms %.5f)", rmsFrom(m3));
      app.transportPlay(); run(0.1); size_t m4 = all.size(); run(0.3); const double up = rmsFrom(m4); size_t m5 = all.size(); run(1.5);
      CHECK(app.status().playing && up < rmsFrom(m5) && rmsFrom(m5) > 0.003, "PLAY runs the tape up again (ramp rms %.4f, then %.4f)", up, rmsFrom(m5));
      // FM-1 hardware modifiers: restart, enter CUE selection by holding OCT+,
      // beat repeat under PLAY, change rate on K1 and release without stopping.
      auto hold = [&](int key, bool down) {
          uint8_t m[3] = {(uint8_t)(down ? 0x91 : 0x81), (uint8_t)key, 100};
          app.onMidi(m, 3);
      };
      const int octaveBefore = app.scale().octave;
      app.clearMidiDebug();
      CHECK(app.midiControlStatus() == "MIDI RX: NO MESSAGES YET",
            "physical MIDI diagnostics report no incoming messages initially");
      s.seek(0.65f); run(0.06);
      // Messages arrive separately: OCT- must be remembered without a
      // simultaneous MIDI callback or an arbitrary time threshold.
      hold(SamplerApp::OCT_DOWN, true);
      CHECK(app.midiControlStatus().find("OCT-:OK") != std::string::npos &&
            app.midiLastEvent().find("CH2 NOTE 0 DOWN [OCT- HELD]") != std::string::npos,
            "MIDI monitor confirms OCT- Note On and remembers modifier is held");
      run(0.50);
      hold(SamplerApp::PLAY, true); run(0.02);
      CHECK(app.midiControlStatus().find("PLAY:OK") != std::string::npos &&
            app.midiLastEvent().find("CH2 NOTE 12 DOWN [OCT- HELD]") != std::string::npos,
            "MIDI monitor confirms PLAY arrives in a distinct, later callback");
      CHECK(app.status().pos < app.status().loopSamples * 0.16 && app.status().playing,
            "OCT- + PLAY instantly restarts on second key-down (%.1f)", app.status().pos);
      hold(SamplerApp::PLAY, false); hold(SamplerApp::OCT_DOWN, false); run(0.02);
      CHECK(app.status().playing, "combo PLAY release does not toggle transport");
      s.seek(0.72f); run(0.04);
      hold(SamplerApp::OCT_DOWN, true); hold(SamplerApp::STOP, true); run(0.02);
      CHECK(app.status().pos < app.status().loopSamples * 0.16 && app.status().playing,
            "OCT- + STOP instantly restarts, not stops (%.1f)", app.status().pos);
      hold(SamplerApp::STOP, false); hold(SamplerApp::OCT_DOWN, false);
      // STRICT ORDER: a modifier pressed AFTER PLAY must not become a shortcut.
      s.seek(0.72f); run(0.04);
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(!app.status().playing, "PLAY without a prior OCT modifier acts normally on key-down");
      hold(SamplerApp::OCT_DOWN, true); run(0.02);
      CHECK(!app.status().playing,
            "PLAY then OCT- does not restart: OCT has to be pressed first");
      hold(SamplerApp::OCT_DOWN, false); hold(SamplerApp::PLAY, false); run(0.02);
      CHECK(!app.status().playing, "releasing OCT and PLAY does not cause an extra transport toggle");
      app.transportSet(true); run(0.2);
      s.seek(0.68f); run(0.04);
      hold(SamplerApp::OCT_DOWN, true);
      hold(SamplerApp::PLAY, true); run(0.02);
      hold(SamplerApp::OCT_DOWN, false); // release modifier BEFORE releasing PLAY
      hold(SamplerApp::PLAY, false); run(0.03);
      CHECK(app.status().playing && app.status().pos < app.status().loopSamples * 0.16,
            "modifier release before PLAY release never produces a second stop");
      s.seek(0.70f); run(0.04);
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(!app.status().playing, "after releasing OCT-, PLAY is the ordinary stop");
      hold(SamplerApp::PLAY, false);
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(app.status().playing, "second unmodified PLAY starts playback normally");
      hold(SamplerApp::PLAY, false);
      CHECK(app.scale().octave == octaveBefore, "OCT modifier does not shift the app's octave");
      const auto originalPage = app.page();
      // Both octave buttons expose the rate menu IMMEDIATELY, regardless
      // of their order; the second button event can arrive much later.
      hold(SamplerApp::OCT_DOWN, true); run(0.15);
      CHECK(app.page() == originalPage, "OCT- by itself does not open RATE menu");
      hold(SamplerApp::OCT_UP, true); run(0.04);
      CHECK(app.page() == SamplerApp::P_CUE && app.knobs()[0].value == "1/4",
            "OCT- then OCT+ opens NOTE REPEAT subdivision menu instantly");
      hold(SamplerApp::OCT_DOWN, false); run(0.02);
      CHECK(app.page() == SamplerApp::P_CUE,
            "OCT- release keeps RATE menu visible while OCT+ remains held");
      hold(SamplerApp::OCT_UP, false); run(0.04);
      CHECK(app.page() == originalPage, "releasing OCT+ closes subdivision menu");
      hold(SamplerApp::OCT_UP, true); run(0.10);
      hold(SamplerApp::OCT_DOWN, true); run(0.04);
      CHECK(app.page() == SamplerApp::P_CUE,
            "OCT+ then OCT- also opens NOTE REPEAT menu instantly");
      hold(SamplerApp::OCT_UP, false); hold(SamplerApp::OCT_DOWN, false);
      CHECK(app.page() == originalPage,
            "release in reverse order restores the original page");
      hold(SamplerApp::OCT_UP, true); run(2.15);
      CHECK(app.page() == SamplerApp::P_CUE && app.knobs()[0].value == "1/4",
            "OCT+ held two seconds opens CUE at 1/4");
      app.knobs()[0].set(1.f); run(0.05);
      CHECK(app.knobs()[0].value == "1/32T", "K1 can choose a 1/32 triplet");
      hold(SamplerApp::PLAY, true);
      run(0.15); const float cq0 = app.status().clipPos[0];
      run(0.10); const float cq1 = app.status().clipPos[0];
      CHECK(std::fabs(cq1 - cq0) < 0.15f,
            "while PLAY is held, CUE repeats a short BPM-locked region (%.3f -> %.3f)", cq0, cq1);
      enc(2, -1); run(0.05);
      CHECK(app.knobs()[0].value == "1/32", "K1 switches from triplet to straight during CUE");
      CHECK(app.midiControlStatus().find("K1:OK") != std::string::npos,
            "MIDI diagnostic recognizes incoming physical K1 CC18");
      hold(SamplerApp::PLAY, false); run(0.15);
      CHECK(app.status().playing, "releasing PLAY returns to the running loop");
      hold(SamplerApp::OCT_UP, false); run(0.04);
      CHECK(app.page() == originalPage, "releasing OCT+ restores the previous page");

      // OCT+ is the CUE modifier. PLAY only repeats while held. K1 can be
      // changed live and the original loop must never be overwritten.
      std::vector<float> before(64), after(64);
      s.loopPeaks(0, before.data(), (int)before.size());
      const auto savedLoopLength = app.status().loopSamples;
      // Enter immediately: waiting for OCT+ long hold must NOT be required.
      hold(SamplerApp::OCT_UP, true);
      hold(SamplerApp::PLAY, true);
      run(0.05);
      CHECK(app.cueActive(), "OCT+ and PLAY starts CUE without a prior 2-second hold");
      enc(2, 1); run(0.08);
      CHECK(app.cueDivision() == 11 && app.knobs()[0].value == "1/32T" && app.cueActive(),
            "physical K1 selects 1/32T live during held PLAY");
      enc(2, -5); run(0.08);
      CHECK(app.cueDivision() == 10 && app.knobs()[0].value == "1/32" && app.cueActive(),
            "physical K1 switches triplet/straight live while PLAY stays held");
      hold(SamplerApp::PLAY, false); run(0.05);
      CHECK(!app.cueActive() && app.status().playing, "releasing PLAY alone restores normal loop");
      hold(SamplerApp::OCT_UP, false);
      // When OCT+ is released before a new PLAY/STOP press, NOTE REPEAT
      // must not be latched for that subsequent ordinary transport action.
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(!app.status().playing && !app.cueActive(),
            "after releasing OCT+, PLAY/STOP returns to ordinary STOP");
      hold(SamplerApp::PLAY, false);
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(app.status().playing && !app.cueActive(),
            "next unmodified PLAY/STOP starts without re-entering NOTE REPEAT");
      hold(SamplerApp::PLAY, false);
      s.loopPeaks(0, after.data(), (int)after.size());
      float diff = 0.f;
      for (size_t i = 0; i < before.size(); ++i)
          diff = std::max(diff, std::fabs(after[i] - before[i]));
      CHECK(diff < 1.e-6f && app.status().loopSamples == savedLoopLength,
            "CUE never modifies the recorded loop audio or duration (diff %.8f)", diff);

      // Every binary rhythm has an adjacent triplet; the chosen value is
      // accessible using K1 and visually distinct on the hardware display.
      for (int base = 0; base < 6; ++base) {
          app.setCueDivision(2 * base);
          const std::string normal = app.knobs(SamplerApp::P_CUE)[0].value;
          app.setCueDivision(2 * base + 1);
          const std::string triplet = app.knobs(SamplerApp::P_CUE)[0].value;
          CHECK(triplet == normal + "T",
                "NOTE REPEAT supports straight %s and triplet %s",
                normal.c_str(), triplet.c_str());
      }
      app.setCueDivision(4); // restore 1/4 for later tests
      // Hardware-only modifiers: NO added OCT+/OCT- buttons on Android.
      // The original deck transport and project editor must remain available.
      SamplerUI touchUi(app);
      touchUi.setSize(860.f, 400.f, 0.f, 0.f);
      touchUi.frame(now);
      bool modifierButton = false;
      for (const auto& op : touchUi.ops()) if (op.type == UiOp::TEXT)
          modifierButton |= op.text == "OCT+" || op.text == "OCT-";
      CHECK(!modifierButton, "deck has no OCT+ or OCT- touch buttons");
      // Touch where the removed modifier buttons used to be: it must not
      // synthetically press hardware keys.
      touchUi.touch(0, 294.f, 234.f, now);
      touchUi.touch(2, 294.f, 234.f, now);
      touchUi.touch(0, 294.f, 265.f, now);
      touchUi.touch(2, 294.f, 265.f, now);
      CHECK(!app.cueModifierHeld() && !app.octDownModifierHeld(),
            "removed touch modifiers cannot arm physical octave keys");
      touchUi.setEditor(true);
      touchUi.frame(now);
      modifierButton = false;
      for (const auto& op : touchUi.ops()) if (op.type == UiOp::TEXT)
          modifierButton |= op.text == "OCT+" || op.text == "OCT-";
      CHECK(!modifierButton, "editor has no OCT+ or OCT- touch buttons");
      // The space previously occupied by synthetic OCT buttons belongs to
      // the original editor's loop-source control. Do not tap it here: doing so
      // would change the test recording source and prevent scene recordings.

      // Normal phone PLAY and STOP do not initiate an OCT- combo, even if
      // the physical octave key is held. A real MIDI PLAY/STOP event must
      // supply the second press to trigger the immediate restart.
      app.transportSet(true); run(0.05);
      s.seek(0.67f); run(0.04);
      hold(SamplerApp::OCT_DOWN, true);
      app.transportSet(true); run(0.04);
      CHECK(app.status().pos > app.status().loopSamples * 0.4,
            "touch PLAY does not trigger the hardware-only OCT- shortcut");
      hold(SamplerApp::PLAY, true); run(0.02);
      CHECK(app.status().pos < app.status().loopSamples * 0.16,
            "physical OCT- + PLAY still restarts immediately");
      hold(SamplerApp::PLAY, false);
      hold(SamplerApp::OCT_DOWN, false);
      run(0.04);

      s.seek(0.64f); run(0.03);
      hold(SamplerApp::PLAY, true); run(0.04);
      CHECK(!app.status().playing, "unmodified PLAY/STOP toggles immediately on key-down");
      hold(SamplerApp::PLAY, false); run(0.04);
      CHECK(!app.status().playing, "releasing unmodified PLAY does not toggle transport twice");
      // Restore the tape to a running state before the next scene-recording test.
      // This was previously done by the now-removed phone STOP test.
      app.transportSet(true); run(0.15);
      app.loopClear(); run(0.3); }
    {   // Live C1/D1 scenes, independent track groups, ordered song, persistence and light/dark UI.
        app.selectTrack(0); app.loopClear(); run(0.2);
        app.loopRecord(); run(0.08);
        app.playNote(60, true); run(0.75); app.playNote(60, false); run(0.18);
        app.loopRecord(); run(0.25);
        CHECK(app.loopExists(), "scenes: first loop exists");
        app.selectScene(0); app.saveScene(); run(0.16);
        CHECK((app.status().sceneMask & 1u) && app.status().sceneTracks[0] == 1u,
              "C1 contains exactly track 1 (mask %u)", (unsigned)app.status().sceneTracks[0]);
        // Build a different pack: add a second loop while track 1 is already sounding.
        app.selectTrack(1); app.loopRecord(); run(0.15);
        app.playNote(65, true); run(0.4); app.playNote(65, false); run(0.25);
        app.loopRecord(); run(0.18);
        app.selectScene(2); app.saveScene(); run(0.22);
        CHECK((app.status().sceneMask & 5u) == 5u &&
              (app.status().sceneTracks[2] & 3u) == 3u,
              "D1 contains tracks 1+2 while C1 preserves only track 1");
        const int32_t compositionLength = app.status().loopSamples;
        const uint32_t originalMask = ((uint32_t)1 << 0) | ((uint32_t)1 << 1);
        CHECK((app.status().track_has_clip[0] && app.status().track_has_clip[1]),
              "composition has two separately recorded loops before performance");
        std::vector<float> baseline0(64), baseline1(64);
        s.loopPeaks(0, baseline0.data(), (int)baseline0.size());
        s.loopPeaks(1, baseline1.data(), (int)baseline1.size());
        app.setSongMode(false);   // snapshot composition before the scene bank
        uint8_t c1[3] = {0x90, 36, 100}, d1[3] = {0x90, 38, 100};
        app.onMidi(c1, 3); run(1.2);
        CHECK(app.status().activeScene == 0 && !app.status().track_has_clip[1],
              "pressing Ableton MIDI C1 (36) launches scene C1 without changing D1");
        app.exitPerformanceMode(); run(0.25);
        CHECK(!app.performanceActive() && app.status().track_has_clip[0] &&
              app.status().track_has_clip[1] && app.status().loopSamples == compositionLength,
              "EXIT restores all composition clips and original loop length after launching C1");
        std::vector<float> restored0(64), restored1(64);
        s.loopPeaks(0, restored0.data(), (int)restored0.size());
        s.loopPeaks(1, restored1.data(), (int)restored1.size());
        float maxDiff = 0.f;
        for (size_t i = 0; i < baseline0.size(); ++i)
            maxDiff = std::max(maxDiff, std::max(std::fabs(restored0[i] - baseline0[i]),
                                               std::fabs(restored1[i] - baseline1[i])));
        CHECK(maxDiff < 1e-5f, "EXIT restores the exact original loop audio (peak diff %.7f)", maxDiff);
        app.setSongMode(false); run(0.06);
        app.onMidi(d1, 3); run(1.2);
        CHECK(app.status().activeScene == 2 && app.status().track_has_clip[1],
              "pressing Ableton MIDI D1 (38) launches a different loop structure");
        // Scientific C1=24 also launches the C1 scene for alternate controllers.
        uint8_t scientificC1[3] = {0x90, 24, 100};
        app.onMidi(scientificC1, 3); run(1.1);
        CHECK(app.status().activeScene == 0, "scientific MIDI C1 (24) also launches C1");
        // FM-1 physical first C-scene key uses firmware's 53 offset.
        uint8_t physicalC1[3] = {0x90, 53, 100};
        app.onMidi(physicalC1, 3); run(1.1);
        CHECK(app.status().activeScene == 0, "FM-1 physical first note launches C1");
        app.setSongMode(true);
        app.selectScene(0); app.songAdd();
        app.selectScene(2); app.songAdd();
        app.selectScene(0); app.songAdd(); run(0.1);
        CHECK(app.songOrder().size() == 3 && app.songOrder()[0] == 0 &&
              app.songOrder()[1] == 2 && app.songOrder()[2] == 0,
              "Song Mode keeps C1, D1, C1, including repeated scenes");
        app.songMove(-1);
        CHECK(app.songOrder()[1] == 0 && app.songOrder()[2] == 2, "Song Mode can reorder steps");
        app.songMove(+1); run(0.1);
        app.requestSongExport();
        CHECK(app.wantSongExport(), "valid three-step SONG requests processed export");
        const std::string songDir = "/tmp/fm1sampler_t2/song_export_three_scenes";
        CHECK(s.exportSong(songDir), "export entire C1-D1-C1 SONG starts");
        for (int k = 0; k < 220 && s.loopExportStatus() == 1; ++k) run(0.04);
        std::vector<float> songAudio; int songRate = 0;
        const bool songOK = readWav(songDir + "/SONG.wav", songAudio, songRate);
        double songEnergy = 0;
        if (songOK) for (float v : songAudio) songEnergy += (double)v * v;
        CHECK(s.loopExportStatus() == 2 && songOK && songRate == sr &&
              songAudio.size() > (size_t)compositionLength * 2 * 3 &&
              songEnergy > 0.01,
              "EXPORT SONG renders all 3 ordered scenes plus the reverb/delay tail");
        app.playSong(); run(0.12);
        CHECK(app.status().songPlaying && app.status().songStep == 0 &&
              app.status().activeScene == 0, "Song starts on its first scene");
        app.exitPerformanceMode(); run(0.25);
        CHECK(app.status().track_has_clip[0] && app.status().track_has_clip[1] &&
              app.status().loopSamples == compositionLength && !app.status().songPlaying,
              "COMPOSE restores original eight-track recording workspace during SONG playback");
        app.setSongMode(true); run(0.06);
        app.playSong(); run(0.12);
        run(3.4);
        CHECK(!app.status().songPlaying && !app.status().playing,
              "Song ends after the final scene instead of looping forever");
        run(0.4);
        CHECK(std::filesystem::exists("/tmp/fm1sampler_t2/fm1_scenes.dat"),
              "saved scenes and Song Mode order are persisted for the next launch");
        {
            Session restored(sr);
            restored.setDataDir("/tmp/fm1sampler_t2");
            std::vector<float> o(512), input(512, 0.f);
            Session::Status loaded{};
            for (int i = 0; i < 80; ++i) {
                restored.process(o.data(), input.data(), 256);
                loaded = restored.status();
                if (loaded.arrangementLoaded) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            CHECK(loaded.arrangementLoaded && (loaded.sceneMask & 5u) == 5u &&
                  loaded.sceneTracks[0] == 1u && (loaded.sceneTracks[2] & 3u) == 3u &&
                  loaded.arrangementSize == 3 && loaded.arrangement[0] == 0 &&
                  loaded.arrangement[1] == 2 && loaded.arrangement[2] == 0,
                  "saved C1/D1 loop packs and the C1-D1-C1 song survive app restart");
        }

        // Project sheet must render two separate columns in both existing themes.
        SamplerUI ui(app); ui.setSize(860.f, 400.f, 0.f, 0.f);
        ui.openProject(false);
        ui.setShortcutHelpVisible(true);
        for (bool light : {true, false}) {
            theme::apply(light);
            ui.frame(now + 1);
            bool restart = false, repeat = false, noteMenu = false;
            bool hide = false, midiStatus = false, last = false;
            for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
                restart |= op.text == "OCT− + PLAY/STOP: RESTART LOOP";
                repeat |= op.text == "OCT+ + PLAY: NOTE REPEAT";
                noteMenu |= op.text == "OCT+ + OCT- = NOTE REPEAT MENU (K1)";
                hide |= op.text == "HIDE";
                midiStatus |= op.text.find("MIDI OCT-:") != std::string::npos;
                last |= op.text.find("LAST CH") != std::string::npos;
            }
            CHECK(restart && repeat && noteMenu && hide && midiStatus && last,
                  "HOME info includes OCT+ + OCT- = NOTE REPEAT MENU and MIDI diagnostics in %s mode",
                  light ? "light" : "dark");
        }
        theme::apply(true);
        ui.frame(now + 3);
        ui.touch(0, 294, 84, now + 3);
        ui.touch(2, 294, 84, now + 3);
        CHECK(!ui.shortcutHelpVisible(), "HIDE chip collapses shortcut legend");
        ui.frame(now + 4);
        bool hasInfo = false, hasRepeat = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            hasInfo |= op.text == "INFO";
            hasRepeat |= op.text.find("OCT+ + PLAY: NOTE REPEAT") != std::string::npos ||
                         op.text.find("OCT+ + OCT- = NOTE REPEAT MENU") != std::string::npos;
        }
        CHECK(hasInfo && !hasRepeat, "collapsed legend displays only the compact INFO button");
        {
            SamplerUI restoredHelp(app);
            CHECK(!restoredHelp.shortcutHelpVisible(),
                  "shortcut visibility survives app UI recreation");
        }
        ui.touch(0, 297, 69, now + 4);
        ui.touch(2, 297, 69, now + 4);
        CHECK(ui.shortcutHelpVisible(), "INFO chip reopens physical shortcuts");
        ui.openProject(true);
        for (bool light : {true, false}) {
            theme::apply(light);
            ui.frame(now + (light ? 1 : 2));
            bool project = false, song = false, pad = false;
            for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
                project |= op.text == "PROJECT";
                song |= op.text == "SONG MODE";
                pad |= op.text == "C1";
            }
            CHECK(project && song && pad,
                  "PROJECT and scene/song editor render together in %s theme",
                  light ? "light" : "dark");
        }
        theme::apply(true);
        // A narrower screen must switch between project and scene columns
        // rather than draw both on top of each other.
        ui.setSize(640.f, 400.f, 0.f, 0.f);
        ui.frame(now + 3);
        bool narrowProject = false, narrowSong = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            narrowProject |= op.text == "PROJECT"; narrowSong |= op.text == "SONG MODE";
        }
        CHECK(narrowProject && !narrowSong, "narrow screen shows PROJECT alone");
        ui.touch(0, 380, 32, now + 3); ui.touch(2, 380, 32, now + 3);
        ui.frame(now + 4);
        narrowProject = narrowSong = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            narrowProject |= op.text == "PROJECT"; narrowSong |= op.text == "SONG MODE";
        }
        CHECK(narrowSong, "narrow screen switches to scene/song column");
        app.setSongMode(false);
        CHECK(app.performanceActive() && !app.songMode(),
              "LOOP mode enables scene-key bank");
        app.exitPerformanceMode(); run(0.08);
        CHECK(!app.performanceActive() && !app.songMode(),
              "EXIT LOOP returns to normal composition and chromatic keyboard");
        app.setSongMode(true); run(0.08);
        CHECK(app.performanceActive() && app.songMode(), "SONG mode enters successfully");
        app.exitPerformanceMode(); run(0.08);
        CHECK(!app.performanceActive() && !app.songMode(),
              "EXIT SONG returns to composition");
        // Verify touchable EXIT beside LAUNCH in the actual PROJECT right panel.
        ui.setSize(860.f, 400.f, 0.f, 0.f);
        ui.openProject(true);
        app.setSongMode(false); ui.frame(now + 7);
        bool exitVisible = false, composeVisible = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            exitVisible |= op.text == "EXIT";
            composeVisible |= op.text == "COMPOSE";
        }
        CHECK(exitVisible && composeVisible, "PROJECT shows EXIT beside LAUNCH and COMPOSE mode tab");
        // Right column x=445..846; four actions at y~221, EXIT is third.
        ui.touch(0, 701.f, 224.f, now + 7);
        ui.touch(2, 701.f, 224.f, now + 7);
        CHECK(!app.performanceActive(), "PROJECT EXIT button actually leaves LOOP mode");

        // FULL PROJECT FILES: eight independent slots, not merely the legacy
        // fm1_scenes.dat bank. Save must include the actual two COMPOSE clips,
        // both scene audio packs, the complete C1-D1-C1 song, track processing,
        // and the embedded WAVs for the original sampler assignments.
        app.selectTrack(0);
        const auto originalSong = app.songOrder();
        const auto originalScale = app.scale();
        const auto originalSceneMask = app.status().sceneMask;
        const auto originalAlgo = app.state(0).algo;
        const auto originalSample0 = app.sampleLabel(0);
        const auto originalSample1 = app.sampleLabel(1);
        CHECK(app.status().track_has_clip[0] && app.status().track_has_clip[1],
              "two COMPOSE recordings are present before full-project save");
        CHECK(app.saveFullProject(0), "Save Full Project starts asynchronous FMP2 snapshot");
        for (int spin = 0; spin < 20 && app.fullProjectOperation() == 1; ++spin) run(0.14);
        CHECK(app.fullProjectOperation() == 2 && app.fullProjectExists(0) &&
              std::filesystem::file_size("/tmp/fm1sampler_t2/project_1.fm1p") > 100000,
              "project slot 1 contains a real self-contained archive with audio");
        app.setAlgo(5); app.selectScene(0); app.deleteScene(); run(0.25);
        app.loopClear(); run(0.25);
        CHECK(!app.status().track_has_clip[0] && !app.status().track_has_clip[1],
              "the working composition can be emptied independently of saved project");
        CHECK(app.loadFullProject(0), "Load Full Project starts validated background restoration");
        for (int spin = 0; spin < 35 && app.fullProjectOperation() == 3; ++spin) run(0.12);
        run(0.65);
        CHECK(app.fullProjectOperation() == 4 &&
              app.status().track_has_clip[0] && app.status().track_has_clip[1],
              "project load restores both independent recorded COMPOSE loops");
        CHECK(app.status().sceneMask == originalSceneMask &&
              app.songOrder() == originalSong,
              "project load restores all scenes and exact song arrangement");
        CHECK(app.state(0).algo == originalAlgo && app.scale().scale == originalScale.scale &&
              app.scale().root == originalScale.root &&
              app.scale().octave == originalScale.octave,
              "project load restores per-track algorithm and exact scale state");
        CHECK(app.sampleLabel(0) == originalSample0 &&
              app.sampleLabel(1) == originalSample1 &&
              std::filesystem::exists("/tmp/fm1sampler_t2/project_assets/slot_1/track_1.wav"),
              "project embeds its source WAV and preserves original sampler names");
        CHECK(!app.performanceActive() && !app.songMode(),
              "project saved in COMPOSE loads in COMPOSE, not a different scene mode");
        app.refreshLibrary();
        CHECK(app.sampleLabel(0) == originalSample0 &&
              app.state(0).entry >= 0 &&
              app.library()[(size_t)app.state(0).entry].label == originalSample0,
              "library refresh keeps the restored source WAV assignment");
        CHECK(app.saveFullProject(3), "a restored project can be saved again into a new independent slot");
        for (int spin = 0; spin < 25 && app.fullProjectOperation() == 1; ++spin) run(0.12);
        CHECK(app.fullProjectOperation() == 2 && app.fullProjectExists(3),
              "re-saving after library refresh embeds the correct original WAV, not a stale entry index");
        CHECK(app.loadFullProject(3), "re-saved project reloads from an independent archive");
        for (int spin = 0; spin < 25 && app.fullProjectOperation() == 3; ++spin) run(0.12);
        run(0.3);
        CHECK(app.sampleLabel(0) == originalSample0 &&
              app.sampleLabel(1) == originalSample1 &&
              app.status().track_has_clip[0] && app.status().track_has_clip[1],
              "second-generation project preserves both source names and two recorded loops");

        // A second independent slot saves the running SONG, while its original
        // COMPOSE workspace remains separately recoverable through HOME/EXIT.
        app.setSongMode(true); run(0.12);
        app.playSong(); run(0.15);
        CHECK(app.status().songPlaying && app.performanceActive() && app.songMode(),
              "SONG playback is live before snapshot");
        CHECK(app.saveFullProject(1), "save full project while SONG mode is active");
        for (int spin = 0; spin < 25 && app.fullProjectOperation() == 1; ++spin) run(0.11);
        CHECK(app.fullProjectOperation() == 2 && app.fullProjectExists(1),
              "second independent slot stores SONG plus original COMPOSE");
        app.exitPerformanceMode(); run(0.2);
        CHECK(app.loadFullProject(1), "reload the second SONG-mode snapshot");
        for (int spin = 0; spin < 35 && app.fullProjectOperation() == 3; ++spin) run(0.10);
        run(0.08);
        CHECK(app.fullProjectOperation() == 4 && app.performanceActive() &&
              app.songMode() && app.songOrder() == originalSong,
              "SONG project restores playback mode and full C1-D1-C1 arrangement");
        CHECK(app.status().songPlaying && app.status().activeScene >= 0,
              "SONG snapshot resumes its active scene and transport");
        app.exitPerformanceMode(); run(0.30);
        CHECK(app.status().track_has_clip[0] && app.status().track_has_clip[1],
              "EXIT after a reloaded SONG restores the saved COMPOSE recordings");

        // Truncated project must never overwrite the current unsaved workspace.
        {
            std::ifstream original("/tmp/fm1sampler_t2/project_1.fm1p", std::ios::binary);
            std::ofstream broken("/tmp/fm1sampler_t2/project_3.fm1p", std::ios::binary);
            std::array<char, 1024> fragment{};
            original.read(fragment.data(), (std::streamsize)fragment.size());
            broken.write(fragment.data(), original.gcount());
        }
        CHECK(app.loadFullProject(2), "corrupt archived slot is detected asynchronously");
        for (int spin = 0; spin < 25 && app.fullProjectOperation() == 3; ++spin) run(0.10);
        CHECK(app.fullProjectOperation() == -2 &&
              app.status().track_has_clip[0] && app.status().track_has_clip[1],
              "corrupt/truncated project cannot destroy the active composition");

        ui.openProject(true);
        ui.frame(now + 9);
        bool fullSave = false, fullLoad = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            fullSave |= op.text == "SAVE PROJECT";
            fullLoad |= op.text == "LOAD PROJECT";
        }
        CHECK(fullSave && fullLoad, "PROJECT page has distinct SAVE/LOAD COMPLETE PROJECT actions");
        bool hasSongExport = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT)
            hasSongExport |= op.text == "EXPORT SONG";
        CHECK(hasSongExport, "PROJECT shows EXPORT SONG beside CLOSE");
        ui.touch(0, 80.f, 64.f, now + 9);
        ui.touch(2, 80.f, 64.f, now + 9);
        ui.frame(now + 10);
        bool slotPicker = false, slot1 = false, slot2 = false, page1 = false;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            slotPicker |= op.text == "SAVE COMPLETE PROJECT";
            slot1 |= op.text == "PROJECT 1";
            slot2 |= op.text == "PROJECT 2";
            page1 |= op.text == "PAGE 1 / 2";
        }
        CHECK(slotPicker && slot1 && slot2 && page1,
              "full-project save picker shows independent slots 1-8 on page 1");
        // 860 x 400 view: slot panel spans x=192..667, y=51..349.
        // Navigate to page 2 and verify all new slots are addressable.
        ui.touch(0, 602, 291, now + 11);
        ui.touch(2, 602, 291, now + 11);
        ui.frame(now + 12);
        bool page2 = false, slot9 = false, slot16 = false, previousHidden = true;
        for (const auto& op : ui.ops()) if (op.type == UiOp::TEXT) {
            page2 |= op.text == "PAGE 2 / 2";
            slot9 |= op.text == "PROJECT 9";
            slot16 |= op.text == "PROJECT 16";
            if (op.text == "PROJECT 1") previousHidden = false;
        }
        CHECK(page2 && slot9 && slot16 && previousHidden,
              "page 2 shows slots 9-16 without replacing slots 1-8");
        CHECK(Session::kProjectSlots == 16,
              "FMP2 storage exposes 16 slots with unchanged filenames for older projects");
        CHECK(app.saveFullProject(15), "slot 16 accepts full-project save");
        for (int spin = 0; spin < 25 && app.fullProjectOperation() == 1; ++spin) run(0.12);
        CHECK(app.fullProjectOperation() == 2 &&
              std::filesystem::exists("/tmp/fm1sampler_t2/project_16.fm1p"),
              "project_16.fm1p saved; slot 1 remains untouched");
        ui.openProject(false);
        app.loopClear(); run(0.25);
    }
    {   // loudness normalisation and auto-tune, on synthetic samples: a quiet and a loud 220 Hz tone
      auto tone = [&](const char* path, float amp, double hz) { std::vector<float> st; for (int i = 0; i < 2 * sr; i++) { const float v = amp * (float)std::sin(2 * M_PI * hz * i / sr) * (i < 100 ? i / 100.f : 1.f); st.push_back(v); st.push_back(v); } writeWav16(path, st, sr); };
      tone("/tmp/fm1sampler_t2/tone_quiet.wav", 0.03f, 220.0); tone("/tmp/fm1sampler_t2/tone_loud.wav", 0.95f, 220.0); tone("/tmp/fm1sampler_t2/tone_flat.wav", 0.5f, 220.0 * std::pow(2.0, 0.3 / 12));
      s.loadSamplePath("/tmp/fm1sampler_t2/tone_quiet.wav", false, 6); s.loadSamplePath("/tmp/fm1sampler_t2/tone_loud.wav", false, 7); run(2.0);
      float pkq = 0, pkl = 0; auto pq = s.samplePeaksOf(6, 64), pl = s.samplePeaksOf(7, 64); for (float v : pq) pkq = std::max(pkq, v); for (float v : pl) pkl = std::max(pkl, v);
      CHECK(pkq > 0.2f && std::fabs(pkq - pkl) < 0.05f * std::max(pkq, pkl) + 0.02f, "loudness normalisation: quiet (%.3f) and loud (%.3f) tones end up equally loud", pkq, pkl);
      {   // a sparse drum hit (a sharp decaying burst, then silence) must not stay quiet next to a sustained tone
        std::vector<float> st; unsigned rs = 1; for (int i = 0; i < 2 * sr; i++) { rs = rs * 1664525u + 1013904223u; const float nz = ((rs >> 8) & 0xffff) / 32768.f - 1.f; const float v = 0.9f * nz * std::exp(-i / (0.04f * sr)); st.push_back(v); st.push_back(v); }
        writeWav16("/tmp/fm1sampler_t2/drum_hit.wav", st, sr); s.loadSamplePath("/tmp/fm1sampler_t2/drum_hit.wav", false, 4); run(1.5);
        auto pd = s.samplePeaksOf(4, 64); float pkd = 0; for (float v : pd) pkd = std::max(pkd, v);
        CHECK(pkd > 0.7f && pkd <= 1.0f, "drum hit: peaks high but limited (%.3f)", pkd); }
      const float m = s.detectRoot(6); CHECK(std::fabs(m - 57.f) < 0.15f, "auto-tune: a 220 Hz tone is detected as A3 = note 57 (%.3f)", m);
      s.loadSamplePath("/tmp/fm1sampler_t2/tone_flat.wav", false, 5); run(1.0);
      const float mf = s.detectRoot(5); CHECK(std::fabs(mf - 57.3f) < 0.15f, "...and a tone 30 cents sharp as 57.3 (%.3f)", mf);
      app.selectTrack(5); app.setAlgo(0); run(0.2); app.autoTune(); run(0.2); CHECK(app.tuned(5), "autoTune marks the track tuned"); app.autoTune(); CHECK(!app.tuned(5), "...and a second tap resets it");
      app.setAlgo(2); app.autoTune(); CHECK(!app.tuned(5), "no auto-tune in LOOPED mode"); app.setAlgo(0); app.selectTrack(0); }
    {   // scrubbing the timeline with a finger: smooth, forward and backward, no clicks; the tape returns to speed when the finger lifts
      app.selectTrack(0); app.loopRecord(); run(0.1); app.playNote(60, true); run(0.8); app.playNote(60, false); app.playNote(67, true); run(0.8); app.playNote(67, false); run(0.2);
      app.transportPlay(); run(0.6);
      auto maxStep = [&](size_t m0) { float mx = 0; for (size_t i = m0 + 2; i < all.size(); i += 2) mx = std::max(mx, std::fabs(all[i] - all[i - 2])); return mx; };
      CHECK(app.loopExists() && app.status().playing, "a loop plays (for the scrub test)");
      size_t b0 = all.size(); run(1.0); const float base = maxStep(b0);
      size_t s0 = all.size(); app.scrub(0, 0.5f); run(0.1);
      for (int k = 0; k < 24; k++) { app.scrub(1, 0.5f + 0.35f * (float)std::sin(k * 0.45)); run(0.04); }
      app.scrub(1, 0.2f); run(0.7); const float at = app.status().clipPos[0];
      CHECK(std::fabs(at - 0.2f) < 0.05f, "the tape follows the finger to its place (%.3f, wanted 0.2)", at);
      app.scrub(2, 0); run(0.8);
      const float sm = maxStep(s0);
      CHECK(sm < 5.f * base + 0.1f, "no clicks while scrubbing (largest step %.3f, normal playback %.3f)", sm, base);
      const float p1 = app.status().clipPos[0]; run(0.3); CHECK(std::fabs(app.status().clipPos[0] - p1) > 0.02f && app.status().playing, "after the finger lifts the loop plays on (%.3f -> %.3f)", p1, app.status().clipPos[0]);
      app.transportPlay(); run(1.2); const float ps = app.status().clipPos[0]; app.scrub(0, 0.6f); run(0.3); app.scrub(2, 0); run(0.8);
      CHECK(!app.status().playing && std::fabs(app.status().clipPos[0] - 0.6f) < 0.06f, "scrubbing a stopped tape moves it, it stays stopped afterwards (%.3f -> %.3f)", ps, app.status().clipPos[0]);
      app.loopClear(); run(0.3); }
    {   // the first take can be drawn while it is recorded
      app.selectTrack(0); app.loopClear(); run(0.3); app.loopRecord(); run(0.1); app.playNote(60, true); run(0.8); app.playNote(60, false); run(0.3);
      float fp[64]; const int fn = s.freePeaks(fp, 64); float fm = 0; for (int i = 0; i < fn; i++) fm = std::max(fm, fp[i]);
      CHECK(app.status().freeRecording && fn >= 15 && fm > 0.05f, "the first take has peaks while recording (%d bins, max %.3f)", fn, fm);
      app.loopRecord(); run(0.3); app.loopClear(); run(0.3); }
    // microphone sample
    btn(SamplerApp::SAVE);
    enc(2, 1); run(0.3); CHECK(app.micState() == 1, "mic sampling waits for sound");
    { double ph = 0; for (long i = 0; i < (long)(1.0 * sr) / 256; i++) { for (int k = 0; k < 256; k++) { float v = 0.4f * std::sin(ph); ph += 2 * M_PI * 440.0 / sr; in[2 * k] = in[2 * k + 1] = v; } std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(200)); if (i % 5 == 0) { now += 33; app.tick(now); } } std::fill(in.begin(), in.end(), 0.f); }
    CHECK(app.micState() == 2, "mic sampling recording");
    enc(2, -1); run(3.0);
    const bool micInSaved = std::any_of(app.library().begin(), app.library().end(),
                                            [](const SamplerApp::Entry& e) {
                                                return e.pack == "SAVED" && e.folder == "RECORDINGS";
                                            });
    CHECK(app.micState() == 0 && micInSaved,
          "microphone recording is available in SAVED/RECORDINGS (%s)",
          app.sampleLabel(0).c_str());
    // import from disk: a WAV dropped into the data dir
    { std::ifstream src(std::string(getenv("HOME")) + "/fm1-sampler/samples/Bass/Fuzz.wav", std::ios::binary); std::ofstream dst("/tmp/fm1sampler_t2/sample_FromDisk.wav", std::ios::binary); dst << src.rdbuf(); }
    app.selectTrack(4); app.importFile("/tmp/fm1sampler_t2/sample_FromDisk.wav"); run(2.0);
    CHECK(app.hasSample(4) && app.sampleLabel(4) == "sample_FromDisk", "import from disk loads into the selected track (%s)", app.sampleLabel(4).c_str());
    // presets audition
    app.selectTrack(4); size_t pm = all.size(); enc(6, 1); run(1.5); CHECK(rmsFrom(pm) > 0.002, "browsing plays the sample (rms %.4f)", rmsFrom(pm));
    // DSP units: offline stretch / pitch and the slicer
    {
        const int N = 44100; std::vector<float> sine((size_t)N * 2);
        for (int i = 0; i < N; i++) sine[(size_t)i * 2] = sine[(size_t)i * 2 + 1] = 0.5f * std::sin(2 * M_PI * 440.0 * i / 44100.0);
        auto freq = [&](const std::vector<float>& v) { int zc = 0; const size_t n = v.size() / 2; for (size_t i = 1; i < n; i++) if ((v[i * 2] > 0) != (v[(i - 1) * 2] > 0)) zc++; return zc / 2.0 / (n / 44100.0); };
        auto fast = stretchPitch(sine, 44100, 2.0, 0), up = stretchPitch(sine, 44100, 1.0, 12), slow = stretchPitch(sine, 44100, 0.5, 0);
        CHECK(std::fabs(fast.size() / 2.0 - N / 2.0) < 4000 && std::fabs(freq(fast) - 440) < 15, "stretch 2x: %.2fs at %.0f Hz", fast.size() / 2.0 / 44100, freq(fast));
        CHECK(std::fabs(slow.size() / 2.0 - N * 2.0) < 6000 && std::fabs(freq(slow) - 440) < 15, "stretch 0.5x: %.2fs at %.0f Hz", slow.size() / 2.0 / 44100, freq(slow));
        CHECK(std::fabs(up.size() / 2.0 - N) < 100 && std::fabs(freq(up) - 880) < 20, "pitch +12: %.2fs at %.0f Hz", up.size() / 2.0 / 44100, freq(up));
        std::vector<float> hits((size_t)N * 2 * 2, 0.f);   // three clicks with decays
        for (int h : {2000, 30000, 60000}) for (int i = 0; i < 6000; i++) hits[(size_t)(h + i) * 2] = hits[(size_t)(h + i) * 2 + 1] = std::exp(-i / 800.f) * std::sin(i * 0.3f);
        auto sl = findSlices(hits, N * 2, 44100, 16);
        auto near = [&](int t) { for (int v : sl) if (std::abs(v - t) < 600) return true; return false; };
        CHECK(near(2000) && near(30000) && near(60000) && sl.size() <= 5, "slicer finds the three hits (%zu slices)", sl.size());
        auto on = findOnsets(hits, N * 2, 44100);
        auto lo = pickSlices(on, 0, N * 2, 44100, 0.0f), hi = pickSlices(on, 0, N * 2, 44100, 1.0f), win = pickSlices(on, 20000, N * 2, 44100, 0.2f);
        CHECK(lo.size() >= hi.size() && !win.empty() && win[0] >= 20000, "threshold: %zu slices at 0, %zu at 1, window starts at %d", lo.size(), hi.size(), win.empty() ? -1 : win[0]);
        auto eq = findSlices(std::vector<float>((size_t)N * 2, 0.1f), N, 44100, 16);
        CHECK(eq.size() == 16, "no hits: %zu equal slices", eq.size());
    }
    {   // sidechain: track 2 ducked by track 1, send effects return something
        app.loopClear(); run(0.3);
        for (int tr : {0, 1}) { app.selectTrack(tr); app.setAlgo(2); }
        auto level = [&]() { app.selectTrack(1); app.playNote(60, true); run(0.5); app.selectTrack(0); app.playNote(60, true); run(0.9); float pk = 0; for (int i = 0; i < 20; i++) { run(0.03); pk = std::max(pk, app.status().track_peak[1]); } app.playNote(60, false); app.selectTrack(1); app.playNote(60, false); run(0.8); return pk; };
        app.selectTrack(1); { auto k = app.knobs(SamplerApp::P_MIX); k[1].set(0.f); } const float open = level();
        { auto k = app.knobs(SamplerApp::P_MIX); k[1].set(1.f / 8.f); k = app.knobs(SamplerApp::P_MIX); k[2].set(0.1f); k = app.knobs(SamplerApp::P_MIX); k[3].set(1.f); } run(0.2);
        const float ducked = level();
        CHECK(open > 0.01f && ducked < open * 0.6f, "sidechain: track 2 ducked by track 1 (%.3f -> %.3f)", open, ducked);
        { auto k = app.knobs(SamplerApp::P_MIX); k[1].set(0.f); }
        app.selectTrack(1); app.setAlgo(0); app.selectTrack(0); app.setAlgo(0);
    }
    {   // Navigation stays on HOME: PRESETS selects a row in its current folder
        // and loads its WAV immediately. ENTER navigates into folders only;
        // BACK returns one level. Android's document picker never opens.
        namespace fs = std::filesystem;
        const fs::path root(s.sampleDir());
        const fs::path nested = root / "PACKS" / "My Imported ZIP" / "Drums" / "Kicks";
        fs::create_directories(nested);
        fs::copy_file(root / "Bass" / "Fuzz.wav", nested / "01_fuzz.wav",
                      fs::copy_options::overwrite_existing);
        fs::copy_file(root / "Bass" / "Fuzz.wav", nested / "02_fuzz.wav",
                      fs::copy_options::overwrite_existing);
        app.refreshLibrary();
        app.browserRoot();
        app.selectTrack(0);
        SamplerUI browser(app);
        browser.setSize(860.f, 400.f, 0.f, 0.f);
        browser.frame(now);
        auto click = [&](float x, float y) {
            browser.touch(0, x, y, now);
            browser.touch(2, x, y, now);
            browser.frame(now);
        };
        auto hasText = [&](const std::string& label) {
            for (const auto& op : browser.ops())
                if (op.type == UiOp::TEXT && op.text.find(label) != std::string::npos) return true;
            return false;
        };
        CHECK(hasText("BACK") && hasText("ENTER") && hasText("FM1-SAMPLER") &&
              !hasText("+ IMPORT ZIP"),
              "HOME shows only inline BACK/ENTER and a filename, no modal browser");
        bool largeLeftBadge = false, decoratedTrack = false, trackCaption = false;
        for (const auto& op : browser.ops()) {
            if (op.type == UiOp::TEXT && op.text == "T1" &&
                op.f[0] > 395 && op.f[0] < 440 && op.f[1] < 140 &&
                op.f[2] >= 25)
                largeLeftBadge = true;
            if (op.type == UiOp::TEXT && op.text == "TRACK" &&
                op.f[0] > 380 && op.f[0] < 440 && op.f[1] < 140)
                trackCaption = true;
            // Reject the old badge background, border and orange stripe.
            if ((op.type == UiOp::RRECT || op.type == UiOp::STROKE) &&
                op.f[0] >= 385 && op.f[0] < 440 && op.f[1] >= 30 &&
                op.f[1] < 140 && op.f[2] < 60 && op.f[3] > 40)
                decoratedTrack = true;
        }
        CHECK(largeLeftBadge && !decoratedTrack && !trackCaption,
              "ONLY large orange T1 to the left of waveform: no TRACK caption, background, frame or stripe");
        click(410.f, 80.f);
        CHECK(app.track() == 1 && hasText("T2"),
              "touching large waveform-side T1/T2 badge cycles selected track");
        click(351.f, 184.f);
        CHECK(app.browserDir().empty() && !app.wantFile() &&
              hasText("+ IMPORT ZIP") && hasText("OFFICIAL") &&
              hasText("PACKS") && hasText("PROYECTOS") && hasText("SAVED"),
              "HOME LIBRARY opens previous in-app library page with IMPORT ZIP");
        click(205.f, 365.f); // actual + IMPORT ZIP button in library sheet
        CHECK(app.wantPack() && !app.wantPack() && !app.wantFile(),
              "library + IMPORT ZIP requests ZIP picker without changing the sample cursor");
        CHECK(hasText("SAVE BACKUP") && hasText("IMPORT BACKUP"),
              "LIBRARY footer presents both portable backup controls");
        click(349.f, 365.f);
        CHECK(app.takeDialogRequest() == 4,
              "SAVE BACKUP requests Android Create Document for a full library ZIP");
        click(537.f, 365.f);
        CHECK(app.takeDialogRequest() == 5,
              "IMPORT BACKUP requests Android Open Document and verified restore");
        app.browserBack(); // deselect to show the archive root itself
        enc(6, 1); // first item: OFFICIAL
        CHECK(app.browserSelected() && app.browserSelected()->label == "OFFICIAL",
              "PRESETS turns to the next directory entry");
        enc(6, 1); // second item: PACKS
        CHECK(app.browserSelected() && app.browserSelected()->label == "PACKS",
              "PRESETS moves row by row within root, including folders");
        click(200.f, 164.f); // sheet ENTER -> PACKS
        CHECK(app.browserDir() == "PACKS" && hasText("My Imported ZIP"),
              "ENTER visits selected folder, showing its name in HOME");
        click(200.f, 164.f); // sheet ENTER -> pack
        CHECK(app.browserDir() == "PACKS/My Imported ZIP",
              "ENTER visits imported ZIP's own subdirectory");
        click(200.f, 164.f); // sheet ENTER -> Drums
        CHECK(app.browserDir() == "PACKS/My Imported ZIP/Drums",
              "ENTER visits nested Drums folder");
        app.selectTrack(4);
        click(200.f, 164.f); // sheet ENTER -> Kicks; auto-select first WAV on track 5
        run(1.0);
        browser.frame(now);
        CHECK(app.browserDir() == "PACKS/My Imported ZIP/Drums/Kicks" &&
              app.browserSelected() && app.browserSelected()->label == "01_fuzz.wav" &&
              app.sampleLabel(4) == "01_fuzz",
              "first WAV is loaded as preset upon selection; no ENTER required");
        enc(6, 1); // PRESETS selects next WAV; must load without ENTER
        run(1.0); browser.frame(now);
        CHECK(app.browserSelected() && app.browserSelected()->label == "02_fuzz.wav" &&
              app.sampleLabel(4) == "02_fuzz",
              "PRESETS auto-loads next WAV immediately without pressing ENTER");
        click(200.f, 164.f); // ENTER over WAV does not change folder or sample
        CHECK(app.browserDir() == "PACKS/My Imported ZIP/Drums/Kicks" &&
              app.sampleLabel(4) == "02_fuzz",
              "ENTER on an audio file has no action (already loaded)");
        click(200.f, 126.f); // sheet BACK highlights Kicks in its parent
        CHECK(app.browserDir() == "PACKS/My Imported ZIP/Drums" &&
              app.browserSelected() && app.browserSelected()->label == "Kicks",
              "BACK returns one level and highlights previous folder");
        click(200.f, 126.f); click(200.f, 126.f); click(200.f, 126.f);
        CHECK(app.browserDir().empty() && hasText("FM1-SAMPLER"),
              "BACK returns all the way to internal FM1-SAMPLER root");
        click(200.f, 126.f); // BACK again highlights the FM1-SAMPLER root itself
        CHECK(app.browserDir().empty() && app.browserSelected() == nullptr &&
              hasText("FM1-SAMPLER"),
              "BACK on FM1-SAMPLER root selects the archive, without a child");
        click(200.f, 164.f); // ENTER directly from archive root: NO PRESETS detent needed
        CHECK(app.browserDir() == "PACKS" && hasText("My Imported ZIP"),
              "ENTER from FM1-SAMPLER root reopens last selected folder on LIBRARY sheet");
        click(200.f, 126.f); // PACKS -> root, still highlighted
        click(200.f, 126.f); // deselect child; FM1-SAMPLER root
        CHECK(app.browserDir().empty() && app.browserSelected() == nullptr,
              "repeated BACK can reach the unselected archive root again");
        click(660.f, 29.f); // CLOSE the library sheet to get back to HOME
        CHECK(!hasText("+ IMPORT ZIP") && hasText("LIBRARY"),
              "closing the library sheet restores HOME controls");
        click(91.f, 26.f); // HOME's ENTER must use the same root navigator
        CHECK(app.browserDir() == "PACKS" && hasText("My Imported ZIP"),
              "HOME ENTER from FM1-SAMPLER root navigates correctly without PRESETS");
        click(40.f, 26.f); // HOME BACK -> root, highlighted PACKS
        click(40.f, 26.f); // HOME BACK -> root itself
        click(91.f, 26.f); // HOME ENTER again, must not lock up
        CHECK(app.browserDir() == "PACKS" && !app.wantFile(),
              "BACK then ENTER at root remains repeatable without external file picker");
        click(351.f, 248.f);
        CHECK(hasText("SAVE PROJECT") && hasText("LOAD PROJECT") &&
              hasText("EXPORT ALL LOOPS"), "PROJECT retains saving and exporting actions");
        click(351.f, 67.f);
        CHECK(app.wantLoopExport() && !app.wantLoopExport(),
              "EXPORT ALL LOOPS still requests Android's public download export");
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
