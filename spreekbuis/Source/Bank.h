#pragma once
// Spreekbuis — the word bank: rendered Tongval items + what you have learned.
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace sb {

juce::File supportDir();          // ~/Library/Application Support/Spreekbuis (Windows: %APPDATA%\Spreekbuis, Linux: ~/.config/Spreekbuis)
juce::File voiceDir(bool female = false);  // …/Spreekbuis/voice, or voice-female (tools/render.sh --female)
juce::File builtInVoiceDir(bool female);   // …/Spreekbuis/voice-builtin[-female]: the Klatt formant voice, made on first use
juce::File tongvalProgressFile(); // ~/Library/Application Support/Tongval/progress.json
juce::File playLogFile();         // …/Spreekbuis/plays.json
int todayNumber();                // local day number, same formula as Tongval's today()

struct Item {
    juce::String key, kind, nl, en;
    int unit = 0;
    std::vector<int16_t> nlS;     // 48 kHz mono
    std::vector<int16_t> enS;     // 24 kHz mono (whisper layer)
    std::vector<int> onsets;      // word onsets, 48 kHz frames
    juce::StringArray words;      // nl split on spaces (karaoke)
    bool known = false;
};

struct Bank {
    std::vector<Item> items;
    std::vector<int> pool;              // items offered to cloud / keys / orca (known, or all)
    std::vector<int> lesson[4];         // lesson unit items: words, sentences, dialog, all
    std::map<juce::String, int> byWord; // lowercase nl (article stripped too) -> item
    juce::String voiceName, status;
    int unit = 1, knownCount = 0;
    bool allItems = false, manifestFound = false;
    bool female = false;               // the woman's voice is what was loaded
    bool builtIn = false;              // no rendered bank: the built-in formant voice speaks the items
    mutable std::vector<double> lastCounted; // audio thread: when each item was last counted as heard
};

struct BankRequest { int unit = 0; bool allItems = false; bool female = false; };

// Builds a bank on the calling thread (call from a background thread).
std::shared_ptr<Bank> loadBank(const BankRequest&, const std::function<bool()>& shouldExit);
int currentTongvalUnit(); // first unit not finished, from Tongval's progress

} // namespace sb
