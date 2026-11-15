#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sb;
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

// ---------------------------------------------------------------- parameters
APVTS::ParameterLayout SpreekbuisProcessor::layout() {
    using namespace juce;
    APVTS::ParameterLayout l;
    auto f = [&](const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f) {
        NormalisableRange<float> r(lo, hi); r.setSkewForCentre(skew == 1.0f ? (lo + hi) / 2 : skew);
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def));
    };
    auto b = [&](const char* id, const char* name, bool def) { l.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, def)); };
    auto c = [&](const char* id, const char* name, std::initializer_list<const char*> raw, int def) {
        StringArray opts; for (auto* o : raw) opts.add(String::fromUTF8(o)); // IPA labels are UTF-8
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, opts, def)); };
    auto i = [&](const char* id, const char* name, int lo, int hi, int def) { l.add(std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, lo, hi, def)); };

    f("master", "Master", 0, 1.5f, 0.8f); f("space", "Space", 0, 1, 0.25f);
    c("keys", "Keys play", { "Words", "Vowel", "G", "Words + Vowel", "Everything" }, 0);
    b("all_items", "All items", false);

    b("loop_on", "Loop on", true); f("loop_level", "Loop level", 0, 1.5f, 0.9f);
    i("loop_unit", "Lesson (0 = current)", 0, 36, 0);
    c("loop_set", "Loop plays", { "Words", "Sentences", "Dialog", "All" }, 0);
    c("loop_bars", "Call bars", { "1", "2", "4" }, 0);
    c("loop_gap", "Answer bars", { "0", "1", "2", "4" }, 1);
    c("loop_order", "Order", { "In order", "Shuffle" }, 0);
    b("loop_whisper", "Whisper translation", true);
    f("loop_pitch", "Loop pitch", -12, 12, 0);
    f("loop_bpm", "Free-run BPM", 50, 200, 100);
    b("loop_midi", "MIDI out", true); i("loop_note", "MIDI out note", 24, 96, 60);

    b("cloud_on", "Cloud on", false); f("cloud_level", "Cloud level", 0, 1.5f, 0.7f);
    f("cloud_density", "Density", 0, 80, 8, 10); f("cloud_size", "Grain ms", 15, 900, 180, 150);
    f("cloud_scatter", "Scatter", 0, 1, 0.6f); f("cloud_pos", "Position", 0, 1, 0.3f);
    f("cloud_pitch", "Cloud pitch", -24, 24, 0); f("cloud_spread", "Spread", 0, 1, 0.7f);
    f("cloud_reverse", "Reverse", 0, 1, 0.1f); f("cloud_whisper", "Translation grains", 0, 1, 0.15f);
    f("whisper_level", "Whisper level", 0, 1.5f, 0.6f);

    b("vowel_on", "Vowel on", false); f("vowel_level", "Vowel level", 0, 1.5f, 0.6f);
    f("vowel_x", "Front-back", 0, 1, 0.35f); f("vowel_y", "Close-open", 0, 1, 1.0f);
    c("vowel_glide", "Glide", { "Off", "\xc9\x9bi (ij/ei)", "\xc5\x93y (ui)", "\xca\x8cu (ou/au)" }, 0);
    f("vowel_rate", "Glide rate", 0.02f, 8, 0.3f, 0.5f);
    c("vowel_src", "Source", { "Saw", "Noise", "Input", "Saw + Input", "Loop", "Wolk", "Loop + Wolk" }, 0);   // (new ones at the end: old sessions keep theirs)
    f("vowel_noise", "Breath", 0, 1, 0.15f); f("vowel_q", "Resonance", 0.3f, 3, 1);
    f("vowel_shift", "Formant shift", -12, 12, 0);
    f("vowel_attack", "Attack", 0.001f, 2, 0.02f, 0.2f); f("vowel_release", "Release", 0.01f, 4, 0.4f, 0.5f);

    b("g_on", "G on", false); f("g_level", "G level", 0, 1.5f, 0.6f);
    f("g_soft", "Holland - Flanders", 0, 1, 0); f("g_rasp", "Rasp", 0, 1, 0.5f);
    f("g_voice", "Voicing", 0, 1, 0.5f); f("g_harm", "Resonance x", 1, 24, 8, 6);
    f("g_release", "G release", 0.01f, 2, 0.15f, 0.3f);
    b("orca_on", "Orca bridge", true);
    // the voice: the words (a second rendered bank) and the vowel chart's formants and pitch
    c("voice", "Voice", { "Man", "Vrouw" }, 0);
    return l;
}

