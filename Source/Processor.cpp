#include "Processor.h"
#include "Editor.h"

using namespace sb;
using namespace sm;
using APVTS = juce::AudioProcessorValueTreeState;

// ---------------------------------------------------------------- Orca UDP bridge
class OrcaListener : public juce::Thread {
public:
    OrcaListener(SpreekbuisProcessor& p, int port) : juce::Thread("Spreekbuis Orca"), proc(p), port(port) {}
    ~OrcaListener() override { if (sock) sock->shutdown(); stopThread(1000); }
    bool open() {
        sock = std::make_unique<juce::DatagramSocket>(false);
        if (!sock->bindToPort(port, "127.0.0.1")) { sock.reset(); return false; }
        startThread();
        return true;
    }
    void run() override {
        char buf[512];
        while (!threadShouldExit() && sock) {
            if (sock->waitUntilReady(true, 200) != 1) continue;
            int n = sock->read(buf, sizeof(buf) - 1, false);
            if (n <= 0) continue;
            buf[n] = 0;
            proc.pushOrca(juce::String::fromUTF8(buf, n).trim());
        }
    }
    SpreekbuisProcessor& proc;
    int port;
    std::unique_ptr<juce::DatagramSocket> sock;
};

// ---------------------------------------------------------------- bank loader
class BankLoader : public juce::Thread {
public:
    BankLoader(BankRequest r, std::function<void(std::shared_ptr<Bank>)> done)
        : juce::Thread("Spreekbuis bank"), req(r), onDone(std::move(done)) {}
    ~BankLoader() override { stopThread(4000); }
    void run() override {
        auto b = loadBank(req, [this] { return threadShouldExit(); });
        if (b && !threadShouldExit()) onDone(b);
    }
    BankRequest req;
    std::function<void(std::shared_ptr<Bank>)> onDone;
};

// ---------------------------------------------------------------- programs
namespace {
enum Kind { kBank, kSpraak, kBoth };
struct Program { const char* name; Kind kind; std::vector<std::pair<const char*, float>> v; };
// a Spraakmaker patch inside the instrument: only the speech computer, at the level and in the room it had
const std::vector<std::pair<const char*, float>>& spraakBase() {
    static const std::vector<std::pair<const char*, float>> b { { "spraak_on", 1 }, { "loop_on", 0 }, { "cloud_on", 0 }, { "vowel_on", 0 }, { "g_on", 0 },
                                                                 { "keys", 5 }, { "master", 1.0f }, { "space", 0 }, { "loop_bpm", 120 } };
    return b;
}
const std::vector<Program>& programs() {
    static const std::vector<Program> p {
        // ---- Spreekbuis 1.0
        { "Les in de loop", kBank, { { "loop_on", 1 }, { "cloud_on", 0 }, { "vowel_on", 0 }, { "g_on", 0 }, { "loop_set", 0 }, { "loop_gap", 1 }, { "space", 0.15f } } },
        { "Zinnen en echo", kBank, { { "loop_on", 1 }, { "loop_set", 1 }, { "loop_bars", 1 }, { "loop_gap", 2 }, { "cloud_on", 1 }, { "cloud_density", 3 }, { "cloud_size", 400 }, { "space", 0.5f } } },
        { "Fluisterwolk", kBank, { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 30 }, { "cloud_size", 120 }, { "cloud_scatter", 1 }, { "cloud_whisper", 0.6f }, { "whisper_level", 0.9f }, { "space", 0.6f } } },
        { "Droomstaat", kBank, { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 2 }, { "cloud_size", 800 }, { "cloud_pitch", -7 }, { "cloud_whisper", 0.4f }, { "vowel_on", 1 }, { "vowel_glide", 3 }, { "vowel_rate", 0.05f }, { "vowel_level", 0.3f }, { "space", 0.9f }, { "keys", 3 } } },
        { "Ui-drone", kBank, { { "loop_on", 0 }, { "vowel_on", 1 }, { "vowel_glide", 2 }, { "vowel_rate", 0.12f }, { "vowel_noise", 0.35f }, { "vowel_release", 3 }, { "space", 0.7f }, { "keys", 1 } } },
        { "Klinkerkoor", kBank, { { "loop_on", 0 }, { "vowel_on", 1 }, { "vowel_src", 3 }, { "vowel_q", 1.8f }, { "vowel_glide", 1 }, { "vowel_rate", 0.4f }, { "space", 0.5f }, { "keys", 1 } } },
        { "Harde g", kBank, { { "loop_on", 0 }, { "g_on", 1 }, { "g_soft", 0 }, { "g_rasp", 0.9f }, { "g_release", 0.08f }, { "keys", 2 }, { "space", 0.2f } } },
        { "Zachte g", kBank, { { "loop_on", 0 }, { "g_on", 1 }, { "g_soft", 1 }, { "g_voice", 0.8f }, { "g_rasp", 0.2f }, { "g_release", 0.5f }, { "keys", 2 }, { "space", 0.4f } } },
        { "Orca-woorden", kBank, { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 0 }, { "orca_on", 1 }, { "g_on", 1 }, { "vowel_on", 1 }, { "keys", 4 }, { "space", 0.3f } } },
        // ---- Spraakmaker 1.0
        { "Hallo NL", kSpraak, { { "lang", 1 }, { "source", 5 }, { "list", 1 }, { "rate", 5 } } },
        { "Classic EN", kSpraak, { { "lang", 0 }, { "source", 0 }, { "rate", 4 }, { "bright", 0.65f } } },
        { "Markov NL", kSpraak, { { "lang", 1 }, { "source", 3 }, { "rate", 6 }, { "revmix", 0.25f } } },
        { "Markov Alice", kSpraak, { { "lang", 0 }, { "source", 3 }, { "rate", 6 }, { "revmix", 0.2f } } },
        { "Robotkoor", kSpraak, { { "lang", 1 }, { "engine", 2 }, { "poly", 4 }, { "chord", 8 }, { "source", 5 }, { "list", 4 }, { "rate", 6 }, { "revmix", 0.3f } } },
        { "Lettrisme", kSpraak, { { "lang", 1 }, { "source", 4 }, { "rate", 3 }, { "speed", 0.7f }, { "emotion", 0.7f }, { "dlymix", 0.25f } } },
        { "Ph.gate stot", kSpraak, { { "lang", 1 }, { "trig", 4 }, { "source", 5 }, { "list", 3 } } },
        { "Alfabet NL", kSpraak, { { "lang", 1 }, { "source", 1 }, { "rate", 4 } } },
        { "Alphabet EN", kSpraak, { { "lang", 0 }, { "source", 1 }, { "rate", 4 } } },
        { "Fluister", kSpraak, { { "lang", 1 }, { "emotion", 0.05f }, { "bright", 0.25f }, { "revmix", 0.45f }, { "revsize", 0.8f } } },
        { "Kind", kSpraak, { { "lang", 1 }, { "formant", 0.82f }, { "pitch", 12 }, { "emotion", 0.75f }, { "source", 5 }, { "list", 1 } } },
        { "Diepe stem", kSpraak, { { "lang", 1 }, { "formant", 0.22f }, { "pitch", -10 }, { "speed", 0.38f }, { "revmix", 0.3f } } },
        { "Gabber spraak", kSpraak, { { "lang", 1 }, { "speed", 0.85f }, { "bits", 6 }, { "down", 4 }, { "emotion", 1.0f }, { "rate", 2 }, { "source", 4 } } },
        { "Vocoder ext", kSpraak, { { "engine", 1 }, { "bands", 20 }, { "trig", 1 } } },
        { "Lunar storm", kSpraak, { { "lang", 0 }, { "source", 5 }, { "list", 5 }, { "revmix", 0.5f }, { "dlymix", 0.3f }, { "cv1src", 1 }, { "cv1dst", 2 }, { "cv1amt", 0.3f }, { "rate", 6 } } },
        { "Typmachine", kSpraak, { { "lang", 1 }, { "source", 2 }, { "trig", 1 } } },
        { "Micro 24", kSpraak, { { "lang", 1 }, { "engine", 2 }, { "poly", 4 }, { "chord", 10 }, { "micro", 4 }, { "source", 3 }, { "rate", 6 } } },
        { "Telwoorden", kSpraak, { { "lang", 1 }, { "source", 5 }, { "list", 2 }, { "rate", 3 } } },
        { "Sprekende lfo", kSpraak, { { "lang", 1 }, { "source", 0 }, { "cv1src", 2 }, { "cv1dst", 1 }, { "cv1amt", 0.25f }, { "cv2src", 1 }, { "cv2dst", 6 }, { "cv2amt", 0.6f }, { "rate", 4 } } },
        { "English keys", kSpraak, { { "lang", 0 }, { "source", 2 }, { "trig", 1 } } },
        // ---- both: the bank's voice and the speech computer working on each other
        { "Les met echo", kBoth, { { "loop_on", 1 }, { "loop_gap", 1 }, { "loop_whisper", 0 }, { "spraak_on", 1 }, { "trig", 1 }, { "loop_echo", 1 }, { "lang", 1 }, { "revmix", 0.2f }, { "space", 0.15f } } },
        { "Echo in het Engels", kBoth, { { "loop_on", 1 }, { "loop_set", 1 }, { "loop_bars", 1 }, { "loop_gap", 2 }, { "loop_whisper", 0 }, { "spraak_on", 1 }, { "trig", 1 }, { "loop_echo", 1 }, { "lang", 0 }, { "pitch", -5 }, { "space", 0.3f } } },
        { "Bankkoor", kBoth, { { "loop_on", 1 }, { "loop_set", 1 }, { "loop_whisper", 0 }, { "spraak_on", 1 }, { "trig", 1 }, { "engine", 2 }, { "poly", 4 }, { "chord", 4 }, { "vocmod", 1 }, { "bands", 20 }, { "keys", 5 }, { "width", 0.6f }, { "revmix", 0.3f }, { "space", 0.3f } } },
        { "Woorden sturen fonemen", kBoth, { { "loop_on", 1 }, { "loop_set", 1 }, { "loop_bars", 1 }, { "loop_level", 0.5f }, { "spraak_on", 1 }, { "trig", 3 }, { "source", 6 }, { "loop_trig", 2 }, { "loop_note", 43 }, { "dlymix", 0.2f }, { "space", 0.2f } } },
        { "Spraak door de klinkerkaart", kBoth, { { "loop_on", 0 }, { "spraak_on", 1 }, { "trig", 0 }, { "rate", 4 }, { "source", 6 }, { "vowel_on", 1 }, { "vowel_src", 7 }, { "vowel_glide", 2 }, { "vowel_rate", 0.3f }, { "vowel_q", 1.6f }, { "vowel_level", 1.0f }, { "keys", 5 }, { "space", 0.4f } } },
        { "Wolk volgt de stem", kBoth, { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 1 }, { "cloud_size", 260 }, { "spraak_on", 1 }, { "trig", 0 }, { "rate", 5 }, { "source", 3 }, { "cv1src", 10 }, { "cv1dst", 15 }, { "cv1amt", 1.0f }, { "cv2src", 10 }, { "cv2dst", 16 }, { "cv2amt", 0.5f }, { "space", 0.5f } } },
        { "Alles door de fx", kBoth, { { "loop_on", 1 }, { "cloud_on", 1 }, { "cloud_density", 6 }, { "spraak_on", 1 }, { "trig", 1 }, { "loop_echo", 1 }, { "fx_src", 1 }, { "bits", 9 }, { "down", 3 }, { "dlymix", 0.3f }, { "dlyfb", 0.5f }, { "width", 0.7f }, { "revmix", 0.2f }, { "space", 0.1f } } },
    };
    return p;
}
const int kChords[10][4] = { { 0, 0, 0, 0 }, { 0, 12, 0, 12 }, { 0, 7, 12, 19 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 },
                             { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 6, 9 }, { 0, 1, 2, 3 } };
