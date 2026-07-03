#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Semi-transparent scrim shown over MainComponent while PluginManager scans
// in the background. Blocks mouse input so nothing touches knownPluginList
// until the scan completes.
class LoadingOverlayComponent : public juce::Component,
                                 private juce::Timer
{
public:
    LoadingOverlayComponent()
    {
        setInterceptsMouseClicks(true, true);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xdd1a1a2e));

        auto bounds = getLocalBounds().toFloat();
        auto centre = bounds.getCentre().translated(0.0f, -14.0f);
        const float radius = 16.0f;

        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius,
                           0.0f, angle, angle + juce::MathConstants<float>::pi * 1.4f, true);
        g.setColour(juce::Colours::white);
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        g.setFont(15.0f);
        g.drawText("Scanning plugins...",
                    bounds.removeFromBottom(bounds.getHeight() * 0.4f).toNearestInt(),
                    juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        angle += 0.12f;
        if (angle > juce::MathConstants<float>::twoPi)
            angle -= juce::MathConstants<float>::twoPi;
        repaint();
    }

    float angle = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingOverlayComponent)
};