// ---------------------------------------------------------------- presets
struct Preset { const char* name; std::vector<std::pair<const char*, float>> v; };
static const std::vector<Preset>& presets() {
    static const std::vector<Preset> p {
        { "Les in de loop", { { "loop_on", 1 }, { "cloud_on", 0 }, { "vowel_on", 0 }, { "g_on", 0 }, { "loop_set", 0 }, { "loop_gap", 1 }, { "space", 0.15f } } },
        { "Zinnen en echo", { { "loop_on", 1 }, { "loop_set", 1 }, { "loop_bars", 1 }, { "loop_gap", 2 }, { "cloud_on", 1 }, { "cloud_density", 3 }, { "cloud_size", 400 }, { "space", 0.5f } } },
        { "Fluisterwolk", { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 30 }, { "cloud_size", 120 }, { "cloud_scatter", 1 }, { "cloud_whisper", 0.6f }, { "whisper_level", 0.9f }, { "space", 0.6f } } },
        { "Droomstaat", { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 2 }, { "cloud_size", 800 }, { "cloud_pitch", -7 }, { "cloud_whisper", 0.4f }, { "vowel_on", 1 }, { "vowel_glide", 3 }, { "vowel_rate", 0.05f }, { "vowel_level", 0.3f }, { "space", 0.9f }, { "keys", 3 } } },
        { "Ui-drone", { { "loop_on", 0 }, { "vowel_on", 1 }, { "vowel_glide", 2 }, { "vowel_rate", 0.12f }, { "vowel_noise", 0.35f }, { "vowel_release", 3 }, { "space", 0.7f }, { "keys", 1 } } },
        { "Klinkerkoor", { { "loop_on", 0 }, { "vowel_on", 1 }, { "vowel_src", 3 }, { "vowel_q", 1.8f }, { "vowel_glide", 1 }, { "vowel_rate", 0.4f }, { "space", 0.5f }, { "keys", 1 } } },
        { "Harde g", { { "loop_on", 0 }, { "g_on", 1 }, { "g_soft", 0 }, { "g_rasp", 0.9f }, { "g_release", 0.08f }, { "keys", 2 }, { "space", 0.2f } } },
        { "Zachte g", { { "loop_on", 0 }, { "g_on", 1 }, { "g_soft", 1 }, { "g_voice", 0.8f }, { "g_rasp", 0.2f }, { "g_release", 0.5f }, { "keys", 2 }, { "space", 0.4f } } },
        { "Orca-woorden", { { "loop_on", 0 }, { "cloud_on", 1 }, { "cloud_density", 0 }, { "orca_on", 1 }, { "g_on", 1 }, { "vowel_on", 1 }, { "keys", 4 }, { "space", 0.3f } } },
    };
    return p;
}
int SpreekbuisProcessor::getNumPrograms() { return (int) presets().size() + 1; }
const juce::String SpreekbuisProcessor::getProgramName(int i) { return i == 0 ? "Init" : presets()[(size_t) i - 1].name; }
void SpreekbuisProcessor::setCurrentProgram(int i) {
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, i);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) rp->setValueNotifyingHost(rp->getDefaultValue());
    if (currentProgram == 0) return;
    for (auto& [id, v] : presets()[(size_t) currentProgram - 1].v)
        if (auto* rp = apvts.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

// ---------------------------------------------------------------- lifecycle
SpreekbuisProcessor::SpreekbuisProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Input", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "params", layout()) {
    state.setProperty("orcaPort", 49162, nullptr);
    noteItem.fill(-1);
    startTimerHz(10);
}

