#include "Bank.h"
#include "Speak.h"
#include <SbData.h>
#include <set>
#include <cstring>
#include <mutex>

namespace sb {

// per-user data: ~/Library/Application Support/<name> on macOS, %APPDATA%/<name> on Windows, ~/.config/<name> on Linux
static juce::File userDataDir(const char* name) {
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #endif
    return base.getChildFile(name);
}
juce::File supportDir() {
    // SPREEKBUIS_DATA_DIR overrides the data folder (tests, CI, a bank on another disk)
    auto env = juce::SystemStats::getEnvironmentVariable("SPREEKBUIS_DATA_DIR", {});
    return env.isNotEmpty() ? juce::File(env) : userDataDir("Spreekbuis");
}
juce::File voiceDir(bool female) { return supportDir().getChildFile(female ? "voice-female" : "voice"); }
juce::File builtInVoiceDir(bool female) { return supportDir().getChildFile(female ? "voice-builtin-female" : "voice-builtin"); }
static juce::File gPlayLogOverride;
void setPlayLogOverride(const juce::File& f) { gPlayLogOverride = f; }
juce::File playLogFile() { return gPlayLogOverride != juce::File() ? gPlayLogOverride : supportDir().getChildFile("plays.json"); }
juce::File legacySpraakmakerDir() { return userDataDir("Spraakmaker"); }
juce::File tongvalProgressFile() { return userDataDir("Tongval").getChildFile("progress.json"); }
int todayNumber() {
    auto now = juce::Time::getCurrentTime();
    auto ms = (double) now.toMilliseconds() + now.getUTCOffsetSeconds() * 1000.0;
    return (int) std::floor(ms / 86400000.0);
}

static juce::var readJson(const juce::File& f) {
    if (!f.existsAsFile()) return {};
    return juce::JSON::parse(f.loadFileAsString());
}

int currentTongvalUnit() {
    auto p = readJson(tongvalProgressFile());
    auto* units = p.getProperty("units", {}).getDynamicObject();
    for (int n = 1; n <= 36; ++n) {
        auto id = juce::String("u") + juce::String(n).paddedLeft('0', 2);
        if (units == nullptr) return 1;
        auto u = units->getProperty(id);
        bool done = (bool) u.getProperty("words", false) && (bool) u.getProperty("zinnen", false) && (bool) u.getProperty("gesprek", false);
        if (!done) return n;
    }
    return 36;
}

static bool readWav(juce::AudioFormatManager& fm, const juce::File& f, std::vector<int16_t>& out, int decimate) {
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    juce::AudioBuffer<float> b(1, (int) r->lengthInSamples);
    r->read(&b, 0, (int) r->lengthInSamples, 0, true, false);
    const float* s = b.getReadPointer(0);
    const int n = b.getNumSamples() / decimate;
    out.resize((size_t) n);
    for (int i = 0; i < n; ++i) {
        float v = 0;
        for (int k = 0; k < decimate; ++k) v += s[i * decimate + k];
        v /= (float) decimate;
        out[(size_t) i] = (int16_t) juce::jlimit(-32767, 32767, (int) std::lround(v * 32767.0f));
    }
    return true;
}

static juce::String stripArticle(juce::String s) {
    s = s.toLowerCase().trim();
    for (auto a : { "de ", "het ", "een " })
        if (s.startsWith(a)) return s.substring((int) std::strlen(a));
    return s;
}

// ---------------------------------------------------------------- the built-in voice
// No rendered voice bank (tools/render.sh not run, or not on this machine): the course items are spoken by the
// Klatt formant voice of the speech computer, once, into voice-builtin[-female]/ in the same layout a rendered
// bank has (nl/<id>.wav 48 kHz, en/<id>.wav, manifest.json with word onsets). The instrument is never silent.
static const int kBuiltInVersion = 1;

static juce::String fileId(const juce::String& key) { // FNV-1a 64, same as tools/render.swift
    uint64_t h = 0xcbf29ce484222325ULL;
    for (auto c : key.toRawUTF8() == nullptr ? std::string() : std::string(key.toRawUTF8())) { h ^= (uint8_t) c; h *= 0x100000001b3ULL; }
    return juce::String::toHexString((juce::int64) h).paddedLeft('0', 16);
}

static bool writeWav(const juce::File& f, const std::vector<float>& x, double sr) {
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto os = f.createOutputStream();
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.release(), sr, 1, 16, {}, 0));
    if (w == nullptr) return false;
    juce::AudioBuffer<float> b(1, (int) x.size());
    b.copyFrom(0, 0, x.data(), (int) x.size());
    return w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

