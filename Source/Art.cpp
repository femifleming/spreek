#include "Art.h"
#include "Sanne.h"
#include "Phonemes.h"

namespace sbui
{
using juce::Colour;
using juce::Point;
using juce::Rectangle;

// one colour per page, all of them Spreekbuis's: blue, paper-white, orange, yellow, green, grey, navy, line-grey
juce::Colour Fold::palette (int i)
{
    static const juce::Colour c[8] = { blue, ink, orange, yellow, ok, mute, navy, soft.brighter (0.25f) };
    return c[((i % 8) + 8) % 8];
}

// (faltung, Source/Art.cpp)
Point<float> Fold::segment (juce::Graphics& g, Point<float> s, float len, float depth, bool up, Colour c,
                            const juce::String& label, Colour inkC, bool outline, float glow, juce::Path* faceOut)
{
    // flat run: along x, width leans back; rising run: up-right, width along x
    const Point<float> D = up ? Point<float> (len * 0.55f, -len * 0.82f) : Point<float> (len, 0.0f);
    const Point<float> W = up ? Point<float> (depth, 0.0f) : Point<float> (depth * 0.62f, -depth * 0.78f);
    juce::Path face;
    face.startNewSubPath (s); face.lineTo (s + D); face.lineTo (s + D + W); face.lineTo (s + W); face.closeSubPath();
    const Colour fc = c.interpolatedWith (juce::Colours::white, juce::jlimit (0.0f, 0.6f, glow * 0.6f));
    g.setColour (fc);
    g.fillPath (face);
    // the fold: a darker sliver along the far edge reads as the paper turning
    juce::Path fold;
    const Point<float> F = up ? Point<float> (depth * 0.45f, depth * 0.35f) : Point<float> (0.0f, depth * 0.42f);
    fold.startNewSubPath (s + D); fold.lineTo (s + D + F); fold.lineTo (s + D + W + F * 0.0f); fold.closeSubPath();
    g.setColour (fc.darker (0.28f));
    g.fillPath (fold);
    if (outline)
    {
        g.setColour (juce::Colours::white);
        g.strokePath (face, juce::PathStrokeType (2.0f));
    }
    if (label.isNotEmpty())
    {
        const float ang = std::atan2 (D.y, D.x);
        const auto mid = s + D * 0.5f + W * 0.5f;
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (ang, mid.x, mid.y));
        g.setColour (inkC);
        g.setFont (sans (15.0f));
        g.drawText (label, Rectangle<float> (mid.x - 60, mid.y - 10, 120, 20), juce::Justification::centred, false);
    }
    if (faceOut) *faceOut = face;
    return s + D;
}

Art::Art (SpreekbuisProcessor& proc) : p (proc)
{
    seenOnsets = p.onsetCount.load(); seenTrig = p.triggerCount();
}

Rectangle<float> Art::karaokeField() const { return { (float) getWidth() - 36.0f - 424.0f, 44.0f, 424.0f, 196.0f }; }

void Art::tick()
{
    time += 1.0f / 30.0f;
    const int o = p.onsetCount.load();
    if (o != seenOnsets) { seenOnsets = o; flash = 1.0f; } else flash *= 0.82f;
    const int t = p.triggerCount();
    if (t != seenTrig) { seenTrig = t; trigFlash = 1.0f; } else trigFlash *= 0.8f;
    const float lv = std::sqrt (juce::jlimit (0.0f, 1.0f, p.outLevel.load()));
    level += (lv - level) * (lv > level ? 0.4f : 0.08f);
    const float m = juce::jlimit (0.0f, 1.0f, p.meter() * 9.0f);
    mouth += (m - mouth) * 0.5f;
    auto lvl = [this] (int k) { return std::sqrt (juce::jlimit (0.0f, 1.0f, p.layerLevel[(size_t) k].load() * 1.6f)); };
    const float target[kPages] = { lvl (0), std::max (lvl (1), lvl (2)), lvl (3), lvl (4), lvl (5), lvl (5) * 0.5f,
                                   std::abs (p.lfoShown[0].load()) * 0.25f, flash * 0.0f };
    for (int i = 0; i < kPages; ++i) glow[(size_t) i] += (target[i] - glow[(size_t) i]) * 0.5f;
    repaint();
}

