#include "Editor.h"

using namespace sbui;
using juce::Colour;
using juce::Rectangle;

namespace {
double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
juce::RangedAudioParameter& prm(SpreekbuisProcessor& p, const juce::String& id) {
    auto* r = p.apvts.getParameter(id);
    jassert(r != nullptr);
    return *r;
}
juce::String noteName(int n) {
    static const char* names[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    return juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2);
}

// ---------------------------------------------------------------- a titled column of controls (faltung's Stage)
class Stage : public juce::Component {
public:
    Stage(juce::String t, Theme th) : title(std::move(t)), theme(th) {}
    void add(juce::Component* c, int h = 18, bool own = true) { if (own) owned.add(c); items.push_back(c); heights.push_back(h); addAndMakeVisible(c); }
    void addGap(int h = 7) { items.push_back(nullptr); heights.push_back(h); }
    int wanted() const { int h = 24; for (size_t i = 0; i < heights.size(); ++i) h += heights[i] + 1; return h; }
    void resized() override {
        int y = 24;
        for (size_t i = 0; i < items.size(); ++i) {
            if (auto* c = items[i]) c->setBounds(0, y, getWidth(), heights[i]);
            y += heights[i] + 1;
        }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(theme.ink); g.setFont(mono(11.0f, true));
        g.drawText(title, 0, 0, getWidth(), 16, juce::Justification::centredLeft);
        g.setColour(theme.fill.withAlpha(0.6f)); g.fillRect(0, 18, getWidth(), 1);
    }
    void setTitle(juce::String t) { title = std::move(t); repaint(); }
private:
    std::vector<int> heights;
    std::vector<juce::Component*> items;
    juce::OwnedArray<juce::Component> owned;
    juce::String title; Theme theme;
};

// lines of quiet mono text that say how things stand
class Info : public juce::Component {
public:
    Info(std::function<juce::String()> f, Colour c = mute) : get(std::move(f)), col(c) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        g.setColour(col); g.setFont(mono(10.0f));
        g.drawFittedText(get(), getLocalBounds(), juce::Justification::topLeft, std::max(1, getHeight() / 12), 1.0f);
    }
    std::function<juce::String()> get; Colour col;
};

// ---------------------------------------------------------------- the Dutch vowel chart as an XY pad
class VowelPad : public juce::Component {
public:
    explicit VowelPad(SpreekbuisProcessor& pr) : p(pr) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    juce::Point<float> toPx(float x, float y) const {
        auto r = getLocalBounds().toFloat().reduced(22, 20);
        float lx = r.getX() + r.getWidth() * (0.35f * y + x * (1 - 0.35f * y));
        return { lx, r.getY() + y * r.getHeight() };
    }
    std::pair<float, float> fromPx(juce::Point<float> pt) const {
        auto r = getLocalBounds().toFloat().reduced(22, 20);
        float y = juce::jlimit(0.0f, 1.0f, (pt.y - r.getY()) / r.getHeight());
        float x = juce::jlimit(0.0f, 1.0f, ((pt.x - r.getX()) / r.getWidth() - 0.35f * y) / (1 - 0.35f * y));
        return { x, y };
    }
    void paint(juce::Graphics& g) override {
        // the chart: a thin-line quadrilateral (front - back, close - open), a rule through each height
        juce::Path tr; tr.startNewSubPath(toPx(0, 0)); tr.lineTo(toPx(1, 0)); tr.lineTo(toPx(1, 1)); tr.lineTo(toPx(0, 1)); tr.closeSubPath();
        g.setColour(card); g.fillPath(tr);
        g.setColour(orange.withAlpha(0.85f)); g.strokePath(tr, juce::PathStrokeType(1.0f));
        g.setColour(orange.withAlpha(0.35f));
        for (float y : { 0.25f, 0.38f, 0.52f, 0.66f }) g.drawLine({ toPx(0, y), toPx(1, y) }, 1.0f);
        g.drawLine({ toPx(0.5f, 0), toPx(0.5f, 1) }, 1.0f);
        const int glide = (int) *p.apvts.getRawParameterValue("vowel_glide");
        if (glide > 0) {
            auto& gl = sb::glides()[(size_t) glide - 1];
            g.setColour(juce::Colours::white);
            juce::Path a; a.addArrow({ toPx(gl.x0, gl.y0), toPx(gl.x1, gl.y1) }, 1.5f, 9, 9);
            g.fillPath(a);
        }
        g.setFont(sans(16.0f, true));
        for (auto& v : sb::vowelChart()) {
            auto c = toPx(v.x, v.y);
            g.setColour(ink); g.drawText(juce::String::fromUTF8(v.ipa), Rectangle<float>(c.x - 12, c.y - 11, 24, 22), juce::Justification::centred);
        }
        // where the chart is: a lit parallelogram
        auto dot = toPx(p.padX.load(), p.padY.load());
        auto mark = skewRect({ dot.x - 9, dot.y - 6, 16, 12 }, 0.4f);
        g.setColour(orange); g.fillPath(mark);
        g.setColour(juce::Colours::white); g.strokePath(mark, juce::PathStrokeType(2.0f));
        g.setColour(mute); g.setFont(mono(10.0f));
        g.drawText("voor", getLocalBounds().removeFromBottom(14), juce::Justification::centredLeft);
        g.drawText("achter", getLocalBounds().removeFromBottom(14), juce::Justification::centredRight);
        g.drawText("gesloten", getLocalBounds().removeFromTop(14), juce::Justification::centredRight);
        g.drawText("open", getLocalBounds().removeFromBottom(14).withTrimmedRight(60), juce::Justification::centredRight);
        // why it might be quiet
        juce::String hint;
        const int src = (int) *p.apvts.getRawParameterValue("vowel_src");
        const bool on = *p.apvts.getRawParameterValue("vowel_on") > 0.5f;
        const bool loopOn = *p.apvts.getRawParameterValue("loop_on") > 0.5f, cloudOn = *p.apvts.getRawParameterValue("cloud_on") > 0.5f;
        const bool spraakOn = *p.apvts.getRawParameterValue("spraak_on") > 0.5f;
        if (src == 2 && p.inputLevel.load() < 1.0e-4f) hint = "bron ingang: er komt geen audio binnen";
        else if (src == 4 && !loopOn) hint = "bron loop: zet loop aan";
        else if (src == 5 && !cloudOn) hint = "bron wolk: zet wolk aan";
        else if (src == 6 && !loopOn && !cloudOn) hint = "bron loop + wolk: zet ze aan";
        else if (src == 7 && !spraakOn) hint = "bron spraak: zet spraak aan";
        else if (src == 8 && !loopOn && !cloudOn && !spraakOn) hint = "bron alles: zet een laag aan";
        else if (src >= 4 && on) hint = "";
        else if (on && !p.notesReachVowel()) hint = "noten: zet toetsen op klinker";
        else if (!on) hint = "klik of sleep om te horen";
        if (hint.isNotEmpty()) {
            g.setColour(on || src == 2 ? orange : mute); g.setFont(mono(10.0f));
            g.drawText(hint, getLocalBounds().removeFromTop(14), juce::Justification::centredLeft);
        }
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
        p.padHeld = true;   // pressing the chart plays the vowel
    }
    void mouseDrag(const juce::MouseEvent& e) override { set(e); }
    void mouseUp(const juce::MouseEvent&) override {
        p.padHeld = false;
        p.apvts.getParameter("vowel_x")->endChangeGesture(); p.apvts.getParameter("vowel_y")->endChangeGesture();
    }
private:
    SpreekbuisProcessor& p;
};

