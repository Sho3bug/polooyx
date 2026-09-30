#pragma once
// POLOOYX — visual language: near-black base, charcoal panels, off-white type,
// one restrained lavender accent. Thin lines, no gradients-for-the-sake-of-it.

#include <juce_gui_basics/juce_gui_basics.h>

namespace plx::ui
{
namespace col
{
    const juce::Colour bg      { 0xff070709 };
    const juce::Colour bg2     { 0xff0d0d11 };
    const juce::Colour panel   { 0xff121217 };
    const juce::Colour line    { 0xff24242b };
    const juce::Colour line2   { 0xff34343d };
    const juce::Colour dim     { 0xff6f6d78 };
    const juce::Colour mid     { 0xffa19fa8 };
    const juce::Colour text    { 0xffe8e5de };
    const juce::Colour accent  { 0xffb9b0ff };
    const juce::Colour accentD { 0xff6c63b8 };
    const juce::Colour hot     { 0xfff2efe8 };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, col::panel);
        setColour (juce::ComboBox::outlineColourId, col::line);
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::ComboBox::arrowColourId, col::mid);
        setColour (juce::PopupMenu::backgroundColourId, col::bg2);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::headerTextColourId, col::accent);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::line);
        setColour (juce::PopupMenu::highlightedTextColourId, col::hot);
        setColour (juce::TextButton::buttonColourId, col::panel);
        setColour (juce::TextButton::buttonOnColourId, col::panel);
        setColour (juce::TextButton::textColourOffId, col::mid);
        setColour (juce::TextButton::textColourOnId, col::hot);
        setColour (juce::AlertWindow::backgroundColourId, col::bg2);
        setColour (juce::AlertWindow::textColourId, col::text);
        setColour (juce::AlertWindow::outlineColourId, col::line2);
        setColour (juce::TextEditor::backgroundColourId, col::panel);
        setColour (juce::TextEditor::textColourId, col::text);
        setColour (juce::TextEditor::outlineColourId, col::line2);
        setColour (juce::TextEditor::focusedOutlineColourId, col::accentD);
        setColour (juce::Label::textColourId, col::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const bool big = s.getProperties().getWithDefault ("big", false);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (big ? 10.0f : 5.0f);
        const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        const bool hot = s.isMouseOverOrDragging();

        // body
        const float bodyR = rad * (big ? 0.74f : 0.64f);
        g.setColour (big ? col::bg2 : col::panel);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
        g.setColour (hot ? col::mid.withAlpha (0.6f) : col::line2);
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

        // track + value arc
        const float arcR = rad * 0.92f, thick = big ? 2.2f : 1.6f;
        juce::Path track; track.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a1, true);
        g.setColour (col::line);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        juce::Path val; val.addCentredArc (c.x, c.y, arcR, arcR, 0, std::min (from, ang), std::max (from, ang), true);
        if (big)
        {
            g.setColour (col::accent.withAlpha (0.10f));
            g.strokePath (val, juce::PathStrokeType (thick * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (col::accent.withAlpha (hot ? 1.0f : 0.85f));
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        const float p0 = bodyR * 0.35f, p1 = bodyR * 0.88f;
        const float sx = std::sin (ang), sy = -std::cos (ang);
        g.setColour (col::text);
        g.drawLine (c.x + sx * p0, c.y + sy * p0, c.x + sx * p1, c.y + sy * p1, big ? 2.0f : 1.5f);

        // tick ring for the big macro knobs
        if (big)
        {
            g.setColour (col::line);
            for (int i = 0; i <= 20; ++i)
            {
                const float a = a0 + (a1 - a0) * (float) i / 20.0f;
                const float r0 = rad * 1.0f, r1 = rad * (i % 5 == 0 ? 1.07f : 1.03f);
                g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, 1.0f);
            }
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float rad = b.getProperties().getWithDefault ("pill", false) ? r.getHeight() * 0.5f : 3.0f;
        g.setColour (on ? col::accent.withAlpha (0.12f) : (over ? col::panel.brighter (0.05f) : col::panel));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? col::accent.withAlpha (down ? 1.0f : 0.8f) : (over ? col::line2 : col::line));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (font (std::min (12.0f, b.getHeight() * 0.48f), true));
        g.setColour (b.getToggleState() ? col::hot : (b.isEnabled() ? col::mid : col::dim));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.28f);
        g.setColour (on ? col::accent : (over ? col::line2 : col::line));
        g.drawRoundedRectangle (box, 2.0f, 1.0f);
        if (on) { g.setColour (col::accent.withAlpha (0.85f)); g.fillRoundedRectangle (box.reduced (3.0f), 1.0f); }
        g.setColour (on ? col::text : col::mid);
        g.setFont (font (11.5f, true));
        g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (2), juce::Justification::centredLeft, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (col::panel); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (cb.isMouseOver (true) ? col::line2 : col::line); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        juce::Path p; p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (col::mid); g.fillPath (p);
    }

    juce::Font getComboBoxFont (juce::ComboBox& cb) override { return font (std::min (13.0f, cb.getHeight() * 0.5f), true); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox& cb, juce::Label& l) override
    {
        l.setBounds (8, 0, cb.getWidth() - 26, cb.getHeight());
        l.setFont (getComboBoxFont (cb));
        l.setJustificationType (juce::Justification::centredLeft);
    }
};
} // namespace plx::ui
