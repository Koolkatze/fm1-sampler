#include "SamplerApp.h"
#include "../ui/Theme.h"
#include <Common.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace {
constexpr uint16_t rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
// The palette of the FM-1 screens: the same as the phone (theme.h), light or dark. BLACK = background, PAPER = ink.
uint16_t BLACK = rgb(241, 239, 233), PAPER = rgb(17, 17, 17), DIM = rgb(154, 150, 140), ACCENT = rgb(255, 90, 10), BAR = rgb(214, 211, 201);
void setPalette(bool light) {
    if (light) { BLACK = rgb(241, 239, 233); PAPER = rgb(17, 17, 17); DIM = rgb(154, 150, 140); BAR = rgb(214, 211, 201); ACCENT = rgb(255, 90, 10); }
    else { BLACK = rgb(0, 0, 0); PAPER = rgb(255, 255, 255); DIM = rgb(106, 106, 106); BAR = rgb(42, 42, 42); ACCENT = rgb(255, 106, 31); }
}
const char* kPage[] = {"SAMPLE", "FX", "ENVELOPE", "LFO", "ARP", "TAPE", "MIXER", "KEY", "LOOP", "LENGTH", "NOTE REPEAT", "SEQUENCER", "TEMPO / PITCH", "METRO TEMPO", "METRO SETTINGS", "CORRECT TEMPO"};
const char* kAlgo[] = {"MELODIC", "ONE-SHOT", "LOOPED", "KIT/SLICE", "GRANULAR", "PAULSTRETCH"};
const char* kScaleName[] = {"CHROM", "MIX", "MAJ", "MIN", "HMIN", "PHRYG", "MIN+", "OFF"};
const char* kNote[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* kArpRate[] = {"OFF", "1", "2", "2T", "4", "4T", "8", "8T", "16", "16T", "32", "32T", "64"};
const char* kArpPatt[] = {"UP", "DOWN", "UPDN", "CONV", "RAND"};
const char* kLfoTarget[] = {"PITCH", "FILTER", "VOLUME", "PAN"};
const char* kLfoShape[] = {"SINE", "TRI", "SQUARE", "S&H"};
// Straight and triplet values alternate: T lasts 2/3 of its straight counterpart.
const char* kCueRates[] = {"1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T",
                           "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
const char* cueRateName(int index) { return kCueRates[std::max(0, std::min(11, index))]; }
std::vector<uint8_t> n14(int v) { return {(uint8_t)(v >> 7 & 127), (uint8_t)(v & 127)}; }
std::vector<uint8_t> col(uint16_t c) { return {(uint8_t)(c >> 14 & 3), (uint8_t)(c >> 7 & 127), (uint8_t)(c & 127)}; }
void cat(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }
std::string pad(std::string s, size_t n) { if (s.size() > n) s.resize(n); while (s.size() < n) s += ' '; return s; }
float cl(float v, float lo = 0.f, float hi = 1.f) { return std::min(hi, std::max(lo, v)); }
std::string fmt(const char* f, double v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }
std::string pct(float v) { return fmt("%.0f%%", v * 100); }
// Tonality. The firmware is kept on a plain chromatic keyboard (scale OFF, notes 53+key) so keys outside the scale stay playable; the app only
// lights the keys that belong to the scale, shifts the notes by the octave and builds chords itself.
const int kScaleN[7] = {12, 7, 7, 7, 7, 7, 8};
const int kScaleStep[7][12] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10},
                               {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 10, 11}};
int scaleIndex(int pc, int scale) { for (int i = 0; i < kScaleN[scale]; i++) if (kScaleStep[scale][i] == pc) return i; return -1; }   // degree of a pitch class, -1 outside
int degreeNote(int d, int scale, int root) { const int n = kScaleN[scale], o = d >= 0 ? d / n : -((-d + n - 1) / n); return root + 12 * o + kScaleStep[scale][d - o * n]; }   // degree 0 = the root of octave 0
float speedOf(float n) { return 0.25f * std::pow(16.f, n); }          // 0.25..4
float micThrOf(float v) { return 0.005f * std::pow(100.f, v); }       // -46..-6 dBFS
int grainCount(float n) { return 1 + (int)std::lround(n * 11); }               // 1..12 grains at once
float paulFactor(float n) { return 2.f * std::pow(32.f, n); }                     // slow-down 2x .. 64x
float scThrDb(float n) { return -40.f * (1.f - n); }                              // 0 .. -40 dB
float scRatioOf(float n) { return 1.f + 19.f * n * n; }                          // 1:1 .. 20:1
float loopSpeedOf(float n) { return 0.25f * std::pow(8.f, n); }       // 0.25..2
float lfoHz(float n) { return 0.1f * std::pow(200.f, n); }            // 0.1..20
}  // namespace

const char* SamplerApp::pageName(Page p) { return kPage[p]; }
bool SamplerApp::inScale(int scale, int pc) { return scale < 7 ? scaleIndex(((pc % 12) + 12) % 12, scale) >= 0 : ((pc % 12) + 12) % 12 == 0; }
const char* SamplerApp::algoName(int i) { return kAlgo[std::min(5, std::max(0, i))]; }
const char* SamplerApp::scaleName(int i) { return kScaleName[std::min(7, std::max(0, i))]; }
const char* SamplerApp::noteName(int r) { return kNote[((r % 12) + 12) % 12]; }

SamplerApp::SamplerApp(Session& s, Send send) : s_(s), send_(std::move(send)) {
    cells_.resize(400); rects_.assign(400, ~0ull); setPalette(theme::light);
    for (int& h : heldNote_) h = -1;
    for (int i = 0; i < kTracks; i++) { ts_[i].vol = 0.8f; }
    refreshLibrary();
    applyFx();
    s_.setRetLevel(1, 0.7f);
    s_.setLoopSource(loopSrc_);
    // a first session to play with: keys on 1, a drum kit on 2, bass on 3
    auto first = [&](const char* group, int track) {
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].group == group) { ts_[track].entry = (int)i; if (group == std::string("Drums")) ts_[track].algo = 3; trackSourcePaths_[track] = lib_[i].path; trackSourceNames_[track] = lib_[i].label; s_.loadSamplePath(lib_[i].path, lib_[i].kit, track); return; }
    };
    // the default project: Tape Key, Acoustic bass, Shiny, Trap Kit
    auto byLabel = [&](const char* label, int track) {
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].label == label) { ts_[track].entry = (int)i; if (lib_[i].kit) ts_[track].algo = 3; trackSourcePaths_[track] = lib_[i].path; trackSourceNames_[track] = lib_[i].label; s_.loadSamplePath(lib_[i].path, lib_[i].kit, track); return; }
    };
    byLabel("Tape Key", 0); byLabel("Acoustic", 1); byLabel("Shiny", 2); byLabel("TrapKit", 3); (void)first;
    for (int t = 0; t < kTracks; t++) applyMix(t);
    applyMetronome();
    if (ts_[0].entry >= 0) libCursor_ = ts_[0].entry;
    static const char* defaults[kSeqTracks] = {
        "Kick", "Snare", "Rim", "Tom 1", "Tom 2", "Hihat Open", "Hihat Closed", "Crash"
    };
    for (int i = 0; i < kSeqTracks; ++i) {
        for (auto& step : seqGrid_[(size_t)i]) step.note = 60;
        seqSourceNames_[(size_t)i] = defaults[i];
        const auto source = (std::filesystem::path(s_.sampleDir()) / "OFFICIAL" /
                             "SEQUENCER" / (std::string(defaults[i]) + ".wav")).string();
        if (std::filesystem::is_regular_file(source)) {
            seqSourcePaths_[(size_t)i] = source;
            s_.loadSequencerSamplePath(source, i);
        }
    }
    for (auto& scene : seqScenes_) {
        for (auto& lane : scene.grid)
            for (auto& hit : lane) hit.note = 60;
    }
    s_.selectSequencerScene(0);
    s_.setSequencerBeats(seqBeats_);
    s_.setSequencerEnabled(true);
}

void SamplerApp::selectSequencerScene(int slot) {
    if (padEditing() || slot < 0 || slot >= kSeqScenes) return;
    switchSequencerBank(slot);
}
void SamplerApp::switchSequencerBank(int slot) {
    if (slot < 0 || slot >= Session::kSeqBanks || slot == seqScene_) return;
    // Every scene remembers its entire 8-track grid and its own time signature.
    seqScenes_[(size_t)seqScene_].grid = seqGrid_;
    seqScenes_[(size_t)seqScene_].beats = seqBeats_;
    seqScene_ = slot;
    seqGrid_ = seqScenes_[(size_t)slot].grid;
    seqBeats_ = seqScenes_[(size_t)slot].beats;
    seqPage_ = 0;
    seqEditStep_ = 0;
    s_.selectSequencerScene(slot);
    lastLed_ = ~0ull;
}
void SamplerApp::enterSequencer() {
    if (padEditing()) { if (!padSequencerEditing()) enterPadEditor(true); return; }
    sequencerMode_ = true;
    page_ = P_STEP;
    needClear_ = true;
}
void SamplerApp::exitSequencer() {
    if (padEditing()) { leavePadEditor(); return; }
    sequencerMode_ = false;
    page_ = P_HOME;
    needClear_ = true;
}
void SamplerApp::selectSequencerTrack(int t) {
    seqTrack_ = std::max(0, std::min(kSeqTracks - 1, t));
    lastLed_ = ~0ull;
}
void SamplerApp::setSequencerVolume(int track, float gain) {
    if (track < 0 || track >= kSeqTracks || !std::isfinite(gain)) return;
    gain = std::round(cl(gain, 0.f, 1.5f) * 100.f) / 100.f;
    if (seqVolume_[(size_t)track] == gain) return;
    seqVolume_[(size_t)track] = gain;
    s_.setSequencerVolume(track, gain);
}
void SamplerApp::setSequencerPage(int page) {
    seqPage_ = std::max(0, std::min((seqBeats_ * 4 - 1) / 16, page));
    lastLed_ = ~0ull;
}
void SamplerApp::setSequencerBeats(int beats) {
    seqBeats_ = std::max(1, std::min(64, beats));
    seqScenes_[(size_t)seqScene_].beats = seqBeats_;
    setSequencerPage(seqPage_);
    s_.setSequencerBeats(seqBeats_);
}
void SamplerApp::sequencerToggleStep(int step) {
    if (step < 0 || step >= seqBeats_ * 4) return;
    seqEditStep_ = step;
    auto& hit = seqGrid_[(size_t)seqTrack_][(size_t)step];
    hit.velocity = hit.velocity ? 0 : 108;
    seqScenes_[(size_t)seqScene_].grid[(size_t)seqTrack_][(size_t)step] = hit;
    s_.setSequencerStep(seqTrack_, step, hit.velocity, hit.note);
    lastLed_ = ~0ull;
}
void SamplerApp::sequencerSetStepNote(int step, int note) {
    if (step < 0 || step >= seqBeats_ * 4) return;
    seqEditStep_ = step;
    auto& hit = seqGrid_[(size_t)seqTrack_][(size_t)step];
    hit.note = (uint8_t)std::max(0, std::min(127, note));
    seqScenes_[(size_t)seqScene_].grid[(size_t)seqTrack_][(size_t)step] = hit;
    s_.setSequencerStep(seqTrack_, step, hit.velocity, hit.note);
}
void SamplerApp::applyCorrectTempo(float bpm) {
    if (!std::isfinite(bpm)) return;
    tempoCorrection_ = cl(bpm, 30.f, 300.f) / std::max(1.f, tempoFlexBpm_ * pitchedTempo_);
    s_.setTempoCorrection(tempoCorrection_);
}
void SamplerApp::setCorrectTempoBpm(float bpm) {
    haveTempoTap_ = false; tempoTapIntervals_.clear();
    applyCorrectTempo(bpm);
}
void SamplerApp::tapTempo(uint32_t now) {
    if (!haveTempoTap_ || now - lastTempoTap_ > 2000) {
        haveTempoTap_ = true; lastTempoTap_ = now; tempoTapIntervals_.clear();
        return;
    }
    const uint32_t dt = now - lastTempoTap_;
    if (dt < 200) return; // ignore accidental double taps
    lastTempoTap_ = now;
    tempoTapIntervals_.push_back(dt);
    if (tempoTapIntervals_.size() > 4) tempoTapIntervals_.pop_front();
    double sum = 0.;
    for (auto interval : tempoTapIntervals_) sum += interval;
    applyCorrectTempo((float)(60000.0 * tempoTapIntervals_.size() / sum));
}
void SamplerApp::setTempoFlexBpm(float bpm) {
    tempoFlexBpm_ = std::max(30.f, std::min(300.f, bpm));
    s_.setMasterBpm(tempoFlexBpm_);
}
void SamplerApp::setPitchSemitones(int v) {
    pitchSemis_ = std::max(-24, std::min(24, v));
    s_.setGlobalPitch((float)pitchSemis_, (float)pitchCents_);
}
void SamplerApp::setPitchCents(int v) {
    pitchCents_ = std::max(-100, std::min(100, v));
    s_.setGlobalPitch((float)pitchSemis_, (float)pitchCents_);
}
void SamplerApp::setPitchedTempo(float ratio) {
    pitchedTempo_ = std::max(.25f, std::min(2.f, ratio));
    s_.setPitchedTempo(pitchedTempo_);
}

// ---- library ----
void SamplerApp::refreshLibrary() {
    namespace fs = std::filesystem;
    const std::string selected = libCursor_ < (int)lib_.size() ? lib_[(size_t)libCursor_].path : "";
    lib_.clear();
    const fs::path root(s_.sampleDir());
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            if (ext != ".wav") continue;
            const fs::path rel = it->path().lexically_relative(root);
            auto p = rel.begin();
            if (p == rel.end()) continue;
            const std::string pack = (p++)->string();
            const fs::path relFolder = rel.parent_path().lexically_relative(pack);
            const std::string folder = relFolder == fs::path(".") ? "" : relFolder.generic_string();
            const std::string stem = it->path().stem().string();
            const std::string category = pack == "OFFICIAL" ? folder : folder.empty() ? pack : folder;
            const bool kit = pack == "OFFICIAL" && folder == "Drums" &&
                (stem == "707" || stem == "OPDrums" || stem == "TrapKit");
            lib_.push_back({stem, category, it->path().string(), kit, pack, folder});
        }
    }
    // Old microphone recordings and imported WAVs remain available without migration loss.
    for (const auto& path : s_.sampleLibrary()) {
        const fs::path p(path);
        std::string name = p.stem().string();
        if (name.rfind("sample_", 0) == 0) name.erase(0, 7);
        lib_.push_back({name, "Recordings", path, false, "SAVED", "RECORDINGS"});
    }
    std::sort(lib_.begin(), lib_.end(), [](const Entry& a, const Entry& b) {
        if (a.pack != b.pack) return a.pack < b.pack;
        if (a.folder != b.folder) return a.folder < b.folder;
        return a.label < b.label;
    });
    // Preserve all old scene/project source paths, including embedded project audio
    // that lives outside samples/. Never rebind a track based on a stale list index.
    for (int t = 0; t < kTracks; ++t) {
        const auto& path = trackSourcePaths_[(size_t)t];
        if (path.empty()) { ts_[(size_t)t].entry = -1; continue; }
        auto it = std::find_if(lib_.begin(), lib_.end(),
                              [&](const Entry& e) { return e.path == path; });
        if (it == lib_.end() && fs::exists(path)) {
            std::string name = trackSourceNames_[(size_t)t];
            if (name.empty()) name = fs::path(path).stem().string();
            lib_.push_back({name, "Project", path, ts_[(size_t)t].algo == 3, "PROJECT", "Embedded"});
            it = lib_.end() - 1;
        }
        ts_[(size_t)t].entry = it == lib_.end() ? -1 : (int)(it - lib_.begin());
    }
    libCursor_ = 0;
    for (size_t i = 0; i < lib_.size(); ++i)
        if (lib_[i].path == selected) { libCursor_ = (int)i; break; }
    rebuildBrowser();  // keep the current directory and selected row after imports
}

