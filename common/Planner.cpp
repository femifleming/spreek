#include "Planner.h"
#include "Phonemes.h"
#include <cstring>

namespace sm {

// ------------------------------------------------------------------ prosody
void buildUtterance (const std::vector<WordPh>& words, Utterance& u, const std::string& text)
{
    u.n = 0; u.nWords = 0; u.totalMs = 0;
    std::snprintf (u.text, sizeof (u.text), "%s", text.c_str());
    auto push = [&] (const Seg& s) { if (u.n < kMaxSegs) u.seg[u.n++] = s; };

    Seg lead; lead.ph = (int16_t) silIndex(); lead.dur = 25; push (lead);

    // phrase spans for declination / final lengthening / question rise
    for (size_t w = 0; w < words.size(); ++w)
    {
        const WordPh& wp = words[w];
        if (wp.pauseMs > 0)
        {
            Seg s; s.ph = (int16_t) silIndex(); s.dur = (float) wp.pauseMs; s.word = (uint8_t) juce::jmax (0, u.nWords - 1);
            push (s);
            continue;
        }
        if (u.nWords >= kMaxWords) break;
        const int wi = u.nWords++;
        std::snprintf (u.words[wi], kWordChars, "%s", wp.word.c_str());
        bool first = true;
        for (const PhTok& t : wp.ph)
        {
            const Phon& p = phon (t.ph);
            Seg s; s.ph = (int16_t) t.ph; s.word = (uint8_t) wi;
            s.stress = wp.function ? 0 : t.stress;
            s.flags = (uint8_t) ((first ? kSegWordStart : 0) | ((t.flags & kFlagNoAsp) ? kSegNoAsp : 0));
            float d = p.dur;
            if (isVowelType (p.type))
            {
                if (wp.function) d *= 0.7f;
                else if (t.stress == 1) d *= 1.25f;
                else if (t.stress == 2) d *= 1.0f;
                else d *= 0.8f;
            }
            s.dur = d;
            first = false;
            push (s);
        }
    }
    Seg tail; tail.ph = (int16_t) silIndex(); tail.dur = 40; push (tail);

    // phrases: split at pauses; final lengthening + intonation
    int start = 0;
    while (start < u.n)
    {
        int end = start;
        while (end < u.n && ! (u.seg[end].ph == silIndex() && u.seg[end].dur >= 150 && end > start)) ++end;
        // [start, end) is a phrase; is it a question?
        bool question = false;
        {
            // look up the pause word that ended this phrase
            int pw = 0;
            for (size_t w = 0; w < words.size(); ++w)
            {
                if (words[w].pauseMs > 0) { if (pw == 0 && end < u.n) {} }
            }
            juce::ignoreUnused (pw);
        }
        // detect '?' from the text: if the phrase is last and text ends with ?, or a '?' pause follows
        {
            int phraseIdx = 0, targetIdx = 0;
            for (int i = 0; i < start; ++i) if (u.seg[i].ph == silIndex() && u.seg[i].dur >= 150 && i > 0) ++targetIdx;
            for (const auto& wp : words)
                if (wp.pauseMs >= 150) { if (phraseIdx == targetIdx) { question = wp.question; break; } ++phraseIdx; }
        }
        // last vowel of the phrase + trailing consonants lengthen
        int lastV = -1, lastAcc = -1, firstAcc = -1;
        for (int i = start; i < end; ++i)
        {
            const Phon& p = phon (u.seg[i].ph);
            if (isVowelType (p.type)) lastV = i;
            if (isVowelType (p.type) && u.seg[i].stress == 1) { lastAcc = i; if (firstAcc < 0) firstAcc = i; }
        }
        if (lastV >= 0)
            for (int i = lastV; i < end; ++i)
                if (u.seg[i].ph != silIndex()) { u.seg[i].dur *= (i == lastV ? 1.35f : 1.15f); u.seg[i].flags |= kSegFinal; }
        // time positions for declination
        float T = 0; for (int i = start; i < end; ++i) T += u.seg[i].dur;
        float t = 0;
        for (int i = start; i < end; ++i)
        {
            Seg& s = u.seg[i];
            const float da = 2.0f - 4.0f * (T > 0 ? t / T : 0);
            t += s.dur;
            const float db = 2.0f - 4.0f * (T > 0 ? t / T : 0);
            s.f0a = da; s.f0b = db;
            const Phon& p = phon (s.ph);
            if (isVowelType (p.type) && s.stress == 1)
            {
                if (i == lastAcc && question) { s.f0a += 1.0f; s.f0b += 7.0f; }
                else if (i == lastAcc) { s.f0a += 3.5f; s.f0b -= 2.5f; }
                else if (i == firstAcc) { s.f0a += 4.5f; s.f0b += 3.5f; }
                else { s.f0a += 3.0f; s.f0b += 2.0f; }
            }
            else if (question && i > lastAcc && lastAcc >= 0) { s.f0a += 7.0f; s.f0b += 8.0f; }
        }
        start = end + 1;
    }
    for (int i = 0; i < u.n; ++i) u.totalMs += u.seg[i].dur;
}

// ------------------------------------------------------------------ planner
Planner::Planner (juce::File dataDir, juce::File legacyDir) : juce::Thread ("Spreekbuis planner"), dir (dataDir), legacy (legacyDir)
{
    dir.createDirectory();
    ensureLists();
}

Planner::~Planner() { stopThread (2000); }

void Planner::ensureLists()
{
    auto ld = listsDir();
    if (! ld.isDirectory() && legacy != juce::File() && legacy.getChildFile ("lists").isDirectory())
    {
        // the lists somebody wrote in Spraakmaker 1.0 come along (copied, the old folder stays as it is)
        ld.createDirectory();
        for (auto& f : legacy.getChildFile ("lists").findChildFiles (juce::File::findFiles, false, "list*.txt")) f.copyFileTo (ld.getChildFile (f.getFileName()));
    }
    if (! ld.isDirectory())
    {
        ld.createDirectory();
        const char* defaults[] = {
            "Hallo!\nGoedemorgen.\nHoe gaat het?\nDank je wel.\nTot straks.\nDoei!",
            "één\ntwee\ndrie\nvier\nvijf\nzes\nzeven\nacht\nnegen\ntien",
            "de synthesizer\nhet geluid\nde kabel\nde luidspreker\nhet ritme\nde toon\nde stem\nde droom",
            "Ik ben een spraakmaker.\nIk spreek een beetje Nederlands.\nWaar is het station?\nMag ik een koffie?\nHet regent in Rotterdam.\nIk droom van de zee.",
            "I am a speech computer.\nHello world.\nDo angels fly through clouds during lunar thunderstorms?\nAstral internet data ambiance.\nThe ground beneath my feet.",
            "zero\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine",
        };
        for (int i = 0; i < 6; ++i) ld.getChildFile (juce::String::formatted ("list%02d.txt", i + 1)).replaceWithText (juce::String::fromUTF8 (defaults[i]));
    }
    std::lock_guard<std::mutex> l (listMutex);
    for (int i = 1; i <= 99; ++i)
    {
        auto f = ld.getChildFile (juce::String::formatted ("list%02d.txt", i));
        juce::StringArray a;
        if (f.existsAsFile()) { a.addLines (f.loadFileAsString()); a.removeEmptyStrings(); a.trim(); }
        lists[i] = a;
    }
}

juce::StringArray Planner::getList (int n) const
{
    std::lock_guard<std::mutex> l (listMutex);
    auto it = lists.find (n);
    return it == lists.end() ? juce::StringArray() : it->second;
}

void Planner::setList (int n, const juce::StringArray& entries)
{
    {
        std::lock_guard<std::mutex> l (listMutex);
        lists[n] = entries;
    }
    listsDir().getChildFile (juce::String::formatted ("list%02d.txt", n)).replaceWithText (entries.joinIntoString ("\n"));
    gen.fetch_add (1);
}

void Planner::setBankTexts (std::shared_ptr<const BankTexts> t)
{
    { std::lock_guard<std::mutex> l (bankMutex); bankTexts = std::move (t); }
    bankVersion.fetch_add (1);
}

void Planner::setKeyboardText (const juce::String& t) { std::lock_guard<std::mutex> l (textMutex); if (t != keyboardText) kbVersion.fetch_add (1); keyboardText = t; }
juce::String Planner::getKeyboardText() const { std::lock_guard<std::mutex> l (textMutex); return keyboardText; }

void Planner::speakNow (const juce::String& t)
{
    setKeyboardText (t);
    std::lock_guard<std::mutex> l (urgentMutex);
    urgentText = t;
    urgentRequested.store (true);
    notify();
}

bool Planner::popUrgent (Utterance& out)
{
    int expected = 2;
    if (! urgentState.compare_exchange_strong (expected, 3)) return false;
    std::memcpy (&out, &urgent, sizeof (Utterance));
    urgentState.store (0);
    urgentRequested.store (false);
    return true;
}

bool Planner::popItem (Utterance& out, int item)
{
    if (itemMade.load() != item) return false;
    int expected = 2;
    if (! itemState.compare_exchange_strong (expected, 3)) return false;
    const bool ok = itemMade.load() == item;
    if (ok) std::memcpy (&out, &itemUtt, sizeof (Utterance));
    itemState.store (2);      // it stays ready: the same item may be asked for again
    return ok;
}

bool Planner::pop (Utterance& out)
{
    while (fifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        bool ok = false;
        if (n1 > 0)
        {
            const Utterance& u = slots[(size_t) s1];
            if (u.gen == gen.load() && u.key == settingsKey()) { std::memcpy (&out, &u, sizeof (Utterance)); ok = true; }
        }
        fifo.finishedRead (n1);
        if (ok) return true;
    }
    return false;
}

void Planner::discardStale()
{
    while (fifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 > 0 && slots[(size_t) s1].gen == gen.load() && slots[(size_t) s1].key == settingsKey()) { return; }   // front is fresh (prepareToRead without finish is fine)
        fifo.finishedRead (n1);
    }
}