const float kJust[12] = { 0, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -9.78f, 1.96f, 13.69f, -15.64f, 17.6f, -11.73f };
const float kPyth[12] = { 0, -9.78f, 3.91f, -5.87f, 7.82f, -1.96f, -11.73f, 1.96f, -7.82f, 5.87f, -3.91f, 9.78f };
const float kMean[12] = { 0, -24.f, -6.8f, 10.3f, -13.7f, 3.4f, -20.5f, -3.4f, -27.4f, -10.3f, 6.8f, -17.1f };
inline float mtof(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }
static const int kScale[] = { 0, 3, 5, 7, 10, 12, 15, 17 };   // the notes the loop's words send
}

int SpreekbuisProcessor::getNumPrograms() { return (int) programs().size() + 1; }
const juce::String SpreekbuisProcessor::getProgramName(int i) { return i <= 0 || i > (int) programs().size() ? juce::String("Init") : juce::String(programs()[(size_t) i - 1].name); }
void SpreekbuisProcessor::resetToDefaults() {
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) rp->setValueNotifyingHost(rp->getDefaultValue());
}
// A host selecting the program that is already current changes nothing: VST2 hosts (REAPER) set the saved program
// again right after restoring a session's state, which must not put every parameter back to the program's values.
void SpreekbuisProcessor::setCurrentProgram(int i) {
    if (juce::jlimit(0, getNumPrograms() - 1, i) == currentProgram) return;
    loadProgram(i);
}
void SpreekbuisProcessor::loadProgram(int i) {
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, i);
    shownPreset = {};
    resetToDefaults();
    if (currentProgram == 0) return;
    auto set = [this](const std::vector<std::pair<const char*, float>>& v) {
        for (auto& [id, val] : v)
            if (auto* rp = apvts.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(val));
    };
    const auto& pr = programs()[(size_t) currentProgram - 1];
    if (pr.kind == kSpraak) set(spraakBase());
    set(pr.v);
}

// ---------------------------------------------------------------- lifecycle
SpreekbuisProcessor::SpreekbuisProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Input", juce::AudioChannelSet::stereo(), false)          // the vowel chart's input, the vocoder's carrier
                         .withInput("Modulator", juce::AudioChannelSet::stereo(), false)),    // the vocoder's modulator
      apvts(*this, nullptr, "params", sm::prm::layout()) {
    state.setProperty("orcaPort", 49162, nullptr);
    noteItem.fill(-1);
    for (auto& c : centsTable) c.store(0.f);
    for (auto& l : layerLevel) l.store(0.f);
    for (auto& l : lfoShown) l.store(0.f);
    for (auto& m : modShown) m.store(0.f);
    rnd.setSeed(0x5b7e151628aed2a6LL);                       // nothing random at start: the same instance makes the same sound
    plan = std::make_unique<Planner>(supportDir(), legacySpraakmakerDir());
    plan->startPlanner();
    startTimerHz(10);
}

SpreekbuisProcessor::~SpreekbuisProcessor() {
    stopTimer();
    *alive = false;
    orca.reset();
    loader.reset();
    flushPlays();
    plan.reset();
}

bool SpreekbuisProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 0; i < l.inputBuses.size(); ++i) {
        const auto in = l.inputBuses[i];
        if (!in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void SpreekbuisProcessor::prepareToPlay(double s, int block) {
    sr = s;
    whisper.prepare((float) s);
    vowel.prepare((float) s);
    gv.prepare((float) s);
    reverb.setSampleRate(s);
    const int n = std::max(block, 64);
    inputCopy.setSize(2, n);
    for (auto* v : { &wordsL, &wordsR, &cloudL, &cloudR, &whisL, &whisR, &spk, &fxL, &fxR, &bankMod }) v->assign((size_t) n, 0.0f);
    pendingOffs.reserve(64); spraakOffs.reserve(64);
    vowelNotes.reserve(16); gNotes.reserve(16);
    // the speech computer
    for (auto& v : voices) v.prepare(sr);
    vocoder.prepare((float) sr);
    carrierOsc.prepare((float) sr);
    speechBuf.setSize(1, n); extMod.setSize(1, n);
    voiceTmp.assign(64, 0.f);
    dly.l.assign((size_t) (sr * 4.0) + 1, 0.f); dly.r.assign((size_t) (sr * 4.0) + 1, 0.f); dly.w = 0;
    widthBuf.assign(4096, 0.f); widthW = 0;
    fxReverb.setSampleRate(sr);
    fxReverb.reset();
    fxTail = 0;
    updateTuning();
}

void SpreekbuisProcessor::setOrcaPort(int p) {
    state.setProperty("orcaPort", juce::jlimit(1024, 65535, p), nullptr);
    orca.reset();
}

// Orca message grammar (UDP ';' operator):
//   a word in the bank ("fiets", "goedemorgen")  -> speak it (the recorded voice)
//   g / gN                                      -> the Dutch g, N (base-36) = semitones up from C3
//   v<vowel> e.g. va vi vu vui veu vij vou      -> vowel pad on that vowel
//   =text (an _ is a space)                      -> the speech computer says it
//   !                                            -> the speech computer's trigger
//   anything else made of 0-9a-z                -> base-36 index into your learned items
void SpreekbuisProcessor::pushOrca(const juce::String& msg) {
    orcaPackets++;
    if (msg.startsWithChar('=')) { const auto t = msg.substring(1).replaceCharacter('_', ' ').trim(); if (t.isNotEmpty()) typeAndSpeak(t); return; }
    if (msg == "!") { manualTrigger(); return; }
    const juce::ScopedLock l(orcaWriteLock);
    int s1, n1, s2, n2;
    orcaFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) { orcaBuf[(size_t) s1] = msg; orcaFifo.finishedWrite(1); }
}

// ---------------------------------------------------------------- message thread housekeeping
void SpreekbuisProcessor::adoptBank(std::shared_ptr<Bank> b) {
    {
        const juce::SpinLock::ScopedLockType l(bankLock);
        if (bankOwner) graveyard.push_back({ bankOwner, 300 });
        bankOwner = b;
    }
    bankPtr.store(b.get());
    // what the bank says, for the speech computer (source LES, the echo)
    auto t = std::make_shared<BankTexts>();
    t->items.reserve(b->items.size());
    for (auto& it : b->items) t->items.push_back({ it.nl.toStdString(), it.en.toStdString() });
    for (int k = 0; k < 4; ++k) t->lesson[k] = b->lesson[k];
    plan->setBankTexts(t);
}

void SpreekbuisProcessor::startBankLoad() {
    BankRequest r;
    r.unit = (int) *apvts.getRawParameterValue("loop_unit");
    r.allItems = *apvts.getRawParameterValue("all_items") > 0.5f;
    r.female = *apvts.getRawParameterValue("voice") > 0.5f;
    loadedUnit = r.unit; loadedAll = r.allItems; loadedFemale = r.female;
    auto stillHere = alive;
    loader = std::make_unique<BankLoader>(r, [this, stillHere](std::shared_ptr<Bank> b) {
        juce::MessageManager::callAsync([this, stillHere, b] { if (*stillHere) adoptBank(b); });
    });
    loader->startThread();
}

void SpreekbuisProcessor::loadBankNow() { // synchronous (tests)
    BankRequest r;
    r.unit = (int) *apvts.getRawParameterValue("loop_unit");
    r.allItems = *apvts.getRawParameterValue("all_items") > 0.5f;
    r.female = *apvts.getRawParameterValue("voice") > 0.5f;
    loadedUnit = r.unit; loadedAll = r.allItems; loadedFemale = r.female;
    auto b = loadBank(r, {});
    graveyard.clear();
    adoptBank(b);
    graveyard.clear();
    reloadPending = false;
}

void SpreekbuisProcessor::timerCallback() {
    int unit = (int) *apvts.getRawParameterValue("loop_unit");
    bool all = *apvts.getRawParameterValue("all_items") > 0.5f;
    bool fem = *apvts.getRawParameterValue("voice") > 0.5f;
    if (unit != loadedUnit || all != loadedAll || fem != loadedFemale) reloadPending = true;
    if (reloadPending && (loader == nullptr || !loader->isThreadRunning())) { reloadPending = false; startBankLoad(); }
    for (auto& g : graveyard) --g.second;
    graveyard.erase(std::remove_if(graveyard.begin(), graveyard.end(), [](auto& g) { return g.second <= 0; }), graveyard.end());

    // orca socket
    bool want = *apvts.getRawParameterValue("orca_on") > 0.5f;
    if (want && orca == nullptr) {
        orca = std::make_unique<OrcaListener>(*this, orcaPort());
        if (!orca->open()) orca.reset();
    } else if (!want && orca) orca.reset();
    orcaListening = orca != nullptr;

    // plays
    int s1, n1, s2, n2;
    playFifo.prepareToRead(playFifo.getNumReady(), s1, n1, s2, n2);
    auto b = uiBank();
    auto take = [&](int start, int n) {
        for (int k = 0; k < n; ++k) {
            int it = playBuf[(size_t) (start + k)];
            if (b && it >= 0 && it < (int) b->items.size()) pendingPlays[b->items[(size_t) it].key]++;
        }
    };
    take(s1, n1); take(s2, n2);
    playFifo.finishedRead(n1 + n2);
    if (++flushCountdown >= 150) { flushCountdown = 0; flushPlays(); } // every 15 s
}

// The play log Tongval reads (…/Spreekbuis/plays.json): { "version": 1, "plays": { "<item key>": { "first": day, "n": count, "day": last day } } }
void SpreekbuisProcessor::flushPlays() {
    if (pendingPlays.empty()) return;
    auto f = playLogFile();
    f.getParentDirectory().createDirectory();
    auto root = juce::JSON::parse(f.loadFileAsString());
    if (!root.isObject()) root = juce::var(new juce::DynamicObject());
    auto plays = root.getProperty("plays", {});
    if (!plays.isObject()) { plays = juce::var(new juce::DynamicObject()); }
    auto* po = plays.getDynamicObject();
    const int day = todayNumber();
    for (auto& [key, n] : pendingPlays) {
        auto e = po->getProperty(key);
        if (!e.isObject()) { e = juce::var(new juce::DynamicObject()); e.getDynamicObject()->setProperty("first", day); }
        auto* eo = e.getDynamicObject();
        eo->setProperty("n", (int) eo->getProperty("n") + n);
        eo->setProperty("day", day);
        po->setProperty(key, e);
    }
    root.getDynamicObject()->setProperty("version", 1);
    root.getDynamicObject()->setProperty("plays", plays);
    auto tmp = f.getSiblingFile("plays.json.tmp");
    if (tmp.replaceWithText(juce::JSON::toString(root, true))) tmp.moveFileTo(f);
    pendingPlays.clear();
}

void SpreekbuisProcessor::countPlay(int item) {
    int s1, n1, s2, n2;
    playFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) { playBuf[(size_t) s1] = item; playFifo.finishedWrite(1); }
}

