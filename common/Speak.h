// spreek — one call that turns text into speech with the built-in Klatt formant voice (shared by both plug-ins).
#pragma once
#include "G2P.h"
#include <string>
#include <vector>

namespace sm {

struct Spoken {
    std::vector<float> samples;   // mono, at the asked-for sample rate
    std::vector<int> onsets;      // first sample of every word
};

// text -> phonemes (CMUdict English / rule-based Dutch) -> prosody -> Klatt voice. Off the audio thread.
// f0 in Hz (110 = a man, 200 = a woman); formant = vocal-tract scale (1.0 man, ~1.17 woman); speed 1 = normal.
Spoken speak (const std::string& text, Lang lang, double sampleRate, float f0, float formant = 1.0f, float speed = 1.0f);

} // namespace sm