// The physical PRESETS knob and the phone's BACK/ENTER share ONE cursor.
// Directory entries are listed alongside the original filenames: folders
// first, then files; compatible WAVs map to live sampler library entries.
void SamplerApp::rebuildBrowser(const std::string& preferred) {
    namespace fs = std::filesystem;
    std::string previous = preferred;
    if (previous.empty() && browserSelected()) previous = browserSelected()->path;
    browserItems_.clear();
    browserIndex_ = -1;
    const fs::path root(s_.sampleDir());
    std::error_code ec;
    if (browserDir_.empty()) {
        // The four permanent library folders remain navigable when empty.
        for (const char* name : {"PROYECTOS", "OFFICIAL", "SAVED", "PACKS"})
            browserItems_.push_back({name, name, -1, true});
    } else {
        const fs::path current = root / browserDir_;
        if (browserDir_ == "PROYECTOS") {
            for (const Entry& e : lib_) if (e.pack == "PROJECT") {
                browserItems_.push_back({"Embedded", "PROYECTOS/Embedded", -1, true});
                break;
            }
        }
        if (browserDir_ == "PROYECTOS/Embedded") {
            for (int i = 0; i < (int)lib_.size(); ++i) {
                const Entry& e = lib_[(size_t)i];
                if (e.pack == "PROJECT") browserItems_.push_back({e.label + ".wav", e.path, i, false});
            }
        } else if (fs::is_directory(current, ec)) {
            for (const fs::directory_entry& file :
                 fs::directory_iterator(current, fs::directory_options::skip_permission_denied, ec)) {
                if (file.is_directory(ec)) {
                    const fs::path rel = file.path().lexically_relative(root);
                    browserItems_.push_back({file.path().filename().string(), rel.generic_string(), -1, true});
                } else if (file.is_regular_file(ec)) {
                    const std::string path = file.path().string();
                    const auto match = std::find_if(lib_.begin(), lib_.end(),
                        [&](const Entry& e) { return e.path == path; });
                    const int entry = match == lib_.end() ? -1 : (int)(match - lib_.begin());
                    browserItems_.push_back({file.path().filename().string(), path, entry, false});
                }
                if (ec) ec.clear();
            }
        }
    }
    std::sort(browserItems_.begin(), browserItems_.end(), [](const BrowserItem& x, const BrowserItem& y) {
        if (x.directory != y.directory) return x.directory;
        return x.label < y.label;
    });
    if (browserItems_.empty()) return;
    browserIndex_ = 0;
    for (int i = 0; i < (int)browserItems_.size(); ++i) {
        if (browserItems_[(size_t)i].path == previous) { browserIndex_ = i; break; }
    }
}
void SamplerApp::loadBrowserSelection() {
    const BrowserItem* chosen = browserSelected();
    if (chosen && !chosen->directory && chosen->entry >= 0)
        loadEntry(chosen->entry); // immediate load, not a delayed audition
}
void SamplerApp::browse(int delta) {
    if (!delta || browserItems_.empty()) return;
    const int n = (int)browserItems_.size();
    // One MIDI encoder event means exactly one item, even if the firmware
    // sends a large relative value for a fast detent.
    const int step = delta > 0 ? 1 : -1;
    const int start = browserIndex_ < 0 ? (step > 0 ? -1 : 0) : browserIndex_;
    browserIndex_ = ((start + step) % n + n) % n;
    browserTouchedAt_ = nowMs_;
    loadBrowserSelection();
}
void SamplerApp::browserSelect(int index) {
    if (index < 0 || index >= (int)browserItems_.size()) return;
    browserIndex_ = index;
    browserTouchedAt_ = nowMs_;
    loadBrowserSelection(); // touching a WAV also loads it, just like PRESETS
}
void SamplerApp::browserEnter() {
    const BrowserItem* chosen = browserSelected();
    if (!chosen && browserDir_.empty() && !browserItems_.empty()) {
        // BACK at the archive root deliberately shows FM1-SAMPLER itself,
        // without a child selected. ENTER must be able to reopen that root:
        // use its last highlighted folder (or the first one on first entry).
        browserIndex_ = std::max(0, std::min(rootLastIndex_, (int)browserItems_.size() - 1));
        chosen = browserSelected();
    }
    if (!chosen || !chosen->directory) return; // ENTER never loads an audio file
    if (browserDir_.empty()) rootLastIndex_ = browserIndex_;
    browserDir_ = chosen->path;
    rebuildBrowser();
    browserTouchedAt_ = nowMs_;
    loadBrowserSelection(); // selecting the first WAV in a directory also loads it
}
void SamplerApp::browserBack() {
    if (browserDir_.empty()) {
        // Keep the root choice for the next ENTER, even though the top-level
        // archive itself is now highlighted as FM1-SAMPLER.
        if (browserSelected()) rootLastIndex_ = browserIndex_;
        browserIndex_ = -1;
        browserTouchedAt_ = nowMs_;
        return;
    }
    const std::string prior = browserDir_;
    const size_t slash = browserDir_.find_last_of('/');
    browserDir_ = slash == std::string::npos ? "" : browserDir_.substr(0, slash);
    rebuildBrowser(prior); // highlight the folder just exited
    browserTouchedAt_ = nowMs_;
}
void SamplerApp::browserRoot() {
    browserDir_.clear();
    rebuildBrowser();
    browserTouchedAt_ = nowMs_;
}
// Only a selected descendant of PACKS may be removed. PROJECTS, SAVED,
// OFFICIAL, the PACKS root itself, and all other app files are excluded.
// Java checks canonical paths again before showing the destructive dialog.
std::string SamplerApp::selectedPackDeletePath() const {
    namespace fs = std::filesystem;
    if (browserDir_ != "PACKS" && browserDir_.rfind("PACKS/", 0) != 0) return {};
    const BrowserItem* selected = browserSelected();
    if (!selected) return {};
    const fs::path packs = fs::path(s_.sampleDir()) / "PACKS";
    const fs::path file = selected->directory
        ? fs::path(s_.sampleDir()) / selected->path : fs::path(selected->path);
    const fs::path relative = file.lexically_relative(packs);
    if (relative.empty() || relative == "." || relative.is_absolute()) return {};
    for (const auto& component : relative)
        if (component.empty() || component == "." || component == "..") return {};
    return relative.generic_string();
}
void SamplerApp::requestDeleteBrowserSelection() {
    pendingPackDeletePath_ = selectedPackDeletePath();
    if (pendingPackDeletePath_.empty()) {
        say("SELECT A FILE OR FOLDER INSIDE PACKS");
        return;
    }
    dialogRequest_ = 6; // Android shows the DELETE / CANCEL confirmation.
}
std::string SamplerApp::takePackDeletePath() {
    std::string path = std::move(pendingPackDeletePath_);
    pendingPackDeletePath_.clear();
    return path;
}
void SamplerApp::loadEntry(int i) {
    // Sequencer samples can be replaced live, including while editing a pad.
    // Keep the free-track restriction only for the sixteen audio looper tracks.
    if (!sequencerMode_ && !padTrackWritable(track_)) { say("TRACK IN USE BY PROJECT"); return; }
    if (i < 0 || i >= (int)lib_.size()) return;
    libCursor_ = i; auditionAt_ = 0;
    const Entry& e = lib_[(size_t)i];
    if (sequencerMode_) {
        seqSourcePaths_[(size_t)seqTrack_] = e.path;
        seqSourceNames_[(size_t)seqTrack_] = e.label;
        s_.loadSequencerSamplePath(e.path, seqTrack_);
        say("SEQ " + std::to_string(seqTrack_ + 1) + ": " + e.label);
        return;
    }
    trackSourcePaths_[(size_t)track_] = e.path;
    trackSourceNames_[(size_t)track_] = e.label;
    TrackState& t = ts_[track_];
    t.entry = i; t.tuned = false; t.start = 0.f; t.end = 1.f;
    if (e.kit) t.algo = 3; else if (t.algo == 3) t.algo = 0;
    previewAfterLoad_ = true;
    s_.loadSamplePath(e.path, e.kit, track_);
}
void SamplerApp::importFile(const std::string& wav) {
    refreshLibrary();
    for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].path == wav) { loadEntry((int)i); say("LOADED FROM DISK"); return; }
    say("FILE NOT READABLE");
}

// ---- parameters ----
void SamplerApp::applyAll(int t) {
    applyAlgo(t); applyRoot(t); applyEnv(t); applyLfo(t); applyHome(t); applyArp(t); applySeq(t); applyMix(t);
}
void SamplerApp::applyRoot(int t) { s_.setRoot(t, ts_[t].tuned ? ts_[t].root : 60.f); }
void SamplerApp::autoTune() {
    if (!padTrackWritable(track_)) return;
    const int t = track_;
    if (ts_[t].algo > 1) { say("AUTOTUNE: MELODIC OR ONE-SHOT"); return; }
    if (ts_[t].tuned) { ts_[t].tuned = false; applyRoot(t); say("TUNING RESET"); return; }
    const float m = s_.detectRoot(t);
    if (m < 0) { say("NO CLEAR PITCH IN THIS SAMPLE"); return; }
    const int n = (int)std::lround(m), cents = (int)std::lround((m - n) * 100.f);
    ts_[t].root = m; ts_[t].tuned = true; applyRoot(t);
    say(std::string("TUNED: ") + noteName(n) + std::to_string(n / 12 - 1) + (cents >= 0 ? " +" : " ") + std::to_string(cents) + " CT");
}
void SamplerApp::applyAlgo(int t) { s_.setMode(t, ts_[t].algo); }
void SamplerApp::applyEnv(int t) {
    const TrackState& x = ts_[t];
    s_.setEnv(t, 3.f * x.a * x.a, x.d, x.s, 5.f * x.r * x.r);   // seconds (as shown on the screens), sustain level
}
void SamplerApp::applyLfo(int t) {
    const TrackState& x = ts_[t];
    Session::Lfo l; l.rate = lfoHz(x.lfoRate); l.depth = x.lfoDepth; l.target = x.lfoTarget; l.shape = x.lfoShape; l.base = 0.5f;
    s_.setLfo(t, l);   // the engine runs the low-pass only while the LFO sweeps the filter
}
void SamplerApp::applyHome(int t, bool commit) {
    const TrackState& x = ts_[t];
    s_.setSampleRange(t, x.start, x.end);
    const float semis = std::round((x.pitch - 0.5f) * 48.f);
    if (x.algo == 3) { s_.setStretch(t, 1.f, 0.f, commit); s_.setSliceThreshold(t, x.kitThr); s_.setKitPitch(t, semis); }   // KIT: threshold instead of speed, the pitch moves every slice
    else if (x.algo == 4) { s_.setStretch(t, 1.f, 0.f, commit); s_.setGrain(t, grainCount(x.grains), x.scatter); }
    else if (x.algo == 5) { s_.setStretch(t, 1.f, 0.f, commit); s_.setPaul(t, paulFactor(x.paulStretch), x.paulWidth * 2.f); }
    else s_.setStretch(t, speedOf(x.speed), semis, commit);
}
void SamplerApp::applyArp(int t) {
    const TrackState& x = ts_[t];
    s_.setArp(t, x.arpRate > 0, std::max(0, x.arpRate - 1), x.arpPatt, x.arpOct + 1, x.arpLen);
}
void SamplerApp::applySeq(int) {   // loop speed and tape colour are global: every track gets them
    s_.setLoopSpeed(loopSpeedOf(loopSpeed_));
    for (int i = 0; i < kTracks; i++)
        s_.setTape(i, exciter_, 1.f - 0.4f * lofi_, wobble_, 0.25f * lofi_, cl(std::max(exciter_ * 3.f, std::max(wobble_, lofi_))), 1.f - 0.6f * lofi_);
}
void SamplerApp::applyMix(int t) {
    const TrackState& x = ts_[t];
    s_.setVolume(t, x.vol * 1.2f); s_.setPan(t, x.pan * 2.f - 1.f); s_.setSend(t, 0, x.sendR); s_.setSend(t, 1, x.sendD);
    s_.setSidechain(t, x.scSrc - 1, scThrDb(x.scThr), scRatioOf(x.scRatio));
}
void SamplerApp::applyFx() {
    s_.setReverb(reverbSize_);
    s_.setRetLevel(0, 0.8f); s_.setRetLevel(1, 0.7f);   // global send effects: how much of a track goes in is set per track (the sends)
    s_.setDelay(0.05f + delayTime_ * 1.45f, delayFb_ * 0.95f);
}
void SamplerApp::queueCommit(int t) { commitAt_ = nowMs_ + 350; commitTrack_ = t; }   // the offline stretch / pitch runs once the knob rests
void SamplerApp::setAlgo(int a) { if (!padTrackWritable(track_)) return; ts_[track_].algo = ((a % 6) + 6) % 6; applyAlgo(track_); applyHome(track_, true); }
void SamplerApp::setVolume(int t, float v) { if (!padTrackWritable(t)) return; ts_[t].vol = cl(v); applyMix(t); }
void SamplerApp::setPan01(int t, float v) { if (!padTrackWritable(t)) return; ts_[t].pan = cl(v); applyMix(t); }
void SamplerApp::setSend(int t, int w, float v) { if (!padTrackWritable(t)) return; (w ? ts_[t].sendD : ts_[t].sendR) = cl(v); applyMix(t); }
void SamplerApp::setScale(int scale, int root, int octave, int mode) {
    scale_ = {std::min(7, std::max(0, scale)), ((root % 12) + 12) % 12, std::min(3, std::max(-3, octave)), mode & 1};
    sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)scale_.mode}, true);
}
void SamplerApp::selectMetronome() {
    if (padEditing()) return;
    if (metroSelected_) return;
    if (st_.track_clip_rec[track_]) s_.clipRecord(track_, false);
    metroSelected_ = true;
    page_ = P_METRO_TEMPO;
}
void SamplerApp::selectTrack(int t) {
    if (padEditing() && !padTrackWritable(t)) { say("TRACK IN USE BY PROJECT"); return; }
    if (t < 0 || t >= kTracks) { selectMetronome(); return; }
    if (t == track_ && !metroSelected_) return;
    metroSelected_ = false;
    if (page_ == P_METRO_TEMPO || page_ == P_METRO_SETTINGS || page_ == P_METRO_CORRECT) page_ = P_HOME;
    if (st_.track_clip_rec[track_]) s_.clipRecord(track_, false);   // an overdub belongs to the track it was started on
    track_ = t;
    if (ts_[t].entry >= 0) libCursor_ = ts_[t].entry;   // PRESETS starts from the sample this track plays
}
void SamplerApp::gotoPage(Page p) {
    if (metroSelected_ && p != P_METRO_TEMPO && p != P_METRO_SETTINGS && p != P_METRO_CORRECT) { page_ = P_METRO_TEMPO; return; }
    if (p == P_STEP) { enterSequencer(); return; }
    sequencerMode_ = false;
    page_ = p;
}

SamplerApp::Knob SamplerApp::cont(const char* name, float* v, std::function<std::string(float)> f, std::function<void()> apply, float stepSize, bool commit) {
    Knob k; k.name = name; k.valid = true; k.norm = *v; k.value = f(*v);
    const int t = track_;
    k.step = [=](int d) { *v = cl(*v + d * stepSize); apply(); if (commit) queueCommit(t); };
    k.set = [=](float n) { *v = cl(n); apply(); if (commit) queueCommit(t); };
    return k;
}
SamplerApp::Knob SamplerApp::choice(const char* name, int* v, std::vector<std::string> opts, std::function<void()> apply) {
    Knob k; k.name = name; k.valid = true; k.isChoice = true;
    const int n = (int)opts.size();
    *v = std::min(n - 1, std::max(0, *v));
    k.value = opts[(size_t)*v]; k.norm = n > 1 ? *v / (float)(n - 1) : 0.f;
    k.step = [=](int d) { *v = std::min(n - 1, std::max(0, *v + (d > 0 ? 1 : -1))); apply(); };
    k.set = [=](float x) { *v = std::min(n - 1, std::max(0, (int)std::lround(x * (n - 1)))); apply(); };
    return k;
}