// ---------------------------------------------------------------- the bank's layers: helpers
int SpreekbuisProcessor::pickLessonItem(const Bank& b, int set) {
    const auto& L = b.lesson[juce::jlimit(0, 3, set)];
    const auto& list = L.empty() ? b.lesson[3] : L;
    if (list.empty()) return b.pool.empty() ? -1 : b.pool[(size_t) (rng.next() * b.pool.size()) % b.pool.size()];
    bool shuffle = *apvts.getRawParameterValue("loop_order") > 0.5f;
    if (shuffle) lessonPos = (int) (rng.next() * list.size());
    else lessonPos = (lessonPos + 1) % (int) list.size();
    return list[(size_t) lessonPos % list.size()];
}

void SpreekbuisProcessor::triggerWord(const Bank& b, int item, float semis, float gain) {
    if (item < 0 || item >= (int) b.items.size()) return;
    auto& s = shots[(size_t) nextShot];
    nextShot = (nextShot + 1) % (int) shots.size();
    s.start(&b.items[(size_t) item].nlS, item, 48000.0 / sr * std::pow(2.0, semis / 12.0), gain, rng.bi() * 0.3f);
    shownItem = item; shownWord = -1;
    countPlay(item);
    onsetCount.fetch_add(1);
    wordEnv = 1.0f;
}

void SpreekbuisProcessor::handleNoteOn(const Bank* b, int note, int vel) {
    const int km = (int) *apvts.getRawParameterValue("keys");
    const bool words = km == 0 || km == 3 || km == 4, vow = km == 1 || km == 3 || km == 4, g = km == 2 || km == 4;
    if (words && b && !b->pool.empty()) {
        int idx = b->pool[(size_t) (((note - 24) % (int) b->pool.size() + (int) b->pool.size()) % (int) b->pool.size())];
        noteItem[(size_t) note] = idx;
        heldItems.insert(idx);
        triggerWord(*b, idx, *apvts.getRawParameterValue("cloud_pitch"), 0.4f + 0.6f * vel / 127.0f);
    }
    if (vow) { vowelNotes.push_back(note); vowel.freq = lastVowelFreq = (float) juce::MidiMessage::getMidiNoteInHertz(note); vowel.gate = vel / 127.0f; }
    if (g) { gNotes.push_back(note); gv.freq = (float) juce::MidiMessage::getMidiNoteInHertz(note); gv.gate = 0.4f + 0.6f * vel / 127.0f; }
}

void SpreekbuisProcessor::handleNoteOff(int note) {
    if (noteItem[(size_t) note] >= 0) {
        int it = noteItem[(size_t) note]; noteItem[(size_t) note] = -1;
        bool still = false;
        for (int n = 0; n < 128; ++n) still |= noteItem[(size_t) n] == it;
        if (!still) heldItems.erase(it);
    }
    vowelNotes.erase(std::remove(vowelNotes.begin(), vowelNotes.end(), note), vowelNotes.end());
    if (vowelNotes.empty()) vowel.gate = padHeld.load() ? 0.8f : 0.0f; else vowel.freq = (float) juce::MidiMessage::getMidiNoteInHertz(vowelNotes.back());
    gNotes.erase(std::remove(gNotes.begin(), gNotes.end(), note), gNotes.end());
    if (gNotes.empty()) gv.gate = 0; else gv.freq = (float) juce::MidiMessage::getMidiNoteInHertz(gNotes.back());
}

void SpreekbuisProcessor::processOrcaEvents(const Bank* b) {
    int s1, n1, s2, n2;
    orcaFifo.prepareToRead(orcaFifo.getNumReady(), s1, n1, s2, n2);
    auto handle = [&](const juce::String& raw) {
        auto m = raw.toLowerCase().trim();
        if (m.isEmpty()) return;
        if (b) {
            auto it = b->byWord.find(m);
            if (it != b->byWord.end()) { triggerWord(*b, it->second, *apvts.getRawParameterValue("cloud_pitch"), 0.9f); return; }
        }
        auto b36 = [](juce::juce_wchar c) -> int { const int ch = (int) c; return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 10 : -1; };   // -1: not base-36 (juce_wchar is unsigned)
        if (m[0] == 'g' && (m.length() == 1 || (m.length() == 2 && b36(m[1]) >= 0))) {
            int semis = m.length() == 2 ? b36(m[1]) : 0;
            gv.freq = (float) juce::MidiMessage::getMidiNoteInHertz(48 + semis); gv.gate = 1; orcaGHold = (int) (0.12 * sr);
            return;
        }
        if (m[0] == 'v' && m.length() >= 2) {
            auto v = m.substring(1);
            static const std::map<juce::String, std::pair<float, float>> V {
                { "i", { 0, 0 } }, { "ie", { 0, 0 } }, { "y", { 0.3f, 0 } }, { "uu", { 0.3f, 0 } }, { "u", { 0.4f, 0.25f } }, { "oe", { 1, 0 } },
                { "e", { 0.15f, 0.66f } }, { "ee", { 0.05f, 0.38f } }, { "eu", { 0.35f, 0.38f } }, { "o", { 0.9f, 0.62f } }, { "oo", { 0.95f, 0.38f } },
                { "a", { 0.8f, 1 } }, { "aa", { 0.35f, 1 } }, { "ij", { 0.1f, 0.4f } }, { "ei", { 0.1f, 0.4f } }, { "ui", { 0.38f, 0.4f } },
                { "ou", { 0.85f, 0.45f } }, { "au", { 0.85f, 0.45f } }, { "uh", { 0.5f, 0.52f } } };
            auto f = V.find(v);
            if (f != V.end()) {
                orcaVX = f->second.first; orcaVY = f->second.second;
                vowel.gate = 1; orcaVowelHold = (int) (0.35 * sr);
                if (vowelNotes.empty()) vowel.freq = *apvts.getRawParameterValue("voice") > 0.5f ? 220.0f : 110.0f;
                return;
            }
        }
        if (b && !b->pool.empty()) {
            long idx = 0; bool ok = true;
            for (auto ch : m) { int d = b36(ch); if (d < 0) { ok = false; break; } idx = idx * 36 + d; }
            if (ok) triggerWord(*b, b->pool[(size_t) (idx % (long) b->pool.size())], *apvts.getRawParameterValue("cloud_pitch"), 0.9f);
        }
    };
    for (int k = 0; k < n1; ++k) handle(orcaBuf[(size_t) (s1 + k)]);
    for (int k = 0; k < n2; ++k) handle(orcaBuf[(size_t) (s2 + k)]);
    orcaFifo.finishedRead(n1 + n2);
}

// ---------------------------------------------------------------- the speech computer: tuning
float SpreekbuisProcessor::noteCents(int note) const { return centsTable[(size_t) juce::jlimit(0, 127, note)].load(); }

void SpreekbuisProcessor::updateTuning() {
    const int m = (int) apvts.getRawParameterValue("micro")->load();
    lastMicro = m;
    for (int n = 0; n < 128; ++n) {
        float c = 0;
        switch (m) {
            case 1: c = kJust[n % 12]; break;
            case 2: c = kPyth[n % 12]; break;
            case 3: c = kMean[n % 12]; break;
            case 4: c = ((60.f + (n - 60) * 0.5f) - (float) n) * 100.f; break;
            case 5:
                if (!sclCents.empty()) {
                    const int N = (int) sclCents.size(), steps = n - 60;
                    const int oct = (int) std::floor((double) steps / N), deg = steps - oct * N;
                    const double abs = oct * sclCents.back() + (deg == 0 ? 0.0 : sclCents[(size_t) deg - 1]);
                    c = (float) (abs - steps * 100.0);
                }
                break;
            default: break;
        }
        centsTable[(size_t) n].store(c);
    }
}

bool SpreekbuisProcessor::loadScl(const juce::File& f) {
    juce::StringArray lines; lines.addLines(f.loadFileAsString());
    std::vector<double> cents; juce::String title; int count = -1;
    for (auto l : lines) {
        l = l.trim();
        if (l.startsWith("!")) continue;
        if (title.isEmpty() && count < 0 && !l.containsOnly("0123456789 ")) { title = l; continue; }
        if (count < 0) { count = l.getIntValue(); continue; }
        auto tok = l.upToFirstOccurrenceOf(" ", false, false);
        if (tok.isEmpty()) continue;
        if (tok.contains("/")) cents.push_back(1200.0 * std::log2(tok.upToFirstOccurrenceOf("/", false, false).getDoubleValue()
                                                                  / tok.fromFirstOccurrenceOf("/", false, false).getDoubleValue()));
        else if (tok.contains(".")) cents.push_back(tok.getDoubleValue());
        else cents.push_back(1200.0 * std::log2(tok.getDoubleValue()));
    }
    if (cents.empty()) return false;
    sclCents = cents;
    sclTitle = title.isNotEmpty() ? title : f.getFileNameWithoutExtension();
    if (auto* p = apvts.getParameter("micro")) p->setValueNotifyingHost(p->convertTo0to1(5));
    updateTuning();
    return true;
}

