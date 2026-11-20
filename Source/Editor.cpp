#include "Editor.h"
#include "Sanne.h"
#include "Phonemes.h"

using namespace sbui;
using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

juce::Font sbui::sans(float h, bool bold) { return juce::Font(juce::FontOptions("Helvetica Neue", h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sbui::mono(float h) { return juce::Font(juce::FontOptions("Menlo", h, juce::Font::plain)); }

namespace {
double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
juce::String U(const char* s) { return juce::String::fromUTF8(s); }
juce::String noteName(int n) {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2);
}
} // namespace

// ---------------------------------------------------------------- look (1.0's)
Look::Look() {
    setColour(juce::Label::textColourId, ink);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::backgroundColourId, card);
    setColour(juce::ComboBox::outlineColourId, soft);
    setColour(juce::ComboBox::arrowColourId, ink);
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
    setColour(juce::Slider::textBoxTextColourId, mute);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, card);
    setColour(juce::TooltipWindow::textColourId, ink);
    setColour(juce::TooltipWindow::outlineColourId, soft);
    setColour(juce::ScrollBar::thumbColourId, soft);
}
void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    float d = std::min(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(d, d);
    g.setColour(card.brighter(0.06f)); g.fillEllipse(c);
    g.setColour(soft.brighter(0.25f)); g.drawEllipse(c, 2.0f);
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
    g.setColour(on ? orange : (over || down ? hover : card));
    g.fillRoundedRectangle(r, 5);
    g.setColour(on ? orange : soft.brighter(0.2f)); g.drawRoundedRectangle(r, 5, 1.6f);
}
void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(b.getToggleState() ? dark : ink);
    g.setFont(sans(12.5f, true));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}
void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1);
    g.setColour(card); g.fillRoundedRectangle(r, 5);
    g.setColour(soft.brighter(0.2f)); g.drawRoundedRectangle(r, 5, 1.6f);
    g.setColour(ink);
    juce::Path t; float cx = (float) w - 12, cy = (float) h / 2;
    t.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.fillPath(t);
}

// ---------------------------------------------------------------- widgets (1.0's)
namespace {
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::String name;
    std::unique_ptr<SA> att;
    Knob(SpreekbuisProcessor& p, const char* id, juce::String label) : name(std::move(label)) {
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 14);
        s.setColour(juce::Slider::textBoxTextColourId, mute);
        addAndMakeVisible(s);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        auto* rp = p.apvts.getParameter(id);
        att = std::make_unique<SA>(p.apvts, id, s);
        const juce::String sid(id);
        if (sid == "loop_note" || sid == "root") s.textFromValueFunction = [](double v) { return noteName((int) std::lround(v)); };
        else if (sid == "list") s.textFromValueFunction = [](double v) { return juce::String((int) std::lround(v)).paddedLeft('0', 2); };
        else if (sid == "down") s.textFromValueFunction = [](double v) { return "1/" + juce::String((int) std::lround(v)); };
        else if (rp != nullptr && rp->getVersionHint() >= 2)   // the speech computer's: as its parameters say them
            s.textFromValueFunction = [rp](double v) { return rp->getText(rp->convertTo0to1((float) v), 12); };
        else   // 1.0's numbers
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
        const juce::String sid(id);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(id))) box.addItemList(c->choices, 1);
        else if (auto* ip = dynamic_cast<juce::AudioParameterInt*>(p.apvts.getParameter(id)))
            for (int v = ip->getRange().getStart(); v <= ip->getRange().getEnd(); ++v) {
                juce::String t(v);
                if (v == 0 && sid == "loop_unit") t = "current (Tongval)";
                if (sid == "chord") t = sm::prm::kChord[juce::jlimit(0, 9, v - 1)];
                box.addItem(t, v - ip->getRange().getStart() + 1);
            }
        addAndMakeVisible(box);
        att = std::make_unique<CA>(p.apvts, id, box);
    }
    void paint(juce::Graphics& g) override { g.setColour(mute); g.setFont(mono(10.5f)); g.drawText(name.toUpperCase(), getLocalBounds().removeFromTop(14), juce::Justification::centredLeft); }
    void resized() override { box.setBounds(getLocalBounds().withTrimmedTop(15)); }
};

// a line or two of quiet mono text that says how things stand
struct Info : juce::Component {
    std::function<juce::String()> get; juce::Colour col;
    Info(std::function<juce::String()> f, juce::Colour c = mute) : get(std::move(f)), col(c) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        g.setColour(col); g.setFont(mono(10.5f));
        g.drawFittedText(get(), getLocalBounds(), juce::Justification::topLeft, std::max(1, getHeight() / 13), 1.0f);
    }
};

// ---------------------------------------------------------------- the karaoke card: the item you are hearing, word by word; Sanne says the speech computer
struct Karaoke : juce::Component {
    SpreekbuisProcessor& p;
    juce::String typed; bool typing = false;
    float time = 0;
    explicit Karaoke(SpreekbuisProcessor& pr) : p(pr) {}

