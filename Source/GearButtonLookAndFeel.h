#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Draws the Settings button as a left-aligned "Settings" label with a
// smaller vector gear-wheel icon on the right - rather than a Unicode
// glyph (U+2699 "⚙" carries the same missing-on-Windows-fonts risk already
// hit once for the transport buttons, see TransportButtonLookAndFeel's
// header comment; vector shapes sidestep it entirely, same fix applied
// here) and rather than an icon-only button now that there's room to just
// say what it does.
class GearButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) override
    {
        g.setColour(button.findColour(juce::TextButton::textColourOffId)
                          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        juce::Font font(14.0f);
        g.setFont(font);

        // Sized off the font itself, not the button - proportional to the
        // text it sits next to rather than filling whatever room the
        // button happens to have.
        float iconDiameter = font.getHeight() * 0.65f;

        // Fixed inset each side rather than tightly measuring the text and
        // centring an exact-width block - that measured width and what
        // drawText() actually lays out didn't quite agree, which clipped
        // the label. A generous fixed inset keeps the label clear of the
        // left edge and the icon clear of the right edge ("towards the
        // middle") with no truncation risk regardless of button text.
        auto bounds = button.getLocalBounds().toFloat();
        bounds.removeFromLeft(17.0f);
        bounds.removeFromRight(18.0f);
        auto iconArea = bounds.removeFromRight(iconDiameter);
        bounds.removeFromRight(6.0f);

        g.drawText(button.getButtonText(), bounds.toNearestInt(), juce::Justification::centredLeft);

        auto centre = iconArea.getCentre();
        float outerRadius = iconDiameter * 0.5f;
        float innerRadius = outerRadius * 0.42f;
        float toothLength  = outerRadius * 0.4f;
        float toothWidth   = outerRadius * 0.55f;
        const int numTeeth = 8;

        juce::Path gear;
        gear.addEllipse(centre.x - outerRadius, centre.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f);

        for (int i = 0; i < numTeeth; ++i)
        {
            auto angle = (juce::MathConstants<float>::twoPi / (float) numTeeth) * (float) i;
            juce::Path tooth;
            tooth.addRoundedRectangle(-toothWidth * 0.5f, -(outerRadius + toothLength),
                                      toothWidth, toothLength, toothWidth * 0.3f);
            tooth.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
            gear.addPath(tooth);
        }

        // Even-odd fill so the inner circle subtracts a hole out of the
        // gear body instead of just overlapping it.
        gear.setUsingNonZeroWinding(false);
        juce::Path hole;
        hole.addEllipse(centre.x - innerRadius, centre.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);
        gear.addPath(hole);

        g.fillPath(gear);
    }
};
