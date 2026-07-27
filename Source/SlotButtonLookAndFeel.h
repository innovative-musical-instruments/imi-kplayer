#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Left-aligns a TextButton's text instead of the stock LookAndFeel_V4's
// centred layout - used for the instrument/insert slot buttons (both
// ChannelComponent's and MasterChainComponent's), where the leading slot
// number should sit flush against the left edge rather than drifting to the
// middle of the button when the rest of the label is short or empty.
class SlotButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) override
    {
        auto font = getTextButtonFont(button, button.getHeight());
        g.setFont(font);
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                               : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        const int yIndent = juce::jmin(4, button.proportionOfHeight(0.3f));
        const int leftIndent = 6;
        const int textWidth = button.getWidth() - leftIndent - 4;

        if (textWidth > 0)
            g.drawFittedText(button.getButtonText(), leftIndent, yIndent, textWidth, button.getHeight() - yIndent * 2,
                             juce::Justification::centredLeft, 2);
    }
};