static std::mutex gBuiltInLock;   // one renderer at a time (two plug-in instances starting together)

static bool ensureBuiltInBank(bool female, const std::function<bool()>& shouldExit) {
    const std::lock_guard<std::mutex> lock(gBuiltInLock);
    auto dir = builtInVoiceDir(female);
    auto manifestFile = dir.getChildFile("manifest.json");
    auto old = readJson(manifestFile);
    if ((int) old.getProperty("builtInVersion", 0) == kBuiltInVersion && old.getProperty("items", {}).getArray() != nullptr) return true;

    auto items = juce::JSON::parse(juce::String::fromUTF8(SbData::items_json, SbData::items_jsonSize));
    auto* list = items.getArray();
    if (list == nullptr) return false;
    dir.getChildFile("nl").createDirectory(); dir.getChildFile("en").createDirectory();
    const float f0 = female ? 200.0f : 110.0f, formant = female ? 1.17f : 1.0f;
    juce::Array<juce::var> out;
    for (auto& v : *list) {
        if (shouldExit && shouldExit()) return false;
        if (v.getProperty("kind", "").toString() == "t") continue;   // story lines: only with a rendered bank
        auto key = v.getProperty("key", "").toString(), nl = v.getProperty("nl", "").toString(), en = v.getProperty("en", "").toString();
        if (key.isEmpty() || nl.isEmpty()) continue;
        auto id = fileId(key);
        auto nlF = dir.getChildFile("nl").getChildFile(id + ".wav"), enF = dir.getChildFile("en").getChildFile(id + ".wav");
        auto s = sm::speak(nl.toStdString(), sm::Lang::NL, 48000.0, f0, formant, 1.0f);
        if (s.samples.empty() || !writeWav(nlF, s.samples, 48000.0)) continue;
        auto e = sm::speak(en.toStdString(), sm::Lang::EN, 48000.0, f0, formant, 1.0f);
        if (!e.samples.empty()) writeWav(enF, e.samples, 48000.0);
        auto* rec = new juce::DynamicObject();
        for (auto& p : v.getDynamicObject()->getProperties()) rec->setProperty(p.name, p.value);
        rec->setProperty("file", "nl/" + id + ".wav"); rec->setProperty("en_file", "en/" + id + ".wav");
        juce::Array<juce::var> ons; for (int o : s.onsets) ons.add(o);
        rec->setProperty("onsets", ons); rec->setProperty("frames", (int) s.samples.size());
        out.add(juce::var(rec));
    }
    auto* m = new juce::DynamicObject();
    m->setProperty("version", 1); m->setProperty("builtInVersion", kBuiltInVersion); m->setProperty("rate", 48000);
    m->setProperty("voice", female ? "spreek.builtin.klatt.female" : "spreek.builtin.klatt.male");
    m->setProperty("voiceName", female ? "Klatt (ingebouwd, vrouw)" : "Klatt (ingebouwd)");
    m->setProperty("enVoice", "Klatt"); m->setProperty("items", out);
    auto tmp = dir.getChildFile("manifest.json.tmp");
    return tmp.replaceWithText(juce::JSON::toString(juce::var(m))) && tmp.moveFileTo(manifestFile);
}