// ---------------------------------------------------------------- the speech computer: display
SpreekbuisProcessor::Display SpreekbuisProcessor::display() const {
    const juce::SpinLock::ScopedLockType l(displayLock);
    return disp;
}

void SpreekbuisProcessor::publishDisplay(const Voice& v) {
    const Utterance& u = v.utterance();
    const juce::SpinLock::ScopedTryLockType l(displayLock);
    if (!l.isLocked()) return;
    const int w = juce::jlimit(0, juce::jmax(0, u.nWords - 1), v.currentWord());
    std::memcpy(disp.word, u.nWords > 0 ? u.words[w] : "", sizeof(disp.word));
    std::memcpy(disp.text, u.text, sizeof(disp.text));
    disp.phon = v.currentPhon();
}

// ---------------------------------------------------------------- the speech computer: triggers
void SpreekbuisProcessor::startNext(float velocity) {
    // typed text (Enter) that is still being turned into phonemes: wait for it rather than say the next queued word
    if (plan->urgentWaiting() || !(plan->popUrgent(scratch) || plan->pop(scratch))) { pendingTrig = (int) (sr * 3.0); pendingVel = velocity; return; }   // the planner may be refilling after a settings change: wait up to 3 s
    pendingTrig = 0;
    startNextFromScratch(velocity);
}

void SpreekbuisProcessor::startNextFromScratch(float velocity) {
    if (plan->urgentPending()) plan->popUrgent(scratch);
    const int engine = (int) apvts.getRawParameterValue("engine")->load();
    const int nv = engine == 0 ? (int) apvts.getRawParameterValue("poly")->load() : 1;
    const float startP = juce::jlimit(0.f, 1.f, apvts.getRawParameterValue("start")->load() + startOff);
    for (int k = 0; k < 4; ++k) {
        if (k < nv) voices[k].start(scratch, startP);
        else voices[k].stop();
    }
    lastVel = velocity;
    for (auto& r : randCv) r = rnd.nextFloat();
    trigCounter.fetch_add(1);
}

void SpreekbuisProcessor::handleTrigger(bool on, float velocity) {
    const int mode = (int) apvts.getRawParameterValue("trig")->load();
    const bool retrig = apvts.getRawParameterValue("retrig")->load() > 0.5f;
    const bool running = !voices[0].finished();
    if (on) {
        switch (mode) {
            case 0: break;                                                       // AUTO: notes only set pitch
            case 1: if (retrig || !running) startNext(velocity); break;
            case 2: gate = true; startNext(velocity); break;
            case 3: case 4:
                gate = true;
                if (running) { for (auto& v : voices) v.advance(); trigCounter.fetch_add(1); }
                else startNext(velocity);
                for (auto& v : voices) v.setGate(true);
                break;
        }
    } else if (nHeld == 0) {
        gate = false;
        if (mode == 2) for (auto& v : voices) v.stop();
        if (mode == 4) for (auto& v : voices) v.setGate(false);
    }
}

