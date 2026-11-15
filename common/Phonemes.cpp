#include "Phonemes.h"
#include <vector>
#include <unordered_map>

namespace sm {

namespace {
Phon V (const char* id, float f1, float f2, float f3, float dur, float bw = 1.0f)
{
    return { id, PT::Vowel, true, f1, f2, f3, f1, f2, f3, 70 * bw, 90 * bw, 140 * bw, dur,
             0, 1, 0, 0, 1, 0, 0, 0, 0, 0, false };
}
Phon D (const char* id, float f1, float f2, float f3, float e1, float e2, float e3, float dur)
{
    return { id, PT::Diph, true, f1, f2, f3, e1, e2, e3, 70, 90, 140, dur,
             0, 1, 0, 0, 1, 0, 0, 0, 0, 0, false };
}
// stop: locus formants, burst noise filters
Phon S (const char* id, bool voiced, float f1, float f2, float f3,
        float n1f, float n1b, float n1g, float n2f, float n2b, float n2g, float nby, bool asp)
{
    return { id, PT::Stop, voiced, f1, f2, f3, f1, f2, f3, 90, 120, 180, voiced ? 70.f : 85.f,
             n1f, n1b, n1g, n2f, n2b, n2g, nby, 1.0f, 0, 0, asp };
}
Phon F (const char* id, bool voiced, float f1, float f2, float f3, float dur,
        float n1f, float n1b, float n1g, float n2f, float n2b, float n2g, float nby, float namp)
{
    return { id, PT::Fric, voiced, f1, f2, f3, f1, f2, f3, 100, 140, 200, dur,
             n1f, n1b, n1g, n2f, n2b, n2g, nby, namp, 0, 0, false };
}
Phon N (const char* id, float f1, float f2, float f3, float nz)
{
    return { id, PT::Nasal, true, f1, f2, f3, f1, f2, f3, 100, 200, 250, 70,
             0, 1, 0, 0, 1, 0, 0, 0, 1.0f, nz, false };
}
Phon L (const char* id, PT t, float f1, float f2, float f3, float dur)
{
    return { id, t, true, f1, f2, f3, f1, f2, f3, 90, 110, 160, dur,
             0, 1, 0, 0, 1, 0, 0, 0, 0, 0, false };
}

std::vector<Phon> buildTable()
{
    std::vector<Phon> t;
    // silence / pause
    t.push_back ({ "_", PT::Sil, false, 450, 1400, 2500, 450, 1400, 2500, 200, 200, 200, 160,
                   0, 1, 0, 0, 1, 0, 0, 0, 0, 0, false });
    // ---- Dutch monophthongs (Adank et al. 2004, male averages, rounded) ----
    t.push_back (V ("ɑ", 680, 1110, 2550, 85));
    t.push_back (V ("aː", 790, 1320, 2500, 150));
    t.push_back (V ("ɛ", 560, 1750, 2550, 85));
    t.push_back (D ("eː", 420, 2000, 2600, 340, 2180, 2700, 150));
    t.push_back (V ("ɪ", 400, 1950, 2600, 75));
    t.push_back (V ("i", 290, 2250, 3000, 90));
    t.push_back (V ("ɔ", 530, 900, 2500, 85));
    t.push_back (D ("oː", 460, 860, 2450, 370, 760, 2400, 150));
    t.push_back (V ("ʏ", 410, 1500, 2300, 80));
    t.push_back (V ("y", 280, 1700, 2150, 95));
    t.push_back (D ("øː", 410, 1450, 2250, 350, 1600, 2250, 150));
    t.push_back (V ("u", 320, 760, 2400, 95));
    t.push_back (V ("ə", 470, 1450, 2500, 50, 1.2f));
    t.push_back (D ("ɛi", 560, 1750, 2550, 330, 2200, 2800, 175));
    t.push_back (D ("œy", 520, 1450, 2350, 330, 1750, 2250, 175));
    t.push_back (D ("ʌu", 620, 1100, 2450, 360, 800, 2400, 175));
    // ---- English vowels (Peterson & Barney / Hillenbrand, male) ----
    t.push_back (V ("æ", 660, 1720, 2410, 130));
    t.push_back (V ("ʌ", 620, 1200, 2390, 90));
    t.push_back (V ("ɑː", 730, 1090, 2440, 140));
    t.push_back (V ("ɔː", 570, 840, 2410, 140));
    t.push_back (V ("ɝ", 490, 1350, 1690, 130));
    t.push_back (V ("iː", 270, 2290, 3010, 120));
    t.push_back (V ("ʊ", 440, 1020, 2240, 80));
    t.push_back (V ("uː", 300, 870, 2240, 130));
    t.push_back (D ("eɪ", 480, 1900, 2550, 330, 2200, 2800, 160));
    t.push_back (D ("aɪ", 700, 1200, 2500, 360, 2100, 2700, 180));
    t.push_back (D ("aʊ", 700, 1200, 2500, 400, 850, 2400, 180));
    t.push_back (D ("oʊ", 520, 900, 2400, 380, 800, 2350, 160));
    t.push_back (D ("ɔɪ", 550, 850, 2450, 350, 2100, 2700, 190));
    // ---- stops: locus formants + burst spectra ----
    t.push_back (S ("p", false, 250, 850, 2200, 900, 1400, 0.6f, 2500, 2000, 0.3f, 0.35f, true));
    t.push_back (S ("b", true, 250, 850, 2200, 900, 1400, 0.5f, 2500, 2000, 0.2f, 0.25f, false));
    t.push_back (S ("t", false, 250, 1700, 2600, 4200, 2000, 1.0f, 6500, 2500, 0.5f, 0.1f, true));
    t.push_back (S ("d", true, 250, 1700, 2600, 4000, 2000, 0.8f, 6000, 2500, 0.4f, 0.1f, false));
    t.push_back (S ("k", false, 250, 1900, 2350, 1900, 700, 1.0f, 3000, 1000, 0.4f, 0.05f, true));
    t.push_back (S ("g", true, 250, 1900, 2350, 1800, 700, 0.8f, 3000, 1000, 0.3f, 0.05f, false));
    // affricates (stop + fricative release)
    {
        Phon c = S ("tʃ", false, 250, 1800, 2400, 2700, 600, 1.0f, 4500, 1500, 0.6f, 0.0f, false);
        c.type = PT::Affr; c.dur = 130; t.push_back (c);
        Phon j = S ("dʒ", true, 250, 1800, 2400, 2700, 600, 0.8f, 4500, 1500, 0.5f, 0.0f, false);
        j.type = PT::Affr; j.dur = 120; t.push_back (j);
    }
    // ---- fricatives ----
    t.push_back (F ("f", false, 300, 1100, 2300, 100, 7000, 3000, 0.25f, 3500, 3000, 0.1f, 0.30f, 0.55f));
    t.push_back (F ("v", true, 300, 1100, 2300, 70, 7000, 3000, 0.2f, 3500, 3000, 0.1f, 0.25f, 0.30f));
    t.push_back (F ("θ", false, 300, 1400, 2600, 100, 6000, 3000, 0.3f, 4000, 3000, 0.1f, 0.25f, 0.45f));
    t.push_back (F ("ð", true, 300, 1400, 2600, 60, 6000, 3000, 0.2f, 4000, 3000, 0.1f, 0.2f, 0.25f));
    t.push_back (F ("s", false, 300, 1600, 2600, 115, 5500, 1500, 1.0f, 8000, 2000, 0.6f, 0.0f, 0.75f));
    t.push_back (F ("z", true, 300, 1600, 2600, 85, 5500, 1500, 0.8f, 8000, 2000, 0.4f, 0.0f, 0.45f));
    t.push_back (F ("ʃ", false, 300, 1800, 2400, 115, 2700, 600, 1.0f, 4500, 1500, 0.6f, 0.0f, 0.75f));
    t.push_back (F ("ʒ", true, 300, 1800, 2400, 85, 2700, 600, 0.8f, 4500, 1500, 0.5f, 0.0f, 0.45f));
    t.push_back (F ("x", false, 320, 1350, 2400, 120, 1300, 450, 0.9f, 2500, 700, 0.6f, 0.05f, 0.70f));
    t.push_back (F ("ɣ", true, 320, 1350, 2400, 95, 1300, 450, 0.8f, 2500, 700, 0.5f, 0.05f, 0.50f));
    {
        Phon h = F ("h", false, 500, 1500, 2500, 70, 0, 1, 0, 0, 1, 0, 0, 0);
        h.type = PT::Asp; t.push_back (h);
    }
    // ---- nasals ----
    t.push_back (N ("m", 270, 1000, 2200, 450));
    t.push_back (N ("n", 270, 1500, 2550, 520));
    t.push_back (N ("ŋ", 270, 1950, 2400, 600));
    t.push_back (N ("ɲ", 270, 2000, 2800, 560));
    // ---- liquids / glides ----
    t.push_back (L ("l", PT::Liquid, 360, 1150, 2700, 65));
    t.push_back (L ("ɹ", PT::Liquid, 310, 1060, 1380, 65));
    t.push_back (L ("ɻ", PT::Liquid, 420, 1250, 1750, 70));     // Dutch post-vocalic (Gooise) r
    {
        Phon r = L ("ʀ", PT::Trill, 450, 1250, 2350, 70);         // Dutch uvular trill
        r.n1f = 1200; r.n1b = 800; r.n1g = 0.4f; r.namp = 0.12f; t.push_back (r);
        Phon rr = L ("r", PT::Trill, 420, 1400, 2400, 65);        // alveolar trill
        rr.namp = 0.06f; rr.n1f = 2500; rr.n1b = 1500; rr.n1g = 0.4f; t.push_back (rr);
    }
    t.push_back (L ("w", PT::Glide, 290, 610, 2150, 55));
    t.push_back (L ("ʋ", PT::Glide, 310, 1350, 2300, 55));
    t.push_back (L ("j", PT::Glide, 260, 2070, 3020, 55));
    return t;
}

const std::vector<Phon>& table()
{
    static const std::vector<Phon> t = buildTable();
    return t;
}
const std::unordered_map<std::string, int>& index()
{
    static const std::unordered_map<std::string, int> m = [] {
        std::unordered_map<std::string, int> r;
        for (int i = 0; i < (int) table().size(); ++i) r[table()[(size_t) i].id] = i;
        return r;
    }();
    return m;
}
} // namespace

int numPhons() { return (int) table().size(); }
const Phon& phon (int i) { return table()[(size_t) i]; }
int findPhon (const std::string& id) { auto it = index().find (id); return it == index().end() ? -1 : it->second; }
int silIndex() { return 0; }
bool isVowelType (PT t) { return t == PT::Vowel || t == PT::Diph; }

} // namespace sm
