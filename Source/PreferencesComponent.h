#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

class PreferencesComponent : public juce::Component
{
public:
    // Tempo is a transport-wide setting shared by all channels, so this
    // takes a plain value + change callback rather than a ChannelProcessor&.
    PreferencesComponent(juce::AudioDeviceManager& dm,
                        double initialTempo,
                        std::function<void(double)> onTempoChanged);
    ~PreferencesComponent() override;
    void resized() override;

private:
    juce::AudioDeviceManager& deviceManager;
    std::function<void(double)> onTempoChanged;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Label  tempoLabel;
    juce::Slider tempoSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