// ---------------------------------------------------------------- an LFO drawn as it runs (faltung's Trace)
class Trace : public juce::Component {
public:
    Trace(std::function<float()> f, Colour c) : get(std::move(f)), col(c) {}
    void tick() { hist[(size_t) pos] = get(); pos = (pos + 1) % kN; repaint(); }
    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        g.setColour(ink.withAlpha(0.06f)); g.fillRect(b);
        juce::Path path;
        for (int i = 0; i < kN; ++i) {
            const float v = hist[(size_t) ((pos + i) % kN)] * 0.5f + 0.5f;
            const float x = b.getX() + b.getWidth() * (float) i / (float) (kN - 1), y = b.getBottom() - 2 - v * (b.getHeight() - 4);
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        g.setColour(col); g.strokePath(path, juce::PathStrokeType(1.4f));
    }
private:
    static constexpr int kN = 160;
    std::function<float()> get; Colour col;
    std::array<float, kN> hist {}; int pos = 0;
};

// ---------------------------------------------------------------- the keys (faltung's parallelogram keyboard)
class Keys : public juce::Component {
public:
    explicit Keys(SpreekbuisProcessor& proc) : p(proc) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    int base = 36;   // C1
    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        g.setColour(ink); g.setFont(mono(11.0f));
        g.drawText("<", b.withWidth(20.0f), juce::Justification::centred);
        g.drawText(">", b.withLeft(b.getRight() - 20.0f), juce::Justification::centred);
        const float w = (b.getWidth() - 48.0f) / 48.0f;
        for (int k = 0; k < 48; ++k) {
            const int nn = base + k;
            const bool black = juce::MidiMessage::isMidiNoteBlack(nn);
            auto r = Rectangle<float>(24.0f + k * w, black ? 2.0f : 8.0f, w - 3.0f, b.getHeight() - (black ? 14.0f : 10.0f));
            auto shape = skewRect(r, 0.25f);
            const bool on = p.keyState.isNoteOnForChannels(0xffff, nn);
            g.setColour(on ? juce::Colours::white : black ? mute.withAlpha(0.55f) : orange.withAlpha(0.85f));
            g.fillPath(shape);
            if (nn % 12 == 0) {
                g.setColour(dark); g.setFont(mono(9.0f));
                g.drawText("c" + juce::String(nn / 12 - 2), r.withTrimmedTop(r.getHeight() - 12.0f), juce::Justification::centred);
            }
        }
    }
    int noteAt(juce::Point<float> pt) const {
        const float w = ((float) getWidth() - 48.0f) / 48.0f;
        const int k = (int) ((pt.x - 24.0f) / w);
        return k >= 0 && k < 48 ? base + k : -1;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.x < 22) { base = std::max(0, base - 12); repaint(); return; }
        if (e.x > getWidth() - 22) { base = std::min(72, base + 12); repaint(); return; }
        down = noteAt(e.position);
        if (down >= 0) p.keyState.noteOn(1, down, 0.6f + 0.4f * (float) e.y / (float) getHeight());
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const int n = noteAt(e.position);
        if (n >= 0 && n != down) {
            const int old = down; down = n;
            p.keyState.noteOn(1, n, 0.85f);
            if (old >= 0) p.keyState.noteOff(1, old, 0.0f);
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent&) override { if (down >= 0) p.keyState.noteOff(1, down, 0.0f); down = -1; repaint(); }
private:
    SpreekbuisProcessor& p; int down = -1;
};

// ---------------------------------------------------------------- one layer of the mixer: its switch and its level
class MixRow : public juce::Component {
public:
    MixRow(SpreekbuisProcessor& p, const char* onId, const char* levelId, const juce::String& label, Theme t)
        : level(prm(p, levelId), t, label) {
        if (onId) { sw = std::make_unique<ToggleBox>(prm(p, onId), t, " "); addAndMakeVisible(*sw); }
        level.setLabelWidth(60);
        addAndMakeVisible(level);
    }
    void resized() override { if (sw) sw->setBounds(0, 0, 26, getHeight()); level.setBounds(30, 0, getWidth() - 30, getHeight()); }
    std::unique_ptr<ToggleBox> sw;
    ParamSlider level;
};