void Planner::setSynchronous (bool on)
{
    if (on)
    {
        signalThreadShouldExit(); notify(); stopThread (4000);
        // forget whatever the thread did before it was stopped (how much depends on timing): queue, generation,
        // settings memory and the random source start from the same state in every run
        fifo.reset(); gen.store (1);
        lastLang = lastSource = lastList = lastOrder = lastLesSet = -1; lastBankVersion = bankVersion.load(); lastKeyb = {}; listPos = alphaPos = 0;
        { std::lock_guard<std::mutex> l (urgentMutex); urgentText = {}; }
        itemWanted.store (-1); itemMade.store (-1); itemState.store (0); itemLang = -1;
        urgentState.store (0); urgentRequested.store (false);
        rng.seed (1234567u);
    }
    synchronous.store (on);
    if (! on && ! isThreadRunning()) startPlanner();
}

void Planner::run()
{
    while (! threadShouldExit())
    {
        { std::lock_guard<std::mutex> l (stepMutex); step (false); }
        wait (15);
    }
}

void Planner::step (bool sync)
{
    G2P::get().preload();
    auto checkSettings = [this]() -> int {
        const int L = lang.load(), S = source.load(), N = listNum.load(), O = order.load(), LS = lesSet.load();
        const uint32_t BV = bankVersion.load();
        const juce::String kb = getKeyboardText();
        if (L != lastLang || S != lastSource || N != lastList || O != lastOrder || (S == (int) Source::Keyb && kb != lastKeyb)
            || (S == (int) Source::Les && (LS != lastLesSet || BV != lastBankVersion)))
        {
            lastLang = L; lastSource = S; lastList = N; lastOrder = O; lastKeyb = kb; lastLesSet = LS; lastBankVersion = BV;
            listPos = 0; alphaPos = 0;
            rng.seed (1234567u);     // the same settings say the same things in the same order (a bounce repeats)
            gen.fetch_add (1);
        }
        return L;
    };
    const int L = checkSettings();
    // urgent text (typed, Enter). A newer text replaces one that was made ready but never spoken.
    juce::String ut;
    {
        std::lock_guard<std::mutex> l (urgentMutex);
        ut = urgentText; urgentText = {};
    }
    if (ut.isNotEmpty())
    {
        for (;;)
        {
            int expected = 0;
            if (urgentState.compare_exchange_strong (expected, 1)) break;
            expected = 2;
            if (urgentState.compare_exchange_strong (expected, 1)) break;
            if (sync || threadShouldExit()) return;     // (state 3: the audio thread is copying; never when called inline)
            wait (1);
        }
        auto words = G2P::get().text (ut.toStdString(), (Lang) L);
        buildUtterance (words, urgent, ut.toStdString());
        urgent.gen = gen.load();
        urgentState.store (2);
    }
    // one item of the word bank, asked for by the audio thread (the lesson loop's echo)
    if (const int want = itemWanted.load(); want >= 0 && (want != itemMade.load() || L != itemLang || bankVersion.load() != itemBankVersion))
    {
        const uint32_t bv = bankVersion.load();
        std::string text;
        if (auto bt = getBankTexts(); bt && want < (int) bt->items.size())
            text = (L == (int) Lang::EN && ! bt->items[(size_t) want].second.empty()) ? bt->items[(size_t) want].second : bt->items[(size_t) want].first;
        if (! text.empty())
        {
            bool mine = false;
            for (int tries = 0; tries < 50 && ! mine; ++tries)
            {
                int expected = 0;
                if (itemState.compare_exchange_strong (expected, 1)) { mine = true; break; }
                expected = 2;
                if (itemState.compare_exchange_strong (expected, 1)) { mine = true; break; }
                if (sync || threadShouldExit()) break;
                wait (1);
            }
            if (mine)
            {
                buildUtterance (G2P::get().text (text, (Lang) L), itemUtt, text);
                itemLang = L; itemBankVersion = bv;
                itemMade.store (want);
                itemState.store (2);
            }
        }
    }
    while (fifo.getFreeSpace() > 0 && (sync || ! threadShouldExit()))
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 <= 0) break;
        makeNext (slots[(size_t) s1]);
        fifo.finishedWrite (n1);
        const uint32_t before = gen.load();
        checkSettings();
        if (gen.load() != before) break;     // settings moved on: stop filling with stale items
    }
}

