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

    // "Rescan Plugins" button - set post-construction rather than a ctor
    // param (like AboutScreenComponent::onCloseRequested) since the owner
    // needs a handle to the enclosing DialogWindow, which doesn't exist yet
    // at the point this component is constructed.
    std::function<void()> onRescanRequested;

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
    // AudioDeviceSelectorComponent self-sizes to its true content height on
    // every layout pass (see its resized(), which ends with
    // setSize(getWidth(), <natural content height>)) - that height grows
    // with the number of enumerated audio channels and MIDI ports and can
    // easily exceed whatever fixed space this dialog has. Housing it in a
    // Viewport (which auto-tracks the viewed component's size via its own
    // ComponentListener) means every control - e.g. a MIDI input far down
    // an alphabetically-sorted "Active MIDI inputs" list - stays reachable
    // by scrolling instead of being silently clipped off the bottom of a
    // fixed-height dialog.
    juce::Viewport audioSettingsViewport;
    juce::Label  channelCountLabel;
    juce::Slider channelCountSlider;

    juce::File recordingsFolder;
    juce::Label recordingLabel;
    juce::Label recordingsFolderPathLabel;
    juce::TextButton chooseFolderButton;
    juce::Label silenceTimeoutLabel;
    juce::Slider silenceTimeoutSlider;

    juce::Label pluginsLabel;
    juce::TextButton rescanPluginsButton;

    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
