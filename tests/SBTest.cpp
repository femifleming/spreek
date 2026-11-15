// Offline test: render each Spreekbuis layer headless and check the output is audible and finite.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

static void setP(SpreekbuisProcessor& p, const char* id, float v) {
    auto* rp = p.apvts.getParameter(id);
    rp->setValueNotifyingHost(rp->convertTo0to1(v));
}
struct Result { double rms = 0; bool finite = true; int midiOut = 0; };
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
        for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { float v = buf.getSample(c, i); if (!std::isfinite(v)) r.finite = false; sum += v * v; ++n; }
    }
    r.rms = std::sqrt(sum / std::max(1L, n));
    return r;
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
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
        std::cout << (ok ? "PASS " : "FAIL ") << name << "  rms=" << r.rms << "  midiOut=" << r.midiOut << (r.finite ? "" : "  NaN!") << "\n";
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
    std::cout << (fails ? "FAILED " : "ALL PASSED ") << fails << "\n";
    return fails ? 1 : 0;
}