void Art::mouseMove (const juce::MouseEvent& e)
{
    bool over = false;
    for (auto& f : faces) over |= f.contains (e.position);
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Art::mouseDown (const juce::MouseEvent& e)
{
    for (int i = kPages - 1; i >= 0; --i)
        if (faces[(size_t) i].contains (e.position)) { if (onPage) onPage (i); return; }
    if (karaokeField().contains (e.position)) p.manualTrigger();    // the field is the speech computer's trigger button
}

void Art::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (paper);
    g.fillRect (b);
    paintTube (g, b);
    paintStrips (g);
    // title, as on the suite's Fell pages: white name, ink line under it
    g.setColour (juce::Colours::white);
    g.setFont (sans (26.0f, true));
    g.drawText ("SPREEKBUIS", Rectangle<float> (36, 20, 300, 32), juce::Justification::centredLeft);
    g.setColour (ink);
    g.setFont (sans (13.0f));
    g.drawText ("NEDERLANDS ALS INSTRUMENT", Rectangle<float> (36, 50, 300, 18), juce::Justification::centredLeft);
    paintKaraoke (g, karaokeField());
}

// ---- the speaking tube: thin lines, white faces that move with the sound -------------------------------
void Art::paintTube (juce::Graphics& g, Rectangle<float> b)
{
    const float x0 = b.getX() + 250.0f;
    const Point<float> A (x0 + 40.0f, 196.0f), M (x0 + 190.0f, 140.0f), H (x0 + 330.0f, 92.0f);
    const float cloud = *p.apvts.getRawParameterValue ("cloud_on") > 0.5f ? juce::jlimit (0.0f, 1.0f, *p.apvts.getRawParameterValue ("cloud_density") / 40.0f) : 0.0f;
    const float px = p.padX.load(), py = p.padY.load();
    constexpr int N = 7;
    static const float ang[N] = { 200, 250, 300, 345, 30, 95, 150 };
    std::array<Point<float>, N> r0, r1, r2;
    for (int i = 0; i < N; ++i)
    {
        const float a = juce::degreesToRadians (ang[i]);
        const float wob = 1.0f + 0.16f * cloud * std::sin ((float) i * 2.3f + time * 0.9f);
        r0[(size_t) i] = A + Point<float> (std::cos (a) * 9.0f, std::sin (a) * 17.0f);
        r1[(size_t) i] = M + Point<float> (std::cos (a) * 22.0f * wob, std::sin (a) * 40.0f * wob) + Point<float> ((px - 0.5f) * 26.0f, (py - 0.5f) * 20.0f);
        const float open = (1.0f + 0.22f * level) * wob;
        r2[(size_t) i] = H + Point<float> (std::cos (a) * 46.0f * open, std::sin (a) * 86.0f * open);
    }
    g.setColour (orange.withAlpha (0.85f));
    for (int i = 0; i < N; ++i)
    {
        const size_t j = (size_t) ((i + 1) % N), k = (size_t) i;
        g.drawLine ({ r0[k], r0[j] }, 1.0f); g.drawLine ({ r1[k], r1[j] }, 1.0f); g.drawLine ({ r2[k], r2[j] }, 1.0f);
        g.drawLine ({ r0[k], r1[k] }, 1.0f);
        if (i % 2 == 0 || i == 5) g.drawLine ({ r1[k], r2[k] }, 1.0f);
        if (i == 1 || i == 3) g.drawLine ({ r1[k], r2[(size_t) ((i + 2) % N)] }, 1.0f);
    }
    g.drawLine ({ r2[0], r1[3] }, 1.0f);
    g.drawLine ({ r1[5], r2[2] }, 1.0f);
    // the far end: the room (ruimte) as a ghost ring
    const float space = *p.apvts.getRawParameterValue ("space");
    if (space > 0.01f)
    {
        g.setColour (orange.withAlpha (0.35f));
        const Point<float> off (40.0f + 90.0f * space, -14.0f - 30.0f * space);
        for (int i = 0; i < N; ++i)
        {
            g.drawLine ({ r2[(size_t) i] + off, r2[(size_t) ((i + 1) % N)] + off }, 1.0f);
            if (i % 3 == 0) g.drawLine ({ r2[(size_t) i], r2[(size_t) i] + off }, 1.0f);
        }
    }

    auto quad = [&] (Point<float> a, Point<float> bb, Point<float> cc, Point<float> d, float fillA)
    {
        juce::Path q; q.startNewSubPath (a); q.lineTo (bb); q.lineTo (cc); q.lineTo (d); q.closeSubPath();
        if (fillA > 0.01f) { g.setColour (juce::Colours::white.withAlpha (fillA)); g.fillPath (q); }
        g.setColour (juce::Colours::white);
        g.strokePath (q, juce::PathStrokeType (2.0f));
    };
    // a word: a lit panel on the bell
    quad (r2[3], r2[4], r2[4] + (r1[4] - r2[4]) * 0.38f, r2[3] + (r1[3] - r2[3]) * 0.38f, flash * 0.5f);
    // the mouthpiece: how open the speech computer's mouth is
    {
        const float mh = 5.0f + 30.0f * mouth, mw = 30.0f - 8.0f * mouth;
        const auto mc = A + Point<float> (-26.0f, 6.0f);
        juce::Path m;
        m.startNewSubPath (mc + Point<float> (-mw * 0.5f, 0)); m.lineTo (mc + Point<float> (0, -mh * 0.5f));
        m.lineTo (mc + Point<float> (mw * 0.5f, 0)); m.lineTo (mc + Point<float> (0, mh * 0.5f)); m.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.2f + 0.5f * trigFlash));
        g.fillPath (m);
        g.setColour (juce::Colours::white);
        g.strokePath (m, juce::PathStrokeType (2.0f));
    }
    // the vowel: a slab in the middle of the tube, where the chart is
    {
        const auto s1 = r1[5] + (r1[1] - r1[5]) * (0.2f + 0.6f * px);
        const float sl = 18.0f + 26.0f * py;
        quad (s1, s1 + Point<float> (sl, -sl * 0.2f), s1 + Point<float> (sl * 0.9f, sl * 0.3f), s1 + Point<float> (-sl * 0.1f, sl * 0.4f), 0.0f);
    }
    // formants: the chart's three resonances (lines), the speech computer's current sound (hollow marks)
    {
        const float fx = x0 + 150.0f, fy = 236.0f, fw = 210.0f;
        const bool female = *p.apvts.getRawParameterValue ("voice") > 0.5f;
        float f[3]; sb::formantsAt (px, py, f[0], f[1], f[2]);
        const float gains[3] = { 1.0f, 0.7f, 0.4f };
        auto xAt = [&] (float hz) { return fx + juce::jlimit (0.0f, 1.0f, std::log2 (hz / 200.0f) / 4.2f) * fw; };
        g.setColour (orange);
        g.drawLine (fx, fy, fx + fw, fy, 1.0f);
        for (int k = 0; k < 3; ++k) { const float x = xAt (f[k] * (female ? 1.17f : 1.0f)); g.drawLine (x, fy, x, fy - 26.0f * gains[k], 2.0f); }
        if (p.speaking())
        {
            const auto& ph = sm::phon (p.display().phon);
            g.setColour (juce::Colours::white);
            for (float hz : { ph.f1, ph.f2, ph.f3 }) if (hz > 150.0f) { const float x = xAt (hz); g.drawRect (Rectangle<float> (x - 2.0f, fy - 31.0f, 4.0f, 4.0f), 1.0f); }
        }
        g.setColour (orange); g.setFont (mono (10.0f));
        g.drawText ("formanten", Rectangle<float> (fx, fy + 3.0f, 80.0f, 12.0f), juce::Justification::centredLeft);
    }
}