    void paintSanne(juce::Graphics& g, float x0, float y0, float px) {
        namespace sn = sm::sanne;
        const bool blink = std::fmod(time, 3.7f) < 0.13f;
        const auto& spr = blink ? sn::eyesClosed : sn::eyesOpen;
        for (int py = 0; py < sn::H; ++py)
            for (int qx = 0; qx < sn::W; ++qx) {
                const char c = spr[py][qx];
                const int v = c <= '9' ? c - '0' : c - 'a' + 10;
                if (v <= 0) continue;
                g.setColour(navy.darker(0.6f).interpolatedWith(ink, (float) v / 15.0f));
                g.fillRect(x0 + (float) qx * px, y0 + (float) py * px, px, px);
            }
        sn::Viseme vis = sn::Rest;
        const float lvl = juce::jlimit(0.0f, 1.0f, p.meter() * 9.0f);
        if (lvl >= 0.06f) vis = sn::visemeFor(sm::phon(p.display().phon).id);
        else { const float bl = p.layerLevel[0].load(); vis = bl > 0.22f ? sn::AA : bl > 0.07f ? sn::Mid : sn::Rest; }
        const auto& m = sn::mouths[vis];
        const int mx = sn::mouthX - m.w / 2, my = sn::mouthY - m.h / 2;
        for (int r = 0; r < m.h; ++r)
            for (int c = 0; c < m.w; ++c) {
                const char k = m.rows[r][c];
                if (k == '.') continue;
                g.setColour(k == 'L' ? navy.darker(0.6f).interpolatedWith(ink, 0.42f) : k == 'T' ? juce::Colours::white : dark);
                g.fillRect(x0 + (float) (mx + c) * px, y0 + (float) (my + r) * px, px, px);
            }
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(navy); g.fillRoundedRectangle(r, 8);
        // Sanne in the corner, standing on the card's floor
        const float px = 2.5f, sw = sm::sanne::W * px, sh = sm::sanne::H * px;
        {
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clip; clip.addRoundedRectangle(r, 8); g.reduceClipRegion(clip);
            paintSanne(g, r.getRight() - sw - 22, r.getBottom() - sh, px);
        }
        auto inner = getLocalBounds().reduced(24, 14);
        inner.removeFromRight((int) sw + 30);
        auto bottom = inner.removeFromBottom(18);
        const auto white = juce::Colours::white;
        auto b = p.uiBank();
        int item = p.shownItem.load(), word = p.shownWord.load();
        g.setColour(white.withAlpha(0.55f)); g.setFont(mono(11));
        if (!b || !b->manifestFound) {
            g.drawText("KARAOKE", inner.removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(white); g.setFont(sans(26, true));
            g.drawFittedText(b ? (b->status.isNotEmpty() ? b->status : juce::String("Geen stemmen - run tools/render.sh")) : U("Laden\xe2\x80\xa6"), inner, juce::Justification::centredLeft, 2);
        } else if (item < 0 || item >= (int) b->items.size()) {
            g.drawText("LES " + juce::String(b->unit) + " - DRUK OP PLAY OF EEN TOETS", inner.removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(white); g.setFont(sans(40, true));
            g.drawText("spreekbuis", inner, juce::Justification::centredLeft);
        } else {
            auto& it = b->items[(size_t) item];
            juce::String kind = it.kind == "w" || it.kind == "w0" ? "WOORD" : it.kind == "s" ? "ZIN" : "GESPREK";
            g.drawText(kind + " - LES " + juce::String(it.unit), inner.removeFromTop(16), juce::Justification::centredLeft);
            // words, highlighted as they are spoken (as large as fits on one line)
            float size = it.nl.length() > 40 ? 30.0f : it.nl.length() > 22 ? 38.0f : 48.0f;
            auto widthAt = [&](float sz) { auto f = sans(sz, true); float w = 0; for (auto& wd : it.words) w += juce::GlyphArrangement::getStringWidth(f, wd + " "); return w; };
            while (size > 18 && widthAt(size) > (float) inner.getWidth()) size -= 2;
            auto f = sans(size, true);
            float x = (float) inner.getX(), y = (float) inner.getY() + 4;
            for (int i = 0; i < it.words.size(); ++i) {
                auto w = it.words[i];
                float ww = juce::GlyphArrangement::getStringWidth(f, w + " ");
                bool lit = i == word;
                if (lit) { g.setColour(yellow); g.fillRoundedRectangle(x - 4, y, juce::GlyphArrangement::getStringWidth(f, w) + 8, size * 1.18f, 5); }
                g.setColour(lit ? dark : (word >= 0 && i < word ? white.withAlpha(0.6f) : white));
                g.setFont(f);
                g.drawText(w, juce::Rectangle<float>(x, y, ww + 10, size * 1.2f), juce::Justification::centredLeft);
                x += ww;
            }
            g.setColour(white.withAlpha(0.7f)); g.setFont(sans(17));
            g.drawText(it.en, juce::Rectangle<float>((float) inner.getX(), y + size * 1.3f, (float) inner.getWidth(), 24), juce::Justification::centredLeft, true);
        }
        // the speech computer's line: what is typed, or what it says
        g.setFont(mono(11));
        if (typing) {
            g.setColour(yellow);
            g.drawText("> " + typed + ((int) (time * 3.0f) % 2 ? "_" : " "), bottom, juce::Justification::centredLeft, true);
        } else {
            const auto d = p.display();
            const bool spraakOn = *p.apvts.getRawParameterValue("spraak_on") > 0.5f;
            g.setColour(white.withAlpha(spraakOn || p.speaking() ? 0.9f : 0.5f));
            juce::String s("SPRAAK ");
            if (p.speaking()) s << "- /" << juce::String::fromUTF8(sm::phon(d.phon).id) << "/ - " << juce::String::fromUTF8(d.word);
            else if (spraakOn) s << "- typ iets + enter";
            else s << "uit - typ iets + enter";
            g.drawText(s, bottom, juce::Justification::centredLeft, true);
        }
    }
};

// ---------------------------------------------------------------- the Dutch vowel chart as an XY pad (1.0's)
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
        g.setColour(paper); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        g.setColour(soft); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6, 1);
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
        g.setColour(dark); g.drawEllipse(dot.x - 7, dot.y - 7, 14, 14, 2);
        g.setColour(mute); g.setFont(mono(9.5f));
        g.drawText("VOOR", getLocalBounds().reduced(6).removeFromBottom(12), juce::Justification::centredLeft);
        g.drawText("ACHTER", getLocalBounds().reduced(6).removeFromBottom(12), juce::Justification::centredRight);
        // why it might be quiet
        juce::String hint;
        const int src = (int) *p.apvts.getRawParameterValue("vowel_src");
        const bool on = *p.apvts.getRawParameterValue("vowel_on") > 0.5f;
        const bool loopOn = *p.apvts.getRawParameterValue("loop_on") > 0.5f, cloudOn = *p.apvts.getRawParameterValue("cloud_on") > 0.5f;
        const bool spraakOn = *p.apvts.getRawParameterValue("spraak_on") > 0.5f;
        if (src == 2 && p.inputLevel.load() < 1.0e-4f) hint = "bron Ingang: er komt geen audio binnen";
        else if (src == 4 && !loopOn) hint = "bron Loop: zet LOOP aan";
        else if (src == 5 && !cloudOn) hint = "bron Wolk: zet WOLK aan";
        else if (src == 6 && !loopOn && !cloudOn) hint = "bron Loop + Wolk: zet ze aan";
        else if (src == 7 && !spraakOn) hint = "bron Spraak: zet SPRAAK aan";
        else if (src == 8 && !loopOn && !cloudOn && !spraakOn) hint = "bron Alles: zet een laag aan";
        else if (src >= 4 && on) hint = "";
        else if (on && !p.notesReachVowel()) hint = "noten: zet Toetsen op Klinker";
        else if (!on) hint = "klik of sleep om te horen";
        if (hint.isNotEmpty()) {
            g.setColour(on || src == 2 ? orange : mute); g.setFont(mono(9.5f));
            g.drawText(hint, getLocalBounds().reduced(6).removeFromTop(12), juce::Justification::centred);
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
};

// an LFO drawn as it runs
struct Trace : juce::Component {
    std::function<float()> get; juce::Colour col;
    static constexpr int kN = 120;
    std::array<float, kN> hist {}; int pos = 0;
    Trace(std::function<float()> f, juce::Colour c) : get(std::move(f)), col(c) {}
    void tick() { hist[(size_t) pos] = get(); pos = (pos + 1) % kN; repaint(); }
    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        g.setColour(paper); g.fillRoundedRectangle(b, 5);
        g.setColour(soft); g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
        juce::Path path;
        for (int i = 0; i < kN; ++i) {
            const float v = hist[(size_t) ((pos + i) % kN)] * 0.5f + 0.5f;
            const float x = b.getX() + 4 + (b.getWidth() - 8) * (float) i / (float) (kN - 1), y = b.getBottom() - 5 - v * (b.getHeight() - 10);
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        g.setColour(col); g.strokePath(path, juce::PathStrokeType(2.0f));
    }
};

// ---------------------------------------------------------------- the keys: a plain keyboard in the panels' colours
struct Keys : juce::Component {
    SpreekbuisProcessor& p; int base = 36, down = -1;
    static constexpr int kOct = 4;
    explicit Keys(SpreekbuisProcessor& pr) : p(pr) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    juce::Rectangle<float> keyArea() const { return getLocalBounds().toFloat().withTrimmedLeft(26).withTrimmedRight(26); }
    float whiteW() const { return keyArea().getWidth() / (7.0f * kOct); }
    juce::Rectangle<float> rectFor(int k) const {   // k: 0 .. 12*kOct-1
        static const int whiteIdx[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const auto a = keyArea(); const float w = whiteW();
        const int oct = k / 12, n = k % 12;
        const float x = a.getX() + (float) (oct * 7 + whiteIdx[n]) * w;
        if (juce::MidiMessage::isMidiNoteBlack(n)) return { x + w * 0.68f, a.getY(), w * 0.64f, a.getHeight() * 0.6f };
        return { x, a.getY(), w, a.getHeight() };
    }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(ink); g.setFont(sans(14, true));
        g.drawText("<", b.withWidth(22), juce::Justification::centred);
        g.drawText(">", b.withLeft(b.getRight() - 22), juce::Justification::centred);
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 0; k < 12 * kOct; ++k) {
                const int nn = base + k;
                const bool black = juce::MidiMessage::isMidiNoteBlack(nn);
                if (black != (pass == 1)) continue;
                auto r = rectFor(k).reduced(1.0f, 0);
                const bool on = p.keyState.isNoteOnForChannels(0xffff, nn);
                g.setColour(on ? orange : black ? paper : card.brighter(0.06f));
                g.fillRoundedRectangle(r, 3);
                g.setColour(on ? orange : soft.brighter(0.2f)); g.drawRoundedRectangle(r, 3, 1.2f);
                if (nn % 12 == 0) {
                    g.setColour(on ? dark : mute); g.setFont(mono(9.5f));
                    g.drawText("C" + juce::String(nn / 12 - 2), r.withTrimmedTop(r.getHeight() - 14), juce::Justification::centred);
                }
            }
    }
    int noteAt(juce::Point<float> pt) const {
        for (int pass = 1; pass >= 0; --pass)
            for (int k = 0; k < 12 * kOct; ++k)
                if (juce::MidiMessage::isMidiNoteBlack(k % 12) == (pass == 1) && rectFor(k).contains(pt)) return base + k;
        return -1;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.x < 24) { base = std::max(0, base - 12); repaint(); return; }
        if (e.x > getWidth() - 24) { base = std::min(72, base + 12); repaint(); return; }
        down = noteAt(e.position);
        if (down >= 0) p.keyState.noteOn(1, down, 0.6f + 0.4f * (float) e.y / (float) getHeight());
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const int n = noteAt(e.position);
        if (n >= 0 && n != down) { const int old = down; down = n; p.keyState.noteOn(1, n, 0.85f); if (old >= 0) p.keyState.noteOff(1, old, 0.0f); repaint(); }
    }
    void mouseUp(const juce::MouseEvent&) override { if (down >= 0) p.keyState.noteOff(1, down, 0.0f); down = -1; repaint(); }
};

