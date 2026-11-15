// Headless test: phonemize + render English and Dutch phrases to WAV, check coverage and levels.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/G2P.h"
#include "../Source/Planner.h"
#include "../Source/Voice.h"
#include "../Source/Phonemes.h"
#include <BinaryData.h>
#include <iostream>
#include <set>

using namespace sm;

static void writeWav (const juce::File& f, const std::vector<float>& x, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto os = f.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), sr, 1, 24, {}, 0));
    juce::AudioBuffer<float> b (1, (int) x.size());
    b.copyFrom (0, 0, x.data(), (int) x.size());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static std::vector<float> render (const std::string& text, Lang lang, double sr, float f0, float* peakOut)
{
    Utterance u;
    buildUtterance (G2P::get().text (text, lang), u, text);
    Voice v; v.prepare (sr);
    v.start (u, 0.f);
    VoiceParams p; p.f0 = f0;
    std::vector<float> out;
    float buf[512];
    int guard = 0;
    while ((v.active() || out.empty()) && guard++ < 20000)
    {
        v.render (buf, 512, p, nullptr);
        out.insert (out.end(), buf, buf + 512);
    }
    float pk = 0; for (float s : out) pk = std::max (pk, std::fabs (s));
    if (peakOut) *peakOut = pk;
    return out;
}

