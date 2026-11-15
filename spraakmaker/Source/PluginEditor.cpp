#include "PluginEditor.h"
#include "Sanne.h"
#include "Phonemes.h"

using namespace sm;

namespace {
// dark edition palette
const juce::Colour kPanel (0xff181818), kPanelHi (0xff1f1f1f), kLabel (0xffe6e6e6), kSmall (0xff9a9a9a);
const juce::Colour kOledFg (0xffeaeaea), kOledMid (0xff8c8c8c), kOledDim (0xff404040);

juce::Font panelFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Avenir Next", h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font oledFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", h, bold ? juce::Font::bold : juce::Font::plain));
}
double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

SpraakEditor::SpraakEditor (SpraakProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    knobs = {
        { "pitch", 88, 287, 59, true, "Pitch", 17, 362 },
        { "formant", 257, 349, 60, true, "Formant", 186, 417 },
        { "inton", 436, 321, 60, true, "Intonation", 352, 390 },
        { "bright", 100, 503, 59, true, "Brightness", 18, 575 },
        { "speed", 257, 572, 60, true, "Speed", 137, 603 },
        { "emotion", 426, 535, 60, true, "Emotion", 351, 603 },
        { "m1", 560, 430, 60, false, "M1", 522, 497 },
        { "m2", 628, 571, 60, false, "M2", 546, 601 },
        { "m3", 776, 474, 60, false, "M3", 686, 524 },
    };
    buttons = {
        { bF1, { 88, 872, 87, 49 }, "F1" }, { bF2, { 203, 872, 87, 49 }, "F2" }, { bF3, { 318, 872, 87, 49 }, "F3" },
        { bF4, { 434, 872, 87, 49 }, "F4" }, { bL, { 631, 872, 87, 49 }, "L" }, { bR, { 747, 872, 87, 49 }, "R" },
        { bShift, { 716, 587, 85, 48 }, "Shift / Exit" },
    };
    jacks = {
        { 210, 180, "Carrier", "CARRIER = SIDECHAIN IN 1" }, { 293, 180, "Modulator", "MODULATOR = SIDECHAIN IN 2" },
        { 378, 180, "Pitch", "PITCH = MIDI NOTE" }, { 461, 180, "CV1", "CV1 = MOD SLOT 1 (MOD PAGE)" },
        { 545, 180, "CV2", "CV2 = MOD SLOT 2 (MOD PAGE)" }, { 628, 180, "CV3", "CV3 = MOD SLOT 3 (MOD PAGE)" },
        { 712, 180, "CV4", "CV4 = MOD SLOT 4 (MOD PAGE)" }, { 795, 180, "Trg In", "TRG IN = MIDI NOTE ON" },
        { 628, 325, "Trg Out", "TRG OUT = MIDI NOTE 60/62" }, { 712, 325, "L Out", "L OUT = AUDIO LEFT" },
        { 795, 325, "R Out", "R OUT = AUDIO RIGHT" },
    };
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (kW / kH);
    setResizeLimits ((int) (kW * 0.45f), (int) (kH * 0.45f), (int) (kW * 1.4f), (int) (kH * 1.4f));
    setSize ((int) (kW * 0.7f), (int) (kH * 0.7f));
    bootUntil = now() + 1.8;
    lastTrig = proc.triggerCount();
    startTimerHz (30);
}

SpraakEditor::~SpraakEditor() { stopTimer(); }

void SpraakEditor::resized()
{
    scale = (float) getWidth() / kW;
    renderBackground();
}

