// The picture at the top of the window, in the manner of the faltung suite: one flat colour field, a thin wireframe
// figure (a speaking tube that breathes with the sound), folded strips with mono codes (the pages: one per layer) and
// a flat navy field where the words are read along (the karaoke line) and Sanne mouths what the speech computer says.
#pragma once
#include "Ui.h"
#include "Processor.h"

namespace sbui
{
/** the snd-style folded strip chain (faltung's Fold, drawn in Spreekbuis's colours) */
struct Fold
{
    static juce::Colour palette (int i);
    /** draws one folded segment: a front face + a darker fold, returns the end point; the face comes back in `faceOut` */
    static juce::Point<float> segment (juce::Graphics&, juce::Point<float> start, float len, float depth, bool up,
                                       juce::Colour c, const juce::String& label, juce::Colour ink, bool outline, float glow, juce::Path* faceOut = nullptr);
};

struct PageInfo { const char* code; const char* name; };
inline constexpr int kPages = 8;
inline const PageInfo kPageInfo[kPages] = { { "lp1", "loop" }, { "wk2", "wolk" }, { "kl3", "klinker" }, { "gg4", "g" },
                                            { "sp5", "spraak" }, { "fx6", "fx" }, { "md7", "mod" }, { "oc8", "orca" } };

class Art : public juce::Component
{
public:
    explicit Art (SpreekbuisProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void tick();                          // ~30 Hz from the editor
    int page = 0;                         // the strip that is outlined
    std::function<void (int)> onPage;     // a strip was clicked
    juce::String typed;                   // what is being typed for the speech computer
    bool typing = false;

private:
    SpreekbuisProcessor& p;
    float time = 0, flash = 0, level = 0, mouth = 0;
    int seenOnsets = 0, seenTrig = 0;
    float trigFlash = 0;
    std::array<float, kPages> glow {};
    std::array<juce::Path, kPages> faces;
    void paintTube (juce::Graphics&, juce::Rectangle<float>);
    void paintStrips (juce::Graphics&);
    void paintKaraoke (juce::Graphics&, juce::Rectangle<float>);
    void paintSanne (juce::Graphics&, float x, float y, float px);
    juce::Rectangle<float> karaokeField() const;
};
} // namespace sbui
