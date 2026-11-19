// The component drawing code of the faltung suite (github.com/femifleming/faltung, Source/Ui.*), copied so that this
// repository stands on its own: flat colour fields, lowercase Inter + IBM Plex Mono, skewed-parallelogram sliders and
// toggles. Here it wears Spreekbuis's colours (the palette below is Spreekbuis 1.0's, nothing else).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace sbui
{
/** UTF-8 literal → String (literals with "·", "–", "…") */
inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// ---- type -----------------------------------------------------------------------------------------
juce::Font sans (float size, bool medium = false);
juce::Font mono (float size, bool medium = false);

// ---- Spreekbuis's colours (1.0: Tongval's dark palette) -------------------------------------------------
const juce::Colour paper { 0xff141413 },  // ground
    card { 0xff1d1c1a },                  // a second, slightly lifted field
    ink { 0xfff1ede4 },                   // text, edges
    mute { 0xff9b968c },                  // quiet text
    soft { 0xff3a3833 },                  // rules
    hover { 0xff2a2926 },
    orange { 0xffff6a00 }, navy { 0xff1b2a6b }, blue { 0xff3d74d6 }, ok { 0xff1f8a4c },
    yellow { 0xfff4c20d }, dark { 0xff111111 };

/** text colour for a ground */
inline juce::Colour inkFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > 0.42f ? dark : ink;
}
/** the slider "edge" mark: white on mid grounds, ink on pale grounds */
inline juce::Colour edgeFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > 0.86f ? inkFor (ground) : ink;
}

struct Theme
{
    juce::Colour ground, ink, fill, edge;
};
/** the one theme: Spreekbuis's ground, ink and orange */
inline Theme spreekTheme() { return { paper, ink, orange, ink }; }

// ---- a parallelogram, the house shape -------------------------------------------------------------
juce::Path skewRect (juce::Rectangle<float> r, float skew = 0.55f);

// ---- parameter slider: label · thin rule · slanted fill · white edge · value ------------------------
class ParamSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamSlider (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setFill (juce::Colour c) { theme.fill = c; repaint(); }
    void setLabelWidth (int w) { labelW = w; resized(); repaint(); }
    void setTheme (Theme t) { theme = t; repaint(); }
    /** the value as text, when the parameter's own is not what the page wants (note names, "tongval") */
    std::function<juce::String()> valueText;
    /** where modulation has moved it (0..1), drawn as a hollow tick; < 0: none */
    void setGhost (float g) { if (std::abs (g - ghost) > 0.002f) { ghost = g; repaint(); } }
    std::function<void()> onTouch;
    juce::RangedAudioParameter& param;

private:
    Theme theme;
    juce::String label;
    int labelW = 78;
    float value01 = 0, dragStart = 0, ghost = -1.0f;
    bool dragging = false;
    juce::ParameterAttachment attach;
    juce::Rectangle<float> track() const;
};

// ---- choice: "label  value" — click for a menu, drag to step ----------------------------------------
class ChoiceBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceBox (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::RangedAudioParameter& param;
    bool asTabs = false;   // draw every option as a row of tabs
    int labelW = 78;
    void setTheme (Theme t) { theme = t; repaint(); }

private:
    Theme theme; juce::String label;
    int index = 0, dragStartIndex = 0; bool moved = false;
    juce::ParameterAttachment attach;
    void set (int i);
    int numItems() const;
};

// ---- toggle: small parallelogram, filled when on --------------------------------------------------
class ToggleBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleBox (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setTheme (Theme t) { theme = t; repaint(); }
    juce::RangedAudioParameter& param;

private:
    Theme theme; juce::String label; bool on = false;
    juce::ParameterAttachment attach;
};

// ---- text button in the house style ----------------------------------------------------------------
class TextLink : public juce::Component, public juce::SettableTooltipClient
{
public:
    TextLink (juce::String t, Theme th) : text (std::move (t)), theme (th) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void setText (juce::String t) { text = std::move (t); repaint(); }
    void setLit (bool l) { lit = l; repaint(); }
    std::function<void()> onClick;
    juce::String text; Theme theme; bool lit = false;
};

// ---- preset bar: ‹ name › ---------------------------------------------------------------------------
class PresetBar : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    PresetBar (juce::AudioProcessor& p, Theme t);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override { if (p.getCurrentProgram() != shown) { shown = p.getCurrentProgram(); repaint(); } }
    juce::AudioProcessor& p; Theme theme; int shown = -1;
};
} // namespace sbui
