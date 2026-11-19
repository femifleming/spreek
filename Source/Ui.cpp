#include "Ui.h"
#include <SbFonts.h>

namespace sbui
{
static juce::Typeface::Ptr face (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}
juce::Font sans (float size, bool medium)
{
    static auto r = face (SbFonts::InterRegular_ttf, SbFonts::InterRegular_ttfSize);
    static auto m = face (SbFonts::InterMedium_ttf, SbFonts::InterMedium_ttfSize);
    return juce::Font (juce::FontOptions (medium ? m : r).withHeight (size));
}
juce::Font mono (float size, bool medium)
{
    static auto r = face (SbFonts::IBMPlexMonoRegular_ttf, SbFonts::IBMPlexMonoRegular_ttfSize);
    static auto m = face (SbFonts::IBMPlexMonoMedium_ttf, SbFonts::IBMPlexMonoMedium_ttfSize);
    return juce::Font (juce::FontOptions (medium ? m : r).withHeight (size));
}

juce::Path skewRect (juce::Rectangle<float> r, float skew)
{
    const float d = r.getHeight() * skew;
    juce::Path p;
    p.startNewSubPath (r.getX() + d, r.getY());
    p.lineTo (r.getRight() + d, r.getY());
    p.lineTo (r.getRight(), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.closeSubPath();
    return p;
}

// ---- ParamSlider ----------------------------------------------------------------------------------
ParamSlider::ParamSlider (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { value01 = param.convertTo0to1 (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64) + U ("  (drag · shift = fine · double-click = default)"));
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

juce::Rectangle<float> ParamSlider::track() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromLeft ((float) labelW);
    b.removeFromRight (70.0f);
    return b.reduced (4.0f, 0.0f);
}

void ParamSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float cy = b.getCentreY();
    g.setColour (theme.ink);
    g.setFont (mono (11.0f));
    g.drawText (label, juce::Rectangle<float> (0, 0, (float) labelW, b.getHeight()), juce::Justification::centredLeft, true);
    g.drawText (valueText ? valueText() : param.getCurrentValueAsText(), juce::Rectangle<float> (b.getRight() - 70.0f, 0, 70.0f, b.getHeight()), juce::Justification::centredRight, true);

    const auto t = track();
    g.setColour (theme.ink.withAlpha (0.35f));
    g.fillRect (t.getX(), cy, t.getWidth(), 1.0f);

    const auto range = param.getNormalisableRange();
    const bool bipolar = range.start < 0.0f && range.end > 0.0f;
    const float zero = bipolar ? range.convertTo0to1 (0.0f) : 0.0f;
    const float a = t.getX() + t.getWidth() * std::min (zero, value01);
    const float e = t.getX() + t.getWidth() * std::max (zero, value01);
    const float h = 9.0f;
    if (e - a > 0.5f)
    {
        g.setColour (theme.fill.withAlpha (dragging ? 1.0f : 0.92f));
        g.fillPath (skewRect ({ a, cy - h * 0.5f, e - a, h }));
    }
    const float x = t.getX() + t.getWidth() * value01;
    if (ghost >= 0.0f && std::abs (ghost - value01) > 0.004f)
    {
        // where the drift has moved it
        const float gx = t.getX() + t.getWidth() * juce::jlimit (0.0f, 1.0f, ghost);
        g.setColour (theme.ink.withAlpha (0.55f));
        g.drawRect (juce::Rectangle<float> (gx - 1.5f, cy - 6.5f, 3.0f, 13.0f), 1.0f);
    }
    g.setColour (theme.edge);
    g.fillRect (x - 1.0f, cy - 5.5f, 2.0f, 11.0f);
}

void ParamSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = true; dragStart = value01;
    attach.beginGesture();
    if (onTouch) onTouch();
    repaint();
}
void ParamSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float w = std::max (40.0f, track().getWidth());
    const float sens = e.mods.isShiftDown() ? 0.12f : 1.0f;
    const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY() * 0.6f) / w * sens;
    const float v = juce::jlimit (0.0f, 1.0f, dragStart + d);
    attach.setValueAsPartOfGesture (param.convertFrom0to1 (v));
}
void ParamSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attach.endGesture();
    repaint();
}
void ParamSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}
void ParamSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f);
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value01 + step)));
}