SpreekbuisProcessor::~SpreekbuisProcessor() {
    stopTimer();
    orca.reset();
    loader.reset();
    flushPlays();
}

bool SpreekbuisProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    auto in = l.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void SpreekbuisProcessor::prepareToPlay(double s, int block) {
    sr = s;
    whisper.prepare((float) s);
    vowel.prepare((float) s);
    gv.prepare((float) s);
    reverb.setSampleRate(s);
    inputCopy.setSize(2, std::max(block, 64));
    pendingOffs.reserve(64);
    vowelNotes.reserve(16); gNotes.reserve(16);
}

void SpreekbuisProcessor::setOrcaPort(int p) {
    state.setProperty("orcaPort", juce::jlimit(1024, 65535, p), nullptr);
    orca.reset();
}

void SpreekbuisProcessor::pushOrca(const juce::String& msg) {
    const juce::ScopedLock l(orcaWriteLock);
    int s1, n1, s2, n2;
    orcaFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) { orcaBuf[(size_t) s1] = msg; orcaFifo.finishedWrite(1); }
    orcaPackets++;
}

// ---------------------------------------------------------------- message thread housekeeping
void SpreekbuisProcessor::startBankLoad() {
    BankRequest r;
    r.unit = (int) *apvts.getRawParameterValue("loop_unit");
    r.allItems = *apvts.getRawParameterValue("all_items") > 0.5f;
    r.female = *apvts.getRawParameterValue("voice") > 0.5f;
    loadedUnit = r.unit; loadedAll = r.allItems; loadedFemale = r.female;
    juce::Component::SafePointer<juce::Component> dummy;
    loader = std::make_unique<BankLoader>(r, [this](std::shared_ptr<Bank> b) {
        juce::MessageManager::callAsync([this, b] {
            {
                const juce::SpinLock::ScopedLockType l(bankLock);
                if (bankOwner) graveyard.push_back({ bankOwner, 300 });
                bankOwner = b;
            }
            bankPtr.store(b.get());
        });
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
    const juce::SpinLock::ScopedLockType l(bankLock);
    bankOwner = b; bankPtr.store(b.get());
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

// ---------------------------------------------------------------- audio helpers
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
}

static int keysMode(APVTS& a) { return (int) *a.getRawParameterValue("keys"); }

void SpreekbuisProcessor::handleNoteOn(const Bank* b, int note, int vel) {
    const int km = keysMode(apvts);
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

// Orca message grammar (UDP ';' operator):
//   a word in the bank ("fiets", "goedemorgen")  -> speak it
//   g / gN                                      -> the Dutch g, N (base-36) = semitones up from C3
//   v<vowel> e.g. va vi vu vui veu vij vou      -> vowel pad on that vowel
//   anything else made of 0-9a-z                -> base-36 index into your learned items
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
        auto b36 = [](juce::juce_wchar ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 10 : -1; };
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

// ---------------------------------------------------------------- process
void SpreekbuisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int N = buffer.getNumSamples();
    const Bank* b = bankPtr.load();
    auto P = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    // keep sidechain input before clearing
    const int inCh = getTotalNumInputChannels();
    if (inputCopy.getNumSamples() < N) inputCopy.setSize(2, N, false, false, true);
    inputCopy.clear();
    for (int c = 0; c < std::min(2, inCh); ++c) inputCopy.copyFrom(c, 0, buffer, c, 0, N);
    if (inCh == 1) inputCopy.copyFrom(1, 0, buffer, 0, 0, N);
    {
        const float lvl = inCh > 0 ? inputCopy.getMagnitude(0, 0, N) : 0.0f;
        inputLevel.store(std::max(lvl, inputLevel.load() * 0.97f));
    }
    buffer.clear();

    if (b != lastBank) { // new bank: drop every voice that points into the old one
        lastBank = b;
        loopVoice.active = loopWhisper.active = false;
        for (auto& s : shots) s.active = false;
        for (auto& g : grains) g.active = false;
        heldItems.clear(); noteItem.fill(-1); lessonPos = -1; loopItem = -1; lastCycle = -1;
    }
    processOrcaEvents(b);

    juce::MidiBuffer out;
    // incoming MIDI (block-start accuracy is fine for these layers)
    for (const auto m : midi) {
        auto msg = m.getMessage();
        if (msg.isNoteOn()) handleNoteOn(b, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff()) handleNoteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { for (int n = 0; n < 128; ++n) handleNoteOff(n); }
    }
    midi.clear();

    // ---------------- lesson loop clock
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
    static const int callOpts[] = { 1, 2, 4 }, gapOpts[] = { 0, 1, 2, 4 };
    const int callBars = callOpts[juce::jlimit(0, 2, (int) P("loop_bars"))];
    const int gapBars = gapOpts[juce::jlimit(0, 3, (int) P("loop_gap"))];
    const double cycleLen = barLen * (callBars + gapBars);
    const float loopRate = (float) (48000.0 / sr * std::pow(2.0, P("loop_pitch") / 12.0));
    const bool midiOutOn = P("loop_midi") > 0.5f;
    const int baseNote = (int) P("loop_note");
    static const int scale[] = { 0, 3, 5, 7, 10, 12, 15, 17 };

    // ---------------- cloud params
    const bool cloudOn = P("cloud_on") > 0.5f;
    const float density = P("cloud_density"), sizeMs = P("cloud_size"), scatter = P("cloud_scatter"), posP = P("cloud_pos");
    const float cPitch = P("cloud_pitch"), spread = P("cloud_spread"), revP = P("cloud_reverse"), cWhisper = P("cloud_whisper");
    // ---------------- vowel params
    const bool vowelOn = P("vowel_on") > 0.5f;
    const int glide = (int) P("vowel_glide"), src = (int) P("vowel_src");
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
    // ---------------- g params
    const bool gOn = P("g_on") > 0.5f;
    const float gSoft = P("g_soft"), gRasp = P("g_rasp"), gVoice = P("g_voice"), gHarm = P("g_harm");
    const float gRel = 1 - std::exp(-1.0f / (P("g_release") * (float) sr));
    // levels
    const float lLoop = P("loop_level"), lCloud = P("cloud_level"), lWh = P("whisper_level"), lVow = P("vowel_level"), lG = P("g_level");
    const float master = P("master");

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);

    for (int i = 0; i < N; ++i) {
        // ---- lesson loop: start an item at each cycle, whisper the translation in the answer bars
        if (loopOn && b && cycleLen > 0) {
            const double p = ppq + i * ppqPerSample;
            const long long cyc = (long long) std::floor(p / cycleLen);
            if (cyc != lastCycle) {
                lastCycle = cyc; whisperFiredThisCycle = false;
                loopItem = pickLessonItem(*b, (int) P("loop_set"));
                if (loopItem >= 0) {
                    loopVoice.start(&b->items[(size_t) loopItem].nlS, loopItem, loopRate, 1.0f);
                    loopWord = -1; shownItem = loopItem; shownWord = -1;
                    countPlay(loopItem);
                }
            }
            if (!whisperFiredThisCycle && gapBars > 0 && P("loop_whisper") > 0.5f && p - (double) cyc * cycleLen >= barLen * callBars) {
                whisperFiredThisCycle = true;
                if (loopItem >= 0 && loopItem < (int) b->items.size()) loopWhisper.start(&b->items[(size_t) loopItem].enS, loopItem, 24000.0 / sr, 1.0f);
            }
            // word onsets -> karaoke + MIDI out
            if (loopVoice.active && loopItem >= 0) {
                const auto& ons = b->items[(size_t) loopItem].onsets;
                int w = loopWord + 1;
                if (w < (int) ons.size() && loopVoice.pos >= ons[(size_t) w]) {
                    loopWord = w; shownWord = w;
                    if (midiOutOn) {
                        int note = juce::jlimit(0, 127, baseNote + scale[w % 8]);
                        out.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) (w == 0 ? 120 : 84)), i);
                        pendingOffs.push_back({ note, (int) (sr * 60.0 / bpm / 4) });
                    }
                }
            }
        }
        for (auto& po : pendingOffs)
            if (--po.samples == 0) out.addEvent(juce::MidiMessage::noteOff(1, po.note), i);
        if (!pendingOffs.empty() && (i & 63) == 0)
            pendingOffs.erase(std::remove_if(pendingOffs.begin(), pendingOffs.end(), [](auto& p) { return p.samples <= 0; }), pendingOffs.end());

        float l = 0, r = 0, wIn = 0;
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
        // Klinker can take the words and / or the cloud and shape them with the vowel chart (an insert: they're heard through it)
        const bool loopToVowel = vowelOn && (src == 4 || src == 6), cloudToVowel = vowelOn && (src == 5 || src == 6);
        float vowelFeed = 0;
        if (loopToVowel) vowelFeed += 0.5f * (loopL + loopR); else { l += loopL; r += loopR; }

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
        if (cloudToVowel) vowelFeed += 0.5f * (cl + cr) * lCloud; else { l += cl * lCloud; r += cr * lCloud; }

        // ---- whisper bus (translations)
        {
            float w = whisper.process(wIn) * lWh;
            l += w * 0.9f; r += w;
        }

        // ---- vowel pad (switched on, or played from the chart or by Orca)
        if (vowelOn || padHeldNow || orcaVowelHold > 0 || vowel.env > 1e-4f) {
            float x = P("vowel_x"), y = P("vowel_y");
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
        }

        // ---- the Dutch g
        if (gOn || orcaGHold > 0) {
            if (orcaGHold > 0 && --orcaGHold == 0 && gNotes.empty()) gv.gate = 0;
            float g = gv.tick(gSoft, gRasp, gVoice, gHarm, gRel) * 0.5f * lG;
            l += g; r += g;
        }

        L[i] = l * master; R[i] = r * master;
    }
    pendingOffs.erase(std::remove_if(pendingOffs.begin(), pendingOffs.end(), [](auto& p) { return p.samples <= 0; }), pendingOffs.end());
    if (!hostPlaying && loopOn) internalPpq += N * ppqPerSample;
    timeSec += N / sr;

    // space
    const float space = P("space");
    if (space > 0.001f) {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.4f * space; rp.damping = 0.4f; rp.wetLevel = 0.5f * space; rp.dryLevel = 1.0f - 0.3f * space; rp.width = 1.0f;
        reverb.setParameters(rp);
        reverb.processStereo(L, R, N);
    }
    for (int c = 0; c < 2; ++c) {
        auto* d = buffer.getWritePointer(c);
        for (int i = 0; i < N; ++i) {
            // soft ceiling: untouched below 0.7, then bent smoothly towards 1.0 (it used to clip hard at 1.5)
            const float x = std::isfinite(d[i]) ? d[i] : 0.0f, a = std::abs(x);
            d[i] = a <= 0.7f ? x : std::copysign(0.7f + 0.3f * std::tanh((a - 0.7f) / 0.3f), x);
        }
    }
    midi.swapWith(out);
}

// ---------------------------------------------------------------- state
void SpreekbuisProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree root("Spreekbuis");
    root.appendChild(apvts.copyState(), nullptr);
    root.appendChild(state.createCopy(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, dest);
}
void SpreekbuisProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto root = juce::ValueTree::fromXml(*xml);
        auto p = root.getChildWithName(apvts.state.getType());
        if (p.isValid()) apvts.replaceState(p);
        auto s = root.getChildWithName("SpreekbuisState");
        if (s.isValid()) { int old = orcaPort(); state.copyPropertiesFrom(s, nullptr); if (orcaPort() != old) orca.reset(); }
    }
}

juce::AudioProcessorEditor* SpreekbuisProcessor::createEditor() { return new SpreekbuisEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpreekbuisProcessor(); }