void Planner::ensureMarkov (Lang l)
{
    const int li = (int) l;
    if (markovBuilt[li]) return;
    markovBuilt[li] = true;
    const std::string& c = G2P::get().corpus (l);
    std::vector<std::string> sent;
    std::string w;
    std::string prev = "^";
    auto emit = [&] (bool end) {
        if (! w.empty())
        {
            markov[li][prev].push_back (w);
            if (prev == "^") markovStarts[li].push_back (w);
            prev = w; w.clear();
        }
        if (end && prev != "^") { markov[li][prev].push_back ("$"); prev = "^"; }
    };
    for (size_t i = 0; i < c.size(); ++i)
    {
        const unsigned char ch = (unsigned char) c[i];
        if (ch == 0xE2 && i + 2 < c.size() && (unsigned char) c[i + 1] == 0x80) { i += 2; emit (false); continue; }  // typographic quotes/dashes
        if (std::isalpha (ch) || ch >= 0x80 || (ch == '\'' && ! w.empty())) w += (char) std::tolower (ch);
        else if (ch == '.' || ch == '!' || ch == '?' || ch == '\n') emit (true);
        else emit (false);
    }
    emit (true);
}

std::string Planner::markovPhrase (Lang l)
{
    ensureMarkov (l);
    const int li = (int) l;
    if (markovStarts[li].empty()) return "hallo";
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        std::string w = markovStarts[li][rng() % markovStarts[li].size()];
        std::string out = w;
        int count = 1;
        while (count < 14)
        {
            auto it = markov[li].find (w);
            if (it == markov[li].end() || it->second.empty()) break;
            const std::string& nx = it->second[rng() % it->second.size()];
            if (nx == "$") break;
            out += " " + nx; w = nx; ++count;
        }
        if (count >= 3 || attempt == 11)
        {
            out[0] = (char) std::toupper ((unsigned char) out[0]);
            return out + ".";
        }
    }
    return "hallo";
}