// ------------------------------------------------------------------ static panel
void SpraakEditor::renderBackground()
{
    const int w = std::max (1, getWidth()), h = std::max (1, getHeight());
    const float ds = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 2.f;
    background = juce::Image (juce::Image::ARGB, (int) (w * ds), (int) (h * ds), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale * ds));

    // panel with brushed texture
    g.setColour (kPanel);
    g.fillRect (0.f, 0.f, kW, kH);
    juce::Random r (26);
    for (int i = 0; i < 1400; ++i)
    {
        const float y = r.nextFloat() * kH, x = r.nextFloat() * kW, len = 80.f + r.nextFloat() * 500.f;
        g.setColour ((r.nextBool() ? kPanelHi : juce::Colours::black).withAlpha (0.25f + 0.35f * r.nextFloat()));
        g.drawLine (x, y, x + len, y, 0.6f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), 0, 0, juce::Colours::black.withAlpha (0.08f), kW, kH, false));
    g.fillRect (0.f, 0.f, kW, kH);

    // mounting slots
    auto slot = [&] (float cx, float cy) {
        juce::Rectangle<float> rr (cx - 17, cy - 10, 34, 20);
        g.setColour (juce::Colour (0xff050505)); g.fillRoundedRectangle (rr, 10);
        g.setColour (juce::Colour (0xff2c2c2c)); g.drawRoundedRectangle (rr, 10, 1.2f);
    };
    slot (46, 24); slot (774, 24); slot (46, 1056); slot (774, 1056);

    // title + wordmark
    g.setColour (kLabel);
    g.setFont (panelFont (46, true));
    g.drawText ("Spraakmaker", juce::Rectangle<float> (16, 74, 420, 52), juce::Justification::centredLeft);
    g.setFont (panelFont (23, true));
    g.drawText ("VCE26-S3", juce::Rectangle<float> (18, 136, 200, 28), juce::Justification::centredLeft);
    g.setFont (panelFont (29, false));
    g.drawText ("moddict", juce::Rectangle<float> (640, 88, 152, 34), juce::Justification::centredRight);

    // jacks
    for (auto& j : jacks)
    {
        const float R = 33.f;
        juce::ColourGradient nut (juce::Colour (0xffe8e8e8), j.x - R, j.y - R, juce::Colour (0xff7d7d7d), j.x + R, j.y + R, false);
        g.setGradientFill (nut);
        g.fillEllipse (j.x - R, j.y - R, 2 * R, 2 * R);
        // knurl
        g.setColour (juce::Colour (0xff5a5a5a).withAlpha (0.7f));
        for (int k = 0; k < 36; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / 36.f;
            g.drawLine (j.x + std::cos (a) * (R - 6), j.y + std::sin (a) * (R - 6), j.x + std::cos (a) * R, j.y + std::sin (a) * R, 1.2f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffbdbdbd), j.x, j.y - 20, juce::Colour (0xff6c6c6c), j.x, j.y + 20, false));
        g.fillEllipse (j.x - 22, j.y - 22, 44, 44);
        g.setColour (juce::Colour (0xff070707));
        g.fillEllipse (j.x - 14, j.y - 14, 28, 28);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.drawEllipse (j.x - 14, j.y - 14, 28, 28, 1.5f);
        g.setColour (kSmall);
        g.setFont (panelFont (17));
        g.drawText (j.label, juce::Rectangle<float> (j.x - 60, j.y + 44, 120, 24), juce::Justification::centred);
    }

    // knob labels
    for (auto& k : knobs)
    {
        g.setColour (kSmall);
        g.setFont (panelFont (20));
        g.drawText (k.label, juce::Rectangle<float> (k.lx, k.ly, 200, 26), juce::Justification::centredLeft);
    }
    g.setColour (kSmall); g.setFont (panelFont (20));
    g.drawText ("Data", juce::Rectangle<float> (686, 813, 120, 26), juce::Justification::centredLeft);

    // OLED glass
    g.setColour (juce::Colour (0xff020202));
    g.fillRoundedRectangle (15, 660, 650, 186, 4);
    g.setColour (juce::Colour (0xff303030));
    g.drawRoundedRectangle (15, 660, 650, 186, 4, 1.2f);

    // button labels
    for (auto& b : buttons)
    {
        if (b.id == bShift) { g.setFont (panelFont (17)); g.setColour (kSmall); g.drawText (b.label, juce::Rectangle<float> (b.r.getX() - 4, b.r.getBottom() + 4, 110, 22), juce::Justification::centredLeft); continue; }
        g.setFont (panelFont (17)); g.setColour (kSmall);
        g.drawText (b.label, juce::Rectangle<float> (b.r.getX() + 2, b.r.getBottom() + 8, 60, 22), juce::Justification::centredLeft);
    }

    // USB host port
    {
        juce::Rectangle<float> u (8, 874, 64, 108);
        g.setColour (juce::Colour (0xff9a9a9a)); g.fillRoundedRectangle (u, 3);
        g.setColour (juce::Colour (0xff111111)); g.fillRect (u.reduced (8, 8));
        g.setColour (juce::Colour (0xffd2d2d2)); g.fillRect (juce::Rectangle<float> (u.getX() + 14, u.getY() + 14, 14, u.getHeight() - 28));
        for (int k = 0; k < 4; ++k) { g.setColour (juce::Colour (0xffc8a24a)); g.fillRect (juce::Rectangle<float> (u.getX() + 30, u.getY() + 22 + k * 20, 16, 7)); }
        g.setColour (kSmall); g.setFont (panelFont (17));
        g.drawText ("Host", juce::Rectangle<float> (8, 986, 80, 22), juce::Justification::centredLeft);
    }
    // screws
    for (auto pt : { juce::Point<float> (147, 970), juce::Point<float> (732, 970) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd0d0d0), pt.x - 10, pt.y - 10, juce::Colour (0xff606060), pt.x + 10, pt.y + 10, false));
        g.fillEllipse (pt.x - 11, pt.y - 11, 22, 22);
        g.setColour (juce::Colour (0xff3a3a3a)); g.drawLine (pt.x - 7, pt.y, pt.x + 7, pt.y, 2.f);
    }
}

