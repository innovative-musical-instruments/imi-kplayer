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

    // Pushed in from outside (MainComponent's own poll timer, which already
    // has a PluginManager& to read from) rather than this component reading
    // PluginManager directly - keeps this a dumb, dependency-free scrim like
    // it was before. A changing name plus a moving progress bar is a much
    // more convincing "still alive" signal on a long scan than the spinner
    // alone, especially if one plugin's init legitimately takes a while.
    void setScanStatus(const juce::String& pluginName, float progress)
    {
        currentPluginName = pluginName;
        currentProgress = juce::jlimit(0.0f, 1.0f, progress);
    }

    // Called once the actual background scan is done, but this overlay
    // needs to stay up a little longer for slower synchronous startup work
    // that still needs it (see MainComponent::showWarmingUpOverlay()) -
    // swaps the header from "Scanning plugins..." to a generic message and
    // drops the plugin-name/progress sub-line, which stops making sense the
    // moment scanning itself is actually over. That startup work runs
    // directly on the message thread and can legitimately block it for a
    // few seconds (plugin instantiation's own safety-margin sleeps), during
    // which nothing here can repaint or animate - the caller is expected to
    // force one synchronous repaint right after calling this, so what's
    // frozen on screen for that stretch is this message, not a stale
    // scanning status implying scanning is still what's happening.
    void setWarmingUp()
    {
        warmingUp = true;
        currentPluginName = {};
        currentProgress = 0.0f;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xdd1a1a2e));

        auto bounds = getLocalBounds().toFloat();
        auto centre = bounds.getCentre().translated(0.0f, -28.0f);
        const float radius = 16.0f;

        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius,
                           0.0f, angle, angle + juce::MathConstants<float>::pi * 1.4f, true);
        g.setColour(juce::Colours::white);
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        auto textArea = bounds.removeFromBottom(bounds.getHeight() * 0.45f);

        g.setFont(15.0f);
        g.drawText(warmingUp ? "Warming up and getting ready..." : "Scanning plugins...",
                    textArea.removeFromTop(22).toNearestInt(), juce::Justification::centred);

        if (currentPluginName.isNotEmpty())
        {
            g.setFont(13.0f);
            g.setColour(juce::Colour(0xffaaaaaa));
            g.drawText(currentPluginName, textArea.removeFromTop(20).toNearestInt(),
                        juce::Justification::centred);

            auto barArea = textArea.removeFromTop(6).reduced(bounds.getWidth() * 0.3f, 0.0f);
            g.setColour(juce::Colour(0xff3d3d55));
            g.fillRoundedRectangle(barArea, 3.0f);
            g.setColour(juce::Colour(0xff3d5a80));
            g.fillRoundedRectangle(barArea.withWidth(barArea.getWidth() * currentProgress), 3.0f);

            // Indeterminate "still alive" shimmer, independent of
            // currentProgress - JUCE only reports progress in whole-file
            // steps (see PluginManager::scanPlugins's comment), so a single
            // slow plugin can legitimately hold the same fraction for a long
            // time. This sweeps continuously regardless, reusing the
            // spinner's own animation phase rather than a second timer.
            float phase = angle / juce::MathConstants<float>::twoPi;
            float sweepWidth = barArea.getWidth() * 0.2f;
            float sweepX = barArea.getX() + phase * (barArea.getWidth() + sweepWidth) - sweepWidth;
            auto sweepArea = barArea.withX(sweepX).withWidth(sweepWidth).getIntersection(barArea);
            if (! sweepArea.isEmpty())
            {
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                g.fillRoundedRectangle(sweepArea, 3.0f);
            }
        }
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
    juce::String currentPluginName;
    float currentProgress = 0.0f;
    bool warmingUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingOverlayComponent)
};
