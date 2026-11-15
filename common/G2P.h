// Spraakmaker — text → phonemes. English: CMU Pronouncing Dictionary (+ letter rules for unknown words).
// Dutch: rule-based grapheme→phoneme (open/closed syllables, digraphs, schwa, final devoicing, stress rules).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace sm {

enum class Lang { EN = 0, NL = 1 };

enum : uint8_t { kFlagNoAsp = 1, kFlagWordStart = 2, kFlagFunction = 4 };

struct PhTok { int ph; uint8_t stress = 0; uint8_t flags = 0; };

struct WordPh {
    std::string word;           // display text (original)
    std::vector<PhTok> ph;
    int pauseMs = 0;            // > 0 = punctuation pause, ph empty
    bool question = false;      // phrase ends with '?'
    bool function = false;      // function word (unaccented)
};

class G2P {
public:
    static G2P& get();
    void preload();                                              // parse dictionaries (call off the audio thread)
    std::vector<WordPh> text (const std::string& text, Lang lang);
    std::vector<PhTok> word (const std::string& w, Lang lang, bool* known = nullptr);
    std::vector<PhTok> letter (char c, Lang lang);
    const std::vector<std::string>& dictWords (Lang lang);       // dictionary word list
    const std::string& corpus (Lang lang);                       // Markov training text
    static std::string lower (const std::string& s);
    std::string numberWords (int n, Lang lang);

private:
    G2P() = default;
    std::vector<PhTok> enWord (const std::string& w, bool* known);
    std::vector<PhTok> enRules (const std::string& w);
    std::vector<PhTok> nlWord (const std::string& w);
};

std::string phonString (const std::vector<PhTok>& p);
int unknownPhonemeLookups();                                     // diagnostics: ids not in the table            // IPA string for display/debug

} // namespace sm
