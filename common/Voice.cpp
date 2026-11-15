#include "Voice.h"
#include <algorithm>
#include <cstring>

namespace sm {

namespace {
constexpr int kFrame = 32;
constexpr float kVoiceGain = 0.11f;
constexpr float kAspGain = 0.05f;
constexpr float kFricGain = 0.20f;
inline float smooth (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate; T = (float) (1.0 / sr);
    for (auto& x : r) x.reset();
    np.reset(); nz.reset(); n1.reset(); n2.reset();
    running = false; env = 0;
}

void Voice::start (const Utterance& u, float startPoint)
{
    std::memcpy (&utt, &u, sizeof (Utterance));
    seg = 0; tMs = 0;
    if (startPoint > 0.001f && utt.totalMs > 0)
    {
        float target = startPoint * utt.totalMs, acc = 0;
        for (int i = 0; i < utt.n; ++i)
        {
            if (acc + utt.seg[i].dur > target) { seg = i; tMs = target - acc; break; }
            acc += utt.seg[i].dur;
        }
        // begin on a word boundary-ish: back up to the previous word start if close
        while (seg > 0 && ! (utt.seg[seg].flags & kSegWordStart) && phon (utt.seg[seg].ph).type != PT::Sil && tMs < 1.f) { --seg; }
    }
    if (env < 0.01f) { for (auto& x : r) x.reset(); np.reset(); nz.reset(); n1.reset(); n2.reset(); f0s = tgF0; }
    running = utt.n > 0;
    advanceReq = false;
    envTarget = 1;
    curWord = utt.seg[seg].word;
    curPhon = utt.seg[seg].ph;
    segTempo = 1.f;
}

void Voice::stop() { running = false; envTarget = 0; }

Voice::Tg Voice::targets (int i, float frac) const
{
    i = std::clamp (i, 0, utt.n - 1);
    const Phon* p = &phon (utt.seg[i].ph);
    // h and silence take the formants of a neighbour (next voiced segment if any, else previous)
    if (p->type == PT::Asp || p->type == PT::Sil)
    {
        int j = i + 1;
        while (j < utt.n && (phon (utt.seg[j].ph).type == PT::Sil || phon (utt.seg[j].ph).type == PT::Asp)) ++j;
        if (j >= utt.n) { j = i - 1; while (j > 0 && phon (utt.seg[j].ph).type == PT::Sil) --j; }
        if (j >= 0 && j < utt.n) { p = &phon (utt.seg[j].ph); frac = j > i ? 0.f : 1.f; }
    }
    Tg t;
    t.f[0] = lerp (p->f1, p->e1, frac); t.f[1] = lerp (p->f2, p->e2, frac); t.f[2] = lerp (p->f3, p->e3, frac);
    t.b[0] = p->b1; t.b[1] = p->b2; t.b[2] = p->b3;
    return t;
}

void Voice::controlFrame (const VoiceParams& vp, int frameLen)
{
    const float emo = std::clamp (vp.emotion, -1.f, 1.f);
    if (running)
    {
        float hp = 1e9f;
        const Seg& cs = utt.seg[seg];
        const Phon& cp = phon (cs.ph);
        if (vp.hold && cp.type != PT::Sil)
            hp = (cp.type == PT::Stop || cp.type == PT::Affr) ? cs.dur * 0.8f : cs.dur * 0.5f;
        const float tempo = vp.speed * (1.f + 0.25f * emo) * segTempo;
        tMs += (float) frameLen * T * 1000.f * std::max (0.05f, tempo);
        if (vp.hold && ! advanceReq && tMs >= hp) tMs = hp;
        while (running && tMs >= utt.seg[seg].dur)
        {
            tMs -= utt.seg[seg].dur;
            ++seg;
            advanceReq = false;
            if (seg >= utt.n) { running = false; seg = utt.n - 1; envTarget = 0; break; }
            const Seg& s = utt.seg[seg];
            segTempo = 1.f + std::fabs (emo) * 0.15f * noise();
            if (phon (s.ph).type != PT::Sil)
            {
                curPhon = s.ph;
                if (pendingEv && pendingEv->nPhon < 32) pendingEv->phonAt[pendingEv->nPhon++] = frameOffset;
            }
            if ((s.flags & kSegWordStart))
            {
                curWord = s.word;
                if (pendingEv && pendingEv->nWord < 16) pendingEv->wordAt[pendingEv->nWord++] = frameOffset;
            }
            if (vp.hold && phon (s.ph).type != PT::Sil)
            {
                const float h2 = (phon (s.ph).type == PT::Stop || phon (s.ph).type == PT::Affr) ? s.dur * 0.8f : s.dur * 0.5f;
                if (tMs > h2) tMs = h2;
            }
        }
    }

    const Seg& s = utt.seg[std::clamp (seg, 0, std::max (0, utt.n - 1))];
    const Phon& p = phon (s.ph);
    const float D = std::max (1.f, s.dur);
    const float frac = std::clamp (tMs / D, 0.f, 1.f);

    // ---- formants with coarticulation ----
    Tg cur = targets (seg, frac);
    const float trans = std::min (45.f, 0.42f * D);
    if (seg > 0 && tMs < trans)
    {
        const Tg a = targets (seg - 1, 1.f), b = targets (seg, 0.f);
        const float w = smooth (tMs / trans);
        for (int k = 0; k < 3; ++k) { const float B = 0.5f * (a.f[k] + b.f[k]); cur.f[k] = lerp (B, cur.f[k], w); }
    }
    else if (seg < utt.n - 1 && tMs > D - trans)
    {
        const Tg a = targets (seg, 1.f), b = targets (seg + 1, 0.f);
        const float w = smooth ((D - tMs) / trans);
        for (int k = 0; k < 3; ++k) { const float B = 0.5f * (a.f[k] + b.f[k]); cur.f[k] = lerp (B, cur.f[k], w); }
    }

    // ---- source amplitudes by segment type / phase ----
    float AV = 0, AF = 0, AH = 0, NAS = 0;
    float nf1 = p.n1f, nb1 = p.n1b, ng1 = p.n1g, nf2 = p.n2f, nb2 = p.n2b, ng2 = p.n2g, nbp = p.nby;
    trillDepth = 0;
    afSlew = 1.f - std::exp (-1.f / (0.004f * (float) sr));
    switch (p.type)
    {
        case PT::Sil: break;
        case PT::Vowel: case PT::Diph:
        {
            AV = 1.f;
            const bool nPrev = seg > 0 && phon (utt.seg[seg - 1].ph).type == PT::Nasal;
            const bool nNext = seg < utt.n - 1 && phon (utt.seg[seg + 1].ph).type == PT::Nasal;
            if (nPrev && tMs < trans) NAS = 0.4f * (1.f - tMs / trans);
            if (nNext && tMs > D - trans) NAS = 0.4f * (1.f - (D - tMs) / trans);
            break;
        }
        case PT::Nasal: AV = 0.65f; NAS = 1.f; break;
        case PT::Liquid: AV = 0.85f; break;
        case PT::Glide: AV = 0.8f; break;
        case PT::Trill: AV = 0.8f; AF = p.namp; trillDepth = 0.55f; break;
        case PT::Asp: AH = 0.75f; break;
        case PT::Fric: AF = p.namp; AV = p.voiced ? 0.45f : 0.f; break;
        case PT::Stop: case PT::Affr:
        {
            const bool affr = p.type == PT::Affr;
            const float clos = D * (affr ? 0.38f : 0.55f);
            const float burst = affr ? D : clos + 12.f;
            if (tMs < clos) { AV = p.voiced ? 0.12f : 0.f; cur.f[0] = 200.f; }
            else if (tMs < burst)
            {
                AF = affr ? 0.85f : 1.0f;
                AV = p.voiced ? 0.35f : 0.f;
                afSlew = 1.f - std::exp (-1.f / (0.0007f * (float) sr));
            }
            else
            {
                const bool asp = p.aspirated && ! (s.flags & kSegNoAsp);
                if (asp) AH = 0.6f;
                else AV = p.voiced ? 0.8f : 0.35f;
            }
            break;
        }
    }

    // ---- prosody ----
    const float rangeScale = std::max (0.25f, 1.f + 0.7f * emo);
    const float semis = vp.intonation * rangeScale * lerp (s.f0a, s.f0b, frac) + 2.5f * emo;
    tgF0 = vp.f0 * std::pow (2.f, semis / 12.f);
    jitter = 0.005f + 0.02f * std::max (0.f, emo) + 0.008f * std::max (0.f, -emo);
    shimmer = 0.03f + 0.08f * std::max (0.f, -emo);
    breath = 0.04f + 0.35f * std::max (0.f, -emo);
    vibDepth = 0.002f + 0.006f * std::max (0.f, emo);
    oq = 0.52f + 0.18f * std::max (0.f, -emo) - 0.06f * std::max (0.f, emo);
    tiltA = 0.88f - 0.84f * std::clamp (vp.brightness, 0.f, 1.f);

    if (! running) { AV = AF = AH = 0; }
    tgAV = AV; tgAF = AF; tgAH = AH; tgNas = NAS;
    if (vp.hold == 2) envTarget = gateOpen ? 1.f : 0.f;
    else if (running) envTarget = 1.f;

    // ---- filters ----
    const float fs = vp.formant;
    const float bwScale = 0.8f + 0.2f * fs;
    const float nyq = 0.45f * (float) sr;
    for (int k = 0; k < 3; ++k) r[k].set (std::min (cur.f[k] * fs, nyq), cur.b[k] * bwScale, T);
    r[3].set (std::min (3500.f * fs, nyq), 250.f, T);
    r[4].set (std::min (4500.f * fs, nyq), 300.f, T);
    np.set (270.f, 100.f, T);
    {
        // nasal zero: the segment's own, or the neighbouring nasal's for nasalised vowels
        float fz = p.nz;
        if (fz <= 0 && seg > 0 && phon (utt.seg[seg - 1].ph).type == PT::Nasal) fz = phon (utt.seg[seg - 1].ph).nz;
        if (fz <= 0 && seg < utt.n - 1 && phon (utt.seg[seg + 1].ph).type == PT::Nasal) fz = phon (utt.seg[seg + 1].ph).nz;
        if (fz <= 0) fz = 450.f;
        nz.set (fz, 120.f, T);
        nasComp = (270.f / fz) * (270.f / fz);
    }
    const float nscale = std::sqrt (fs);
    if (p.type == PT::Stop && seg < utt.n - 1 && ! std::strcmp (p.id, "k"))
    {   // velar burst follows the next vowel's F2
        const Tg nx = targets (seg + 1, 0.f);
        nf1 = std::max (1200.f, nx.f[1] * 1.1f);
    }
    if (nf1 > 0) { n1.set (nf1 * nscale, nb1, (float) sr); n1g = ng1; } else n1g = 0;
    if (nf2 > 0) { n2.set (nf2 * nscale, nb2, (float) sr); n2g = ng2; } else n2g = 0;
    if (nf1 > 0 || nbp > 0) nby = nbp;
}

void Voice::render (float* out, int n, const VoiceParams& vp, VoiceEvents* ev)
{
    pendingEv = ev;
    const float slewAV = 1.f - std::exp (-1.f / (0.003f * (float) sr));
    const float slewF0 = 1.f - std::exp (-1.f / (0.018f * (float) sr));
    const float envUp = 1.f - std::exp (-1.f / (0.004f * (float) sr));
    const float envDn = 1.f - std::exp (-1.f / (0.025f * (float) sr));
    float sumSq = 0;
    for (int i0 = 0; i0 < n; i0 += kFrame)
    {
        const int len = std::min (kFrame, n - i0);
        frameOffset = i0;
        if (! running && env < 1.0e-4f && av < 1.0e-4f)
        {
            std::fill (out + i0, out + i0 + len, 0.f);
            continue;
        }
        controlFrame (vp, len);
        for (int k = 0; k < len; ++k)
        {
            av += (tgAV - av) * slewAV;
            af += (tgAF - af) * afSlew;
            ah += (tgAH - ah) * slewAV;
            nas += (tgNas - nas) * slewAV;
            f0s += (tgF0 - f0s) * slewF0;
            env += (envTarget - env) * (envTarget > env ? envUp : envDn);

            vibPhase += 5.5f * T; if (vibPhase >= 1.f) vibPhase -= 1.f;
            phase += periodF0 * T;
            if (phase >= 1.f)
            {
                phase -= std::floor (phase);
                periodF0 = f0s * (1.f + jitter * gauss()) * (1.f + vibDepth * std::sin (6.2831853f * vibPhase));
                periodF0 = std::clamp (periodF0, 20.f, 1800.f);
                periodAmp = 1.f + shimmer * gauss();
            }
            const bool open = phase < oq;
            const float x = phase / oq;
            const float g = open ? (2.f * x - 3.f * x * x) : 0.f;
            float src = g * av * periodAmp;
            tiltY = (1.f - tiltA) * src + tiltA * tiltY;
            src = tiltY * (1.f + 2.5f * tiltA);
            const float asp = noise() * (ah + breath * av * (open ? 1.f : 0.25f));
            float exc = src * kVoiceGain + asp * kAspGain;
            if (nas > 0.002f) { const float nn = nz.tick (np.tick (exc)) * nasComp; exc += (nn - exc) * nas; }
            float y = exc;
            for (int f = 4; f >= 0; --f) y = r[f].tick (y);
            const float fn = noise() * af;
            const float fr = n1.tick (fn) * n1g + n2.tick (fn) * n2g + fn * nby;
            float am = 1.f;
            if (trillDepth > 0) { trillPhase += 26.f * T; if (trillPhase >= 1.f) trillPhase -= 1.f; am = 1.f - trillDepth * (0.5f + 0.5f * std::sin (6.2831853f * trillPhase)); }
            float o = (y * am + fr * kFricGain) * env * vp.level;
            // DC blocker
            const float d = o - dcX + 0.995f * dcY; dcX = o; dcY = d;
            o = d;
            if (! std::isfinite (o)) { o = 0; for (auto& rr : r) rr.reset(); n1.reset(); n2.reset(); dcX = dcY = 0; }
            out[i0 + k] = o;
            sumSq += o * o;
        }
    }
    meterLevel = std::sqrt (sumSq / (float) std::max (1, n));
    pendingEv = nullptr;
}

} // namespace sm
