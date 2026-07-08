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

private:
    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    MasterChainProcessor& processor;

    juce::Label titleLabel;
    std::array<juce::TextButton, numSlots> slotButtons;

    std::unique_ptr<ConsoleFaderLookAndFeel> volumeFaderLookAndFeel;
    juce::Label      volumeLabel;
    juce::Slider     volumeSlider;
    PeakMeterComponent leftLevelMeter;
    PeakMeterComponent rightLevelMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainComponent)
};
