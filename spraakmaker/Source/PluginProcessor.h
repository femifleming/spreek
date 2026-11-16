// Spraakmaker — speech computer instrument (an homage to a Eurorack speech module), English + Dutch.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Planner.h"
#include "Voice.h"
#include "Vocoder.h"

class SpraakProcessor : public juce::AudioProcessor
{
public:
    SpraakProcessor();
    ~SpraakProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Spraakmaker"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 100; }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int i) override { loadPreset (i); }
    const juce::String getProgramName (int i) override { return presetName (i); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor ----
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", sm::prm::layout() };
    sm::Planner& planner() { return *plan; }
    void manualTrigger() { manualTrig.store (true); }
    void typeAndSpeak (const juce::String& t) { plan->speakNow (t); manualTrig.store (true); }
    juce::String presetName (int i) const;
    void loadPreset (int i);
    void savePreset (int i, const juce::String& name);
    bool loadScl (const juce::File& f);
    juce::String sclName() const { return sclTitle; }
    juce::File dataDir() const;
    // offline tests: planner driven synchronously from processBlock and a fixed random seed, so a render is repeatable
    void setOfflineTest (int seed) { plan->setSynchronous (true); rnd.setSeed (seed); }

    // display snapshot
    struct Display { char word[sm::kWordChars] {}; char text[160] {}; int phon = 0; };
    Display display() const;
    float meter() const { return meterLevel.load(); }
    int triggerCount() const { return trigCounter.load(); }
    bool externalCarrier() const { return extCarrier.load(); }
    bool externalModulator() const { return extModulator.load(); }

private:
    void startNext (float velocity);
    void startNextFromScratch (float velocity);
    int pendingTrig = 0; float pendingVel = 1.f;
    void handleTrigger (bool on, float velocity);
    void updateTuning();
    float noteCents (int note) const;
    void publishDisplay (const sm::Voice& v);

    std::unique_ptr<sm::Planner> plan;
    sm::Voice voices[4];
    sm::VoiceEvents events[4];
    sm::Utterance scratch;
    sm::Vocoder vocoder;
    sm::PolyOsc carrierOsc;

    double sr = 48000.0;
    juce::AudioBuffer<float> speechBuf, carBuf, modBuf, extCar, extMod;
    std::vector<float> voiceTmp;

    // midi / triggers
    int held[16] {}; int nHeld = 0; float lastVel = 1.f; float modWheel = 0.f;
    bool gate = false;
    std::atomic<bool> manualTrig { false };
    double autoPhase = 0;       // free-running clock in quarter notes when transport is stopped
    double lastPpq = -1;
    int freeGap = 0;
    float randCv[4] {};
    float lfoPhase[4] {}, lfoSH[4] {};
    float baseSemis[4] { 48, 48, 48, 48 };
    float startOff = 0;
    juce::Random rnd;

    // FX
    struct Delay { std::vector<float> l, r; int w = 0; } dly;
    std::vector<float> widthBuf; int widthW = 0; float widthPhase = 0;
    juce::Reverb reverb;
    float crushHold = 0; int crushCount = 0;
    float crushHoldR = 0;

    // tuning
    std::array<std::atomic<float>, 128> centsTable;
    std::vector<double> sclCents;
    juce::String sclTitle;
    int lastMicro = -1;

    // midi out
    struct PendingOff { int note; int samples; };
    std::vector<PendingOff> pendingOffs;

    // display
    mutable juce::SpinLock displayLock;
    Display disp;
    std::atomic<float> meterLevel { 0 };
    std::atomic<int> trigCounter { 0 };
    std::atomic<bool> extCarrier { false }, extModulator { false };
    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpraakProcessor)
};
