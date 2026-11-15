#include "G2P.h"
#include "Phonemes.h"
#include <BinaryData.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstring>
#include <atomic>

namespace sm {

namespace {
std::once_flag gLoadOnce;
std::unordered_map<std::string, std::string> gEnDict;     // word -> ARPAbet
std::vector<std::string> gEnWords, gNlWords;
std::string gEnCorpus, gNlCorpus;

std::atomic<int> gUnknown { 0 };
int P (const char* id)
{
    int i = findPhon (id);
    if (i < 0) gUnknown.fetch_add (1);

    return i < 0 ? silIndex() : i;
}

// parse "ˈn eː d ə ɻ" style lists (ˈ before a token = primary stress on next vowel)
std::vector<PhTok> parseIpa (const std::string& s, bool dutch)
{
    std::vector<PhTok> out;
    size_t i = 0;
    bool stressNext = false;
    while (i < s.size())
    {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        size_t j = s.find (' ', i);
        if (j == std::string::npos) j = s.size();
        std::string tok = s.substr (i, j - i);
        i = j;
        if (tok.rfind ("ˈ", 0) == 0) { stressNext = true; tok = tok.substr (std::strlen ("ˈ")); }
        if (tok.empty()) continue;
        PhTok t { P (tok.c_str()), 0, 0 };
        if (dutch && (tok == "p" || tok == "t" || tok == "k")) t.flags |= kFlagNoAsp;
        if (stressNext && isVowelType (phon (t.ph).type)) { t.stress = 1; stressNext = false; }
        out.push_back (t);
    }
    // if no stress was given, stress the first full vowel
    bool any = false;
    for (auto& t : out) any |= t.stress == 1;
    if (! any)
        for (auto& t : out)
            if (isVowelType (phon (t.ph).type) && phon (t.ph).id != std::string ("ə")) { t.stress = 1; break; }
    return out;
}

const std::unordered_set<std::string> kEnFunction {
    "a", "an", "the", "of", "to", "in", "on", "at", "for", "and", "or", "but", "is", "are", "was", "were", "be",
    "it", "i", "you", "he", "she", "we", "they", "my", "your", "his", "her", "its", "our", "their", "this", "that",
    "with", "as", "by", "from", "not", "do", "does", "had", "has", "have", "so", "if", "me", "him", "them", "us" };
const std::unordered_set<std::string> kNlFunction {
    "de", "het", "een", "en", "of", "maar", "is", "zijn", "was", "waren", "ik", "je", "jij", "u", "hij", "zij", "ze",
    "we", "wij", "mijn", "jouw", "haar", "ons", "onze", "hun", "dit", "dat", "die", "deze", "met", "van", "in", "op",
    "aan", "te", "voor", "om", "bij", "naar", "niet", "er", "al", "ook", "nog", "wel", "me", "mij", "heb", "hebt", "heeft" };

const std::unordered_map<std::string, std::string> kNlExceptions {
    { "een", "ə n" }, { "één", "ˈeː n" }, { "het", "ə t" }, { "'t", "ə t" }, { "'s", "s" },
    { "hallo", "h ɑ ˈl oː" }, { "café", "k ɑ ˈf eː" }, { "oké", "oː ˈk eː" }, { "hoi", "h ɔ j" },
    { "doei", "d u j" }, { "goedemorgen", "ɣ u d ə ˈm ɔ ɻ ɣ ə" }, { "goedemiddag", "ɣ u d ə ˈm ɪ d ɑ x" },
    { "goedenavond", "ɣ u d ə ˈn aː v ɔ n t" }, { "goedenacht", "ɣ u d ə ˈn ɑ x t" },
    { "dankjewel", "d ɑ ŋ k j ə ˈʋ ɛ l" }, { "alstublieft", "ɑ l s t y ˈb l i f t" },
    { "alsjeblieft", "ɑ ʃ ə ˈb l i f t" }, { "nederland", "ˈn eː d ə ɻ l ɑ n t" },
    { "nederlands", "ˈn eː d ə ɻ l ɑ n t s" }, { "amsterdam", "ɑ m s t ə ɻ ˈd ɑ m" },
    { "rotterdam", "ˈʀ ɔ t ə ɻ d ɑ m" }, { "spraakmaker", "ˈs p ʀ aː k m aː k ə ɻ" },
    { "eigenlijk", "ˈɛi ɣ ə n l ə k" }, { "natuurlijk", "n aː ˈt y ɻ l ə k" }, { "gezellig", "ɣ ə ˈz ɛ l ə x" },
    { "beter", "ˈb eː t ə ɻ" }, { "bezig", "ˈb eː z ə x" }, { "geven", "ˈɣ eː v ə" }, { "verder", "ˈv ɛ ɻ d ə ɻ" },
    { "vers", "v ɛ ɻ s" }, { "verf", "v ɛ ɻ f" }, { "ver", "v ɛ ɻ" }, { "de", "d ə" }, { "te", "t ə" },
    { "je", "j ə" }, { "me", "m ə" }, { "we", "ʋ ə" }, { "ze", "z ə" }, { "u", "y" }, { "nu", "n y" },
    { "zo", "z oː" }, { "ja", "j aː" }, { "nee", "n eː" }, { "ok", "oː ˈk eː" }, { "synthesizer", "ˈs ɪ n t ə s ɑ j z ə ɻ" },
    { "computer", "k ɔ m ˈp j u t ə ɻ" }, { "techno", "ˈt ɛ k n oː" }, { "house", "h ʌu s" }, { "beat", "b i t" },
    { "jazz", "dʒ ɛ s" }, { "club", "k l ʏ p" }, { "festival", "ˈf ɛ s t i v ɑ l" }, { "idee", "i ˈd eː" },
    { "museum", "m y ˈz eː j ʏ m" }, { "muziek", "m y ˈz i k" }, { "koffie", "ˈk ɔ f i" }, { "thee", "t eː" },
    { "eten", "ˈeː t ə" }, { "oranje", "oː ˈʀ ɑ ɲ ə" }, { "logisch", "ˈl oː ɣ i s" }, { "even", "ˈeː v ə" }, { "heel", "h eː l" }, { "tegen", "ˈt eː ɣ ə" },
    { "leven", "ˈl eː v ə" }, { "nemen", "ˈn eː m ə" }, { "geleden", "ɣ ə ˈl eː d ə" }, { "belgië", "ˈb ɛ l ɣ i j ə" },
};

// ---- small utf-8 helpers ----
std::u32string utf8to32 (const std::string& s)
{
    std::u32string out;
    for (size_t i = 0; i < s.size();)
    {
        unsigned char c = (unsigned char) s[i];
        char32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + (size_t) k < s.size(); ++k) cp = (cp << 6) | ((unsigned char) s[i + (size_t) k] & 0x3f);
        out.push_back (cp); i += (size_t) n;
    }
    return out;
}
std::string utf32to8 (char32_t cp)
{
    std::string r;
    if (cp < 0x80) r += (char) cp;
    else if (cp < 0x800) { r += (char) (0xC0 | (cp >> 6)); r += (char) (0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { r += (char) (0xE0 | (cp >> 12)); r += (char) (0x80 | ((cp >> 6) & 0x3f)); r += (char) (0x80 | (cp & 0x3f)); }
    else { r += (char) (0xF0 | (cp >> 18)); r += (char) (0x80 | ((cp >> 12) & 0x3f)); r += (char) (0x80 | ((cp >> 6) & 0x3f)); r += (char) (0x80 | (cp & 0x3f)); }
    return r;
}
char32_t lowerCp (char32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}
bool isWordCp (char32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'' || c == 0x2019
        || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7);
}
bool isVowelCh (char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
} // namespace

std::string G2P::lower (const std::string& s)
{
    std::string r;
    for (char32_t c : utf8to32 (s)) r += utf32to8 (lowerCp (c));
    return r;
}

G2P& G2P::get() { static G2P g; return g; }

void G2P::preload()
{
    std::call_once (gLoadOnce, [] {
        const char* d = BinaryData::en_dict_txt;
        const int n = BinaryData::en_dict_txtSize;
        gEnDict.reserve (130000);
        int i = 0;
        while (i < n)
        {
            int e = i; while (e < n && d[e] != '\n') ++e;
            int sp = i; while (sp < e && d[sp] != ' ') ++sp;
            if (sp < e)
            {
                std::string w (d + i, (size_t) (sp - i));
                gEnDict.emplace (w, std::string (d + sp + 1, (size_t) (e - sp - 1)));
                bool alpha = w.size() >= 2;
                for (char c : w) alpha &= (c >= 'a' && c <= 'z');
                if (alpha) gEnWords.push_back (w);
            }
            i = e + 1;
        }
        std::string nl (BinaryData::nl_words_txt, (size_t) BinaryData::nl_words_txtSize);
        size_t p = 0;
        while (p < nl.size()) { size_t e = nl.find ('\n', p); if (e == std::string::npos) e = nl.size(); if (e > p) gNlWords.push_back (nl.substr (p, e - p)); p = e + 1; }
        gEnCorpus.assign (BinaryData::en_corpus_txt, (size_t) BinaryData::en_corpus_txtSize);
        gNlCorpus.assign (BinaryData::nl_corpus_txt, (size_t) BinaryData::nl_corpus_txtSize);
    });
}

const std::vector<std::string>& G2P::dictWords (Lang l) { preload(); return l == Lang::EN ? gEnWords : gNlWords; }
const std::string& G2P::corpus (Lang l) { preload(); return l == Lang::EN ? gEnCorpus : gNlCorpus; }

// ---------------------------------------------------------------- numbers
std::string G2P::numberWords (int n, Lang lang)
{
    if (n < 0) n = -n;
    if (lang == Lang::EN)
    {
        static const char* u[] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
            "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
        static const char* t[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
        if (n < 20) return u[n];
        if (n < 100) return std::string (t[n / 10]) + (n % 10 ? std::string (" ") + u[n % 10] : "");
        if (n < 1000) return std::string (u[n / 100]) + " hundred" + (n % 100 ? " " + numberWords (n % 100, lang) : "");
        if (n < 1000000) return numberWords (n / 1000, lang) + " thousand" + (n % 1000 ? " " + numberWords (n % 1000, lang) : "");
        return "many";
    }
    static const char* u[] = { "nul", "één", "twee", "drie", "vier", "vijf", "zes", "zeven", "acht", "negen", "tien",
        "elf", "twaalf", "dertien", "veertien", "vijftien", "zestien", "zeventien", "achttien", "negentien" };
    static const char* t[] = { "", "", "twintig", "dertig", "veertig", "vijftig", "zestig", "zeventig", "tachtig", "negentig" };
    if (n < 20) return u[n];
    if (n < 100)
    {
        if (n % 10 == 0) return t[n / 10];
        std::string unit = n % 10 == 1 ? "een" : u[n % 10];
        bool trema = unit.back() == 'e';
        return unit + (trema ? "ën" : "en") + t[n / 10];
    }
    if (n < 1000) return std::string (n / 100 == 1 ? "" : (n / 100 == 1 ? "" : std::string (u[n / 100]))) + "honderd" + (n % 100 ? numberWords (n % 100, lang) : "");
    if (n < 1000000) return (n / 1000 == 1 ? std::string() : numberWords (n / 1000, lang)) + "duizend" + (n % 1000 ? " " + numberWords (n % 1000, lang) : "");
    return "veel";
}

// ---------------------------------------------------------------- English
std::vector<PhTok> G2P::enWord (const std::string& w, bool* known)
{
    preload();
    auto it = gEnDict.find (w);
    std::string arpa;
    if (it != gEnDict.end()) arpa = it->second;
    else if (w.size() > 2 && w.substr (w.size() - 2) == "'s" && gEnDict.count (w.substr (0, w.size() - 2)))
        arpa = gEnDict[w.substr (0, w.size() - 2)] + " Z";
    if (arpa.empty()) { if (known) *known = false; return enRules (w); }
    if (known) *known = true;

    static const std::unordered_map<std::string, const char*> map {
        { "AA", "ɑː" }, { "AE", "æ" }, { "AO", "ɔː" }, { "AW", "aʊ" }, { "AY", "aɪ" }, { "EH", "ɛ" }, { "ER", "ɝ" },
        { "EY", "eɪ" }, { "IH", "ɪ" }, { "IY", "iː" }, { "OW", "oʊ" }, { "OY", "ɔɪ" }, { "UH", "ʊ" }, { "UW", "uː" },
        { "B", "b" }, { "CH", "tʃ" }, { "D", "d" }, { "DH", "ð" }, { "F", "f" }, { "G", "g" }, { "HH", "h" },
        { "JH", "dʒ" }, { "K", "k" }, { "L", "l" }, { "M", "m" }, { "N", "n" }, { "NG", "ŋ" }, { "P", "p" },
        { "R", "ɹ" }, { "S", "s" }, { "SH", "ʃ" }, { "T", "t" }, { "TH", "θ" }, { "V", "v" }, { "W", "w" },
        { "Y", "j" }, { "Z", "z" }, { "ZH", "ʒ" } };
    std::vector<PhTok> out;
    size_t i = 0;
    while (i < arpa.size())
    {
        size_t j = arpa.find (' ', i); if (j == std::string::npos) j = arpa.size();
        std::string tok = arpa.substr (i, j - i); i = j + 1;
        if (tok.empty()) continue;
        int stress = -1;
        if (std::isdigit ((unsigned char) tok.back())) { stress = tok.back() - '0'; tok.pop_back(); }
        const char* id = nullptr;
        if (tok == "AH") id = stress == 0 ? "ə" : "ʌ";
        else { auto m = map.find (tok); if (m != map.end()) id = m->second; }
        if (! id) continue;
        PhTok t { P (id), (uint8_t) (stress == 1 ? 1 : stress == 2 ? 2 : 0), 0 };
        out.push_back (t);
    }
    return out;
}

std::vector<PhTok> G2P::enRules (const std::string& w)
{
    std::vector<PhTok> out;
    auto add = [&] (const char* id) { out.push_back ({ P (id), 0, 0 }); };
    const size_t n = w.size();
    for (size_t i = 0; i < n;)
    {
        auto at = [&] (const char* s) { return w.compare (i, std::strlen (s), s) == 0; };
        if (! std::isalpha ((unsigned char) w[i])) { ++i; continue; }
        if (at ("tion")) { add ("ʃ"); add ("ə"); add ("n"); i += 4; continue; }
        if (at ("sion")) { add ("ʒ"); add ("ə"); add ("n"); i += 4; continue; }
        if (at ("igh")) { add ("aɪ"); i += 3; continue; }
        if (at ("ch")) { add ("tʃ"); i += 2; continue; }
        if (at ("sh")) { add ("ʃ"); i += 2; continue; }
        if (at ("th")) { add ("θ"); i += 2; continue; }
        if (at ("ph")) { add ("f"); i += 2; continue; }
        if (at ("wh")) { add ("w"); i += 2; continue; }
        if (at ("ck")) { add ("k"); i += 2; continue; }
        if (at ("ng")) { add ("ŋ"); i += 2; continue; }
        if (at ("qu")) { add ("k"); add ("w"); i += 2; continue; }
        if (at ("ee") || at ("ea")) { add ("iː"); i += 2; continue; }
        if (at ("oo")) { add ("uː"); i += 2; continue; }
        if (at ("ou")) { add ("aʊ"); i += 2; continue; }
        if (at ("ow")) { add ("oʊ"); i += 2; continue; }
        if (at ("ai") || at ("ay")) { add ("eɪ"); i += 2; continue; }
        if (at ("oa")) { add ("oʊ"); i += 2; continue; }
        if (at ("oi") || at ("oy")) { add ("ɔɪ"); i += 2; continue; }
        if (at ("au") || at ("aw")) { add ("ɔː"); i += 2; continue; }
        if (at ("ar")) { add ("ɑː"); add ("ɹ"); i += 2; continue; }
        if (at ("er") || at ("ir") || at ("ur")) { add ("ɝ"); i += 2; continue; }
        if (at ("or")) { add ("ɔː"); add ("ɹ"); i += 2; continue; }
        char c = w[i];
        if (isVowelCh (c))
        {
            // magic e: V C e#
            bool magic = i + 2 < n && ! isVowelCh (w[i + 1]) && w[i + 2] == 'e' && i + 3 == n;
            if (c == 'e' && i == n - 1 && n > 2) { ++i; continue; }    // silent final e
            switch (c)
            {
                case 'a': add (magic ? "eɪ" : "æ"); break;
                case 'e': add (magic ? "iː" : "ɛ"); break;
                case 'i': add (magic ? "aɪ" : "ɪ"); break;
                case 'o': add (magic ? "oʊ" : "ɑː"); break;
                case 'u': add (magic ? "uː" : "ʌ"); break;
                case 'y': if (i == 0) add ("j"); else add (n <= 3 ? "aɪ" : "iː"); break;
            }
            ++i; continue;
        }
        if (i + 1 < n && w[i + 1] == c) { ++i; }    // double consonant
        switch (c)
        {
            case 'c': add (i + 1 < n && (w[i + 1] == 'e' || w[i + 1] == 'i' || w[i + 1] == 'y') ? "s" : "k"); break;
            case 'g': add ("g"); break;
            case 'j': add ("dʒ"); break;
            case 'x': add ("k"); add ("s"); break;
            case 'r': add ("ɹ"); break;
            case 'q': add ("k"); break;
            default: { char s[2] = { c, 0 }; if (findPhon (s) >= 0) add (s); }
        }
        ++i;
    }
    for (auto& t : out) if (isVowelType (phon (t.ph).type)) { t.stress = 1; break; }
    return out;
}

// ---------------------------------------------------------------- Dutch
std::vector<PhTok> G2P::nlWord (const std::string& raw)
{
    auto ex = kNlExceptions.find (raw);
    if (ex != kNlExceptions.end()) return parseIpa (ex->second, true);

    // normalise: '|' marks a hiatus (trema)
    std::string w;
    for (char32_t c : utf8to32 (raw))
    {
        switch (c)
        {
            case 0xE0: case 0xE1: case 0xE2: w += 'a'; break;
            case 0xE4: w += "|a"; break;
            case 0xE8: case 0xE9: case 0xEA: w += 'e'; break;
            case 0xEB: w += "|e"; break;
            case 0xEC: case 0xED: case 0xEE: w += 'i'; break;
            case 0xEF: w += "|i"; break;
            case 0xF2: case 0xF3: case 0xF4: w += 'o'; break;
            case 0xF6: w += "|o"; break;
            case 0xF9: case 0xFA: case 0xFB: w += 'u'; break;
            case 0xFC: w += "|u"; break;
            case 0xE7: w += 's'; break;
            case 0xF1: w += 'n'; break;
            default: if (c >= 'a' && c <= 'z') w += (char) c;
        }
    }
    if (w.empty()) return {};
    auto ex2 = kNlExceptions.find (w);
    if (ex2 != kNlExceptions.end()) return parseIpa (ex2->second, true);

    struct U { std::string g; bool vowel; int letters; int pos; };
    std::vector<U> us;
    static const char* vunits[] = { "eeuw", "ieuw", "aai", "ooi", "oei", "ouw", "auw", "aa", "ee", "oo", "uu", "ie", "oe", "eu",
                                    "ui", "ij", "ei", "ou", "au", "oi", "uw" };
    static const char* cunits[] = { "sch", "ch", "ng", "nk", "tj", "sj", "nj", "ph", "th", "qu", "ck", "dt" };
    for (size_t i = 0; i < w.size();)
    {
        if (w[i] == '|') { ++i; continue; }
        bool done = false;
        for (auto* v : vunits)
        {
            size_t L = std::strlen (v);
            if (w.compare (i, L, v) == 0)
            {
                if (std::strcmp (v, "uw") == 0 && i > 0 && isVowelCh (w[i - 1])) continue;
                us.push_back ({ v, true, (int) L, (int) i }); i += L; done = true; break;
            }
        }
        if (done) continue;
        if (isVowelCh (w[i])) { us.push_back ({ std::string (1, w[i]), true, 1, (int) i }); ++i; continue; }
        for (auto* c : cunits)
        {
            size_t L = std::strlen (c);
            if (w.compare (i, L, c) == 0) { us.push_back ({ c, false, (int) L, (int) i }); i += L; done = true; break; }
        }
        if (done) continue;
        if (i + 1 < w.size() && w[i + 1] == w[i]) { us.push_back ({ std::string (1, w[i]), false, 2, (int) i }); i += 2; continue; }
        us.push_back ({ std::string (1, w[i]), false, w[i] == 'x' ? 2 : 1, (int) i }); ++i;
    }

    std::vector<int> vidx;
    for (int k = 0; k < (int) us.size(); ++k) if (us[(size_t) k].vowel) vidx.push_back (k);
    const int syl = (int) vidx.size();
    auto endsWith = [&] (const char* s) { size_t L = std::strlen (s); return w.size() >= L && w.compare (w.size() - L, L, s) == 0; };

    // prefix
    int prefixUnit = -1; bool prefixSchwa = false;
    if (syl >= 2)
    {
        auto starts = [&] (const char* s) { return w.rfind (s, 0) == 0; };
        if ((starts ("be") || starts ("ge")) && w.size() > 4 && ! isVowelCh (w[2]) && w[2] != '|' && us.size() > 2 && us[1].g == "e")
        { prefixUnit = 1; prefixSchwa = true; }
        else if (starts ("ver") && w.size() > 5 && ! isVowelCh (w[3]) && us.size() > 2 && us[1].g == "e") { prefixUnit = 1; prefixSchwa = true; }
        else if ((starts ("ont") || starts ("her")) && w.size() > 5 && ! isVowelCh (w[3])) prefixUnit = 1;
        else if (starts ("er") && w.size() > 5 && ! isVowelCh (w[2]) && us.size() > 1 && us[0].g == "e") prefixUnit = 0;
    }

    // stress
    int stressV = -1;   // index into vidx
    auto lastVowelBeforeSuffix = [&] (int fromEnd) { return syl - 1 - fromEnd; };
    if (syl >= 2)
    {
        if (endsWith ("teit") || endsWith ("ief") || endsWith ("eel") || endsWith ("aal") || endsWith ("ist")
            || endsWith ("eur") || endsWith ("ier") || endsWith ("erij") || endsWith ("aat") || endsWith ("ant")
            || endsWith ("ent") || endsWith ("ein") || endsWith ("oor") || endsWith ("uur"))
            stressV = syl - 1;
        else if (syl >= 3 && (endsWith ("atie") || endsWith ("itie") || endsWith ("utie") || endsWith ("otie")))
            stressV = syl - 2;
        else if (syl >= 3 && endsWith ("ie") && ! endsWith ("tie"))
            stressV = syl - 1;
        else if (syl >= 3 && endsWith ("eren"))
            stressV = lastVowelBeforeSuffix (1);
    }
    if (stressV < 0)
    {
        stressV = 0;
        if (prefixUnit >= 0)
            for (int v = 0; v < syl; ++v) if (vidx[(size_t) v] > prefixUnit) { stressV = v; break; }
    }

    std::vector<PhTok> out;
    auto add = [&] (const char* id, uint8_t st = 0) {
        PhTok t { P (id), st, 0 };
        if (! std::strcmp (id, "p") || ! std::strcmp (id, "t") || ! std::strcmp (id, "k")) t.flags |= kFlagNoAsp;
        out.push_back (t);
    };

    for (int k = 0; k < (int) us.size(); ++k)
    {
        const U& u = us[(size_t) k];
        const bool atEnd = k == (int) us.size() - 1;
        const U* next = atEnd ? nullptr : &us[(size_t) k + 1];
        if (u.vowel)
        {
            int vi = 0; for (int v = 0; v < syl; ++v) if (vidx[(size_t) v] == k) vi = v;
            uint8_t st = vi == stressV ? 1 : 0;
            int follow = 0; bool lastVowel = true;
            for (int j = k + 1; j < (int) us.size(); ++j) { if (us[(size_t) j].vowel) { lastVowel = false; break; } follow += us[(size_t) j].letters; }
            const bool open = (follow == 0) || (follow == 1 && ! lastVowel);
            std::string tail; for (int j = k + 1; j < (int) us.size(); ++j) tail += us[(size_t) j].g;
            const std::string& g = u.g;
            if (g == "e")
            {
                bool schwa = (k == prefixUnit && prefixSchwa);
                if (! schwa && st == 0 && lastVowel && syl >= 2)
                    schwa = tail.empty() || tail == "n" || tail == "l" || tail == "r" || tail == "m" || tail == "s" || tail == "nd"
                         || tail == "ns" || tail == "ls" || tail == "rs" || tail == "nt" || tail == "rd" || tail == "rt" || tail == "lt";
                if (! schwa && syl == 1 && tail.empty()) schwa = true;
                if (schwa) { add ("ə"); continue; }
                add (open ? "eː" : "ɛ", st); continue;
            }
            if (g == "a") { add (open ? "aː" : "ɑ", st); continue; }
            if (g == "o") { add (open ? "oː" : "ɔ", st); continue; }
            if (g == "u") { add (open ? "y" : "ʏ", st); continue; }
            if (g == "i")
            {
                if (lastVowel && syl >= 2 && st == 0 && tail == "g") { add ("ə"); continue; }      // -ig
                add (open ? "i" : "ɪ", st); continue;
            }
            if (g == "y") { add ("i", st); continue; }
            if (g == "ij" && lastVowel && syl >= 2 && st == 0 && k > 0 && us[(size_t) k - 1].g == "l" && tail == "k") { add ("ə"); continue; } // -lijk
            if (g == "aa") { add ("aː", st); continue; }
            if (g == "ee") { add ("eː", st); continue; }
            if (g == "oo") { add ("oː", st); continue; }
            if (g == "uu") { add ("y", st); continue; }
            if (g == "ie") { add ("i", st); continue; }
            if (g == "oe") { add ("u", st); continue; }
            if (g == "eu") { add ("øː", st); continue; }
            if (g == "ui") { add ("œy", st); continue; }
            if (g == "ij" || g == "ei") { add ("ɛi", st); continue; }
            if (g == "ou" || g == "au" || g == "ouw" || g == "auw") { add ("ʌu", st); continue; }
            if (g == "eeuw") { add ("eː", st); add ("ʋ"); continue; }
            if (g == "ieuw") { add ("i", st); add ("ʋ"); continue; }
            if (g == "uw") { add ("y", st); add ("ʋ"); continue; }
            if (g == "aai") { add ("aː", st); add ("j"); continue; }
            if (g == "ooi") { add ("oː", st); add ("j"); continue; }
            if (g == "oei") { add ("u", st); add ("j"); continue; }
            if (g == "oi") { add ("ɔ", st); add ("j"); continue; }
            continue;
        }
        const std::string& g = u.g;
        const bool nextVowel = next && next->vowel;
        if (g == "sch") { if (atEnd) add ("s"); else { add ("s"); add ("x"); } continue; }
        if (g == "ch") { add ("x"); continue; }
        if (g == "ng") { add ("ŋ"); continue; }
        if (g == "nk") { add ("ŋ"); add ("k"); continue; }
        if (g == "tj") { add ("tʃ"); continue; }
        if (g == "sj") { add ("ʃ"); continue; }
        if (g == "nj") { if (nextVowel && k > 0 && next->g == "e" && k + 2 == (int) us.size()) add ("ɲ"); else { add ("n"); add ("j"); } continue; }
        if (g == "ph") { add ("f"); continue; }
        if (g == "th") { add ("t"); continue; }
        if (g == "qu") { add ("k"); add ("ʋ"); continue; }
        if (g == "ck" || g == "dt") { add (g == "ck" ? "k" : "t"); continue; }
        switch (g[0])
        {
            case 'b': add ("b"); break;
            case 'c':
            {
                char nc = (size_t) u.pos + 1 < w.size() ? w[(size_t) u.pos + 1] : 0;
                add (nc == 'e' || nc == 'i' || nc == 'y' ? "s" : "k"); break;
            }
            case 'd': add ("d"); break;
            case 'f': add ("f"); break;
            case 'g': add ("ɣ"); break;
            case 'h': add ("h"); break;
            case 'j': add ("j"); break;
            case 'k': add ("k"); break;
            case 'l': add ("l"); break;
            case 'm': add ("m"); break;
            case 'n': add ("n"); break;
            case 'p': add ("p"); break;
            case 'q': add ("k"); break;
            case 'r': add (nextVowel ? "ʀ" : "ɻ"); break;
            case 's': add ("s"); break;
            case 't':
                // -tie after a vowel: natie, politie → ts
                if (next && next->g == "ie" && k + 2 == (int) us.size() && k > 0 && us[(size_t) k - 1].vowel && syl >= 2) { add ("t"); add ("s"); }
                else add ("t");
                break;
            case 'v': add ("v"); break;
            case 'w': add ("ʋ"); break;
            case 'x': add ("k"); add ("s"); break;
            case 'z': add ("z"); break;
            default: break;
        }
    }

    // n-deletion in -en (multi-syllable)
    if (out.size() >= 3 && syl >= 2 && phon (out.back().ph).id == std::string ("n") && phon (out[out.size() - 2].ph).id == std::string ("ə"))
        out.pop_back();

    // voicing: regressive assimilation before voiceless obstruents + final devoicing
    auto devoice = [] (int ph) -> int {
        const std::string id = phon (ph).id;
        if (id == "b") return P ("p"); if (id == "d") return P ("t"); if (id == "v") return P ("f");
        if (id == "z") return P ("s"); if (id == "ɣ") return P ("x"); if (id == "dʒ") return P ("tʃ"); if (id == "ʒ") return P ("ʃ");
        return ph;
    };
    auto voicelessObs = [] (int ph) { const Phon& p = phon (ph); return ! p.voiced && (p.type == PT::Stop || p.type == PT::Fric || p.type == PT::Affr); };
    for (int i = (int) out.size() - 1; i >= 0; --i)
    {
        const PT ty = phon (out[(size_t) i].ph).type;
        if (! (ty == PT::Stop || ty == PT::Fric || ty == PT::Affr)) break;
        out[(size_t) i].ph = devoice (out[(size_t) i].ph);
    }
    for (int i = 0; i + 1 < (int) out.size(); ++i)
        if (voicelessObs (out[(size_t) i + 1].ph)) out[(size_t) i].ph = devoice (out[(size_t) i].ph);
    for (auto& t : out) { const std::string id = phon (t.ph).id; if (id == "p" || id == "t" || id == "k") t.flags |= kFlagNoAsp; }
    return out;
}

// ---------------------------------------------------------------- public
std::vector<PhTok> G2P::word (const std::string& w, Lang lang, bool* known)
{
    std::string lw = lower (w);
    // strip apostrophes at edges (keep 's)
    while (! lw.empty() && (lw.back() == '\'')) lw.pop_back();
    if (lang == Lang::EN) return enWord (lw, known);
    if (known) *known = true;
    return nlWord (lw);
}

std::vector<PhTok> G2P::letter (char c, Lang lang)
{
    c = (char) std::tolower ((unsigned char) c);
    if (c < 'a' || c > 'z') return {};
    static const char* en[] = { "ˈeɪ", "b ˈiː", "s ˈiː", "d ˈiː", "ˈiː", "ˈɛ f", "dʒ ˈiː", "ˈeɪ tʃ", "ˈaɪ", "dʒ ˈeɪ", "k ˈeɪ", "ˈɛ l",
        "ˈɛ m", "ˈɛ n", "ˈoʊ", "p ˈiː", "k j ˈuː", "ˈɑː ɹ", "ˈɛ s", "t ˈiː", "j ˈuː", "v ˈiː", "d ˈʌ b ə l j uː", "ˈɛ k s", "w ˈaɪ", "z ˈiː" };
    static const char* nl[] = { "ˈaː", "b ˈeː", "s ˈeː", "d ˈeː", "ˈeː", "ˈɛ f", "ɣ ˈeː", "h ˈaː", "ˈi", "j ˈeː", "k ˈaː", "ˈɛ l",
        "ˈɛ m", "ˈɛ n", "ˈoː", "p ˈeː", "k ˈy", "ˈɛ ʀ", "ˈɛ s", "t ˈeː", "ˈy", "v ˈeː", "ʋ ˈeː", "ˈɪ k s", "ˈɛi", "z ˈɛ t" };
    return parseIpa (lang == Lang::EN ? en[c - 'a'] : nl[c - 'a'], lang == Lang::NL);
}

std::vector<WordPh> G2P::text (const std::string& txt, Lang lang)
{
    preload();
    std::vector<WordPh> out;
    std::u32string s = utf8to32 (txt);
    std::u32string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        std::string w8; for (char32_t c : cur) w8 += utf32to8 (c == 0x2019 ? U'\'' : c);
        cur.clear();
        // digits → number words
        bool digits = ! w8.empty(); for (char c : w8) digits &= (c >= '0' && c <= '9');
        if (digits && w8.size() < 7)
        {
            std::string nw = numberWords (std::stoi (w8), lang);
            for (auto& sub : text (nw, lang)) out.push_back (sub);
            return;
        }
        WordPh wp; wp.word = w8;
        const std::string lw = lower (w8);
        wp.function = lang == Lang::EN ? kEnFunction.count (lw) > 0 : kNlFunction.count (lw) > 0;
        wp.ph = word (w8, lang);
        if (! wp.ph.empty()) out.push_back (wp);
    };
    for (char32_t c : s)
    {
        if (isWordCp (c)) { cur.push_back (c); continue; }
        flush();
        int pause = 0; bool q = false;
        if (c == ',' || c == ';' || c == ':' || c == 0x2014 || c == '(' || c == ')') pause = 170;
        else if (c == '.' || c == '!' || c == '\n') pause = 320;
        else if (c == '?') { pause = 320; q = true; }
        if (pause && ! out.empty() && out.back().pauseMs == 0)
        {
            WordPh p; p.pauseMs = pause; p.question = q; p.word = std::string (1, (char) (c < 128 ? c : ' '));
            out.push_back (p);
        }
    }
    flush();
    return out;
}

int unknownPhonemeLookups() { return gUnknown.load(); }

std::string phonString (const std::vector<PhTok>& p)
{
    std::string s;
    for (auto& t : p) { if (t.stress == 1) s += "ˈ"; s += phon (t.ph).id; s += ' '; }
    if (! s.empty()) s.pop_back();
    return s;
}

} // namespace sm
