// Spraakmaker — parameter ids, choice lists and the OLED page layouts (shared by processor and editor).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace sm::prm {

inline const juce::StringArray kTrig { "AUTO", "TRIG", "GATE", "PH.TRG", "PH.GATE" };
inline const juce::StringArray kRate { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR" };
inline const juce::StringArray kSource { "DICT", "ALPHA", "KEYB", "MARKOV", "LETTR", "LIST" };
inline const juce::StringArray kOrder { "SEQ", "RAND" };
inline const juce::StringArray kTrgOut { "WORD", "PHON", "BOTH", "OFF" };
inline const juce::StringArray kLang { "EN", "NL" };
inline const juce::StringArray kEngine { "CLASSIC", "VOCODER", "VOSINGER" };
inline const juce::StringArray kChord { "UNISON", "OCTAVE", "FIFTH", "MAJOR", "MINOR", "SUS4", "MAJ7", "MIN7", "DIM", "CLUSTER" };
inline const juce::StringArray kMicro { "EQUAL", "JUST", "PYTHAG", "MEANTN", "24-TET", "USER" };
inline const juce::StringArray kCarrier { "SAW", "PULSE", "NOISE" };
inline const juce::StringArray kDly { "1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
inline const juce::StringArray kShape { "SIN", "TRI", "SAW", "SQR", "S&H" };
inline const juce::StringArray kCvSrc { "KNOB", "LFO1", "LFO2", "LFO3", "LFO4", "VEL", "MODWH", "RAND" };
inline const juce::StringArray kDest { "---", "PITCH", "FORMNT", "INTON", "BRIGHT", "SPEED", "EMOTN", "START", "FINE", "CRUSH", "DELAY", "REVERB", "WIDTH" };
enum Dest { dNone, dPitch, dFormant, dInton, dBright, dSpeed, dEmotion, dStart, dFine, dCrush, dDelay, dReverb, dWidth, dCount };

struct Item { const char* id; const char* label; };

// OLED pages (F1..F4)
inline const std::vector<Item> kPageTrg {
    { "trig", "TRIG" }, { "retrig", "RETRIG" }, { "rate", "RATE" }, { "source", "SOURCE" },
    { "list", "LIST" }, { "order", "ORDER" }, { "start", "START" }, { "trgout", "TRG OUT" } };
inline const std::vector<Item> kPageVoice {
    { "lang", "LANG" }, { "engine", "ENGINE" }, { "poly", "POLY" }, { "chord", "CHORD" },
    { "fine", "FINE" }, { "micro", "MICRO" }, { "glide", "GLIDE" }, { "root", "ROOT" },
    { "bands", "BANDS" }, { "carrier", "CARRIER" } };
inline const std::vector<Item> kPageFx {
    { "bits", "BITS" }, { "down", "DOWNSMP" }, { "width", "WIDTH" }, { "dlytime", "DLY TIME" },
    { "dlyfb", "DLY FB" }, { "dlymix", "DLY MIX" }, { "revsize", "REV SIZE" }, { "revmix", "REV MIX" }, { "level", "LEVEL" } };
inline const std::vector<Item> kPageMod {
    { "lfo1rate", "LFO1 RT" }, { "lfo1shape", "LFO1 SH" }, { "lfo2rate", "LFO2 RT" }, { "lfo2shape", "LFO2 SH" },
    { "lfo3rate", "LFO3 RT" }, { "lfo3shape", "LFO3 SH" }, { "lfo4rate", "LFO4 RT" }, { "lfo4shape", "LFO4 SH" },
    { "cv1src", "CV1 SRC" }, { "cv1dst", "CV1 DST" }, { "cv1amt", "CV1 AMT" },
    { "cv2src", "CV2 SRC" }, { "cv2dst", "CV2 DST" }, { "cv2amt", "CV2 AMT" },
    { "cv3src", "CV3 SRC" }, { "cv3dst", "CV3 DST" }, { "cv3amt", "CV3 AMT" },
    { "cv4src", "CV4 SRC" }, { "cv4dst", "CV4 DST" }, { "cv4amt", "CV4 AMT" },
    { "m1dst", "M1 DST" }, { "m2dst", "M2 DST" }, { "m3dst", "M3 DST" } };

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, float lo, float hi, float def, float skewMid = -1) {
        NormalisableRange<float> r (lo, hi);
        if (skewMid > lo) r.setSkewForCentre (skewMid);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };
    auto c = [&] (const char* id, const char* name, const StringArray& ch, int def) {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, ch, def));
    };
    auto i = [&] (const char* id, const char* name, int lo, int hi, int def) {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    // panel
    f ("pitch", "Pitch", -24.f, 24.f, 0.f);
    f ("formant", "Formant", 0.f, 1.f, 0.5f);
    f ("inton", "Intonation", 0.f, 1.f, 0.5f);
    f ("bright", "Brightness", 0.f, 1.f, 0.5f);
    f ("speed", "Speed", 0.f, 1.f, 0.5f);
    f ("emotion", "Emotion", 0.f, 1.f, 0.5f);
    f ("m1", "M1", 0.f, 1.f, 0.f); f ("m2", "M2", 0.f, 1.f, 0.f); f ("m3", "M3", 0.f, 1.f, 0.f);
    f ("cv1", "CV1", 0.f, 1.f, 0.f); f ("cv2", "CV2", 0.f, 1.f, 0.f); f ("cv3", "CV3", 0.f, 1.f, 0.f); f ("cv4", "CV4", 0.f, 1.f, 0.f);
    // TRG/SRC
    c ("trig", "Trigger mode", kTrig, 0);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "retrig", 1 }, "Retrigger", true));
    c ("rate", "Auto rate", kRate, 5);
    c ("source", "Source", kSource, 0);
    i ("list", "List", 1, 99, 1);
    c ("order", "Order", kOrder, 0);
    f ("start", "Start point", 0.f, 1.f, 0.f);
    c ("trgout", "Trg Out", kTrgOut, 0);
    // VOICE
    c ("lang", "Language", kLang, 1);
    c ("engine", "Engine", kEngine, 0);
    i ("poly", "Poly", 1, 4, 1);
    i ("chord", "Chord", 1, 10, 1);
    f ("fine", "Fine", 0.f, 50.f, 8.f);
    c ("micro", "Micro", kMicro, 0);
    f ("glide", "Glide", 0.f, 1.f, 0.1f);
    i ("root", "Root note", 24, 84, 48);
    i ("bands", "Bands", 8, 20, 16);
    c ("carrier", "Carrier", kCarrier, 0);
    // FX
    i ("bits", "Bits", 4, 16, 16);
    i ("down", "Downsample", 1, 16, 1);
    f ("width", "Width", 0.f, 1.f, 0.3f);
    c ("dlytime", "Delay time", kDly, 2);
    f ("dlyfb", "Delay feedback", 0.f, 0.9f, 0.35f);
    f ("dlymix", "Delay mix", 0.f, 1.f, 0.f);
    f ("revsize", "Reverb size", 0.f, 1.f, 0.5f);
    f ("revmix", "Reverb mix", 0.f, 1.f, 0.15f);
    f ("level", "Level", 0.f, 1.f, 0.8f);
    // MOD
    const float lfoDef[4] = { 0.5f, 0.13f, 2.f, 6.f };
    for (int k = 1; k <= 4; ++k)
    {
        auto id = [&] (const char* s) { static thread_local std::string b; b = "lfo" + std::to_string (k) + s; return b; };
        std::string rid = id ("rate"), sid = id ("shape");
        NormalisableRange<float> r (0.01f, 20.f); r.setSkewForCentre (1.f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { rid, 1 }, "LFO" + std::to_string (k) + " rate", r, lfoDef[k - 1]));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { sid, 1 }, "LFO" + std::to_string (k) + " shape", kShape, k == 4 ? 4 : 0));
    }
    for (int k = 1; k <= 4; ++k)
    {
        const std::string n = std::to_string (k);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cv" + n + "src", 1 }, "CV" + n + " source", kCvSrc, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cv" + n + "dst", 1 }, "CV" + n + " dest", kDest, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cv" + n + "amt", 1 }, "CV" + n + " amount", NormalisableRange<float> (-1.f, 1.f), 0.5f));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m1dst", 1 }, "M1 dest", kDest, dStart));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m2dst", 1 }, "M2 dest", kDest, dFine));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m3dst", 1 }, "M3 dest", kDest, dReverb));
    return l;
}

} // namespace sm::prm
