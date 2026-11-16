// Offline test: render each Spreekbuis layer headless and check the output is audible and finite.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <juce_dsp/juce_dsp.h>

static void setP(SpreekbuisProcessor& p, const char* id, float v) {
    auto* rp = p.apvts.getParameter(id);
    rp->setValueNotifyingHost(rp->convertTo0to1(v));
}
struct Result { double rms = 0; bool finite = true; int midiOut = 0; float peak = 0; };
static Result render(SpreekbuisProcessor& p, double seconds, std::vector<int> notes, std::function<void(int)> perBlock = {}) {
    const int bs = 512; const double sr = 48000;
    juce::AudioBuffer<float> buf(2, bs);
    Result r; double sum = 0; long n = 0;
    int blocks = (int) (seconds * sr / bs);
    for (int k = 0; k < blocks; ++k) {
        juce::MidiBuffer m;
        if (k == 2) for (int nt : notes) m.addEvent(juce::MidiMessage::noteOn(1, nt, (juce::uint8) 100), 0);
        if (k == blocks - 20) for (int nt : notes) m.addEvent(juce::MidiMessage::noteOff(1, nt), 0);
        if (perBlock) perBlock(k);
        buf.clear();
        p.processBlock(buf, m);
        for (const auto ev : m) if (ev.getMessage().isNoteOn()) r.midiOut++;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { float v = buf.getSample(c, i); if (!std::isfinite(v)) r.finite = false; sum += v * v; ++n; r.peak = std::max(r.peak, std::abs(v)); }
    }
    r.rms = std::sqrt(sum / std::max(1L, n));
    return r;
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String(argv[1]) == "--vowelwav") { // the chart pressed, man and woman, to <dir>/man.wav, vrouw.wav
        for (int v = 0; v < 2; ++v) {
            SpreekbuisProcessor p; setP(p, "orca_on", 0); setP(p, "voice", (float) v); setP(p, "vowel_on", 1); setP(p, "space", 0);
            setP(p, "loop_on", 0); p.prepareToPlay(48000, 512); p.padHeld = true;
            juce::AudioBuffer<float> all(1, 512 * 80), buf(2, 512);
            for (int k = 0; k < 80; ++k) { buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); all.copyFrom(0, k * 512, buf, 0, 0, 512); }
            juce::File out = juce::File(argv[2]).getChildFile(v ? "vrouw.wav" : "man.wav"); out.deleteFile();
            juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(out), 48000, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer(all, 0, all.getNumSamples());
        }
        return 0;
    }
    if (argc > 2 && juce::String(argv[1]) == "--shot") { // UI snapshot
        SpreekbuisProcessor p; setP(p, "orca_on", 0); setP(p, "vowel_on", 1); setP(p, "vowel_glide", 2); setP(p, "all_items", 1);
        p.loadBankNow(); p.prepareToPlay(48000, 512);
        auto b = p.uiBank(); for (int i = 0; i < (int) b->items.size(); ++i) if (b->items[(size_t) i].nl == "Goed, dank je wel.") p.shownItem = i;
        p.shownWord = 1;
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(SpreekbuisEditor::W, SpreekbuisEditor::H);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File out(argv[2]); out.deleteFile(); juce::FileOutputStream os(out); juce::PNGImageFormat().writeImageToStream(img, os);
        return 0;
    }
    int fails = 0;
    auto check = [&](const char* name, Result r, bool wantMidi = false) {
        bool ok = r.finite && r.rms > 1e-4 && (!wantMidi || r.midiOut > 0);
        std::cout << (ok ? "PASS " : "FAIL ") << name << "  rms=" << r.rms << "  peak=" << r.peak << "  midiOut=" << r.midiOut << (r.finite ? "" : "  NaN!") << "\n";
        if (!ok) ++fails;
    };
    auto fresh = [](std::function<void(SpreekbuisProcessor&)> setup) {
        auto p = std::make_unique<SpreekbuisProcessor>();
        setP(*p, "orca_on", 0);
        for (auto id : { "loop_on", "cloud_on", "vowel_on", "g_on" }) setP(*p, id, 0);
        setP(*p, "all_items", 1); setP(*p, "space", 0);
        setup(*p);
        p->loadBankNow();
        p->prepareToPlay(48000, 512);
        return p;
    };
    {
        auto p = fresh([](auto& p) { setP(p, "loop_on", 1); setP(p, "loop_bpm", 160); setP(p, "loop_gap", 1); });
        auto b = p->uiBank();
        std::cout << "bank: " << b->items.size() << " items, pool " << b->pool.size() << ", lesson " << b->lesson[3].size() << " (" << b->status << ")\n";
        if (b->items.empty()) { std::cout << "FAIL no voice bank\n"; return 1; }
        check("lesson loop (+ whisper, MIDI out)", render(*p, 8, {}), true);
    }
    check("word cloud", render(*fresh([](auto& p) { setP(p, "cloud_on", 1); setP(p, "cloud_density", 30); setP(p, "cloud_whisper", 0.5f); }), 3, {}));
    check("keys -> words", render(*fresh([](auto& p) { setP(p, "keys", 0); }), 2, { 60, 64 }));
    check("vowel pad (glide ui)", render(*fresh([](auto& p) { setP(p, "vowel_on", 1); setP(p, "vowel_glide", 2); setP(p, "keys", 1); }), 2, { 48 }));
    check("vowel pad (noise)", render(*fresh([](auto& p) { setP(p, "vowel_on", 1); setP(p, "vowel_src", 1); setP(p, "keys", 1); }), 2, { 55 }));
    check("g hard", render(*fresh([](auto& p) { setP(p, "g_on", 1); setP(p, "keys", 2); }), 1, { 60 }));
    check("g soft (voiced)", render(*fresh([](auto& p) { setP(p, "g_on", 1); setP(p, "g_soft", 1); setP(p, "keys", 2); }), 1, { 48 }));
    {
        auto p = fresh([](auto&) {});
        check("orca messages (fiets, g7, vui, 1a)", render(*p, 2, {}, [&](int k) {
            if (k == 1) p->pushOrca("fiets"); if (k == 30) p->pushOrca("g7"); if (k == 60) p->pushOrca("vui"); if (k == 90) p->pushOrca("1a"); }));
    }
    {
        auto p = fresh([](auto& p) { for (auto id : { "loop_on", "cloud_on", "vowel_on", "g_on" }) setP(p, id, 1); setP(p, "keys", 4); setP(p, "space", 0.8f); });
        check("everything + space", render(*p, 4, { 60, 67 }));
        for (int i = 0; i < p->getNumPrograms(); ++i) { p->setCurrentProgram(i); }
        check("after cycling presets", render(*p, 2, { 62 }));
        juce::MemoryBlock mb; p->getStateInformation(mb); p->setStateInformation(mb.getData(), (int) mb.getSize());
        std::cout << "state round-trip " << mb.getSize() << " bytes\n";
    }
    // ---- the vowel chart and the ways it is used
    {
        auto p = fresh([](auto& p) { setP(p, "vowel_on", 1); });   // keys left on Words
        p->padHeld = true;
        check("klinker: pressing the chart plays it (no notes)", render(*p, 1.5, {}));
    }
    {
        auto p = fresh([](auto&) {});                                 // klinker switched off
        p->padHeld = true;
        check("klinker: the chart plays even with the layer off", render(*p, 1.5, {}));
    }
    {
        auto p = fresh([](auto&) {});
        check("klinker: orca ;vui plays with the layer off", render(*p, 1, {}, [&](int k) { if (k == 2) p->pushOrca("vui"); }));
    }
    {
        auto p = fresh([](auto& p) { setP(p, "vowel_on", 1); setP(p, "keys", 1); setP(p, "vowel_release", 0.1f); });
        juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buf(2, 512);
        for (int k = 0; k < 40; ++k) { buf.clear(); juce::MidiBuffer m; if (k == 0) m = on; p->processBlock(buf, m); }
        setP(*p, "vowel_on", 0);                                      // switched off while the note is held
        double tail = 0;
        for (int k = 0; k < 140; ++k) { buf.clear(); juce::MidiBuffer m; p->processBlock(buf, m); if (k >= 120) tail += buf.getRMSLevel(0, 0, 512); }
        const bool ok = tail / 20 < 1e-4;
        std::cout << (ok ? "PASS " : "FAIL ") << "klinker: switched off mid-note it fades out  tail rms=" << tail / 20 << "\n";
        if (!ok) ++fails;
    }
    // ---- loudness: everything at once, hot settings, never past full scale
    {
        auto p = fresh([](auto& p) { for (auto id : { "loop_on", "cloud_on", "vowel_on", "g_on" }) setP(p, id, 1);
                                     setP(p, "keys", 4); setP(p, "vowel_q", 3); setP(p, "master", 1.5f); setP(p, "cloud_density", 60);
                                     for (auto id : { "loop_level", "cloud_level", "vowel_level", "g_level", "whisper_level" }) setP(p, id, 1.5f); });
        auto r = render(*p, 4, { 36, 48, 60, 67 });
        const bool ok = r.finite && r.peak <= 1.0f;
        std::cout << (ok ? "PASS " : "FAIL ") << "everything hot: peak " << r.peak << " (soft ceiling 1.0)\n";
        if (!ok) ++fails;
    }
    // ---- the real Orca socket (a spare port, so a running Spreekbuis doesn't get in the way)
    {
        auto p = fresh([](auto& p) { setP(p, "orca_on", 1); });
        p->setOrcaPort(49177);
        p->testTick();
        juce::DatagramSocket out;
        const int sent = out.write("127.0.0.1", 49177, "g7", 2) + out.write("127.0.0.1", 49177, "veu", 3);
        juce::Thread::sleep(300);
        auto r = render(*p, 1, {});
        const bool ok = p->orcaListening.load() && sent == 5 && p->orcaPackets.load() == 2 && r.rms > 1e-4;
        std::cout << (ok ? "PASS " : "FAIL ") << "orca over udp: listening " << p->orcaListening.load() << ", packets " << p->orcaPackets.load() << ", rms " << r.rms << "\n";
        if (!ok) ++fails;
    }
    // ---- Klinker shaping the words and the cloud (Bron: Loop, Wolk, Loop + Wolk)
    {
        auto run = [&](int src, bool vowelOn, const char* layer) {
            // the whispered translations stay dry by design (they'd be the same in both renders and mask the shaping)
            auto p = fresh([&](auto& p) { setP(p, layer, 1); setP(p, "loop_bpm", 160); setP(p, "loop_whisper", 0); setP(p, "cloud_whisper", 0); setP(p, "cloud_density", 20);
                                           setP(p, "vowel_on", vowelOn ? 1.0f : 0.0f); setP(p, "vowel_src", (float) src); setP(p, "vowel_glide", 1); setP(p, "vowel_rate", 2); });
            juce::AudioBuffer<float> all(2, 512 * 300), buf(2, 512);
            for (int k = 0; k < 300; ++k) { buf.clear(); juce::MidiBuffer m; p->processBlock(buf, m); for (int c = 0; c < 2; ++c) all.copyFrom(c, k * 512, buf, c, 0, 512); }
            return all;
        };
        auto compare = [&](const char* name, int src, const char* layer) {
            auto dry = run(src, false, layer), shaped = run(src, true, layer);
            double d = 0, e = 0, s2 = 0, xy = 0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < dry.getNumSamples(); ++i) { const double x = dry.getSample(c, i), y = shaped.getSample(c, i); d += (x - y) * (x - y); e += x * x; s2 += y * y; xy += x * y; }
            const double rmsShaped = std::sqrt(s2 / (2.0 * dry.getNumSamples())), moved = std::sqrt(d / std::max(1e-12, e));
            // the formant filter changes the waveform's shape, not just its level: the part of the shaped signal that no gain
            // applied to the dry signal can explain (sine of the angle between them), independent of how loud or how bright
            // a voice bank's clips are
            const double cosA = xy / std::max(1e-12, std::sqrt(e * s2)), reshaped = std::sqrt(std::max(0.0, 1.0 - cosA * cosA));
            const bool ok = rmsShaped > 1e-3 && reshaped > 0.3;
            std::cout << (ok ? "PASS " : "FAIL ") << name << "  rms " << rmsShaped << " (dry " << std::sqrt(e / (2.0 * dry.getNumSamples())) << "), reshaped " << reshaped << " (level-inclusive difference " << moved << ")\n";
            if (!ok) ++fails;
        };
        compare("klinker shapes the loop (Bron Loop)", 4, "loop_on");
        compare("klinker shapes the cloud (Bron Wolk)", 5, "cloud_on");
        compare("klinker shapes both (Bron Loop + Wolk)", 6, "loop_on");
    }
    // ---- the voice switch: the woman's words and a higher vowel chart
    {
        // the chart pressed: the spectral centroid of a second of it (the woman's is higher: pitch and formants)
        auto zc = [&](float voice) {
            auto p = fresh([&](auto& p) { setP(p, "voice", voice); setP(p, "vowel_on", 1); setP(p, "vowel_src", 0); });
            p->padHeld = true;
            juce::AudioBuffer<float> buf(2, 512); std::vector<float> x;
            for (int k = 0; k < 120; ++k) { buf.clear(); juce::MidiBuffer m; p->processBlock(buf, m); if (k >= 30 && x.size() < 32768) x.insert(x.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); }
            x.resize(32768);
            juce::dsp::FFT fft(15); std::vector<float> d(65536, 0.0f); std::copy(x.begin(), x.end(), d.begin());
            fft.performFrequencyOnlyForwardTransform(d.data());
            double num = 0, den = 0; for (int i = 1; i < 16384; ++i) { num += d[(size_t) i] * (i * 48000.0 / 32768.0); den += d[(size_t) i]; }
            return std::make_pair((int) (num / den), p->uiBank());
        };
        auto [man, bm] = zc(0); auto [vrouw, bv] = zc(1);
        const bool ok = vrouw > man * 1.05 && bm && !bm->female && bv && bv->female && !bv->items.empty();
        std::cout << (ok ? "PASS " : "FAIL ") << "voice switch: chart centroid man (Hz) " << man << " / vrouw " << vrouw
                  << "; words " << bm->voiceName << " / " << bv->voiceName << " (" << bv->items.size() << " items)\n";
        if (!ok) ++fails;
    }
    std::cout << (fails ? "FAILED " : "ALL PASSED ") << fails << "\n";
    return fails ? 1 : 0;
}
