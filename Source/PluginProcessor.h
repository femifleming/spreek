#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Bank.h"
#include "Dsp.h"
#include <atomic>
#include <set>

class OrcaListener;

class SpreekbuisProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    SpreekbuisProcessor();
    ~SpreekbuisProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Spreekbuis"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    // ---- shared with the editor
    std::shared_ptr<sb::Bank> uiBank() const { const juce::SpinLock::ScopedLockType l(bankLock); return bankOwner; }
    std::atomic<int> shownItem { -1 }, shownWord { -1 };
    std::atomic<float> padX { 0.5f }, padY { 0.5f };
    std::atomic<bool> orcaListening { false };
    std::atomic<int> orcaPackets { 0 };
    int orcaPort() const { return (int) state.getProperty("orcaPort", 49162); }
    void setOrcaPort(int p);
    void requestReload() { reloadPending = true; }
    juce::ValueTree state { "SpreekbuisState" };

    // Orca events (from the network thread)
    void pushOrca(const juce::String& msg);

    // the vowel chart pressed with the mouse: it plays the vowel itself (message thread writes, audio thread reads)
    std::atomic<bool> padHeld { false };
    // for the editor's hints: is any audio coming in, and do notes reach the vowel?
    std::atomic<float> inputLevel { 0.0f };
    bool notesReachVowel() const { const int k = (int) *apvts.getRawParameterValue("keys"); return k == 1 || k == 3 || k == 4; }

    // test hooks
    void loadBankNow();
    void testTick() { timerCallback(); }

private:
    void timerCallback() override;
    void startBankLoad();
    void flushPlays();
    void countPlay(int item);
    int pickLessonItem(const sb::Bank&, int set);
    void triggerWord(const sb::Bank&, int item, float pitchSemis, float gain);
    void handleNoteOn(const sb::Bank*, int note, int vel);
    void handleNoteOff(int note);
    void processOrcaEvents(const sb::Bank*);

    // bank handoff
    mutable juce::SpinLock bankLock;
    std::shared_ptr<sb::Bank> bankOwner;            // message thread owner
    std::atomic<const sb::Bank*> bankPtr { nullptr }; // audio thread view
    std::vector<std::pair<std::shared_ptr<sb::Bank>, int>> graveyard;
    std::unique_ptr<juce::Thread> loader;
    std::atomic<bool> reloadPending { true };
    int loadedUnit = -1; bool loadedAll = false, loadedFemale = false;

    // play log
    juce::AbstractFifo playFifo { 1024 };
    std::array<int, 1024> playBuf {};
    std::map<juce::String, int> pendingPlays;
    int flushCountdown = 0;

    // orca
    std::unique_ptr<OrcaListener> orca;
    juce::AbstractFifo orcaFifo { 256 };
    std::array<juce::String, 256> orcaBuf;
    juce::CriticalSection orcaWriteLock;

    // dsp state
    double sr = 48000;
    sb::Rng rng;
    // lesson loop
    double internalPpq = 0; long long lastCycle = -1; bool whisperFiredThisCycle = false;
    int lessonPos = -1, loopItem = -1, loopWord = -1;
    sb::OneShot loopVoice, loopWhisper;
    struct PendingOff { int note, samples; };
    std::vector<PendingOff> pendingOffs;
    // one-shots from keys / orca
    std::array<sb::OneShot, 8> shots; int nextShot = 0;
    // cloud
    std::array<sb::Grain, 64> grains;
    double grainCountdown = 0;
    std::set<int> heldItems;
    std::array<int, 128> noteItem {};
    // whisper bus
    sb::Whisperizer whisper;
    // vowel
    sb::VowelVoice vowel;
    std::vector<int> vowelNotes;
    float glidePh = 0, f1s = 500, f2s = 1500, f3s = 2500;
    int orcaVowelHold = 0; float orcaVX = 0.5f, orcaVY = 0.5f;
    bool padWasHeld = false; float lastVowelFreq = -1.0f;   // < 0: no note yet (the chart then plays A2, or A3 for the woman's voice)
    // g
    sb::GVoice gv;
    std::vector<int> gNotes; int orcaGHold = 0;
    // space
    juce::Reverb reverb;
    juce::AudioBuffer<float> inputCopy;
    int currentProgram = 0;
    double timeSec = 0;
    const sb::Bank* lastBank = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpreekbuisProcessor)
};