// ---------------------------------------------------------------- process
void SpreekbuisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int N = buffer.getNumSamples();
    const Bank* b = bankPtr.load();
    auto P = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    auto I = [&](const char* id) { return (int) apvts.getRawParameterValue(id)->load(); };
    if ((int) wordsL.size() < N) {
        for (auto* v : { &wordsL, &wordsR, &cloudL, &cloudR, &whisL, &whisR, &spk, &fxL, &fxR, &bankMod }) v->assign((size_t) N, 0.0f);
        speechBuf.setSize(1, N, false, false, true); extMod.setSize(1, N, false, false, true);
    }

    // ---------------- inputs (copied before the outputs are written): bus 0 = the vowel chart's input and the vocoder's
    // carrier, bus 1 = the vocoder's modulator
    if (inputCopy.getNumSamples() < N) inputCopy.setSize(2, N, false, false, true);
    inputCopy.clear();
    bool hasCar = false, hasMod = false;
    {
        auto* b0 = getBus(true, 0);
        if (b0 != nullptr && b0->isEnabled() && b0->getNumberOfChannels() > 0) {
            auto in = getBusBuffer(buffer, true, 0);
            for (int c = 0; c < std::min(2, in.getNumChannels()); ++c) inputCopy.copyFrom(c, 0, in, c, 0, N);
            if (in.getNumChannels() == 1) inputCopy.copyFrom(1, 0, in, 0, 0, N);
            const float lvl = inputCopy.getMagnitude(0, 0, N);
            inputLevel.store(std::max(lvl, inputLevel.load() * 0.97f));
            double ss = 0; const float* l = inputCopy.getReadPointer(0); const float* r = inputCopy.getReadPointer(1);
            for (int i = 0; i < N; ++i) { const float m = 0.5f * (l[i] + r[i]); ss += m * m; }
            hasCar = ss / N > 1.0e-8;
        } else inputLevel.store(inputLevel.load() * 0.97f);
        extMod.clear();
        auto* b1 = getBus(true, 1);
        if (b1 != nullptr && b1->isEnabled() && b1->getNumberOfChannels() > 0) {
            auto in = getBusBuffer(buffer, true, 1);
            for (int c = 0; c < in.getNumChannels(); ++c) extMod.addFrom(0, 0, in, c, 0, N, 1.f / (float) in.getNumChannels());
            double ss = 0; const float* d = extMod.getReadPointer(0);
            for (int i = 0; i < N; ++i) ss += d[i] * d[i];
            hasMod = ss / N > 1.0e-8;
        }
    }
    extCarrier.store(hasCar); extModulator.store(hasMod);
    buffer.clear();

    if (b != lastBank) { // new bank: drop every voice that points into the old one
        lastBank = b;
        loopVoice.active = loopWhisper.active = false;
        for (auto& s : shots) s.active = false;
        for (auto& g : grains) g.active = false;
        heldItems.clear(); noteItem.fill(-1); lessonPos = -1; loopItem = -1; lastCycle = -1;
    }
    processOrcaEvents(b);

    // ---------------- the one clock: the host's while it plays, otherwise our own
    const bool loopOn = P("loop_on") > 0.5f;
    double ppq = 0, bpm = P("loop_bpm"), barLen = 4;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (pos->getIsPlaying() && pos->getPpqPosition()) { hostPlaying = true; ppq = *pos->getPpqPosition(); }
            if (pos->getBpm()) bpm = *pos->getBpm();
            if (auto ts = pos->getTimeSignature()) barLen = ts->numerator * 4.0 / std::max(1, ts->denominator);
        }
    if (!hostPlaying) ppq = internalPpq;
    const double ppqPerSample = bpm / 60.0 / sr;

    // ---------------- the one modulation system: four LFOs, four slots, three macros -> every layer (block rate)
    float lfo[4];
    for (int k = 0; k < 4; ++k) {
        static const char* rid[4] = { "lfo1rate", "lfo2rate", "lfo3rate", "lfo4rate" };
        static const char* sid[4] = { "lfo1shape", "lfo2shape", "lfo3shape", "lfo4shape" };
        const float rate = P(rid[k]);
        const int shape = I(sid[k]);
        float& ph = lfoPhase[k];
        ph += rate * (float) N / (float) sr;
        if (ph >= 1.f) { ph -= std::floor(ph); lfoSH[k] = rnd.nextFloat() * 2.f - 1.f; }
        switch (shape) {
            case 0: lfo[k] = std::sin(6.2831853f * ph); break;
            case 1: lfo[k] = 1.f - 4.f * std::fabs(ph - 0.5f); break;
            case 2: lfo[k] = 2.f * ph - 1.f; break;
            case 3: lfo[k] = ph < 0.5f ? 1.f : -1.f; break;
            default: lfo[k] = lfoSH[k]; break;
        }
        lfoShown[(size_t) k].store(lfo[k]);
    }
    float off[prm::dCount] {};
    for (int k = 0; k < 4; ++k) {
        static const char* src[4] = { "cv1src", "cv2src", "cv3src", "cv4src" };
        static const char* dst[4] = { "cv1dst", "cv2dst", "cv3dst", "cv4dst" };
        static const char* amt[4] = { "cv1amt", "cv2amt", "cv3amt", "cv4amt" };
        static const char* knob[4] = { "cv1", "cv2", "cv3", "cv4" };
        const int s = I(src[k]), d = I(dst[k]);
        if (d <= 0 || d >= prm::dCount) continue;
        float v = 0;
        switch (s) {
            case prm::sKnob: v = P(knob[k]); break;
            case prm::sLfo1: case prm::sLfo2: case prm::sLfo3: case prm::sLfo4: v = lfo[s - 1]; break;
            case prm::sVel: v = lastVel; break;
            case prm::sModWheel: v = modWheel; break;
            case prm::sRand: v = randCv[k]; break;
            case prm::sWord: v = wordEnv; break;                   // the bank's word onsets
            case prm::sBank: v = bankEnv; break;                   // how loud the bank's voice is
            case prm::sSpraak: v = spraakEnv; break;               // how loud the speech computer is
            case prm::sVowelX: v = padX.load() * 2.f - 1.f; break; // where the vowel chart is
            case prm::sVowelY: v = padY.load() * 2.f - 1.f; break;
            default: break;
        }
        off[d] += v * P(amt[k]);
    }
    for (int k = 0; k < 3; ++k) {
        static const char* md[3] = { "m1dst", "m2dst", "m3dst" };
        static const char* mv[3] = { "m1", "m2", "m3" };
        const int d = I(md[k]);
        if (d > 0 && d < prm::dCount) off[d] += P(mv[k]);
    }
    for (int d = 0; d < prm::dCount; ++d) modShown[(size_t) d].store(off[d]);
    auto cl01 = [](float v) { return juce::jlimit(0.f, 1.f, v); };

    // ---------------- MIDI in: the on-screen keys join the host's notes; "Toetsen" says which layers they play
    keyState.processNextMidiBuffer(midi, 0, N, true);
    const int keysMode = I("keys");
    const bool keysSpraak = keysMode == prm::kEverything || keysMode == prm::kSpraak;
    struct Ev { int pos; int type; int note; float vel; };   // type 0 on, 1 off, 2 trigger, 3 the loop's echo, 4 a loop word (trigger, with its note)
    Ev evs[320]; int nev = 0;
    for (const auto m : midi) {
        auto msg = m.getMessage();
        if (msg.isNoteOn()) {
            handleNoteOn(b, msg.getNoteNumber(), msg.getVelocity());       // (block-start accuracy is fine for the bank's layers)
            if (keysSpraak && nev < 250) evs[nev++] = { m.samplePosition, 0, msg.getNoteNumber(), msg.getFloatVelocity() };
        } else if (msg.isNoteOff()) {
            handleNoteOff(msg.getNoteNumber());
            if (nev < 250) evs[nev++] = { m.samplePosition, 1, msg.getNoteNumber(), 0.f };
        } else if (msg.isController() && msg.getControllerNumber() == 1) modWheel = msg.getControllerValue() / 127.f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            for (int n = 0; n < 128; ++n) handleNoteOff(n);
            if (nev < 250) evs[nev++] = { m.samplePosition, 1, -1, 0.f };
        }
    }
    midi.clear();
    juce::MidiBuffer out;

    // ---------------- the bank's layers: parameters
    static const int callOpts[] = { 1, 2, 4 }, gapOpts[] = { 0, 1, 2, 4 };
    const int callBars = callOpts[juce::jlimit(0, 2, I("loop_bars"))];
    const int gapBars = gapOpts[juce::jlimit(0, 3, I("loop_gap"))];
    const double cycleLen = barLen * (callBars + gapBars);
    const float loopRate = (float) (48000.0 / sr * std::pow(2.0, juce::jlimit(-24.0f, 24.0f, P("loop_pitch") + off[prm::dLoopPitch] * 12.0f) / 12.0));
    const bool midiOutOn = P("loop_midi") > 0.5f;
    const int baseNote = I("loop_note");
    const int loopSet = I("loop_set");
    const bool spraakOn = P("spraak_on") > 0.5f;
    const bool echoOn = spraakOn && P("loop_echo") > 0.5f;
    const int loopTrig = spraakOn ? I("loop_trig") : 0;

    const bool cloudOn = P("cloud_on") > 0.5f;
    const float density = juce::jlimit(0.0f, 80.0f, P("cloud_density") + off[prm::dCloudDens] * 40.0f), sizeMs = P("cloud_size"), scatter = P("cloud_scatter");
    const float posP = cl01(P("cloud_pos") + off[prm::dCloudPos]);
    const float cPitch = juce::jlimit(-48.0f, 48.0f, P("cloud_pitch") + off[prm::dCloudPitch] * 24.0f), spread = P("cloud_spread"), revP = P("cloud_reverse"), cWhisper = P("cloud_whisper");
    const float lLoop = P("loop_level"), lCloud = P("cloud_level"), lWh = juce::jlimit(0.0f, 1.5f, P("whisper_level") + off[prm::dWhisper]), lVow = P("vowel_level"), lG = P("g_level");
    const float master = P("master");
    const bool whisperOn = P("loop_whisper") > 0.5f;
    nOnsets = 0; echoAt = -1;
    float pkLoop = 0, pkCloud = 0, pkWh = 0, pkVow = 0, pkG = 0, pkSpk = 0;

    // ---------------- stage 1: the lesson loop, the words played by keys and Orca, the cloud, the whispers
    for (int i = 0; i < N; ++i) {
        // ---- lesson loop: start an item at each cycle, whisper the translation in the answer bars
        if (loopOn && b && cycleLen > 0) {
            const double p = ppq + i * ppqPerSample;
            const long long cyc = (long long) std::floor(p / cycleLen);
            if (cyc != lastCycle) {
                lastCycle = cyc; whisperFiredThisCycle = false; echoFiredThisCycle = false;
                loopItem = pickLessonItem(*b, loopSet);
                if (loopItem >= 0) {
                    loopVoice.start(&b->items[(size_t) loopItem].nlS, loopItem, loopRate, 1.0f);
                    loopWord = -1; shownItem = loopItem; shownWord = -1;
                    countPlay(loopItem);
                    if (echoOn) plan->requestItem(loopItem);     // the speech computer gets the item ready for the answer bars
                }
            }
            const bool answer = gapBars > 0 && p - (double) cyc * cycleLen >= barLen * callBars;
            if (!whisperFiredThisCycle && answer && whisperOn) {
                whisperFiredThisCycle = true;
                if (loopItem >= 0 && loopItem < (int) b->items.size()) loopWhisper.start(&b->items[(size_t) loopItem].enS, loopItem, 24000.0 / sr, 1.0f);
            }
            if (!echoFiredThisCycle && answer && echoOn) { echoFiredThisCycle = true; if (echoAt < 0) echoAt = i; }
            // word onsets -> karaoke + MIDI out + the speech computer's triggers
            if (loopVoice.active && loopItem >= 0) {
                const auto& ons = b->items[(size_t) loopItem].onsets;
                int w = loopWord + 1;
                if (w < (int) ons.size() && loopVoice.pos >= ons[(size_t) w]) {
                    loopWord = w; shownWord = w;
                    const int note = juce::jlimit(0, 127, baseNote + kScale[w % 8]);
                    if (midiOutOn) {
                        out.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) (w == 0 ? 120 : 84)), i);
                        pendingOffs.push_back({ note, (int) (sr * 60.0 / bpm / 4) });
                    }
                    if (nOnsets < (int) onsets.size()) onsets[(size_t) nOnsets++] = { i, note };
                    onsetCount.fetch_add(1);
                    wordEnv = 1.0f;
                }
            }
        }
        for (auto& po : pendingOffs)
            if (--po.samples == 0) out.addEvent(juce::MidiMessage::noteOff(1, po.note), i);
        if (!pendingOffs.empty() && (i & 63) == 0)
            pendingOffs.erase(std::remove_if(pendingOffs.begin(), pendingOffs.end(), [](auto& p) { return p.samples <= 0; }), pendingOffs.end());

        float wIn = 0;
        float loopL = 0, loopR = 0;   // the spoken words (loop + keys / Orca), before deciding where they go
        if (b) {
            float lv = loopVoice.tick() * lLoop; loopL += lv; loopR += lv;
            wIn += loopWhisper.tick();
            for (auto& s : shots) {
                if (!s.active) continue;
                float v = s.tick() * lLoop;
                loopL += v * (1 - s.pan); loopR += v * (1 + s.pan);
            }
        }
        wordsL[(size_t) i] = loopL; wordsR[(size_t) i] = loopR;

        // ---- word cloud
        if (cloudOn && b && density > 0.01f) {
            grainCountdown -= 1;
            if (grainCountdown <= 0) {
                grainCountdown = sr / density * (0.5 + rng.next());
                int item = -1;
                if (!heldItems.empty()) { auto it = heldItems.begin(); std::advance(it, (int) (rng.next() * heldItems.size()) % heldItems.size()); item = *it; }
                else if (!b->pool.empty()) item = b->pool[(size_t) (rng.next() * b->pool.size()) % b->pool.size()];
                if (item >= 0 && item < (int) b->items.size()) {
                    auto& it = b->items[(size_t) item];
                    const bool en = rng.next() < cWhisper && !it.enS.empty();
                    const auto& d = en ? it.enS : it.nlS;
                    for (auto& g : grains) {
                        if (g.active) continue;
                        const double srcRate = en ? 24000.0 : 48000.0;
                        float centre = juce::jlimit(0.0f, 1.0f, posP + scatter * rng.bi() * 0.5f);
                        g.data = &d; g.len = std::max(16, (int) (sizeMs * 0.001 * sr));
                        double inc = srcRate / sr * std::pow(2.0, (cPitch + (scatter * 0.3f) * rng.bi()) / 12.0);
                        if (rng.next() < revP) inc = -inc;
                        g.inc = inc; g.pos = centre * (double) d.size(); g.age = 0;
                        float pan = spread * rng.bi();
                        g.panL = std::sqrt(0.5f * (1 - pan)); g.panR = std::sqrt(0.5f * (1 + pan));
                        g.gain = 0.6f; g.whisper = en; g.active = true;
                        if (timeSec - b->lastCounted[(size_t) item] > 20.0) { b->lastCounted[(size_t) item] = timeSec; countPlay(item); }
                        break;
                    }
                }
            }
        }
        float cl = 0, cr = 0;
        for (auto& g : grains) {
            if (!g.active) continue;
            float v = g.tick();
            if (g.whisper) wIn += v * 0.7f;
            else { cl += v * g.panL; cr += v * g.panR; }
        }
        cloudL[(size_t) i] = cl; cloudR[(size_t) i] = cr;

        // ---- whisper bus (translations)
        {
            float w = whisper.process(wIn) * lWh;
            whisL[(size_t) i] = w * 0.9f; whisR[(size_t) i] = w;
        }
        pkLoop = std::max(pkLoop, std::abs(loopL)); pkCloud = std::max(pkCloud, std::abs(cl) * lCloud); pkWh = std::max(pkWh, std::abs(whisR[(size_t) i]));
    }
    pendingOffs.erase(std::remove_if(pendingOffs.begin(), pendingOffs.end(), [](auto& p) { return p.samples <= 0; }), pendingOffs.end());

    // ---------------- routing: who is heard through what
    const bool vowelOn = P("vowel_on") > 0.5f;
    const int glide = I("vowel_glide"), src = I("vowel_src");
    const int engine = I("engine");
    const int vocMod = (spraakOn && engine != 0) ? I("vocmod") : 0;
    const bool loopToVoc = vocMod == 1 || vocMod == 3, cloudToVoc = vocMod == 2 || vocMod == 3;
    // Klinker can take the words, the cloud and the speech computer and shape them with the vowel chart (an insert: they're heard through it)
    const bool loopToVowel = vowelOn && !loopToVoc && (src == 4 || src == 6 || src == 8), cloudToVowel = vowelOn && !cloudToVoc && (src == 5 || src == 6 || src == 8);
    const bool spraakToVowel = vowelOn && (src == 7 || src == 8);

    // ---------------- stage 2: the speech computer (its events are sample-accurate)
    if (spraakOn) {
        for (int k = 0; k < nOnsets && nev < 300; ++k) if (loopTrig > 0) evs[nev++] = { onsets[(size_t) k].pos, 4, loopTrig == 2 ? onsets[(size_t) k].note : -1, 1.f };
        if (echoAt >= 0 && nev < 310) evs[nev++] = { echoAt, 3, -1, 1.f };
    }
    if (loopTrig != 2) loopNote = -1;
    {
        plan->lang = I("lang"); plan->source = I("source"); plan->listNum = I("list"); plan->order = I("order"); plan->lesSet = loopSet;
        plan->discardStale();
        // (an offline bounce makes its utterances on this thread: the same project always says the same thing)
        if (isNonRealtime() && !plan->isSynchronous()) plan->pumpOffline(); else plan->pump();
        if (I("micro") != lastMicro) updateTuning();
        const bool manual = manualTrig.exchange(false);
        if (manual) { if (nev < 315) evs[nev++] = { 0, 2, -1, 1.f }; if (!spraakOn) forcedSpeech = true; }
        const bool run = spraakOn || forcedSpeech;
        bool anyActive = false; for (auto& v : voices) anyActive |= v.active();
        if (!run && spraakWasOn) { for (auto& v : voices) v.stop(); nHeld = 0; gate = false; pendingTrig = 0; pendingEcho = 0; }
        spraakWasOn = run;
        if (run || anyActive) {
            // the vocoder's modulator when it listens to the bank's voice
            if (loopToVoc || cloudToVoc)
                for (int i = 0; i < N; ++i)
                    bankMod[(size_t) i] = (loopToVoc ? 0.5f * (wordsL[(size_t) i] + wordsR[(size_t) i]) : 0.0f) + (cloudToVoc ? 0.5f * (cloudL[(size_t) i] + cloudR[(size_t) i]) * lCloud : 0.0f);
            std::stable_sort(evs, evs + nev, [](const Ev& a, const Ev& e) { return a.pos < e.pos; });
            // ---- parameters with their modulation
            const bool female = P("voice") > 0.5f;
            const float pitchSemis = P("pitch") + off[prm::dPitch] * 24.f;
            const float formantScale = 0.7f * std::pow(2.0714f, cl01(P("formant") + off[prm::dFormant])) * (female ? 1.17f : 1.0f);
            const float intonation = 2.f * cl01(P("inton") + off[prm::dInton]);
            const float bright = cl01(P("bright") + off[prm::dBright]);
            const float speed = 0.4f * std::pow(6.25f, cl01(P("speed") + off[prm::dSpeed]));
            const float emotion = 2.f * cl01(P("emotion") + off[prm::dEmotion]) - 1.f;
            const float fine = juce::jlimit(0.f, 100.f, P("fine") + off[prm::dFine] * 50.f);
            const int mode = I("trig"), poly = I("poly"), chord = juce::jlimit(1, 10, I("chord")) - 1;
            const int rootParam = I("root") + (female ? 12 : 0);   // the woman's voice: an octave up when no note is held
            startOff = off[prm::dStart];                           // (the start point is read at trigger time)
            const int rateSel = I("rate");
            if (spraakOn && mode == 0 && rateSel > 0) {
                static const double q[8] = { 0, 0.25, 0.5, 1, 2, 0, 0, 0 };
                const double rq = rateSel >= 5 ? barLen * (rateSel == 5 ? 1 : rateSel == 6 ? 2 : 4) : q[rateSel];
                double k = std::ceil((ppq - 1e-9) / rq) * rq;
                int added = 0;
                while (k < ppq + N * ppqPerSample && nev < 319) {
                    evs[nev++] = { juce::jlimit(0, N - 1, (int) ((k - ppq) / ppqPerSample)), 2, -1, 1.f };
                    k += rq; ++added;
                }
                if (added) std::stable_sort(evs, evs + nev, [](const Ev& a, const Ev& e) { return a.pos < e.pos; });
            }
            // ---- pitches
            auto rootNow = [&]() { return nHeld > 0 ? held[nHeld - 1] : loopNote >= 0 ? loopNote : rootParam; };
            auto voiceNote = [&](int k, int count) -> float {
                float note;
                if (engine == 0 && nHeld > 1 && count > 1) note = (float) held[juce::jmax(0, nHeld - 1 - k)];
                else note = (float) rootNow() + (float) kChords[chord][k % 4] + (k >= 4 ? 12.f : 0.f);
                const float det = count > 1 ? fine * ((float) k - (count - 1) * 0.5f) / std::max(1.f, (count - 1) * 0.5f) : 0.f;
                return note + noteCents((int) note) / 100.f + det / 100.f;
            };
            const int nSpeech = engine == 0 ? poly : 1;
            const float glideT = P("glide");
            const float glideCoef = 1.f - std::exp(-32.f / (float) sr / (0.002f + glideT * 0.6f));
            const int hold = mode == 3 ? 1 : mode == 4 ? 2 : 0;
            const int trgOut = I("trgout");
            for (auto it = spraakOffs.begin(); it != spraakOffs.end();) {
                if (it->samples < N) { out.addEvent(juce::MidiMessage::noteOff(1, it->note), juce::jmax(0, it->samples)); it = spraakOffs.erase(it); }
                else { it->samples -= N; ++it; }
            }
            auto midiOut = [&](int note, int pos) {
                out.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), pos);
                const int offAt = pos + (int) (0.01 * sr);
                if (offAt < N) out.addEvent(juce::MidiMessage::noteOff(1, note), offAt);
                else if (spraakOffs.size() < 64) spraakOffs.push_back({ note, offAt - N });
            };

            float* speech = speechBuf.getWritePointer(0);
            std::fill(speech, speech + N, 0.f);
            int evi = 0;
            const int chunk = 32;
            if ((int) voiceTmp.size() < chunk) voiceTmp.resize(chunk);
            for (int pos = 0; pos < N; pos += chunk) {
                const int len = std::min(chunk, N - pos);
                while (evi < nev && evs[evi].pos < pos + len) {
                    const Ev& e = evs[evi++];
                    if (e.type == 0) {
                        if (!spraakOn) continue;
                        if (nHeld < 16) held[nHeld++] = e.note;
                        handleTrigger(true, e.vel);
                    } else if (e.type == 1) {
                        if (e.note < 0) nHeld = 0;
                        bool was = false;
                        for (int h = 0; h < nHeld; ++h) if (held[h] == e.note) { std::memmove(held + h, held + h + 1, sizeof(int) * (size_t) (nHeld - h - 1)); --nHeld; was = true; break; }
                        if (was || e.note < 0) handleTrigger(false, 0.f);
                    } else if (e.type == 3) {
                        // the answer bars: say what the loop just said (Dutch, or its English gloss)
                        if (plan->popItem(scratch, loopItem)) { pendingEcho = 0; pendingTrig = 0; startNextFromScratch(1.f); }
                        else pendingEcho = (int) (sr * 1.0);
                    } else {
                        if (e.type == 4 && e.note >= 0) loopNote = e.note;
                        const bool running = !voices[0].finished();
                        const bool retrig = P("retrig") > 0.5f;
                        if (mode == 3 || mode == 4) handleTrigger(true, 1.f);
                        else if (retrig || !running) startNext(1.f);
                    }
                }
                // a trigger that found the queue empty retries (the planner may be refilling)
                if (pendingTrig > 0) {
                    pendingTrig -= len;
                    if (plan->urgentPending()) { pendingTrig = 0; plan->popUrgent(scratch); startNextFromScratch(pendingVel); }
                    else if (!plan->urgentWaiting() && plan->pop(scratch)) { pendingTrig = 0; startNextFromScratch(pendingVel); }
                }
                if (pendingEcho > 0) {
                    pendingEcho -= len;
                    if (plan->popItem(scratch, loopItem)) { pendingEcho = 0; pendingTrig = 0; startNextFromScratch(1.f); }
                }
                // free-running auto: next utterance after a short gap
                if (spraakOn && mode == 0 && rateSel == 0) {
                    if (voices[0].finished()) { freeGap += len; if (freeGap > (int) (sr * 0.25 / speed)) { freeGap = 0; startNext(1.f); } }
                    else freeGap = 0;
                }
                // gate mode keeps talking while held
                if (spraakOn && mode == 2 && gate && voices[0].finished()) startNext(lastVel);

                for (int k = 0; k < nSpeech; ++k) {
                    const float target = voiceNote(k, nSpeech) + pitchSemis;
                    baseSemis[k] += (target - baseSemis[k]) * glideCoef;
                    VoiceParams vp;
                    vp.f0 = mtof(baseSemis[k]);
                    vp.formant = formantScale; vp.intonation = intonation; vp.brightness = bright;
                    vp.speed = speed; vp.emotion = emotion; vp.hold = hold;
                    vp.level = 1.f / std::sqrt((float) nSpeech);
                    if (!voices[k].active()) continue;
                    events[k].clear();
                    voices[k].render(voiceTmp.data(), len, vp, k == 0 ? &events[0] : nullptr);
                    for (int i = 0; i < len; ++i) speech[pos + i] += voiceTmp[(size_t) i];
                    if (k == 0 && trgOut != 3) {
                        if (trgOut == 0 || trgOut == 2) for (int w = 0; w < events[0].nWord; ++w) midiOut(60, pos + events[0].wordAt[w]);
                        if (trgOut == 1 || trgOut == 2) for (int w = 0; w < events[0].nPhon; ++w) midiOut(62, pos + events[0].phonAt[w]);
                    }
                    if (k == 0 && events[0].nWord > 0) trigCounter.fetch_add(1);
                }
                for (int k = nSpeech; k < 4; ++k) if (voices[k].active()) voices[k].stop();
            }

            // ---- engine: the voice itself, or a vocoder / chord singer listening to it, to the bank or to the side-chain
            if (engine == 0) std::copy(speech, speech + N, spk.begin());
            else {
                const bool extC = engine == 1 && hasCar, extM = engine == 1 && hasMod;
                const int nCar = std::max(1, poly);
                float carFreq[4];
                for (int k = 0; k < nCar; ++k) {
                    float note;
                    if (engine == 2 && nHeld > 0) note = (float) held[juce::jmax(0, nHeld - 1 - (k % nHeld))] + (k >= nHeld ? 12.f : 0.f);
                    else note = (float) rootNow() + (float) kChords[chord][k % 4];
                    const float det = nCar > 1 ? fine * ((float) k - (nCar - 1) * 0.5f) / std::max(1.f, (nCar - 1) * 0.5f) : 0.f;
                    carFreq[k] = mtof(note + noteCents((int) note) / 100.f + det / 100.f + (engine == 2 ? 0.f : pitchSemis));
                }
                vocoder.configure(I("bands"), formantScale, bright);
                const int wave = I("carrier");
                const float* cm = extMod.getReadPointer(0);
                const float* inL = inputCopy.getReadPointer(0); const float* inR = inputCopy.getReadPointer(1);
                const bool fromBank = (loopToVoc || cloudToVoc) && !extM;
                for (int i = 0; i < N; ++i) {
                    const float m = extM ? cm[i] : fromBank ? bankMod[(size_t) i] * 0.5f : speech[i];
                    const float c = extC ? 0.5f * (inL[i] + inR[i]) : carrierOsc.tick(carFreq, nCar, wave);
                    spk[(size_t) i] = vocoder.tick(m, c);
                }
            }
            meterLevel.store(voices[0].meter());
            publishDisplay(voices[0]);
            bool still = false; for (auto& v : voices) still |= v.active();
            speakingNow.store(still);
            if (forcedSpeech && !still && pendingTrig <= 0 && !plan->urgentWaiting() && !plan->urgentPending()) forcedSpeech = false;
        } else {
            std::fill(spk.begin(), spk.begin() + N, 0.0f);
            meterLevel.store(0.0f); speakingNow.store(false);
            for (auto it = spraakOffs.begin(); it != spraakOffs.end();) {
                if (it->samples < N) { out.addEvent(juce::MidiMessage::noteOff(1, it->note), juce::jmax(0, it->samples)); it = spraakOffs.erase(it); }
                else { it->samples -= N; ++it; }
            }
        }
    }

    // ---------------- stage 3: the vowel chart, the g, the effects, the mix
    const float vRate = P("vowel_rate"), vNoise = P("vowel_noise"), vQ = P("vowel_q"), vShift = std::pow(2.0f, P("vowel_shift") / 12.0f);
    const float vAtk = 1 - std::exp(-1.0f / (P("vowel_attack") * (float) sr)), vRel = 1 - std::exp(-1.0f / (P("vowel_release") * (float) sr));
    const float fSmooth = 1 - std::exp(-1.0f / (0.01f * (float) sr));
    // the woman's voice: vowel resonances about a sixth higher (F1 x1.17, F2 x1.18, F3 x1.15, after Peterson & Barney)
    const bool female = P("voice") > 0.5f;
    const float fF1 = female ? 1.17f : 1.0f, fF2 = female ? 1.18f : 1.0f, fF3 = female ? 1.15f : 1.0f;
    // the chart pressed with the mouse plays the vowel by itself (at the last note's pitch, or A2)
    const bool padHeldNow = padHeld.load();
    if (padHeldNow != padWasHeld) {
        padWasHeld = padHeldNow;
        if (padHeldNow) { if (vowelNotes.empty()) vowel.freq = lastVowelFreq > 0 ? lastVowelFreq : (female ? 220.0f : 110.0f); vowel.gate = std::max(vowel.gate, 0.8f); }
        else if (vowelNotes.empty() && orcaVowelHold == 0) vowel.gate = 0;
    }
    const bool gOn = P("g_on") > 0.5f;
    const float gSoft = cl01(P("g_soft") + off[prm::dGSoft]), gRasp = P("g_rasp"), gVoice = P("g_voice"), gHarm = P("g_harm");
    const float gRel = 1 - std::exp(-1.0f / (P("g_release") * (float) sr));
    const auto* pVX = apvts.getRawParameterValue("vowel_x"); const auto* pVY = apvts.getRawParameterValue("vowel_y");
    const float vxOff = off[prm::dVowelX], vyOff = off[prm::dVowelY];

    // the effects (the speech computer's rack): crush, width, ping-pong echo, reverb. They take the speech computer, or every layer
    const bool fxAll = I("fx_src") == 1;
    const float spkGain = P("level") * 1.2f;
    const int bits = juce::jlimit(2, 16, (int) std::round(P("bits") - off[prm::dCrush] * 12.f));
    const int down = juce::jlimit(1, 32, (int) std::round(P("down") + off[prm::dCrush] * 8.f));
    const float dlyMix = cl01(P("dlymix") + off[prm::dDelay]);
    const float revMix = cl01(P("revmix") + off[prm::dReverb]);
    const float width = cl01(P("width") + off[prm::dWidth]);
    const float q = std::pow(2.f, (float) (bits - 1));
    static const double dlyQ[6] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    const int dlySamples = juce::jlimit(1, (int) dly.l.size() - 2, (int) (dlyQ[juce::jlimit(0, 5, I("dlytime"))] * 60.0 / bpm * sr));
    const float fb = P("dlyfb");
    const int wsz = (int) widthBuf.size();
    bool spkPresent = speakingNow.load();
    if (!spkPresent) for (int i = 0; i < N; i += 16) if (std::abs(spk[(size_t) i]) > 1.0e-6f) { spkPresent = true; break; }
    if (spkPresent || fxAll) fxTail = (int) (sr * 12.0);
    const bool fxRun = fxTail > 0;
    if (fxRun) fxTail -= N;

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    for (int i = 0; i < N; ++i) {
        float l = 0, r = 0;
        const float loopL = wordsL[(size_t) i], loopR = wordsR[(size_t) i];
        const float cl = cloudL[(size_t) i], cr = cloudR[(size_t) i];
        const float s = spk[(size_t) i];
        float vowelFeed = 0;
        if (loopToVoc) {}                                   // heard through the vocoder
        else if (loopToVowel) vowelFeed += 0.5f * (loopL + loopR); else { l += loopL; r += loopR; }
        if (cloudToVoc) {}
        else if (cloudToVowel) vowelFeed += 0.5f * (cl + cr) * lCloud; else { l += cl * lCloud; r += cr * lCloud; }
        if (spraakToVowel) vowelFeed += s * spkGain * 1.5f;
        l += whisL[(size_t) i]; r += whisR[(size_t) i];

        // ---- vowel pad (switched on, or played from the chart or by Orca)
        if (vowelOn || padHeldNow || orcaVowelHold > 0 || vowel.env > 1e-4f) {
            float x = cl01(pVX->load() + vxOff), y = cl01(pVY->load() + vyOff);
            if (glide > 0) {
                glidePh += vRate / (float) sr; if (glidePh >= 1) glidePh -= 1;
                float t = 0.5f - 0.5f * std::cos(2 * kPi * glidePh);
                auto& gl = glides()[(size_t) glide - 1];
                x = gl.x0 + (gl.x1 - gl.x0) * t; y = gl.y0 + (gl.y1 - gl.y0) * t;
            }
            if (orcaVowelHold > 0) { x = orcaVX; y = orcaVY; if (--orcaVowelHold == 0 && vowelNotes.empty() && !padHeldNow) vowel.gate = 0; }
            if ((i & 31) == 0) { padX = x; padY = y; }
            float f1, f2, f3;
            formantsAt(x, y, f1, f2, f3);
            f1s += fSmooth * (f1 * fF1 * vShift - f1s); f2s += fSmooth * (f2 * fF2 * vShift - f2s); f3s += fSmooth * (f3 * fF3 * vShift - f3s);
            float srcSig = 0;
            const float inMono = 0.5f * (inputCopy.getSample(0, i) + inputCopy.getSample(1, i));
            const bool synthSrc = src == 0 || src == 1 || src == 3;
            // switched off (and not played from the chart or Orca): the held notes only get their release
            const float gateNow = (vowelOn || padHeldNow || orcaVowelHold > 0) ? vowel.gate : 0.0f;
            vowel.env += (gateNow > vowel.env ? vAtk : vRel) * (gateNow - vowel.env);
            if (synthSrc) srcSig += vowel.osc(src == 1 ? 1.0f : vNoise) * vowel.env;
            if (src == 2 || src == 3) srcSig += inMono * 2.0f;
            if (src >= 4) srcSig += vowelFeed * 2.0f;
            float v = vowel.filter(srcSig, f1s, f2s, f3s, vQ) * 0.5f * lVow;
            l += v; r += v;
            pkVow = std::max(pkVow, std::abs(v));
        }

        // ---- the Dutch g
        if (gOn || orcaGHold > 0) {
            if (orcaGHold > 0 && --orcaGHold == 0 && gNotes.empty()) gv.gate = 0;
            float g = gv.tick(gSoft, gRasp, gVoice, gHarm, gRel) * 0.5f * lG;
            l += g; r += g;
            pkG = std::max(pkG, std::abs(g));
        }

        // ---- the effects: crush and width here, echo and reverb on their way out
        if (fxRun) {
            float xs = spraakToVowel ? 0.0f : s, bl = 0, br = 0;
            if (fxAll) { bl = l; br = r; l = 0; r = 0; }
            if (down > 1) { if (crushCount++ % down == 0) { crushS = xs; crushL = bl; crushR = br; } xs = crushS; bl = crushL; br = crushR; }
            if (bits < 16) { xs = std::round(xs * q) / q; bl = std::round(bl * q) / q; br = std::round(br * q) / q; }
            xs *= spkGain;
            // width: two modulated taps -> side signal
            widthBuf[(size_t) widthW] = xs + 0.5f * (bl + br);
            widthPhase += 0.25f / (float) sr; if (widthPhase >= 1.f) widthPhase -= 1.f;
            const float d1 = (0.011f + 0.002f * std::sin(6.2831853f * widthPhase)) * (float) sr;
            const float d2 = (0.017f + 0.002f * std::cos(6.2831853f * widthPhase)) * (float) sr;
            auto tap = [&](float d) { const int di = (int) d; const float fr = d - (float) di;
                const float a = widthBuf[(size_t) ((widthW - di + wsz) % wsz)], bb = widthBuf[(size_t) ((widthW - di - 1 + wsz) % wsz)];
                return a + (bb - a) * fr; };
            const float side = 0.5f * (tap(d1) - tap(d2)) * width * 1.6f;
            widthW = (widthW + 1) % wsz;
            float fl = xs + bl + side, fr = xs + br - side;
            // ping-pong delay
            const int dsz = (int) dly.l.size();
            const int rd = (dly.w - dlySamples + dsz) % dsz;
            const float dl = dly.l[(size_t) rd], dr = dly.r[(size_t) rd];
            dly.l[(size_t) dly.w] = 0.5f * (fl + fr) + dr * fb;
            dly.r[(size_t) dly.w] = dl * fb;
            dly.w = (dly.w + 1) % dsz;
            fxL[(size_t) i] = fl + dl * dlyMix; fxR[(size_t) i] = fr + dr * dlyMix;
        }
        pkSpk = std::max(pkSpk, std::abs(s) * spkGain);
        L[i] = l; R[i] = r;
    }
    if (fxRun) {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.05f + 0.9f * P("revsize"); rp.damping = 0.45f; rp.wetLevel = revMix * 0.6f; rp.dryLevel = 1.f; rp.width = 1.f;
        fxReverb.setParameters(rp);
        fxReverb.processStereo(fxL.data(), fxR.data(), N);
        for (int i = 0; i < N; ++i) { L[i] = (L[i] + std::tanh(fxL[(size_t) i])) * master; R[i] = (R[i] + std::tanh(fxR[(size_t) i])) * master; }
    } else {
        for (int i = 0; i < N; ++i) { L[i] *= master; R[i] *= master; }
    }
    internalPpq = hostPlaying ? ppq + N * ppqPerSample : internalPpq + N * ppqPerSample;
    timeSec += N / sr;

    // ---------------- ruimte: the room everything stands in
    const float space = P("space");
    if (space > 0.001f) {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.4f * space; rp.damping = 0.4f; rp.wetLevel = 0.5f * space; rp.dryLevel = 1.0f - 0.3f * space; rp.width = 1.0f;
        reverb.setParameters(rp);
        reverb.processStereo(L, R, N);
    }
    float pkOut = 0;
    for (int c = 0; c < 2; ++c) {
        auto* d = buffer.getWritePointer(c);
        for (int i = 0; i < N; ++i) {
            // soft ceiling: untouched below 0.7, then bent smoothly towards 1.0
            const float x = std::isfinite(d[i]) ? d[i] : 0.0f, a = std::abs(x);
            d[i] = a <= 0.7f ? x : std::copysign(0.7f + 0.3f * std::tanh((a - 0.7f) / 0.3f), x);
            pkOut = std::max(pkOut, std::abs(d[i]));
        }
    }

    // ---------------- what moves the modulation next block, and what the editor draws
    const float blockSec = (float) N / (float) sr;
    if (nOnsets == 0) wordEnv *= std::exp(-blockSec / 0.25f);
    {
        const float target = juce::jlimit(0.0f, 1.0f, (pkLoop + pkCloud) * 2.0f);
        bankEnv += (target - bankEnv) * (target > bankEnv ? 0.6f : 1.0f - std::exp(-blockSec / 0.15f));
        const float st = juce::jlimit(0.0f, 1.0f, meterLevel.load() * 9.0f);
        spraakEnv += (st - spraakEnv) * (st > spraakEnv ? 0.6f : 1.0f - std::exp(-blockSec / 0.15f));
    }
    const float decay = std::exp(-blockSec / 0.3f);
    const float pk[6] = { pkLoop, pkCloud, pkWh, pkVow, pkG, pkSpk };
    for (int k = 0; k < 6; ++k) layerLevel[(size_t) k].store(std::max(pk[k], layerLevel[(size_t) k].load() * decay));
    outLevel.store(std::max(pkOut, outLevel.load() * decay));
    midi.swapWith(out);
}

