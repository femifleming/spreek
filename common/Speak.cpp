#include "Speak.h"
#include "Planner.h"
#include "Voice.h"
#include <cmath>

namespace sm {

Spoken speak (const std::string& text, Lang lang, double sr, float f0, float formant, float speed)
{
    Spoken out;
    Utterance u;
    buildUtterance (G2P::get().text (text, lang), u, text);
    if (u.n == 0) return out;
    Voice v; v.prepare (sr);
    v.start (u, 0.f);
    VoiceParams p; p.f0 = f0; p.formant = formant; p.speed = speed;
    VoiceEvents ev;
    float buf[512];
    int guard = 0, pos = 0;
    while ((v.active() || out.samples.empty()) && guard++ < 4000)   // at most ~40 s
    {
        ev.clear();
        v.render (buf, 512, p, &ev);
        for (int w = 0; w < ev.nWord; ++w) out.onsets.push_back (pos + ev.wordAt[w]);
        out.samples.insert (out.samples.end(), buf, buf + 512);
        pos += 512;
    }
    // trim the tail of silence, keep a little room
    const float thr = 0.002f;
    int end = (int) out.samples.size();
    while (end > 0 && std::fabs (out.samples[(size_t) end - 1]) < thr) --end;
    end = std::min ((int) out.samples.size(), end + (int) (sr * 0.05));
    out.samples.resize ((size_t) std::max (end, 1));
    for (auto& o : out.onsets) o = std::min (o, end - 1);
    return out;
}

} // namespace sm
