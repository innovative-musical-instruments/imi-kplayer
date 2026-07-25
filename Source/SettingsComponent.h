#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

class SettingsComponent : public juce::Component
{
public:
    SettingsComponent(juce::AudioDeviceManager& dm,
                        int initialChannelCount,
                        int maxChannelCount,
                        std::function<void(int)> onChannelCountChanged,
                        const juce::File& initialRecordingsFolder,
                        double initialSilenceTimeoutSeconds,
                        std::function<void(juce::File)> onRecordingsFolderChanged,
                        std::function<void(double)> onSilenceTimeoutChanged);
    ~SettingsComponent() override;
    void resized() override;

    // Lets the owner revert the displayed value if a requested shrink is
    // cancelled by the user (e.g. after a "channels have loaded plugins"
    // confirmation is declined) - the slider has already visually moved by
    // the time onChannelCountChanged fires.
    void setDisplayedChannelCount(int count) { channelCountSlider.setValue(count, juce::dontSendNotification); }

private:
    void updateRecordingsFolderLabel();
    void chooseRecordingsFolder();

    juce::AudioDeviceManager& deviceManager;
    std::function<void(int)> onChannelCountChanged;
    std::function<void(juce::File)> onRecordingsFolderChanged;
    std::function<void(double)> onSilenceTimeoutChanged;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Label  channelCountLabel;
    juce::Slider channelCountSlider;

    juce::File recordingsFolder;
    juce::Label recordingLabel;
    juce::Label recordingsFolderPathLabel;
    juce::TextButton chooseFolderButton;
    juce::Label silenceTimeoutLabel;
    juce::Slider silenceTimeoutSlider;

    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