// ---------------------------------------------------------------- one slot of the matrix: source > destination
class MatrixRow : public juce::Component {
public:
    MatrixRow(SpreekbuisProcessor& p, int k, Theme t) : index(k), src(prm(p, "cv" + juce::String(k) + "src"), t, " "), dst(prm(p, "cv" + juce::String(k) + "dst"), t, " ") {
        src.labelW = 0; dst.labelW = 0;
        addAndMakeVisible(src); addAndMakeVisible(dst);
    }
    void resized() override {
        const int w = getWidth() - 18;
        src.setBounds(18, 0, w / 2 - 10, getHeight()); dst.setBounds(18 + w / 2 + 8, 0, w / 2 - 8, getHeight());
    }
    void paint(juce::Graphics& g) override {
        g.setColour(ink.withAlpha(0.6f)); g.setFont(mono(10.0f));
        g.drawText(juce::String(index), 0, 0, 14, getHeight(), juce::Justification::centredLeft);
        g.drawText(">", 18 + (getWidth() - 18) / 2 - 8, 0, 14, getHeight(), juce::Justification::centred);
    }
    int index; ChoiceBox src, dst;
};

// ---------------------------------------------------------------- presets: < name >, and a menu with everything
class PresetBox : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit PresetBox(SpreekbuisProcessor& proc) : p(proc) { setMouseCursor(juce::MouseCursor::PointingHandCursor); setTooltip("presets: fabriek, eigen, Spraakmaker 1.0, bewaren, importeren"); }
    juce::String name() const { return p.presetLabel().isNotEmpty() ? p.presetLabel() : p.getProgramName(p.getCurrentProgram()); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(ink);
        g.setFont(mono(11.0f));
        g.drawText("<", b.removeFromLeft(16.0f), juce::Justification::centred);
        g.drawText(">", b.removeFromRight(16.0f), juce::Justification::centred);
        g.setFont(mono(11.0f, true));
        g.drawText(name().toLowerCase(), b, juce::Justification::centred, true);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int n = p.getNumPrograms(), cur = p.getCurrentProgram();
        if (e.x < 18) { p.loadProgram((cur - 1 + n) % n); return; }
        if (e.x > getWidth() - 18) { p.loadProgram((cur + 1) % n); return; }
        juce::PopupMenu m, fab, sp, both, own, old;
        for (int i = 0; i < n; ++i) {
            auto& dst = i == 0 ? m : i <= 9 ? fab : i <= 29 ? sp : both;
            dst.addItem(i + 1, p.getProgramName(i).toLowerCase(), true, i == cur && p.presetLabel().isEmpty());
        }
        m.addSubMenu("woordenbank (spreekbuis 1.0)", fab);
        m.addSubMenu("spraakcomputer (spraakmaker 1.0)", sp);
        m.addSubMenu("samen", both);
        files.clear();
        for (auto& f : p.presetDir().findChildFiles(juce::File::findFiles, false, "*.xml")) { files.add(f); own.addItem(1000 + files.size(), f.getFileNameWithoutExtension().toLowerCase()); }
        if (own.getNumItems() > 0) m.addSubMenu("eigen presets", own);
        for (auto& f : sb::legacySpraakmakerDir().getChildFile("presets").findChildFiles(juce::File::findFiles, false, "p*.xml")) {
            juce::String nm = f.getFileNameWithoutExtension();
            if (auto x = juce::XmlDocument::parse(f)) nm << " " << x->getStringAttribute("presetName");
            files.add(f); old.addItem(1000 + files.size(), nm.toLowerCase());
        }
        if (old.getNumItems() > 0) m.addSubMenu("opgeslagen in spraakmaker 1.0", old);
        m.addSeparator();
        m.addItem(900, juce::String::fromUTF8("bewaar als\xe2\x80\xa6"));
        m.addItem(901, juce::String::fromUTF8("open of importeer (ook spraakmaker 1.0)\xe2\x80\xa6"));
        juce::Component::SafePointer<PresetBox> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
            if (!safe || r <= 0) return;
            if (r == 900) safe->save();
            else if (r == 901) safe->open();
            else if (r > 1000) safe->p.loadPresetFile(safe->files[r - 1001]);
            else safe->p.loadProgram(r - 1);
        });
    }
    void save() {
        chooser = std::make_unique<juce::FileChooser>("Bewaar preset", p.presetDir().getChildFile(name().isEmpty() ? "preset" : name()).withFileExtension("xml"), "*.xml");
        p.presetDir().createDirectory();
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f != juce::File()) p.savePresetFile(f.withFileExtension("xml"), f.getFileNameWithoutExtension());
        });
    }
    void open() {
        chooser = std::make_unique<juce::FileChooser>("Open een preset (Spreekbuis, of iets dat Spraakmaker 1.0 bewaarde)", p.presetDir(), "*.xml;*.vstpreset;*.aupreset;*.fxp;*.fxb;*");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f.existsAsFile() && !p.loadPresetFile(f)) juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Spreekbuis", "Daar zit geen Spreekbuis- of Spraakmaker-preset in.");
        });
    }
private:
    SpreekbuisProcessor& p;
    juce::Array<juce::File> files;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