SamplerApp::Knobs SamplerApp::knobs() { return knobs(page_); }
SamplerApp::Knobs SamplerApp::knobs(Page p) {
    Knobs k;
    if (padCompose_ && (!padTrackWritable(track_) || p == P_LEN || p == P_SEQ || p == P_TEMPO || p == P_METRO_TEMPO || p == P_METRO_SETTINGS || p == P_METRO_CORRECT)) return k;
    TrackState& x = ts_[track_];
    const int t = track_;
    auto sec = [](float s) { return fmt(s < 1 ? "%.2fS" : "%.1fS", s); };
    switch (p) {
    case P_HOME:
        k[0] = cont("START", &x.start, [](float v) { return pct(v); }, [=] { if (ts_[t].start > ts_[t].end - 0.02f) ts_[t].start = ts_[t].end - 0.02f; applyHome(t); }, 0.01f, true);
        k[1] = cont("END", &x.end, [](float v) { return pct(v); }, [=] { if (ts_[t].end < ts_[t].start + 0.02f) ts_[t].end = ts_[t].start + 0.02f; applyHome(t); }, 0.01f, true);
        if (x.algo == 3) {   // KIT: the threshold the sample is cut at, the pitch of every slice
            k[2] = cont("THRESHOLD", &x.kitThr, [](float v) { return pct(v); }, [=] { applyHome(t); }, 0.01f);
            k[3] = cont("PITCH", &x.pitch, [](float v) { return fmt("%+.0f ST", std::round((v - 0.5f) * 48.f)); }, [=] { applyHome(t); }, 1.f / 48.f);
        } else if (x.algo == 4) {   // GRAIN: how many grains sound together and how far around the cursor they scatter
            k[2] = cont("GRAINS", &x.grains, [](float v) { return std::to_string(grainCount(v)); }, [=] { applyHome(t); }, 1.f / 11.f);
            k[3] = cont("SCATTER", &x.scatter, [](float v) { return pct(v); }, [=] { applyHome(t); }, 0.02f);
        } else if (x.algo == 5) {   // PAULSTRETCH: how much slower, how wide
            k[2] = cont("STRETCH", &x.paulStretch, [](float v) { return fmt("%.0fX", paulFactor(v)); }, [=] { applyHome(t); }, 0.02f);
            k[3] = cont("WIDTH", &x.paulWidth, [](float v) { return pct(v * 2.f); }, [=] { applyHome(t); }, 0.02f);
        } else {
            k[2] = cont("SPEED", &x.speed, [](float v) { return fmt("%.2fX", speedOf(v)); }, [=] { applyHome(t); }, 0.01f, true);
            k[3] = cont("PITCH", &x.pitch, [](float v) { return fmt("%+.0f ST", std::round((v - 0.5f) * 48.f)); }, [=] { applyHome(t); }, 1.f / 48.f, true);
        }
        break;
    case P_FX:   // two global send effects; the sends are per track (the selected one), size and time are global
        k[0] = cont("REVERB SEND", &x.sendR, [](float v) { return pct(v); }, [=] { applyMix(t); });
        k[1] = cont("REVERB SIZE", &reverbSize_, [](float v) { return pct(v); }, [=] { applyFx(); });
        k[2] = cont("DELAY SEND", &x.sendD, [](float v) { return pct(v); }, [=] { applyMix(t); });
        k[3] = cont("DELAY TIME", &delayTime_, [](float v) { return fmt("%.2fS", 0.05 + v * 1.45); }, [=] { applyFx(); });
        break;
    case P_ENV:
        k[0] = cont("ATTACK", &x.a, [=](float v) { return sec(3 * v * v); }, [=] { applyEnv(t); });
        k[1] = cont("DECAY", &x.d, [=](float v) { return sec(v); }, [=] { applyEnv(t); });
        k[2] = cont("SUSTAIN", &x.s, [](float v) { return pct(v); }, [=] { applyEnv(t); });
        k[3] = cont("RELEASE", &x.r, [=](float v) { return sec(5 * v * v); }, [=] { applyEnv(t); });
        break;
    case P_LFO:
        k[0] = cont("RATE", &x.lfoRate, [](float v) { return fmt("%.1fHZ", lfoHz(v)); }, [=] { applyLfo(t); });
        k[1] = cont("DEPTH", &x.lfoDepth, [](float v) { return pct(v); }, [=] { applyLfo(t); });
        k[2] = choice("TARGET", &x.lfoTarget, {kLfoTarget[0], kLfoTarget[1], kLfoTarget[2], kLfoTarget[3]}, [=] { applyLfo(t); });
        k[3] = choice("SHAPE", &x.lfoShape, {kLfoShape[0], kLfoShape[1], kLfoShape[2], kLfoShape[3]}, [=] { applyLfo(t); });
        break;
    case P_ARP: {
        std::vector<std::string> rates(kArpRate, kArpRate + 13), patt(kArpPatt, kArpPatt + 5);
        k[0] = choice("RATE", &x.arpRate, rates, [=] { applyArp(t); });
        k[1] = choice("PATTERN", &x.arpPatt, patt, [=] { applyArp(t); });
        k[2] = choice("OCTAVES", &x.arpOct, {"1", "2", "3", "4"}, [=] { applyArp(t); });
        k[3] = cont("LENGTH", &x.arpLen, [](float v) { return pct(v); }, [=] { applyArp(t); });
        break;
    }
    case P_SEQ:
        k[0] = cont("LOOP SPEED", &loopSpeed_, [](float v) { return fmt("%.2fX", loopSpeedOf(v)); }, [=] { applySeq(t); }, 0.02f);
        k[1] = cont("EXCITER", &exciter_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        k[2] = cont("LO-FI", &lofi_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        k[3] = cont("WOBBLE", &wobble_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        break;
    case P_MIX: {
        k[0] = cont("VOLUME", &x.vol, [](float v) { return pct(v); }, [=] { applyMix(t); });
        std::vector<std::string> src = {"OFF"}; for (int i = 1; i <= kTracks; i++) src.push_back("TRACK " + std::to_string(i));
        k[1] = choice("SC SOURCE", &x.scSrc, src, [=] { applyMix(t); });
        k[2] = cont("SC THRESHOLD", &x.scThr, [](float v) { return fmt("%.0f DB", scThrDb(v)); }, [=] { applyMix(t); });
        k[3] = cont("SC RATIO", &x.scRatio, [](float v) { return fmt("%.1f:1", scRatioOf(v)); }, [=] { applyMix(t); });
        break;
    }
    case P_KEY: {
        auto mk = [this](const char* name, std::vector<std::string> opts, int cur, std::function<void(int)> setter) {
            Knob b; b.name = name; b.valid = true; b.isChoice = true;
            const int n = (int)opts.size(); cur = std::min(n - 1, std::max(0, cur));
            b.value = opts[(size_t)cur]; b.norm = cur / (float)(n - 1);
            b.step = [=](int d) { setter(std::min(n - 1, std::max(0, cur + (d > 0 ? 1 : -1)))); };
            b.set = [=](float v) { setter(std::min(n - 1, std::max(0, (int)std::lround(v * (n - 1))))); };
            return b;
        };
        const ScaleState sc = scale_;
        k[0] = mk("SCALE", std::vector<std::string>(kScaleName, kScaleName + 8), sc.scale, [=](int v) { setScale(v, sc.root, sc.octave, sc.mode); });
        k[1] = mk("ROOT", std::vector<std::string>(kNote, kNote + 12), sc.root, [=](int v) { setScale(sc.scale, v, sc.octave, sc.mode); });
        k[2] = mk("OCTAVE", {"-3", "-2", "-1", "0", "+1", "+2", "+3"}, sc.octave + 3, [=](int v) { setScale(sc.scale, sc.root, v - 3, sc.mode); });
        k[3] = mk("MODE", {"KEY", "CHORD"}, sc.mode, [=](int v) { setScale(sc.scale, sc.root, sc.octave, v); });
        break;
    }
    case P_SAVE: {
        static const char* micTxt[] = {"READY", "WAITING", "RECORDING", "SAVING"};
        Knob m; m.name = "MIC SAMPLE"; m.valid = true; m.isChoice = true; m.value = micTxt[mic_]; m.norm = mic_ == 0 ? 0.f : 1.f;
        m.step = [this](int d) { if ((d > 0) == (mic_ == 0)) micSample(); };
        m.set = [this](float v) { if ((v > 0.5f) == (mic_ == 0)) micSample(); };
        k[0] = m;
        k[1] = choice("LOOP SOURCE", &loopSrc_, {"MIC", "SOUND"}, [this] { s_.setLoopSource(loopSrc_); });
        k[3] = cont("MIC THRESHOLD", &micThr_, [](float v) { return fmt("%.0f DB", 20 * std::log10(micThrOf(v))); }, [this] { s_.setMicThreshold(micThrOf(micThr_)); }, 0.02f);
        break;
    }
    case P_LEN: {
        const double sec = st_.loopSamples > 0 ? st_.loopSamples / (double)SAMPLERATE : 0.0;
        const float cur = st_.loopSamples > 0 ? cl((float)(std::log2(std::max(1.0, (double)st_.loopBars)) / 5.0)) : 0.f;
        Knob len; len.name = "LENGTH"; len.valid = true; len.isChoice = true;
        len.value = st_.loopSamples > 0 ? fmt("%.1fS", sec) : "NO LOOP"; len.norm = cur;
        len.step = [this](int d) { lenAction(d > 0); };
        len.set = [this, cur](float v) { if (v > cur + 0.05f) lenAction(true); else if (v < cur - 0.05f) lenAction(false); };
        auto act = [this](const char* name, const char* what, bool dbl) {
            Knob a; a.name = name; a.valid = true; a.isChoice = true; a.value = st_.loopSamples > 0 ? what : "NO LOOP";
            a.step = [this, dbl](int) { lenAction(dbl); }; a.set = [this, dbl](float) { lenAction(dbl); };
            return a;
        };
        k[0] = len;
        k[1] = act("DUPLICATE", "TURN: X2", true);
        k[2] = act("HALVE", "TURN: /2", false);
        Knob b; b.name = "BARS / BPM"; b.valid = true; b.isChoice = true; b.value = fmt("%.0f", st_.loopBars) + " / " + fmt("%.0f", st_.bpm * tempoCorrection_); b.step = [](int) {}; b.set = [](float) {};
        k[3] = b;
        break;
    }
    case P_METRO_TEMPO: return knobs(P_TEMPO);
    case P_METRO_CORRECT: {
        Knob& a = k[0];
        a.name = "CORRECT BPM"; a.value = fmt("%.2f BPM", correctedTempoBpm());
        a.norm = cl((correctedTempoBpm() - 30.f) / 270.f); a.valid = true;
        a.step = [this](int d) { setCorrectTempoBpm((std::round(correctedTempoBpm() * 100.f) + (d > 0 ? 10.f : -10.f)) / 100.f); };
        a.set = [this](float v) { setCorrectTempoBpm(std::round((30.f + cl(v) * 270.f) * 10.f) / 10.f); };
        // K4 changes hundredths independently. Using integer hundredths avoids
        // repeated float additions and keeps the displayed and edited value aligned.
        const int cents = std::clamp((int)std::lround(correctedTempoBpm() * 100.f), 3000, 30000);
        Knob& fine = k[3];
        fine.name = "DECIMALS"; fine.value = std::string(".") + (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100);
        fine.valid = true; fine.norm = (cents % 100) / 99.f;
        fine.step = [this](int d) {
            const int c = (int)std::lround(correctedTempoBpm() * 100.f);
            setCorrectTempoBpm(std::clamp(c + (d > 0 ? 1 : -1), 3000, 30000) / 100.f);
        };
        fine.set = [this](float v) {
            const int c = std::clamp((int)std::lround(correctedTempoBpm() * 100.f), 3000, 30000);
            const int fraction = (int)std::lround(cl(v) * 99.f);
            setCorrectTempoBpm(std::min(30000, (c / 100) * 100 + fraction) / 100.f);
        };
        break;
    }
    case P_METRO_SETTINGS: {
        auto mk = [](const char* name, const std::string& value, float norm,
                     std::function<void(int)> step, std::function<void(float)> set) {
            Knob b; b.name = name; b.value = value; b.norm = cl(norm);
            b.valid = true; b.isChoice = true; b.step = std::move(step); b.set = std::move(set); return b;
        };
        k[0] = mk("TIME SIGNATURE", std::to_string(metroBeats_) + "/4", (metroBeats_ - 1) / 63.f,
                  [this](int d) { metroBeats_ = std::clamp(metroBeats_ + (d > 0 ? 1 : -1), 1, 64); applyMetronome(); },
                  [this](float v) { metroBeats_ = 1 + (int)std::lround(cl(v) * 63.f); applyMetronome(); });
        k[1] = mk("SWING", fmt("%.0f%%", metroSwing_ * 100.f), metroSwing_,
                  [this](int d) { metroSwing_ = cl(metroSwing_ + (d > 0 ? .01f : -.01f)); applyMetronome(); },
                  [this](float v) { metroSwing_ = cl(v); applyMetronome(); });
        k[2] = mk("CLICK VOLUME", fmt("%.0f%%", metroVolume_ * 100.f), metroVolume_,
                  [this](int d) { metroVolume_ = cl(metroVolume_ + (d > 0 ? .01f : -.01f)); applyMetronome(); },
                  [this](float v) { metroVolume_ = cl(v); applyMetronome(); });
        static const char* names[] = {"PLAY/REC", "REC", "OFF", "PLAY/REC MON", "REC MON", "OFF MON"};
        k[3] = mk("CLICK MODE", names[std::clamp(metroMode_, 0, 5)], metroMode_ / 5.f,
                  [this](int d) { metroMode_ = std::clamp(metroMode_ + (d > 0 ? 1 : -1), 0, 5); applyMetronome(); },
                  [this](float v) { metroMode_ = std::clamp((int)std::lround(cl(v) * 5.f), 0, 5); applyMetronome(); });
        break;
    }
    case P_TEMPO: {
        auto dial = [](const char* label, const std::string& value, float norm,
                       std::function<void(int)> turn, std::function<void(float)> absolute) {
            Knob a; a.name = label; a.value = value; a.norm = cl(norm);
            a.valid = true; a.step = std::move(turn); a.set = std::move(absolute);
            return a;
        };
        k[0] = dial("TEMPO FLEX", fmt("%.0f BPM", tempoFlexBpm_),
                    (tempoFlexBpm_ - 30.f) / 270.f,
                    [this](int d) { setTempoFlexBpm(tempoFlexBpm_ + (d > 0 ? 1.f : -1.f)); },
                    [this](float v) { setTempoFlexBpm(30.f + cl(v) * 270.f); });
        k[1] = dial("SEMITONES", fmt("%+.0f ST", (double)pitchSemis_), (pitchSemis_ + 24) / 48.f,
                    [this](int d) { setPitchSemitones(pitchSemis_ + (d > 0 ? 1 : -1)); },
                    [this](float v) { setPitchSemitones((int)std::lround(cl(v) * 48 - 24)); });
        k[2] = dial("CENTS", fmt("%+.0f CT", (double)pitchCents_), (pitchCents_ + 100) / 200.f,
                    [this](int d) { setPitchCents(pitchCents_ + (d > 0 ? 1 : -1)); },
                    [this](float v) { setPitchCents((int)std::lround(cl(v) * 200 - 100)); });
        k[3] = dial("PITCHED TEMPO", fmt("%.2fX", pitchedTempo_),
                    (pitchedTempo_ - .25f) / 1.75f,
                    [this](int d) { setPitchedTempo(pitchedTempo_ + (d > 0 ? .05f : -.05f)); },
                    [this](float v) { setPitchedTempo(.25f + cl(v) * 1.75f); });
        break;
    }
    case P_STEP: {
        const int pages = std::max(1, (seqBeats_ * 4 + 15) / 16);
        const int selected = std::max(0, std::min(kSeqSteps - 1, seqEditStep_));
        const auto make = [](const std::string& name, const std::string& value, float norm,
                             std::function<void(int)> step, std::function<void(float)> set) {
            Knob b; b.name = name; b.value = value; b.norm = norm; b.valid = true;
            b.isChoice = true; b.step = std::move(step); b.set = std::move(set);
            return b;
        };
        k[0] = make("STEP PAGE", std::to_string(seqPage_ + 1) + "/" + std::to_string(pages),
                    pages > 1 ? seqPage_ / (float)(pages - 1) : 0.f,
                    [this](int d) { setSequencerPage(seqPage_ + (d > 0 ? 1 : -1)); },
                    [this, pages](float v) { setSequencerPage((int)std::lround(cl(v) * (pages - 1))); });
        k[1] = make("STEP NOTE", std::to_string((int)seqGrid_[(size_t)seqTrack_][(size_t)selected].note),
                    seqGrid_[(size_t)seqTrack_][(size_t)selected].note / 127.f,
                    [this, selected](int d) { sequencerSetStepNote(selected,
                        (int)seqGrid_[(size_t)seqTrack_][(size_t)selected].note + (d > 0 ? 1 : -1)); },
                    [this, selected](float v) { sequencerSetStepNote(selected, (int)std::lround(cl(v) * 127)); });
        k[2] = make("LENGTH /4", std::to_string(seqBeats_) + "/4",
                    (seqBeats_ - 1) / 63.f,
                    [this](int d) { setSequencerBeats(seqBeats_ + (d > 0 ? 1 : -1)); },
                    [this](float v) { setSequencerBeats(1 + (int)std::lround(cl(v) * 63)); });
        if (!padSequencerEditing()) k[3] = make("SEQ SCENE", std::to_string(seqScene_ + 1) + "/8", seqScene_ / 7.f,
                    [this](int d) { selectSequencerScene(seqScene_ + (d > 0 ? 1 : -1)); },
                    [this](float v) { selectSequencerScene((int)std::lround(cl(v) * 7)); });
        break;
    }
    case P_CUE:
        k[0] = choice("RATE", &cueDivision_,
                      {"1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T",
                       "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"},
                      [this] { if (cueActive_) s_.setCue(true, cueDivision_); });
        // Keep the remaining encoders inert during the momentary beat-repeat.
        break;
    default: break;
    }
    if (padCompose_ && p == P_FX) { k[1] = Knob{}; k[3] = Knob{}; }
    return k;
}

// ---- live scene launcher and arrangement ----
std::string SamplerApp::sceneLabel(int slot) {
    static const char* notes[12] = {"C1", "C#1", "D1", "D#1", "E1", "F1",
                                    "F#1", "G1", "G#1", "A1", "A#1", "B1"};
    return slot >= 0 && slot < Session::kScenes ? notes[slot] : "--";
}
void SamplerApp::setSongMode(bool song) {
    if (padEditing()) leavePadEditor();
    if (song == songMode_ && sceneKeys_) return;
    if (recordingNow()) { say("FINISH RECORDING FIRST"); return; }
    if (!sceneKeys_) s_.beginPerformanceMode(); // one COW snapshot for LOOP and SONG
    if (st_.songPlaying) s_.stopSong();
    if (!song) s_.cancelSceneLaunch();
    songMode_ = song;
    sceneKeys_ = true;
    say(songMode_ ? "SONG MODE" : "LOOP MODE: C1-B1");
}
bool SamplerApp::enterPadEditor(bool sequencer) {
    if (recordingNow() || mic_ != 0) { say("FINISH RECORDING FIRST"); return false; }
    if (!sceneSaved(selectedScene_)) { say("SAVE THIS PAD FIRST"); return false; }
    if (padEditing()) leavePadEditor();
    const auto status = s_.status();
    uint16_t used = status.compositionTracks;
    for (int t = 0; t < kTracks; ++t) if (status.track_has_clip[t]) used |= (uint16_t)(1u << t);
    for (int i = 0; i < Session::kScenes; ++i) used |= status.sceneTracks[i];
    padWritable_ = (uint16_t)~used;
    if (!sequencer && !padWritable_) { say("NO FREE PROJECT TRACKS"); return false; }
    if (!sceneKeys_) s_.beginPerformanceMode();
    songMode_ = false; sceneKeys_ = true;
    padEditSlot_ = selectedScene_; padCompose_ = !sequencer;
    metroSelected_ = false; sequencerMode_ = sequencer;
    s_.beginPadEdit(padEditSlot_, !sequencer);
    if (sequencer) {
        globalSeqScene_ = seqScene_;
        const int bank = kSeqScenes + padEditSlot_;
        const int source = (int)status.sceneSequencers[padEditSlot_] - 1;
        if (source != bank) {
            seqScenes_[(size_t)bank] = source >= 0 ? seqScenes_[(size_t)source] : SequencerScene{};
            Session::SeqPattern pattern{};
            for (int t = 0; t < kSeqTracks; ++t) for (int i = 0; i < kSeqSteps; ++i) {
                const auto hit = seqScenes_[(size_t)bank].grid[(size_t)t][(size_t)i];
                pattern[(size_t)t][(size_t)i] = {hit.velocity, hit.note};
            }
            s_.replaceSequencerScene(bank, seqScenes_[(size_t)bank].beats, pattern);
        }
        switchSequencerBank(bank);
        s_.attachPadSequencer(padEditSlot_, bank);
        loopSeqScene_ = bank;
        page_ = P_STEP;
    } else {
        page_ = P_HOME;
        for (int t = 0; t < kTracks; ++t) if (padTrackWritable(t)) { selectTrack(t); break; }
    }
    say("EDIT PAD " + sceneLabel(padEditSlot_) + " / AUTO SAVE");
    needClear_ = true;
    return true;
}
void SamplerApp::leavePadEditor() {
    if (!padEditing()) return;
    if (recordingNow()) endRecording();
    if (padCompose_) for (int t = 0; t < kTracks; ++t) if (padTrackWritable(t) && st_.track_has_clip[t]) sceneTracks_ |= (uint16_t)(1u << t);
    s_.endPadEdit();
    if (!padCompose_) switchSequencerBank(globalSeqScene_);
    padEditSlot_ = -1; padCompose_ = false; padWritable_ = 0;
    sequencerMode_ = false; page_ = P_HOME; needClear_ = true;
}
void SamplerApp::goHome() {
    leavePadEditor();
    if (sequencerMode_) exitSequencer();
    if (metroSelected_) selectTrack(track_);
    exitPerformanceMode(); page_ = P_HOME; ++homeNavigationSerial_;
}
void SamplerApp::exitPerformanceMode() {
    leavePadEditor();
    // The old code only cancelled playback; scene activation had already
    // replaced all 8 clip pointers. Restore the edit workspace instead.
    if (sceneKeys_) s_.restoreComposition();
    else if (st_.songPlaying) s_.stopSong();
    else s_.cancelSceneLaunch();
    songMode_ = false;
    sceneKeys_ = false;
    if (cueActive_) cueHold(false);
    say("COMPOSITION MODE");
}
void SamplerApp::selectScene(int slot, bool trigger) {
    if (padEditing()) return;
    if (slot < 0 || slot >= Session::kScenes) return;
    selectedScene_ = slot;
    sceneTracks_ = sceneSaved(slot) ? st_.sceneTracks[slot] : 0xffff;
    loopSeqScene_ = sceneSaved(slot) ? (int)st_.sceneSequencers[slot] - 1 : -1;
    if (!trigger || songMode_) return;
    if (recordingNow()) { say("FINISH RECORDING FIRST"); return; }
    if (!sceneSaved(slot)) { say("EMPTY PAD " + sceneLabel(slot)); return; }
    s_.launchScene(slot);
    say("SCENE " + sceneLabel(slot) + " QUEUED");
}
void SamplerApp::selectLoopSequencerScene(int slot) {
    loopSeqScene_ = std::max(-1, std::min(kSeqScenes - 1, slot));
}
void SamplerApp::toggleSceneTrack(int t) {
    if (t >= 0 && t < kTracks) sceneTracks_ ^= (uint16_t)(1u << t);
}
void SamplerApp::saveScene() {
    if (recordingNow()) { say("END RECORDING FIRST"); return; }
    const auto status = s_.status();
    const bool compose = status.compositionValid;
    const int32_t sourceSamples = compose ? status.compositionSamples
                                          : (int32_t)status.loopSamples;
    uint16_t available = compose ? status.compositionTracks : 0;
    if (!compose)
        for (int t = 0; t < kTracks; ++t)
            if (status.track_has_clip[t]) available |= (uint16_t)(1u << t);
    if ((!sourceSamples || !(sceneTracks_ & available)) && loopSeqScene_ < 0) {
        say("SELECT A LOOP OR SEQ SCENE"); return;
    }
    const bool overwrite = sceneSaved(selectedScene_);
    s_.captureScene(selectedScene_, sceneTracks_, loopSeqScene_);
    say((overwrite ? "REPLACED " : "SAVED TO ") + sceneLabel(selectedScene_));
}
void SamplerApp::deleteScene() {
    s_.clearScene(selectedScene_);
    for (int& x : songOrder_) if (x == selectedScene_) x = -1;
    songOrder_.erase(std::remove(songOrder_.begin(), songOrder_.end(), -1), songOrder_.end());
    songSelected_ = songOrder_.empty() ? -1 : std::min(songSelected_, (int)songOrder_.size() - 1);
    s_.saveArrangement(songOrder_);
    say("SCENE DELETED");
}
void SamplerApp::selectSongStep(int step) {
    if (step >= 0 && step < (int)songOrder_.size()) {
        songSelected_ = step;
        selectedScene_ = songOrder_[(size_t)step];
        sceneTracks_ = st_.sceneTracks[selectedScene_];
        loopSeqScene_ = (int)st_.sceneSequencers[selectedScene_] - 1;
    }
}
void SamplerApp::songAdd() {
    if (!sceneSaved(selectedScene_)) { say("SAVE THIS SCENE FIRST"); return; }
    if (songOrder_.size() >= 64) { say("SONG LIMIT: 64 SCENES"); return; }
    const int at = std::min((int)songOrder_.size(), songSelected_ + 1);
    songOrder_.insert(songOrder_.begin() + at, selectedScene_);
    songSelected_ = at;
    s_.saveArrangement(songOrder_);
    say(sceneLabel(selectedScene_) + " ADDED");
}
void SamplerApp::songRemove() {
    if (songSelected_ < 0 || songSelected_ >= (int)songOrder_.size()) return;
    songOrder_.erase(songOrder_.begin() + songSelected_);
    songSelected_ = songOrder_.empty() ? -1 : std::min(songSelected_, (int)songOrder_.size() - 1);
    s_.saveArrangement(songOrder_);
}
void SamplerApp::songMove(int direction) {
    const int to = songSelected_ + direction;
    if (songSelected_ < 0 || to < 0 || to >= (int)songOrder_.size()) return;
    std::swap(songOrder_[(size_t)songSelected_], songOrder_[(size_t)to]);
    songSelected_ = to;
    s_.saveArrangement(songOrder_);
}
void SamplerApp::playSong() {
    if (recordingNow()) { say("END RECORDING FIRST"); return; }
    if (songOrder_.empty()) { say("ADD SCENES TO THE SONG"); return; }
    s_.startSong(songOrder_);
    say("SONG FROM THE START");
}
void SamplerApp::stopSong() {
    s_.stopSong();
    say("SONG STOPPED");
}


// ---- full project: all UI/music parameters and embedded sample source paths ----
// The engine writes an atomic FMP2 archive on its worker and captures all
// loop/scene buffers on one audio callback. This app section contains only
// trivially-copyable musical state, never pointers or MIDI connection handles.
namespace {
struct FullProjectSettings {
    uint32_t magic = 0x32505041;  // APP2
    // v4 adds independent sequencer and tempo fields without changing the v2/v3 prefix.
    uint32_t version = 10, bytes = sizeof(FullProjectSettings);
    std::array<SamplerApp::TrackState, 8> tracks{};
    SamplerApp::ScaleState scale{};
    float reverbSize = .7f, reverbWet = .35f, delayTime = .4f, delayFeedback = .45f;
    float exciter = 0.f, lofi = 0.f, wobble = 0.f, loopSpeed = 2.f / 3.f, micThreshold = .452f;
    int32_t loopSource = 1, selectedTrack = 0, page = 0;
    int32_t selectedScene = 0, selectedSongStep = -1, cueDivision = 4;
    uint8_t sceneTrackMask = 0xff, performance = 0, songMode = 0, reserved = 0;
    uint32_t songSize = 0;
    std::array<int32_t, 64> songOrder{};
    std::array<std::array<char, 96>, 8> sampleNames{};
    // Appended v4 fields. Preserve the complete v2/v3 prefix for old projects.
    std::array<std::array<uint8_t, SamplerApp::kSeqSteps>, SamplerApp::kSeqTracks> seqVelocity{};
    std::array<std::array<uint8_t, SamplerApp::kSeqSteps>, SamplerApp::kSeqTracks> seqNote{};
    std::array<std::array<char, 96>, SamplerApp::kSeqTracks> seqSampleNames{};
    int32_t seqTrack = 0, seqPage = 0, seqBeats = 4, seqEditedStep = 0;
    float tempoBpm = 120.f, pitchedTempo = 1.f;
    int32_t pitchSemis = 0, pitchCents = 0;
    uint8_t sequencerMode = 0, sequencerEnabled = 1, reservedSeq[2] = {};
    // Appended v5: eight distinct complete sequencer patterns + per-scene length.
    // No entries refer to individual sequencer tracks in LOOP Mode.
    std::array<std::array<std::array<uint8_t, SamplerApp::kSeqSteps>, SamplerApp::kSeqTracks>, SamplerApp::kSeqScenes> seqScenesVelocity{};
    std::array<std::array<std::array<uint8_t, SamplerApp::kSeqSteps>, SamplerApp::kSeqTracks>, SamplerApp::kSeqScenes> seqScenesNote{};
    std::array<uint8_t, SamplerApp::kSeqScenes> seqScenesBeats{};
    int32_t selectedSeqScene = 0, selectedLoopSeqScene = -1;
    // Appended v6: preserve byte-for-byte v5 prefix for existing user archives.
    std::array<SamplerApp::TrackState, 8> extraTracks{};
    std::array<std::array<char, 96>, 8> extraSampleNames{};
    uint16_t sceneTrackMask16 = 0xffff;
    uint8_t metronomeSelected = 0, metronomeMode = 0;
    int32_t metronomeBeats = 4;
    float metronomeSwing = 0.f, metronomeVolume = .7f;
    // v7: appended grid calibration, previous project layouts unchanged.
    float tempoCorrection = 1.f;
    std::array<std::array<std::array<uint8_t, SamplerApp::kSeqSteps>, SamplerApp::kSeqTracks>, Session::kScenes> padVelocity{}, padNote{};
    std::array<uint8_t, Session::kScenes> padBeats{};
    // v9 is private current-session recovery only; manual projects remain v8.
    int32_t recoveryPadSlot = -1, recoveryView = 0;
    uint16_t recoveryPadWritable = 0;
    uint8_t recoveryPadCompose = 0, recoveryReserved = 0;
    // v10: project-wide lane gain, for manual saves and recovery alike.
    std::array<float, SamplerApp::kSeqTracks> seqVolume{1,1,1,1,1,1,1,1};
};
static_assert(std::is_trivially_copyable_v<FullProjectSettings>);
}
std::string SamplerApp::projectName(int slot) const {
    if (slot < 0 || slot >= Session::kProjectSlots) return "";
    const std::string path = s_.dataDir() + "/project_" + std::to_string(slot + 1) + ".name";
    std::ifstream in(path);
    std::string value;
    if (std::getline(in, value) && !value.empty()) return value;
    return "PROJECT " + std::to_string(slot + 1);
}

void SamplerApp::submitName(int kind, const std::string& name) {
    if (name.empty()) { say("NAME REQUIRED"); return; }
    if (kind == 1) { loopSave(name); return; }
    if (kind != 2) return;
    const int slot = pendingProjectSlot_;
    pendingProjectSlot_ = -1;
    if (slot < 0 || slot >= Session::kProjectSlots) { say("NO PROJECT SLOT SELECTED"); return; }
    if (!saveFullProject(slot)) return;
    // Sidecar label only: full FMP2 audio, scene, song and compose project format is unchanged.
    pendingProjectLabel_ = name;
    pendingProjectLabelSlot_ = slot;
}

bool SamplerApp::fullProjectExists(int slot) const { return s_.projectExists(slot); }
bool SamplerApp::saveFullProject(int slot) {
    if (slot < 0 || slot >= Session::kProjectSlots) return false;
    return saveProjectSnapshot(slot);
}
bool SamplerApp::saveProjectSnapshot(int slot, bool force) {
    const bool recovery = slot == Session::kRecoverySlot;
    if (recordingNow() || mic_ != 0) { if (!recovery) say("FINISH RECORDING FIRST"); return false; }
    FullProjectSettings m{};
    if (recovery) { m.recoveryPadSlot = padEditSlot_; m.recoveryPadCompose = padCompose_; m.recoveryPadWritable = padWritable_; m.recoveryView = recoveryView_; }
    m.seqVolume = seqVolume_;
    std::copy_n(ts_.begin(), 8, m.tracks.begin());
    std::copy_n(ts_.begin() + 8, 8, m.extraTracks.begin());
    m.metronomeSelected = metroSelected_;
    m.metronomeBeats = metroBeats_; m.metronomeSwing = metroSwing_;
    m.metronomeVolume = metroVolume_; m.metronomeMode = (uint8_t)metroMode_;
    m.sceneTrackMask16 = sceneTracks_;
    m.scale = scale_;
    m.reverbSize = reverbSize_; m.reverbWet = reverbDw_;
    m.delayTime = delayTime_; m.delayFeedback = delayFb_;
    m.exciter = exciter_; m.lofi = lofi_; m.wobble = wobble_;
    m.loopSpeed = loopSpeed_; m.micThreshold = micThr_; m.loopSource = loopSrc_;
    m.selectedTrack = track_; m.page = (int)page_;
    m.selectedScene = selectedScene_; m.selectedSongStep = songSelected_;
    m.cueDivision = cueDivision_;
    m.sceneTrackMask = (uint8_t)(sceneTracks_ & 0xff); m.performance = sceneKeys_; m.songMode = songMode_;
    m.songSize = (uint32_t)std::min<size_t>(64, songOrder_.size());
    m.seqTrack = seqTrack_; m.seqPage = seqPage_; m.seqBeats = seqBeats_;
    m.selectedSeqScene = padSequencerEditing() ? globalSeqScene_ : seqScene_;
    for (int p = 0; p < Session::kScenes; ++p) {
        const auto& bank = seqScenes_[(size_t)(kSeqScenes + p)];
        m.padBeats[(size_t)p] = (uint8_t)bank.beats;
        for (int t = 0; t < kSeqTracks; ++t) for (int i = 0; i < kSeqSteps; ++i) {
            const auto hit = bank.grid[(size_t)t][(size_t)i];
            m.padVelocity[(size_t)p][(size_t)t][(size_t)i] = hit.velocity;
            m.padNote[(size_t)p][(size_t)t][(size_t)i] = hit.note;
        }
    }
    m.selectedLoopSeqScene = loopSeqScene_;
    for (int scene = 0; scene < kSeqScenes; ++scene) {
        // The active scene is edited live in seqGrid_/seqBeats_ and may not
        // have been switched away from yet. Snapshot those live edits too.
        auto pattern = seqScenes_[(size_t)scene];
        if (scene == seqScene_) {
            pattern.grid = seqGrid_;
            pattern.beats = seqBeats_;
        }
        m.seqScenesBeats[(size_t)scene] = (uint8_t)pattern.beats;
        for (int t = 0; t < kSeqTracks; ++t)
            for (int step = 0; step < kSeqSteps; ++step) {
                const auto& hit = pattern.grid[(size_t)t][(size_t)step];
                m.seqScenesVelocity[(size_t)scene][(size_t)t][(size_t)step] = hit.velocity;
                m.seqScenesNote[(size_t)scene][(size_t)t][(size_t)step] = hit.note;
            }
    }
    m.seqEditedStep = seqEditStep_; m.sequencerMode = sequencerMode_ && (recovery || !padEditing());
    m.sequencerEnabled = 1;
    m.tempoBpm = tempoFlexBpm_; m.pitchedTempo = pitchedTempo_;
    m.tempoCorrection = tempoCorrection_;
    m.pitchSemis = pitchSemis_; m.pitchCents = pitchCents_;
    for (int t = 0; t < kSeqTracks; ++t) {
        std::snprintf(m.seqSampleNames[(size_t)t].data(),
                      m.seqSampleNames[(size_t)t].size(), "%s",
                      seqSourceNames_[(size_t)t].c_str());
        for (int i = 0; i < kSeqSteps; ++i) {
            const auto hit = seqGrid_[(size_t)t][(size_t)i];
            m.seqVelocity[(size_t)t][(size_t)i] = hit.velocity;
            m.seqNote[(size_t)t][(size_t)i] = hit.note;
        }
    }
    for (uint32_t i = 0; i < m.songSize; ++i) m.songOrder[i] = songOrder_[i];
    std::array<std::string, kTracks> sources{};
    for (int i = 0; i < kTracks; ++i) {
        const std::string label = s_.trackLabel(i);
        auto& storedName = i < 8 ? m.sampleNames[(size_t)i] : m.extraSampleNames[(size_t)(i - 8)];
        std::snprintf(storedName.data(), storedName.size(), "%s", label.c_str());
        sources[(size_t)i] = trackSourcePaths_[(size_t)i];
        if (sources[(size_t)i].empty() && hasSample(i)) {
            if (!recovery) say("SAMPLE SOURCE MISSING ON TRACK " + std::to_string(i + 1));
            return false; // never silently save a project without its sound
        }
        if (!sources[(size_t)i].empty() && !std::filesystem::exists(sources[(size_t)i])) {
            if (!recovery) say("SAMPLE FILE MISSING ON TRACK " + std::to_string(i + 1));
            return false;
        }
    }
    std::array<std::string, kSeqTracks> sequencerSources{};
    for (int i = 0; i < kSeqTracks; ++i) {
        const auto& source = seqSourcePaths_[(size_t)i];
        if (!source.empty() && !std::filesystem::is_regular_file(source)) {
            if (!recovery) say("SEQ SAMPLE MISSING ON TRACK " + std::to_string(i + 1));
            return false;
        }
        sequencerSources[(size_t)i] = source;
    }
    std::vector<uint8_t> bytes(m.bytes);
    std::memcpy(bytes.data(), &m, bytes.size());
    if (recovery) {
        uint64_t hash = 14695981039346656037ULL;
        auto mix = [&](uint64_t x) { hash ^= x; hash *= 1099511628211ULL; };
        for (uint8_t x : bytes) mix(x);
        for (const auto& path : sources) { for (unsigned char x : path) mix(x); mix(0); }
        for (const auto& path : sequencerSources) { for (unsigned char x : path) mix(x); mix(0); }
        mix(s_.recoveryRevision()); mix((uint64_t)st_.loopSamples); mix(st_.sceneMask);
        mix(st_.playing); mix(st_.activeScene + 1); mix(st_.queuedScene + 1); mix(st_.songStep + 1);
        mix(st_.compositionTracks); mix(st_.compositionValid);
        for (int t = 0; t < kTracks; ++t) mix(st_.track_has_clip[t]);
        if (!st_.playing) mix((uint64_t)std::max(0., st_.pos));
        if (!force && hash == recoveryFingerprint_ && !(st_.playing && nowMs_ - recoveryLastWrite_ >= 10000)) return true;
        if (!s_.saveProject(slot, std::move(bytes), sources, sequencerSources)) return false;
        recoveryPendingFingerprint_ = hash; recoveryWriting_ = true;
        return true;
    }
    if (!s_.saveProject(slot, std::move(bytes), sources, sequencerSources)) {
        say("PROJECT BUSY OR INVALID"); return false;
    }
    say("SAVING PROJECT " + std::to_string(slot + 1));
    return true;
}
bool SamplerApp::loadFullProject(int slot) {
    if (slot < 0 || slot >= Session::kProjectSlots) return false;
    if (songExportState_ == 1) { say("FINISH SONG EXPORT FIRST"); return false; }
    if (recordingNow() || mic_ != 0) { say("FINISH RECORDING FIRST"); return false; }
    if (!s_.loadProject(slot)) { say("EMPTY PROJECT OR BUSY"); return false; }
    say("LOADING PROJECT " + std::to_string(slot + 1));
    return true;
}
void SamplerApp::applyLoadedProject(const Session::LoadedProject& project) {
    if (songExportState_ != 1) songExportState_ = 0;
    const size_t legacySize = offsetof(FullProjectSettings, seqVelocity);
    const bool oldProject = project.appState.size() == legacySize;
    const bool v4Project = project.appState.size() == offsetof(FullProjectSettings, seqScenesVelocity);
    const bool v5Project = project.appState.size() == offsetof(FullProjectSettings, extraTracks);
    const bool v10Project = project.appState.size() == sizeof(FullProjectSettings);
    const bool v9Project = v10Project || project.appState.size() == offsetof(FullProjectSettings, seqVolume);
    const bool v8Project = v9Project || project.appState.size() == offsetof(FullProjectSettings, recoveryPadSlot);
    const bool v7Project = v8Project || project.appState.size() == offsetof(FullProjectSettings, padVelocity);
    const bool v6Project = v7Project || project.appState.size() == offsetof(FullProjectSettings, tempoCorrection);
    if (!oldProject && !v4Project && !v5Project && !v6Project) {
        say("PROJECT SETTINGS INCOMPATIBLE"); return;
    }
    FullProjectSettings m{};
    std::memcpy(&m, project.appState.data(), project.appState.size());
    if (m.magic != 0x32505041 ||
        (oldProject ? (m.version != 2 && m.version != 3) : v4Project ? m.version != 4 : v5Project ? m.version != 5 : v10Project ? m.version != 10 : v9Project ? m.version != 9 : v8Project ? m.version != 8 : v7Project ? m.version != 7 : m.version != 6) ||
        m.bytes != project.appState.size() || m.songSize > 64 ||
        m.selectedTrack < 0 || m.selectedTrack >= kTracks ||
        m.page < 0 || m.page >= kPages || m.selectedScene < 0 ||
        m.selectedScene >= Session::kScenes || m.selectedSongStep < -1 ||
        m.selectedSongStep >= (int)m.songSize || m.cueDivision < 0 ||
        m.cueDivision > (m.version == 2 ? 5 : 11) ||
        m.performance > 1 || m.songMode > 1) {
        say("PROJECT SETTINGS CORRUPT"); return;
    }
    // These scene/transport flags match the engine snapshot restored on the
    // same audio callback. Changing COMPOSE/LOOP/SONG here must not restart it.
    std::copy_n(m.tracks.begin(), 8, ts_.begin());
    if (v6Project) std::copy_n(m.extraTracks.begin(), 8, ts_.begin() + 8);
    else for (int i = 8; i < kTracks; ++i) { ts_[(size_t)i] = TrackState{}; ts_[(size_t)i].vol = .8f; }
    for (int t = 0; t < kSeqTracks; ++t) {
        const float gain = v10Project && std::isfinite(m.seqVolume[(size_t)t]) ? cl(m.seqVolume[(size_t)t], 0.f, 1.5f) : 1.f;
        seqVolume_[(size_t)t] = gain;
        s_.setSequencerVolume(t, gain);
    }
    padEditSlot_ = -1; padCompose_ = false; padWritable_ = 0;
    metroSelected_ = v6Project && m.metronomeSelected != 0;
    metroBeats_ = v6Project ? std::clamp((int)m.metronomeBeats, 1, 64) : 4;
    metroSwing_ = v6Project && std::isfinite(m.metronomeSwing) ? cl(m.metronomeSwing) : 0.f;
    metroVolume_ = v6Project && std::isfinite(m.metronomeVolume) ? cl(m.metronomeVolume) : .7f;
    metroMode_ = v6Project ? std::clamp((int)m.metronomeMode, 0, 5) : 2;
    tempoCorrection_ = v7Project && std::isfinite(m.tempoCorrection) ? cl(m.tempoCorrection, .05f, 40.f) : 1.f;
    haveTempoTap_ = false; tempoTapIntervals_.clear();
    s_.setTempoCorrection(tempoCorrection_);
    applyMetronome();
    scale_ = m.scale;
    reverbSize_ = m.reverbSize; reverbDw_ = m.reverbWet;
    delayTime_ = m.delayTime; delayFb_ = m.delayFeedback;
    exciter_ = m.exciter; lofi_ = m.lofi; wobble_ = m.wobble;
    loopSpeed_ = m.loopSpeed; micThr_ = m.micThreshold; loopSrc_ = m.loopSource;
    track_ = m.selectedTrack; page_ = (Page)m.page;
    selectedScene_ = m.selectedScene; songSelected_ = m.selectedSongStep;
    sceneTracks_ = v6Project ? m.sceneTrackMask16 : (uint16_t)m.sceneTrackMask;
    loopSeqScene_ = v4Project || oldProject ? -1 : std::max(-1, std::min(Session::kSeqBanks - 1, (int)m.selectedLoopSeqScene));
    cueDivision_ = m.version == 2 ? m.cueDivision * 2 : m.cueDivision;
    // v2 saved 0..5 as straight divisions. v3 interleaves T values,
    // keeping the original rhythmic choice without losing old projects.
    sceneKeys_ = m.performance; songMode_ = m.songMode;
    if (!oldProject) {
        seqTrack_ = std::max(0, std::min(kSeqTracks - 1, (int)m.seqTrack));
        seqBeats_ = std::max(1, std::min(64, (int)m.seqBeats));
        seqPage_ = std::max(0, std::min((seqBeats_ * 4 - 1) / 16, (int)m.seqPage));
        seqEditStep_ = std::max(0, std::min(kSeqSteps - 1, (int)m.seqEditedStep));
        sequencerMode_ = m.sequencerMode != 0;
        tempoFlexBpm_ = std::isfinite(m.tempoBpm) ? cl(m.tempoBpm, 30.f, 300.f) : 120.f;
        pitchedTempo_ = std::isfinite(m.pitchedTempo) ? cl(m.pitchedTempo, .25f, 2.f) : 1.f;
        pitchSemis_ = std::max(-24, std::min(24, (int)m.pitchSemis));
        pitchCents_ = std::max(-100, std::min(100, (int)m.pitchCents));
        seqScene_ = v4Project ? 0 : std::max(0, std::min(kSeqScenes - 1, (int)m.selectedSeqScene));
        // Legacy v4 had one pattern; migrate it into sequencer scene 1.
        for (int scene = 0; scene < kSeqScenes; ++scene) {
            auto& bank = seqScenes_[(size_t)scene];
            bank.beats = v4Project ? (scene == 0 ? seqBeats_ : 4) :
                std::max(1, std::min(64, (int)m.seqScenesBeats[(size_t)scene]));
            for (int t = 0; t < kSeqTracks; ++t)
                for (int step = 0; step < kSeqSteps; ++step) {
                    auto& hit = bank.grid[(size_t)t][(size_t)step];
                    hit.velocity = v4Project ? (scene == 0 ? m.seqVelocity[(size_t)t][(size_t)step] : 0)
                        : m.seqScenesVelocity[(size_t)scene][(size_t)t][(size_t)step];
                    hit.note = v4Project ? (scene == 0 ? m.seqNote[(size_t)t][(size_t)step] : 60)
                        : m.seqScenesNote[(size_t)scene][(size_t)t][(size_t)step];
                }
            Session::SeqPattern restored{};
            for (int t = 0; t < kSeqTracks; ++t)
                for (int step = 0; step < kSeqSteps; ++step) {
                    const auto hit = bank.grid[(size_t)t][(size_t)step];
                    restored[(size_t)t][(size_t)step] = {hit.velocity, hit.note};
                }
            s_.replaceSequencerScene(scene, bank.beats, restored);
        }
        seqGrid_ = seqScenes_[(size_t)seqScene_].grid;
        seqBeats_ = seqScenes_[(size_t)seqScene_].beats;
        s_.selectSequencerScene(seqScene_);
        s_.setSequencerEnabled(m.sequencerEnabled != 0);
        for (int t = 0; t < kSeqTracks; ++t) {
            const auto& source = project.seqSamplePaths[(size_t)t];
            const auto& title = m.seqSampleNames[(size_t)t];
            const auto end = std::find(title.begin(), title.end(), '\0');
            seqSourcePaths_[(size_t)t] = source;
            seqSourceNames_[(size_t)t] = std::string(title.begin(), end);
            if (source.empty()) s_.clearSequencerSample(t);
            else s_.loadSequencerSamplePath(source, t);
            for (int i = 0; i < kSeqSteps; ++i) {
                auto& hit = seqGrid_[(size_t)t][(size_t)i];
                hit = seqGrid_[(size_t)t][(size_t)i];
            }
        }
        s_.setMasterBpm(tempoFlexBpm_);
        s_.setGlobalPitch((float)pitchSemis_, (float)pitchCents_);
        s_.setPitchedTempo(pitchedTempo_);
    } else {
        // v2/v3 archives remain playable with fresh, unmodified sequencer lanes.
        sequencerMode_ = false;
        seqScene_ = 0;
        for (auto& bank : seqScenes_) {
            bank.beats = 4;
            for (auto& lane : bank.grid)
                for (auto& hit : lane) hit = {0, 60};
        }
        for (int t = 0; t < kSeqTracks; ++t)
            for (int i = 0; i < kSeqSteps; ++i) {
                seqGrid_[(size_t)t][(size_t)i] = {0, 60};
                s_.setSequencerStep(t, i, 0, 60);
            }
    }
    for (int p = 0; p < Session::kScenes; ++p) {
        auto& bank = seqScenes_[(size_t)(kSeqScenes + p)];
        bank = SequencerScene{};
        bank.beats = v8Project ? std::clamp((int)m.padBeats[(size_t)p], 1, 64) : 4;
        Session::SeqPattern pattern{};
        for (int t = 0; t < kSeqTracks; ++t) for (int i = 0; i < kSeqSteps; ++i) {
            auto& hit = bank.grid[(size_t)t][(size_t)i];
            hit = {v8Project ? m.padVelocity[(size_t)p][(size_t)t][(size_t)i] : (uint8_t)0, v8Project ? m.padNote[(size_t)p][(size_t)t][(size_t)i] : (uint8_t)60};
            pattern[(size_t)t][(size_t)i] = {hit.velocity, hit.note};
        }
        s_.replaceSequencerScene(kSeqScenes + p, bank.beats, pattern);
    }
    if (page_ == P_STEP && !sequencerMode_) page_ = P_HOME;
    songOrder_.clear();
    for (uint32_t i = 0; i < m.songSize; ++i) songOrder_.push_back(m.songOrder[i]);
    scenesLoaded_ = true;
    cueActive_ = cueSelect_ = playModified_ = false;
    held_ = 0; mic_ = 0; micTrack_ = 0;
    auditionAt_ = 0; previewNote_ = -1; previewAfterLoad_ = false; commitAt_ = 0;
    pending_.clear(); pendingBytes_ = 0; needClear_ = true; fwDirty_ = true;
    // Install stable paths *before* rebuilding the browser so future
    // refreshes/secondary saves cannot accidentally pick a different WAV.
    for (int i = 0; i < kTracks; ++i) {
        const std::string& path = project.samplePaths[(size_t)i];
        const auto& stored = i < 8 ? m.sampleNames[(size_t)i] : m.extraSampleNames[(size_t)(i - 8)];
        const auto end = std::find(stored.begin(), stored.end(), '\0');
        std::string title(stored.begin(), end);
        if (title.empty() && !path.empty())
            title = "P" + std::to_string(project.slot + 1) +
                    " TRACK " + std::to_string(i + 1);
        trackSourcePaths_[(size_t)i] = path;
        trackSourceNames_[(size_t)i] = title;
    }
    refreshLibrary();
    for (int i = 0; i < kTracks; ++i) {
        const auto& path = trackSourcePaths_[(size_t)i];
        if (path.empty()) s_.clearSample(i);
        else s_.loadSamplePath(path, ts_[(size_t)i].algo == 3, i,
                               trackSourceNames_[(size_t)i]);
    }
    libCursor_ = std::max(0, ts_[(size_t)track_].entry);
    applyFx();
    s_.setLoopSource(loopSrc_);
    s_.setLoopSpeed(loopSpeedOf(loopSpeed_));
    s_.setMicThreshold(micThrOf(micThr_));
    for (int t = 0; t < kTracks; ++t) applyAll(t);
    if (project.slot == Session::kRecoverySlot) {
        if (v9Project && m.recoveryPadSlot >= 0 && m.recoveryPadSlot < Session::kScenes && st_.activeScene == m.recoveryPadSlot) {
            padEditSlot_ = m.recoveryPadSlot; padCompose_ = m.recoveryPadCompose != 0; padWritable_ = m.recoveryPadWritable;
            globalSeqScene_ = seqScene_;
            if (!padCompose_) {
                switchSequencerBank(kSeqScenes + padEditSlot_); sequencerMode_ = true; page_ = P_STEP;
                seqPage_ = std::clamp((int)m.seqPage, 0, (seqBeats_ * 4 - 1) / 16);
                seqEditStep_ = std::clamp((int)m.seqEditedStep, 0, kSeqSteps - 1);
            }
            s_.restorePadEditor(padEditSlot_, padCompose_ ? padWritable_ : 0);
        }
        restoredView_ = v9Project ? std::clamp((int)m.recoveryView, 0, 4) : 0;
        recoveryView_ = restoredView_; ++recoveryNavigationSerial_;
        say(recoveryNew_ ? "NEW PROJECT" : "SESSION RESTORED");
    } else say("PROJECT " + std::to_string(project.slot + 1) + " RESTORED");
}

void SamplerApp::enableRecovery() {
    if (recoveryEnabled_) return;
    recoveryEnabled_ = true;
    if (s_.projectExists(Session::kRecoverySlot)) {
        recoveryRestoring_ = s_.loadProject(Session::kRecoverySlot);
        recoveryBlocked_ = !recoveryRestoring_;
    }
}
void SamplerApp::requestCheckpoint(bool finishRecording) {
    if (!recoveryEnabled_) return;
    recoveryRequested_ = true;
    recoveryFinishing_ |= finishRecording;
    if (finishRecording && recordingNow()) endRecording();
    if (finishRecording && (mic_ == 1 || mic_ == 2)) micSample();
}
bool SamplerApp::checkpointPending() const {
    return recoveryRestoring_ || recoveryRequested_ || recoveryWriting_;
}
bool SamplerApp::newFullProject() {
    if (recordingNow() || mic_ != 0 || songExportState_ == 1 || s_.loopExportStatus() == 1) {
        say("FINISH RECORDING OR EXPORT FIRST"); return false;
    }
    FullProjectSettings m{};
    m.version = 10; m.recoveryView = 0;
    m.sceneTrackMask16 = 0xffff; m.metronomeMode = 2;
    m.seqScenesBeats.fill(4); m.padBeats.fill(4);
    for (auto& row : m.seqNote) row.fill(60);
    for (auto& scene : m.seqScenesNote) for (auto& row : scene) row.fill(60);
    for (auto& scene : m.padNote) for (auto& row : scene) row.fill(60);
    std::vector<uint8_t> bytes(sizeof(m)); std::memcpy(bytes.data(), &m, bytes.size());
    if (!s_.newRecoveryProject(std::move(bytes))) { say("PROJECT BUSY - TRY AGAIN"); return false; }
    recoveryRestoring_ = recoveryNew_ = true; recoveryRequested_ = false;
    say("CREATING NEW PROJECT"); return true;
}
void SamplerApp::tickRecovery(uint32_t now) {
    if (!recoveryEnabled_) return;
    if (recoveryWriting_ && s_.recoveryOperation() != 1) {
        recoveryWriting_ = false;
        if (s_.recoveryOperation() == 2) {
            recoveryFingerprint_ = recoveryPendingFingerprint_; recoveryLastWrite_ = now;
        } else say("SESSION AUTO SAVE FAILED - PREVIOUS CHECKPOINT KEPT");
    }
    if (recoveryBlocked_) { recoveryRequested_ = false; recoveryFinishing_ = false; return; }
    if (recoveryRestoring_ || recoveryWriting_ || s_.projectOperation() == 3) return;
    if (recoveryFinishing_ && recordingNow()) endRecording();
    if (recoveryFinishing_ && (mic_ == 1 || mic_ == 2)) micSample();
    if (recordingNow() || mic_ != 0) return;
    if (!recoveryRequested_ && now - recoveryLastAttempt_ < 3000) return;
    recoveryLastAttempt_ = now;
    const bool forced = recoveryRequested_;
    recoveryRequested_ = false; recoveryFinishing_ = false;
    if (!saveProjectSnapshot(Session::kRecoverySlot, forced) && forced)
        say("SESSION AUTO SAVE FAILED - PREVIOUS CHECKPOINT KEPT");
}

// ---- looper, recorder ----
void SamplerApp::endRecording() {
    if (st_.freeArmed || st_.freeRecording) { s_.freeRecord(track_, false); say(st_.freeArmed && !st_.freeRecording ? "CANCELLED" : "LOOP SET"); }
    for (int t = 0; t < kTracks; t++) if (st_.track_clip_rec[t]) s_.clipRecord(t, false);
}
void SamplerApp::transportPlay() {   // normal transport: modifiers only work through physical MIDI button presses
    if (recordingNow()) { endRecording(); return; }
    if (songMode_) { if (st_.songPlaying) stopSong(); else playSong(); return; }
    s_.play(!st_.playing);
}
void SamplerApp::transportSet(bool play) {
    // The mobile screen has ordinary transport controls only. OCT combinations
    // are decoded solely from physical/external MIDI button note events.
    if (recordingNow()) { if (!play) { endRecording(); s_.play(false); } return; }
    if (songMode_) { if (play) playSong(); else stopSong(); return; }
    s_.play(play);
}
void SamplerApp::restartLoop() {
    if (recordingNow()) { say("FINISH RECORDING FIRST"); return; }
    if (songMode_ && !songOrder_.empty()) {
        playSong();  // restart the full arrangement on long PLAY in SONG mode
        return;
    }
    if (st_.loopSamples <= 0) { say("NO LOOP TO RESTART"); return; }
    cueHold(false);
    s_.seek(0.f);
    s_.play(true);
    say("LOOP FROM START");
}
void SamplerApp::setCueDivision(int division) {
    cueDivision_ = std::max(0, std::min(11, division));
    if (cueActive_) s_.setCue(true, cueDivision_);
}
void SamplerApp::cueHold(bool down) {
    if (!down) {
        if (cueActive_) { cueActive_ = false; s_.setCue(false, cueDivision_); }
        return;
    }
    if (recordingNow()) { say("FINISH RECORDING FIRST"); return; }
    if (!loopAny_ || st_.loopSamples <= 0) { say("NO LOOP FOR NOTE REPEAT"); return; }
    if (!cueActive_) {
        cueActive_ = true;
        s_.setCue(true, cueDivision_);
    }
}
void SamplerApp::loopRecord() {
    if (metroSelected_ || !padTrackWritable(track_)) { say("SELECT A FREE LOOP TRACK TO RECORD"); return; }
    if (st_.songPlaying) { say("STOP SONG BEFORE RECORDING"); return; }
    if (st_.freeArmed || st_.freeRecording) { s_.freeRecord(track_, false); say("LOOP SET"); return; }
    if (!padEditing() && !loopAny_ && !st_.track_clip_rec[track_]) {   // a clean looper: arm the first take, it starts with the first note (or sound) and sets the loop length
        s_.freeRecord(track_, true);
        say(loopSrc_ ? "PLAY A KEY TO START" : "WAITING FOR SOUND");
        return;
    }
    if (!st_.playing) s_.play(true);
    s_.clipRecord(track_, !st_.track_clip_rec[track_]);
    say(st_.track_clip_rec[track_] ? "OVERDUB OFF" : "OVERDUB");
}
void SamplerApp::lenAction(bool dbl) { if (padEditing()) { say("PAD LENGTH LOCKED"); return; }   // loop x2 (the content repeats) or /2; guarded so a dragged slider does not double it again and again
    if (st_.loopSamples <= 0 || nowMs_ - lenAt_ < 300) return;
    lenAt_ = nowMs_ ? nowMs_ : 1;
    if (dbl) { s_.loopDouble(); say("LOOP DOUBLED"); } else { s_.loopHalve(); say("LOOP HALVED"); }
}
void SamplerApp::loopSave(const std::string& name) {
    if (recordingNow() || mic_ != 0) { say("FINISH RECORDING FIRST"); return; }
    if (!s_.saveClip(track_, name.empty() ? "Loop Track " + std::to_string(track_ + 1) : name)) {
        say("NO LOOP TO SAVE"); return;
    }
    say("LOOP SAVE REQUESTED");
    previewAfterLoad_ = false;
    auditionAt_ = 0;
    commitAt_ = nowMs_ + 1;   // refresh the library shortly, once the file is written
    commitTrack_ = -2;
}
void SamplerApp::loopClearTrack() {
    if (padEditing()) { say("EXIT PAD EDIT TO CLEAR LOOPS"); return; }
    if (metroSelected_) { say("METRONOME HAS NO AUDIO CLIP"); return; }
    if (!st_.track_has_clip[track_]) { say("NO LOOP ON THIS TRACK"); return; }
    s_.clipClear(track_);
    say("LOOP OF TRACK " + std::to_string(track_ + 1) + " CLEARED");
}
void SamplerApp::loopClear() { if (padEditing()) { say("EXIT PAD EDIT TO CLEAR LOOPS"); return; }   // PROJECT menu: every loop of every track, so the looper is clean again
    s_.clipClearAll();
    say("ALL LOOPS CLEARED");
}
void SamplerApp::micSample() { if (!padTrackWritable(track_)) return;
    if (metroSelected_) { say("SELECT AN AUDIO TRACK FIRST"); return; }
    if (mic_ == 0) {
        s_.recorderWait(true); s_.recorderStart();
        mic_ = 1; micTrack_ = track_; say("SAMPLING: MAKE A SOUND");
    } else if (mic_ == 1) { s_.recorderStop(); mic_ = 0; say("SAMPLING CANCELLED"); }
    else if (mic_ == 2) { s_.recorderStop(); mic_ = 3; say("SAVING"); }
}
std::string SamplerApp::loopText() const {
    if (st_.freeArmed) return loopSrc_ ? "PLAY A KEY TO START" : "WAITING FOR SOUND";
    if (st_.freeRecording) return fmt("RECORDING %.1fS", st_.freeSeconds);
    if (st_.track_clip_rec[track_]) return "OVERDUB";
    if (!loopAny_) return "LOOP EMPTY";
    return fmt("LOOP %.0fBPM ", st_.bpm * tempoCorrection_) + std::to_string(st_.loopBars) + (st_.loopBars == 1 ? " BAR" : " BARS");
}
void SamplerApp::keyEvent(int raw, bool down) {   // MIDI scene bank in LOOP mode; ordinary keyboard elsewhere
    if (raw < 0 || raw > 127) return;
    if (sequencerMode_) {
        if (!down || raw < 53 || raw > 79) return;
        // Exactly 27 keys: F3..G5 = 16 white steps and 11 black.
        // First eight black keys select the sequencer lane.
        static constexpr int white[] = {53,55,57,59,60,62,64,65,67,69,71,72,74,76,77,79};
        static constexpr int black[] = {54,56,58,61,63,66,68,70,73,75,78};
        for (int i = 0; i < 16; ++i) if (raw == white[i]) {
            sequencerToggleStep(seqPage_ * 16 + i);
            return;
        }
        for (int i = 0; i < 8; ++i) if (raw == black[i]) {
            selectSequencerTrack(i);
            return;
        }
        return;
    }
    if (metroSelected_ && down) return; // no note triggers on the click-only track
    if (sceneKeys_ && !songMode_ && !padEditing()) {
        // Ableton labels MIDI 36..47 as C1..B1. Also accept scientific C1
        // (24..35), while the physical FM-1 first 12 keys arrive as 53..64.
        const int slot = raw >= 36 && raw <= 47 ? raw - 36 :
                         raw >= 24 && raw <= 35 ? raw - 24 :
                         raw >= 53 && raw <= 64 ? raw - 53 : -1;
        if (slot >= 0) { if (down) selectScene(slot, true); return; }
    }
    if (down) {
        if (keyCount_[raw]) keyEvent(raw, false);
        const bool kit = ts_[track_].algo == 3;   // KIT: every key is a pad (note 60 + key), whatever the tonality or octave
        const int base = kit ? 60 + (raw - 53) : raw + 12 * scale_.octave;
        int cnt = 0; keyNotes_[raw][cnt++] = base;
        if (!kit && scale_.scale < 7 && scale_.mode) {
            const int rel = base - scale_.root, o = rel >= 0 ? rel / 12 : -((-rel + 11) / 12), i = scaleIndex(rel - 12 * o, scale_.scale);
            if (i >= 0) { const int d = o * kScaleN[scale_.scale] + i; keyNotes_[raw][cnt++] = scale_.root + degreeNote(d + 2, scale_.scale, 0); keyNotes_[raw][cnt++] = scale_.root + degreeNote(d + 4, scale_.scale, 0); }
        }
        keyCount_[raw] = cnt;
        for (int j = 0; j < cnt; j++) playNote(keyNotes_[raw][j], true);
    } else {
        for (int j = 0; j < keyCount_[raw]; j++) playNote(keyNotes_[raw][j], false);
        keyCount_[raw] = 0;
    }
}
void SamplerApp::playNote(int note, bool down) {
    if (note < 0 || note > 127) return;
    if (down) { if (!padTrackWritable(track_)) return; heldNote_[note] = track_; s_.noteOn(track_, note, 100); }
    else if (heldNote_[note] >= 0) { s_.noteOff(heldNote_[note], note); heldNote_[note] = -1; }
}
std::vector<float> SamplerApp::peaks(int t, int bins) {
    const std::string lab = s_.trackLabel(t);
    if (peaksLabel_[t] != lab || nowMs_ - peaksAt_[t] > 700 || (int)peaks_[t].size() != bins) {
        peaksLabel_[t] = lab; peaksAt_[t] = nowMs_; peaks_[t] = s_.samplePeaksOf(t, bins);
    }
    return peaks_[t];
}

// ---- read-only live diagnostic of inbound physical MIDI ----
void SamplerApp::clearMidiDebug() {
    midiRxMessages_ = midiControlMask_ = 0;
    lastMidiChannel_ = lastMidiData_ = -1;
    lastMidiController_ = lastMidiPressed_ = false;
}
std::string SamplerApp::midiControlStatus() const {
    if (!midiRxMessages_) return "MIDI RX: NO MESSAGES YET";
    const auto seen = [this](int bit) { return midiControlMask_ & (1u << bit) ? "OK" : "--"; };
    return std::string("MIDI OCT-:") + seen(0) + " OCT+:" + seen(1) +
           " PLAY:" + seen(2) + " K1:" + seen(3);
}
std::string SamplerApp::midiLastEvent() const {
    if (!midiRxMessages_ || lastMidiChannel_ < 0) return "PRESS PHYSICAL FM-1 BUTTONS";
    std::string msg = std::string("LAST CH") + std::to_string(lastMidiChannel_ + 1) +
           (lastMidiController_ ? " CC " : " NOTE ") + std::to_string(lastMidiData_);
    if (!lastMidiController_) msg += lastMidiPressed_ ? " DOWN" : " UP";
    if (held_ & (1u << OCT_DOWN)) msg += " [OCT- HELD]";
    else if (held_ & (1u << OCT_UP)) msg += " [OCT+ HELD]";
    return msg;
}

// A lost OCT+ Note Off on USB detach used to trap K1 in NOTE REPEAT.
void SamplerApp::onMidiDisconnected() {
    if (cueActive_) cueHold(false);
    if (cueSelect_) page_ = cueReturnPage_;
    cueSelect_ = false;
    held_ = 0;
    playModified_ = false;
    octUpAt_ = 0;
    for (int raw = 0; raw < 128; ++raw) {
        for (int i = 0; i < keyCount_[raw]; ++i) playNote(keyNotes_[raw][i], false);
        keyCount_[raw] = 0;
    }
    for (int note = 0; note < 128; ++note)
        if (heldNote_[note] >= 0) playNote(note, false);
    haveDropBase_ = false;
    linkSeen_ = false;
    needClear_ = true;
    pending_.clear();
    pendingBytes_ = 0;
    clearMidiDebug();
}

// ---- input ----
void SamplerApp::onMidi(const uint8_t* m, size_t n) {
    if (n < 3) return;
    linkSeen_ = true;
    const int type = m[0] & 0xf0, ch = m[0] & 15, a = m[1], b = m[2];
    if (type == 0x90 || type == 0x80 || type == 0xb0) {
        ++midiRxMessages_;
        lastMidiChannel_ = ch; lastMidiData_ = a;
        lastMidiController_ = type == 0xb0;
        lastMidiPressed_ = type == 0x90 && b > 0;
        // Detect only known control-surface events, not ordinary keys.
        if (ch == 1 && (type == 0x90 || type == 0x80)) {
            if (a == OCT_DOWN) midiControlMask_ |= 1u;
            if (a == OCT_UP) midiControlMask_ |= 2u;
            if (a == PLAY) midiControlMask_ |= 4u;
        }
        if (ch == 0 && type == 0xb0 && a == 18) midiControlMask_ |= 8u;
    }
    if (ch == 0 && (type == 0x90 || type == 0x80)) keyEvent(a, type == 0x90 && b > 0);
    else if (ch == 1 && (type == 0x90 || type == 0x80)) button(a, type == 0x90 && b > 0);
    else if (ch == 0 && type == 0xb0) {
        if (a >= 16 && a < 23) encoder(a - 16, b - 64);
        else if (a == 7) knobMsb_ = b;
        else if (a == 39) s_.setMasterVolume(((knobMsb_ << 3) | (b >> 4)) / 1023.f * 1.2f);
    }
}
void SamplerApp::onSysex(const uint8_t* m, size_t n) {
    if (n < 9 || m[1] != 0x7d || m[2] != 'D' || m[3] != 'A' || m[4] != 'W') return;
    linkSeen_ = true;
    if (m[6] == 0x41 && n >= 12) {   // the firmware's own SEL screen / OCT buttons: its scale index is the tonality unless it is the OFF we keep it on
        if ((m[7] & 7) != 7) { scale_.scale = m[7] & 7; fwDirty_ = true; }
        scale_.root = m[8] % 12; scale_.octave = (int)m[9] - 3; scale_.mode = m[10] & 1;
    }
    else if (m[6] == 0x40 && n >= 30) {   // PONG: frames the FM-1 had to drop
        uint32_t d = 0; for (int i = 0; i < 5; i++) d = d << 7 | (m[24 + i] & 127);
        if (haveDropBase_ && d != dropped_) { gap_ = std::min<uint32_t>(250, gap_ * 16 / 10 + 1); lastLoss_ = nowMs_; needResend_ = true; pending_.clear(); pendingBytes_ = 0; }
        if (!haveDropBase_) { needClear_ = true; repaintAt_ = nowMs_ + 1500; }   // first contact: whatever the firmware drew (its splash) goes
        if (!haveDropBase_) fwDirty_ = true;
        dropped_ = d; haveDropBase_ = true;
    }
    else if (m[6] == 0x42) needClear_ = true;     // the firmware had its own screen up: repaint everything
}
void SamplerApp::button(int b, bool down) {
    if (b < OCT_DOWN || b > STOP) return; // ignore unknown vendor notes safely
    const uint32_t mask = 1u << b;
    const bool wasDown = !!(held_ & mask);
    if (wasDown == down) return; // duplicate MIDI Note On/Off must not toggle transport
    lastLed_ = ~0ull;
    if (down) held_ |= mask; else held_ &= ~mask;
    if (b == OCT_UP || b == OCT_DOWN) {
        if (b == OCT_UP && down) octUpAt_ = nowMs_;
        // The two Note On events can arrive in either order or in separate
        // USB MIDI callbacks. Show the RATE menu as soon as both are held.
        const uint32_t octBoth = (1u << OCT_UP) | (1u << OCT_DOWN);
        if (down && (held_ & octBoth) == octBoth && !cueSelect_) {
            cueReturnPage_ = page_;
            cueSelect_ = true;
            page_ = P_CUE;
        }
        if (b == OCT_UP && !down) {
            cueHold(false);
            if (cueSelect_) page_ = cueReturnPage_;
            cueSelect_ = false;
        }
        // Releasing only OCT- leaves the menu visible while OCT+ is held.
        // Neither modifier changes PLAY/STOP until PLAY is actually pressed.
        return;
    }
    if (b == PLAY || b == STOP) {
        if (!down) {
            // A held NOTE REPEAT is momentary. Neither releasing PLAY nor
            // releasing OCT can fall through into a second transport toggle.
            if (cueActive_) cueHold(false);
            playModified_ = false;
            return;
        }
        const bool repeat = !!(held_ & (1u << OCT_UP));
        const bool restart = !!(held_ & (1u << OCT_DOWN));
        playModified_ = repeat || restart; // latch what was held BEFORE PLAY/STOP
        if (repeat) {
            if (!recordingNow() && loopAny_ && st_.loopSamples > 0) {
                if (!cueSelect_) { cueReturnPage_ = page_; cueSelect_ = true; }
                page_ = P_CUE;
                cueHold(true); // BPM-synced, K1 live; ends on either release
            } else say("NO LOOP FOR NOTE REPEAT");
        } else if (restart) {
            restartLoop(); // same callback: no 1-second threshold
        } else if (b == PLAY) {
            transportPlay(); // normal play/stop immediately, without a modifier
        } else {
            transportSet(false); // optional external STOP, native FM-1 uses PLAY
        }
        return;
    }
    if (b == SAVE) {   // metronome never enters a sample save page
        if (down) page_ = metroSelected_ ? P_METRO_SETTINGS : P_SAVE;
        return;
    }
    if (!down) return;
    if (metroSelected_ && b != HOME && b != SEL) return;
    switch (b) {
    case HOME: goHome(); break;
    case FX: page_ = P_FX; break;
    case ENV: page_ = P_ENV; break;
    case LFO: page_ = P_LFO; break;
    case ARP: page_ = P_ARP; break;
    case SEQ: if (sequencerMode_) exitSequencer(); else enterSequencer(); break;
    case EDIT: page_ = P_MIX; break;
    case GLO: page_ = P_KEY; break;
    case SEL: page_ = metroSelected_ ? (page_ == P_METRO_TEMPO ? P_METRO_SETTINGS : page_ == P_METRO_SETTINGS ? P_METRO_CORRECT : P_METRO_TEMPO) : P_LEN; break;
    case REC: loopRecord(); break;
    default: break;
    }
}
void SamplerApp::encoder(int i, int delta) {
    if (!delta) return;
    if (i == 0) {
        if (sequencerMode_) selectSequencerTrack(seqTrack_ + (delta > 0 ? 1 : -1));
        else if (padCompose_) { for (int n = 1; n <= kTracks; ++n) { const int t = (track_ + (delta > 0 ? n : -n) + kTracks) % kTracks; if (padTrackWritable(t)) { selectTrack(t); break; } } }
        else if (metroSelected_) selectTrack(delta > 0 ? 0 : kTracks - 1);
        else if ((delta > 0 && track_ == kTracks - 1) || (delta < 0 && track_ == 0)) selectMetronome();
        else selectTrack(track_ + (delta > 0 ? 1 : -1));
        return;
    }
    if (i == 6) { browse(delta); return; }
    // In the open LIBRARY browser K1 navigates directory levels. Other
    // pages retain their existing K1 parameter and OCT+ / CUE behavior.
    if (i == 2 && libraryBrowserActive_) {
        if (delta > 0) browserEnter();
        else browserBack();
        return;
    }
    if (i == 1) {
        if (sequencerMode_) {
            setSequencerVolume(seqTrack_, sequencerVolume(seqTrack_) + delta * .01f);
            say("SEQ T" + std::to_string(seqTrack_ + 1) + " VOLUME " +
                std::to_string((int)std::lround(sequencerVolume(seqTrack_) * 100.f)) + "%");
        } else if (!metroSelected_) setAlgo(ts_[track_].algo + (delta > 0 ? 1 : -1));
        return;
    }
    // OCT+ makes K1 the CUE subdivision control BEFORE and DURING PLAY.
    // Crucially, microphone threshold or the underlying page never steals K1.
    if (i == 2 && !metroSelected_ && ((held_ & (1u << OCT_UP)) || cueActive_ || page_ == P_CUE)) {
        setCueDivision(cueDivision_ + (delta > 0 ? 1 : -1));
        touchedKnob_ = 0; touchedAt_ = nowMs_;
        return;
    }
    if (i == 2 && !metroSelected_ && mic_ == 1 && page_ != P_SAVE) { setMicThreshold01(micThr_ + (delta > 0 ? 0.03f : -0.03f)); touchedKnob_ = 0; touchedAt_ = nowMs_; return; }   // K1 = trigger level while the microphone listens
    Knobs k = knobs();
    const int idx = i - 2;
    if (idx < 4 && k[(size_t)idx].valid && k[(size_t)idx].step) { k[(size_t)idx].step(delta); touchedKnob_ = idx; touchedAt_ = nowMs_; }
}

// ---- tick ----
void SamplerApp::tick(uint32_t now) {
    nowMs_ = now;
    if (fwDirty_) { fwDirty_ = false; sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)scale_.mode}, true); }
    if (repaintAt_ && now >= repaintAt_) { repaintAt_ = repaintAt_ + 2500 < now + 2500 && repaintCount_++ < 1 ? now + 2500 : 0; needClear_ = true; }
    st_ = s_.status();
    // Clock/transport phase are captured from the audio thread, never
    // calculated from the UI ticker's wall clock. Send a superseding sync
    // packet at most ten times per second (SysEx control task is not an ISR).
    if (!lastMetroAt_ || now - lastMetroAt_ >= 100) {
        lastMetroAt_ = now;
        const int bpm10 = std::clamp((int)std::lround(st_.metroBpm * 10.f), 10, 16383);
        const int beat = std::clamp((int)std::floor(st_.metroPhase), 0, 63);
        const int frac = std::clamp((int)std::lround((st_.metroPhase - beat) * 16383.f), 0, 16383);
        sysex(7, {(uint8_t)(bpm10 >> 7), (uint8_t)(bpm10 & 127),
                   (uint8_t)std::clamp(st_.metroBeats, 1, 64),
                   (uint8_t)std::clamp((int)std::lround(st_.metroSwing * 100), 0, 100),
                   (uint8_t)std::clamp((int)std::lround(st_.metroVolume * 100), 0, 100),
                   (uint8_t)(!st_.playing ? 5 : std::clamp(st_.metroMode, 0, 5)),
                   (uint8_t)beat, (uint8_t)(frac >> 7), (uint8_t)(frac & 127),
                   (uint8_t)(st_.metroRecording ? 1 : 0)}, true, 1100);
    }
    Session::LoadedProject loadedProject;
    const bool loadedThisTick = s_.takeLoadedProject(loadedProject);
    if (loadedThisTick) {
        st_ = s_.status();
        applyLoadedProject(loadedProject);
        st_ = s_.status();
        if (loadedProject.slot == Session::kRecoverySlot) {
            recoveryRestoring_ = false; recoveryNew_ = false; recoveryBlocked_ = false;
            recoveryFingerprint_ = 0; recoveryLastAttempt_ = now;
        } else if (recoveryEnabled_) recoveryRequested_ = true;
    }
    // Initial free recording chooses its own measured BPM. Scene changes can
    // also change BPM. Keep the Tempo Flex knob on the audible transport value
    // rather than an obsolete 120 BPM; do not overwrite a freshly loaded v4
    // project's explicit saved setting before its audio command is applied.
    if (!loadedThisTick &&
        (observedLoopSamples_ != st_.loopSamples || observedScene_ != st_.activeScene)) {
        const float ratio = std::max(.25f, pitchedTempo_);
        if (std::isfinite(st_.bpm) && st_.bpm >= 30.f)
            tempoFlexBpm_ = cl(st_.bpm / ratio, 30.f, 300.f);
    }
    observedLoopSamples_ = st_.loopSamples;
    observedScene_ = st_.activeScene;
    const int projectState = s_.projectOperation();
    if (projectState != lastProjectOperation_) {
        lastProjectOperation_ = projectState;
        if (projectState == 2) {
            if (pendingProjectLabelSlot_ >= 0) {
                const std::string path = s_.dataDir() + "/project_" +
                    std::to_string(pendingProjectLabelSlot_ + 1) + ".name";
                std::ofstream out(path, std::ios::trunc);
                if (out) out << pendingProjectLabel_ << "\n";
                pendingProjectLabel_.clear();
                pendingProjectLabelSlot_ = -1;
            }
            say("FULL PROJECT SAVED");
        } else if (projectState == -1) {
            pendingProjectLabelSlot_ = -1; pendingProjectLabel_.clear();
            say("PROJECT SAVE FAILED");
        }
        else if (projectState == -2) {
            if (recoveryRestoring_) { recoveryRestoring_ = false; recoveryNew_ = false; recoveryBlocked_ = true; say("SESSION RECOVERY FAILED - SAVED FILE KEPT"); }
            else say("PROJECT LOAD FAILED");
        }
    }
    if (!scenesLoaded_ && st_.arrangementLoaded) {
        scenesLoaded_ = true;
        songOrder_.clear();
        for (int i = 0; i < st_.arrangementSize; ++i)
            if (st_.arrangement[i] >= 0 && st_.arrangement[i] < Session::kScenes)
                songOrder_.push_back(st_.arrangement[i]);
        songSelected_ = songOrder_.empty() ? -1 : 0;
    }
    // OCT+ held for two seconds opens the beat division selector without changing key octave.
    if ((held_ & (1u << OCT_UP)) && !cueSelect_ && now - octUpAt_ >= 2000U) {
        cueReturnPage_ = page_;
        cueSelect_ = true;
        page_ = P_CUE;
    }
    loopAny_ = false;
    for (int t = 0; t < kTracks; t++) loopAny_ |= st_.track_has_clip[t];
    if (now - lastLoss_ > 4000 && now - lastRaise_ > 2000 && gap_ > 16) { gap_ = std::max<uint32_t>(16, gap_ * 9 / 10); lastRaise_ = now; }
    if (now - lastPing_ >= 500) { lastPing_ = now; pingId_ = (pingId_ + 1) & 0x3fff; sysex(5, n14(pingId_)); }
    // audition: the cursor rested, load the entry on the selected track and play it
    // PRESETS loads the selected WAV synchronously into the sampler queue; no delayed audition.
    if (st_.sampleVersion != lastSampleVersion_) {   // a sample landed on a track: give it this track's settings
        lastSampleVersion_ = st_.sampleVersion;
        // A user choosing a different sample clears tuning at selection time.
        // Do not reset saved per-track tuning when archived WAVs are reloaded.
        for (int i = 0; i < kTracks; i++) if (hasSample(i)) applyAll(i);   // several samples can land between two ticks (start-up): every track gets its settings, not only the last
        if (previewAfterLoad_ && st_.sampleTrack == track_) { previewAfterLoad_ = false; previewNote_ = 60; previewTrack_ = track_; s_.noteOn(track_, 60, 100); previewOffAt_ = now + 900; }
    }
    if (st_.sampleFails != lastSampleFails_) { lastSampleFails_ = st_.sampleFails; say("COULD NOT LOAD THE FILE"); }
    if (previewNote_ >= 0 && now >= previewOffAt_) { s_.noteOff(previewTrack_, previewNote_); previewNote_ = -1; }
    if (commitAt_ && now >= commitAt_) {
        commitAt_ = 0;
        if (commitTrack_ == -2) refreshLibrary();
        else if (commitTrack_ >= 0) applyHome(commitTrack_, true);   // the knob rested: build the stretched / pitched copy
    }
    // microphone sample: wait for sound -> recording -> saved -> load into the track it was started on
    if (mic_ == 1 && st_.sampleState == 1) mic_ = 2;
    if (mic_ == 3 && st_.samplePending) {
        const std::string path = s_.lastSamplePath();
        s_.recorderDismiss();
        refreshLibrary();
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].path == path) { const int keep = track_; track_ = micTrack_; loadEntry((int)i); track_ = keep; break; }
        mic_ = 0; say("SAMPLE LOADED");
    }
    if (mic_ == 3 && st_.sampleState == 0 && !st_.samplePending && now - lastDraw_ > 3000) mic_ = 0;   // too short: nothing was kept
    if (now - lastDraw_ >= 33) {
        lastDraw_ = now;
        if (now - healAt_ >= 150 && pending_.size() < 4) { healAt_ = now; if (!needClear_) heal(); }
        draw(now);
        leds();
    }
    tickRecovery(now);
    flush(now);
}

