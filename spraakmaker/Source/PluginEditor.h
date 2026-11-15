// Spraakmaker — the 20HP panel (dark edition), drawn in vector at the module's proportions, with a working OLED menu.
#pragma once
#include "PluginProcessor.h"

class SpraakEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpraakEditor (SpraakProcessor&);
    ~SpraakEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void showPage (int pg) { page = pg; sel = 0; renderOled(); }   // tests / snapshots
    static constexpr float kW = 850.f, kH = 1080.f;   // panel coordinate space (20HP photo proportions)

private:
    struct Knob { const char* param; float x, y, r; bool white; const char* label; float lx, ly; };
    struct Btn { int id; juce::Rectangle<float> r; const char* label; };
    struct Jack { float x, y; const char* label; const char* info; };
    enum BtnId { bF1 = 1, bF2, bF3, bF4, bL, bR, bShift, bTrig };

    void timerCallback() override;
    void renderBackground();
    void renderOled();
    void drawKnob (juce::Graphics&, const Knob&);
    juce::Point<float> toPanel (juce::Point<float> p) const { return p / scale; }
    const std::vector<sm::prm::Item>* pageItems() const;
    void pressButton (int id);
    void dataTurn (int steps);
    void dataPush();
    void adjustItem (int index, int steps, bool fine);
    juce::String itemValue (const sm::prm::Item&) const;
    void flash (const juce::String& msg, double secs = 2.0);
    void editList();
    void loadScl();

    SpraakProcessor& proc;
    float scale = 0.7f;
    juce::Image background;
    juce::Image oled { juce::Image::ARGB, 256, 64, true };
    std::vector<Knob> knobs;
    std::vector<Btn> buttons;
    std::vector<Jack> jacks;

    // interaction
    int dragKnob = -1; float dragStartY = 0, dragStartVal = 0;
    int pressedBtn = 0;
    bool dataDrag = false, dataMoved = false; float dataAccum = 0, dataLastY = 0;
    int oledDragItem = -1; float oledLastY = 0, oledAccum = 0;

    // OLED state
    int page = 0;                // 0 home, 1..4 pages
    int sel = 0;                 // selected item on a page
    bool shift = false;
    int browsePreset = -1;       // preset browse on home
    juce::String flashMsg; double flashUntil = 0;
    juce::String typed; double typedUntil = 0;
    int lastTrig = 0; double ledUntil = 0;
    double bootUntil = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpraakEditor)
};