// ---------------------------------------------------------------- menus, text fields and dialogs in the same hand
struct SpreekbuisEditor::Look : juce::LookAndFeel_V4 {
    Look() {
        setColour(juce::PopupMenu::backgroundColourId, card);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, orange);
        setColour(juce::PopupMenu::highlightedTextColourId, dark);
        setColour(juce::PopupMenu::headerTextColourId, mute);
        setColour(juce::TextEditor::backgroundColourId, card);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, soft);
        setColour(juce::TextEditor::focusedOutlineColourId, orange);
        setColour(juce::TextEditor::highlightColourId, orange.withAlpha(0.4f));
        setColour(juce::TextEditor::highlightedTextColourId, ink);
        setColour(juce::CaretComponent::caretColourId, orange);
        setColour(juce::ResizableWindow::backgroundColourId, paper);
        setColour(juce::TooltipWindow::backgroundColourId, card);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, soft);
        setColour(juce::TextButton::buttonColourId, card);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::ScrollBar::thumbColourId, soft);
    }
    juce::Font getPopupMenuFont() override { return mono(12.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return mono(12.0f); }
    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override { g.fillAll(card); g.setColour(soft); g.drawRect(0, 0, w, h, 1); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(4.0f, 3.0f);
        g.setColour(down ? orange : over ? hover : card);
        g.fillPath(skewRect(r.withTrimmedRight(r.getHeight() * 0.35f), 0.35f));
    }
    juce::Rectangle<int> getTooltipBounds(const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area) override {
        const int w = (int) juce::GlyphArrangement::getStringWidth(mono(11.0f), tip) + 16;
        return juce::Rectangle<int>(pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 14, pos.y > area.getCentreY() ? pos.y - 30 : pos.y + 18, w, 22).constrainedWithin(area);
    }
    void drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) override {
        g.fillAll(card); g.setColour(soft); g.drawRect(0, 0, w, h, 1);
        g.setColour(ink); g.setFont(mono(11.0f)); g.drawText(text, 8, 0, w - 12, h, juce::Justification::centredLeft);
    }
};

// ---------------------------------------------------------------- content (laid out at 1100 x 780, scaled)
struct SpreekbuisEditor::Content : juce::Component {
    SpreekbuisProcessor& p;
    Theme th = spreekTheme();
    Art art;
    PresetBox presets;
    ChoiceBox stem;
    Keys keys;
    Stage mixer { "mengpaneel", spreekTheme() };
    struct Page { juce::OwnedArray<Stage> stages; std::vector<int> column; juce::Component body; };
    juce::OwnedArray<Page> pages;
    int page = 0;
    std::vector<MixRow*> mixRows;
    std::vector<Trace*> traces;
    struct Ghost { ParamSlider* s; int dest; float scale; };
    std::vector<Ghost> ghosts;
    std::map<juce::String, ParamSlider*> sliders;
    juce::TextEditor say, port;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String flashMsg; double flashUntil = 0;

    Stage* stage(Page& pg, int col, const juce::String& title) {
        auto* s = pg.stages.add(new Stage(title, th));
        pg.column.push_back(col);
        pg.body.addAndMakeVisible(s);
        return s;
    }
    juce::Component* ctl(Stage& s, const char* id, const juce::String& label, bool tabs = false) {
        auto& r = prm(p, id);
        juce::Component* c;
        if (dynamic_cast<juce::AudioParameterChoice*>(&r)) { auto* cb = new ChoiceBox(r, th, label); cb->asTabs = tabs; c = cb; }
        else if (dynamic_cast<juce::AudioParameterBool*>(&r)) c = new ToggleBox(r, th, label);
        else {
            auto* sl = new ParamSlider(r, th, label);
            sliders[id] = sl;
            const juce::String sid(id);
            auto* raw = p.apvts.getRawParameterValue(id);
            if (sid == "loop_unit") sl->valueText = [raw] { const int v = (int) raw->load(); return v == 0 ? juce::String("tongval") : "les " + juce::String(v); };
            else if (sid == "loop_note" || sid == "root") sl->valueText = [raw] { return noteName((int) raw->load()); };
            else if (sid == "chord") sl->valueText = [raw] { return sm::prm::kChord[juce::jlimit(0, 9, (int) raw->load() - 1)]; };
            else if (sid == "list") sl->valueText = [raw] { return juce::String((int) raw->load()).paddedLeft('0', 2); };
            else if (sid == "bits") sl->valueText = [raw] { return juce::String((int) raw->load()) + " bit"; };
            else if (sid == "down") sl->valueText = [raw] { return "1/" + juce::String((int) raw->load()); };
            c = sl;
        }
        s.add(c);
        return c;
    }
    TextLink* link(Stage& s, const juce::String& text, std::function<void()> f) {
        auto* l = new TextLink(text, th);
        l->onClick = std::move(f);
        s.add(l);
        return l;
    }
    void ghost(const char* id, int dest, float scale) { if (sliders.count(id)) ghosts.push_back({ sliders[id], dest, scale }); }