// ---------------------------------------------------------------- state
// <Spreekbuis> <params> <PARAM id= value=/>… </params> <SpreekbuisState orcaPort= uiW= uiH= [version=2 keyb= scl= sclTitle= program= preset=]/> </Spreekbuis>
// Spreekbuis 1.0 wrote the same tree with fewer parameters and no version: what it doesn't know stays at its default.
void SpreekbuisProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree root("Spreekbuis");
    root.appendChild(apvts.copyState(), nullptr);
    auto s = state.createCopy();
    s.setProperty("version", 2, nullptr);
    s.setProperty("keyb", plan->getKeyboardText(), nullptr);
    juce::StringArray sc; for (double c : sclCents) sc.add(juce::String(c, 4));
    s.setProperty("scl", sc.joinIntoString(","), nullptr);
    s.setProperty("sclTitle", sclTitle, nullptr);
    s.setProperty("program", currentProgram, nullptr);
    s.setProperty("preset", shownPreset, nullptr);
    root.appendChild(s, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, dest);
}

bool SpreekbuisProcessor::applyStateXml(const juce::XmlElement& xml) {
    if (xml.hasTagName("PARAMS")) return importSpraakmaker(xml);            // a state of Spraakmaker 1.0
    if (xml.hasTagName("SpraakPreset")) { if (auto* p = xml.getChildByName("PARAMS")) { const bool ok = importSpraakmaker(*p); shownPreset = xml.getStringAttribute("presetName"); return ok; } return false; }
    if (!xml.hasTagName("Spreekbuis")) return false;
    auto root = juce::ValueTree::fromXml(xml);
    auto p = root.getChildWithName(apvts.state.getType());
    if (p.isValid()) {
        // a parameter the state doesn't mention (an older version wrote it) is at its default, not where it happened to be
        for (auto* prm : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                if (!p.getChildWithProperty("id", rp->getParameterID()).isValid()) {
                    juce::ValueTree c("PARAM");
                    c.setProperty("id", rp->getParameterID(), nullptr);
                    c.setProperty("value", rp->convertFrom0to1(rp->getDefaultValue()), nullptr);
                    p.appendChild(c, nullptr);
                }
        apvts.replaceState(p);
    }
    auto s = root.getChildWithName("SpreekbuisState");
    if (s.isValid()) {
        int old = orcaPort();
        for (auto name : { "orcaPort", "uiW", "uiH", "uiPage", "uiTab" }) if (s.hasProperty(name)) state.setProperty(name, s.getProperty(name), nullptr);
        if (orcaPort() != old) orca.reset();
        plan->setKeyboardText(s.getProperty("keyb", "").toString());
        sclCents.clear();
        for (auto& t : juce::StringArray::fromTokens(s.getProperty("scl", "").toString(), ",", "")) if (t.isNotEmpty()) sclCents.push_back(t.getDoubleValue());
        sclTitle = s.getProperty("sclTitle", "").toString();
        if (s.hasProperty("program")) currentProgram = juce::jlimit(0, getNumPrograms() - 1, (int) s.getProperty("program"));
        shownPreset = s.getProperty("preset", "").toString();
        if ((int) s.getProperty("version", 1) < 2) { state.removeProperty("uiW", nullptr); state.removeProperty("uiH", nullptr); }   // 1.0's window had other proportions
    }
    updateTuning();
    return true;
}

void SpreekbuisProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) applyStateXml(*xml);
}

