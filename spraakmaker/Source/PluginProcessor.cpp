#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sm;

namespace {
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> f {
        { "HALLO NL", { { "lang", 1 }, { "source", 5 }, { "list", 1 }, { "rate", 5 } } },
        { "CLASSIC EN", { { "lang", 0 }, { "source", 0 }, { "rate", 4 }, { "bright", 0.65f } } },
        { "MARKOV NL", { { "lang", 1 }, { "source", 3 }, { "rate", 6 }, { "revmix", 0.25f } } },
        { "MARKOV ALICE", { { "lang", 0 }, { "source", 3 }, { "rate", 6 }, { "revmix", 0.2f } } },
        { "ROBOTKOOR", { { "lang", 1 }, { "engine", 2 }, { "poly", 4 }, { "chord", 8 }, { "source", 5 }, { "list", 4 }, { "rate", 6 }, { "revmix", 0.3f } } },
        { "LETTRISME", { { "lang", 1 }, { "source", 4 }, { "rate", 3 }, { "speed", 0.7f }, { "emotion", 0.7f }, { "dlymix", 0.25f } } },
        { "PH.GATE STOT", { { "lang", 1 }, { "trig", 4 }, { "source", 5 }, { "list", 3 } } },
        { "ALFABET NL", { { "lang", 1 }, { "source", 1 }, { "rate", 4 } } },
        { "ALPHABET EN", { { "lang", 0 }, { "source", 1 }, { "rate", 4 } } },
        { "FLUISTER", { { "lang", 1 }, { "emotion", 0.05f }, { "bright", 0.25f }, { "revmix", 0.45f }, { "revsize", 0.8f } } },
        { "KIND", { { "lang", 1 }, { "formant", 0.82f }, { "pitch", 12 }, { "emotion", 0.75f }, { "source", 5 }, { "list", 1 } } },
        { "DIEPE STEM", { { "lang", 1 }, { "formant", 0.22f }, { "pitch", -10 }, { "speed", 0.38f }, { "revmix", 0.3f } } },
        { "GABBER SPRAAK", { { "lang", 1 }, { "speed", 0.85f }, { "bits", 6 }, { "down", 4 }, { "emotion", 1.0f }, { "rate", 2 }, { "source", 4 } } },
        { "VOCODER EXT", { { "engine", 1 }, { "bands", 20 }, { "trig", 1 } } },
        { "LUNAR STORM", { { "lang", 0 }, { "source", 5 }, { "list", 5 }, { "revmix", 0.5f }, { "dlymix", 0.3f },
                           { "cv1src", 1 }, { "cv1dst", 2 }, { "cv1amt", 0.3f }, { "rate", 6 } } },
        { "TYPMACHINE", { { "lang", 1 }, { "source", 2 }, { "trig", 1 } } },
        { "MICRO 24", { { "lang", 1 }, { "engine", 2 }, { "poly", 4 }, { "chord", 10 }, { "micro", 4 }, { "source", 3 }, { "rate", 6 } } },
        { "TELWOORDEN", { { "lang", 1 }, { "source", 5 }, { "list", 2 }, { "rate", 3 } } },
        { "SPREKENDE LFO", { { "lang", 1 }, { "source", 0 }, { "cv1src", 2 }, { "cv1dst", 1 }, { "cv1amt", 0.25f },
                             { "cv2src", 1 }, { "cv2dst", 6 }, { "cv2amt", 0.6f }, { "rate", 4 } } },
        { "ENGLISH KEYS", { { "lang", 0 }, { "source", 2 }, { "trig", 1 } } },
    };
    return f;
}
const int kChords[10][4] = { { 0, 0, 0, 0 }, { 0, 12, 0, 12 }, { 0, 7, 12, 19 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 },
                             { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 6, 9 }, { 0, 1, 2, 3 } };
const float kJust[12] = { 0, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -9.78f, 1.96f, 13.69f, -15.64f, 17.6f, -11.73f };
const float kPyth[12] = { 0, -9.78f, 3.91f, -5.87f, 7.82f, -1.96f, -11.73f, 1.96f, -7.82f, 5.87f, -3.91f, 9.78f };
const float kMean[12] = { 0, -24.f, -6.8f, 10.3f, -13.7f, 3.4f, -20.5f, -3.4f, -27.4f, -10.3f, 6.8f, -17.1f };
inline float mtof (float m) { return 440.f * std::pow (2.f, (m - 69.f) / 12.f); }
}