// ---------------------------------------------------------------- presets: a box in 1.0's hand, with every kind of preset in its menu
struct PresetBox : juce::Component, juce::SettableTooltipClient {
    SpreekbuisProcessor& p;
    juce::Array<juce::File> files;
    std::unique_ptr<juce::FileChooser> chooser;
    explicit PresetBox(SpreekbuisProcessor& pr) : p(pr) { setMouseCursor(juce::MouseCursor::PointingHandCursor); setTooltip("presets: fabriek, eigen, Spraakmaker 1.0, bewaren, importeren"); }
    juce::String name() const { return p.presetLabel().isNotEmpty() ? p.presetLabel() : p.getProgramName(p.getCurrentProgram()); }
    void paint(juce::Graphics& g) override {
        getLookAndFeel().drawComboBox(g, getWidth(), getHeight(), false, 0, 0, 0, 0, dummy);
        g.setColour(ink); g.setFont(sans(13));
        g.drawText(name(), getLocalBounds().withTrimmedLeft(10).withTrimmedRight(24), juce::Justification::centredLeft, true);
    }
    void mouseDown(const juce::MouseEvent&) override {
        const int n = p.getNumPrograms(), cur = p.getCurrentProgram();
        juce::PopupMenu m, fab, sp, both, own, old;
        for (int i = 0; i < n; ++i) {
            auto& dst = i == 0 ? m : i <= 9 ? fab : i <= 29 ? sp : both;
            dst.addItem(i + 1, p.getProgramName(i), true, i == cur && p.presetLabel().isEmpty());
        }
        m.addSubMenu("Woordenbank (Spreekbuis 1.0)", fab);
        m.addSubMenu("Spraakcomputer (Spraakmaker 1.0)", sp);
        m.addSubMenu("Samen", both);
        files.clear();
        for (auto& f : p.presetDir().findChildFiles(juce::File::findFiles, false, "*.xml")) { files.add(f); own.addItem(1000 + files.size(), f.getFileNameWithoutExtension()); }
        if (own.getNumItems() > 0) m.addSubMenu("Eigen presets", own);
        for (auto& f : sb::legacySpraakmakerDir().getChildFile("presets").findChildFiles(juce::File::findFiles, false, "p*.xml")) {
            juce::String nm = f.getFileNameWithoutExtension();
            if (auto x = juce::XmlDocument::parse(f)) nm << " " << x->getStringAttribute("presetName");
            files.add(f); old.addItem(1000 + files.size(), nm);
        }
        if (old.getNumItems() > 0) m.addSubMenu("Opgeslagen in Spraakmaker 1.0", old);
        m.addSeparator();
        m.addItem(900, U("Bewaar als\xe2\x80\xa6"));
        m.addItem(901, U("Open of importeer (ook Spraakmaker 1.0)\xe2\x80\xa6"));
        juce::Component::SafePointer<PresetBox> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
            if (!safe || r <= 0) return;
            if (r == 900) safe->save();
            else if (r == 901) safe->open();
            else if (r > 1000) safe->p.loadPresetFile(safe->files[r - 1001]);
            else safe->p.loadProgram(r - 1);
            safe->repaint();
        });
    }
    void save() {
        p.presetDir().createDirectory();
        chooser = std::make_unique<juce::FileChooser>("Bewaar preset", p.presetDir().getChildFile(name().isEmpty() ? "preset" : name()).withFileExtension("xml"), "*.xml");
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
    juce::ComboBox dummy;
};
} // namespace

