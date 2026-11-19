#pragma once
// Spreekbuis DSP building blocks: filters, sample voices, grains, whisperizer, vowel chart, g generator.
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

namespace sb {

constexpr float kPi = 3.14159265358979f;

struct Rng {
    uint32_t s = 0x9e3779b9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f; } // 0..1
    float bi() { return next() * 2.0f - 1.0f; }
};

// TPT state-variable filter (Zavalishin), band/low/high outputs
struct Svf {
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set(float freq, float q, float sr) {
        freq = std::clamp(freq, 20.0f, sr * 0.45f);
        g = std::tan(kPi * freq / sr); k = 1.0f / std::max(0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns band; low/high via refs
    float process(float x, float& lp, float& hp) {
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; hp = x - k * v1 - v2;
        return v1;
    }
    float bp(float x) { float l, h; return process(x, l, h); }
    float lp(float x) { float l, h; process(x, l, h); return l; }
    float hp(float x) { float l, h; process(x, l, h); return h; }
};

inline float readS16(const std::vector<int16_t>& d, double pos) {
    if (pos < 0) return 0;
    size_t i = (size_t) pos;
    if (i + 1 >= d.size()) return i < d.size() ? d[i] / 32768.0f : 0.0f;
    float f = (float) (pos - (double) i);
    return (d[i] + (d[i + 1] - d[i]) * f) / 32768.0f;
}

// One-shot sample voice (word / sentence playback)
struct OneShot {
    const std::vector<int16_t>* data = nullptr;
    double pos = 0, inc = 1;
    int item = -1;
    float gain = 1, pan = 0, fade = 1;
    bool active = false;
    void start(const std::vector<int16_t>* d, int it, double increment, float g = 1, float p = 0) {
        data = d; item = it; pos = 0; inc = increment; gain = g; pan = p; active = d && !d->empty(); fade = 1;
    }
    float tick() {
        if (!active) return 0;
        float v = readS16(*data, pos) * gain;
        pos += inc;
        if (pos >= (double) data->size()) active = false;
        return v;
    }
};

// Granular cloud grain
struct Grain {
    const std::vector<int16_t>* data = nullptr;
    double pos = 0, inc = 1;
    int len = 0, age = 0;
    float panL = 0.7f, panR = 0.7f, gain = 1;
    bool active = false, whisper = false;
    float tick() {
        if (!active) return 0;
        float ph = (float) age / (float) len;
        float w = 0.5f - 0.5f * std::cos(2 * kPi * ph);
        float v = readS16(*data, pos) * w * gain;
        pos += inc; ++age;
        if (age >= len || pos < 0 || pos >= (double) data->size()) active = false;
        return v;
    }
};

// Noise vocoder: turns any voice into a whisper (band envelopes applied to white noise)
struct Whisperizer {
    static constexpr int N = 12;
    std::array<Svf, N> an, sy;
    std::array<float, N> env {};
    float atk = 0, rel = 0;
    Svf hp;
    Rng rng;
    void prepare(float sr) {
        for (int i = 0; i < N; ++i) {
            float f = 180.0f * std::pow(7000.0f / 180.0f, (float) i / (N - 1));
            an[(size_t) i].set(f, 4.0f, sr); sy[(size_t) i].set(f, 4.0f, sr);
            an[(size_t) i].reset(); sy[(size_t) i].reset(); env[(size_t) i] = 0;
        }
        atk = 1 - std::exp(-1.0f / (0.002f * sr)); rel = 1 - std::exp(-1.0f / (0.03f * sr));
        hp.set(300, 0.7f, sr);
    }
    float process(float x) {
        float out = 0;
        for (int i = 0; i < N; ++i) {
            float b = std::abs(an[(size_t) i].bp(x));
            float& e = env[(size_t) i];
            e += (b > e ? atk : rel) * (b - e);
            out += sy[(size_t) i].bp(rng.bi()) * e * (1.0f + i * 0.25f);
        }
        return hp.hp(out * 2.5f);
    }
};

// The Dutch vowel chart: x = front(0)..back(1), y = close(0)..open(1); formants for an adult voice
struct Vowel { const char* ipa; float x, y, f1, f2, f3; };
inline const std::array<Vowel, 13>& vowelChart() {
    static const std::array<Vowel, 13> v { {
        { "i", 0.00f, 0.00f, 294, 2208, 2766 }, { "y", 0.30f, 0.00f, 305, 1730, 2208 }, { "u", 1.00f, 0.00f, 339, 810, 2200 },
        { "\xc9\xaa", 0.15f, 0.25f, 388, 2003, 2571 }, { "\xca\x8f", 0.40f, 0.25f, 410, 1625, 2150 },
        { "e", 0.05f, 0.38f, 407, 2017, 2553 }, { "\xc3\xb8", 0.35f, 0.38f, 443, 1497, 2260 }, { "o", 0.95f, 0.38f, 487, 911, 2450 },
        { "\xc9\x99", 0.50f, 0.52f, 500, 1500, 2500 }, { "\xc9\x94", 0.90f, 0.62f, 523, 866, 2550 },
        { "\xc9\x9b", 0.15f, 0.66f, 583, 1725, 2471 }, { "a", 0.35f, 1.00f, 795, 1301, 2565 }, { "\xc9\x91", 0.80f, 1.00f, 679, 1051, 2619 },
    } };
    return v;
}
// diphthong glide paths (start x,y -> end x,y): ɛi, œy, ʌu
struct Glide { const char* name; float x0, y0, x1, y1; };
inline const std::array<Glide, 3>& glides() {
    static const std::array<Glide, 3> g { { { "\xc9\x9bi", 0.15f, 0.66f, 0.02f, 0.05f }, { "\xc5\x93y", 0.40f, 0.62f, 0.30f, 0.02f },
                                            { "\xca\x8cu", 0.72f, 0.80f, 1.00f, 0.02f } } };
    return g;
}
inline void formantsAt(float x, float y, float& f1, float& f2, float& f3) {
    float w = 0, a = 0, b = 0, c = 0;
    for (auto& v : vowelChart()) {
        float d = (v.x - x) * (v.x - x) + (v.y - y) * (v.y - y);
        float k = 1.0f / (d * d + 1e-5f);
        w += k; a += k * v.f1; b += k * v.f2; c += k * v.f3;
    }
    f1 = a / w; f2 = b / w; f3 = c / w;
}

inline float polyBlep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

// Vowel pad: saw/noise (or input) through three formant band-passes
struct VowelVoice {
    Svf b1, b2, b3;
    float phase = 0, freq = 110, env = 0, gate = 0, sr = 48000;
    float cf1 = 500, cf2 = 1500, cf3 = 2500;
    Rng rng;
    void prepare(float s) { sr = s; b1.reset(); b2.reset(); b3.reset(); env = 0; }
    float osc(float noiseMix) {
        float dt = freq / sr;
        phase += dt; if (phase >= 1) phase -= 1;
        float saw = 2 * phase - 1 - polyBlep(phase, dt);
        return saw * (1 - noiseMix) + rng.bi() * noiseMix * 0.8f;
    }
    // x: source sample; formants already smoothed by caller
    float filter(float x, float f1, float f2, float f3, float q) {
        b1.set(f1, 6.0f * q, sr); b2.set(f2, 9.0f * q, sr); b3.set(f3, 12.0f * q, sr);
        return b1.bp(x) * 1.0f + b2.bp(x) * 0.7f + b3.bp(x) * 0.4f;
    }
};

// The Dutch g / ch: fricative noise, hard Holland /x/ <-> soft Flemish /ɣ/ (voiced)
struct GVoice {
    Svf r1, r2, lpv, raspLp;
    Rng rng;
    float sr = 48000, env = 0, gate = 0, freq = 262, phase = 0, raspPh = 0;
    void prepare(float s) { sr = s; r1.reset(); r2.reset(); lpv.reset(); env = 0; raspLp.set(40, 0.7f, s); }
    float tick(float soft, float rasp, float voicing, float harm, float relCoef) {
        float target = gate;
        env += (target > env ? 0.02f : relCoef) * (target - env);
        if (env < 1e-5f && gate == 0) return 0;
        float centre = std::clamp(freq * harm * (1.0f - 0.35f * soft), 300.0f, 9000.0f);
        float q = 7.0f - 5.0f * soft;
        r1.set(centre, q, sr); r2.set(centre * 1.62f, q * 0.8f, sr);
        float n = rng.bi();
        // uvular scrape: slow random amplitude flutter (~30 Hz)
        float fl = raspLp.lp(rng.bi() * 6.0f);
        float scrape = 1.0f + rasp * (0.8f * fl);
        float fric = (r1.bp(n) + 0.6f * r2.bp(n)) * scrape;
        // voicing (soft g): pulse at note pitch, low-passed
        float dt = freq / sr; phase += dt; if (phase >= 1) phase -= 1;
        float pulse = (phase < 0.12f ? 1.0f : -0.14f);
        lpv.set(900, 0.8f, sr);
        float voiced = lpv.lp(pulse) * voicing * soft;
        return (fric * (1.2f - 0.5f * soft) + voiced * 0.6f) * env;
    }
};

} // namespace sb
