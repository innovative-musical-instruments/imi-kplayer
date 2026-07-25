#include "SettingsComponent.h"

SettingsComponent::SettingsComponent(juce::AudioDeviceManager& dm,
                                            int initialChannelCount,
                                            int maxChannelCount,
                                            std::function<void(int)> onChannelCountChangedIn,
                                            const juce::File& initialRecordingsFolder,
                                            double initialSilenceTimeoutSeconds,
                                            std::function<void(juce::File)> onRecordingsFolderChangedIn,
                                            std::function<void(double)> onSilenceTimeoutChangedIn)
    : deviceManager(dm),
      onChannelCountChanged(std::move(onChannelCountChangedIn)),
      onRecordingsFolderChanged(std::move(onRecordingsFolderChangedIn)),
      onSilenceTimeoutChanged(std::move(onSilenceTimeoutChangedIn)),
      recordingsFolder(initialRecordingsFolder)
{
    selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager,
        0,    // min input channels
        8,    // max input channels - lets users enable inputs for per-channel audio routing
        2,    // min output channels
        2,    // max output channels
        true, // show MIDI input
        false,// show MIDI output
        false,// stereo pair
        false // hide advanced options
    );
    addAndMakeVisible(selector.get());

    channelCountLabel.setText("Channels", juce::dontSendNotification);
    addAndMakeVisible(channelCountLabel);

    channelCountSlider.setSliderStyle(juce::Slider::IncDecButtons);
    channelCountSlider.setRange(1, maxChannelCount, 1);
    channelCountSlider.setValue(initialChannelCount, juce::dontSendNotification);
    channelCountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
    channelCountSlider.onValueChange = [this]
    {
        if (onChannelCountChanged)
            onChannelCountChanged((int) channelCountSlider.getValue());
    };
    addAndMakeVisible(channelCountSlider);

    recordingLabel.setText("Recording", juce::dontSendNotification);
    recordingLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(recordingLabel);

    recordingsFolderPathLabel.setJustificationType(juce::Justification::centredLeft);
    recordingsFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    updateRecordingsFolderLabel();
    addAndMakeVisible(recordingsFolderPathLabel);

    chooseFolderButton.setButtonText("Choose...");
    chooseFolderButton.onClick = [this] { chooseRecordingsFolder(); };
    addAndMakeVisible(chooseFolderButton);

    silenceTimeoutLabel.setText("Auto-stop after silence (s)", juce::dontSendNotification);
    addAndMakeVisible(silenceTimeoutLabel);

    silenceTimeoutSlider.setSliderStyle(juce::Slider::IncDecButtons);
    silenceTimeoutSlider.setRange(5.0, 600.0, 1.0);
    silenceTimeoutSlider.setValue(initialSilenceTimeoutSeconds, juce::dontSendNotification);
    silenceTimeoutSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
    silenceTimeoutSlider.onValueChange = [this]
    {
        if (onSilenceTimeoutChanged)
            onSilenceTimeoutChanged(silenceTimeoutSlider.getValue());
    };
    addAndMakeVisible(silenceTimeoutSlider);

    setSize(500, 530);
}

SettingsComponent::~SettingsComponent() {}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto channelRow = area.removeFromTop(30);
    channelCountLabel.setBounds(channelRow.removeFromLeft(100));
    channelCountSlider.setBounds(channelRow);

    area.removeFromTop(10);

    recordingLabel.setBounds(area.removeFromTop(20));

    auto folderRow = area.removeFromTop(26);
    chooseFolderButton.setBounds(folderRow.removeFromRight(90));
    folderRow.removeFromRight(6);
    recordingsFolderPathLabel.setBounds(folderRow);

    area.removeFromTop(6);
    auto silenceRow = area.removeFromTop(26);
    silenceTimeoutLabel.setBounds(silenceRow.removeFromLeft(180));
    silenceTimeoutSlider.setBounds(silenceRow);

    area.removeFromTop(10);
    selector->setBounds(area);
}

void SettingsComponent::updateRecordingsFolderLabel()
{
    recordingsFolderPathLabel.setText(
        recordingsFolder != juce::File() ? recordingsFolder.getFullPathName() : "(not set)",
        juce::dontSendNotification);
}

void SettingsComponent::chooseRecordingsFolder()
{
    auto startFolder = recordingsFolder != juce::File()
                          ? recordingsFolder
                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    activeFileChooser = std::make_unique<juce::FileChooser>("Choose a folder for recordings", startFolder);

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    activeFileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        if (result == juce::File())
            return;

        recordingsFolder = result;
        updateRecordingsFolderLabel();
        if (onRecordingsFolderChanged) onRecordingsFolderChanged(recordingsFolder);
    });
}
