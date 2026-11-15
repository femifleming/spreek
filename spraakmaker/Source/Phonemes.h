// Spraakmaker — phoneme inventory (English + Dutch) with formant targets for the Klatt-style synth.
#pragma once
#include <cstdint>
#include <string>

namespace sm {

enum class PT : uint8_t { Sil, Vowel, Diph, Stop, Affr, Fric, Nasal, Liquid, Glide, Asp, Trill };

struct Phon {
    const char* id;      // IPA
    PT type;
    bool voiced;
    float f1, f2, f3;    // start targets (Hz)
    float e1, e2, e3;    // end targets (diphthongs; = start otherwise)
    float b1, b2, b3;    // bandwidths
    float dur;           // base duration ms
    float n1f, n1b, n1g; // noise filter 1 (frication / burst)
    float n2f, n2b, n2g; // noise filter 2
    float nby;           // broadband noise bypass
    float namp;          // noise amplitude
    float nasal;         // nasal coupling 0..1
    float nz;            // nasal zero freq
    bool aspirated;      // voiceless stop with aspiration (English)
};

int numPhons();
const Phon& phon (int i);
int findPhon (const std::string& id);   // -1 if unknown
int silIndex();                          // "_" pause
bool isVowelType (PT t);

} // namespace sm
