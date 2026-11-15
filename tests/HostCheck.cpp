// SpreekHostCheck — loads the BUILT plug-ins the way a DAW does and checks that they make sound.
//   SpreekHostCheck file.vst3|file.component ...
// For each file: instantiate (VST3 / AU), stereo main bus, 48 kHz / 512, host transport playing at 122 bpm.
// Instruments get MIDI (kit notes 36-43, modules / synth: a note every half beat); effects get a drum-like
// test signal. Prints peak / rms of the main output for: transport playing (no midi), midi notes, and the
// first three programs. FAIL when the main output is silent.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 122; bool playing = true; juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq);
        p.setTimeInSamples (samples); p.setTimeSignature (TimeSignature { 4, 4 }); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return p;
    }
};

static int failures = 0;

static void run (juce::AudioPluginInstance& p, bool instrument, bool isKit, bool sendMidi, bool playing, double secs, float& peak, float& rms)
{
    const double sr = 48000.0; const int bs = 512;
    Head head; head.playing = playing; p.setPlayHead (&head);
    p.prepareToPlay (sr, bs);
    const int ch = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> b (std::max (ch, 2), bs);
    double sum = 0; juce::int64 n = 0; peak = 0;
    const int total = (int) (secs * sr), every = (int) (sr * 60.0 / head.bpm / 2.0);
    juce::Random rnd (7);
    int note = 0;
    for (int pos = 0; pos < total; pos += bs)
    {
        b.clear();
        juce::MidiBuffer m;
        if (! instrument)
            for (int k = 0; k < bs; ++k)
            {
                const int t = (pos + k) % every;
                const float env = std::exp (-(float) t / 2400.0f);
                const float s = env * (0.6f * std::sin ((float) t * 0.012f) + 0.2f * (rnd.nextFloat() * 2 - 1));
                b.setSample (0, k, s); b.setSample (1, k, s);
            }
        else if (sendMidi)
            for (int k = 0; k < bs; ++k)
                if ((pos + k) % every == 0)
                {
                    const int key = isKit ? 36 + (note % 8) : 48 + (note % 3) * 5;
                    m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 110), k);
                    m.addEvent (juce::MidiMessage::noteOff (1, key), std::min (bs - 1, k + 400));
                    ++note;
                }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < bs; ++k) { const float x = b.getSample (c, k); sum += x * x; peak = std::max (peak, std::abs (x)); }
        n += 2 * bs;
        head.samples += bs; head.ppq += bs / sr * head.bpm / 60.0;
        if (pos < 30 * bs) juce::Thread::sleep ((int) (bs * 1000.0 / sr));   // the first blocks at real-time pace, as a host delivers them (background threads catch up)
    }
    p.releaseResources();
    rms = (float) std::sqrt (sum / (double) std::max<juce::int64> (1, n));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    auto db = [] (float x) { return juce::String (juce::Decibels::gainToDecibels (x, -150.0f), 1); };
    for (int a = 1; a < argc; ++a)
    {
        const juce::String path (argv[a]);
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : fm.getFormats())
            if (f->fileMightContainThisPluginType (path)) { f->findAllTypesForFile (found, path); if (! found.isEmpty()) break; }
        if (found.isEmpty()) { std::cout << "FAIL " << path << ": not loadable\n"; ++failures; continue; }
        juce::String err;
        auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
        if (! inst) { std::cout << "FAIL " << path << ": " << err << "\n"; ++failures; continue; }
        // let it settle as it would in a DAW: plug-ins that load things on the message thread (timers) get to run
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        // stereo main in/out, aux buses off (a plain Live track)
        auto layout = inst->getBusesLayout();
        for (int i = 0; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        for (int i = 0; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        inst->setBusesLayout (layout);
        const bool instrument = found[0]->isInstrument;
        const bool isKit = found[0]->name.endsWith ("kt1");
        std::cout << found[0]->name << " (" << found[0]->pluginFormatName << ", " << inst->getTotalNumOutputChannels() << " out ch, "
                  << inst->getNumPrograms() << " programs, program " << inst->getCurrentProgram() << " \"" << inst->getProgramName (inst->getCurrentProgram()) << "\")\n";
        float pk, rm; bool any = false;
        if (instrument)
        {
            run (*inst, true, isKit, false, true, 4.0, pk, rm);
            std::cout << "  transport, no midi:  peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            if (isKit || found[0]->name.endsWith ("kt2")) any |= pk > 1.0e-3f;
            run (*inst, true, isKit, true, false, 4.0, pk, rm);
            std::cout << "  midi, stopped:       peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            any |= pk > 1.0e-3f;
            if (pk <= 1.0e-3f) { std::cout << "  FAIL midi notes are silent\n"; ++failures; }
        }
        else
        {
            run (*inst, false, false, false, true, 4.0, pk, rm);
            std::cout << "  effect, test signal: peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            any = pk > 1.0e-3f;
        }
        for (int pr = 0; pr < std::min (3, inst->getNumPrograms()); ++pr)
        {
            inst->setCurrentProgram (pr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            run (*inst, instrument, isKit, true, true, 2.0, pk, rm);
            std::cout << "  program " << pr << " \"" << inst->getProgramName (pr) << "\": peak " << db (pk) << " dB\n";
            if (pk <= 1.0e-3f && inst->getProgramName (pr) != "empty") { std::cout << "  FAIL program silent\n"; ++failures; }
        }
        if (! any) { std::cout << "  FAIL silent\n"; ++failures; }
    }
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failures\n");
    return failures == 0 ? 0 : 1;
}
