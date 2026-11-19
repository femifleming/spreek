// Spreekbuis 2 — the window, in the design language of the faltung suite (flat fields, thin lines, folded strips,
// parallelogram sliders, lowercase Inter + IBM Plex Mono) and in Spreekbuis's own colours.
#pragma once
#include "Art.h"

class SpreekbuisEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SpreekbuisEditor(SpreekbuisProcessor&);
    ~SpreekbuisEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    static constexpr int W = 1100, H = 780;

    // tests / screenshots
    void showPage(int page);
    void tickForTest();
    void typeForTest(const juce::String& t);

private:
    void timerCallback() override;
    SpreekbuisProcessor& proc;
    struct Look;
    std::unique_ptr<Look> look;
    struct Content;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tips { this, 700 };
    double typedUntil = 0;
};