// ---- FM-1 link ----
void SamplerApp::sysex(int cmd, const std::vector<uint8_t>& p, bool urgent, int key) {
    std::vector<uint8_t> m = {0xf0, 0x7d, 'D', 'A', 'W', 1, (uint8_t)cmd};
    cat(m, p); m.push_back(0xf7);
    if (cmd == 2) { pending_.clear(); pendingBytes_ = 0; }
    if (key >= 0) for (auto& f : pending_) if (f.key == key) { pendingBytes_ += m.size() - f.bytes.size(); f.bytes = std::move(m); return; }
    pendingBytes_ += m.size();
    if (urgent) pending_.push_front({key, std::move(m)}); else pending_.push_back({key, std::move(m)});
}
void SamplerApp::clear(uint16_t c) { sysex(2, col(c)); }
void SamplerApp::rect(int id, int x, int y, int w, int h, uint16_t c) {
    uint64_t sig = (uint64_t)c | (uint64_t)(w & 0xfff) << 16 | (uint64_t)(h & 0xfff) << 28 | (uint64_t)(x & 0xfff) << 40 | (uint64_t)(y & 0xfff) << 52;
    if (!needClear_ && rects_[id] == sig) return;
    rects_[id] = sig;
    std::vector<uint8_t> p; cat(p, n14(x)); cat(p, n14(y)); cat(p, n14(std::max(1, w))); cat(p, n14(std::max(1, h))); cat(p, col(c));
    sysex(3, p, false, id);
}
void SamplerApp::drawText(int id, int x, int y, int scale, uint16_t fg, uint16_t bg, const std::string& text, int width) {
    std::string t = pad(text, (size_t)width);
    SurfaceCell& c = cells_[id];
    if (!needClear_ && c.known && c.text == t && c.fg == fg && c.bg == bg) return;
    c.text = t; c.fg = fg; c.bg = bg; c.known = true;
    std::vector<uint8_t> p; cat(p, n14(x)); cat(p, n14(y)); p.push_back((uint8_t)scale); cat(p, col(fg)); cat(p, col(bg));
    for (char ch : t) p.push_back((uint8_t)(ch & 127));
    sysex(4, p, false, 1000 + id);
}
void SamplerApp::flush(uint32_t now) {
    // ONE frame per call, at least gap_ ms apart: the FM-1 has a 4-frame queue. Small commands are packed into one batch frame.
    if (pending_.empty() || now - lastSend_ < gap_) return;
    lastSend_ = now;
    std::vector<uint8_t> batch = {0xf0, 0x7d, 'D', 'A', 'W', 1, 10};
    int n = 0;
    while (!pending_.empty()) {
        const auto& f = pending_.front().bytes;
        const size_t sub = f.size() - 7;
        if (n > 0 && (batch.size() + 2 + sub + 1 > 290 || sub > 127)) break;
        if (n == 0 && sub > 127) break;
        batch.push_back((uint8_t)(sub >> 7 & 127)); batch.push_back((uint8_t)(sub & 127));
        batch.insert(batch.end(), f.begin() + 6, f.end() - 1);
        pendingBytes_ -= f.size(); pending_.pop_front(); n++;
    }
    if (n == 0) { auto& f = pending_.front().bytes; pendingBytes_ -= f.size(); send_(f.data(), f.size()); pending_.pop_front(); return; }
    batch.push_back(0xf7);
    send_(batch.data(), batch.size());
}
void SamplerApp::heal() {   // a lost frame leaves a hole nothing would redraw: one element is re-sent in the background on every pass
    healCursor_ = (healCursor_ + 1) % (int)cells_.size();
    cells_[(size_t)healCursor_].known = false; rects_[(size_t)healCursor_] = ~0ull;
    const int c2 = 100 + healCursor_ % 160; rects_[(size_t)c2] = ~0ull;
}

