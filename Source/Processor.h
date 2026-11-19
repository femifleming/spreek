// Spreekbuis 2 — Dutch as an instrument, with a speech computer inside. One processor: the word bank's layers (lesson
// loop, word cloud, whispered translations, vowel chart, g) and the formant speech computer (Spraakmaker 1.0) share one
// voice, one clock, one modulation matrix, one mixer and one set of effects, and feed each other.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Bank.h"
#include "Dsp.h"
#include "Planner.h"
#include "Voice.h"
#include "Vocoder.h"
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
    double getTailLengthSeconds() const override { return 3.0; }

    // programs: Init, the nine of Spreekbuis 1.0, the twenty of Spraakmaker 1.0, then the ones that use both
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    void loadProgram(int);     // the editor's choice: always loads it, even the current one
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- the word bank, shared with the editor
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
    // for the editor's hints: is any audio coming in, and do notes reach the layers?
    std::atomic<float> inputLevel { 0.0f };
    bool notesReachVowel() const { const int k = (int) *apvts.getRawParameterValue("keys"); return k == 1 || k == 3 || k == 4; }
    bool notesReachSpraak() const { const int k = (int) *apvts.getRawParameterValue("keys"); return k == 4 || k == 5; }

    // ---- the speech computer, shared with the editor
    sm::Planner& planner() { return *plan; }
    void manualTrigger() { manualTrig.store(true); }
    void typeAndSpeak(const juce::String& t) { plan->speakNow(t); manualTrig.store(true); }
    bool loadScl(const juce::File& f);
    juce::String sclName() const { return sclTitle; }
    struct Display { char word[sm::kWordChars] {}; char text[160] {}; int phon = 0; };
    Display display() const;
    float meter() const { return meterLevel.load(); }
    int triggerCount() const { return trigCounter.load(); }
    bool externalCarrier() const { return extCarrier.load(); }
    bool externalModulator() const { return extModulator.load(); }
    bool speaking() const { return speakingNow.load(); }

    // ---- what the editor draws
    std::array<std::atomic<float>, 6> layerLevel {};   // loop, wolk, fluister, klinker, g, spraak (peak, decaying)
    std::atomic<float> outLevel { 0 };
    std::atomic<int> onsetCount { 0 };                 // counts the word onsets of the bank (the picture flashes)
    std::array<std::atomic<float>, 4> lfoShown {};     // -1..1
    std::array<std::atomic<float>, sm::prm::dCount> modShown {};   // the matrix's offset per destination
    juce::MidiKeyboardState keyState;                  // the on-screen keys

    // ---- presets as files (the whole instrument), and what Spraakmaker 1.0 wrote
    juce::File presetDir() const;                                     // …/Spreekbuis/presets
    bool savePresetFile(const juce::File&, const juce::String& name);
    // a Spreekbuis preset or state, or anything Spraakmaker 1.0 saved: its preset slots (pNN.xml), a state a host
    // stored (.vstpreset, .aupreset, .fxp, a chunk), found by its content
    bool loadPresetFile(const juce::File&);
    bool loadAnyState(const void* data, size_t size);                 // the same, from memory
    bool importSpraakmaker(const juce::XmlElement& params);           // <PARAMS …> of Spraakmaker 1.0
    juce::String presetLabel() const { return shownPreset; }

    // ---- tests
    void loadBankNow();
    void testTick() { timerCallback(); }
    void flushPlaysNow() { timerCallback(); flushPlays(); }
    // offline tests: planner driven synchronously from processBlock and fixed random seeds, so a render is repeatable
    void setOfflineTest(int seed) { plan->setSynchronous(true); rnd.setSeed(seed); }