// ------------------------------------------------------------------ dynamic parts
void SpraakEditor::drawKnob (juce::Graphics& g, const Knob& k)
{
    auto* prm = proc.apvts.getParameter (k.param);
    const float v = prm ? prm->getValue() : 0.f;
    const float a0 = juce::degreesToRadians (-150.f), a1 = juce::degreesToRadians (150.f);
    const float ang = a0 + (a1 - a0) * v;
    const float R = k.r;
    // skirt with ribs
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillEllipse (k.x - R, k.y - R, 2 * R, 2 * R);
    g.setColour (juce::Colour (0xff2e2e2e));
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 48.f;
        g.drawLine (k.x + std::cos (a) * (R - 5), k.y + std::sin (a) * (R - 5), k.x + std::cos (a) * R, k.y + std::sin (a) * R, 1.3f);
    }
    if (k.white)
    {
        const float c = R * 0.71f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), k.x - c, k.y - c, juce::Colour (0xffd8d8d8), k.x + c, k.y + c, false));
        g.fillEllipse (k.x - c, k.y - c, 2 * c, 2 * c);
        g.setColour (juce::Colour (0xffbdbdbd)); g.drawEllipse (k.x - c, k.y - c, 2 * c, 2 * c, 1.f);
        g.setColour (juce::Colour (0xff141414));
        g.drawLine (k.x + std::sin (ang) * c * 0.15f, k.y - std::cos (ang) * c * 0.15f, k.x + std::sin (ang) * c * 0.92f, k.y - std::cos (ang) * c * 0.92f, 4.f);
    }
    else
    {
        const float c = R * 0.84f;
        g.setColour (juce::Colour (0xff3c3c3c)); g.fillEllipse (k.x - c - 2, k.y - c - 2, 2 * c + 4, 2 * c + 4);   // lighter rim
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), k.x - c, k.y - c, juce::Colour (0xff0e0e0e), k.x + c, k.y + c, false));
        g.fillEllipse (k.x - c, k.y - c, 2 * c, 2 * c);
        g.setColour (juce::Colour (0xffeeeeee));
        g.drawLine (k.x + std::sin (ang) * c * 0.35f, k.y - std::cos (ang) * c * 0.35f, k.x + std::sin (ang) * c * 0.9f, k.y - std::cos (ang) * c * 0.9f, 4.f);
    }
}

void SpraakEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    g.addTransform (juce::AffineTransform::scale (scale));
    for (auto& k : knobs) drawKnob (g, k);

    // data encoder (no pointer, knurled)
    {
        const float x = 760, y = 752, R = 60;
        g.setColour (juce::Colour (0xff0b0b0b)); g.fillEllipse (x - R, y - R, 2 * R, 2 * R);
        g.setColour (juce::Colour (0xff3c3c3c)); g.drawEllipse (x - R + 2, y - R + 2, 2 * R - 4, 2 * R - 4, 2.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), x - 45, y - 45, juce::Colour (0xff0c0c0c), x + 45, y + 45, false));
        g.fillEllipse (x - 46, y - 46, 92, 92);
        g.setColour (juce::Colour (0xff353535));
        for (int i = 0; i < 40; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 40.f + (float) dataAccum * 0.02f;
            g.drawLine (x + std::cos (a) * 52, y + std::sin (a) * 52, x + std::cos (a) * 58, y + std::sin (a) * 58, 1.2f);
        }
    }
    // buttons (black, faint outline, lit when pressed / active)
    for (auto& b : buttons)
    {
        const bool active = (b.id >= bF1 && b.id <= bF4 && page == b.id - bF1 + 1) || (b.id == bShift && shift);
        g.setColour (pressedBtn == b.id ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff0b0b0b));
        g.fillRoundedRectangle (b.r, 3);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), b.r.getX(), b.r.getY(), juce::Colours::transparentBlack, b.r.getX(), b.r.getCentreY(), false));
        g.fillRoundedRectangle (b.r, 3);
        g.setColour (active ? juce::Colour (0xff9a9a9a) : juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (b.r, 3, active ? 2.f : 1.2f);
    }
    // round trigger button
    {
        const bool p = pressedBtn == bTrig;
        g.setGradientFill (juce::ColourGradient (juce::Colour (p ? 0xff9a9a9a : 0xffdedede), 570, 955, juce::Colour (0xff8a8a8a), 600, 990, false));
        g.fillEllipse (564, 948, 44, 44);
        g.setColour (juce::Colour (0xff444444)); g.drawEllipse (564, 948, 44, 44, 1.5f);
    }
    // LED
    {
        const bool lit = now() < ledUntil;
        const float x = 651, y = 405;
        if (lit) { g.setColour (juce::Colour (0x66ffd500)); g.fillEllipse (x - 22, y - 22, 44, 44); }
        g.setColour (lit ? juce::Colour (0xffffe14a) : juce::Colour (0xff6b5a10));
        g.fillEllipse (x - 12, y - 12, 24, 24);
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.7f : 0.2f)); g.fillEllipse (x - 6, y - 8, 7, 5);
    }
    // OLED content
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (oled, juce::Rectangle<float> (72, 689, 512, 128), juce::RectanglePlacement::stretchToFit);
}

// ------------------------------------------------------------------ OLED
const std::vector<prm::Item>* SpraakEditor::pageItems() const
{
    switch (page) { case 1: return &prm::kPageTrg; case 2: return &prm::kPageVoice; case 3: return &prm::kPageFx; case 4: return &prm::kPageMod; default: return nullptr; }
}

juce::String SpraakEditor::itemValue (const prm::Item& it) const
{
    auto* p = proc.apvts.getParameter (it.id);
    if (! p) return "-";
    const juce::String id (it.id);
    if (id == "micro" && (int) proc.apvts.getRawParameterValue ("micro")->load() == 5 && proc.sclName().isNotEmpty()) return proc.sclName().substring (0, 10).toUpperCase();
    if (id == "root") return juce::MidiMessage::getMidiNoteName ((int) proc.apvts.getRawParameterValue ("root")->load(), true, true, 3);
    if (id == "chord") return prm::kChord[(int) proc.apvts.getRawParameterValue ("chord")->load() - 1];
    if (id == "start" || id == "width" || id == "dlymix" || id == "revsize" || id == "revmix" || id == "level" || id == "glide")
        return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (it.id)->load() * 100.f)) + "%";
    if (id.endsWith ("amt")) return juce::String (proc.apvts.getRawParameterValue (it.id)->load(), 2);
    if (id.endsWith ("rate") && id.startsWith ("lfo")) return juce::String (proc.apvts.getRawParameterValue (it.id)->load(), 2) + "HZ";
    if (id == "fine") return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("fine")->load())) + "C";
    if (id == "dlyfb") return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("dlyfb")->load() * 100.f)) + "%";
    return p->getCurrentValueAsText().toUpperCase();
}