SpraakProcessor::SpraakProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Carrier", juce::AudioChannelSet::stereo(), false)
                          .withInput ("Modulator", juce::AudioChannelSet::stereo(), false))
{
    for (auto& c : centsTable) c.store (0.f);
    plan = std::make_unique<Planner> (dataDir());
    plan->startPlanner();
    loadPreset (0);
}

SpraakProcessor::~SpraakProcessor() { plan.reset(); }

juce::File SpraakProcessor::dataDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support").getChildFile ("Spraakmaker");
}

bool SpraakProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    for (int i = 0; i < l.inputBuses.size(); ++i)
    {
        const auto in = l.inputBuses[i];
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void SpraakProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    vocoder.prepare ((float) sr);
    carrierOsc.prepare ((float) sr);
    const int n = std::max (samplesPerBlock, 32);
    speechBuf.setSize (1, n); extCar.setSize (1, n); extMod.setSize (1, n);
    voiceTmp.assign (64, 0.f);
    dly.l.assign ((size_t) (sr * 4.0) + 1, 0.f); dly.r.assign ((size_t) (sr * 4.0) + 1, 0.f); dly.w = 0;
    widthBuf.assign (4096, 0.f); widthW = 0;
    reverb.setSampleRate (sr);
    reverb.reset();
    updateTuning();
}

// ------------------------------------------------------------------ tuning
float SpraakProcessor::noteCents (int note) const { return centsTable[(size_t) juce::jlimit (0, 127, note)].load(); }

void SpraakProcessor::updateTuning()
{
    const int m = (int) apvts.getRawParameterValue ("micro")->load();
    lastMicro = m;
    for (int n = 0; n < 128; ++n)
    {
        float c = 0;
        switch (m)
        {
            case 1: c = kJust[n % 12]; break;
            case 2: c = kPyth[n % 12]; break;
            case 3: c = kMean[n % 12]; break;
            case 4: c = ((60.f + (n - 60) * 0.5f) - (float) n) * 100.f; break;
            case 5:
                if (! sclCents.empty())
                {
                    const int N = (int) sclCents.size(), steps = n - 60;
                    const int oct = (int) std::floor ((double) steps / N), deg = steps - oct * N;
                    const double abs = oct * sclCents.back() + (deg == 0 ? 0.0 : sclCents[(size_t) deg - 1]);
                    c = (float) (abs - steps * 100.0);
                }
                break;
            default: break;
        }
        centsTable[(size_t) n].store (c);
    }
}

bool SpraakProcessor::loadScl (const juce::File& f)
{
    juce::StringArray lines; lines.addLines (f.loadFileAsString());
    std::vector<double> cents; juce::String title; int count = -1;
    for (auto l : lines)
    {
        l = l.trim();
        if (l.startsWith ("!")) continue;
        if (title.isEmpty() && count < 0 && ! l.containsOnly ("0123456789 ")) { title = l; continue; }
        if (count < 0) { count = l.getIntValue(); continue; }
        auto tok = l.upToFirstOccurrenceOf (" ", false, false);
        if (tok.isEmpty()) continue;
        if (tok.contains ("/")) cents.push_back (1200.0 * std::log2 (tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue()
                                                                    / tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue()));
        else if (tok.contains (".")) cents.push_back (tok.getDoubleValue());
        else cents.push_back (1200.0 * std::log2 (tok.getDoubleValue()));
    }
    if (cents.empty()) return false;
    sclCents = cents;
    sclTitle = title.isNotEmpty() ? title : f.getFileNameWithoutExtension();
    if (auto* p = apvts.getParameter ("micro")) p->setValueNotifyingHost (p->convertTo0to1 (5));
    updateTuning();
    return true;
}

// ------------------------------------------------------------------ presets
juce::String SpraakProcessor::presetName (int i) const
{
    auto f = dataDir().getChildFile ("presets").getChildFile (juce::String::formatted ("p%02d.xml", i + 1));
    if (f.existsAsFile())
        if (auto x = juce::XmlDocument::parse (f)) return x->getStringAttribute ("presetName", "USER");
    if (i < (int) factory().size()) return factory()[(size_t) i].name;
    return "INIT";
}