std::string Planner::lettrism (Lang l, std::vector<WordPh>& out)
{
    static const char* nlV[] = { "ɑ", "aː", "ɛ", "eː", "ɪ", "i", "ɔ", "oː", "ʏ", "y", "øː", "u", "ə", "ɛi", "œy", "ʌu" };
    static const char* nlC[] = { "p", "b", "t", "d", "k", "f", "v", "s", "z", "x", "ɣ", "h", "m", "n", "ŋ", "l", "ʀ", "ʋ", "j" };
    static const char* enV[] = { "æ", "ʌ", "ɑː", "ɔː", "ɛ", "ɝ", "ɪ", "iː", "ʊ", "uː", "eɪ", "aɪ", "aʊ", "oʊ", "ɔɪ", "ə" };
    static const char* enC[] = { "p", "b", "t", "d", "k", "g", "f", "v", "θ", "ð", "s", "z", "ʃ", "h", "m", "n", "ŋ", "l", "ɹ", "w", "j", "tʃ", "dʒ" };
    const bool nl = l == Lang::NL;
    const char* const* V = nl ? nlV : enV; const int nv = 16;
    const char* const* C = nl ? nlC : enC; const int nc = nl ? 19 : 23;
    std::string text;
    const int nwords = 1 + (int) (rng() % 4);
    for (int w = 0; w < nwords; ++w)
    {
        WordPh wp;
        const int syl = 1 + (int) (rng() % 3);
        const int stressSyl = (int) (rng() % (unsigned) syl);
        for (int s = 0; s < syl; ++s)
        {
            if (rng() % 4) wp.ph.push_back ({ findPhon (C[rng() % (unsigned) nc]), 0, (uint8_t) (nl ? kFlagNoAsp : 0) });
            wp.ph.push_back ({ findPhon (V[rng() % nv]), (uint8_t) (s == stressSyl ? 1 : 0), 0 });
            if (rng() % 3 == 0) wp.ph.push_back ({ findPhon (C[rng() % (unsigned) nc]), 0, 0 });
        }
        wp.word = phonString (wp.ph);
        text += (w ? " " : "") + wp.word;
        out.push_back (wp);
    }
    return text;
}

