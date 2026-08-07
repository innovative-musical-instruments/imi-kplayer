#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Draws a simple vector gear-wheel icon for the Settings button, rather
// than a Unicode glyph (U+2699 "⚙" carries the same missing-on-Windows-
// fonts risk already hit once for the transport buttons - see
// TransportButtonLookAndFeel's header comment; vector shapes sidestep it
// entirely, same fix applied here).
class GearButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) override
    {
        g.setColour(button.findColour(juce::TextButton::textColourOffId)
                          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        auto bounds = button.getLocalBounds().toFloat();
        auto centre = bounds.getCentre();
        float outerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.3f;
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