void SpraakEditor::renderOled()
{
    oled.clear (oled.getBounds(), juce::Colours::black);
    juce::Graphics g (oled);
    const double t = now();
    const auto d = proc.display();
    auto tabs = [&] {
        const char* names[4] = { "TRG/SRC", "VOICE", "FX", "MOD" };
        for (int i = 0; i < 4; ++i)
        {
            juce::Rectangle<float> r (1.f + i * 63.f, 52.f, 61.f, 11.f);
            const bool on = page == i + 1;
            g.setColour (on ? kOledFg : kOledMid);
            if (on) g.fillRect (r); else g.drawRect (r, 1.f);
            g.setColour (on ? juce::Colours::black : kOledFg);
            g.setFont (oledFont (8.5f, true));
            g.drawText (names[i], r.withTrimmedLeft (3), juce::Justification::centredLeft);
        }
    };
    if (page == 0)
    {
        // top line: trigger / source / language (or preset browse / flash / typing)
        g.setFont (oledFont (9.f, true));
        g.setColour (kOledFg);
        juce::String top;
        auto choice = [&] (const char* id) { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
        top = choice ("trig").toLowerCase().replace ("ph.", "Ph.").replaceCharacter ('a', 'a');
        top = juce::String (prm::kTrig[(int) proc.apvts.getRawParameterValue ("trig")->load()]).toLowerCase();
        top = top.substring (0, 1).toUpperCase() + top.substring (1) + " / "
            + juce::String (prm::kSource[(int) proc.apvts.getRawParameterValue ("source")->load()]).toLowerCase().replace ("keyb", "keyboard").replace ("dict", "word")
            + " / " + juce::String (prm::kLang[(int) proc.apvts.getRawParameterValue ("lang")->load()]);
        if (browsePreset >= 0) top = juce::String::formatted ("P%02d ", browsePreset + 1) + proc.presetName (browsePreset) + "  [push = load]";
        else top = juce::String::formatted ("P%02d  ", proc.getCurrentProgram() + 1) + top;
        if (t < flashUntil) top = flashMsg;
        g.drawText (top, juce::Rectangle<float> (3, 1, 200, 11), juce::Justification::centredLeft);

        // big word
        juce::String big = juce::String::fromUTF8 (d.word);
        if (t < bootUntil || big.isEmpty()) big = "spraakmaker";
        if (t < typedUntil) big = typed + ((int) (t * 3) % 2 ? "_" : " ");
        float fh = 30.f;
        g.setFont (oledFont (fh, true));
        while (fh > 12.f && juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), big) > 168.f) { fh -= 1.f; g.setFont (oledFont (fh, true)); }
        g.setColour (kOledFg);
        g.drawText (big, juce::Rectangle<float> (3, 12, 172, 36), juce::Justification::centredLeft);

        // phoneme readout
        g.setFont (oledFont (8.f));
        g.setColour (kOledMid);
        if (t >= bootUntil) g.drawText (juce::String ("/") + juce::String::fromUTF8 (sm::phon (d.phon).id) + "/", juce::Rectangle<float> (178, 1, 40, 10), juce::Justification::centredLeft);

        // Sanne (pixel tutor): mouth follows the phoneme being spoken, blinks now and then
        {
            namespace sn = sm::sanne;
            const int x0 = 208, y0 = 9;
            const float lvl = juce::jlimit (0.f, 1.f, proc.meter() * 9.f);
            const bool blink = std::fmod (t, 3.7) < 0.13;
            const auto& spr = blink ? sn::eyesClosed : sn::eyesOpen;
            for (int py = 0; py < sn::H; ++py)
                for (int px = 0; px < sn::W; ++px)
                {
                    const char c = spr[py][px];
                    const int v = c <= '9' ? c - '0' : c - 'a' + 10;
                    if (v <= 0) continue;
                    g.setColour (juce::Colour::greyLevel ((float) v / 15.f));
                    g.fillRect (x0 + px, y0 + py, 1, 1);
                }
            const auto vis = (t < bootUntil || lvl < 0.06f) ? sn::Rest : sn::visemeFor (sm::phon (d.phon).id);
            const auto& m = sn::mouths[vis];
            const int mx = x0 + sn::mouthX - m.w / 2, my = y0 + sn::mouthY - m.h / 2;
            for (int r = 0; r < m.h; ++r)
                for (int c = 0; c < m.w; ++c)
                {
                    const char k = m.rows[r][c];
                    if (k == '.') continue;
                    g.setColour (k == 'L' ? juce::Colour::greyLevel (0.42f) : k == 'T' ? juce::Colours::white : juce::Colours::black);
                    g.fillRect (mx + c, my + r, 1, 1);
                }
        }
        tabs();
        return;
    }
    // ---- parameter page: 4 items (2x2) per screen ----
    auto* items = pageItems();
    const int N = (int) items->size();
    sel = juce::jlimit (0, N - 1, sel);
    const int first = (sel / 4) * 4;
    g.setFont (oledFont (8.f, true));
    g.setColour (kOledMid);
    const char* titles[5] = { "", "TRIGGER / SOURCE", "VOICE", "EFFECTS", "MODULATION" };
    g.drawText (juce::String (titles[page]) + (shift ? "  [SHIFT]" : ""), juce::Rectangle<float> (3, 0, 180, 9), juce::Justification::centredLeft);
    g.drawText (juce::String (first / 4 + 1) + "/" + juce::String ((N + 3) / 4), juce::Rectangle<float> (220, 0, 33, 9), juce::Justification::centredRight);
    if (t < flashUntil)
    {
        g.setColour (kOledFg); g.setFont (oledFont (9.f, true));
        g.drawText (flashMsg, juce::Rectangle<float> (3, 20, 250, 14), juce::Justification::centred);
        tabs(); return;
    }
    for (int k = 0; k < 4 && first + k < N; ++k)
    {
        const auto& it = (*items)[(size_t) (first + k)];
        juce::Rectangle<float> r (1.f + (k % 2) * 127.f, 10.f + (k / 2) * 21.f, 126.f, 20.f);
        const bool s = first + k == sel;
        if (s) { g.setColour (kOledFg); g.fillRect (r); }
        else { g.setColour (kOledDim); g.drawRect (r, 1.f); }
        g.setColour (s ? juce::Colours::black : kOledMid);
        g.setFont (oledFont (7.5f, true));
        g.drawText (it.label, r.withTrimmedLeft (3).withHeight (9), juce::Justification::centredLeft);
        g.setColour (s ? juce::Colours::black : kOledFg);
        g.setFont (oledFont (11.f, true));
        g.drawText (itemValue (it), r.withTrimmedLeft (3).withTrimmedTop (8), juce::Justification::centredLeft);
    }
    tabs();
}

