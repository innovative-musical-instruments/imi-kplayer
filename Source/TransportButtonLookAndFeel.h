#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Draws the master transport buttons (Play/Pause/RTZ, see MasterChainComponent
// and SessionTransport) as simple filled vector shapes instead of Unicode
// glyphs - U+23F8 "⏸" and U+23EE "⏮" turned out not to be covered by any
// installed font on Windows, so JUCE fell back to the OS's last-resort
// "unknown character" box glyph instead of the real symbol. Vector shapes
// sidestep font coverage entirely and render identically on Mac and Windows.
// Dispatches purely on the button's text ("PLAY"/"PAUSE"/"RTZ"/"REC"/
// "CAPTURE", set by MasterChainComponent/GlobalSectionComponent - not shown
// to the user, drawButtonText() below draws the icon instead of the literal
// text) so one shared instance can serve every button in the transport
// cluster, toggling or momentary.
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
        else if (text == "PLAY")
        {
            juce::Path triangle;
            triangle.addTriangle(icon.getX(), icon.getY(),
                                 icon.getX(), icon.getBottom(),
                                 icon.getRight(), icon.getCentreY());
            g.fillPath(triangle);
        }
        else if (text == "CAPTURE")
        {
            // Up arrow - "pull the playhead up into the field above this
            // button" (GlobalSectionComponent's Range capture buttons).
            // Vector-drawn for the same reason as everything else here: a
            // Unicode arrow glyph is exactly the kind of character the
            // Windows font-coverage gap in this class's header bit on.
            juce::Path arrow;
            arrow.addTriangle(icon.getX(), icon.getCentreY(),
                              icon.getRight(), icon.getCentreY(),
                              icon.getCentreX(), icon.getY());
            g.fillPath(arrow);

            auto stemWidth = icon.getWidth() * 0.24f;
            g.fillRect(icon.getCentreX() - stemWidth * 0.5f, icon.getCentreY(),
                       stemWidth, icon.getHeight() * 0.5f);
        }
        else // "REC" - GlobalSectionComponent's Record Ready button. Same
             // icon bounding box (halfSize above) as Play/Pause/RTZ use, so
             // the record dot reads as proportional to the Play arrow
             // rather than an independently-sized glyph.
        {
            g.fillEllipse(icon);
        }
    }
};