std::shared_ptr<Bank> loadBank(const BankRequest& req, const std::function<bool()>& shouldExit) {
    auto bank = std::make_shared<Bank>();
    bank->allItems = req.allItems;
    bank->unit = req.unit > 0 ? req.unit : currentTongvalUnit();

    // the woman's voice if it was asked for and has been rendered; otherwise the man's; with no rendered bank at
    // all, the built-in formant voice (man or woman as asked)
    juce::File vd;
    if (voiceDir(req.female).getChildFile("manifest.json").existsAsFile()) { bank->female = req.female; vd = voiceDir(req.female); }
    else if (voiceDir(false).getChildFile("manifest.json").existsAsFile()) { bank->female = false; vd = voiceDir(false); }
    else {
        bank->status = "Ingebouwde stem wordt gemaakt…";
        if (!ensureBuiltInBank(req.female, shouldExit)) { if (shouldExit && shouldExit()) return nullptr; bank->status = "No voice: the built-in voice could not be written to " + builtInVoiceDir(req.female).getFullPathName(); return bank; }
        bank->female = req.female; bank->builtIn = true; vd = builtInVoiceDir(req.female);
    }
    auto manifest = readJson(vd.getChildFile("manifest.json"));
    auto* items = manifest.getProperty("items", {}).getArray();
    if (items == nullptr) {
        bank->status = "No voice bank - run tools/render.sh";
        return bank;
    }
    bank->manifestFound = true;
    bank->voiceName = manifest.getProperty("voiceName", "").toString();

    // what has been learned in Tongval
    std::set<juce::String> known;
    std::set<int> dialogUnits;
    bool l0 = false;
    auto prog = readJson(tongvalProgressFile());
    if (auto* cards = prog.getProperty("cards", {}).getDynamicObject())
        for (auto& p : cards->getProperties()) known.insert(p.name.toString());
    if (auto* units = prog.getProperty("units", {}).getDynamicObject())
        for (auto& p : units->getProperties())
            if ((bool) p.value.getProperty("gesprek", false)) dialogUnits.insert(p.name.toString().substring(1).getIntValue());
    if (auto* l = prog.getProperty("l0", {}).getDynamicObject()) l0 = l->getProperties().size() > 0;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    for (auto& v : *items) {
        if (shouldExit && shouldExit()) return nullptr;
        Item it;
        it.key = v.getProperty("key", "").toString();
        it.kind = v.getProperty("kind", "").toString();
        it.nl = v.getProperty("nl", "").toString();
        it.en = v.getProperty("en", "").toString();
        it.unit = (int) v.getProperty("unit", 0);
        if (it.kind == "w" || it.kind == "s") it.known = known.count(it.key) > 0;
        else if (it.kind == "d") it.known = dialogUnits.count(it.unit) > 0;
        else if (it.kind == "w0") it.known = l0;
        const bool inLesson = it.unit == bank->unit && (it.kind == "w" || it.kind == "s" || it.kind == "d");
        if (!(req.allItems || it.known || inLesson)) continue;
        if (!readWav(fm, vd.getChildFile(v.getProperty("file", "").toString()), it.nlS, 1)) continue;
        readWav(fm, vd.getChildFile(v.getProperty("en_file", "").toString()), it.enS, 2);
        if (auto* on = v.getProperty("onsets", {}).getArray())
            for (auto& o : *on) it.onsets.push_back((int) o);
        it.words = juce::StringArray::fromTokens(it.nl, " ", "");
        bank->items.push_back(std::move(it));
    }
    for (int i = 0; i < (int) bank->items.size(); ++i) {
        auto& it = bank->items[(size_t) i];
        if (it.known) bank->knownCount++;
        if (req.allItems || it.known) bank->pool.push_back(i);
        if (it.unit == bank->unit) {
            int k = it.kind == "w" ? 0 : it.kind == "s" ? 1 : it.kind == "d" ? 2 : -1;
            if (k >= 0) { bank->lesson[k].push_back(i); bank->lesson[3].push_back(i); }
        }
        if (it.kind == "w" || it.kind == "w0") {
            auto w = it.nl.toLowerCase().trim();
            if (!bank->byWord.count(w)) bank->byWord[w] = i;
            auto s = stripArticle(w);
            if (!bank->byWord.count(s)) bank->byWord[s] = i;
        }
    }
    bank->lastCounted.assign(bank->items.size(), -1e9);
    // an empty pool would make the instrument silent: fall back to the lesson
    if (bank->pool.empty()) bank->pool = bank->lesson[3];
    bank->status = juce::String(bank->knownCount) + " learned items" + (req.allItems ? " (playing all " + juce::String(bank->items.size()) + ")" : "")
                 + " - lesson " + juce::String(bank->unit) + " - voice " + bank->voiceName
                 + (bank->builtIn ? " (tools/render.sh for a natural voice)" : "")
                 + (req.female && !bank->female ? " (vrouwenstem nog niet gerenderd: tools/render.sh --female)" : "");
    return bank;
}

} // namespace sb