void SpraakProcessor::loadPreset (int i)
{
    i = juce::jlimit (0, 99, i);
    currentPreset = i;
    auto f = dataDir().getChildFile ("presets").getChildFile (juce::String::formatted ("p%02d.xml", i + 1));
    if (f.existsAsFile())
        if (auto x = juce::XmlDocument::parse (f))
            if (auto* st = x->getChildByName (apvts.state.getType()))
            {
                apvts.replaceState (juce::ValueTree::fromXml (*st));
                return;
            }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) rp->setValueNotifyingHost (rp->getDefaultValue());
    if (i < (int) factory().size())
        for (auto& [id, v] : factory()[(size_t) i].v)
            if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void SpraakProcessor::savePreset (int i, const juce::String& name)
{
    auto d = dataDir().getChildFile ("presets"); d.createDirectory();
    juce::XmlElement x ("SpraakPreset");
    x.setAttribute ("presetName", name);
    if (auto st = apvts.copyState().createXml()) x.addChildElement (st.release());
    x.writeTo (d.getChildFile (juce::String::formatted ("p%02d.xml", i + 1)));
    currentPreset = i;
}

// ------------------------------------------------------------------ state
void SpraakProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    auto xml = st.createXml();
    xml->setAttribute ("keyb", plan->getKeyboardText());
    xml->setAttribute ("preset", currentPreset);
    juce::StringArray sc; for (double c : sclCents) sc.add (juce::String (c, 4));
    xml->setAttribute ("scl", sc.joinIntoString (","));
    xml->setAttribute ("sclTitle", sclTitle);
    copyXmlToBinary (*xml, dest);
}

void SpraakProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            plan->setKeyboardText (xml->getStringAttribute ("keyb"));
            currentPreset = xml->getIntAttribute ("preset", 0);
            sclCents.clear();
            for (auto& t : juce::StringArray::fromTokens (xml->getStringAttribute ("scl"), ",", "")) if (t.isNotEmpty()) sclCents.push_back (t.getDoubleValue());
            sclTitle = xml->getStringAttribute ("sclTitle");
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            updateTuning();
        }
    }
}

// ------------------------------------------------------------------ display
SpraakProcessor::Display SpraakProcessor::display() const
{
    const juce::SpinLock::ScopedLockType l (displayLock);
    return disp;
}

void SpraakProcessor::publishDisplay (const Voice& v)
{
    const Utterance& u = v.utterance();
    const juce::SpinLock::ScopedTryLockType l (displayLock);
    if (! l.isLocked()) return;
    const int w = juce::jlimit (0, juce::jmax (0, u.nWords - 1), v.currentWord());
    std::memcpy (disp.word, u.nWords > 0 ? u.words[w] : "", sizeof (disp.word));
    std::memcpy (disp.text, u.text, sizeof (disp.text));
    disp.phon = v.currentPhon();
}

// ------------------------------------------------------------------ triggers
void SpraakProcessor::startNext (float velocity)
{
    // typed text (Enter) that is still being turned into phonemes: wait for it rather than say the next queued word
    if (plan->urgentWaiting() || ! (plan->popUrgent (scratch) || plan->pop (scratch))) { pendingTrig = (int) (sr * 3.0); pendingVel = velocity; return; }   // the planner may be refilling after a settings change: wait up to 3 s
    pendingTrig = 0;
    startNextFromScratch (velocity);
}

void SpraakProcessor::startNextFromScratch (float velocity)
{
    if (plan->urgentPending()) plan->popUrgent (scratch);
    const int engine = (int) apvts.getRawParameterValue ("engine")->load();
    const int nv = engine == 0 ? (int) apvts.getRawParameterValue ("poly")->load() : 1;
    const float startP = juce::jlimit (0.f, 1.f, apvts.getRawParameterValue ("start")->load() + startOff);
    for (int k = 0; k < 4; ++k)
    {
        if (k < nv) voices[k].start (scratch, startP);
        else voices[k].stop();
    }
    lastVel = velocity;
    for (auto& r : randCv) r = rnd.nextFloat();
    trigCounter.fetch_add (1);
}

