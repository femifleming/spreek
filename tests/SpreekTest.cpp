// What joins the word bank and the speech computer, and what keeps Spreekbuis 2 the same instrument for old sessions:
// every cross-feature, Spreekbuis 1.0 and Spraakmaker 1.0 states (tests/fixtures, saved by the released 1.0.0 plug-ins),
// the state round trip, deterministic defaults, stereo output, the play log Tongval reads, every program.
//   SpreekTest                 run every check (exit code = number of failures)
//   SpreekTest --shots <dir>   draw every page of the window into <dir>
#include "Processor.h"
#include "Editor.h"
#include <iostream>

namespace {
int fails = 0, passes = 0;
void report (bool ok, const juce::String& name, const juce::String& detail = {})
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << "\n";
    (ok ? passes : fails)++;
}
void setP (SpreekbuisProcessor& p, const char* id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    jassert (rp != nullptr);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}
float getP (SpreekbuisProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

struct Audio
{
    juce::AudioBuffer<float> buf;
    int midiOn = 0;
    double rms (int from = 0) const
    {
        double s = 0; long n = 0;
        for (int c = 0; c < 2; ++c) for (int i = from; i < buf.getNumSamples(); ++i) { const float v = buf.getSample (c, i); s += v * v; ++n; }
        return std::sqrt (s / std::max (1L, n));
    }
    bool finite() const
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < buf.getNumSamples(); ++i) if (! std::isfinite (buf.getSample (c, i))) return false;
        return true;
    }
    double side() const   // rms of (L - R) / 2
    {
        double s = 0;
        for (int i = 0; i < buf.getNumSamples(); ++i) { const float d = 0.5f * (buf.getSample (0, i) - buf.getSample (1, i)); s += d * d; }
        return std::sqrt (s / std::max (1, buf.getNumSamples()));
    }
};
double diff (const Audio& a, const Audio& b)
{
    double s = 0; long n = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < std::min (a.buf.getNumSamples(), b.buf.getNumSamples()); ++i) { const float d = a.buf.getSample (c, i) - b.buf.getSample (c, i); s += d * d; ++n; }
    return std::sqrt (s / std::max (1L, n));
}

// a fresh instance with every layer off, the bank loaded synchronously, the planner driven from processBlock
std::unique_ptr<SpreekbuisProcessor> fresh (std::function<void (SpreekbuisProcessor&)> setup, int seed = 11)
{
    auto p = std::make_unique<SpreekbuisProcessor>();
    setP (*p, "orca_on", 0);
    for (auto id : { "loop_on", "cloud_on", "vowel_on", "g_on", "spraak_on" }) setP (*p, id, 0);
    setP (*p, "all_items", 1); setP (*p, "space", 0);
    setup (*p);
    p->setOfflineTest (seed);
    p->loadBankNow();
    p->prepareToPlay (48000, 512);
    return p;
}
Audio render (SpreekbuisProcessor& p, double seconds, std::vector<int> notes = {}, std::function<void (int, juce::MidiBuffer&)> perBlock = {})
{
    const int bs = 512;
    const int blocks = (int) (seconds * 48000 / bs);
    Audio a; a.buf.setSize (2, blocks * bs); a.buf.clear();
    juce::AudioBuffer<float> b (2, bs);
    for (int k = 0; k < blocks; ++k)
    {
        juce::MidiBuffer m;
        if (k == 2) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (k == blocks - 10) for (int n : notes) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        if (perBlock) perBlock (k, m);
        b.clear();
        p.processBlock (b, m);
        for (const auto ev : m) if (ev.getMessage().isNoteOn()) a.midiOn++;
        for (int c = 0; c < 2; ++c) a.buf.copyFrom (c, k * bs, b, c, 0, bs);
    }
    return a;
}
juce::String fmt (double v) { return juce::String (v, 5); }

