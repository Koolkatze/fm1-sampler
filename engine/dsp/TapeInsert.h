// Per-track "cassette": saturation, tone, wow and flutter, hiss, wet/dry mix, and a bit crusher after it. All parameters 0..1.
#pragma once
#include "Common.h"
#include "Filter.h"
#include <vector>

class TapeInsert {
public:
    // Lightweight parameter snapshot for offline loop bounces: do NOT copy
    // the 4096-frame delay buffer on the real-time audio callback.
    struct Settings {
        float sat = 0.f, tone = 1.f, wobble = 0.f, hiss = 0.f;
        float mix = 0.f, bits = 1.f;
    };
    explicit TapeInsert(int sr) : sr_(sr), buf_(4096 * 2, 0.f) { setParams(0, 1, 0, 0, 0, 1); }
    Settings settings() const { return settings_; }
    // sat: drive; tone: 1 = bright; wobble: wow and flutter depth; hiss: noise; mix: wet share of the cassette; bits: 1 = 16 bit (clean), 0 = 4 bit
    void setParams(float sat, float tone, float wobble, float hiss, float mix, float bits) {
        settings_ = {clampf(sat, 0.f, 1.f), clampf(tone, 0.f, 1.f),
                     clampf(wobble, 0.f, 1.f), clampf(hiss, 0.f, 1.f),
                     clampf(mix, 0.f, 1.f), clampf(bits, 0.f, 1.f)};
        sat_ = settings_.sat; wobble_ = settings_.wobble;
        hiss_ = settings_.hiss; mix_ = settings_.mix;
        bits_ = 4.f + 12.f * settings_.bits;
        tone_.set(800.f * std::pow(25.f, settings_.tone), (float)sr_);
    }
    bool active() const { return mix_ > 0.001f || bits_ < 15.9f; }
    void process(float* x /*stereo, in place*/, int n) {
        if (!active()) return;
        const float drive = 1.f + sat_ * 8.f, norm = 1.f / std::tanh(drive);
        const float q = bits_ < 15.9f ? std::pow(2.f, 1.f - bits_) : 0.f;
        for (int i = 0; i < n; i++) {
            wow_ += 0.7f / sr_; if (wow_ >= 1.f) wow_ -= 1.f;
            flut_ += 6.3f / sr_; if (flut_ >= 1.f) flut_ -= 1.f;
            const float mod = wobble_ * (std::sin(2.f * kPi * wow_) * 40.f + std::sin(2.f * kPi * flut_) * 4.f);   // samples of delay
            for (int c = 0; c < 2; c++) {
                const float dry = x[2 * i + c];
                buf_[(size_t)(wr_ * 2 + c)] = dry;
                float wet = dry;
                if (wobble_ > 0.001f) {
                    double rp = (double)wr_ - (64.0 + mod); while (rp < 0) rp += 4096.0;
                    const int i0 = (int)rp % 4096, i1 = (i0 + 1) % 4096; const float fr = (float)(rp - std::floor(rp));
                    wet = buf_[(size_t)(i0 * 2 + c)] + (buf_[(size_t)(i1 * 2 + c)] - buf_[(size_t)(i0 * 2 + c)]) * fr;
                }
                wet = std::tanh(wet * drive) * norm;
                wet = tone_.process(c, wet);
                seed_ = seed_ * 1664525u + 1013904223u;
                wet += ((int32_t)seed_ / 2147483648.f) * hiss_ * 0.05f;
                float y = dry + (wet - dry) * mix_;
                if (q > 0.f) y = std::floor(y / q + 0.5f) * q;
                x[2 * i + c] = y;
            }
            if (++wr_ >= 4096) wr_ = 0;
        }
    }
private:
    Settings settings_{};
    int sr_; std::vector<float> buf_; int wr_ = 0;
    float sat_ = 0, wobble_ = 0, hiss_ = 0, mix_ = 0, bits_ = 16, wow_ = 0, flut_ = 0; uint32_t seed_ = 12345;
    OnePole tone_;
};