// ---------------------------------------------------------------- content (laid out at W x H, scaled)
struct SpreekbuisEditor::Content : juce::Component {
    using W_ = SpreekbuisEditor;
    SpreekbuisProcessor& p;
    Karaoke karaoke;
    PresetBox presets;
    Keys keys;
    juce::ComboBox voiceBox;
    std::unique_ptr<CA> voiceAtt;
    Toggle allItems;
    juce::TextButton reload { "Herlaad" };
    juce::TextButton tabs[kPages];
    juce::Label status, help;
    juce::String flashMsg; double flashUntil = 0;

    struct Sec { juce::Rectangle<int> r; juce::String sub; int page; };
    std::vector<Sec> sections;
    struct Page { juce::Component body; };
    Page pages[kPages];
    int page = 0;
    std::map<juce::String, juce::Component*> byId;
    juce::OwnedArray<juce::Component> owned;
    std::vector<Trace*> traces;
    std::unique_ptr<juce::FileChooser> chooser;

    // page 0 (1.0's panels)
    VowelPad pad;
    juce::TextEditor port;
    juce::Label orcaStatus;
    // page 1
    juce::TextEditor say;

    template <class T> T* own(int pg, T* c) { owned.add(c); pages[pg].body.addAndMakeVisible(c); return c; }
    Knob* knob(int pg, const char* id, const char* label) { auto* k = own(pg, new Knob(p, id, label)); byId[id] = k; return k; }
    Choice* choice(int pg, const char* id, const char* label) { auto* c = own(pg, new Choice(p, id, label)); byId[id] = c; return c; }
    Toggle* toggle(int pg, const char* id, const char* label) { auto* t = own(pg, new Toggle(p, id, label)); byId[id] = t; return t; }
    juce::TextButton* button(int pg, const char* key, const char* label, std::function<void()> f) {
        auto* b = own(pg, new juce::TextButton(label)); b->onClick = std::move(f); byId[key] = b; return b;
    }
    Info* info(int pg, const char* key, std::function<juce::String()> f, juce::Colour c = mute) { auto* i = own(pg, new Info(std::move(f), c)); byId[key] = i; return i; }

