// Spraakmaker — channel vocoder (8–20 bands) and a small polyphonic carrier oscillator.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace sm {

struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass (float f, float q, float sr) {
        f = std::clamp (f, 20.f, sr * 0.45f);
        const float w = 6.2831853f * f / sr, al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos (w) / a0; a2 = (1 - al) / a0;
    }
    void highpass (float f, float q, float sr) {
        const float w = 6.2831853f * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

class Vocoder {
public:
    void prepare (float sampleRate) { sr = sampleRate; configure (16, 1.f, 0.5f, true); hp.highpass (4500.f, 0.7f, sr); }
    void configure (int nBands, float shift, float tilt, bool force = false)
    {
        nBands = std::clamp (nBands, 4, 24);
        if (! force && nBands == bands && std::fabs (shift - curShift) < 1e-3f && std::fabs (tilt - curTilt) < 1e-3f) return;
        bands = nBands; curShift = shift; curTilt = tilt;
        mod.resize ((size_t) bands * 2); car.resize ((size_t) bands * 2); env.assign ((size_t) bands, 0.f); gain.resize ((size_t) bands);
        const float lo = 110.f, hi = 7600.f;
        const float q = 1.2f * (float) bands / 8.f;
        for (int b = 0; b < bands; ++b)
        {
            const float f = lo * std::pow (hi / lo, (float) b / (float) (bands - 1));
            for (int k = 0; k < 2; ++k)
            {
                mod[(size_t) (b * 2 + k)].bandpass (f, q, sr);
                car[(size_t) (b * 2 + k)].bandpass (f * shift, q, sr);
            }
            const float pos = (float) b / (float) (bands - 1);
            gain[(size_t) b] = std::pow (10.f, ((tilt - 0.5f) * 24.f * (pos - 0.5f)) / 20.f) * 3.0f;
        }
        att = 1.f - std::exp (-1.f / (0.004f * sr));
        rel = 1.f - std::exp (-1.f / (0.035f * sr));
    }
    float tick (float m, float c)
    {
        float out = 0;
        for (int b = 0; b < bands; ++b)
        {
            float mb = mod[(size_t) (b * 2)].tick (m); mb = mod[(size_t) (b * 2 + 1)].tick (mb);
            float cb = car[(size_t) (b * 2)].tick (c); cb = car[(size_t) (b * 2 + 1)].tick (cb);
            const float a = std::fabs (mb);
            float& e = env[(size_t) b];
            e += (a - e) * (a > e ? att : rel);
            out += cb * e * gain[(size_t) b];
        }
        out += hp.tick (m) * 0.5f;    // sibilance pass-through keeps consonants intelligible
        return out * (float) std::sqrt (16.0 / bands) * 4.f;
    }
private:
    float sr = 48000.f, att = 0.1f, rel = 0.01f, curShift = 1.f, curTilt = 0.5f;
    int bands = 0;
    std::vector<Biquad> mod, car;
    std::vector<float> env, gain;
    Biquad hp;
};

class PolyOsc {
public:
    void prepare (float sampleRate) { sr = sampleRate; }
    // freqs: up to 4, wave 0 saw 1 pulse 2 noise
    float tick (const float* freqs, int n, int wave)
    {
        float s = 0;
        for (int i = 0; i < n; ++i)
        {
            const float dt = freqs[i] / sr;
            float& p = ph[i];
            p += dt; if (p >= 1.f) p -= 1.f;
            if (wave == 2) { rs = rs * 1664525u + 1013904223u; s += (float) (int32_t) rs * (1.0f / 2147483648.0f) * 0.6f; continue; }
            float v = 2.f * p - 1.f - blep (p, dt);
            if (wave == 1) { float q = p + 0.5f; if (q >= 1.f) q -= 1.f; v -= 2.f * q - 1.f - blep (q, dt); }
            s += v * 0.5f;
        }
        return n > 0 ? s / std::sqrt ((float) n) : 0.f;
    }
private:
    static float blep (float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    float sr = 48000.f, ph[4] {};
    uint32_t rs = 777u;
};

} // namespace sm
