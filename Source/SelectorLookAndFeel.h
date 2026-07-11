#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Narrower, right-hugging dropdown arrow for the channel strip's ComboBoxes
// (audio input / MIDI input / MIDI channel selectors). The stock
// LookAndFeel_V4 arrow sits in a 20px zone 10px in from the right edge; this
// halves that to a 10px zone just a few pixels from the edge, and reserves
// less text-label width to match.
class SelectorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto cornerSize = 3.0f;
        juce::Rectangle<int> boxBounds(0, 0, width, height);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

        juce::Rectangle<int> arrowZone(width - rightPadding - arrowZoneWidth, 0, arrowZoneWidth, height);
        juce::Path path;
        path.startNewSubPath((float) arrowZone.getX() + 1.5f, (float) arrowZone.getCentreY() - 2.0f);
        path.lineTo((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
        path.lineTo((float) arrowZone.getRight() - 1.5f, (float) arrowZone.getCentreY() - 2.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(1, 1,
                        box.getWidth() - (rightPadding + arrowZoneWidth + 2),
                        box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

private:
    static constexpr int arrowZoneWidth = 10;
    static constexpr int rightPadding = 4;
};
