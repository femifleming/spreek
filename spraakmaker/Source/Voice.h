// Spraakmaker — one speech voice: Klatt-style cascade/parallel formant synthesizer driven by an Utterance.
#pragma once
#include "Planner.h"
#include "Phonemes.h"
#include <cmath>
#include <cstdint>

namespace sm {

struct VoiceParams {
    float f0 = 110.f;          // base pitch Hz (note + pitch knob + micro + chord)
    float formant = 1.f;       // vocal-tract scale
    float intonation = 1.f;    // contour depth multiplier
    float brightness = 0.5f;   // 0 dark .. 1 bright
    float speed = 1.f;         // tempo multiplier
    float emotion = 0.f;       // -1 sad/calm .. +1 excited
    int hold = 0;              // 0 none, 1 phoneme-trigger hold, 2 phoneme-gate
    float level = 1.f;
};

struct VoiceEvents {           // filled during render (sample offsets inside the block)
    int nWord = 0, nPhon = 0;
    int wordAt[16] {}, phonAt[32] {};
    void clear() { nWord = nPhon = 0; }
};

class Voice {
public:
    void prepare (double sampleRate);
    void start (const Utterance& u, float startPoint);
    void stop();                       // quick fade
    void advance() { advanceReq = true; gateOpen = true; }
    void setGate (bool g) { gateOpen = g; }
    bool active() const { return running || env > 1.0e-4f; }
    bool finished() const { return ! running; }
    void render (float* out, int n, const VoiceParams& p, VoiceEvents* ev);

    // display
    int currentWord() const { return curWord; }
    int currentPhon() const { return curPhon; }
    float meter() const { return meterLevel; }
    const Utterance& utterance() const { return utt; }
    int segIndex() const { return seg; }

private:
    struct Tg { float f[3]; float b[3]; };
    Tg targets (int i, float frac) const;
    void controlFrame (const VoiceParams& p, int frameLen);
    float noise() { rs = rs * 1664525u + 1013904223u; return (float) (int32_t) rs * (1.0f / 2147483648.0f); }
    float gauss() { return (noise() + noise() + noise()) * 0.577f; }

    struct Reso {
        float a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
        void set (float f, float bw, float T) {
            const float r = std::exp (-3.14159265f * bw * T);
            c = -r * r; b = 2.f * r * std::cos (6.2831853f * f * T); a = 1.f - b - c;
        }
        float tick (float x) { const float y = a * x + b * y1 + c * y2; y2 = y1; y1 = y; return y; }
        void reset() { y1 = y2 = 0; }
    };
    struct Anti {
        float a = 1, b = 0, c = 0, x1 = 0, x2 = 0;
        void set (float f, float bw, float T) {
            const float r = std::exp (-3.14159265f * bw * T);
            const float C = -r * r, B = 2.f * r * std::cos (6.2831853f * f * T), A = 1.f - B - C;
            a = 1.f / A; b = -B / A; c = -C / A;
        }
        float tick (float x) { const float y = a * x + b * x1 + c * x2; x2 = x1; x1 = x; return y; }
        void reset() { x1 = x2 = 0; }
    };
    struct BP {   // RBJ band-pass, 0 dB peak
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void set (float f, float bw, float sr) {
            f = std::fmin (std::fmax (f, 40.f), sr * 0.45f);
            const float w = 6.2831853f * f / sr, q = std::fmax (0.3f, f / std::fmax (bw, 10.f));
            const float al = std::sin (w) / (2 * q), a0 = 1 + al;
            b0 = al / a0; b2 = -al / a0; a1 = -2 * std::cos (w) / a0; a2 = (1 - al) / a0;
        }
        float tick (float x) { const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
        void reset() { x1 = x2 = y1 = y2 = 0; }
    };

    double sr = 48000.0;
    float T = 1.f / 48000.f;
    Utterance utt;
    bool running = false;
    int seg = 0;
    float tMs = 0;
    float segTempo = 1.f;
    bool advanceReq = false, gateOpen = true;
    int curWord = 0, curPhon = 0;
    float meterLevel = 0;

    // control targets (per frame) and smoothed per-sample values
    float tgAV = 0, tgAF = 0, tgAH = 0, tgNas = 0, tgF0 = 110;
    float av = 0, af = 0, ah = 0, nas = 0, f0s = 110, env = 0, envTarget = 1;
    float trillPhase = 0, trillDepth = 0;
    float avSlew = 0.01f, afSlew = 0.01f;

    // glottal source
    float phase = 0, periodF0 = 110, periodAmp = 1, oq = 0.55f, tiltY = 0, tiltA = 0.3f;
    float jitter = 0.006f, shimmer = 0.03f, breath = 0.05f, vibPhase = 0, vibDepth = 0;
    uint32_t rs = 22222u;

    Reso r[5];
    Reso np;
    Anti nz;
    BP n1, n2;
    float n1g = 0, n2g = 0, nby = 0, nasComp = 1;
    float dcX = 0, dcY = 0;
    VoiceEvents* pendingEv = nullptr;
    int frameOffset = 0;
    float fricLP = 0;
};

} // namespace sm