    explicit Content(SpreekbuisProcessor& pr)
        : p(pr), karaoke(pr), presets(pr), keys(pr), allItems(pr, "all_items", "Alle items"), pad(pr) {
        for (auto* c : std::initializer_list<juce::Component*> { &karaoke, &presets, &keys, &allItems, &reload, &status, &help }) addAndMakeVisible(*c);
        // man's or woman's voice: the words, the vowel chart and the speech computer
        voiceBox.addItemList({ "Man", "Vrouw" }, 1);
        voiceBox.setTooltip("de stem: de woorden, de klinkerkaart en de spraakcomputer");
        addAndMakeVisible(voiceBox);
        voiceAtt = std::make_unique<CA>(p.apvts, "voice", voiceBox);
        reload.onClick = [this] { p.requestReload(); };
        for (auto* l : { &status, &help, &orcaStatus }) { l->setFont(mono(11)); l->setColour(juce::Label::textColourId, mute); }
        status.setJustificationType(juce::Justification::centredLeft);
        help.setText("orca:  ;fiets  ;g  ;g7  ;vui  ;veu  ;1a  ;=hallo_jij      typ in het venster + enter: de spraakcomputer zegt het      natural voices: tools/render.sh",
                     juce::dontSendNotification);
        static const char* tabNames[kPages] = { "Klanken", "Spraak + FX", "Mod" };
        for (int i = 0; i < kPages; ++i) {
            tabs[i].setButtonText(tabNames[i]);
            tabs[i].setClickingTogglesState(false);
            tabs[i].onClick = [this, i] { setPage(i); };
            addAndMakeVisible(tabs[i]);
            addChildComponent(pages[i].body);
        }

        // ================= page 0: the word bank (1.0's five panels)
        toggle(0, "loop_on", "LOOP"); toggle(0, "loop_whisper", "Fluister vertaling"); toggle(0, "loop_midi", "MIDI uit");
        toggle(0, "loop_echo", "Spraak antwoordt");
        choice(0, "loop_unit", "Les"); choice(0, "loop_set", "Speelt"); choice(0, "loop_bars", "Roep (maten)"); choice(0, "loop_gap", "Antwoord (maten)");
        choice(0, "loop_order", "Volgorde"); choice(0, "loop_trig", "Woord > spraak");
        knob(0, "loop_level", "Niveau"); knob(0, "loop_pitch", "Toon"); knob(0, "loop_bpm", "Vrije BPM"); knob(0, "loop_note", "MIDI noot");
        toggle(0, "cloud_on", "WOLK");
        knob(0, "cloud_level", "Niveau"); knob(0, "cloud_density", "Dichtheid"); knob(0, "cloud_size", "Korrel ms"); knob(0, "cloud_scatter", "Spreiding");
        knob(0, "cloud_pos", "Positie"); knob(0, "cloud_pitch", "Toon"); knob(0, "cloud_spread", "Stereo"); knob(0, "cloud_reverse", "Achteruit");
        knob(0, "cloud_whisper", "Vertaling"); knob(0, "whisper_level", "Fluister");
        toggle(0, "vowel_on", "KLINKER");
        pages[0].body.addAndMakeVisible(pad);
        choice(0, "vowel_glide", "Tweeklank"); choice(0, "vowel_src", "Bron");
        knob(0, "vowel_level", "Niveau"); knob(0, "vowel_rate", "Snelheid"); knob(0, "vowel_noise", "Adem"); knob(0, "vowel_q", "Resonantie");
        knob(0, "vowel_shift", "Formant"); knob(0, "vowel_attack", "Aanzet"); knob(0, "vowel_release", "Uitklank");
        toggle(0, "g_on", "G");
        knob(0, "g_level", "Niveau"); knob(0, "g_soft", "NL - BE"); knob(0, "g_rasp", "Rasp"); knob(0, "g_voice", "Stem");
        knob(0, "g_harm", "Resonantie x"); knob(0, "g_release", "Uitklank");
        toggle(0, "orca_on", "ORCA");
        pages[0].body.addAndMakeVisible(port); pages[0].body.addAndMakeVisible(orcaStatus);
        port.setFont(mono(13)); port.setInputRestrictions(5, "0123456789");
        port.setText(juce::String(p.orcaPort()), false);
        port.onReturnKey = [this] { p.setOrcaPort(port.getText().getIntValue()); };
        port.onFocusLost = [this] { p.setOrcaPort(port.getText().getIntValue()); };
        choice(0, "keys", "Toetsen"); knob(0, "master", "Master"); knob(0, "space", "Ruimte");

        // ================= page 1: the speech computer and the effects
        toggle(1, "spraak_on", "SPRAAK");
        knob(1, "level", "Niveau"); knob(1, "pitch", "Toonhoogte"); knob(1, "formant", "Formant"); knob(1, "inton", "Intonatie");
        knob(1, "bright", "Helderheid"); knob(1, "speed", "Tempo"); knob(1, "emotion", "Emotie");
        pages[1].body.addAndMakeVisible(say);
        say.setFont(mono(13)); say.setTextToShowWhenEmpty("typ iets + enter", mute);
        say.setInputRestrictions(120);
        say.onReturnKey = [this] { const auto t = say.getText().trim(); if (t.isNotEmpty()) { p.typeAndSpeak(t); flash("> " + t); } else p.manualTrigger(); say.clear(); };
        say.onEscapeKey = [this] { say.clear(); if (auto* e = findParentComponentOfClass<SpreekbuisEditor>()) e->grabKeyboardFocus(); };
        button(1, "speak", "Spreek", [this] { p.manualTrigger(); });
        info(1, "spraak_info", [this] {
            if (now() < flashUntil) return flashMsg;
            const bool on = *p.apvts.getRawParameterValue("spraak_on") > 0.5f;
            const int trig = (int) *p.apvts.getRawParameterValue("trig");
            const bool fed = *p.apvts.getRawParameterValue("loop_trig") > 0.5f || *p.apvts.getRawParameterValue("loop_echo") > 0.5f;
            if (!on) return juce::String("de laag staat uit: enter en Spreek spreken toch");
            if (trig != 0 && !p.notesReachSpraak() && !fed) return juce::String("noten: zet Toetsen op Spraak of Alles");
            if ((int) *p.apvts.getRawParameterValue("engine") == 1) return juce::String("ingang 1: draaggolf ") + (p.externalCarrier() ? "(signaal)" : "(eigen)") + " / ingang 2: modulator " + (p.externalModulator() ? "(signaal)" : "(-)");
            return juce::String::fromUTF8(p.display().text);
        }, orange);
        choice(1, "trig", "Trigger"); choice(1, "rate", "Maat"); choice(1, "source", "Bron"); choice(1, "order", "Volgorde"); choice(1, "trgout", "Trg uit");
        toggle(1, "retrig", "Hertrigger");
        knob(1, "list", "Lijst"); knob(1, "start", "Startpunt");
        button(1, "editlist", "Lijst bewerken", [this] { editList(); });
        info(1, "trig_info", [] { return juce::String("bron les: de woordenbank (wat de loop speelt). trg uit: midi 60 per woord, 62 per foneem"); });
        choice(1, "lang", "Taal"); choice(1, "engine", "Motor"); choice(1, "poly", "Stemmen"); choice(1, "chord", "Akkoord");
        choice(1, "micro", "Stemming"); choice(1, "carrier", "Draaggolf"); choice(1, "vocmod", "Modulator");
        knob(1, "fine", "Fijn"); knob(1, "glide", "Glijden"); knob(1, "root", "Grondtoon"); knob(1, "bands", "Banden");
        button(1, "scl", ".scl laden", [this] { loadScl(); });
        info(1, "motor_info", [this] { return p.sclName().isNotEmpty() ? "scala: " + p.sclName() : juce::String("modulator loop / wolk: de stem van de bank door de vocoder"); });
        choice(1, "fx_src", "Bewerkt"); choice(1, "dlytime", "Echo tijd");
        knob(1, "bits", "Bits"); knob(1, "down", "Downsample"); knob(1, "width", "Breedte");
        knob(1, "dlyfb", "Echo terug"); knob(1, "dlymix", "Echo mix"); knob(1, "revsize", "Galm"); knob(1, "revmix", "Galm mix");
        info(1, "fx_info", [] { return juce::String("Spraak: alleen de spraakcomputer gaat door de effecten. Alles: ook loop, wolk, fluister, klinker en g"); });

        // ================= page 2: modulation
        for (int k = 1; k <= 4; ++k) {
            const juce::String n(k);
            auto* t = own(2, new Trace([this, k] { return p.lfoShown[(size_t) k - 1].load(); }, k == 1 ? orange : k == 2 ? yellow : k == 3 ? blue : ink));
            traces.push_back(t); byId["trace" + n] = t;
            knob(2, ("lfo" + n + "rate").toRawUTF8(), ("LFO " + n).toRawUTF8());
            choice(2, ("lfo" + n + "shape").toRawUTF8(), "Vorm");
            choice(2, ("cv" + n + "src").toRawUTF8(), ("Bron " + n).toRawUTF8());
            choice(2, ("cv" + n + "dst").toRawUTF8(), "Doel");
            knob(2, ("cv" + n + "amt").toRawUTF8(), "Diepte");
            knob(2, ("cv" + n).toRawUTF8(), "Knop");
        }
        for (int k = 1; k <= 3; ++k) {
            const juce::String n(k);
            knob(2, ("m" + n).toRawUTF8(), ("M" + n).toRawUTF8());
            choice(2, ("m" + n + "dst").toRawUTF8(), "Doel");
        }
        info(2, "mod_info", [] { return juce::String("bronnen: knop, lfo 1-4, aanslag, modwiel, toeval, en uit de bank: woord (elk woord), bank (hoe luid), spraak (hoe luid), klink x / y.\n\ndoelen: de spraakcomputer, de effecten, de klinkerkaart, de wolk, de loop, de g, het gefluister."); });

        setPage((int) p.state.getProperty("uiTab", 0));
        refresh();
    }