// ---- the pages: two folded chains, one segment per layer; the lit ones are on -----------------------------
void Art::paintStrips (juce::Graphics& g)
{
    auto P = [this] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    bool modOn = false;
    for (auto id : { "cv1dst", "cv2dst", "cv3dst", "cv4dst" }) modOn |= P (id) > 0.5f;
    const bool on[kPages] = { P ("loop_on") > 0.5f, P ("cloud_on") > 0.5f, P ("vowel_on") > 0.5f, P ("g_on") > 0.5f, P ("spraak_on") > 0.5f,
                              P ("spraak_on") > 0.5f || P ("fx_src") > 0.5f, modOn || P ("m1") + P ("m2") + P ("m3") > 0.001f, P ("orca_on") > 0.5f };
    auto chain = [&] (Point<float> start, int from, int to)
    {
        bool up = false;
        for (int i = from; i < to; ++i)
        {
            Colour c = Fold::palette (i);
            if (! on[i]) c = c.interpolatedWith (paper, 0.62f);
            start = Fold::segment (g, start, 64.0f, 42.0f, up, c, kPageInfo[i].code, on[i] ? inkFor (c) : ink.withAlpha (0.75f), i == page, glow[(size_t) i], &faces[(size_t) i]);
            up = ! up;
        }
    };
    chain ({ 44.0f, 218.0f }, 0, 4);
    chain ({ 262.0f, 240.0f }, 4, 8);
}

