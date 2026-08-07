#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrandingStripComponent.h"
#include "TempoSyncComponent.h"
#include "TransportButtonLookAndFeel.h"
#include "GearButtonLookAndFeel.h"

// The rightmost, always-visible strip: branding, channel-count control,
// access to Settings, the global channel-I/O collapse toggle, tempo/sync,
// session transport (Play/Rec/RTZ), and Panic - everything that isn't
// scoped to a single channel or to the master bus signal chain (that's
// MasterChainComponent, its own separate strip immediately to this one's
// left - see MainComponent::resized() for the actual strip order).
class GlobalSectionComponent : public juce::Component,
                               private juce::Timer
{
public:
    enum class RecordState { idle, armed, recording };

    GlobalSectionComponent(int initialChannelCount, int maxChannelCount);
    ~GlobalSectionComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Fired with the requested new count (already clamped to
    // [1, maxChannelCount] and with +/- disabled at the extremes) whenever
    // the +/- boxes are clicked. The owner decides whether to actually
    // apply it (e.g. confirming a destructive shrink first) and calls
    // setChannelCount() back with the real outcome either way.
    std::function<void(int)> onChannelCountChangeRequested;
    // Reflects external state (session load, or reverting a cancelled
    // shrink) without firing the callback above.
    void setChannelCount(int count);

    std::function<void()> onSettingsRequested;

    std::function<void()> onCollapseToggled;
    void setInputSectionCollapsed(bool collapsed);

    // MainComponent owns the actual tempo/sync state and wires this
    // component's callbacks/setters directly, same relationship it already
    // had before this component existed - just reached through here now.
    TempoSyncComponent& getTempoSyncComponent() { return tempoSyncComponent; }

    std::function<void()> onPlayPauseClicked;
    std::function<void()> onRtzClicked;
    void setTransportPlaying(bool playing);

    // Record Ready - the state machine itself lives in MainComponent
    // (toggleRecordArm()/startArmedRecording()), this component only
    // reflects it and reports clicks: idle -> click -> armed (blinking,
    // this component's own Timer) -> click -> back to idle, or Play while
    // armed transitions to recording (solid, owner's call) -> click stops
    // it (also the owner's call, this just forwards the click either way).
    std::function<void()> onRecordButtonClicked;
    void setRecordState(RecordState state);

    // Momentary visual only (a brief red flash on click) - no persistent
    // state to reflect back in, unlike Record Ready above.
    std::function<void()> onPanicClicked;

private:
    void timerCallback() override; // drives the armed-state blink only

    BrandingStripComponent brandingStrip;

    juce::Label channelsLabel;
    juce::TextButton channelMinusButton;
    juce::Label channelCountLabel;
    juce::TextButton channelPlusButton;
    int channelCount = 1;
    int maxChannels  = 1;
    void updateChannelButtons();

    juce::TextButton settingsButton;
    std::unique_ptr<GearButtonLookAndFeel> gearButtonLookAndFeel;

    juce::TextButton collapseInputButton;
    bool inputCollapsed = false;

    TempoSyncComponent tempoSyncComponent;

    juce::TextButton playPauseButton;
    juce::TextButton rtzButton;
    std::unique_ptr<TransportButtonLookAndFeel> transportButtonLookAndFeel;
    bool transportPlaying = false;
    void updateTransportButtons();

    juce::TextButton recordButton;
    RecordState recordState = RecordState::idle;
    bool blinkPhaseOn = false;
    void updateRecordButtonColour();

    juce::TextButton panicButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalSectionComponent)
};