    explicit Content(SpreekbuisProcessor& pr)
        : p(pr), art(pr), presets(pr), stem(prm(pr, "voice"), spreekTheme(), "stem"), keys(pr) {
        addAndMakeVisible(art); addAndMakeVisible(presets); addAndMakeVisible(stem); addAndMakeVisible(keys); addAndMakeVisible(mixer);
        stem.asTabs = true;
        stem.setTooltip("de stem: de woorden, de klinkerkaart en de spraakcomputer (Voice)");
        art.onPage = [this](int pg) { setPage(pg); };

        // ---- the mixer: every layer, always there
        auto mix = [&](const char* on, const char* lvl, const char* label) { auto* r = new MixRow(p, on, lvl, label, th); mixRows.push_back(r); mixer.add(r); };
        mix("loop_on", "loop_level", "loop"); mix("cloud_on", "cloud_level", "wolk"); mix(nullptr, "whisper_level", "fluister");
        mix("vowel_on", "vowel_level", "klinker"); mix("g_on", "g_level", "g"); mix("spraak_on", "level", "spraak");
        mixer.addGap();
        ctl(mixer, "master", "master"); ctl(mixer, "space", "ruimte");
        mixer.addGap();
        ctl(mixer, "keys", "toetsen");
        mixer.add(new Info([this] {
            const int k = (int) *p.apvts.getRawParameterValue("keys");
            static const char* what[6] = { "noten spelen woorden", "noten spelen de klinkerkaart", "noten spelen de g", "noten spelen woorden en klinker",
                                           "noten spelen alle lagen", "noten spelen de spraakcomputer" };
            return juce::String(what[juce::jlimit(0, 5, k)]);
        }), 14);

        for (int i = 0; i < kPages; ++i) { auto* pg = pages.add(new Page()); addChildComponent(pg->body); }
        // ---- lp1 loop
        {
            auto& pg = *pages[0];
            auto* a = stage(pg, 0, "les");
            ctl(*a, "loop_unit", "les"); ctl(*a, "loop_set", "speelt"); ctl(*a, "loop_order", "volgorde"); ctl(*a, "all_items", "alle items");
            link(*a, "herlaad de woordenbank", [this] { p.requestReload(); });
            a->add(new Info([this] { auto b = p.uiBank(); return b ? b->status : juce::String::fromUTF8("woordenbank wordt geladen\xe2\x80\xa6"); }), 50);
            auto* b = stage(pg, 1, "roep en antwoord");
            ctl(*b, "loop_bars", "roep", true); ctl(*b, "loop_gap", "antwoord", true);
            ctl(*b, "loop_whisper", "fluister de vertaling"); ctl(*b, "loop_echo", "spraak antwoordt");
            b->add(new Info([] { return juce::String("in de antwoordmaten: de vertaling gefluisterd, en / of de spraakcomputer die het item herhaalt (taal en: de vertaling)"); }), 38);
            ctl(*b, "loop_pitch", "toon"); ctl(*b, "loop_bpm", "vrije bpm");
            auto* c = stage(pg, 2, "woorden naar buiten");
            ctl(*c, "loop_midi", "midi uit"); ctl(*c, "loop_note", "midi noot"); ctl(*c, "loop_trig", "spraak");
            c->add(new Info([] { return juce::String("elk woord van de loop: een midi-noot naar buiten, en een trigger (met of zonder toon) voor de spraakcomputer"); }), 38);
        }
        // ---- wk2 wolk
        {
            auto& pg = *pages[1];
            auto* a = stage(pg, 0, "korrels");
            ctl(*a, "cloud_density", "dichtheid"); ctl(*a, "cloud_size", "korrel"); ctl(*a, "cloud_scatter", "spreiding"); ctl(*a, "cloud_pos", "positie");
            auto* b = stage(pg, 1, "toon en ruimte");
            ctl(*b, "cloud_pitch", "toon"); ctl(*b, "cloud_spread", "stereo"); ctl(*b, "cloud_reverse", "achteruit");
            auto* c = stage(pg, 2, "vertaling");
            ctl(*c, "cloud_whisper", "vertaling"); ctl(*c, "whisper_level", "fluister");
            c->add(new Info([] { return juce::String("korrels uit de engelse vertaling gaan door de ruisvocoder: gefluister"); }), 26);
        }
        // ---- kl3 klinker
        {
            auto& pg = *pages[2];
            auto* a = stage(pg, 0, "klinkerkaart");
            a->add(new VowelPad(p), 300);
            auto* c = stage(pg, 2, "klank");
            ctl(*c, "vowel_glide", "tweeklank"); ctl(*c, "vowel_rate", "snelheid"); ctl(*c, "vowel_src", "bron");
            ctl(*c, "vowel_noise", "adem"); ctl(*c, "vowel_q", "resonantie"); ctl(*c, "vowel_shift", "formant");
            ctl(*c, "vowel_attack", "aanzet"); ctl(*c, "vowel_release", "uitklank");
            c->add(new Info([] { return juce::String("bron loop, wolk, spraak of alles: die laag klinkt door de kaart heen"); }), 26);
        }
        // ---- gg4 g
        {
            auto& pg = *pages[3];
            auto* a = stage(pg, 0, "g / ch");
            ctl(*a, "g_soft", "nl - be"); ctl(*a, "g_rasp", "rasp"); ctl(*a, "g_voice", "stem");
            auto* b = stage(pg, 1, "klank");
            ctl(*b, "g_harm", "resonantie"); ctl(*b, "g_release", "uitklank");
            auto* c = stage(pg, 2, " ");
            c->add(new Info([] { return juce::String::fromUTF8("de nederlandse g: van de harde hollandse /x/ naar de zachte vlaamse /\xc9\xa3/. toetsen op g of alles; orca: ;g ;g7"); }), 38);
        }
        // ---- sp5 spraak
        {
            auto& pg = *pages[4];
            auto* a = stage(pg, 0, "stem");
            ctl(*a, "pitch", "toonhoogte"); ctl(*a, "formant", "formant"); ctl(*a, "inton", "intonatie");
            ctl(*a, "bright", "helderheid"); ctl(*a, "speed", "tempo"); ctl(*a, "emotion", "emotie");
            a->addGap();
            say.setFont(mono(12.0f)); say.setTextToShowWhenEmpty("typ iets en druk op enter", mute);
            say.setInputRestrictions(120);
            say.onReturnKey = [this] { const auto t = say.getText().trim(); if (t.isNotEmpty()) { p.typeAndSpeak(t); flash("> " + t); } else p.manualTrigger(); say.clear(); };
            say.onEscapeKey = [this] { say.clear(); if (auto* e = findParentComponentOfClass<SpreekbuisEditor>()) e->grabKeyboardFocus(); };
            a->add(&say, 22, false);
            link(*a, "spreek (trigger)", [this] { p.manualTrigger(); });
            a->add(new Info([this] {
                if (now() < flashUntil) return flashMsg;
                const bool on = *p.apvts.getRawParameterValue("spraak_on") > 0.5f;
                const int trig = (int) *p.apvts.getRawParameterValue("trig");
                const bool fed = *p.apvts.getRawParameterValue("loop_trig") > 0.5f || *p.apvts.getRawParameterValue("loop_echo") > 0.5f;
                if (!on) return juce::String("de laag staat uit: enter en de trigger spreken toch");
                if (trig != 0 && !p.notesReachSpraak() && !fed) return juce::String("noten: zet toetsen op spraak of alles");
                if ((int) *p.apvts.getRawParameterValue("engine") == 1) return juce::String("ingang 1: draaggolf ") + (p.externalCarrier() ? "(signaal)" : "(eigen)") + " / ingang 2: modulator " + (p.externalModulator() ? "(signaal)" : "(-)");
                return juce::String::fromUTF8(p.display().text);
            }, orange), 26);
            auto* b = stage(pg, 1, juce::String::fromUTF8("trigger \xc2\xb7 bron"));
            ctl(*b, "trig", "trigger"); ctl(*b, "retrig", "hertrigger"); ctl(*b, "rate", "maat"); ctl(*b, "source", "bron");
            ctl(*b, "list", "lijst");
            link(*b, "lijst bewerken", [this] { editList(); });
            ctl(*b, "order", "volgorde"); ctl(*b, "start", "startpunt"); ctl(*b, "trgout", "trg uit");
            b->add(new Info([] { return juce::String("bron les: de woordenbank (wat de loop speelt). trg uit: midi 60 per woord, 62 per foneem"); }), 26);
            auto* c = stage(pg, 2, "motor");
            ctl(*c, "lang", "taal", true); ctl(*c, "engine", "motor"); ctl(*c, "poly", "stemmen"); ctl(*c, "chord", "akkoord"); ctl(*c, "fine", "fijn");
            ctl(*c, "micro", "stemming");
            link(*c, ".scl laden", [this] { loadScl(); });
            ctl(*c, "glide", "glijden"); ctl(*c, "root", "grondtoon"); ctl(*c, "bands", "banden"); ctl(*c, "carrier", "draaggolf"); ctl(*c, "vocmod", "modulator");
            c->add(new Info([this] { return p.sclName().isNotEmpty() ? "scala: " + p.sclName().toLowerCase() : juce::String("modulator loop / wolk: de stem van de bank door de vocoder"); }), 26);
        }
        // ---- fx6 fx
        {
            auto& pg = *pages[5];
            auto* a = stage(pg, 0, juce::String::fromUTF8("crush \xc2\xb7 breedte"));
            ctl(*a, "bits", "bits"); ctl(*a, "down", "downsample"); ctl(*a, "width", "breedte");
            auto* b = stage(pg, 1, juce::String::fromUTF8("echo \xc2\xb7 galm"));
            ctl(*b, "dlytime", "echo tijd"); ctl(*b, "dlyfb", "echo terug"); ctl(*b, "dlymix", "echo mix");
            b->addGap();
            ctl(*b, "revsize", "galm grootte"); ctl(*b, "revmix", "galm mix");
            auto* c = stage(pg, 2, "route");
            ctl(*c, "fx_src", "bewerkt", true); ctl(*c, "level", "niveau");
            c->add(new Info([] { return juce::String("spraak: alleen de spraakcomputer gaat door de effecten. alles: ook loop, wolk, fluister, klinker en g"); }), 38);
        }
        // ---- md7 mod
        {
            auto& pg = *pages[6];
            auto* a = stage(pg, 0, "lfo");
            for (int k = 1; k <= 4; ++k) {
                auto* t = new Trace([this, k] { return p.lfoShown[(size_t) k - 1].load(); }, k == 1 ? orange : k == 2 ? yellow : k == 3 ? blue : ink);
                traces.push_back(t); a->add(t, 20);
                const juce::String n(k);
                ctl(*a, ("lfo" + n + "rate").toRawUTF8(), "lfo " + n); ctl(*a, ("lfo" + n + "shape").toRawUTF8(), "vorm");
                a->addGap(5);
            }
            auto* b = stage(pg, 1, "matrix  (bron > doel)");
            for (int k = 1; k <= 4; ++k) {
                const juce::String n(k);
                b->add(new MatrixRow(p, k, th));
                ctl(*b, ("cv" + n + "amt").toRawUTF8(), "diepte"); ctl(*b, ("cv" + n).toRawUTF8(), "knop");
                b->addGap(8);
            }
            auto* c = stage(pg, 2, "macro");
            for (int k = 1; k <= 3; ++k) {
                const juce::String n(k);
                ctl(*c, ("m" + n).toRawUTF8(), "m" + n); ctl(*c, ("m" + n + "dst").toRawUTF8(), "doel");
                c->addGap(8);
            }
            c->add(new Info([] { return juce::String("bronnen: knop, lfo 1-4, aanslag, modwiel, toeval, en uit de bank: woord (elk woord), bank (hoe luid), spraak (hoe luid), klink x / y. doelen: de spraakcomputer, de effecten, de klinkerkaart, de wolk, de loop, de g, het gefluister"); }), 76);
        }
        // ---- oc8 orca
        {
            auto& pg = *pages[7];
            auto* a = stage(pg, 0, "orca (udp)");
            ctl(*a, "orca_on", "orca-brug");
            port.setFont(mono(12.0f)); port.setInputRestrictions(5, "0123456789");
            port.setText(juce::String(p.orcaPort()), false);
            port.onReturnKey = [this] { p.setOrcaPort(port.getText().getIntValue()); };
            port.onFocusLost = [this] { p.setOrcaPort(port.getText().getIntValue()); };
            a->add(&port, 22, false);
            a->add(new Info([this] { return p.orcaListening ? "luistert op 127.0.0.1:" + juce::String(p.orcaPort()) + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(p.orcaPackets.load()) + " berichten"
                                                            : juce::String("niet verbonden (poort bezet?)"); }, orange), 14);
            auto* b = stage(pg, 1, "berichten");
            b->add(new Info([] { return juce::String(";fiets      een woord uit de bank\n;1a         je geleerde items, base 36\n;g  ;g7     de g (7 halve tonen boven c3)\n;vui ;veu   de klinkerkaart\n;=hallo_jij de spraakcomputer zegt het\n;!         de trigger van de spraakcomputer"); }, ink), 80);
            auto* c = stage(pg, 2, "stemmen");
            c->add(new Info([this] {
                auto b2 = p.uiBank();
                juce::String s = b2 ? "stem: " + b2->voiceName + (b2->builtIn ? " (ingebouwd)" : "") : juce::String("stem: -");
                s << "\nnatuurlijke stemmen: tools/render.sh (zie de handleiding)\nmap: " << sb::supportDir().getFullPathName();
                return s;
            }), 76);
        }
        // modulation shown on the sliders it moves (a hollow tick)
        ghost("pitch", sm::prm::dPitch, 0.5f); ghost("formant", sm::prm::dFormant, 1); ghost("inton", sm::prm::dInton, 1); ghost("bright", sm::prm::dBright, 1);
        ghost("speed", sm::prm::dSpeed, 1); ghost("emotion", sm::prm::dEmotion, 1); ghost("start", sm::prm::dStart, 1); ghost("fine", sm::prm::dFine, 1);
        ghost("dlymix", sm::prm::dDelay, 1); ghost("revmix", sm::prm::dReverb, 1); ghost("width", sm::prm::dWidth, 1);
        ghost("cloud_density", sm::prm::dCloudDens, 0.5f); ghost("cloud_pos", sm::prm::dCloudPos, 1); ghost("cloud_pitch", sm::prm::dCloudPitch, 0.5f);
        ghost("loop_pitch", sm::prm::dLoopPitch, 0.5f); ghost("g_soft", sm::prm::dGSoft, 1); ghost("whisper_level", sm::prm::dWhisper, 1.0f / 1.5f);
        setPage((int) p.state.getProperty("uiPage", 0));
    }

