// Full-plugin test: every factory preset rendered through processBlock (MIDI triggers, AUTO clock, vocoder),
// plus an editor snapshot of each OLED page.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File out (argc > 1 ? argv[1] : "/tmp");
    out.createDirectory();
    std::unique_ptr<SpraakProcessor> p (dynamic_cast<SpraakProcessor*> (createPluginFilter()));
    const double sr = 48000; const int bs = 512;
    p->setPlayConfigDetails (0, 2, sr, bs);
    p->prepareToPlay (sr, bs);
    juce::Thread::sleep (1500);       // planner loads dictionaries
    int fails = 0;
    const int only = argc > 2 ? juce::String (argv[2]).getIntValue() : -1;
    for (int preset = 0; preset < 20; ++preset)
    {
        if (only >= 0 && preset != only) continue;
        p->setCurrentProgram (preset);
        juce::Thread::sleep (600);
        juce::AudioBuffer<float> buf (2, bs);
        juce::AudioBuffer<float> all (2, (int) (sr * 6));
        all.clear();
        double ss = 0; float pk = 0; bool finite = true;
        int midiOut = 0;
        for (int b = 0; b * bs < all.getNumSamples() - bs; ++b)
        {
            buf.clear();
            juce::MidiBuffer m;
            if (b % 60 == 1) m.addEvent (juce::MidiMessage::noteOn (1, 48 + (b / 60) % 12, (juce::uint8) 100), 10);
            if (b % 60 == 40) m.addEvent (juce::MidiMessage::noteOff (1, 48 + (b / 60) % 12), 10);
            if (b % 20 == 5 && preset == 6) m.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0);
            if (b % 20 == 15 && preset == 6) m.addEvent (juce::MidiMessage::noteOff (1, 50), 0);
            p->processBlock (buf, m);
            for (const auto meta : m) if (meta.getMessage().isNoteOn()) ++midiOut;
            for (int c = 0; c < 2; ++c) all.copyFrom (c, b * bs, buf, c, 0, bs);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float x = buf.getSample (c, i); finite &= std::isfinite (x); ss += x * x; pk = std::max (pk, std::fabs (x)); }
            if (b % 30 == 0) juce::Thread::sleep (2);   // let the planner refill
            if (only >= 0 && b % 50 == 0) std::cout << "  b" << b << " gen " << p->planner().generation() << " trig " << p->triggerCount() << " lang " << p->planner().lang.load() << " src " << p->planner().source.load() << "\n";
        }
        const double rms = std::sqrt (ss / (all.getNumSamples() * 2.0));
        const bool ok = finite && rms > 0.003;
        if (! ok) ++fails;
        std::cout << (ok ? "OK  " : "BAD ") << juce::String::formatted ("P%02d %-14s", preset + 1, p->getProgramName (preset).toRawUTF8())
                  << " rms " << rms << " peak " << pk << " midiOut " << midiOut << " last: \"" << p->display().text << "\"\n";
        juce::WavAudioFormat wav;
        auto f = out.getChildFile (juce::String::formatted ("preset%02d.wav", preset + 1)); f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    }
    // editor snapshots
    p->setCurrentProgram (0);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (850, 1080);
    auto shoot = [&] (const char* name) {
        juce::Thread::sleep (60);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile (name); f.deleteFile();
        juce::PNGImageFormat png; auto os = f.createOutputStream(); png.writeImageToStream (img, *os);
    };
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2200);
    shoot ("ui_home.png");
    for (int pg = 0; pg < 4; ++pg)
    {
        if (auto* se = dynamic_cast<SpraakEditor*> (ed.get())) se->showPage (pg + 1);
        shoot (juce::String::formatted ("ui_page%d.png", pg + 1).toRawUTF8());
    }
    ed.reset();
    std::cout << (fails ? "FAILURES " : "ALL OK ") << fails << "\n";
    return fails ? 1 : 0;
}