void SpraakProcessor::handleTrigger (bool on, float velocity)
{
    const int mode = (int) apvts.getRawParameterValue ("trig")->load();
    const bool retrig = apvts.getRawParameterValue ("retrig")->load() > 0.5f;
    const bool running = ! voices[0].finished();
    if (on)
    {
        switch (mode)
        {
            case 0: break;                                                       // AUTO: notes only set pitch
            case 1: if (retrig || ! running) startNext (velocity); break;
            case 2: gate = true; startNext (velocity); break;
            case 3: case 4:
                gate = true;
                if (running) { for (auto& v : voices) v.advance(); trigCounter.fetch_add (1); }
                else startNext (velocity);
                for (auto& v : voices) v.setGate (true);
                break;
        }
    }
    else if (nHeld == 0)
    {
        gate = false;
        if (mode == 2) for (auto& v : voices) v.stop();
        if (mode == 4) for (auto& v : voices) v.setGate (false);
    }
}

// ------------------------------------------------------------------ audio
void SpraakProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (speechBuf.getNumSamples() < n) { speechBuf.setSize (1, n, false, false, true); extCar.setSize (1, n, false, false, true); extMod.setSize (1, n, false, false, true); }

    // ---- side-chain inputs (copied before the outputs are written) ----
    auto grabBus = [&] (int bus, juce::AudioBuffer<float>& dst) -> bool {
        auto* b = getBus (true, bus);
        if (b == nullptr || ! b->isEnabled() || b->getNumberOfChannels() == 0) { dst.clear(); return false; }
        auto in = getBusBuffer (buffer, true, bus);
        dst.clear();
        double ss = 0;
        for (int c = 0; c < in.getNumChannels(); ++c) dst.addFrom (0, 0, in, c, 0, n, 1.f / (float) in.getNumChannels());
        const float* d = dst.getReadPointer (0);
        for (int i = 0; i < n; ++i) ss += d[i] * d[i];
        return ss / n > 1.0e-8;
    };
    const bool hasCar = grabBus (0, extCar), hasMod = grabBus (1, extMod);
    extCarrier.store (hasCar); extModulator.store (hasMod);
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    auto P = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto I = [&] (const char* id) { return (int) apvts.getRawParameterValue (id)->load(); };

    // ---- transport ----
    double bpm = 120.0, ppq = 0.0; bool playing = false; double beatsPerBar = 4.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
            if (auto ts = pos->getTimeSignature()) beatsPerBar = ts->numerator * 4.0 / std::max (1, ts->denominator);
        }

    // ---- LFOs + modulation matrix (block rate) ----
    float lfo[4];
    for (int k = 0; k < 4; ++k)
    {
        const float rate = P (("lfo" + std::to_string (k + 1) + "rate").c_str());
        const int shape = I (("lfo" + std::to_string (k + 1) + "shape").c_str());
        float& ph = lfoPhase[k];
        ph += rate * (float) n / (float) sr;
        if (ph >= 1.f) { ph -= std::floor (ph); lfoSH[k] = rnd.nextFloat() * 2.f - 1.f; }
        switch (shape)
        {
            case 0: lfo[k] = std::sin (6.2831853f * ph); break;
            case 1: lfo[k] = 1.f - 4.f * std::fabs (ph - 0.5f); break;
            case 2: lfo[k] = 2.f * ph - 1.f; break;
            case 3: lfo[k] = ph < 0.5f ? 1.f : -1.f; break;
            default: lfo[k] = lfoSH[k]; break;
        }
    }
    float off[prm::dCount] {};
    for (int k = 0; k < 4; ++k)
    {
        const std::string s = std::to_string (k + 1);
        const int src = I (("cv" + s + "src").c_str()), dst = I (("cv" + s + "dst").c_str());
        if (dst <= 0) continue;
        float v = 0;
        switch (src)
        {
            case 0: v = P (("cv" + s).c_str()); break;
            case 1: case 2: case 3: case 4: v = lfo[src - 1]; break;
            case 5: v = lastVel; break;
            case 6: v = modWheel; break;
            default: v = randCv[k]; break;
        }
        off[dst] += v * P (("cv" + s + "amt").c_str());
    }
    for (int k = 0; k < 3; ++k)
    {
        const int dst = I (("m" + std::to_string (k + 1) + "dst").c_str());
        if (dst > 0) off[dst] += P (("m" + std::to_string (k + 1)).c_str());
    }
    auto cl = [] (float v) { return juce::jlimit (0.f, 1.f, v); };
    const float pitchSemis = P ("pitch") + off[prm::dPitch] * 24.f;
    const float formantScale = 0.7f * std::pow (2.0714f, cl (P ("formant") + off[prm::dFormant]));
    const float intonation = 2.f * cl (P ("inton") + off[prm::dInton]);
    const float bright = cl (P ("bright") + off[prm::dBright]);
    const float speed = 0.4f * std::pow (6.25f, cl (P ("speed") + off[prm::dSpeed]));
    const float emotion = 2.f * cl (P ("emotion") + off[prm::dEmotion]) - 1.f;
    const float fine = juce::jlimit (0.f, 100.f, P ("fine") + off[prm::dFine] * 50.f);
    const int bits = juce::jlimit (2, 16, (int) std::round (P ("bits") - off[prm::dCrush] * 12.f));
    const int down = juce::jlimit (1, 32, (int) std::round (P ("down") + off[prm::dCrush] * 8.f));
    const float dlyMix = cl (P ("dlymix") + off[prm::dDelay]);
    const float revMix = cl (P ("revmix") + off[prm::dReverb]);
    const float width = cl (P ("width") + off[prm::dWidth]);
    const int engine = I ("engine"), mode = I ("trig"), poly = I ("poly"), chord = juce::jlimit (1, 10, I ("chord")) - 1;
    const int rootParam = I ("root");
    // start point is read at trigger time; apply its modulation by nudging the parameter value
    startOff = off[prm::dStart];

    plan->lang = I ("lang"); plan->source = I ("source"); plan->listNum = I ("list"); plan->order = I ("order");
    plan->discardStale();
    if (I ("micro") != lastMicro) updateTuning();

    // ---- events ----
    struct Ev { int pos; int type; int note; float vel; };   // type 0 on, 1 off, 2 auto/manual trigger
    Ev evs[256]; int nev = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (nev >= 250) break;
        if (m.isNoteOn()) evs[nev++] = { meta.samplePosition, 0, m.getNoteNumber(), m.getFloatVelocity() };
        else if (m.isNoteOff()) evs[nev++] = { meta.samplePosition, 1, m.getNoteNumber(), 0.f };
        else if (m.isController() && m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.f;
        else if (m.isAllNotesOff()) { nHeld = 0; evs[nev++] = { meta.samplePosition, 1, -1, 0.f }; }
    }
    if (manualTrig.exchange (false)) evs[nev++] = { 0, 2, -1, 1.f };
    const int rateSel = I ("rate");
    if (mode == 0 && rateSel > 0)
    {
        static const double q[8] = { 0, 0.25, 0.5, 1, 2, 0, 0, 0 };
        double rq = rateSel >= 5 ? beatsPerBar * (rateSel == 5 ? 1 : rateSel == 6 ? 2 : 4) : q[rateSel];
        const double pps = bpm / 60.0 / sr;
        double p0 = playing ? ppq : autoPhase;
        if (! playing) autoPhase += n * pps;
        double k = std::ceil ((p0 - 1e-9) / rq) * rq;
        while (k < p0 + n * pps && nev < 255)
        {
            evs[nev++] = { juce::jlimit (0, n - 1, (int) ((k - p0) / pps)), 2, -1, 1.f };
            k += rq;
        }
    }
    std::stable_sort (evs, evs + nev, [] (const Ev& a, const Ev& b) { return a.pos < b.pos; });

    // ---- pitches ----
    const int root = nHeld > 0 ? held[nHeld - 1] : rootParam;
    auto voiceNote = [&] (int k, int count) -> float {
        float note;
        if (engine == 0 && nHeld > 1 && count > 1) note = (float) held[juce::jmax (0, nHeld - 1 - k)];
        else note = (float) root + (float) kChords[chord][k % 4] + (k >= 4 ? 12.f : 0.f);
        const float det = count > 1 ? fine * ((float) k - (count - 1) * 0.5f) / std::max (1.f, (count - 1) * 0.5f) : 0.f;
        return note + noteCents ((int) note) / 100.f + det / 100.f;
    };
    const int nSpeech = engine == 0 ? poly : 1;
    const float glideT = P ("glide");
    const float glideCoef = 1.f - std::exp (-32.f / (float) sr / (0.002f + glideT * 0.6f));
    float carFreq[4]; int nCar = 0;

    const int hold = mode == 3 ? 1 : mode == 4 ? 2 : 0;
    const int trgOut = I ("trgout");
    midi.clear();
    for (auto it = pendingOffs.begin(); it != pendingOffs.end();)
    {
        if (it->samples < n) { midi.addEvent (juce::MidiMessage::noteOff (1, it->note), juce::jmax (0, it->samples)); it = pendingOffs.erase (it); }
        else { it->samples -= n; ++it; }
    }
    auto midiOut = [&] (int note, int pos) {
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), pos);
        const int offAt = pos + (int) (0.01 * sr);
        if (offAt < n) midi.addEvent (juce::MidiMessage::noteOff (1, note), offAt);
        else if (pendingOffs.size() < 64) pendingOffs.push_back ({ note, offAt - n });
    };

    float* speech = speechBuf.getWritePointer (0);
    std::fill (speech, speech + n, 0.f);
    int evi = 0;
    const int chunk = 32;
    if ((int) voiceTmp.size() < chunk) voiceTmp.resize (chunk);
    for (int pos = 0; pos < n; pos += chunk)
    {
        const int len = std::min (chunk, n - pos);
        while (evi < nev && evs[evi].pos < pos + len)
        {
            const Ev& e = evs[evi++];
            if (e.type == 0)
            {
                if (nHeld < 16) held[nHeld++] = e.note;
                handleTrigger (true, e.vel);
            }
            else if (e.type == 1)
            {
                if (e.note < 0) nHeld = 0;
                for (int h = 0; h < nHeld; ++h) if (held[h] == e.note) { std::memmove (held + h, held + h + 1, sizeof (int) * (size_t) (nHeld - h - 1)); --nHeld; break; }
                handleTrigger (false, 0.f);
            }
            else
            {
                const bool running = ! voices[0].finished();
                const bool retrig = P ("retrig") > 0.5f;
                if (mode == 3 || mode == 4) handleTrigger (true, 1.f);
                else if (retrig || ! running) startNext (1.f);
            }
        }
        // a trigger that found the queue empty retries for up to half a second
        if (pendingTrig > 0)
        {
            pendingTrig -= len;
            if (plan->urgentPending()) { pendingTrig = 0; plan->popUrgent (scratch); startNextFromScratch (pendingVel); }
            else if (! plan->urgentWaiting() && plan->pop (scratch)) { pendingTrig = 0; startNextFromScratch (pendingVel); }
        }
        // free-running auto: next utterance after a short gap
        if (mode == 0 && rateSel == 0)
        {
            if (voices[0].finished()) { freeGap += len; if (freeGap > (int) (sr * 0.25 / speed)) { freeGap = 0; startNext (1.f); } }
            else freeGap = 0;
        }
        // gate mode keeps talking while held
        if (mode == 2 && gate && voices[0].finished()) startNext (lastVel);

        for (int k = 0; k < nSpeech; ++k)
        {
            const float target = voiceNote (k, nSpeech) + pitchSemis;
            baseSemis[k] += (target - baseSemis[k]) * glideCoef;
            VoiceParams vp;
            vp.f0 = mtof (baseSemis[k]);
            vp.formant = formantScale; vp.intonation = intonation; vp.brightness = bright;
            vp.speed = speed; vp.emotion = emotion; vp.hold = hold;
            vp.level = 1.f / std::sqrt ((float) nSpeech);
            if (! voices[k].active()) continue;
            events[k].clear();
            voices[k].render (voiceTmp.data(), len, vp, k == 0 ? &events[0] : nullptr);
            for (int i = 0; i < len; ++i) speech[pos + i] += voiceTmp[(size_t) i];
            if (k == 0 && trgOut != 3)
            {
                if (trgOut == 0 || trgOut == 2) for (int w = 0; w < events[0].nWord; ++w) midiOut (60, pos + events[0].wordAt[w]);
                if (trgOut == 1 || trgOut == 2) for (int w = 0; w < events[0].nPhon; ++w) midiOut (62, pos + events[0].phonAt[w]);
            }
            if (k == 0 && events[0].nWord > 0) trigCounter.fetch_add (1);
        }
        for (int k = nSpeech; k < 4; ++k) if (voices[k].active()) voices[k].stop();
    }

    // ---- engine ----
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (engine == 0)
    {
        std::copy (speech, speech + n, L);
    }
    else
    {
        const bool extC = engine == 1 && hasCar, extM = engine == 1 && hasMod;
        nCar = std::max (1, poly);
        for (int k = 0; k < nCar; ++k)
        {
            float note;
            if (engine == 2 && nHeld > 0) note = (float) held[juce::jmax (0, nHeld - 1 - (k % nHeld))] + (k >= nHeld ? 12.f : 0.f);
            else note = (float) root + (float) kChords[chord][k % 4];
            const float det = nCar > 1 ? fine * ((float) k - (nCar - 1) * 0.5f) / std::max (1.f, (nCar - 1) * 0.5f) : 0.f;
            carFreq[k] = mtof (note + noteCents ((int) note) / 100.f + det / 100.f + (engine == 2 ? 0.f : pitchSemis));
        }
        vocoder.configure (I ("bands"), formantScale, bright);
        const int wave = I ("carrier");
        const float* cm = extMod.getReadPointer (0);
        const float* cc = extCar.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float m = extM ? cm[i] : speech[i];
            const float c = extC ? cc[i] : carrierOsc.tick (carFreq, nCar, wave);
            L[i] = vocoder.tick (m, c);
        }
    }

    // ---- FX: crush, width, delay, reverb, level ----
    const float q = std::pow (2.f, (float) (bits - 1));
    const double dlyQ[6] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    const int dlySamples = juce::jlimit (1, (int) dly.l.size() - 2, (int) (dlyQ[I ("dlytime")] * 60.0 / bpm * sr));
    const float fb = P ("dlyfb");
    const float level = P ("level");
    const int wsz = (int) widthBuf.size();
    for (int i = 0; i < n; ++i)
    {
        float x = L[i];
        if (down > 1) { if (crushCount++ % down == 0) crushHold = x; x = crushHold; }
        if (bits < 16) x = std::round (x * q) / q;
        // width: two modulated taps → side signal
        widthBuf[(size_t) widthW] = x;
        widthPhase += 0.25f / (float) sr; if (widthPhase >= 1.f) widthPhase -= 1.f;
        const float d1 = (0.011f + 0.002f * std::sin (6.2831853f * widthPhase)) * (float) sr;
        const float d2 = (0.017f + 0.002f * std::cos (6.2831853f * widthPhase)) * (float) sr;
        auto tap = [&] (float d) { const int di = (int) d; const float fr = d - (float) di;
            const float a = widthBuf[(size_t) ((widthW - di + wsz) % wsz)], b = widthBuf[(size_t) ((widthW - di - 1 + wsz) % wsz)];
            return a + (b - a) * fr; };
        const float side = 0.5f * (tap (d1) - tap (d2)) * width * 1.6f;
        widthW = (widthW + 1) % wsz;
        float l = x + side, r = x - side;
        // ping-pong delay
        const int dsz = (int) dly.l.size();
        const int rd = (dly.w - dlySamples + dsz) % dsz;
        const float dl = dly.l[(size_t) rd], dr = dly.r[(size_t) rd];
        dly.l[(size_t) dly.w] = 0.5f * (l + r) + dr * fb;
        dly.r[(size_t) dly.w] = dl * fb;
        dly.w = (dly.w + 1) % dsz;
        l += dl * dlyMix; r += dr * dlyMix;
        L[i] = l;
        if (R) R[i] = r;
    }
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.05f + 0.9f * P ("revsize"); rp.damping = 0.45f; rp.wetLevel = revMix * 0.6f; rp.dryLevel = 1.f; rp.width = 1.f;
    reverb.setParameters (rp);
    if (R) reverb.processStereo (L, R, n); else reverb.processMono (L, n);
    for (int i = 0; i < n; ++i) { L[i] = std::tanh (L[i] * level * 1.2f); if (R) R[i] = std::tanh (R[i] * level * 1.2f); }

    meterLevel.store (voices[0].meter());
    publishDisplay (voices[0]);
}

juce::AudioProcessorEditor* SpraakProcessor::createEditor() { return new SpraakEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpraakProcessor(); }