    void flash(const juce::String& m, double secs = 2.0) { flashMsg = m; flashUntil = now() + secs; }

    void setPage(int pg) {
        page = juce::jlimit(0, kPages - 1, pg);
        for (int i = 0; i < pages.size(); ++i) pages[i]->body.setVisible(i == page);
        art.page = page;
        p.state.setProperty("uiPage", page, nullptr);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(paper);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // the page's name, as the suite writes it: code, name
        g.setColour(ink); g.setFont(sans(15.0f, true));
        g.drawText(kPageInfo[page].code, 36, 258, 40, 20, juce::Justification::centredLeft);
        g.setFont(sans(15.0f));
        g.drawText(kPageInfo[page].name, 70, 258, 200, 20, juce::Justification::centredLeft);
        // status line
        g.setColour(mute); g.setFont(mono(10.0f));
        auto b = p.uiBank();
        juce::String s = b ? b->status : juce::String::fromUTF8("woordenbank wordt geladen\xe2\x80\xa6");
        g.drawText(s, 36, H - 72, W - 72 - 250, 14, juce::Justification::centredLeft, true);
        g.drawText(juce::String("spreekbuis ") + JucePlugin_VersionString, W - 36 - 250, H - 72, 250, 14, juce::Justification::centredRight);
    }

