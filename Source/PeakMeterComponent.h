#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

// A small self-timed peak meter: a two-column (L/R) vertical bar with a
// square clip LED above it. Reads directly from atomics owned by whatever
// is doing the audio processing (ChannelProcessor or MainComponent) - the
// audio thread only ever writes them, and this component's own Timer
// (running on the message thread) is the only reader, so plain relaxed
// atomics are enough; no lock or queue needed.
//
// Ballistics: instant attack (displayed level jumps up the moment a louder
// block is reported), decaying release (~20dB/sec) otherwise. The clip LED
// latches red the instant a block overshoots 0dBFS and stays lit until
// either the user clicks it or clipHoldMs passes with no further clips.
class PeakMeterComponent : public juce::Component, private juce::Timer
{
public:
    PeakMeterComponent();
    ~PeakMeterComponent() override;

    // Pointers must outlive this component - true for both call sites
    // (ChannelProcessor/MainComponent members with the same lifetime as
    // the ChannelComponent/MainComponent that owns this meter).
    void setSources(const std::atomic<float>* leftLevel,
                     const std::atomic<float>* rightLevel,
                     std::atomic<bool>* clipFlag);

    // Single-bar mode: draws one full-width channel with its own clip LED,
    // for callers that want independent left/right meters positioned
    // separately (e.g. flanking a fader) rather than one dual-bar widget.
    void setMonoSource(const std::atomic<float>* level, std::atomic<bool>* clipFlag);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    static float levelToFraction(float linearLevel);

    const std::atomic<float>* leftLevelSource  = nullptr;
    const std::atomic<float>* rightLevelSource = nullptr;
    std::atomic<bool>*        clipFlagSource   = nullptr;
    bool mono = false;

    float displayedLeft  = 0.0f;
    float displayedRight = 0.0f;
    float decayMultiplier = 1.0f;

    bool clipLatched     = false;
    int  msSinceLastClip = 0;

    juce::Rectangle<int> clipLedBounds;
    juce::Rectangle<int> barBounds;

    static constexpr int timerHz    = 30;
    static constexpr int clipHoldMs = 1500;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakMeterComponent)
};
