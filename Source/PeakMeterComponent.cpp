#include "PeakMeterComponent.h"

PeakMeterComponent::PeakMeterComponent()
{
    // ~20dB/sec release, expressed as a per-tick linear multiplier.
    const float dbPerTick = 20.0f / (float) timerHz;
    decayMultiplier = juce::Decibels::decibelsToGain(-dbPerTick);

    setInterceptsMouseClicks(true, false);
    startTimerHz(timerHz);
}

PeakMeterComponent::~PeakMeterComponent()
{
    stopTimer();
}

void PeakMeterComponent::setSources(const std::atomic<float>* leftLevel,
                                     const std::atomic<float>* rightLevel,
                                     std::atomic<bool>* clipFlag)
{
    leftLevelSource  = leftLevel;
    rightLevelSource = rightLevel;
    clipFlagSource   = clipFlag;
    mono = false;
}

void PeakMeterComponent::setMonoSource(const std::atomic<float>* level, std::atomic<bool>* clipFlag)
{
    leftLevelSource  = level;
    rightLevelSource = nullptr;
    clipFlagSource   = clipFlag;
    mono = true;
}

float PeakMeterComponent::levelToFraction(float linearLevel)
{
    // -60dB..+3dB mapped onto 0..1 of the bar's height.
    const float minDb = -60.0f;
    const float maxDb = 3.0f;
    float db = linearLevel <= 0.0f ? minDb : juce::Decibels::gainToDecibels(linearLevel);
    return juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

void PeakMeterComponent::mouseDown(const juce::MouseEvent&)
{
    clipLatched     = false;
    msSinceLastClip = 0;
    repaint();
}

void PeakMeterComponent::timerCallback()
{
    auto pullLevel = [this](const std::atomic<float>* source, float& displayed)
    {
        if (source == nullptr)
            return;

        float instant = source->load(std::memory_order_relaxed);
        if (instant > displayed)
            displayed = instant;          // instant attack
        else
            displayed *= decayMultiplier; // decaying release
    };

    pullLevel(leftLevelSource, displayedLeft);
    pullLevel(rightLevelSource, displayedRight);

    // exchange() both reads and resets in one step, so a clip reported by
    // the audio thread is consumed exactly once no matter how the two
    // threads interleave.
    if (clipFlagSource != nullptr && clipFlagSource->exchange(false, std::memory_order_relaxed))
    {
        clipLatched     = true;
        msSinceLastClip = 0;
    }
    else if (clipLatched)
    {
        msSinceLastClip += 1000 / timerHz;
        if (msSinceLastClip >= clipHoldMs)
            clipLatched = false;
    }

    repaint();
}

void PeakMeterComponent::resized()
{
    auto area = getLocalBounds();
    clipLedBounds = area.removeFromTop(8);
    area.removeFromTop(3);
    barBounds = area;
}

void PeakMeterComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff14141f));
    g.fillRoundedRectangle(clipLedBounds.toFloat(), 2.0f);
    g.setColour(clipLatched ? juce::Colours::red : juce::Colour(0xff3a3a4a));
    g.fillRoundedRectangle(clipLedBounds.toFloat().reduced(1.5f), 1.5f);

    g.setColour(juce::Colour(0xff14141f));
    g.fillRoundedRectangle(barBounds.toFloat(), 2.0f);

    auto drawBar = [&](juce::Rectangle<int> bounds, float level)
    {
        float fraction = levelToFraction(level);
        int   filledHeight = (int) (fraction * (float) bounds.getHeight());
        auto  filled = bounds.removeFromBottom(filledHeight);

        float db = level <= 0.0f ? -100.0f : juce::Decibels::gainToDecibels(level);
        juce::Colour colour = juce::Colours::limegreen;
        if (db >= 0.0f)       colour = juce::Colours::red;
        else if (db >= -6.0f) colour = juce::Colours::yellow;

        g.setColour(colour);
        g.fillRect(filled);
    };

    if (mono)
    {
        drawBar(barBounds.reduced(1, 0), displayedLeft);
        return;
    }

    auto bars  = barBounds.reduced(1, 0);
    auto left  = bars.removeFromLeft(bars.getWidth() / 2);
    bars.removeFromLeft(1);
    auto right = bars;

    drawBar(left,  displayedLeft);
    drawBar(right, displayedRight);
}