    void resized() override {
        art.setBounds(0, 0, W, 250);
        presets.setBounds(W - 36 - 220, 14, 220, 20);
        stem.setBounds(W - 36 - 424, 14, 180, 20);
        const int top = 286, colW = 232, gap = 28;
        mixer.setBounds(W - 36 - 240, top, 240, mixer.wanted());
        for (auto* pg : pages) {
            pg->body.setBounds(36, top, 3 * colW + 2 * gap, H - top - 80);
            int y[3] = { 0, 0, 0 };
            for (int i = 0; i < pg->stages.size(); ++i) {
                const int c = pg->column[(size_t) i];
                auto* s = pg->stages[i];
                // the vowel chart takes two columns
                const bool wide = pg == pages[2] && i == 0;
                s->setBounds(c * (colW + gap), y[c], wide ? 2 * colW + gap : colW, s->wanted());
                y[c] += s->wanted() + 12;
            }
        }
        keys.setBounds(36, H - 52, W - 72, 36);
    }

    void refresh() {
        art.tick();
        static const int lv[6] = { 0, 1, 2, 3, 4, 5 };
        for (size_t i = 0; i < mixRows.size() && i < 6; ++i) {
            const float v = p.layerLevel[(size_t) lv[i]].load();
            mixRows[i]->level.setGhost(v > 0.003f ? juce::jlimit(0.0f, 1.0f, std::sqrt(v)) : -1.0f);
        }
        for (auto& gh : ghosts) {
            const float m = p.modShown[(size_t) gh.dest].load();
            gh.s->setGhost(std::abs(m) > 0.002f ? juce::jlimit(0.0f, 1.0f, gh.s->param.getValue() + m * gh.scale) : -1.0f);
        }
        if (page == 6) for (auto* t : traces) t->tick();
        pages[page]->body.repaint();
        keys.repaint();
        if (!port.hasKeyboardFocus(true) && port.getText().getIntValue() != p.orcaPort()) port.setText(juce::String(p.orcaPort()), false);
    }

