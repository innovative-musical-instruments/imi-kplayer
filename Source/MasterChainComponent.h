#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MasterChainProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"
#include "SlotButtonLookAndFeel.h"

// The master bus strip: the insert-slot buttons (same load/replace/remove/
// bypass popup-menu pattern as ChannelComponent's insert slots, applied
// once to the master sum instead of once per channel), the output gain
// fader and its peak meter integrated directly below them, and the master
// ARM toggle - mirroring how a channel bundles its own gain fader under its
// insert slots. Everything session-global (transport, Record Ready, tempo/
// sync, channel count, Settings, Panic) lives one strip further right, in
// GlobalSectionComponent - see MainComponent::resized() for the actual
// strip order.
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
    // arm truth and is the one global transport for every armed channel and
    // the master together; this component only reflects that state via
    // setArmed() and reports user clicks. The arm toggle stays clickable
    // while recording is active, same as a channel's own arm button -
    // arming mid-take takes effect immediately rather than being rejected.
    std::function<void(bool armed)> onMasterArmToggled;

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

    void setArmed(bool shouldBeArmed);

    // Global collapse of each channel's Audio In/MIDI In/MIDI Channel rows
    // (see MainComponent::setInputSectionCollapsedState) shifts
    // ChannelComponent's own divider line up/down - this strip has no such
    // rows to collapse, but needs to know the state anyway so its own
    // heading divider can track the same Y position either way (see
    // resized()'s comment).
    void setInputSectionCollapsed(bool collapsed);

private:
    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    MasterChainProcessor& processor;

    juce::Label titleLabel;
    int titleDividerY = 0;
    bool inputSectionCollapsed = false;
    std::array<juce::TextButton, numSlots> slotButtons;
    std::unique_ptr<SlotButtonLookAndFeel> slotButtonLookAndFeel;

    std::unique_ptr<ConsoleFaderLookAndFeel> volumeFaderLookAndFeel;
    juce::Label      volumeLabel;
    juce::Slider     volumeSlider;
    PeakMeterComponent leftLevelMeter;
    PeakMeterComponent rightLevelMeter;

    juce::TextButton armButton;
    bool armed = false;
    void updateArmButton();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainComponent)
};