int main (int argc, char** argv)
{
    juce::File outDir (argc > 1 ? argv[1] : "/tmp");
    outDir.createDirectory();
    G2P::get().preload();
    int fails = 0;
    if (argc > 3 && juce::String (argv[2]) == "say")
    {
        for (auto& w : juce::StringArray::fromTokens (argv[3], "|", ""))
        {
            float pk = 0; auto x = render (w.toStdString(), argc > 4 ? Lang::NL : Lang::EN, 48000, 110, &pk);
            std::cout << w << " : peak " << pk << "  " << phonString (G2P::get().word (w.toStdString(), argc > 4 ? Lang::NL : Lang::EN)) << "\n";
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[2]) == "thread")
    {
        Planner pl (outDir.getChildFile ("plannerdata"));
        pl.lang = 0; pl.source = 3;
        pl.startPlanner();
        for (int i = 0; i < 20; ++i)
        {
            juce::Thread::sleep (100);
            Utterance u;
            if (pl.pop (u)) std::cout << i << " popped: " << u.text << " segs " << u.n << "\n"; else std::cout << i << " none (gen " << pl.generation() << ")\n";
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[2]) == "scan")
    {
        for (int ph = 1; ph < numPhons(); ++ph)
        {
            for (int ctx = 0; ctx < 2; ++ctx)
            {
                std::vector<WordPh> w (1);
                w[0].word = "x";
                int a = findPhon ("aː");
                if (ctx == 0) w[0].ph = { { a, 1, 0 }, { ph, 0, 0 }, { a, 0, 0 } };
                else w[0].ph = { { ph, 1, 0 } };
                Utterance u; buildUtterance (w, u, "x");
                Voice v; v.prepare (48000); v.start (u, 0);
                VoiceParams p; p.f0 = 110;
                float buf[256], pk = 0; double ss = 0; int n = 0;
                while (v.active() && n < 48000 * 3) { v.render (buf, 256, p, nullptr); for (float x : buf) { pk = std::max (pk, std::fabs (x)); ss += x * x; } n += 256; }
                std::cout << phon (ph).id << (ctx ? " alone" : " in aː_aː") << " peak " << pk << " rms " << std::sqrt (ss / std::max (1, n)) << "\n";
            }
        }
        return 0;
    }
    // 1. phoneme coverage: every Tongval word must map to known phonemes (G2P uses only table ids)
    {
        std::string nl (BinaryData::nl_words_txt, (size_t) BinaryData::nl_words_txtSize);
        juce::StringArray words; words.addLines (juce::String::fromUTF8 (nl.c_str()));
        words.removeEmptyStrings();
        for (int i = words.size(); --i >= 0;) if (! words[i].containsAnyOf ("abcdefghijklmnopqrstuvwxyzéëèïöüá")) words.remove (i);
        int empty = 0;
        for (auto& w : words)
        {
            auto ph = G2P::get().word (w.toStdString(), Lang::NL);
            if (ph.empty()) { ++empty; std::cout << "  no phonemes: " << w << "\n"; }
        }
        // also every Tongval sentence + all Markov corpus text
        std::string corpus (BinaryData::nl_corpus_txt, (size_t) BinaryData::nl_corpus_txtSize);
        size_t nph = 0; for (auto& wp : G2P::get().text (corpus, Lang::NL)) nph += wp.ph.size();
        std::string ec (BinaryData::en_corpus_txt, (size_t) BinaryData::en_corpus_txtSize);
        size_t eph = 0; for (auto& wp : G2P::get().text (ec, Lang::EN)) eph += wp.ph.size();
        std::cout << "NL words: " << words.size() << " , empty: " << empty << " | NL corpus phonemes " << nph
                  << " | EN corpus phonemes " << eph << " | unknown phoneme ids: " << unknownPhonemeLookups() << "\n";
        if (empty || unknownPhonemeLookups()) ++fails;
    }
    // 2. sample transcriptions
    const char* nlSamples[] = { "hallo", "goedemorgen", "huis", "lopen", "mannen", "manen", "school", "logisch", "gezellig",
        "vrijdag", "eeuw", "nieuw", "kopje", "natie", "studeren", "universiteit", "hond", "zegt", "Nederlands", "spraakmaker",
        "gewoon", "verkeerd", "fiets", "regenjas", "koffie", "geluid", "eigenlijk", "beginnen", "gebruiken", "vrienden", "uitgaan", "meisje", "oranje" };
    for (auto* w : nlSamples) std::cout << "  nl " << w << " -> " << phonString (G2P::get().word (w, Lang::NL)) << "\n";
    const char* enSamples[] = { "hello", "speech", "computer", "thunderstorms", "spraakmaker", "zorblax" };
    for (auto* w : enSamples) std::cout << "  en " << w << " -> " << phonString (G2P::get().word (w, Lang::EN)) << "\n";

    // 3. renders
    struct R { const char* name; const char* text; Lang l; float f0; };
    R rs[] = {
        { "en_hello", "Hello world. I am a speech computer.", Lang::EN, 110 },
        { "en_question", "Do angels fly through clouds during lunar thunderstorms?", Lang::EN, 110 },
        { "nl_hallo", "Hallo! Hoe gaat het met je?", Lang::NL, 120 },
        { "nl_zin", "Ik ben een spraakmaker. Het regent in Rotterdam.", Lang::NL, 110 },
        { "nl_klanken", "huis, ui, eu, ij, goed, school, uur, muur", Lang::NL, 115 },
        { "nl_alphabet", "a b c d e f g h i j k l m n o p q r s t u v w x y z", Lang::NL, 130 },
    };
    for (auto& r : rs)
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            float pk = 0;
            auto x = render (r.text, r.l, sr, r.f0, &pk);
            double ss = 0; bool finite = true;
            for (float s : x) { ss += s * s; finite &= std::isfinite (s); }
            const double rms = std::sqrt (ss / (double) x.size());
            const bool ok = finite && rms > 0.005 && pk < 1.5f;
            if (sr == 48000.0)
            {
                writeWav (outDir.getChildFile (juce::String (r.name) + ".wav"), x, sr);
                std::cout << (ok ? "OK  " : "BAD ") << r.name << "  " << x.size() / sr << " s  rms " << rms << "  peak " << pk << "\n";
            }
            if (! ok) { ++fails; std::cout << "  FAIL at " << sr << " Hz: rms " << rms << " peak " << pk << " finite " << finite << "\n"; }
        }
    }
    // 4. planner sources (synchronous)
    {
        Planner pl (outDir.getChildFile ("plannerdata"));
        for (int lang = 0; lang < 2; ++lang)
            for (int src = 0; src < 6; ++src)
            {
                pl.lang = lang; pl.source = src; pl.listNum = lang ? 4 : 5;
                Utterance u; pl.makeNext (u);
                std::cout << "  src " << src << " lang " << lang << ": \"" << u.text << "\" segs " << u.n << " words " << u.nWords << "\n";
                if (u.n < 3) ++fails;
            }
    }
    std::cout << (fails ? "FAILURES: " : "ALL OK ") << fails << "\n";
    return fails ? 1 : 0;
}