// ---- the navy field: read along with the bank's voice; Sanne says what the speech computer says -------------
void Art::paintSanne (juce::Graphics& g, float x0, float y0, float px)
{
    namespace sn = sm::sanne;
    const bool blink = std::fmod (time, 3.7f) < 0.13f;
    const auto& spr = blink ? sn::eyesClosed : sn::eyesOpen;
    for (int py = 0; py < sn::H; ++py)
        for (int qx = 0; qx < sn::W; ++qx)
        {
            const char c = spr[py][qx];
            const int v = c <= '9' ? c - '0' : c - 'a' + 10;
            if (v <= 0) continue;
            g.setColour (navy.darker (0.6f).interpolatedWith (ink, (float) v / 15.0f));
            g.fillRect (x0 + (float) qx * px, y0 + (float) py * px, px, px);
        }
    // the mouth: the speech computer's phoneme; when only the bank's voice sounds, it opens with its loudness
    sn::Viseme vis = sn::Rest;
    const float lvl = juce::jlimit (0.0f, 1.0f, p.meter() * 9.0f);
    if (lvl >= 0.06f) vis = sn::visemeFor (sm::phon (p.display().phon).id);
    else { const float bl = p.layerLevel[0].load(); vis = bl > 0.22f ? sn::AA : bl > 0.07f ? sn::Mid : sn::Rest; }
    const auto& m = sn::mouths[vis];
    const int mx = sn::mouthX - m.w / 2, my = sn::mouthY - m.h / 2;
    for (int r = 0; r < m.h; ++r)
        for (int c = 0; c < m.w; ++c)
        {
            const char k = m.rows[r][c];
            if (k == '.') continue;
            g.setColour (k == 'L' ? navy.darker (0.6f).interpolatedWith (ink, 0.42f) : k == 'T' ? juce::Colours::white : dark);
            g.fillRect (x0 + (float) (mx + c) * px, y0 + (float) (my + r) * px, px, px);
        }
}