    // the tab (uiTab; 2.0.0 kept its eight strips in uiPage)
    void setPage(int pg) {
        page = juce::jlimit(0, kPages - 1, pg);
        for (int i = 0; i < kPages; ++i) { pages[i].body.setVisible(i == page); tabs[i].setToggleState(i == page, juce::dontSendNotification); }
        p.state.setProperty("uiTab", page, nullptr);
        repaint();
    }
    void flash(const juce::String& m, double secs = 2.0) { flashMsg = m; flashUntil = now() + secs; }

    void paint(juce::Graphics& g) override {
        g.fillAll(paper);
        g.setColour(ink); g.setFont(sans(26, true)); g.drawText("spreekbuis", 22, 12, 200, 34, juce::Justification::centredLeft);
        g.setColour(mute); g.setFont(sans(12)); g.drawText("Nederlands als instrument", 160, 16, 220, 30, juce::Justification::centredLeft);
        g.setFont(mono(10.5f));
        g.drawText(juce::String("v") + JucePlugin_VersionString, W - 18 - 120, H - 24, 120, 16, juce::Justification::centredRight);
        for (auto& s : sections) {
            if (s.page != page) continue;
            g.setColour(card); g.fillRoundedRectangle(s.r.toFloat(), 8);
            g.setColour(soft); g.drawRoundedRectangle(s.r.toFloat().reduced(1), 8, 1.5f);
            g.setColour(mute); g.setFont(mono(10.5f));
            g.drawText(s.sub, s.r.reduced(14, 10).removeFromTop(16).withTrimmedLeft(56), juce::Justification::centredRight);
        }
    }

    void place(const juce::String& id, juce::Rectangle<int> r) { if (byId.count(id)) byId[id]->setBounds(r); }

    static constexpr int top = 252, k = 62;   // panels start here; knob cell
    static constexpr int panelH = H - top - 104;

