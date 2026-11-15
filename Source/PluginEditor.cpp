#include "PluginEditor.h"

using namespace sbui;
using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

juce::Font sbui::sans(float h, bool bold) { return juce::Font(juce::FontOptions("Helvetica Neue", h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sbui::mono(float h) { return juce::Font(juce::FontOptions("Menlo", h, juce::Font::plain)); }

// ---------------------------------------------------------------- look
Look::Look() {
    setColour(juce::Label::textColourId, ink);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::backgroundColourId, card);
    setColour(juce::ComboBox::outlineColourId, ink);
    setColour(juce::ComboBox::arrowColourId, ink);
    setColour(juce::PopupMenu::backgroundColourId, card);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour(juce::PopupMenu::highlightedTextColourId, paper);
    setColour(juce::TextEditor::backgroundColourId, card);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, ink);
    setColour(juce::TextEditor::focusedOutlineColourId, orange);
    setColour(juce::Slider::textBoxTextColourId, mute);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}
void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    float d = std::min(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(d, d);
    g.setColour(card); g.fillEllipse(c);
    g.setColour(ink); g.drawEllipse(c, 2.0f);
    float ang = a0 + pos * (a1 - a0);
    juce::Path arc;
    arc.addCentredArc(c.getCentreX(), c.getCentreY(), d / 2 - 5, d / 2 - 5, 0, a0, ang, true);
    g.setColour(orange); g.strokePath(arc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour(ink);
    auto p = c.getCentre().getPointOnCircumference(d / 2 - 9, ang);
    g.drawLine({ c.getCentre(), p }, 2.2f);
}
void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    bool on = b.getToggleState();
    g.setColour(on ? ink : (over || down ? soft : card));
    g.fillRoundedRectangle(r, 5);
    g.setColour(ink); g.drawRoundedRectangle(r, 5, 1.6f);
}
void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(b.getToggleState() ? paper : ink);
    g.setFont(sans(12.5f, true));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}
void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1);
    g.setColour(card); g.fillRoundedRectangle(r, 5);
    g.setColour(ink); g.drawRoundedRectangle(r, 5, 1.6f);
    juce::Path t; float cx = (float) w - 12, cy = (float) h / 2;
    t.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.fillPath(t);
}

// ---------------------------------------------------------------- widgets
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::String name;
    std::unique_ptr<SA> att;
    Knob(SpreekbuisProcessor& p, const char* id, juce::String label) : name(std::move(label)) {
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 14);
        s.setColour(juce::Slider::textBoxTextColourId, mute);
        addAndMakeVisible(s);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        att = std::make_unique<SA>(p.apvts, id, s);
        s.textFromValueFunction = [](double v) { return std::abs(v) >= 100 || (std::abs(v) >= 10 && v == std::floor(v)) ? juce::String((int) std::lround(v)) : juce::String(v, std::abs(v) >= 10 ? 1 : 2); };
        s.updateText();
    }
    void paint(juce::Graphics& g) override { g.setColour(ink); g.setFont(sans(11.5f, true)); g.drawText(name, getLocalBounds().removeFromTop(14), juce::Justification::centred); }
    void resized() override { s.setBounds(getLocalBounds().withTrimmedTop(14)); }
};

struct Toggle : juce::TextButton {
    std::unique_ptr<BA> att;
    Toggle(SpreekbuisProcessor& p, const char* id, juce::String t) : juce::TextButton(t) {
        setClickingTogglesState(true);
        att = std::make_unique<BA>(p.apvts, id, *this);
    }
};

struct Choice : juce::Component {
    juce::ComboBox box; juce::String name; std::unique_ptr<CA> att;
    Choice(SpreekbuisProcessor& p, const char* id, juce::String label) : name(std::move(label)) {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(id))) box.addItemList(c->choices, 1);
        else if (auto* ip = dynamic_cast<juce::AudioParameterInt*>(p.apvts.getParameter(id)))
            for (int v = ip->getRange().getStart(); v <= ip->getRange().getEnd(); ++v) box.addItem(v == 0 && juce::String(id) == "loop_unit" ? "current (Tongval)" : juce::String(v), v - ip->getRange().getStart() + 1);
        addAndMakeVisible(box);
        att = std::make_unique<CA>(p.apvts, id, box);
    }
    void paint(juce::Graphics& g) override { g.setColour(mute); g.setFont(mono(10.5f)); g.drawText(name.toUpperCase(), getLocalBounds().removeFromTop(14), juce::Justification::centredLeft); }
    void resized() override { box.setBounds(getLocalBounds().withTrimmedTop(15)); }
};

