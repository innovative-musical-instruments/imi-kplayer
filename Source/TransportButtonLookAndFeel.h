#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Draws the master transport buttons (Play/Pause/RTZ, see MasterChainComponent
// and SessionTransport) as simple filled vector shapes instead of Unicode
// glyphs - U+23F8 "⏸" and U+23EE "⏮" turned out not to be covered by any
// installed font on Windows, so JUCE fell back to the OS's last-resort
// "unknown character" box glyph instead of the real symbol. Vector shapes
// sidestep font coverage entirely and render identically on Mac and Windows.
// Dispatches purely on the button's text ("PLAY"/"PAUSE"/"RTZ", set by
// MasterChainComponent - not shown to the user, drawButtonText() below draws
// the icon instead of the literal text) so one shared instance can serve
// both the toggling Play/Pause button and the momentary RTZ button.
class TransportButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) override
    {
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                               : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        auto bounds = button.getLocalBounds().toFloat();
        float halfSize = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.21f;
        auto centre = bounds.getCentre();
        juce::Rectangle<float> icon(centre.x - halfSize, centre.y - halfSize, halfSize * 2.0f, halfSize * 2.0f);

        auto text = button.getButtonText();
        if (text == "PAUSE")
        {
            auto barWidth = icon.getWidth() * 0.32f;
            auto gap      = icon.getWidth() * 0.14f;
            g.fillRoundedRectangle(icon.getCentreX() - gap * 0.5f - barWidth, icon.getY(),
                                   barWidth, icon.getHeight(), 1.0f);
            g.fillRoundedRectangle(icon.getCentreX() + gap * 0.5f, icon.getY(),
                                   barWidth, icon.getHeight(), 1.0f);
        }
        else if (text == "RTZ")
        {
            // "Skip to start": a bar followed by a left-pointing triangle.
            auto barWidth = icon.getWidth() * 0.16f;
            g.fillRoundedRectangle(icon.getX(), icon.getY(), barWidth, icon.getHeight(), 1.0f);

            juce::Path triangle;
            triangle.addTriangle(icon.getRight(), icon.getY(),
                                 icon.getRight(), icon.getBottom(),
                                 icon.getX() + barWidth * 1.6f, icon.getCentreY());
            g.fillPath(triangle);
        }
        else // "PLAY"
        {
            juce::Path triangle;
            triangle.addTriangle(icon.getX(), icon.getY(),
                                 icon.getX(), icon.getBottom(),
                                 icon.getRight(), icon.getCentreY());
            g.fillPath(triangle);
        }
    }
};
