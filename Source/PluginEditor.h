#pragma once
#include "PluginProcessor.h"

namespace sbui {
// dark only (Tongval's dark palette)
const juce::Colour paper { 0xff141413 },  // background
    card { 0xff1d1c1a },                  // panels
    ink { 0xfff1ede4 },                   // text
    mute { 0xff9b968c },                  // muted text
    soft { 0xff3a3833 },                  // lines / borders
    hover { 0xff2a2926 }, heavy { 0xff000000 },
    orange { 0xffff6a00 }, navy { 0xff1b2a6b }, blue { 0xff3d74d6 }, ok { 0xff1f8a4c },
    yellow { 0xfff4c20d }, dark { 0xff111111 };
juce::Font sans(float h, bool bold = false);
juce::Font mono(float h);

struct Look : juce::LookAndFeel_V4 {
    Look();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return sans(13); }
    juce::Font getPopupMenuFont() override { return sans(14); }
    juce::Font getLabelFont(juce::Label&) override { return sans(12); }
};
} // namespace sbui

class SpreekbuisEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SpreekbuisEditor(SpreekbuisProcessor&);
    ~SpreekbuisEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1180, H = 700;

private:
    void timerCallback() override;
    SpreekbuisProcessor& proc;
    sbui::Look look;
    struct Content;
    std::unique_ptr<Content> content;
};
