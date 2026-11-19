// Spreekbuis — utterance planning for the speech computer: speech sources (dictionary, alphabet, keyboard, Markov,
// lettrism, lists, and the word bank's lesson), prosody (durations, accents, declination), and a lock-free queue
// feeding the audio thread.
#pragma once
#include <juce_core/juce_core.h>
#include "G2P.h"
#include <atomic>
#include <array>
#include <map>
#include <mutex>
#include <random>
#include <memory>
#include <vector>

namespace sm {

constexpr int kMaxSegs = 256;
constexpr int kMaxWords = 40;
constexpr int kWordChars = 32;

enum : uint8_t { kSegWordStart = 2, kSegNoAsp = 1, kSegFinal = 8 };

struct Seg {
    int16_t ph = 0;
    uint8_t stress = 0;
    uint8_t flags = 0;
    uint8_t word = 0;
    float dur = 0;    // ms at speed 1
    float f0a = 0;    // semitone offset at start (at intonation 1)
    float f0b = 0;    // semitone offset at end
};

struct Utterance {
    int n = 0;
    Seg seg[kMaxSegs];
    int nWords = 0;
    char words[kMaxWords][kWordChars] {};
    char text[160] {};
    uint32_t gen = 0;
    uint32_t key = 0;   // the settings it was made for (Planner::settingsKey)
    float totalMs = 0;
};

enum class Source { Dict = 0, Alpha, Keyb, Markov, Lettr, List, Les };

// what the word bank says, for the speech computer: every item (Dutch, English gloss) and the lesson's lists
struct BankTexts {
    std::vector<std::pair<std::string, std::string>> items;   // nl, en (same index as the bank's items)
    std::vector<int> lesson[4];                               // words, sentences, dialog, all
};

void buildUtterance (const std::vector<WordPh>& words, Utterance& u, const std::string& text);

class Planner : private juce::Thread {
public:
    // dataDir holds lists/ (99 editable lists); legacyDir: an older folder whose lists are copied on first use
    explicit Planner (juce::File dataDir, juce::File legacyDir = {});
    ~Planner() override;

    // settings (written by the processor every block; planner reacts)
    std::atomic<int> lang { 0 }, source { 0 }, listNum { 1 }, order { 0 };
    std::atomic<int> lesSet { 0 };            // LES source: which lesson list (0 words, 1 sentences, 2 dialog, 3 all)

    // audio thread
    bool pop (Utterance& out);                // next utterance of the current generation
    void discardStale();
    uint32_t generation() const { return gen.load(); }
    // language, source, list, order (and the typed text, for KEYB) as one number: an utterance made for other
    // settings is never spoken, even before the planner thread has noticed the change
    uint32_t settingsKey() const
    {
        const uint32_t s = (uint32_t) source.load();
        return (uint32_t) lang.load() | (s << 2) | ((uint32_t) listNum.load() << 6) | ((uint32_t) order.load() << 13)
             | (s == 2 ? (kbVersion.load() & 0xffffu) << 14
                : s == 6 ? (((bankVersion.load() << 2) | (uint32_t) (lesSet.load() & 3)) & 0xffffu) << 14 : 0u);
    }

    // message thread
    void setKeyboardText (const juce::String& t);
    juce::String getKeyboardText() const;
    void speakNow (const juce::String& t);     // urgent utterance, played on the next trigger
    bool urgentPending() const { return urgentState.load() == 2; }
    bool urgentWaiting() const { return urgentRequested.load() && urgentState.load() != 2; }   // typed text still being turned into phonemes
    bool popUrgent (Utterance& out);

    // the word bank (message thread sets it when a bank has loaded; the planner thread reads it)
    void setBankTexts (std::shared_ptr<const BankTexts> t);
    // audio thread, lock-free: have one bank item made into an utterance of its own (the lesson loop's echo), in the
    // current language: the Dutch item, or its English gloss. popItem gives it once it is ready.
    void requestItem (int item) { itemWanted.store (item); }
    bool popItem (Utterance& out, int item);

    // lists (1..99)
    juce::StringArray getList (int n) const;
    void setList (int n, const juce::StringArray& entries);
    juce::File listsDir() const { return dir.getChildFile ("lists"); }

    // synchronous (tests)
    void makeNext (Utterance& u);
    // tests: is an utterance for the current settings waiting in the queue? (call from the thread that pops)
    bool ready() const { int s1, n1, s2, n2; fifo.prepareToRead (1, s1, n1, s2, n2); return n1 > 0 && slots[(size_t) s1].key == settingsKey(); }
    void startPlanner() { startThread (juce::Thread::Priority::normal); }
    // offline / tests: no planner thread; the processor pumps the planner once per block on its own thread, so a
    // render depends only on the input, never on wall-clock timing
    void setSynchronous (bool on);
    void pump() { if (synchronous.load()) { std::lock_guard<std::mutex> l (stepMutex); step (true); } }
    // a host rendering offline: the audio thread makes the utterances itself (it may wait there), so a bounce never
    // depends on how fast the planner thread happened to be
    void pumpOffline() { std::lock_guard<std::mutex> l (stepMutex); step (true); }
    bool isSynchronous() const { return synchronous.load(); }

private:
    void run() override;
    void step (bool inlineCall);               // one planner iteration (the thread loop body; inlineCall: made by the audio thread, never waits)
    std::mutex stepMutex;
    std::atomic<bool> synchronous { false };
    std::string markovPhrase (Lang l);
    std::string lettrism (Lang l, std::vector<WordPh>& out);
    void ensureMarkov (Lang l);
    void ensureLists();

    juce::File dir;
    static constexpr int kQ = 8;
    std::array<Utterance, kQ> slots;
    juce::AbstractFifo fifo { kQ };
    std::atomic<uint32_t> gen { 1 };
    int lastLang = -1, lastSource = -1, lastList = -1, lastOrder = -1, lastLesSet = -1;
    uint32_t lastBankVersion = 0;
    std::atomic<uint32_t> bankVersion { 0 };
    Utterance itemUtt;
    std::atomic<int> itemWanted { -1 }, itemMade { -1 };
    int itemLang = -1; uint32_t itemBankVersion = 0;
    std::atomic<int> itemState { 0 };       // 0 empty, 1 writing, 2 ready, 3 reading
    mutable std::mutex bankMutex;
    std::shared_ptr<const BankTexts> bankTexts;
    std::shared_ptr<const BankTexts> getBankTexts() const { std::lock_guard<std::mutex> l (bankMutex); return bankTexts; }
    juce::File legacy;
    juce::String lastKeyb;

    Utterance urgent;
    std::atomic<int> urgentState { 0 };     // 0 empty, 1 writing, 2 ready, 3 reading
    std::atomic<bool> urgentRequested { false };
    std::atomic<uint32_t> kbVersion { 0 };
    juce::String urgentText;
    std::mutex urgentMutex;

    mutable std::mutex textMutex;
    juce::String keyboardText;

    mutable std::mutex listMutex;
    std::map<int, juce::StringArray> lists;
    int listPos = 0, alphaPos = 0;

    std::map<std::string, std::vector<std::string>> markov[2];
    std::vector<std::string> markovStarts[2];
    bool markovBuilt[2] { false, false };
    std::mt19937 rng { 1234567u };
};

} // namespace sm