    void resized() override {
        presets.setBounds(W - 520, 16, 200, 28);
        voiceBox.setBounds(W - 622, 16, 94, 28);
        allItems.setBounds(W - 310, 16, 96, 28);
        reload.setBounds(W - 206, 16, 80, 28);
        status.setBounds(380, 10, W - 1012, 40);
        karaoke.setBounds(18, 56, W - 36, 150);
        for (int i = 0; i < kPages; ++i) tabs[i].setBounds(18 + i * 128, 218, 120, 26);
        for (auto& pg : pages) pg.body.setBounds(getLocalBounds());
        keys.setBounds(18, H - 96, W - 36, 60);
        help.setBounds(18, H - 30, W - 160, 24);

        sections.clear();
        const int h = panelH, gap = 12;
        int x = 18, pg = 0;
        auto sec = [&](int w, juce::String sub) { juce::Rectangle<int> r(x, top, w, h); sections.push_back({ r, sub, pg }); x += w + gap; return r.reduced(14, 10); };
        auto grid = [&](std::initializer_list<const char*> ids, juce::Rectangle<int> area, int cols, int rowH, int cellW = 64) {
            int i = 0;
            for (auto* id : ids) { place(id, juce::Rectangle<int>(area.getX() + (i % cols) * cellW, area.getY() + (i / cols) * rowH, k, k + 14)); ++i; }
            return (i + cols - 1) / cols * rowH;
        };
        auto pair = [&](juce::Rectangle<int>& row, const char* a, const char* b) {
            auto rr = row.removeFromTop(40); place(a, rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); if (b) place(b, rr);
            row.removeFromTop(4);
        };

        // ---------------- page 0: LOOP WOLK KLINKER G ORCA (1.0's widths)
        auto r = sec(216, "les als loop");
        place("loop_on", { r.getX(), r.getY(), 80, 24 });
        auto row = r.withTrimmedTop(34);
        place("loop_unit", row.removeFromTop(40)); row.removeFromTop(4);
        pair(row, "loop_set", "loop_order");
        pair(row, "loop_bars", "loop_gap");
        row.removeFromTop(4);
        { auto rr = row.removeFromTop(24); place("loop_whisper", rr.removeFromLeft(118)); rr.removeFromLeft(6); place("loop_midi", rr); }
        row.removeFromTop(6);
        { auto rr = row.removeFromTop(24); place("loop_echo", rr.removeFromLeft(118)); }
        row.removeFromTop(6);
        { auto rr = row.removeFromTop(k + 14); for (auto id : { "loop_level", "loop_pitch", "loop_bpm" }) place(id, rr.removeFromLeft(k + 2)); }
        { auto rr = row.removeFromTop(k + 14); place("loop_note", rr.removeFromLeft(k + 6)); rr.removeFromTop(16); place("loop_trig", rr.removeFromTop(40)); }
        r = sec(284, "woordwolk");
        place("cloud_on", { r.getX(), r.getY(), 80, 24 });
        grid({ "cloud_level", "cloud_density", "cloud_size", "cloud_scatter", "cloud_pos", "cloud_pitch", "cloud_spread", "cloud_reverse", "cloud_whisper", "whisper_level" },
             r.withTrimmedTop(34), 4, 92);
        r = sec(284, "klinkerkaart");
        place("vowel_on", { r.getX(), r.getY(), 90, 24 });
        row = r.withTrimmedTop(32);
        pad.setBounds(row.removeFromTop(170));
        row.removeFromTop(6);
        pair(row, "vowel_glide", "vowel_src");
        grid({ "vowel_level", "vowel_rate", "vowel_noise", "vowel_q", "vowel_shift", "vowel_attack", "vowel_release" }, row, 4, 84);
        r = sec(150, "g / ch");
        place("g_on", { r.getX(), r.getY(), 50, 24 });
        grid({ "g_level", "g_soft", "g_rasp", "g_voice", "g_harm", "g_release" }, r.withTrimmedTop(34), 2, 92, 62);
        r = sec(W - x - 18, "orca + uit");
        place("orca_on", { r.getX(), r.getY(), 70, 24 });
        row = r.withTrimmedTop(34);
        { auto rr = row.removeFromTop(26); port.setBounds(rr.removeFromLeft(80)); }
        orcaStatus.setBounds(row.removeFromTop(36));
        row.removeFromTop(6);
        place("keys", row.removeFromTop(40));
        row.removeFromTop(8);
        { auto rr = row.removeFromTop(k + 18); place("master", rr.removeFromLeft(k + 2)); place("space", rr.removeFromLeft(k + 2)); }

        // ---------------- page 1: SPRAAK TRIGGER MOTOR FX
        x = 18; pg = 1;
        r = sec(284, "spraakcomputer");
        place("spraak_on", { r.getX(), r.getY(), 90, 24 });
        row = r.withTrimmedTop(34);
        row.removeFromTop(grid({ "level", "pitch", "formant", "inton", "bright", "speed", "emotion" }, row, 4, 84));
        row.removeFromTop(4);
        { auto rr = row.removeFromTop(28); place("speak", rr.removeFromRight(80)); rr.removeFromRight(6); say.setBounds(rr); }
        row.removeFromTop(6);
        place("spraak_info", row.removeFromTop(40));
        r = sec(250, "trigger + bron");
        place("retrig", { r.getX(), r.getY(), 100, 24 });
        row = r.withTrimmedTop(34);
        pair(row, "trig", "rate");
        pair(row, "source", "order");
        pair(row, "trgout", nullptr);
        row.removeFromTop(4);
        row.removeFromTop(grid({ "list", "start" }, row, 3, 84));
        place("editlist", row.removeFromTop(24).withWidth(120));
        row.removeFromTop(10);
        place("trig_info", row.removeFromTop(54));
        r = sec(300, "motor");
        row = r.withTrimmedTop(34);
        pair(row, "lang", "engine");
        pair(row, "poly", "chord");
        pair(row, "micro", "carrier");
        pair(row, "vocmod", nullptr);
        row.removeFromTop(4);
        row.removeFromTop(grid({ "fine", "glide", "root", "bands" }, row, 4, 84, 66));
        { auto rr = row.removeFromTop(24); place("scl", rr.removeFromLeft(110)); }
        row.removeFromTop(8);
        place("motor_info", row.removeFromTop(40));
        r = sec(W - x - 18, "effecten");
        row = r.withTrimmedTop(34);
        pair(row, "fx_src", "dlytime");
        row.removeFromTop(4);
        row.removeFromTop(grid({ "bits", "down", "width", "dlyfb", "dlymix", "revsize", "revmix" }, row, 4, 84, 60));
        row.removeFromTop(4);
        place("fx_info", row.removeFromTop(54));

        // ---------------- page 2: LFO MATRIX MACRO
        x = 18; pg = 2;
        r = sec(372, "vier lfo's");
        row = r.withTrimmedTop(34);
        for (int n = 1; n <= 4; ++n) {
            const juce::String s(n);
            auto rr = row.removeFromTop(k + 14); row.removeFromTop(8);
            place("lfo" + s + "rate", rr.removeFromLeft(k + 4));
            rr.removeFromLeft(8);
            auto right = rr; place("lfo" + s + "shape", right.removeFromLeft(90).withTrimmedTop(18).withHeight(40));
            right.removeFromLeft(10); place("trace" + s, right.withTrimmedTop(20).withHeight(52));
        }
        r = sec(470, "matrix: bron > doel");
        row = r.withTrimmedTop(34);
        for (int n = 1; n <= 4; ++n) {
            const juce::String s(n);
            auto rr = row.removeFromTop(k + 14); row.removeFromTop(8);
            auto c = rr.withTrimmedTop(18).withHeight(40);
            place("cv" + s + "src", c.removeFromLeft(130)); c.removeFromLeft(8);
            place("cv" + s + "dst", c.removeFromLeft(130));
            rr.removeFromLeft(276);
            place("cv" + s + "amt", rr.removeFromLeft(k + 2)); place("cv" + s, rr.removeFromLeft(k + 2));
        }
        r = sec(W - x - 18, "macro's");
        row = r.withTrimmedTop(34);
        for (int n = 1; n <= 3; ++n) {
            const juce::String s(n);
            auto rr = row.removeFromTop(k + 14); row.removeFromTop(8);
            place("m" + s, rr.removeFromLeft(k + 4)); rr.removeFromLeft(8);
            place("m" + s + "dst", rr.withTrimmedTop(18).withHeight(40));
        }
        row.removeFromTop(6);
        place("mod_info", row);
    }