// karaoke line: the item you are hearing, word by word
struct Karaoke : juce::Component {
    SpreekbuisProcessor& p;
    explicit Karaoke(SpreekbuisProcessor& pr) : p(pr) {}
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(navy); g.fillRoundedRectangle(r, 8);
        auto b = p.uiBank();
        int item = p.shownItem.load(), word = p.shownWord.load();
        auto inner = getLocalBounds().reduced(24, 14);
        g.setColour(juce::Colours::white.withAlpha(0.55f)); g.setFont(mono(11));
        if (!b || !b->manifestFound) {
            g.drawText("KARAOKE", inner.removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(juce::Colours::white); g.setFont(sans(30, true));
            g.drawText(b ? "Geen stemmen - run tools/render.sh" : "Laden…", inner, juce::Justification::centredLeft);
            return;
        }
        if (item < 0 || item >= (int) b->items.size()) {
            g.drawText("LES " + juce::String(b->unit) + " - DRUK OP PLAY OF EEN TOETS", inner.removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(juce::Colours::white); g.setFont(sans(40, true));
            g.drawText("spreekbuis", inner, juce::Justification::centredLeft);
            return;
        }
        auto& it = b->items[(size_t) item];
        juce::String kind = it.kind == "w" || it.kind == "w0" ? "WOORD" : it.kind == "s" ? "ZIN" : "GESPREK";
        g.drawText(kind + " - LES " + juce::String(it.unit), inner.removeFromTop(16), juce::Justification::centredLeft);
        // words, highlighted as they are spoken
        float size = it.nl.length() > 40 ? 30.0f : it.nl.length() > 22 ? 38.0f : 48.0f;
        auto f = sans(size, true);
        float x = (float) inner.getX(), y = (float) inner.getY() + 4;
        juce::GlyphArrangement ga;
        for (int i = 0; i < it.words.size(); ++i) {
            auto w = it.words[i];
            float ww = juce::GlyphArrangement::getStringWidth(f, w + " ");
            bool lit = i == word;
            if (lit) { g.setColour(orange); g.fillRoundedRectangle(x - 4, y, juce::GlyphArrangement::getStringWidth(f, w) + 8, size * 1.18f, 5); }
            g.setColour(lit ? juce::Colours::white : (word >= 0 && i < word ? juce::Colours::white.withAlpha(0.6f) : juce::Colours::white));
            g.setFont(f);
            g.drawText(w, juce::Rectangle<float>(x, y, ww + 10, size * 1.2f), juce::Justification::centredLeft);
            x += ww;
        }
        g.setColour(juce::Colours::white.withAlpha(0.7f)); g.setFont(sans(17));
        g.drawText(it.en, juce::Rectangle<float>((float) inner.getX(), y + size * 1.3f, (float) inner.getWidth(), 24), juce::Justification::centredLeft);
    }
};

// the Dutch vowel chart as an XY pad
struct VowelPad : juce::Component {
    SpreekbuisProcessor& p;
    explicit VowelPad(SpreekbuisProcessor& pr) : p(pr) {}
    juce::Point<float> toPx(float x, float y) const {
        auto r = getLocalBounds().toFloat().reduced(16);
        float lx = r.getX() + r.getWidth() * (0.35f * y + x * (1 - 0.35f * y));
        return { lx, r.getY() + y * r.getHeight() };
    }
    std::pair<float, float> fromPx(juce::Point<float> pt) const {
        auto r = getLocalBounds().toFloat().reduced(16);
        float y = juce::jlimit(0.0f, 1.0f, (pt.y - r.getY()) / r.getHeight());
        float x = juce::jlimit(0.0f, 1.0f, ((pt.x - r.getX()) / r.getWidth() - 0.35f * y) / (1 - 0.35f * y));
        return { x, y };
    }
    void paint(juce::Graphics& g) override {
        g.setColour(card); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        juce::Path tr; tr.startNewSubPath(toPx(0, 0)); tr.lineTo(toPx(1, 0)); tr.lineTo(toPx(1, 1)); tr.lineTo(toPx(0, 1)); tr.closeSubPath();
        g.setColour(soft); g.strokePath(tr, juce::PathStrokeType(1.5f));
        int glide = (int) *p.apvts.getRawParameterValue("vowel_glide");
        if (glide > 0) {
            auto& gl = sb::glides()[(size_t) glide - 1];
            g.setColour(orange.withAlpha(0.6f));
            juce::Path a; a.addArrow({ toPx(gl.x0, gl.y0), toPx(gl.x1, gl.y1) }, 2.5f, 10, 10);
            g.fillPath(a);
        }
        g.setFont(sans(16, true));
        for (auto& v : sb::vowelChart()) {
            auto c = toPx(v.x, v.y);
            g.setColour(ink); g.drawText(juce::String::fromUTF8(v.ipa), juce::Rectangle<float>(c.x - 12, c.y - 11, 24, 22), juce::Justification::centred);
        }
        auto dot = toPx(p.padX.load(), p.padY.load());
        g.setColour(orange); g.fillEllipse(dot.x - 7, dot.y - 7, 14, 14);
        g.setColour(ink); g.drawEllipse(dot.x - 7, dot.y - 7, 14, 14, 2);
        g.setColour(mute); g.setFont(mono(9.5f));
        g.drawText("VOOR", getLocalBounds().reduced(6).removeFromBottom(12), juce::Justification::centredLeft);
        g.drawText("ACHTER", getLocalBounds().reduced(6).removeFromBottom(12), juce::Justification::centredRight);
    }
    void set(const juce::MouseEvent& e) {
        auto [x, y] = fromPx(e.position);
        if (auto* px = p.apvts.getParameter("vowel_x")) px->setValueNotifyingHost(x);
        if (auto* py = p.apvts.getParameter("vowel_y")) py->setValueNotifyingHost(y);
        p.padX = x; p.padY = y;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        p.apvts.getParameter("vowel_x")->beginChangeGesture(); p.apvts.getParameter("vowel_y")->beginChangeGesture(); set(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override { set(e); }
    void mouseUp(const juce::MouseEvent&) override { p.apvts.getParameter("vowel_x")->endChangeGesture(); p.apvts.getParameter("vowel_y")->endChangeGesture(); }
};

// ---------------------------------------------------------------- content (laid out at 1180x700, scaled)
struct SpreekbuisEditor::Content : juce::Component {
    SpreekbuisProcessor& p;
    Karaoke karaoke;
    VowelPad pad;
    juce::ComboBox presets;
    Toggle allItems, loopOn, loopWhisper, loopMidi, cloudOn, vowelOn, gOn, orcaOn;
    juce::TextButton reload { "Herlaad" };
    juce::TextEditor port;
    juce::Label status, orcaStatus, help;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::map<juce::String, juce::Component*> byId;

    Knob* knob(const char* id, const char* label) { knobs.push_back(std::make_unique<Knob>(p, id, label)); addAndMakeVisible(*knobs.back()); byId[id] = knobs.back().get(); return knobs.back().get(); }
    Choice* choice(const char* id, const char* label) { choices.push_back(std::make_unique<Choice>(p, id, label)); addAndMakeVisible(*choices.back()); byId[id] = choices.back().get(); return choices.back().get(); }

    explicit Content(SpreekbuisProcessor& pr)
        : p(pr), karaoke(pr), pad(pr),
          allItems(pr, "all_items", "Alle items"), loopOn(pr, "loop_on", "LOOP"), loopWhisper(pr, "loop_whisper", "Fluister vertaling"),
          loopMidi(pr, "loop_midi", "MIDI uit"), cloudOn(pr, "cloud_on", "WOLK"), vowelOn(pr, "vowel_on", "KLINKER"), gOn(pr, "g_on", "G"),
          orcaOn(pr, "orca_on", "ORCA") {
        for (auto* c : std::initializer_list<juce::Component*> { &karaoke, &pad, &presets, &allItems, &loopOn, &loopWhisper, &loopMidi, &cloudOn, &vowelOn, &gOn, &orcaOn, &reload, &port, &status, &orcaStatus, &help })
            addAndMakeVisible(*c);
        for (int i = 0; i < p.getNumPrograms(); ++i) presets.addItem(p.getProgramName(i), i + 1);
        presets.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
        presets.onChange = [this] { p.setCurrentProgram(presets.getSelectedId() - 1); };
        reload.onClick = [this] { p.requestReload(); };
        port.setColour(juce::TextEditor::textColourId, ink);
        port.setColour(juce::TextEditor::backgroundColourId, card);
        port.setColour(juce::TextEditor::outlineColourId, ink);
        port.setFont(mono(13));
        port.setInputRestrictions(5, "0123456789");
        port.setText(juce::String(p.orcaPort()), false);
        port.onReturnKey = [this] { p.setOrcaPort(port.getText().getIntValue()); };
        port.onFocusLost = [this] { p.setOrcaPort(port.getText().getIntValue()); };
        for (auto* l : { &status, &orcaStatus, &help }) { l->setFont(mono(11)); l->setColour(juce::Label::textColourId, mute); }
        help.setText("orca:  ;fiets  ;g  ;g7  ;vui  ;veu  ;1a      re-render voices: ~/spreekbuis/tools/render.sh", juce::dontSendNotification);

        // loop
        choice("loop_unit", "Les"); choice("loop_set", "Speelt"); choice("loop_bars", "Roep (maten)"); choice("loop_gap", "Antwoord (maten)");
        choice("loop_order", "Volgorde");
        knob("loop_level", "Niveau"); knob("loop_pitch", "Toon"); knob("loop_bpm", "Vrije BPM"); knob("loop_note", "MIDI noot");
        // cloud
        knob("cloud_level", "Niveau"); knob("cloud_density", "Dichtheid"); knob("cloud_size", "Korrel ms"); knob("cloud_scatter", "Spreiding");
        knob("cloud_pos", "Positie"); knob("cloud_pitch", "Toon"); knob("cloud_spread", "Stereo"); knob("cloud_reverse", "Achteruit");
        knob("cloud_whisper", "Vertaling"); knob("whisper_level", "Fluister");
        // vowel
        choice("vowel_glide", "Tweeklank"); choice("vowel_src", "Bron");
        knob("vowel_level", "Niveau"); knob("vowel_rate", "Snelheid"); knob("vowel_noise", "Adem"); knob("vowel_q", "Resonantie");
        knob("vowel_shift", "Formant"); knob("vowel_attack", "Aanzet"); knob("vowel_release", "Uitklank");
        // g
        knob("g_level", "Niveau"); knob("g_soft", "NL - BE"); knob("g_rasp", "Rasp"); knob("g_voice", "Stem");
        knob("g_harm", "Resonantie x"); knob("g_release", "Uitklank");
        // out
        choice("keys", "Toetsen"); knob("master", "Master"); knob("space", "Ruimte");
        refresh();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(paper);
        g.setColour(ink); g.setFont(sans(26, true)); g.drawText("spreekbuis", 22, 12, 200, 34, juce::Justification::centredLeft);
        g.setColour(mute); g.setFont(sans(12)); g.drawText("Nederlands als instrument", 160, 16, 220, 30, juce::Justification::centredLeft);
        for (auto& s : sections) {
            g.setColour(card); g.fillRoundedRectangle(s.r.toFloat(), 8);
            g.setColour(ink); g.drawRoundedRectangle(s.r.toFloat().reduced(1), 8, 2);
            g.setColour(mute); g.setFont(mono(10.5f));
            g.drawText(s.sub, s.r.reduced(14, 10).removeFromTop(16).withTrimmedLeft(56), juce::Justification::centredRight);
        }
    }
    struct Sec { juce::Rectangle<int> r; juce::String sub; };
    std::vector<Sec> sections;

    void place(const char* id, juce::Rectangle<int> r) { if (byId.count(id)) byId[id]->setBounds(r); }

    void resized() override {
        presets.setBounds(W - 520, 16, 200, 28);
        allItems.setBounds(W - 310, 16, 96, 28);
        reload.setBounds(W - 206, 16, 80, 28);
        status.setBounds(380, 16, W - 900, 28);
        karaoke.setBounds(18, 56, W - 36, 150);

        sections.clear();
        const int top = 220, h = H - top - 46, gap = 12;
        int x = 18;
        auto sec = [&](int w, juce::String sub) { juce::Rectangle<int> r(x, top, w, h); sections.push_back({ r, sub }); x += w + gap; return r.reduced(14, 10); };
        const int k = 62; // knob cell
        // LOOP
        auto r = sec(216, "les als loop");
        loopOn.setBounds(r.getX(), r.getY(), 80, 24);
        auto row = r.withTrimmedTop(34);
        place("loop_unit", row.removeFromTop(40)); row.removeFromTop(4);
        { auto rr = row.removeFromTop(40); place("loop_set", rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); place("loop_order", rr); }
        row.removeFromTop(4);
        { auto rr = row.removeFromTop(40); place("loop_bars", rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); place("loop_gap", rr); }
        row.removeFromTop(8);
        { auto rr = row.removeFromTop(24); loopWhisper.setBounds(rr.removeFromLeft(118)); rr.removeFromLeft(6); loopMidi.setBounds(rr); }
        row.removeFromTop(8);
        { auto rr = row.removeFromTop(k + 14); for (auto id : { "loop_level", "loop_pitch", "loop_bpm" }) place(id, rr.removeFromLeft(k + 2)); }
        { auto rr = row.removeFromTop(k + 14); place("loop_note", rr.removeFromLeft(k + 3)); }
        // CLOUD
        r = sec(284, "woordwolk");
        cloudOn.setBounds(r.getX(), r.getY(), 80, 24);
        row = r.withTrimmedTop(34);
        const char* cl[] = { "cloud_level", "cloud_density", "cloud_size", "cloud_scatter", "cloud_pos", "cloud_pitch", "cloud_spread", "cloud_reverse", "cloud_whisper", "whisper_level" };
        for (int i = 0; i < 10; ++i) place(cl[i], juce::Rectangle<int>(row.getX() + (i % 4) * 64, row.getY() + (i / 4) * 92, k, k + 18));
        // VOWEL
        r = sec(284, "klinkerkaart");
        vowelOn.setBounds(r.getX(), r.getY(), 90, 24);
        row = r.withTrimmedTop(32);
        pad.setBounds(row.removeFromTop(170));
        row.removeFromTop(6);
        { auto rr = row.removeFromTop(40); place("vowel_glide", rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); place("vowel_src", rr); }
        row.removeFromTop(4);
        const char* vl[] = { "vowel_level", "vowel_rate", "vowel_noise", "vowel_q", "vowel_shift", "vowel_attack", "vowel_release" };
        for (int i = 0; i < 7; ++i) place(vl[i], juce::Rectangle<int>(row.getX() + (i % 4) * 64, row.getY() + (i / 4) * 84, k, k + 14));
        // G
        r = sec(150, "g / ch");
        gOn.setBounds(r.getX(), r.getY(), 50, 24);
        row = r.withTrimmedTop(34);
        const char* gl[] = { "g_level", "g_soft", "g_rasp", "g_voice", "g_harm", "g_release" };
        for (int i = 0; i < 6; ++i) place(gl[i], juce::Rectangle<int>(row.getX() + (i % 2) * 62, row.getY() + (i / 2) * 92, k, k + 18));
        // ORCA + OUT
        r = sec(W - x - 18, "orca + uit");
        orcaOn.setBounds(r.getX(), r.getY(), 70, 24);
        row = r.withTrimmedTop(34);
        { auto rr = row.removeFromTop(26); port.setBounds(rr.removeFromLeft(80)); }
        orcaStatus.setBounds(row.removeFromTop(36));
        row.removeFromTop(6);
        place("keys", row.removeFromTop(40));
        row.removeFromTop(8);
        { auto rr = row.removeFromTop(k + 18); place("master", rr.removeFromLeft(k + 2)); place("space", rr.removeFromLeft(k + 2)); }
        help.setBounds(18, H - 40, W - 36, 30);
    }

    void refresh() {
        auto b = p.uiBank();
        status.setText(b ? b->status : juce::String("loading word bank…"), juce::dontSendNotification);
        orcaStatus.setText(p.orcaListening ? "udp :" + juce::String(p.orcaPort()) + "\n" + juce::String(p.orcaPackets.load()) + " berichten" : "niet verbonden\n(poort bezet?)",
                           juce::dontSendNotification);
        karaoke.repaint(); pad.repaint();
    }
};

// ---------------------------------------------------------------- editor
SpreekbuisEditor::SpreekbuisEditor(SpreekbuisProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&look);
    content = std::make_unique<Content>(p);
    addAndMakeVisible(*content);
    content->setBounds(0, 0, W, H);
    setResizable(true, true);
    setResizeLimits(W * 6 / 10, H * 6 / 10, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize((int) p.state.getProperty("uiW", W), (int) p.state.getProperty("uiH", H));
    startTimerHz(30);
}
SpreekbuisEditor::~SpreekbuisEditor() { setLookAndFeel(nullptr); }
void SpreekbuisEditor::paint(juce::Graphics& g) { g.fillAll(paper); }
void SpreekbuisEditor::resized() {
    content->setTransform(juce::AffineTransform::scale((float) getWidth() / W));
    proc.state.setProperty("uiW", getWidth(), nullptr);
    proc.state.setProperty("uiH", getHeight(), nullptr);
}
void SpreekbuisEditor::timerCallback() { content->refresh(); }