void SamplerApp::leds() {
    uint64_t m = 0;
    const bool blinkFast = (nowMs_ / 250) & 1, blinkSlow = (nowMs_ / 500) & 1;
    static const int pageBtn[] = {HOME, FX, ENV, LFO, ARP, SEQ, EDIT, GLO, SAVE, SEL, SEQ, SEQ, SEQ, SEL, SEL};
    m |= 1ull << pageBtn[std::max(0, std::min((int)page_, (int)kPages - 1))];
    if (sequencerMode_) {
        static constexpr int whites[] = {53,55,57,59,60,62,64,65,67,69,71,72,74,76,77,79};
        static constexpr int blacks[] = {54,56,58,61,63,66,68,70,73,75,78};
        for (int k = 0; k < 16; ++k) {
            const int step = seqPage_ * 16 + k;
            const bool hit = step < seqBeats_ * 4 && seqGrid_[(size_t)seqTrack_][(size_t)step].velocity;
            const bool cursor = st_.playing && step == st_.seqStep;
            if (hit || (cursor && blinkFast)) m |= 1ull << (14 + whites[k] - 53);
        }
        for (int k = 0; k < 8; ++k)
            if (k == seqTrack_) m |= 1ull << (14 + blacks[k] - 53);
    } else {
    if (st_.playing) m |= 1ull << PLAY;
    if (st_.freeRecording || (!metroSelected_ && st_.track_clip_rec[track_]) || mic_ == 2) { if (blinkFast) m |= 1ull << REC; }
    else if (st_.freeArmed || mic_ == 1) { if (blinkSlow) m |= 1ull << REC; }
    else if (loopAny_) m |= 1ull << REC;
    // keys follow the tonality: the keyboard stays chromatic, only the keys that belong to the scale light, the root of each octave blinks slowly;
    // with the scale OFF only the root keys light. A sounding key goes dark.
    for (int k = 0; k < 27; k++) {
        const int raw = 53 + k, note = raw + 12 * scale_.octave, pc = ((note - scale_.root) % 12 + 12) % 12;
        bool on = ts_[track_].algo == 3 ? true : scale_.scale < 7 ? scaleIndex(pc, scale_.scale) >= 0 : pc == 0;   // KIT: every key is a pad
        if (on && pc == 0 && scale_.scale < 7 && ts_[track_].algo != 3) on = blinkSlow;
        if (keyCount_[raw]) on = false;
        if (note < 0 || note > 127) on = false;
        if (on) m |= 1ull << (14 + k);
    }
    }
    if (m == lastLed_ && nowMs_ - ledAt_ < 400) return;
    lastLed_ = m; ledAt_ = nowMs_;
    std::vector<uint8_t> p(6);
    for (int i = 0; i < 6; i++) p[i] = (uint8_t)(m >> (7 * i) & 127);
    sysex(1, p, true, 900);   // keyed: a newer state replaces an older one still waiting in the queue
}

