// Spreekbuis — every parameter of the instrument: the word bank's layers (loop, cloud, vowel chart, g) as they were in
// Spreekbuis 1.0 (same ids, ranges and defaults, so old sessions load), the speech computer's (Spraakmaker 1.0's ids,
// so its states import), and the ones that join the two.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace sm::prm {

// ---- the speech computer's choices (indices as in Spraakmaker 1.0; new entries only at the end)
inline const juce::StringArray kTrig { "auto", "trig", "gate", "ph.trg", "ph.gate" };
inline const juce::StringArray kRate { "free", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bar", "4 bar" };
inline const juce::StringArray kSource { "dict", "alpha", "keyb", "markov", "lettr", "list", "les" };   // les: the word bank's lesson
inline const juce::StringArray kOrder { "seq", "rand" };
inline const juce::StringArray kTrgOut { "word", "phon", "both", "off" };
inline const juce::StringArray kLang { "en", "nl" };
inline const juce::StringArray kEngine { "classic", "vocoder", "vosinger" };
inline const juce::StringArray kChord { "unison", "octave", "fifth", "major", "minor", "sus4", "maj7", "min7", "dim", "cluster" };
inline const juce::StringArray kMicro { "equal", "just", "pythag", "meantn", "24-tet", "user" };
inline const juce::StringArray kCarrier { "saw", "pulse", "noise" };
inline const juce::StringArray kDly { "1/16", "1/8", "1/8d", "1/4", "1/4d", "1/2" };
inline const juce::StringArray kShape { "sin", "tri", "saw", "sqr", "s&h" };
// modulation sources: the first eight are Spraakmaker's; then the word bank's (word onsets, its loudness), the speech
// computer's loudness and where the vowel chart is
inline const juce::StringArray kCvSrc { "knob", "lfo1", "lfo2", "lfo3", "lfo4", "vel", "modwh", "rand", "woord", "bank", "spraak", "klink x", "klink y" };
enum CvSrc { sKnob, sLfo1, sLfo2, sLfo3, sLfo4, sVel, sModWheel, sRand, sWord, sBank, sSpraak, sVowelX, sVowelY, sCount };
// destinations: the first thirteen are Spraakmaker's; then the word bank's layers
inline const juce::StringArray kDest { "---", "pitch", "formnt", "inton", "bright", "speed", "emotn", "start", "fine", "crush", "delay", "reverb", "width",
                                       "klink x", "klink y", "wolk d", "wolk p", "wolk t", "loop t", "g nl-be", "fluist" };
enum Dest { dNone, dPitch, dFormant, dInton, dBright, dSpeed, dEmotion, dStart, dFine, dCrush, dDelay, dReverb, dWidth,
            dVowelX, dVowelY, dCloudDens, dCloudPos, dCloudPitch, dLoopPitch, dGSoft, dWhisper, dCount };
// what the vocoder listens to, what the effects process, what the lesson loop's words do to the speech computer
inline const juce::StringArray kVocMod { "spraak", "loop", "wolk", "loop + wolk" };
inline const juce::StringArray kFxSrc { "spraak", "alles" };
inline const juce::StringArray kLoopTrig { "uit", "trigger", "trigger + toon" };

// ---- the word bank's choices (indices as in Spreekbuis 1.0; new entries only at the end)
inline const juce::StringArray kKeys { "woorden", "klinker", "g", "woorden + klinker", "alles", "spraak" };
enum Keys { kWords, kVowel, kG, kWordsVowel, kEverything, kSpraak };
inline const juce::StringArray kLoopSet { "woorden", "zinnen", "gesprek", "alles" };
inline const juce::StringArray kVowelSrc { "zaag", "ruis", "ingang", "zaag + ingang", "loop", "wolk", "loop + wolk", "spraak", "alles" };

inline juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
inline juce::String semis (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st"; }
inline juce::String secs (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }
inline juce::String hertz (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " hz"; }
inline juce::String plain2 (float v, int) { return juce::String (v, 2); }

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    using Fn = String (*) (float, int);
    // ver 1: parameters Spreekbuis 1.0 had; ver 2: added in 2.0 (hosts that care about new parameters, e.g. AU, are told)
    auto f = [&] (int ver, const char* id, const char* name, float lo, float hi, float def, float skewMid, Fn text) {
        NormalisableRange<float> r (lo, hi);
        if (skewMid > lo && skewMid < hi) r.setSkewForCentre (skewMid);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, ver }, name, r, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction ([text] (float v, int n) { return text (v, n); })));
    };
    auto b = [&] (int ver, const char* id, const char* name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, ver }, name, def)); };
    auto c = [&] (int ver, const char* id, const char* name, const StringArray& opts, int def) {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, ver }, name, opts, def)); };
    auto i = [&] (int ver, const char* id, const char* name, int lo, int hi, int def) { l.add (std::make_unique<AudioParameterInt> (ParameterID { id, ver }, name, lo, hi, def)); };
    const float lin = -1.0e9f;   // no skew

    // ================= Spreekbuis 1.0 (ids, order, ranges and defaults unchanged) =================
    f (1, "master", "Master", 0, 1.5f, 0.8f, lin, pct); f (1, "space", "Space", 0, 1, 0.25f, lin, pct);
    c (1, "keys", "Keys play", kKeys, 0);
    b (1, "all_items", "All items", false);

    b (1, "loop_on", "Loop on", true); f (1, "loop_level", "Loop level", 0, 1.5f, 0.9f, lin, pct);
    i (1, "loop_unit", "Lesson (0 = current)", 0, 36, 0);
    c (1, "loop_set", "Loop plays", kLoopSet, 0);
    c (1, "loop_bars", "Call bars", { "1", "2", "4" }, 0);
    c (1, "loop_gap", "Answer bars", { "0", "1", "2", "4" }, 1);
    c (1, "loop_order", "Order", { "op volgorde", "willekeurig" }, 0);
    b (1, "loop_whisper", "Whisper translation", true);
    f (1, "loop_pitch", "Loop pitch", -12, 12, 0, lin, semis);
    f (1, "loop_bpm", "Free-run BPM", 50, 200, 100, lin, [] (float v, int) { return String (v, 1) + " bpm"; });
    b (1, "loop_midi", "MIDI out", true); i (1, "loop_note", "MIDI out note", 24, 96, 60);

    b (1, "cloud_on", "Cloud on", false); f (1, "cloud_level", "Cloud level", 0, 1.5f, 0.7f, lin, pct);
    f (1, "cloud_density", "Density", 0, 80, 8, 10, [] (float v, int) { return String (v, 1) + " /s"; });
    f (1, "cloud_size", "Grain ms", 15, 900, 180, 150, [] (float v, int) { return String (roundToInt (v)) + " ms"; });
    f (1, "cloud_scatter", "Scatter", 0, 1, 0.6f, lin, pct); f (1, "cloud_pos", "Position", 0, 1, 0.3f, lin, pct);
    f (1, "cloud_pitch", "Cloud pitch", -24, 24, 0, lin, semis); f (1, "cloud_spread", "Spread", 0, 1, 0.7f, lin, pct);
    f (1, "cloud_reverse", "Reverse", 0, 1, 0.1f, lin, pct); f (1, "cloud_whisper", "Translation grains", 0, 1, 0.15f, lin, pct);
    f (1, "whisper_level", "Whisper level", 0, 1.5f, 0.6f, lin, pct);

    b (1, "vowel_on", "Vowel on", false); f (1, "vowel_level", "Vowel level", 0, 1.5f, 0.6f, lin, pct);
    f (1, "vowel_x", "Front-back", 0, 1, 0.35f, lin, plain2); f (1, "vowel_y", "Close-open", 0, 1, 1.0f, lin, plain2);
    c (1, "vowel_glide", "Glide", { "uit", String::fromUTF8 ("\xc9\x9bi (ij/ei)"), String::fromUTF8 ("\xc5\x93y (ui)"), String::fromUTF8 ("\xca\x8cu (ou/au)") }, 0);
    f (1, "vowel_rate", "Glide rate", 0.02f, 8, 0.3f, 0.5f, hertz);
    c (1, "vowel_src", "Source", kVowelSrc, 0);   // (new ones at the end: old sessions keep theirs)
    f (1, "vowel_noise", "Breath", 0, 1, 0.15f, lin, pct); f (1, "vowel_q", "Resonance", 0.3f, 3, 1, lin, plain2);
    f (1, "vowel_shift", "Formant shift", -12, 12, 0, lin, semis);
    f (1, "vowel_attack", "Attack", 0.001f, 2, 0.02f, 0.2f, secs); f (1, "vowel_release", "Release", 0.01f, 4, 0.4f, 0.5f, secs);

    b (1, "g_on", "G on", false); f (1, "g_level", "G level", 0, 1.5f, 0.6f, lin, pct);
    f (1, "g_soft", "Holland - Flanders", 0, 1, 0, lin, pct); f (1, "g_rasp", "Rasp", 0, 1, 0.5f, lin, pct);
    f (1, "g_voice", "Voicing", 0, 1, 0.5f, lin, pct); f (1, "g_harm", "Resonance x", 1, 24, 8, 6, [] (float v, int) { return "x " + String (v, 1); });
    f (1, "g_release", "G release", 0.01f, 2, 0.15f, 0.3f, secs);
    b (1, "orca_on", "Orca bridge", true);
    // the voice: the words (a second rendered bank), the vowel chart's formants and pitch, the speech computer's vocal tract
    c (1, "voice", "Voice", { "man", "vrouw" }, 0);

    // ================= the speech computer (Spraakmaker 1.0's ids, ranges and defaults) =================
    b (2, "spraak_on", "Spraak on", false);
    // panel
    f (2, "pitch", "Spraak pitch", -24.f, 24.f, 0.f, lin, semis);
    f (2, "formant", "Spraak formant", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "inton", "Spraak intonation", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "bright", "Spraak brightness", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "speed", "Spraak speed", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "emotion", "Spraak emotion", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "m1", "M1", 0.f, 1.f, 0.f, lin, pct); f (2, "m2", "M2", 0.f, 1.f, 0.f, lin, pct); f (2, "m3", "M3", 0.f, 1.f, 0.f, lin, pct);
    f (2, "cv1", "CV1", 0.f, 1.f, 0.f, lin, pct); f (2, "cv2", "CV2", 0.f, 1.f, 0.f, lin, pct); f (2, "cv3", "CV3", 0.f, 1.f, 0.f, lin, pct); f (2, "cv4", "CV4", 0.f, 1.f, 0.f, lin, pct);
    // trigger / source
    c (2, "trig", "Spraak trigger mode", kTrig, 0);
    b (2, "retrig", "Spraak retrigger", true);
    c (2, "rate", "Spraak auto rate", kRate, 5);
    c (2, "source", "Spraak source", kSource, 0);
    i (2, "list", "Spraak list", 1, 99, 1);
    c (2, "order", "Spraak order", kOrder, 0);
    f (2, "start", "Spraak start point", 0.f, 1.f, 0.f, lin, pct);
    c (2, "trgout", "Spraak trg out", kTrgOut, 0);
    // voice
    c (2, "lang", "Spraak language", kLang, 1);
    c (2, "engine", "Spraak engine", kEngine, 0);
    i (2, "poly", "Spraak poly", 1, 4, 1);
    i (2, "chord", "Spraak chord", 1, 10, 1);
    f (2, "fine", "Spraak fine", 0.f, 50.f, 8.f, lin, [] (float v, int) { return String (roundToInt (v)) + " ct"; });
    c (2, "micro", "Spraak micro", kMicro, 0);
    f (2, "glide", "Spraak glide", 0.f, 1.f, 0.1f, lin, pct);
    i (2, "root", "Spraak root note", 24, 84, 48);
    i (2, "bands", "Vocoder bands", 8, 20, 16);
    c (2, "carrier", "Vocoder carrier", kCarrier, 0);
    // effects
    i (2, "bits", "FX bits", 4, 16, 16);
    i (2, "down", "FX downsample", 1, 16, 1);
    f (2, "width", "FX width", 0.f, 1.f, 0.3f, lin, pct);
    c (2, "dlytime", "FX delay time", kDly, 2);
    f (2, "dlyfb", "FX delay feedback", 0.f, 0.9f, 0.35f, lin, pct);
    f (2, "dlymix", "FX delay mix", 0.f, 1.f, 0.f, lin, pct);
    f (2, "revsize", "FX reverb size", 0.f, 1.f, 0.5f, lin, pct);
    f (2, "revmix", "FX reverb mix", 0.f, 1.f, 0.15f, lin, pct);
    f (2, "level", "Spraak level", 0.f, 1.f, 0.8f, lin, pct);
    // modulation
    const float lfoDef[4] = { 0.5f, 0.13f, 2.f, 6.f };
    for (int k = 1; k <= 4; ++k)
    {
        const String n (k);
        NormalisableRange<float> r (0.01f, 20.f); r.setSkewForCentre (1.f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfo" + n + "rate", 2 }, "LFO" + n + " rate", r, lfoDef[k - 1],
                                                      AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " hz"; })));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo" + n + "shape", 2 }, "LFO" + n + " shape", kShape, k == 4 ? 4 : 0));
    }
    for (int k = 1; k <= 4; ++k)
    {
        const String n (k);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cv" + n + "src", 2 }, "CV" + n + " source", kCvSrc, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cv" + n + "dst", 2 }, "CV" + n + " dest", kDest, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cv" + n + "amt", 2 }, "CV" + n + " amount", NormalisableRange<float> (-1.f, 1.f), 0.5f,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m1dst", 2 }, "M1 dest", kDest, dStart));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m2dst", 2 }, "M2 dest", kDest, dFine));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "m3dst", 2 }, "M3 dest", kDest, dReverb));

    // ================= what joins them =================
    b (2, "loop_echo", "Spraak answers the loop", false);            // the speech computer repeats the loop's item in the answer bars
    c (2, "loop_trig", "Loop words trigger spraak", kLoopTrig, 0);   // the loop's word onsets are trigger notes for the speech computer
    c (2, "vocmod", "Vocoder modulator", kVocMod, 0);                // the vocoder listens to the speech, or to the bank's voice
    c (2, "fx_src", "FX process", kFxSrc, 0);                        // the effects take the speech computer, or every layer
    return l;
}

} // namespace sm::prm
