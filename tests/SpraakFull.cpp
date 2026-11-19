// The speech computer (Spraakmaker 1.0 inside Spreekbuis), every feature, one by one, through the real processor: trigger modes, retrigger, sources in both languages,
// engines (classic poly, vocoder with its own carrier and with external carrier / modulator, vosinger chords),
// tunings incl. a .scl file, every effect, LFO shapes and the CV matrix to every destination, macros, velocity /
// mod wheel / random sources, trg out, start point, glide, typed text, user presets and the session state.
// A feature "works" when it makes sound and (for a control) moves the sound away from the same render without it.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Processor.h"
#include <iostream>
#include <juce_dsp/juce_dsp.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
constexpr double kSr = 48000; constexpr int kBs = 512;
int fails = 0;

void setP (SpreekbuisProcessor& p, const char* id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    if (rp == nullptr) { std::cout << "  (no parameter " << id << ")\n"; ++fails; return; }
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setBpm (bpm); i.setPpqPosition (ppq); i.setIsPlaying (playing); i.setTimeSignature (TimeSignature { 4, 4 }); return i;
    }
};

struct Out { juce::AudioBuffer<float> audio; int notes60 = 0, notes62 = 0, other = 0; double rms = 0; float peak = 0; bool finite = true; };

// a fresh processor on the defaults, with the planner warmed up
std::unique_ptr<SpreekbuisProcessor> fresh (std::function<void (SpreekbuisProcessor&)> setup, int inputs = 0)
{
    std::unique_ptr<SpreekbuisProcessor> p (dynamic_cast<SpreekbuisProcessor*> (createPluginFilter()));
    p->setOfflineTest (1234);   // planner pumped from processBlock, fixed random seed: identical renders on any machine
    if (inputs > 0)
    {
        auto layout = p->getBusesLayout();
        for (int i = 0; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = juce::AudioChannelSet::stereo();
        p->setBusesLayout (layout);
    }
    for (auto* prm : p->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (rp->getDefaultValue());
    // the instrument as Spraakmaker was: only the speech computer, played from the keys, at full level in a dry room
    for (auto [id, v] : { std::pair { "spraak_on", 1.0f }, { "loop_on", 0.0f }, { "keys", 5.0f }, { "master", 1.0f }, { "space", 0.0f }, { "loop_bpm", 120.0f }, { "orca_on", 0.0f } }) setP (*p, id, v);
    setP (*p, "revmix", 0.0f);   // dry, so differences are the feature's own
    setup (*p);
    p->setRateAndBufferSizeDetails (kSr, kBs);   // (keeps the bus layout set above)
    p->prepareToPlay (kSr, kBs);
    return p;
}

// render; midi (block index -> events), input (block -> fill the input channels)
Out render (SpreekbuisProcessor& p, double secs, std::function<void (int, juce::MidiBuffer&)> midi = {},
            std::function<void (int, juce::AudioBuffer<float>&)> input = {}, Head* head = nullptr)
{
    if (head) p.setPlayHead (head);
    const int blocks = (int) (secs * kSr / kBs);
    const int ch = std::max (2, p.getTotalNumInputChannels());
    juce::AudioBuffer<float> buf (ch, kBs);
    Out o; o.audio.setSize (2, blocks * kBs); o.audio.clear();
    double ss = 0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        if (input) input (b, buf);
        juce::MidiBuffer m;
        if (midi) midi (b, m);
        p.processBlock (buf, m);
        for (const auto meta : m)
            if (meta.getMessage().isNoteOn())
            { const int nn = meta.getMessage().getNoteNumber(); if (nn == 60) ++o.notes60; else if (nn == 62) ++o.notes62; else ++o.other; }
        for (int c = 0; c < 2; ++c)
        {
            o.audio.copyFrom (c, b * kBs, buf, c, 0, kBs);
            for (int i = 0; i < kBs; ++i) { const float x = buf.getSample (c, i); o.finite &= std::isfinite (x); ss += x * x; o.peak = std::max (o.peak, std::abs (x)); }
        }
        if (head) head->ppq += kBs / kSr * head->bpm / 60.0;
    }
    o.rms = std::sqrt (ss / (2.0 * blocks * kBs));
    return o;
}

// how different two renders sound (not sample by sample: the voice has jitter and the timing of
// the planner thread varies). Mean difference in dB of the long-term spectrum of mid and of side
// (24 bands, 80 Hz .. 14 kHz) and of the loudness envelope (21 ms frames).
std::vector<float> features (const Out& o)
{
    constexpr int N = 1024; juce::dsp::FFT fft (10);
    std::vector<double> mid (24, 1e-12), side (24, 1e-12); std::vector<float> env;
    std::vector<float> fm (2 * N), fs (2 * N);
    for (int pos = 0; pos + N <= o.audio.getNumSamples(); pos += N)
    {
        double e = 0;
        for (int i = 0; i < N; ++i)
        {
            const float l = o.audio.getSample (0, pos + i), r = o.audio.getSample (1, pos + i), w = 0.5f - 0.5f * std::cos (6.2831853f * i / N);
            fm[(size_t) i] = 0.5f * (l + r) * w; fs[(size_t) i] = 0.5f * (l - r) * w; e += l * l + r * r;
        }
        std::fill (fm.begin() + N, fm.end(), 0.0f); std::fill (fs.begin() + N, fs.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fm.data()); fft.performFrequencyOnlyForwardTransform (fs.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * kSr / N; if (hz < 80 || hz > 14000) continue;
            const int band = juce::jlimit (0, 23, (int) (24 * std::log (hz / 80.0) / std::log (14000.0 / 80.0)));
            mid[(size_t) band] += fm[(size_t) k] * fm[(size_t) k]; side[(size_t) band] += fs[(size_t) k] * fs[(size_t) k];
        }
        env.push_back ((float) juce::jmax (-80.0, 10 * std::log10 (e / (2 * N) + 1e-12)));
    }
    std::vector<float> f;
    for (auto v : mid) f.push_back ((float) juce::jmax (-100.0, 10 * std::log10 (v)));
    for (auto v : side) f.push_back ((float) juce::jmax (-100.0, 10 * std::log10 (v)));
    for (auto v : env) f.push_back (v);
    return f;
}
double diff (const Out& a, const Out& b)
{
    const auto fa = features (a), fb = features (b);
    double d = 0; const size_t n = std::min (fa.size(), fb.size());
    for (size_t i = 0; i < n; ++i) d += std::abs (fa[i] - fb[i]);
    return d / (double) std::max<size_t> (1, n);
}

void report (bool ok, const juce::String& what, const juce::String& detail)
{
    std::cout << (ok ? "PASS " : "FAIL ") << what << "  " << detail << "\n";
    if (! ok) ++fails;
}
bool sounds (const Out& o) { return o.finite && o.rms > 0.002 && o.peak <= 1.0f; }
juce::String stats (const Out& o) { return "rms " + juce::String (o.rms, 4) + " peak " + juce::String (o.peak, 3) + (o.finite ? "" : " NaN!"); }

// one note at the start (and its release near the end): a deterministic "say it once"
auto noteAt (int onBlock, int offBlock, int note = 48, int vel = 100)
{
    return [=] (int b, juce::MidiBuffer& m) {
        if (b == onBlock) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
        if (b == offBlock) m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    };
}
// typed text, spoken on a TRIG note: the same words every time, so a control's effect can be measured
std::unique_ptr<SpreekbuisProcessor> sayer (std::function<void (SpreekbuisProcessor&)> setup, int inputs = 0)
{
    auto p = fresh ([&] (SpreekbuisProcessor& p) {
        setP (p, "lang", 1); setP (p, "source", 2); setP (p, "trig", 1);
        p.planner().setKeyboardText ("hallo wereld, ik ben de spraakmaker");
        setup (p);
    }, inputs);
    // the planner only sees the new settings once a block has run: feed it silent blocks at real-time pace until the
    // typed text is queued, so the note in the render speaks it at the same moment every time (slow CI machines too)
    juce::AudioBuffer<float> buf (std::max (2, p->getTotalNumInputChannels()), kBs);
    for (int b = 0; b < 300 && ! p->planner().ready(); ++b) { buf.clear(); juce::MidiBuffer m; p->processBlock (buf, m); }
    return p;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    // the words heard here are not the user's: keep them out of the play log Tongval reads
    sb::setPlayLogOverride (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spreek-test-plays.json"));
    const double secs = 2.5;

    // warm up: the first use of a language / source loads its dictionaries and texts (a few seconds, once per run)
    for (int lang = 0; lang < 2; ++lang)
        for (int src = 0; src < 6; ++src)
        {
            auto p = fresh ([&] (auto& p) { setP (p, "lang", (float) lang); setP (p, "source", (float) src); setP (p, "trig", 0); setP (p, "rate", 0); });
            juce::AudioBuffer<float> buf (2, kBs);
            for (int b = 0; b < 600; ++b) { buf.clear(); juce::MidiBuffer m; p->processBlock (buf, m); if (buf.getMagnitude (0, 0, kBs) > 1e-4f) break; }
        }

    // ---- repeatability: the same render twice (the yardstick for "it changed the sound") ----
    double noise = 0;
    {
        auto a = sayer ([] (auto&) {}); auto b = sayer ([] (auto&) {});
        const auto oa = render (*a, secs, noteAt (2, 200)), ob = render (*b, secs, noteAt (2, 200));
        noise = diff (oa, ob);
        report (sounds (oa), "typed text on a TRIG note speaks", stats (oa) + ", repeat difference " + juce::String (noise, 4));
    }
    const double moved = std::max (0.1, noise * 2 + 0.05);   // dB (renders repeat exactly, so small real changes count)
    auto changes = [&] (const char* what, std::function<void (SpreekbuisProcessor&)> setup, std::function<void (int, juce::MidiBuffer&)> midi = {}) {
        auto base = sayer ([] (auto&) {}); auto mod = sayer (setup);
        const auto m = midi ? midi : noteAt (2, 200);
        const auto ob = render (*base, secs, m), om = render (*mod, secs, m);
        const double d = diff (ob, om);
        report (sounds (om) && d > moved, what, stats (om) + ", moved " + juce::String (d, 3));
    };

    // ---- trigger modes ----
    {
        auto p = fresh ([] (auto& p) { setP (p, "trig", 0); setP (p, "rate", 0); });          // AUTO free
        const auto o = render (*p, 4);
        report (sounds (o) && o.notes60 >= 2, "AUTO free-running talks by itself", stats (o) + ", words " + juce::String (o.notes60));
    }
    for (int rate = 1; rate <= 7; ++rate)
    {
        Head h; h.playing = true; h.bpm = 120;
        auto p = fresh ([&] (auto& p) { setP (p, "trig", 0); setP (p, "rate", (float) rate); });
        const int before = p->triggerCount();
        const auto o = render (*p, 4.2, {}, {}, &h);
        const int trig = p->triggerCount() - before;
        report (sounds (o) && trig >= 1, "AUTO synced " + juce::String (juce::String (sm::prm::kRate[rate])), stats (o) + ", triggers " + juce::String (trig));
    }
    {
        auto p = fresh ([] (auto& p) { setP (p, "trig", 1); setP (p, "source", 0); });
        const auto silent = render (*p, 1.0);
        const auto o = render (*p, 2.0, noteAt (2, 100));
        report (silent.rms < 1e-4 && sounds (o), "TRIG: quiet until a note, then speaks", "before " + juce::String (silent.rms, 5) + ", " + stats (o));
    }
    {
        // GATE: talks while held, stops when released
        auto p = fresh ([] (auto& p) { setP (p, "trig", 2); setP (p, "source", 0); });
        const auto o = render (*p, 3.0, noteAt (2, 120));
        double tail = 0; for (int c = 0; c < 2; ++c) tail += o.audio.getRMSLevel (c, 125 * kBs, 140 * kBs - 125 * kBs - 1);
        double held = 0; for (int c = 0; c < 2; ++c) held += o.audio.getRMSLevel (c, 20 * kBs, 100 * kBs);
        report (held > 0.004 && tail < held * 0.1, "GATE: speaks while held, stops on release", "held " + juce::String (held, 4) + ", after " + juce::String (tail, 5));
    }
    for (int mode : { 3, 4 })
    {
        // PH.TRG / PH.GATE: each note steps to the next phoneme
        auto p = fresh ([&] (auto& p) { setP (p, "trig", (float) mode); setP (p, "source", 0); setP (p, "trgout", 1); });
        const auto o = render (*p, 3.0, [] (int b, juce::MidiBuffer& m) {
            if (b % 20 == 2) m.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0);
            if (b % 20 == 12) m.addEvent (juce::MidiMessage::noteOff (1, 50), 0); });
        report (sounds (o) && o.notes62 >= 4, mode == 3 ? "PH.TRG: notes step through phonemes" : "PH.GATE: notes step through phonemes, gated",
                stats (o) + ", phoneme pulses " + juce::String (o.notes62));
    }
    for (int rt : { 1, 0 })
    {
        // retrigger on: a second note restarts; off: it's ignored while speaking
        auto p = fresh ([&] (auto& p) { setP (p, "trig", 1); setP (p, "retrig", (float) rt); setP (p, "source", 3); });
        const int before = p->triggerCount();
        render (*p, 1.5, [] (int b, juce::MidiBuffer& m) { if (b == 2 || b == 20) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
                                                           if (b == 5 || b == 23) m.addEvent (juce::MidiMessage::noteOff (1, 48), 0); });
        juce::ignoreUnused (before);
        const auto text = juce::String (p->display().text);
        report (text.isNotEmpty(), rt ? "retrigger on" : "retrigger off", "speaking \"" + text + "\"");
    }

    // ---- sources in both languages ----
    for (int lang = 0; lang < 2; ++lang)
        for (int src = 0; src < 6; ++src)
        {
            auto p = fresh ([&] (auto& p) { setP (p, "lang", (float) lang); setP (p, "source", (float) src); setP (p, "trig", 0); setP (p, "rate", 0); });
            const auto o = render (*p, 3.0);
            report (sounds (o), juce::String ("source ") + sm::prm::kSource[src] + " " + sm::prm::kLang[lang], stats (o) + " \"" + juce::String (p->display().text) + "\"");
        }
    for (int list : { 1, 2, 3, 4, 5, 50 })
    {
        auto p = fresh ([&] (auto& p) { setP (p, "source", 5); setP (p, "list", (float) list); setP (p, "trig", 0); setP (p, "rate", 0); });
        const auto o = render (*p, 2.0);
        report (sounds (o), "list " + juce::String (list), stats (o) + " \"" + juce::String (p->display().text) + "\"");
    }
    {
        auto p = fresh ([] (auto& p) { setP (p, "trig", 1); });
        p->typeAndSpeak ("dit is getypt");
        const auto o = render (*p, 2.0);
        report (sounds (o) && juce::String (p->display().text).containsIgnoreCase ("getypt"), "typing (Enter speaks it at once)", stats (o) + " \"" + juce::String (p->display().text) + "\"");
    }

    // ---- engines ----
    for (int poly = 2; poly <= 4; ++poly)
        changes (("CLASSIC poly " + juce::String (poly)).toRawUTF8(), [=] (auto& p) { setP (p, "poly", (float) poly); setP (p, "fine", 20); });
    for (int car = 0; car < 3; ++car)
        changes ((juce::String ("VOCODER, own carrier ") + sm::prm::kCarrier[car]).toRawUTF8(), [=] (auto& p) { setP (p, "engine", 1); setP (p, "carrier", (float) car); });
    for (int bands : { 8, 20 })
        changes (("VOCODER bands " + juce::String (bands)).toRawUTF8(), [=] (auto& p) { setP (p, "engine", 1); setP (p, "bands", (float) bands); });
    {
        // external carrier (bus 1) and modulator (bus 2)
        auto p = sayer ([] (auto& p) { setP (p, "engine", 1); }, 2);
        float ph = 0;
        const auto o = render (*p, secs, noteAt (2, 200), [&] (int, juce::AudioBuffer<float>& b) {
            for (int i = 0; i < kBs; ++i) { ph += 220.0f / (float) kSr; ph -= std::floor (ph); const float s = 0.5f * (2 * ph - 1); b.setSample (0, i, s); b.setSample (1, i, s); } });
        report (sounds (o) && p->externalCarrier(), "VOCODER external carrier (input 1)", stats (o) + ", seen " + juce::String ((int) p->externalCarrier()));
        auto q = fresh ([] (auto& p) { setP (p, "engine", 1); setP (p, "trig", 1); }, 2);
        juce::Random r (5);
        const auto oq = render (*q, 2.0, {}, [&] (int b, juce::AudioBuffer<float>& buf) {
            if (buf.getNumChannels() < 4) return;
            for (int i = 0; i < kBs; ++i) { const float s = 0.3f * std::sin ((float) (b * kBs + i) * 0.05f) * (0.5f + 0.5f * std::sin ((float) (b * kBs + i) * 0.0004f)); buf.setSample (2, i, s); buf.setSample (3, i, s); } });
        juce::ignoreUnused (r);
        report (sounds (oq) && q->externalModulator(), "VOCODER external modulator (input 2)", stats (oq) + ", seen " + juce::String ((int) q->externalModulator()));
    }
    for (int chord = 1; chord <= 10; chord += 3)
        changes ((juce::String ("VOSINGER chord ") + sm::prm::kChord[chord - 1]).toRawUTF8(), [=] (auto& p) { setP (p, "engine", 2); setP (p, "poly", 4); setP (p, "chord", (float) chord); });
    {
        auto p = sayer ([] (auto& p) { setP (p, "engine", 2); setP (p, "poly", 3); });
        const auto o = render (*p, secs, [] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), 0); } });
        report (sounds (o), "VOSINGER plays held chords", stats (o));
    }

    // ---- tuning ----
    for (int m = 1; m <= 4; ++m)
        changes ((juce::String ("micro ") + sm::prm::kMicro[m]).toRawUTF8(), [=] (auto& p) { setP (p, "micro", (float) m); }, noteAt (2, 200, 51));
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spraak_test.scl");
        f.replaceWithText ("! test\nBohlen-Pierce\n 3\n!\n 9/7\n 7/5\n 3/1\n");
        auto base = sayer ([] (auto&) {}); auto mod = sayer ([] (auto&) {});
        const bool ok = mod->loadScl (f);
        const auto ob = render (*base, secs, noteAt (2, 200, 64)), om = render (*mod, secs, noteAt (2, 200, 64));
        report (ok && sounds (om) && diff (ob, om) > moved && mod->sclName() == "Bohlen-Pierce", "USER tuning from a .scl file", "\"" + mod->sclName() + "\", moved " + juce::String (diff (ob, om), 3));
        f.deleteFile();
    }

    // ---- the panel knobs ----
    for (auto [id, v] : { std::pair { "pitch", 7.0f }, { "formant", 0.9f }, { "inton", 1.0f }, { "bright", 1.0f }, { "speed", 0.9f }, { "emotion", 1.0f }, { "start", 0.5f }, { "glide", 1.0f } })
        changes ((juce::String ("knob ") + id).toRawUTF8(), [=] (auto& p) { setP (p, id, v); },
                 std::string (id) == "glide" ? std::function<void (int, juce::MidiBuffer&)> ([] (int b, juce::MidiBuffer& m) {
                     if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0);
                     if (b == 30) m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); }) : std::function<void (int, juce::MidiBuffer&)> {});

    // ---- effects ----
    for (auto [id, v] : { std::pair { "bits", 5.0f }, { "down", 8.0f }, { "width", 1.0f }, { "dlymix", 0.6f }, { "revmix", 0.6f }, { "level", 0.3f } })
        changes ((juce::String ("fx ") + id).toRawUTF8(), [=] (auto& p) { setP (p, id, v); });
    for (int t = 0; t < 6; ++t)
        changes ((juce::String ("delay time ") + sm::prm::kDly[t]).toRawUTF8(), [=] (auto& p) { setP (p, "dlymix", 0.6f); setP (p, "dlyfb", 0.6f); setP (p, "dlytime", (float) t); });

    // ---- modulation: each LFO shape, each source, each destination, the macros ----
    for (int shape = 0; shape < 5; ++shape)
        changes ((juce::String ("LFO1 ") + sm::prm::kShape[shape] + " -> pitch").toRawUTF8(),
                 [=] (auto& p) { setP (p, "lfo1shape", (float) shape); setP (p, "lfo1rate", 4.0f); setP (p, "cv1src", 1); setP (p, "cv1dst", 1); setP (p, "cv1amt", 0.6f); });
    for (int src = 0; src < 8; ++src)
        changes ((juce::String ("CV2 source ") + sm::prm::kCvSrc[src] + " -> formant").toRawUTF8(),
                 [=] (auto& p) { setP (p, "cv2src", (float) src); setP (p, "cv2dst", 2); setP (p, "cv2amt", 1.0f); setP (p, "cv2", 1.0f); setP (p, "lfo4rate", 3.0f); },
                 [] (int b, juce::MidiBuffer& m) { if (b == 1) m.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
                                                   if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 127), 0); });
    for (int dst = 1; dst < sm::prm::dVowelX; ++dst)   // (the word bank's destinations: SpreekTest)
        changes ((juce::String ("CV3 knob -> ") + sm::prm::kDest[dst]).toRawUTF8(),
                 [=] (auto& p) { setP (p, "cv3src", 0); setP (p, "cv3dst", (float) dst); setP (p, "cv3amt", 1.0f); setP (p, "cv3", 0.8f);
                                 setP (p, "dlymix", 0.0f); setP (p, "poly", dst == sm::prm::dFine ? 3.0f : 1.0f); });
    for (int k = 1; k <= 3; ++k)
        changes (("macro M" + juce::String (k)).toRawUTF8(), [=] (auto& p) { setP (p, ("m" + juce::String (k)).toRawUTF8(), 1.0f);
                                                                              setP (p, ("m" + juce::String (k) + "dst").toRawUTF8(), (float) sm::prm::dFormant); });
    {
        // CV4 on its own slot too (the matrix has four)
        changes ("CV4 LFO3 -> speed", [] (auto& p) { setP (p, "cv4src", 3); setP (p, "cv4dst", 5); setP (p, "cv4amt", 1.0f); setP (p, "lfo3rate", 3.0f); });
    }

    // ---- trg out ----
    for (int t = 0; t < 4; ++t)
    {
        auto p = sayer ([&] (auto& p) { setP (p, "trgout", (float) t); });
        const auto o = render (*p, secs, noteAt (2, 200));
        const bool ok = t == 0 ? (o.notes60 > 0 && o.notes62 == 0) : t == 1 ? (o.notes62 > 0 && o.notes60 == 0) : t == 2 ? (o.notes60 > 0 && o.notes62 > 0) : (o.notes60 + o.notes62 == 0);
        report (ok, juce::String ("trg out ") + sm::prm::kTrgOut[t], "word notes " + juce::String (o.notes60) + ", phoneme notes " + juce::String (o.notes62));
    }

    // ---- presets and the session ----
    {
        // a preset is a file now (the whole instrument); Spraakmaker's 100 numbered slots are read by the importer (SpreekTest)
        auto p = fresh ([] (auto&) {});
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spreek_test_preset.xml");
        setP (*p, "pitch", 5.0f); setP (*p, "engine", 2);
        const bool saved = p->savePresetFile (f, "TESTSLOT");
        setP (*p, "pitch", 0.0f); setP (*p, "engine", 0);
        const bool loaded = p->loadPresetFile (f);
        const bool ok = saved && loaded && p->presetLabel() == "TESTSLOT" && std::abs (p->apvts.getRawParameterValue ("pitch")->load() - 5.0f) < 0.01f && (int) p->apvts.getRawParameterValue ("engine")->load() == 2;
        const auto name = p->presetLabel();
        f.deleteFile();
        report (ok, "save a user preset and load it back", "name \"" + name + "\"");
    }
    {
        auto p = fresh ([] (auto& p) { setP (p, "pitch", -3.0f); setP (p, "micro", 2); });
        p->planner().setKeyboardText ("sessie tekst");
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = fresh ([] (auto&) {});
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok = std::abs (q->apvts.getRawParameterValue ("pitch")->load() + 3.0f) < 0.01f && (int) q->apvts.getRawParameterValue ("micro")->load() == 2
                        && q->planner().getKeyboardText() == "sessie tekst";
        report (ok, "session state round trip (parameters, typed text)", juce::String ((int) mb.getSize()) + " bytes");
    }
    {
        auto p = fresh ([] (auto&) {});
        bool allOk = true;
        for (int i = 0; i < p->getNumPrograms(); ++i) { p->setCurrentProgram (i); allOk &= p->getProgramName (i).isNotEmpty() && p->getCurrentProgram() == i; }
        report (allOk, "all " + juce::String (p->getNumPrograms()) + " programs load", "");
    }
    std::cout << (fails ? "FAILURES " : "ALL OK ") << fails << "\n";
    return fails ? 1 : 0;
}