void Planner::makeNext (Utterance& u)
{
    const uint32_t key = settingsKey();   // taken before the settings are read, so a change mid-way makes it stale
    const Lang L = (Lang) juce::jlimit (0, 1, lang.load());
    const Source S = (Source) juce::jlimit (0, 6, source.load());
    std::string text;
    std::vector<WordPh> words;
    switch (S)
    {
        case Source::Dict:
        {
            const auto& d = G2P::get().dictWords (L);
            text = d.empty() ? "hallo" : d[rng() % d.size()];
            break;
        }
        case Source::Alpha:
        {
            const int i = order.load() == 1 ? (int) (rng() % 26) : alphaPos++ % 26;
            const char c = (char) ('a' + i);
            WordPh wp; wp.word = std::string (1, (char) std::toupper (c)); wp.ph = G2P::get().letter (c, L);
            words.push_back (wp); text = wp.word;
            break;
        }
        case Source::Keyb:
            text = getKeyboardText().toStdString();
            if (text.empty()) text = L == Lang::NL ? "typ iets" : "type something";
            break;
        case Source::Markov: text = markovPhrase (L); break;
        case Source::Lettr: text = lettrism (L, words); break;
        case Source::List:
        {
            auto entries = getList (juce::jlimit (1, 99, listNum.load()));
            if (entries.isEmpty()) { text = L == Lang::NL ? "lege lijst" : "empty list"; break; }
            const int i = order.load() == 1 ? (int) (rng() % (unsigned) entries.size()) : listPos++ % entries.size();
            text = entries[i].toStdString();
            break;
        }
        case Source::Les:
        {
            // the word bank's lesson (the list the loop plays), in order or shuffled; Dutch, or the English gloss
            auto bt = getBankTexts();
            const std::vector<int>* list = nullptr; std::vector<int> all;
            if (bt)
            {
                list = &bt->lesson[juce::jlimit (0, 3, lesSet.load())];
                if (list->empty()) list = &bt->lesson[3];
                if (list->empty()) { all.resize (bt->items.size()); for (size_t k = 0; k < all.size(); ++k) all[k] = (int) k; list = &all; }
            }
            if (list == nullptr || list->empty()) { text = L == Lang::NL ? "geen woorden" : "no words"; break; }
            const int i = order.load() == 1 ? (int) (rng() % (unsigned) list->size()) : listPos++ % (int) list->size();
            const auto& it = bt->items[(size_t) (*list)[(size_t) i]];
            text = (L == Lang::EN && ! it.second.empty()) ? it.second : it.first;
            break;
        }
    }
    if (words.empty()) words = G2P::get().text (text, L);
    buildUtterance (words, u, text);
    u.gen = gen.load();
    u.key = key;
}

} // namespace sm
