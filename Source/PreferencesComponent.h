#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "ChannelProcessor.h"

class PreferencesComponent : public juce::Component
{
public:
    PreferencesComponent(juce::AudioDeviceManager& dm, ChannelProcessor& cp);
    ~PreferencesComponent() override;
    void resized() override;

private:
    juce::AudioDeviceManager& deviceManager;
    ChannelProcessor& channelProcessor;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Label  tempoLabel;
    juce::Slider tempoSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
