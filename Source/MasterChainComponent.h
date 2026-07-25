#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MasterChainProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"

// The master bus strip: the insert-slot buttons (same load/replace/remove/
// bypass popup-menu pattern as ChannelComponent's insert slots, applied
// once to the master sum instead of once per channel) with the output
// gain fader and its peak meter integrated directly below them, mirroring
// how a channel bundles its own gain fader under its insert slots.
class MasterChainComponent : public juce::Component
{
public:
    static constexpr int numSlots = MasterChainProcessor::numSlots;

    std::function<void(int slotIndex)> onLoadPlugin;
    std::function<void(int slotIndex)> onReplacePlugin;
    std::function<void()> onDirty;

    // Fired with the new linear gain whenever the user moves the fader.
    std::function<void(float linearGain)> onVolumeChanged;

    // Multitrack recording (see RecordingManager) - MainComponent owns the
    // arm/recording-active truth and is the one global transport for every
    // armed channel and the master together; this component only reflects
    // that state via setArmed()/setRecordingActive() and reports user
    // clicks. The arm toggle is disabled while recording is active, same as
    // a channel's own arm button.
    std::function<void(bool armed)> onMasterArmToggled;
    std::function<void()> onRecordButtonClicked;

    explicit MasterChainComponent(MasterChainProcessor& processor);
    ~MasterChainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Sets the displayed fader position without firing onVolumeChanged -
    // for MainComponent to push a value in (session load, external change).
    void setVolume(float linearGain);

    void setLevelMeterSources(const std::atomic<float>* leftLevel,
                              const std::atomic<float>* rightLevel,
                              std::atomic<bool>* clipFlagLeft,
                              std::atomic<bool>* clipFlagRight);

    // Extra empty space reserved above titleLabel, so the insert-slot
    // buttons below it can be pushed down to align with the channel strips'
    // own insert slots (which sit below a taller "Input section" that the
    // master column has nothing equivalent to) - set by MainComponent's
    // resized(), which is the only place that can see both column layouts
    // at once.
    void setTopSpacerHeight(int height);

    void setArmed(bool shouldBeArmed);
    void setRecordingActive(bool active);

private:
    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    MasterChainProcessor& processor;

    int topSpacerHeight = 0;

    juce::Label titleLabel;
    std::array<juce::TextButton, numSlots> slotButtons;

    std::unique_ptr<ConsoleFaderLookAndFeel> volumeFaderLookAndFeel;
    juce::Label      volumeLabel;
    juce::Slider     volumeSlider;
    PeakMeterComponent leftLevelMeter;
    PeakMeterComponent rightLevelMeter;

    juce::TextButton armButton;
    juce::TextButton recordButton;
    bool armed = false;
    bool recordingActive = false;
    void updateArmAndRecordButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainComponent)
};