void Art::paintKaraoke (juce::Graphics& g, Rectangle<float> r)
{
    g.setColour (navy);
    g.fillRect (r);
    const float sanneW = sm::sanne::W * 3.0f;
    paintSanne (g, r.getRight() - sanneW - 16.0f, r.getBottom() - sm::sanne::H * 3.0f - 0.0f, 3.0f);
    auto inner = r.reduced (16.0f, 12.0f);
    inner.removeFromRight (sanneW + 12.0f);
    const auto white = juce::Colours::white;
    auto b = p.uiBank();
    const int item = p.shownItem.load(), word = p.shownWord.load();
    g.setFont (mono (10.0f));
    g.setColour (white.withAlpha (0.6f));
    auto top = inner.removeFromTop (14.0f);
    auto bottom = inner.removeFromBottom (30.0f);
    if (! b || ! b->manifestFound)
    {
        g.drawText ("karaoke", top, juce::Justification::centredLeft);
        g.setColour (white); g.setFont (sans (17.0f, true));
        g.drawFittedText (b ? (b->status.isNotEmpty() ? b->status : juce::String ("geen stemmen - tools/render.sh")) : U ("laden\xe2\x80\xa6"), inner.toNearestInt(), juce::Justification::centredLeft, 3);
    }
    else if (item < 0 || item >= (int) b->items.size())
    {
        g.drawText ("les " + juce::String (b->unit) + U (" \xc2\xb7 druk op play of een toets"), top, juce::Justification::centredLeft);
        g.setColour (white); g.setFont (sans (34.0f, true));
        g.drawText ("spreekbuis", inner, juce::Justification::centredLeft);
    }
    else
    {
        auto& it = b->items[(size_t) item];
        const juce::String kind = it.kind == "w" || it.kind == "w0" ? "woord" : it.kind == "s" ? "zin" : "gesprek";
        g.drawText (kind + U (" \xc2\xb7 les ") + juce::String (it.unit), top, juce::Justification::centredLeft);
        // the words, lit as they are spoken: wrapped over as many lines as they need, as large as fits
        float size = 36.0f;
        std::vector<Rectangle<float>> boxes;
        for (; size >= 15.0f; size -= 2.0f)
        {
            boxes.clear();
            const auto f = sans (size, true);
            float x = inner.getX(), y = inner.getY() + 6.0f; bool fits = true;
            const float lh = size * 1.22f, sp = size * 0.3f;
            for (int i = 0; i < it.words.size(); ++i)
            {
                const float ww = juce::GlyphArrangement::getStringWidth (f, it.words[i]);
                if (x + ww > inner.getRight() && x > inner.getX()) { x = inner.getX(); y += lh; }
                boxes.push_back ({ x, y, ww, lh });
                x += ww + sp;
            }
            if (! boxes.empty() && boxes.back().getBottom() + 22.0f > inner.getBottom()) fits = false;
            for (auto& bx : boxes) if (bx.getRight() > inner.getRight() + 1.0f) fits = false;
            if (fits) break;
        }
        const auto f = sans (size, true);
        g.setFont (f);
        for (int i = 0; i < (int) boxes.size(); ++i)
        {
            const bool lit = i == word;
            if (lit) { g.setColour (yellow); g.fillPath (skewRect (boxes[(size_t) i].expanded (5.0f, -size * 0.08f).translated (-size * 0.06f, 0.0f), 0.16f)); }
            g.setColour (lit ? dark : (word >= 0 && i < word ? white.withAlpha (0.6f) : white));
            g.drawText (it.words[i], boxes[(size_t) i].expanded (4.0f, 0.0f), juce::Justification::centred, false);
        }
        const float gy = boxes.empty() ? inner.getY() : boxes.back().getBottom() + 4.0f;
        g.setColour (white.withAlpha (0.7f)); g.setFont (sans (14.0f));
        g.drawText (it.en, Rectangle<float> (inner.getX(), gy, inner.getWidth(), 18.0f), juce::Justification::centredLeft, true);
    }
    // the speech computer's line: what is typed, or what it says (word and phoneme)
    g.setColour (white.withAlpha (0.35f));
    g.fillRect (bottom.getX(), bottom.getY() + 3.0f, bottom.getWidth(), 1.0f);
    auto line = bottom.withTrimmedTop (8.0f);
    g.setFont (mono (11.0f));
    if (typing)
    {
        g.setColour (yellow);
        g.drawText ("> " + typed + ((int) (time * 3.0f) % 2 ? "_" : " "), line, juce::Justification::centredLeft, true);
    }
    else
    {
        const auto d = p.display();
        const bool spraakOn = *p.apvts.getRawParameterValue ("spraak_on") > 0.5f;
        g.setColour (white.withAlpha (spraakOn || p.speaking() ? 0.95f : 0.55f));
        juce::String s ("spraak ");
        if (p.speaking()) s << U ("\xc2\xb7 /") << juce::String::fromUTF8 (sm::phon (d.phon).id) << "/ " << U ("\xc2\xb7 ") << juce::String::fromUTF8 (d.word);
        else if (spraakOn) s << U ("\xc2\xb7 typ iets + enter");
        else s << U ("uit \xc2\xb7 typ iets + enter");
        g.drawText (s, line, juce::Justification::centredLeft, true);
    }
}
} // namespace sbui