// Spraakmaker 1.0's state: <PARAMS keyb= preset= scl= sclTitle=> <PARAM id= value=/>… </PARAMS>. Its parameters kept
// their ids here; the rest of the instrument is put where Spraakmaker was: only the speech computer, playing from the
// keys, at full level in a dry room.
bool SpreekbuisProcessor::importSpraakmaker(const juce::XmlElement& x) {
    if (!x.hasTagName("PARAMS")) return false;
    resetToDefaults();
    auto set = [this](const juce::String& id, float v) {
        if (auto* rp = apvts.getParameter(id)) { const auto& r = rp->getNormalisableRange(); rp->setValueNotifyingHost(rp->convertTo0to1(juce::jlimit(r.start, r.end, v))); return true; }
        return false;
    };
    for (auto& [id, v] : spraakBase()) set(id, v);
    int n = 0;
    for (auto* c : x.getChildWithTagNameIterator("PARAM"))
        if (set(c->getStringAttribute("id"), (float) c->getDoubleAttribute("value"))) ++n;
    plan->setKeyboardText(x.getStringAttribute("keyb"));
    sclCents.clear();
    for (auto& t : juce::StringArray::fromTokens(x.getStringAttribute("scl"), ",", "")) if (t.isNotEmpty()) sclCents.push_back(t.getDoubleValue());
    sclTitle = x.getStringAttribute("sclTitle");
    updateTuning();
    currentProgram = 0;
    shownPreset = "spraakmaker";
    return n > 0;
}