private:
    void timerCallback() override;
    void startBankLoad();
    void adoptBank(std::shared_ptr<sb::Bank>);
    void flushPlays();
    void countPlay(int item);
    int pickLessonItem(const sb::Bank&, int set);
    void triggerWord(const sb::Bank&, int item, float pitchSemis, float gain);
    void handleNoteOn(const sb::Bank*, int note, int vel);
    void handleNoteOff(int note);
    void processOrcaEvents(const sb::Bank*);
    bool applyStateXml(const juce::XmlElement&);
    void resetToDefaults();

    // the speech computer
    void startNext(float velocity);
    void startNextFromScratch(float velocity);
    void handleTrigger(bool on, float velocity);
    void updateTuning();
    float noteCents(int note) const;
    void publishDisplay(const sm::Voice& v);
    void renderSpraak(int n, juce::MidiBuffer& out, const float* off, bool run, double bpm, double ppq, bool playing, double beatsPerBar, bool hasCar, bool hasMod);

    // bank handoff
    mutable juce::SpinLock bankLock;
    std::shared_ptr<sb::Bank> bankOwner;            // message thread owner
    std::atomic<const sb::Bank*> bankPtr { nullptr }; // audio thread view
    std::vector<std::pair<std::shared_ptr<sb::Bank>, int>> graveyard;
    std::unique_ptr<juce::Thread> loader;
    std::atomic<bool> reloadPending { true };
    int loadedUnit = -1; bool loadedAll = false, loadedFemale = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);   // (a bank that arrives after we are gone)

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

    // ---- dsp state: the bank's layers
    double sr = 48000;
    sb::Rng rng;
    double internalPpq = 0; long long lastCycle = -1; bool whisperFiredThisCycle = false, echoFiredThisCycle = false;
    int lessonPos = -1, loopItem = -1, loopWord = -1;
    sb::OneShot loopVoice, loopWhisper;
    struct PendingOff { int note, samples; };
    std::vector<PendingOff> pendingOffs;
    std::array<sb::OneShot, 8> shots; int nextShot = 0;
    std::array<sb::Grain, 64> grains;
    double grainCountdown = 0;
    std::set<int> heldItems;
    std::array<int, 128> noteItem {};
    sb::Whisperizer whisper;
    sb::VowelVoice vowel;
    std::vector<int> vowelNotes;
    float glidePh = 0, f1s = 500, f2s = 1500, f3s = 2500;
    int orcaVowelHold = 0; float orcaVX = 0.5f, orcaVY = 0.5f;
    bool padWasHeld = false; float lastVowelFreq = -1.0f;   // < 0: no note yet (the chart then plays A2, or A3 for the woman's voice)
    sb::GVoice gv;
    std::vector<int> gNotes; int orcaGHold = 0;
    juce::Reverb reverb;                                    // "ruimte": the room everything stands in
    juce::AudioBuffer<float> inputCopy;
    std::vector<float> wordsL, wordsR, cloudL, cloudR, whisL, whisR;   // the layers of this block, before they are routed
    int currentProgram = 0;
    juce::String shownPreset;
    double timeSec = 0;
    const sb::Bank* lastBank = nullptr;
    struct Onset { int pos, note; };
    std::array<Onset, 64> onsets {}; int nOnsets = 0;       // the loop's word onsets in this block
    int echoAt = -1;                                        // sample at which the answer bars begin in this block

    // ---- dsp state: the speech computer
    std::unique_ptr<sm::Planner> plan;
    sm::Voice voices[4];
    sm::VoiceEvents events[4];
    sm::Utterance scratch;
    sm::Vocoder vocoder;
    sm::PolyOsc carrierOsc;
    juce::AudioBuffer<float> speechBuf, extMod;
    std::vector<float> voiceTmp, spk, bankMod;
    int held[16] {}; int nHeld = 0; float lastVel = 1.f; float modWheel = 0.f;
    bool gate = false;
    std::atomic<bool> manualTrig { false };
    int pendingTrig = 0; float pendingVel = 1.f;
    int pendingEcho = 0;
    int freeGap = 0;
    float randCv[4] {};
    float lfoPhase[4] {}, lfoSH[4] {};
    float baseSemis[4] { 48, 48, 48, 48 };
    float startOff = 0;
    juce::Random rnd;
    bool forcedSpeech = false;       // spoken on request (Enter, the trigger, Orca) while the layer is switched off
    bool spraakWasOn = false;
    int loopNote = -1;               // the pitch the loop's words give the speech computer (loop_trig: trigger + toon)
    float wordEnv = 0, bankEnv = 0, spraakEnv = 0;   // modulation sources
    // effects
    struct Delay { std::vector<float> l, r; int w = 0; } dly;
    std::vector<float> widthBuf; int widthW = 0; float widthPhase = 0;
    juce::Reverb fxReverb;
    float crushS = 0, crushL = 0, crushR = 0; int crushCount = 0;
    int fxTail = 0;
    std::vector<float> fxL, fxR;
    // tuning
    std::array<std::atomic<float>, 128> centsTable;
    std::vector<double> sclCents;
    juce::String sclTitle;
    int lastMicro = -1;
    // midi out of the speech computer
    std::vector<PendingOff> spraakOffs;
    // display
    mutable juce::SpinLock displayLock;
    Display disp;
    std::atomic<float> meterLevel { 0 };
    std::atomic<int> trigCounter { 0 };
    std::atomic<bool> extCarrier { false }, extModulator { false }, speakingNow { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpreekbuisProcessor)
};
