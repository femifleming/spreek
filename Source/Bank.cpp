#include "Bank.h"
#include <set>

namespace sb {

juce::File supportDir() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("Spreekbuis");
}
juce::File voiceDir() { return supportDir().getChildFile("voice"); }
juce::File playLogFile() { return supportDir().getChildFile("plays.json"); }
juce::File tongvalProgressFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("Tongval").getChildFile("progress.json");
}
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
        if (s.startsWith(a)) return s.substring((int) strlen(a));
    return s;
}

std::shared_ptr<Bank> loadBank(const BankRequest& req, const std::function<bool()>& shouldExit) {
    auto bank = std::make_shared<Bank>();
    bank->allItems = req.allItems;
    bank->unit = req.unit > 0 ? req.unit : currentTongvalUnit();

    auto manifest = readJson(voiceDir().getChildFile("manifest.json"));
    auto* items = manifest.getProperty("items", {}).getArray();
    if (items == nullptr) {
        bank->status = "No voice bank yet - run tools/render.sh in ~/spreekbuis";
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
    auto vd = voiceDir();

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
                 + " - lesson " + juce::String(bank->unit) + " - voice " + bank->voiceName;
    return bank;
}

} // namespace sb