// ---- screen ----
// Layout (240x240): track tags, page and state, sample name, a 60 px picture that depends on the page, the loop strip, four knobs, a hint.
void SamplerApp::draw(uint32_t now) {
    static int lastLight = -1; if ((int)theme::light != lastLight) { lastLight = theme::light; setPalette(theme::light); needClear_ = true; }
    static int lastHero = -1;
    const int heroKind = mic_ == 1 || mic_ == 2 ? 6 : page_ == P_ENV ? 1 : page_ == P_LFO ? 2 : page_ == P_MIX ? 3 : page_ == P_KEY ? 4 : page_ == P_LEN ? 5 : page_ == P_CUE ? 7 : page_ == P_STEP ? 8 : 0;
    if (heroKind != lastHero) { lastHero = heroKind; needClear_ = true; }
    if (needClear_) { fwDirty_ = true; clear(BLACK); std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); needResend_ = false; }
    else if (needResend_) { needResend_ = false; std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); }
    char b[96];
    const Knobs k = knobs();
    // header: the track number and the sample name, the transport state as the same pictogram as on the phone
    const bool recNow = !metroSelected_ && (st_.freeRecording || st_.track_clip_rec[track_]);
    {
        const std::string lab = metroSelected_ ? "METRONOME" : sequencerMode_
            ? seqSourceNames_[(size_t)seqTrack_] : s_.trackLabel(track_);
        drawText(0, 12, 10, 3, ACCENT, BLACK,
                 metroSelected_ ? "M" : std::to_string(sequencerMode_ ? seqTrack_ + 1 : track_ + 1), 2);
        drawText(1, 36, 10, 3, lab.empty() ? DIM : PAPER, BLACK, lab.empty() ? "EMPTY" : lab, 8);
        const int stt = recNow ? 2 : st_.playing ? 1 : 0;
        static int lastSt = -1; if (stt != lastSt || needClear_) { lastSt = stt; rects_[26] = ~0ull; for (int i = 0; i < 16; i++) rects_[300 + i] = ~0ull; }
        rect(26, 212, 12, 16, 16, BLACK);
        for (int i = 0; i < 16; i++) {
            int w = 16, x = 212; uint16_t c = PAPER;
            if (stt == 1) { w = std::max(1, (int)std::lround(16 * (1 - std::fabs(i - 7.5f) / 8))); c = ACCENT; }
            else if (stt == 2) { const float d = (i - 7.5f) / 8; w = std::max(1, (int)std::lround(16 * std::sqrt(std::max(0.f, 1 - d * d)))); x = 212 + (16 - w) / 2; c = ACCENT; }
            rect(300 + i, x, 12 + i, w, 1, c);
        }
    }
    drawText(20, 12, 40, 2, ACCENT, BLACK, kPage[page_], 9);
    for (int i = 0; i < kTracks; i++)
        rect(8 + i, 127 + (i % 8) * 13, 44 + (i / 8) * 6, 10, 4,
             i == (sequencerMode_ ? seqTrack_ : track_) && !metroSelected_ ? ACCENT : BAR);
    hero(now, k);
    // the loop: a thin bar with the position, under it the state (mic / toast / audition, else the sample mode and the loop)
    std::string line;
    if (mic_ == 1) line = "MIC: WAITING FOR SOUND";
    else if (mic_ == 2) line = fmt("MIC: RECORDING %.1fS", st_.sampleSeconds);
    else if (cueActive_) line = std::string("NOTE REPEAT ") + cueRateName(cueDivision_) + "  REPEATING";
    else if (cueSelect_) line = "NOTE REPEAT: K1 RATE, HOLD PLAY";
    else if (!toast().empty()) line = toast();
    else if (browserTouchedAt_ && now - browserTouchedAt_ < 4000) {
        const BrowserItem* item = browserSelected();
        line = item ? std::string(item->directory ? "[DIR] " : item->entry >= 0 ? "[WAV] " : "[FILE] ") + item->label
                    : "FM1-SAMPLER /";
    }
    else if (metroSelected_)
        line = std::to_string(metroBeats_) + "/4  CLICK " + std::to_string(metroMode_) + "  SEL: NEXT PAGE";
    else if (sequencerMode_)
        line = fmt("SEQ %.0f BPM  ", st_.bpm * tempoCorrection_) + std::to_string(seqBeats_) +
               "/4  PAGE " + std::to_string(seqPage_ + 1);
    else line = std::string(kAlgo[ts_[track_].algo]) + "  " + loopText();
    const bool browserNotice = browserTouchedAt_ && now - browserTouchedAt_ < 4000;
    const bool hot = cueActive_ || cueSelect_ || !toast().empty() || browserNotice || mic_ || st_.freeRecording || recNow || st_.freeArmed;
    const bool hasLoopBar = (loopAny_ && st_.loopSamples > 0) || sequencerMode_;
    const int px = sequencerMode_
        ? (int)(st_.seqStep * 216.f / std::max(4, seqBeats_ * 4))
        : (int)(st_.track_has_clip[track_] ? st_.clipPos[track_] * 216
              : (hasLoopBar ? st_.pos / st_.loopSamples * 216 : 0));
    static int lastPx = -1; if (px != lastPx) { lastPx = px; rects_[24] = ~0ull; }   // the bar is redrawn under the growing fill
    rect(24, 12, 102, 216, 3, BAR);
    rects_[25] = ~0ull;
    if (hasLoopBar || recNow) rect(25, 12, 102, std::max(1, px), 3, ACCENT);
    drawText(23, 12, 110, 1, hot ? ACCENT : DIM, BLACK, line, 38);
    // knobs: 2 x 2: label, value, bar
    for (int i = 0; i < 4; i++) {
        const int cx = 12 + (i % 2) * 114, cy = 124 + (i / 2) * 48;
        const Knob& p = k[(size_t)i];
        if (!p.valid) { drawText(40 + i * 2, cx, cy, 1, DIM, BLACK, "", 17); drawText(41 + i * 2, cx, cy + 10, 3, PAPER, BLACK, "", 6); rect(60 + i, cx, cy + 38, 102, 3, BLACK); rects_[70 + i] = ~0ull; rect(70 + i, cx, cy + 38, 1, 3, BLACK); continue; }
        snprintf(b, sizeof b, "K%d %s", i + 1, p.name.c_str());
        drawText(40 + i * 2, cx, cy, 1, i == touchedKnob_ && now - touchedAt_ < 1000 ? ACCENT : DIM, BLACK, b, 17);
        const bool big = p.value.size() <= 6;   // up to 6 characters at scale 3, longer values at scale 2
        const uint16_t vc = p.isChoice ? ACCENT : PAPER;
        static int lastBig[4] = {-1, -1, -1, -1};
        if (lastBig[i] != (int)big) { lastBig[i] = big; rects_[350 + i] = ~0ull; cells_[(size_t)(41 + i * 2)].known = false; }   // the size changed: wipe the old digits
        rect(350 + i, cx, cy + 10, 108, 24, BLACK);
        if (big) drawText(41 + i * 2, cx, cy + 10, 3, vc, BLACK, p.value, 6); else drawText(41 + i * 2, cx, cy + 14, 2, vc, BLACK, p.value, 9);
        const int bw = std::max(1, (int)(cl(p.norm) * 102));
        static int lastBw[4] = {-1, -1, -1, -1};
        if (bw < lastBw[i] || needClear_) rects_[60 + i] = ~0ull;   // the bar shrank: paint the track again under it
        lastBw[i] = bw;
        rect(60 + i, cx, cy + 38, 102, 3, BAR);
        rects_[70 + i] = ~0ull;
        rect(70 + i, cx, cy + 38, bw, 3, ACCENT);
    }
    const char* hint = metroSelected_ ? (page_ == P_METRO_CORRECT ? "K1: BPM  K4: DECIMALS  TAP: PHONE" : "SELECT: METRONOME PAGE  K1..K4: EDIT") : sequencerMode_ ? "16 WHITE: STEPS  8 BLACK: TRACKS  K1: PAGE" : mic_ == 1 ? (page_ == P_SAVE ? "K4 = TRIGGER LEVEL  LINE = THRESHOLD" : "K1 = TRIGGER LEVEL  LINE = THRESHOLD") : mic_ == 2 ? "RECORDING  K1 ON LOOP PAGE STOPS"  : page_ == P_CUE ? "OCT+ + OCT- MENU   K1 RATE / TRIPLETS" : page_ == P_LEN ? "TURN K1: X2 OR /2  THE LOOP REPEATS" : page_ == P_SAVE ? "SAVE AND CLEAR LOOPS: PROJECT MENU ON THE PHONE" : page_ == P_KEY ? "KEYS FOLLOW THE KEY  CHANGE ANY TIME"
                     : "SEL TRACK  PRE BROWSE  ALG ALGORITHM";
    drawText(90, 12, 228, 1, DIM, BLACK, hint, 38);
    needClear_ = false;
}