// Anything that holds one of our states: the XML itself, a host's preset file around it (.vstpreset, .fxp, .aupreset)…
bool SpreekbuisProcessor::loadAnyState(const void* data, size_t size) {
    if (data == nullptr || size < 8) return false;
    if (auto xml = getXmlFromBinary(data, (int) size)) return applyStateXml(*xml);
    const juce::String text = juce::String::fromUTF8((const char*) data, (int) std::min<size_t>(size, 4 * 1024 * 1024));
    if (text.trimStart().startsWithChar('<'))
        if (auto xml = juce::XmlDocument::parse(text)) {
            if (xml->hasTagName("Spreekbuis") || xml->hasTagName("PARAMS") || xml->hasTagName("SpraakPreset")) return applyStateXml(*xml);
            // an .aupreset: a property list with the state as base64 data
            if (xml->hasTagName("plist")) {
                std::function<const juce::XmlElement*(const juce::XmlElement&)> find = [&](const juce::XmlElement& e) -> const juce::XmlElement* {
                    for (auto* c : e.getChildIterator()) {
                        if (c->hasTagName("key") && c->getAllSubText().trim() == "jucePluginState") return c->getNextElement();
                        if (auto* f = find(*c)) return f;
                    }
                    return nullptr;
                };
                if (auto* d = find(*xml)) {
                    juce::MemoryOutputStream mo;
                    if (juce::Base64::convertFromBase64(mo, d->getAllSubText().removeCharacters(" \t\r\n")))
                        return loadAnyState(mo.getData(), mo.getDataSize());
                }
            }
        }
    // a binary file around the state: look for the block itself ("VC2!", its length, the XML)
    const auto* bytes = (const uint8_t*) data;
    for (size_t i = 1; i + 8 < size; ++i)
        if (bytes[i] == 'V' && bytes[i + 1] == 'C' && bytes[i + 2] == '2' && bytes[i + 3] == '!')
            if (auto xml = getXmlFromBinary(bytes + i, (int) (size - i)))
                if (applyStateXml(*xml)) return true;
    return false;
}

juce::File SpreekbuisProcessor::presetDir() const { return supportDir().getChildFile("presets"); }

bool SpreekbuisProcessor::savePresetFile(const juce::File& f, const juce::String& name) {
    juce::MemoryBlock mb; getStateInformation(mb);
    auto xml = getXmlFromBinary(mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return false;
    xml->setAttribute("presetName", name);
    if (auto* s = xml->getChildByName("SpreekbuisState")) { s->removeAttribute("uiW"); s->removeAttribute("uiH"); s->setAttribute("preset", name); }
    f.getParentDirectory().createDirectory();
    if (!xml->writeTo(f)) return false;
    shownPreset = name;
    return true;
}

bool SpreekbuisProcessor::loadPresetFile(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.existsAsFile() || !f.loadFileAsData(mb)) return false;
    const auto before = shownPreset;
    if (!loadAnyState(mb.getData(), mb.getSize())) return false;
    juce::ignoreUnused(before);
    if (shownPreset.isEmpty() || shownPreset == "spraakmaker") shownPreset = f.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessorEditor* SpreekbuisProcessor::createEditor() { return new SpreekbuisEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpreekbuisProcessor(); }