void SpraakEditor::timerCallback()
{
    const int tc = proc.triggerCount();
    if (tc != lastTrig) { lastTrig = tc; ledUntil = now() + 0.08; }
    renderOled();
    repaint();
}

void SpraakEditor::flash (const juce::String& msg, double secs) { flashMsg = msg; flashUntil = now() + secs; }

// ------------------------------------------------------------------ interaction
void SpraakEditor::adjustItem (int index, int steps, bool fine)
{
    auto* items = pageItems();
    if (! items || index < 0 || index >= (int) items->size()) return;
    auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter ((*items)[(size_t) index].id));
    if (! p) return;
    const auto& range = p->getNormalisableRange();
    float v = p->convertFrom0to1 (p->getValue());
    const bool discrete = dynamic_cast<juce::AudioParameterChoice*> (p) || dynamic_cast<juce::AudioParameterInt*> (p) || dynamic_cast<juce::AudioParameterBool*> (p);
    if (discrete) v += (float) steps;
    else
    {
        // step in normalised space so skewed ranges feel even
        float nv = p->getValue() + (float) steps * (fine ? 0.002f : 0.01f);
        v = p->convertFrom0to1 (juce::jlimit (0.f, 1.f, nv));
    }
    v = juce::jlimit (range.start, range.end, v);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    p->endChangeGesture();
}

void SpraakEditor::dataTurn (int steps)
{
    if (steps == 0) return;
    if (page == 0)
    {
        browsePreset = juce::jlimit (0, 99, (browsePreset < 0 ? proc.getCurrentProgram() : browsePreset) + steps);
        return;
    }
    adjustItem (sel, steps, shift);
}

void SpraakEditor::dataPush()
{
    if (page == 0)
    {
        if (shift)
        {
            const int slot = proc.getCurrentProgram();
            juce::String name = proc.presetName (slot);
            if (name == "INIT") name = "USER " + juce::String (slot + 1);
            proc.savePreset (slot, name);
            flash (juce::String::formatted ("SAVED P%02d ", slot + 1) + name);
            shift = false;
            return;
        }
        if (browsePreset >= 0) { proc.loadPreset (browsePreset); flash (juce::String::formatted ("LOADED P%02d ", browsePreset + 1) + proc.presetName (browsePreset)); browsePreset = -1; }
        else proc.manualTrigger();
        return;
    }
    auto* items = pageItems();
    const juce::String id ((*items)[(size_t) sel].id);
    if (id == "list" || id == "source") editList();
    else if (id == "micro") loadScl();
}

void SpraakEditor::pressButton (int id)
{
    switch (id)
    {
        case bF1: case bF2: case bF3: case bF4:
        {
            const int pg = id - bF1 + 1;
            if (shift && pg == 1) { shift = false; editList(); break; }
            if (shift && pg == 2) { shift = false; loadScl(); break; }
            page = page == pg ? 0 : pg; sel = 0; browsePreset = -1;
            break;
        }
        case bL: if (page) sel = juce::jmax (0, sel - 1); else dataTurn (-1); break;
        case bR: if (page) { auto* it = pageItems(); sel = juce::jmin ((int) it->size() - 1, sel + 1); } else dataTurn (1); break;
        case bShift:
            if (page != 0 && ! shift) { page = 0; browsePreset = -1; }
            else shift = ! shift;
            break;
        case bTrig: proc.manualTrigger(); break;
        default: break;
    }
}

void SpraakEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = toPanel (e.position);
    for (int i = 0; i < (int) knobs.size(); ++i)
        if (p.getDistanceFrom ({ knobs[(size_t) i].x, knobs[(size_t) i].y }) < knobs[(size_t) i].r)
        {
            dragKnob = i; dragStartY = p.y;
            auto* prm = proc.apvts.getParameter (knobs[(size_t) i].param);
            dragStartVal = prm->getValue(); prm->beginChangeGesture();
            return;
        }
    if (p.getDistanceFrom ({ 760, 752 }) < 60) { dataDrag = true; dataMoved = false; dataLastY = p.y; return; }
    for (auto& b : buttons) if (b.r.contains (p)) { pressedBtn = b.id; pressButton (b.id); return; }
    if (p.getDistanceFrom ({ 586, 970 }) < 24) { pressedBtn = bTrig; pressButton (bTrig); return; }
    for (auto& j : jacks)
        if (p.getDistanceFrom ({ j.x, j.y }) < 34)
        {
            juce::String info (j.info);
            if (juce::String (j.label) == "Carrier") info << (proc.externalCarrier() ? "  (SIGNAL)" : "  (NO SIGNAL)");
            if (juce::String (j.label) == "Modulator") info << (proc.externalModulator() ? "  (SIGNAL)" : "  (NO SIGNAL)");
            flash (info, 2.5); return;
        }
    // OLED
    juce::Rectangle<float> oledR (72, 689, 512, 128);
    if (oledR.contains (p))
    {
        const float ox = (p.x - 72) / 2.f, oy = (p.y - 689) / 2.f;
        if (oy >= 52) { const int tab = juce::jlimit (0, 3, (int) (ox / 63.f)); page = page == tab + 1 ? 0 : tab + 1; sel = 0; return; }
        if (page == 0) { proc.manualTrigger(); return; }
        if (oy >= 10 && oy < 52)
        {
            const int k = (oy >= 31 ? 2 : 0) + (ox >= 128 ? 1 : 0);
            const int idx = (sel / 4) * 4 + k;
            if (idx < (int) pageItems()->size()) { sel = idx; oledDragItem = idx; oledLastY = p.y; oledAccum = 0; }
        }
    }
}

void SpraakEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = toPanel (e.position);
    if (dragKnob >= 0)
    {
        auto* prm = proc.apvts.getParameter (knobs[(size_t) dragKnob].param);
        const float sens = e.mods.isShiftDown() ? 1200.f : 300.f;
        prm->setValueNotifyingHost (juce::jlimit (0.f, 1.f, dragStartVal + (dragStartY - p.y) / sens));
        return;
    }
    if (dataDrag)
    {
        dataAccum += (dataLastY - p.y); dataLastY = p.y;
        while (dataAccum > 14) { dataTurn (1); dataAccum -= 14; dataMoved = true; }
        while (dataAccum < -14) { dataTurn (-1); dataAccum += 14; dataMoved = true; }
        return;
    }
    if (oledDragItem >= 0)
    {
        oledAccum += (oledLastY - p.y); oledLastY = p.y;
        while (oledAccum > 6) { adjustItem (oledDragItem, 1, e.mods.isShiftDown()); oledAccum -= 6; }
        while (oledAccum < -6) { adjustItem (oledDragItem, -1, e.mods.isShiftDown()); oledAccum += 6; }
    }
}

void SpraakEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragKnob >= 0) proc.apvts.getParameter (knobs[(size_t) dragKnob].param)->endChangeGesture();
    if (dataDrag && ! dataMoved) dataPush();
    dragKnob = -1; dataDrag = false; pressedBtn = 0; oledDragItem = -1;
}

void SpraakEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = toPanel (e.position);
    for (auto& k : knobs)
        if (p.getDistanceFrom ({ k.x, k.y }) < k.r)
        {
            auto* prm = proc.apvts.getParameter (k.param);
            prm->setValueNotifyingHost (prm->getDefaultValue());
            return;
        }
    if (juce::Rectangle<float> (72, 689, 512, 104).contains (p) && page != 0) dataPush();
}

void SpraakEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto p = toPanel (e.position);
    const int steps = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0;
    for (auto& k : knobs)
        if (p.getDistanceFrom ({ k.x, k.y }) < k.r)
        {
            auto* prm = proc.apvts.getParameter (k.param);
            prm->setValueNotifyingHost (juce::jlimit (0.f, 1.f, prm->getValue() + w.deltaY * (e.mods.isShiftDown() ? 0.05f : 0.2f)));
            return;
        }
    if (p.getDistanceFrom ({ 760, 752 }) < 60) { dataTurn (steps); dataAccum += (float) steps * 10.f; return; }
    if (juce::Rectangle<float> (72, 689, 512, 104).contains (p) && page != 0)
    {
        const float ox = (p.x - 72) / 2.f, oy = (p.y - 689) / 2.f;
        const int k = (oy >= 31 ? 2 : 0) + (ox >= 128 ? 1 : 0);
        const int idx = (sel / 4) * 4 + k;
        if (idx < (int) pageItems()->size()) { sel = idx; adjustItem (idx, steps, e.mods.isShiftDown()); }
    }
}

// USB "Host": typing on the computer keyboard
bool SpraakEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey)
    {
        if (typed.isNotEmpty()) { proc.typeAndSpeak (typed); flash ("> " + typed.toUpperCase(), 1.5); }
        else proc.manualTrigger();
        typed.clear(); typedUntil = 0;
        return true;
    }
    if (k == juce::KeyPress::escapeKey) { typed.clear(); typedUntil = 0; if (page) page = 0; return true; }
    if (k == juce::KeyPress::backspaceKey) { typed = typed.dropLastCharacters (1); typedUntil = now() + 6; return true; }
    if (k == juce::KeyPress::leftKey) { pressButton (bL); return true; }
    if (k == juce::KeyPress::rightKey) { pressButton (bR); return true; }
    if (k == juce::KeyPress::upKey) { dataTurn (1); return true; }
    if (k == juce::KeyPress::downKey) { dataTurn (-1); return true; }
    const juce::juce_wchar c = k.getTextCharacter();
    if (c >= 32 && ! k.getModifiers().isCommandDown() && typed.length() < 120)
    {
        typed += juce::String::charToString (c);
        typedUntil = now() + 6;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ dialogs
void SpraakEditor::editList()
{
    const int n = (int) proc.apvts.getRawParameterValue ("list")->load();
    auto* ed = new juce::TextEditor();
    ed->setMultiLine (true, true); ed->setReturnKeyStartsNewLine (true);
    ed->setFont (juce::Font (juce::FontOptions ("Menlo", 14.f, juce::Font::plain)));
    ed->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101010));
    ed->setColour (juce::TextEditor::textColourId, kLabel);
    ed->setText (proc.planner().getList (n).joinIntoString ("\n"));
    ed->setSize (420, 360);
    struct Holder : juce::Component
    {
        Holder (juce::TextEditor* e, std::function<void (juce::String)> onSave) : editor (e)
        {
            addAndMakeVisible (editor.get());
            save.setButtonText ("Save"); cancel.setButtonText ("Cancel");
            addAndMakeVisible (save); addAndMakeVisible (cancel);
            save.onClick = [this, onSave] { onSave (editor->getText()); close(); };
            cancel.onClick = [this] { close(); };
            setSize (440, 420);
        }
        void close() { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            auto b = r.removeFromBottom (32);
            save.setBounds (b.removeFromRight (90)); b.removeFromRight (8); cancel.setBounds (b.removeFromRight (90));
            r.removeFromBottom (8); editor->setBounds (r);
        }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff181818)); }
        std::unique_ptr<juce::TextEditor> editor;
        juce::TextButton save, cancel;
    };
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Holder (ed, [this, n] (juce::String text) {
        juce::StringArray a; a.addLines (text); a.trim(); a.removeEmptyStrings();
        proc.planner().setList (n, a);
        flash (juce::String::formatted ("LIST %02d SAVED (", n) + juce::String (a.size()) + ")");
    }));
    o.dialogTitle = juce::String::formatted ("Spraakmaker - list %02d (one entry per line)", n);
    o.dialogBackgroundColour = juce::Colour (0xff181818);
    o.useNativeTitleBar = true; o.resizable = true;
    o.launchAsync();
}

void SpraakEditor::loadScl()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Scala tuning (.scl)", juce::File(), "*.scl");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.existsAsFile()) flash (proc.loadScl (f) ? "SCL: " + proc.sclName().toUpperCase() : "SCL NOT READABLE");
    });
}