// ---- ChoiceBox ------------------------------------------------------------------------------------
ChoiceBox::ChoiceBox (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
int ChoiceBox::numItems() const
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param)) return c->choices.size();
    return std::max (2, param.getNumSteps());
}
void ChoiceBox::set (int i)
{
    i = juce::jlimit (0, numItems() - 1, i);
    attach.setValueAsCompleteGesture ((float) i);
}
void ChoiceBox::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setFont (mono (11.0f));
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param);
    if (asTabs && c)
    {
        if (label.trim().isNotEmpty())
        {
            g.setColour (theme.ink);
            g.drawText (label, b.removeFromLeft (64.0f), juce::Justification::centredLeft);
        }
        const int n = c->choices.size();
        const float w = b.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto r = juce::Rectangle<float> (b.getX() + w * i, b.getY(), w, b.getHeight()).reduced (3.0f, 2.0f);
            if (i == index)
            {
                g.setColour (theme.fill);
                g.fillPath (skewRect (r.reduced (2.0f, 0.0f), 0.35f));
            }
            g.setColour (i == index ? inkFor (theme.fill) : theme.ink);
            g.drawText (c->choices[i], r, juce::Justification::centred);
        }
        return;
    }
    g.setColour (theme.ink);
    const float lw = label.isEmpty() ? 0.0f : (float) labelW;
    g.drawText (label, b.removeFromLeft (lw), juce::Justification::centredLeft);
    g.setFont (mono (11.0f, true));
    g.drawText (param.getCurrentValueAsText(), b, juce::Justification::centredLeft);
    g.setColour (theme.ink.withAlpha (0.35f));
    g.fillRect (b.getX(), b.getBottom() - 2.0f, std::min (b.getWidth(), 90.0f), 1.0f);
}
void ChoiceBox::mouseDown (const juce::MouseEvent& e)
{
    moved = false; dragStartIndex = index;
    if (asTabs)
    {
        auto b = getLocalBounds().toFloat();
        if (label.trim().isNotEmpty()) b.removeFromLeft (64.0f);
        const int i = (int) ((e.position.x - b.getX()) / (b.getWidth() / (float) numItems()));
        set (i);
        moved = true;
    }
}
void ChoiceBox::mouseDrag (const juce::MouseEvent& e)
{
    if (asTabs) return;
    const int d = -e.getDistanceFromDragStartY() / 12;
    if (d != 0) moved = true;
    if (moved) set (dragStartIndex + d);
}
void ChoiceBox::mouseUp (const juce::MouseEvent&)
{
    if (moved) return;
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param);
    if (! c) { set ((index + 1) % numItems()); return; }
    juce::PopupMenu m;
    for (int i = 0; i < c->choices.size(); ++i) m.addItem (i + 1, c->choices[i], true, i == index);
    juce::Component::SafePointer<ChoiceBox> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe && r > 0) safe->set (r - 1); });
}

// ---- ToggleBox ------------------------------------------------------------------------------------
ToggleBox::ToggleBox (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void ToggleBox::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto r = juce::Rectangle<float> (0.0f, b.getCentreY() - 5.0f, 18.0f, 10.0f);
    auto p = skewRect (r);
    if (on) { g.setColour (theme.fill); g.fillPath (p); }
    g.setColour (theme.ink);
    g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setFont (mono (11.0f));
    g.drawText (label, b.withTrimmedLeft (30.0f), juce::Justification::centredLeft);
}
void ToggleBox::mouseDown (const juce::MouseEvent&) { attach.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

// ---- TextLink -------------------------------------------------------------------------------------
void TextLink::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (lit) { g.setColour (theme.fill); g.fillPath (skewRect (b.reduced (2.0f, 3.0f), 0.35f)); }
    g.setColour (lit ? inkFor (theme.fill) : theme.ink);
    g.setFont (mono (11.0f));
    g.drawText (text, b, juce::Justification::centred);
}

// ---- PresetBar ------------------------------------------------------------------------------------
PresetBar::PresetBar (juce::AudioProcessor& proc, Theme t) : p (proc), theme (t)
{
    startTimerHz (8);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("presets");
}
void PresetBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme.ink);
    g.setFont (mono (11.0f));
    g.drawText ("<", b.removeFromLeft (16.0f), juce::Justification::centred);
    g.drawText (">", b.removeFromRight (16.0f), juce::Justification::centred);
    g.setFont (mono (11.0f, true));
    g.drawText (p.getProgramName (p.getCurrentProgram()), b, juce::Justification::centred, true);
}
void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    const int n = p.getNumPrograms(), cur = p.getCurrentProgram();
    if (e.x < 18) { p.setCurrentProgram ((cur - 1 + n) % n); return; }
    if (e.x > getWidth() - 18) { p.setCurrentProgram ((cur + 1) % n); return; }
    juce::PopupMenu m;
    for (int i = 0; i < n; ++i) m.addItem (i + 1, p.getProgramName (i), true, i == cur);
    auto* proc = &p;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [proc] (int r) { if (r > 0) proc->setCurrentProgram (r - 1); });
}
} // namespace sbui