void SamplerApp::hero(uint32_t now, const Knobs& k) {
    const int x0 = 12, y0 = 54, W = 216, H = 46;
    // one column: background (cleared only when the height changed) + bar
    static uint32_t sig[100]; if (needClear_ || needResend_) for (auto& s : sig) s = ~0u;
    auto column = [&](int idx, int x, int w, int h, uint16_t fg, bool fromBottom) {
        h = std::min(H, std::max(1, h));
        const uint32_t s = (uint32_t)h | (uint32_t)fg << 8 | (uint32_t)w << 24;
        if (sig[idx] == s && !needClear_) return;
        sig[idx] = s;
        rects_[100 + idx] = ~0ull; rects_[200 + idx] = ~0ull;
        rect(100 + idx, x, y0, w, H, BLACK);
        rect(200 + idx, x, fromBottom ? y0 + H - h : y0 + (H - h) / 2, w, h, fg);
    };
    const TrackState& x = ts_[track_];
    if (mic_ == 1 || mic_ == 2) {   // microphone: input level against the trigger line (K1 sets it), then the growing take
        const int n = 54; const float thr = std::min(1.f, micThreshold() * 2.f), lv = std::min(1.f, st_.inputPeak * 2.f);
        if (mic_ == 1) {
            for (int i = 0; i < n; i++) column(i, x0 + i * 4, 3, (int)(lv * H * (i < (int)(lv * n) ? 1.f : 0.f)) + 1, lv > thr ? ACCENT : PAPER, false);
            column(57, x0 + (int)(thr * (n - 1)) * 4, 3, H, ACCENT, true);
        } else {
            float live[2048]; const int ln = s_.livePeaks(live, 2048), per = std::max(1, (ln + n - 1) / n);
            for (int i = 0; i < n; i++) { float v = 0; for (int j = i * per; j < std::min(ln, (i + 1) * per); j++) v = std::max(v, live[j]); column(i, x0 + i * 4, 3, (int)(std::min(1.f, v * 1.1f) * H) + 1, i * per < ln ? PAPER : BAR, false); }
        }
    } else if (page_ == P_STEP) {
        for (int i = 0; i < 16; ++i) {
            const int step = seqPage_ * 16 + i;
            const bool valid = step < seqBeats_ * 4;
            const bool filled = valid && seqGrid_[(size_t)seqTrack_][(size_t)step].velocity != 0;
            const bool cursor = valid && st_.playing && st_.seqStep == step;
            column(i, x0 + i * 13, 11, filled ? 37 : (cursor ? 26 : 7),
                   cursor ? ACCENT : filled ? PAPER : BAR, true);
        }
    } else if (page_ == P_CUE) {
        drawText(100, x0, y0 + 8, 3, ACCENT, BLACK, cueRateName(cueDivision_), 9);
        drawText(101, x0, y0 + 32, 2, PAPER, BLACK, cueActive_ ? "NOTE REPEAT" : "OCT+ + HOLD PLAY", 18);
    } else if (page_ == P_LEN) {   // the loop length in big letters
        char b[32];
        if (st_.loopSamples > 0) snprintf(b, sizeof b, "%.0f BARS", (double)st_.loopBars); else snprintf(b, sizeof b, "NO LOOP");
        drawText(100, x0, y0 + 8, 3, ACCENT, BLACK, b, 9);
        snprintf(b, sizeof b, "%.1fS  %.0f BPM", st_.loopSamples / (double)SAMPLERATE, st_.bpm * tempoCorrection_);
        drawText(101, x0, y0 + 32, 2, PAPER, BLACK, st_.loopSamples > 0 ? b : "REC TO MAKE ONE", 18);
    } else if (page_ == P_ENV) {   // the ADSR curve
        const float sa = 0.08f + 0.5f * x.a, sd = 0.08f + 0.5f * x.d, ss = 0.3f, sr = 0.08f + 0.5f * x.r, tot = sa + sd + ss + sr;
        for (int i = 0; i < 54; i++) {
            const float u = (i + 0.5f) / 54.f * tot; float v;
            if (u < sa) v = u / sa; else if (u < sa + sd) v = 1.f - (1.f - x.s) * (u - sa) / sd; else if (u < sa + sd + ss) v = x.s; else v = x.s * (1.f - (u - sa - sd - ss) / sr);
            column(i, x0 + i * 4, 3, (int)(v * H), PAPER, true);
        }
    } else if (page_ == P_LFO) {   // one and a half cycles of the shape, amplitude = depth; the bar on the right is the live value
        for (int i = 0; i < 52; i++) {
            const float ph = std::fmod(i / 52.f * 1.5f * 2.f, 1.f); float v;
            v = x.lfoShape == 0 ? std::sin(6.2831853f * ph) : x.lfoShape == 1 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : x.lfoShape == 2 ? (ph < 0.5f ? 1.f : -1.f) : std::sin(ph * 71.f + (int)(i / 9) * 5.1f);
            column(i, x0 + i * 4, 3, (int)((0.08f + 0.92f * x.lfoDepth * std::fabs(v)) * H), PAPER, false);
        }
        column(56, x0 + 212, 4, (int)((0.5f + 0.5f * st_.lfoValue[track_]) * H), ACCENT, true);
    } else if (page_ == P_MIX) {   // sixteen strips, two columns of eight
        for (int i = 0; i < kTracks; i++) column(i, x0 + i * 13, 9, (int)(std::min(1.f, st_.track_peak[i] * 1.5f) * H), i == track_ ? ACCENT : PAPER, true);
    } else if (page_ == P_KEY) {   // the tonality in big letters
        char b[32]; snprintf(b, sizeof b, "%s %s", kNote[scale_.root], kScaleName[scale_.scale]);
        drawText(100, x0, y0 + 8, 3, ACCENT, BLACK, b, 9);
        snprintf(b, sizeof b, "OCT %+d  %s", scale_.octave, scale_.mode ? "CHORD" : "KEY");
        drawText(101, x0, y0 + 32, 2, PAPER, BLACK, b, 18);
    } else {   // the waveform with the playback window
        auto pk = peaks(track_, 54);
        for (int i = 0; i < 54; i++) {
            const float pos = (i + 0.5f) / 54.f; const bool in = pos >= x.start && pos <= x.end;
            const float v = i < (int)pk.size() ? pk[(size_t)i] : 0.f;
            column(i, x0 + i * 4, 3, (int)(std::min(1.f, v * 1.1f) * H) + 1, in ? PAPER : BAR, false);
        }
    }
    (void)k; (void)now;
}