    void refresh() {
        karaoke.time = (float) now();
        auto b = p.uiBank();
        status.setText(b ? b->status : U("woordenbank wordt geladen\xe2\x80\xa6"), juce::dontSendNotification);
        orcaStatus.setText(p.orcaListening ? "udp :" + juce::String(p.orcaPort()) + "\n" + juce::String(p.orcaPackets.load()) + " berichten" : "niet verbonden\n(poort bezet?)",
                           juce::dontSendNotification);
        if (!port.hasKeyboardFocus(true) && port.getText().getIntValue() != p.orcaPort()) port.setText(juce::String(p.orcaPort()), false);
        karaoke.repaint(); presets.repaint(); keys.repaint();
        if (page == 0) pad.repaint();
        if (page == 1) if (auto* i = byId["spraak_info"]) i->repaint();
        if (page == 2) for (auto* t : traces) t->tick();
    }

    // ---- the lists the speech computer reads (1..99), one entry per line
    void editList() {
        const int n = (int) p.apvts.getRawParameterValue("list")->load();
        auto* ed = new juce::TextEditor();
        ed->setMultiLine(true, true); ed->setReturnKeyStartsNewLine(true);
        ed->setFont(mono(13.0f));
        ed->setText(p.planner().getList(n).joinIntoString("\n"));
        struct Holder : juce::Component {
            Holder(juce::TextEditor* e, std::function<void(juce::String)> onSave) : editor(e) {
                addAndMakeVisible(editor.get());
                save.setButtonText("Bewaar"); cancel.setButtonText("Annuleer");
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
            if (f.existsAsFile()) flash(p.loadScl(f) ? "scala: " + p.sclName() : juce::String("dit .scl-bestand is niet te lezen"));
        });
    }
};

// ---------------------------------------------------------------- editor
SpreekbuisEditor::SpreekbuisEditor(SpreekbuisProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&look);
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
    auto& k = content->karaoke;
    if (k.typing && now() > typedUntil) { k.typing = false; k.typed.clear(); }
    content->refresh();
}
void SpreekbuisEditor::showPage(int page) { content->setPage(page); }
void SpreekbuisEditor::tickForTest() { content->refresh(); }
void SpreekbuisEditor::typeForTest(const juce::String& t) { content->karaoke.typed = t; content->karaoke.typing = t.isNotEmpty(); typedUntil = now() + 60; }

// Typing anywhere in the window is typing to the speech computer: Enter makes it say that.
bool SpreekbuisEditor::keyPressed(const juce::KeyPress& key) {
    auto& a = content->karaoke;
    if (key == juce::KeyPress::returnKey) {
        if (a.typed.isNotEmpty()) { proc.typeAndSpeak(a.typed); content->flash("> " + a.typed); }
        else proc.manualTrigger();
        a.typed.clear(); a.typing = false;
        return true;
    }
    if (key == juce::KeyPress::escapeKey) { a.typed.clear(); a.typing = false; return true; }
    if (key == juce::KeyPress::backspaceKey) { a.typed = a.typed.dropLastCharacters(1); a.typing = a.typed.isNotEmpty(); typedUntil = now() + 6; return true; }
    if (key == juce::KeyPress::leftKey) { content->setPage((content->page + kPages - 1) % kPages); return true; }
    if (key == juce::KeyPress::rightKey) { content->setPage((content->page + 1) % kPages); return true; }
    const juce::juce_wchar c = key.getTextCharacter();
    if (c == 32 && a.typed.isEmpty()) return false;   // (a first space is the host's: play / stop)
    if (c >= 32 && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && a.typed.length() < 120) {
        a.typed += juce::String::charToString(c);
        a.typing = true; typedUntil = now() + 6;
        return true;
    }
    return false;
}