std::unique_ptr<juce::XmlElement> fixtureXml (const juce::File& f)
{
    juce::MemoryBlock mb; f.loadFileAsData (mb);
    return juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
}
// every parameter the old state names has that value now; the ones it doesn't name are at their defaults
void checkOldParams (SpreekbuisProcessor& p, const juce::XmlElement& params, const juce::String& what, bool othersDefault)
{
    int same = 0, differ = 0, named = 0;
    juce::StringArray bad, seen;
    for (auto* c : params.getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = c->getStringAttribute ("id");
        auto* rp = p.apvts.getParameter (id);
        if (rp == nullptr) { bad.add (id + " (gone)"); ++differ; continue; }
        ++named; seen.add (id);
        const float want = (float) c->getDoubleAttribute ("value"), have = getP (p, id);
        if (std::abs (want - have) <= 1.0e-4f * std::max (1.0f, std::abs (want))) ++same; else { ++differ; bad.add (id + "=" + juce::String (have) + " want " + juce::String (want)); }
    }
    report (differ == 0 && named > 30, what + ": every saved parameter has its value", juce::String (same) + " of " + juce::String (named) + (bad.isEmpty() ? "" : "; " + bad.joinIntoString (", ").substring (0, 300)));
    if (othersDefault)
    {
        int nd = 0; juce::StringArray off;
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! seen.contains (rp->getParameterID()) && std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-5f) { ++nd; off.add (rp->getParameterID()); }
        report (nd == 0, what + ": the parameters 2.0 added are at their defaults", off.joinIntoString (", "));
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto testPlays = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spreek-test-plays.json");
    sb::setPlayLogOverride (testPlays);   // the words heard here are not the user's

    if (argc > 2 && juce::String (argv[1]) == "--shots")
    {
        juce::File out (argv[2]); out.createDirectory();
        SpreekbuisProcessor p;
        setP (p, "orca_on", 0);
        // something to look at on every page: the lesson loop with the speech computer answering it, the cloud, the chart
        setP (p, "loop_on", 1); setP (p, "cloud_on", 1); setP (p, "vowel_on", 1); setP (p, "spraak_on", 1); setP (p, "loop_echo", 1);
        setP (p, "trig", 1); setP (p, "cv1src", 8); setP (p, "cv1dst", 15); setP (p, "cv2src", 1); setP (p, "cv2dst", 2);
        p.setOfflineTest (3);
        p.loadBankNow(); p.prepareToPlay (48000, 512);
        render (p, 3.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (SpreekbuisEditor::W, SpreekbuisEditor::H);
        auto* se = dynamic_cast<SpreekbuisEditor*> (ed.get());
        for (int pg = 0; pg < SpreekbuisEditor::kPages; ++pg)
        {
            se->showPage (pg);
            for (int k = 0; k < 4; ++k) { render (p, 0.1); se->tickForTest(); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto f = out.getChildFile ("page" + juce::String (pg + 1) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }

    const auto fixtures = juce::File (__FILE__).getSiblingFile ("fixtures");

    // ================= deterministic defaults
    {
        SpreekbuisProcessor p;
        int off = 0; juce::StringArray which;
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-6f) { ++off; which.add (rp->getParameterID()); }
        report (off == 0, "a fresh instance: every parameter at its default", which.joinIntoString (", "));
        report (p.getCurrentProgram() == 0 && p.presetLabel().isEmpty() && p.getProgramName (0) == "Init", "a fresh instance: no preset loaded (program Init)");
        report (getP (p, "spraak_on") < 0.5f && getP (p, "loop_on") > 0.5f, "a fresh instance is Spreekbuis 1.0's default (the loop on, the speech computer off)");
        report (p.getTotalNumOutputChannels() == 2 && p.getMainBusNumOutputChannels() == 2, "stereo out", juce::String (p.getTotalNumOutputChannels()) + " channels");
    }
    {
        // two fresh instances, nothing seeded by the test: the same sound
        auto make = [] {
            auto p = std::make_unique<SpreekbuisProcessor>();
            setP (*p, "orca_on", 0); setP (*p, "all_items", 1);
            setP (*p, "cloud_on", 1);
            p->loadBankNow(); p->prepareToPlay (48000, 512);
            return p;
        };
        auto a = make(), b = make();
        auto ra = render (*a, 4), rb = render (*b, 4);
        report (ra.rms() > 1.0e-3 && diff (ra, rb) == 0.0, "two fresh instances render identically (nothing random at start)", "rms " + fmt (ra.rms()) + ", diff " + fmt (diff (ra, rb)));
        report (ra.side() > 1.0e-3, "the default sound is stereo (the cloud's spread)", "side " + fmt (ra.side()));
    }
    {
        // the same with the speech computer running (seeded, planner on this thread): repeatable
        auto mk = [] { return fresh ([] (auto& p) { setP (p, "spraak_on", 1); setP (p, "rate", 3); setP (p, "width", 0.8f); setP (p, "source", 3); }, 5); };
        auto a = mk(), b = mk();
        auto ra = render (*a, 4), rb = render (*b, 4);
        report (ra.rms() > 1.0e-3 && diff (ra, rb) == 0.0, "the speech computer renders repeatably (fixed seed)", "rms " + fmt (ra.rms()) + ", diff " + fmt (diff (ra, rb)));
        report (ra.side() > 1.0e-3, "the speech computer is stereo (width)", "side " + fmt (ra.side()));
    }

    // ================= what joins them
    {
        // the speech computer answers the loop: in the answer bars it says the item the loop just played
        auto mk = [] (bool echo) {
            return fresh ([echo] (auto& p) {
                setP (p, "loop_on", 1); setP (p, "loop_level", 0); setP (p, "loop_whisper", 0); setP (p, "loop_bpm", 160); setP (p, "loop_gap", 1);
                setP (p, "spraak_on", 1); setP (p, "trig", 1); setP (p, "loop_echo", echo ? 1.f : 0.f); setP (p, "revmix", 0);
            });
        };
        auto on = mk (true), off = mk (false);
        auto ro = render (*on, 6), rf = render (*off, 6);
        auto d = on->display();
        report (ro.rms() > 3.0e-3 && rf.rms() < 1.0e-4 && on->triggerCount() > 0, "loop_echo: the speech computer answers the loop",
                "on " + fmt (ro.rms()) + " off " + fmt (rf.rms()) + ", triggers " + juce::String (on->triggerCount()) + ", said \"" + juce::String (d.text) + "\"");
        auto bank = on->uiBank(); bool known = false;
        for (auto& it : bank->items) known |= it.nl.trim().equalsIgnoreCase (juce::String (d.text).trim()) || it.en.trim().equalsIgnoreCase (juce::String (d.text).trim());
        report (known, "loop_echo: what it says is the loop's item", juce::String (d.text));
    }
    {
        // the loop's word onsets trigger the speech computer; "trigger + toon" also gives it the loop's notes
        auto mk = [] (int lt) {
            return fresh ([lt] (auto& p) {
                setP (p, "loop_on", 1); setP (p, "loop_level", 0); setP (p, "loop_whisper", 0); setP (p, "loop_set", 1); setP (p, "loop_bpm", 140);
                setP (p, "spraak_on", 1); setP (p, "trig", 1); setP (p, "loop_trig", (float) lt); setP (p, "loop_note", 43); setP (p, "revmix", 0);
            });
        };
        auto p0 = mk (0), p1 = mk (1), p2 = mk (2);
        auto r0 = render (*p0, 6), r1 = render (*p1, 6), r2 = render (*p2, 6);
        report (r0.rms() < 1.0e-4 && r1.rms() > 3.0e-3 && p1->triggerCount() > 2, "loop_trig trigger: the loop's words trigger the speech computer",
                "uit " + fmt (r0.rms()) + ", trigger " + fmt (r1.rms()) + ", triggers " + juce::String (p1->triggerCount()));
        report (r2.rms() > 3.0e-3 && diff (r1, r2) > 1.0e-3, "loop_trig trigger + toon: and its notes set the pitch", "diff " + fmt (diff (r1, r2)));
    }
    {
        // source LES: the speech computer speaks the bank's lesson (its words, from the planner)
        auto p = fresh ([] (auto& p) { setP (p, "spraak_on", 1); setP (p, "source", 6); setP (p, "rate", 4); setP (p, "lang", 1); });
        juce::StringArray said;
        auto r = render (*p, 8, {}, [&] (int k, juce::MidiBuffer&) { if (k % 8 == 0) { auto t = juce::String (p->display().text).trim(); if (t.isNotEmpty()) said.addIfNotAlreadyThere (t); } });
        auto bank = p->uiBank(); int inBank = 0;
        for (auto& s : said) for (auto& it : bank->items) if (it.nl.trim().equalsIgnoreCase (s)) { ++inBank; break; }
        report (r.rms() > 3.0e-3 && said.size() > 0 && inBank == said.size(), "source les: the speech computer says the bank's items", said.joinIntoString (" | ").substring (0, 200));
    }
    {
        // the vocoder listens to the bank's voice (the loop) instead of the speech
        auto mk = [] (int vm) {
            return fresh ([vm] (auto& p) {
                setP (p, "loop_on", 1); setP (p, "loop_whisper", 0); setP (p, "loop_bpm", 140);
                setP (p, "spraak_on", 1); setP (p, "trig", 1); setP (p, "engine", 1); setP (p, "vocmod", (float) vm); setP (p, "revmix", 0);
            });
        };
        auto a = mk (0), b = mk (1);
        auto ra = render (*a, 4), rb = render (*b, 4);
        // with vocmod spraak the loop is heard dry (and the vocoder is silent: nothing triggers it); with vocmod loop the
        // loop is heard only through the vocoder
        report (ra.rms() > 1.0e-3 && rb.rms() > 1.0e-3 && diff (ra, rb) > 1.0e-3, "vocmod loop: the vocoder speaks with the loop's voice", "dry " + fmt (ra.rms()) + ", vocoded " + fmt (rb.rms()) + ", diff " + fmt (diff (ra, rb)));
    }
    {
        // the speech computer heard through the vowel chart
        auto mk = [] (int on) {
            return fresh ([on] (auto& p) {
                setP (p, "spraak_on", 1); setP (p, "rate", 3); setP (p, "revmix", 0);
                setP (p, "vowel_on", (float) on); setP (p, "vowel_src", 7); setP (p, "vowel_glide", 2);
            });
        };
        auto a = mk (0), b = mk (1);
        auto ra = render (*a, 4), rb = render (*b, 4);
        report (rb.rms() > 1.0e-3 && diff (ra, rb) > 1.0e-3 && b->layerLevel[3].load() > 0, "klinker bron spraak: the speech computer through the vowel chart", "dry " + fmt (ra.rms()) + ", shaped " + fmt (rb.rms()));
    }
    {
        // the effects take every layer
        auto mk = [] (int all) {
            return fresh ([all] (auto& p) {
                setP (p, "loop_on", 1); setP (p, "loop_bpm", 140); setP (p, "fx_src", (float) all); setP (p, "bits", 5); setP (p, "down", 6); setP (p, "dlymix", 0.4f);
            });
        };
        auto a = mk (0), b = mk (1);
        auto ra = render (*a, 4), rb = render (*b, 4);
        report (rb.rms() > 1.0e-3 && diff (ra, rb) > 3.0e-3, "fx_src alles: the loop through crush and echo", "diff " + fmt (diff (ra, rb)));
    }
    {
        // the bank's word onsets as a modulation source, the matrix reaching the bank's layers
        auto p = fresh ([] (auto& p) {
            setP (p, "loop_on", 1); setP (p, "loop_bpm", 160); setP (p, "cloud_on", 1);
            setP (p, "cv1src", 8); setP (p, "cv1dst", 15); setP (p, "cv1amt", 1);       // woord -> wolk dichtheid
            setP (p, "cv2src", 9); setP (p, "cv2dst", 13); setP (p, "cv2amt", 1);       // bank -> klinker x
            setP (p, "cv3src", 1); setP (p, "cv3dst", 17); setP (p, "cv3amt", 0.5f);    // lfo1 -> wolk toonhoogte
        });
        float mx[3] = { 0, 0, 0 };
        render (*p, 4, {}, [&] (int, juce::MidiBuffer&) {
            mx[0] = std::max (mx[0], std::abs (p->modShown[(size_t) sm::prm::dCloudDens].load()));
            mx[1] = std::max (mx[1], std::abs (p->modShown[(size_t) sm::prm::dVowelX].load()));
            mx[2] = std::max (mx[2], std::abs (p->modShown[(size_t) sm::prm::dCloudPitch].load()));
        });
        report (mx[0] > 0.2f && mx[1] > 0.05f && mx[2] > 0.1f, "modulation: word onsets, the bank's loudness and an LFO move the bank's layers",
                fmt (mx[0]) + " " + fmt (mx[1]) + " " + fmt (mx[2]));
        // and the speech computer's loudness moves the cloud
        auto q = fresh ([] (auto& p) { setP (p, "spraak_on", 1); setP (p, "rate", 3); setP (p, "cv1src", 10); setP (p, "cv1dst", 20); setP (p, "cv1amt", 1); });
        float m = 0;
        render (*q, 3, {}, [&] (int, juce::MidiBuffer&) { m = std::max (m, std::abs (q->modShown[(size_t) sm::prm::dWhisper].load())); });
        report (m > 0.1f, "modulation: the speech computer's loudness as a source", fmt (m));
    }
    {
        // every destination the matrix gained reaches its layer (the knob source, at 0.8)
        for (int dst = sm::prm::dVowelX; dst < sm::prm::dCount; ++dst)
        {
            std::vector<int> notes;
            auto layer = [dst, &notes] (SpreekbuisProcessor& p) {
                using namespace sm::prm;
                if (dst == dVowelX || dst == dVowelY) { setP (p, "vowel_on", 1); setP (p, "keys", 1); setP (p, "vowel_glide", 0); setP (p, "vowel_y", 0.2f); notes = { 48 }; }
                else if (dst == dGSoft) { setP (p, "g_on", 1); setP (p, "keys", 2); notes = { 60 }; }
                else if (dst == dLoopPitch) { setP (p, "loop_on", 1); setP (p, "loop_whisper", 0); setP (p, "loop_bpm", 160); }
                else if (dst == dWhisper) { setP (p, "cloud_on", 1); setP (p, "cloud_whisper", 1); setP (p, "whisper_level", 0.3f); }
                else { setP (p, "cloud_on", 1); setP (p, "cloud_density", 10); }
            };
            auto base = fresh (layer);
            auto mod = fresh ([&] (auto& p) { layer (p); setP (p, "cv3src", 0); setP (p, "cv3dst", (float) dst); setP (p, "cv3amt", 1); setP (p, "cv3", 0.8f); });
            auto rb = render (*base, 3, notes), rm = render (*mod, 3, notes);
            report (rm.rms() > 1.0e-3 && diff (rb, rm) > 1.0e-3, juce::String ("modulation: knob -> ") + sm::prm::kDest[dst], "rms " + fmt (rm.rms()) + ", diff " + fmt (diff (rb, rm)));
        }
    }
    {
        // keys -> spraak: notes trigger the speech computer and set its pitch; keys -> woorden leaves it alone
        auto mk = [] (int keys) { return fresh ([keys] (auto& p) { setP (p, "spraak_on", 1); setP (p, "trig", 1); setP (p, "keys", (float) keys); setP (p, "revmix", 0); }); };
        auto a = mk (5), b = mk (0);
        auto ra = render (*a, 2, { 55 }), rb = render (*b, 2, { 55 });
        report (ra.rms() > 3.0e-3 && a->triggerCount() > 0, "keys spraak: a note triggers the speech computer", fmt (ra.rms()));
        report (b->triggerCount() == 0, "keys woorden: notes don't reach the speech computer", juce::String (b->triggerCount()));
    }
    {
        // Orca: "=text" makes the speech computer say it (even switched off), "!" triggers it
        auto p = fresh ([] (auto&) {});
        auto r = render (*p, 3, {}, [&] (int k, juce::MidiBuffer&) { if (k == 4) p->pushOrca ("=hallo_wereld"); });
        report (r.rms() > 3.0e-3 && juce::String (p->display().text).containsIgnoreCase ("hallo"), "orca =hallo_wereld: the speech computer says it", juce::String (p->display().text));
        auto q = fresh ([] (auto& p) { setP (p, "spraak_on", 1); setP (p, "trig", 1); });
        auto rq = render (*q, 2, {}, [&] (int k, juce::MidiBuffer&) { if (k == 4) q->pushOrca ("!"); });
        report (rq.rms() > 3.0e-3 && q->triggerCount() > 0, "orca !: triggers the speech computer", fmt (rq.rms()));
    }
    {
        // the voice switch reaches the speech computer too (the woman's: higher formants, an octave up)
        auto mk = [] (int v) { return fresh ([v] (auto& p) { setP (p, "spraak_on", 1); setP (p, "rate", 3); setP (p, "voice", (float) v); setP (p, "revmix", 0); }); };
        auto a = mk (0), b = mk (1);
        auto ra = render (*a, 3), rb = render (*b, 3);
        report (ra.rms() > 1.0e-3 && rb.rms() > 1.0e-3 && diff (ra, rb) > 1.0e-3, "stem vrouw: the speech computer's voice changes with it", "diff " + fmt (diff (ra, rb)));
    }

    // ================= old sessions
    {
        for (auto name : { "spreekbuis-1.0.0-vst3-a", "spreekbuis-1.0.0-vst3-b", "spreekbuis-1.0.0-au-a" })
        {
            auto f = fixtures.getChildFile (juce::String (name) + ".state");
            auto xml = fixtureXml (f);
            if (xml == nullptr) { report (false, juce::String (name) + ": fixture readable", f.getFullPathName()); continue; }
            SpreekbuisProcessor p;
            juce::MemoryBlock mb; f.loadFileAsData (mb);
            setP (p, "spraak_on", 1); setP (p, "pitch", 7);   // (not in the old state: must go back to the defaults)
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            checkOldParams (p, *xml->getChildByName ("params"), juce::String ("Spreekbuis 1.0 state ") + name, true);
            if (auto* s = xml->getChildByName ("SpreekbuisState"))
                report (p.orcaPort() == s->getIntAttribute ("orcaPort", 49162), juce::String (name) + ": the Orca port", juce::String (p.orcaPort()));
        }
        // what hosts wrapped around it: REAPER's VST3 chunk, the Audio Unit's preset (property list)
        for (auto [file, ref] : { std::pair { "spreekbuis-1.0.0-vst3-a.hostchunk", "spreekbuis-1.0.0-vst3-a.state" }, std::pair { "spreekbuis-1.0.0-au-a.aupreset", "spreekbuis-1.0.0-au-a.state" } })
        {
            SpreekbuisProcessor p; juce::MemoryBlock mb; fixtures.getChildFile (file).loadFileAsData (mb);
            const bool ok = p.loadAnyState (mb.getData(), mb.getSize());
            auto xml = fixtureXml (fixtures.getChildFile (ref));
            report (ok, juce::String ("loads ") + file);
            if (ok && xml) checkOldParams (p, *xml->getChildByName ("params"), juce::String (file), true);
        }
        // and an old session still plays: the woman's voice, every layer
        {
            SpreekbuisProcessor p; juce::MemoryBlock mb; fixtures.getChildFile ("spreekbuis-1.0.0-vst3-a.state").loadFileAsData (mb);
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            setP (p, "orca_on", 0); p.setOfflineTest (1); p.loadBankNow(); p.prepareToPlay (48000, 512);
            auto r = render (p, 4, { 60, 64 });
            report (r.finite() && r.rms() > 1.0e-3 && getP (p, "spraak_on") < 0.5f, "a Spreekbuis 1.0 session plays (and the speech computer stays off)", fmt (r.rms()));
        }
    }
    {
        // Spraakmaker 1.0: its state (from a host) imports into the speech computer
        auto f = fixtures.getChildFile ("spraakmaker-1.0.0-vst3-a.state");
        auto xml = fixtureXml (f);
        SpreekbuisProcessor p; juce::MemoryBlock mb; f.loadFileAsData (mb);
        const bool ok = p.loadAnyState (mb.getData(), mb.getSize());
        report (ok && xml != nullptr, "loads a Spraakmaker 1.0 state");
        if (ok && xml)
        {
            checkOldParams (p, *xml, "Spraakmaker 1.0 state", false);
            report (getP (p, "spraak_on") > 0.5f && getP (p, "loop_on") < 0.5f && getP (p, "cloud_on") < 0.5f && (int) getP (p, "keys") == 5,
                    "Spraakmaker 1.0 state: only the speech computer, played from the keys");
        }
        SpreekbuisProcessor q; juce::MemoryBlock hc; fixtures.getChildFile ("spraakmaker-1.0.0-vst3-a.hostchunk").loadFileAsData (hc);
        report (q.loadAnyState (hc.getData(), hc.getSize()) && (int) getP (q, "engine") == (int) getP (p, "engine") && (int) getP (q, "list") == (int) getP (p, "list"),
                "loads a Spraakmaker 1.0 state from REAPER's VST3 chunk");
        // and it plays
        setP (p, "orca_on", 0); p.setOfflineTest (2); p.loadBankNow(); p.prepareToPlay (48000, 512);
        auto r = render (p, 4, { 48, 52, 55 });
        report (r.finite() && r.rms() > 1.0e-3, "a Spraakmaker 1.0 state plays", fmt (r.rms()));
        // a preset file Spraakmaker 1.0 wrote in its slots folder (…/Spraakmaker/pNN.xml)
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spreek-test-p42.xml");
        juce::XmlElement sp ("SpraakPreset"); sp.setAttribute ("presetName", "OUDE STEM");
        auto* params = new juce::XmlElement (*xml); sp.addChildElement (params);
        sp.writeTo (tmp);
        SpreekbuisProcessor s;
        report (s.loadPresetFile (tmp) && (int) getP (s, "engine") == (int) getP (p, "engine") && s.presetLabel() == "OUDE STEM", "loads a Spraakmaker 1.0 preset file", s.presetLabel());
        tmp.deleteFile();
    }

    // ================= state round trip
    {
        SpreekbuisProcessor a;
        juce::Random r (99);
        for (auto* prm : a.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (r.nextFloat());
        a.typeAndSpeak ("de fiets is rood");
        a.planner().setKeyboardText ("de fiets is rood");
        a.setOrcaPort (49200);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        SpreekbuisProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        int same = 0, n = 0; juce::StringArray bad;
        for (int i = 0; i < a.getParameters().size(); ++i)
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i]);
            auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b.getParameters()[i]);
            ++n;
            const float va = pa->convertFrom0to1 (pa->getValue()), vb = pb->convertFrom0to1 (pb->getValue());   // (a switch keeps the raw value it was given)
            if (std::abs (va - vb) <= 1.0e-5f * std::max (1.0f, std::abs (va)) || pa->getCurrentValueAsText() == pb->getCurrentValueAsText()) ++same; else bad.add (pa->getParameterID());
        }
        report (same == n, "state round trip: every parameter", juce::String (same) + "/" + juce::String (n) + " " + bad.joinIntoString (","));
        report (b.planner().getKeyboardText() == "de fiets is rood" && b.orcaPort() == 49200, "state round trip: typed text and the Orca port");
        juce::MemoryBlock mb2; b.getStateInformation (mb2);
        report (mb == mb2, "state round trip: the state saved again is the same bytes");
        // a VST2 host sets the saved program again right after restoring the state: nothing may change
        b.setCurrentProgram (b.getCurrentProgram());
        juce::MemoryBlock mb3; b.getStateInformation (mb3);
        report (mb3 == mb, "state round trip: the host re-selecting the current program keeps the state");
    }

    // ================= the play log Tongval reads
    {
        report (sb::supportDir().getFileName() == "Spreekbuis", "the data folder is still called Spreekbuis", sb::supportDir().getFullPathName());
        sb::setPlayLogOverride ({});
        report (sb::playLogFile() == sb::supportDir().getChildFile ("plays.json"), "the play log is …/Spreekbuis/plays.json", sb::playLogFile().getFullPathName());
        sb::setPlayLogOverride (testPlays);
        testPlays.replaceWithText ("{ \"version\": 1, \"plays\": { \"zz-old\": { \"first\": 20000, \"n\": 3, \"day\": 20001 } } }");
        {
            auto p = fresh ([] (auto& p) { setP (p, "loop_on", 1); setP (p, "loop_bpm", 200); setP (p, "loop_gap", 0); });
            render (*p, 6);
            p->flushPlaysNow();
        }
        auto root = juce::JSON::parse (testPlays.loadFileAsString());
        auto plays = root.getProperty ("plays", {});
        int entries = 0, wellFormed = 0;
        if (auto* o = plays.getDynamicObject())
            for (auto& kv : o->getProperties())
            {
                ++entries;
                const auto& e = kv.value;
                if (e.hasProperty ("first") && e.hasProperty ("n") && e.hasProperty ("day") && (int) e["n"] > 0) ++wellFormed;
            }
        const bool oldKept = plays.hasProperty ("zz-old") && (int) plays["zz-old"]["n"] == 3 && (int) plays["zz-old"]["first"] == 20000;
        report ((int) root.getProperty ("version", 0) == 1 && entries >= 2 && wellFormed == entries && oldKept,
                "the play log: plays written as { version 1, plays { key: { first, n, day } } }, earlier entries kept",
                juce::String (entries) + " entries");
        testPlays.deleteFile();
    }

    // ================= every program loads and plays
    {
        SpreekbuisProcessor p; setP (p, "orca_on", 0);
        p.setOfflineTest (4); p.loadBankNow(); p.prepareToPlay (48000, 512);
        int ok = 0; juce::StringArray bad;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.loadProgram (i); setP (p, "orca_on", 0); setP (p, "all_items", 1);
            auto r = render (p, 4.5, { 55 });   // (the slowest auto rate is two bars)
            if (r.finite() && r.rms() > 1.0e-4) ++ok; else bad.add (p.getProgramName (i) + " " + fmt (r.rms()));
        }
        report (ok == p.getNumPrograms(), "every program plays (" + juce::String (p.getNumPrograms()) + ")", bad.joinIntoString (", "));
    }

    std::cout << "\nSpreekTest: " << passes << " passed, " << fails << " failed\n";
    return fails;
}
