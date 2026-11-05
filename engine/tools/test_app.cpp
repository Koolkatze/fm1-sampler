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
    int wavs = 0; { FILE* p = popen("ls /tmp/fm1sampler_t2/sample_*.wav 2>/dev/null | wc -l", "r"); if (p) { fscanf(p, "%d", &wavs); pclose(p); } }
    CHECK(wavs == 1, "SAVE wrote the loop into the library (%d files)", wavs);
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
      s.seek(0.65f); run(0.06);
      // No timer: verify the audio pointer on the very next audio callback.
      hold(SamplerApp::OCT_DOWN, true);
      hold(SamplerApp::PLAY, true); run(0.02);
      CHECK(app.status().pos < app.status().loopSamples * 0.16 && app.status().playing,
            "OCT- + PLAY instantly restarts on second key-down (%.1f)", app.status().pos);
      hold(SamplerApp::PLAY, false); hold(SamplerApp::OCT_DOWN, false); run(0.02);
      CHECK(app.status().playing, "combo PLAY release does not toggle transport");
      s.seek(0.72f); run(0.04);
      hold(SamplerApp::OCT_DOWN, true); hold(SamplerApp::STOP, true); run(0.02);
      CHECK(app.status().pos < app.status().loopSamples * 0.16 && app.status().playing,
            "OCT- + STOP instantly restarts, not stops (%.1f)", app.status().pos);
      hold(SamplerApp::STOP, false); hold(SamplerApp::OCT_DOWN, false);
      s.seek(0.72f); run(0.04);
      hold(SamplerApp::PLAY, true); hold(SamplerApp::OCT_DOWN, true); run(0.02);
      CHECK(app.status().pos < app.status().loopSamples * 0.16 && app.status().playing,
            "PLAY then OCT- also restarts on second key-down");
      hold(SamplerApp::OCT_DOWN, false); hold(SamplerApp::PLAY, false); run(0.02);
      CHECK(app.status().playing, "reverse combo release does not stop");
      CHECK(app.scale().octave == octaveBefore, "OCT modifier does not shift the app's octave");
      const auto originalPage = app.page();
      hold(SamplerApp::OCT_UP, true); run(2.15);
      CHECK(app.page() == SamplerApp::P_CUE && app.knobs()[0].value == "1/4",
            "OCT+ held two seconds opens CUE at 1/4");
      app.knobs()[0].set(1.f); run(0.05);
      CHECK(app.knobs()[0].value == "1/32", "K1 chooses the 1/32 beat division");
      hold(SamplerApp::PLAY, true);
      run(0.15); const float cq0 = app.status().clipPos[0];
      run(0.10); const float cq1 = app.status().clipPos[0];
      CHECK(std::fabs(cq1 - cq0) < 0.15f,
            "while PLAY is held, CUE repeats a short BPM-locked region (%.3f -> %.3f)", cq0, cq1);
      enc(2, -1); run(0.05);
      CHECK(app.knobs()[0].value == "1/16", "K1 changes the subdivision live during CUE");
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
      CHECK(app.cueDivision() == 5 && app.cueActive(),
            "physical K1 selects 1/32 during held PLAY");
      enc(2, -5); run(0.08);
      CHECK(app.cueDivision() == 4 && app.cueActive(),
            "physical K1 changes rate live while PLAY stays held");
      hold(SamplerApp::PLAY, false); run(0.05);
      CHECK(!app.cueActive() && app.status().playing, "releasing PLAY alone restores normal loop");
      hold(SamplerApp::OCT_UP, false);
      s.loopPeaks(0, after.data(), (int)after.size());
      float diff = 0.f;
      for (size_t i = 0; i < before.size(); ++i)
          diff = std::max(diff, std::fabs(after[i] - before[i]));
      CHECK(diff < 1.e-6f && app.status().loopSamples == savedLoopLength,
            "CUE never modifies the recorded loop audio or duration (diff %.8f)", diff);

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
      touchUi.touch(0, 365.f, 22.f, now);
      touchUi.touch(2, 365.f, 22.f, now);
      CHECK(!app.cueModifierHeld() && !app.octDownModifierHeld(),
            "former editor modifier area does not arm hardware octave keys");

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
      CHECK(app.status().playing, "PLAY alone no longer has a timed restart");
      hold(SamplerApp::PLAY, false); run(0.04);
      CHECK(!app.status().playing, "PLAY alone retains normal transport on release");
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
        app.setSongMode(false);   // enable the scene note bank explicitly
        uint8_t c1[3] = {0x90, 36, 100}, d1[3] = {0x90, 38, 100};
        app.onMidi(c1, 3); run(1.2);
        CHECK(app.status().activeScene == 0 && !app.status().track_has_clip[1],
              "pressing Ableton MIDI C1 (36) launches scene C1 without changing D1");
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
        app.playSong(); run(0.12);
        CHECK(app.status().songPlaying && app.status().songStep == 0 &&
              app.status().activeScene == 0, "Song starts on its first scene");
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
        ui.touch(0, 490, 32, now + 3); ui.touch(2, 490, 32, now + 3);
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
    CHECK(app.micState() == 0 && app.library().back().group == "User", "mic sample saved into the library and loaded (%s)", app.sampleLabel(0).c_str());
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
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