    // ---- the lists the speech computer reads (1..99), one entry per line
    void editList() {
        const int n = (int) p.apvts.getRawParameterValue("list")->load();
        auto* ed = new juce::TextEditor();
        ed->setMultiLine(true, true); ed->setReturnKeyStartsNewLine(true);
        ed->setFont(mono(13.0f));
        ed->setColour(juce::TextEditor::backgroundColourId, card);
        ed->setColour(juce::TextEditor::textColourId, ink);
        ed->setText(p.planner().getList(n).joinIntoString("\n"));
        struct Holder : juce::Component {
            Holder(juce::TextEditor* e, std::function<void(juce::String)> onSave) : editor(e) {
                addAndMakeVisible(editor.get());
                save.setButtonText("bewaar"); cancel.setButtonText("annuleer");
                addAndMakeVisible(save); addAndMakeVisible(cancel);
                save.onClick = [this, onSave] { onSave(editor->getText()); close(); };
                cancel.onClick = [this] { close(); };
                setSize(440, 420);
            }
            void close() { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0); }
            void resized() override {
                auto r = getLocalBounds().reduced(10);
                auto b = r.removeFromBottom(30);
                save.setBounds(b.removeFromRight(100)); b.removeFromRight(8); cancel.setBounds(b.removeFromRight(100));
                r.removeFromBottom(8); editor->setBounds(r);
            }
            void paint(juce::Graphics& g) override { g.fillAll(paper); }
            std::unique_ptr<juce::TextEditor> editor;
            juce::TextButton save, cancel;
        };
        juce::DialogWindow::LaunchOptions o;
        auto* holder = new Holder(ed, [this, n](juce::String text) {
            juce::StringArray a; a.addLines(text); a.trim(); a.removeEmptyStrings();
            p.planner().setList(n, a);
            flash(juce::String::formatted("lijst %02d bewaard (", n) + juce::String(a.size()) + ")");
        });
        holder->setLookAndFeel(&getLookAndFeel());
        o.content.setOwned(holder);
        o.dialogTitle = juce::String::formatted("Spreekbuis - lijst %02d (een regel per item)", n);
        o.dialogBackgroundColour = paper;
        o.useNativeTitleBar = true; o.resizable = true;
        o.launchAsync();
    }
    void loadScl() {
        chooser = std::make_unique<juce::FileChooser>("Laad een Scala-stemming (.scl)", juce::File(), "*.scl");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f.existsAsFile()) flash(p.loadScl(f) ? "scala: " + p.sclName().toLowerCase() : juce::String("dit .scl-bestand is niet te lezen"));
        });
    }
};

// ---------------------------------------------------------------- editor
SpreekbuisEditor::SpreekbuisEditor(SpreekbuisProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    look = std::make_unique<Look>();
    setLookAndFeel(look.get());
    content = std::make_unique<Content>(p);
    addAndMakeVisible(*content);
    content->setBounds(0, 0, W, H);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(W * 6 / 10, H * 6 / 10, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    const int w = juce::jlimit(W * 6 / 10, W * 2, (int) p.state.getProperty("uiW", W));
    setSize(w, juce::roundToInt(w * (double) H / W));
    startTimerHz(30);
}
SpreekbuisEditor::~SpreekbuisEditor() { stopTimer(); setLookAndFeel(nullptr); }
void SpreekbuisEditor::paint(juce::Graphics& g) { g.fillAll(paper); }
void SpreekbuisEditor::resized() {
    content->setTransform(juce::AffineTransform::scale((float) getWidth() / W));
    proc.state.setProperty("uiW", getWidth(), nullptr);
    proc.state.setProperty("uiH", getHeight(), nullptr);
}
void SpreekbuisEditor::timerCallback() {
    if (content->art.typing && now() > typedUntil) { content->art.typing = false; content->art.typed.clear(); }
    content->refresh();
}
void SpreekbuisEditor::showPage(int page) { content->setPage(page); }
void SpreekbuisEditor::tickForTest() { content->refresh(); }
void SpreekbuisEditor::typeForTest(const juce::String& t) { content->art.typed = t; content->art.typing = t.isNotEmpty(); typedUntil = now() + 60; }

// Typing anywhere in the window is typing to the speech computer (the module's USB keyboard): Enter makes it say that.
bool SpreekbuisEditor::keyPressed(const juce::KeyPress& k) {
    auto& a = content->art;
    if (k == juce::KeyPress::returnKey) {
        if (a.typed.isNotEmpty()) { proc.typeAndSpeak(a.typed); content->flash("> " + a.typed); }
        else proc.manualTrigger();
        a.typed.clear(); a.typing = false;
        return true;
    }
    if (k == juce::KeyPress::escapeKey) { a.typed.clear(); a.typing = false; return true; }
    if (k == juce::KeyPress::backspaceKey) { a.typed = a.typed.dropLastCharacters(1); a.typing = a.typed.isNotEmpty(); typedUntil = now() + 6; return true; }
    if (k == juce::KeyPress::leftKey) { content->setPage((content->page + kPages - 1) % kPages); return true; }
    if (k == juce::KeyPress::rightKey) { content->setPage((content->page + 1) % kPages); return true; }
    const juce::juce_wchar c = k.getTextCharacter();
    if (c == 32 && a.typed.isEmpty()) return false;   // (a first space is the host's: play / stop)
    if (c >= 32 && !k.getModifiers().isCommandDown() && !k.getModifiers().isCtrlDown() && a.typed.length() < 120) {
        a.typed += juce::String::charToString(c);
        a.typing = true; typedUntil = now() + 6;
        return true;
    }
    return false;
}
