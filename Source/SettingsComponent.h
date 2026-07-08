#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

class SettingsComponent : public juce::Component
{
public:
    // Tempo is a transport-wide setting shared by all channels, so this
    // takes a plain value + change callback rather than a ChannelProcessor&.
    SettingsComponent(juce::AudioDeviceManager& dm,
                        double initialTempo,
                        std::function<void(double)> onTempoChanged,
                        int initialChannelCount,
                        int maxChannelCount,
                        std::function<void(int)> onChannelCountChanged);
    ~SettingsComponent() override;
    void resized() override;

    // Lets the owner revert the displayed value if a requested shrink is
    // cancelled by the user (e.g. after a "channels have loaded plugins"
    // confirmation is declined) - the slider has already visually moved by
    // the time onChannelCountChanged fires.
    void setDisplayedChannelCount(int count) { channelCountSlider.setValue(count, juce::dontSendNotification); }

private:
    juce::AudioDeviceManager& deviceManager;
    std::function<void(double)> onTempoChanged;
    std::function<void(int)> onChannelCountChanged;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Label  tempoLabel;
    juce::Slider tempoSlider;
    juce::Label  channelCountLabel;
    juce::Slider channelCountSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
