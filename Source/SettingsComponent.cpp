#include "SettingsComponent.h"

SettingsComponent::SettingsComponent(juce::AudioDeviceManager& dm,
                                            const juce::File& initialRecordingsFolder,
                                            double initialSilenceTimeoutSeconds,
                                            std::function<void(juce::File)> onRecordingsFolderChangedIn,
                                            std::function<void(double)> onSilenceTimeoutChangedIn)
    : deviceManager(dm),
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
    // Initial size is just a starting point for its own self-sizing layout
    // pass (width drives its internal proportional layout, height is
    // immediately overridden to fit its actual content - see the
    // audioSettingsViewport comment in the header) - not a final size.
    selector->setSize(460, 400);
    audioSettingsViewport.setViewedComponent(selector.get(), false);
    audioSettingsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(audioSettingsViewport);

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

    pluginsLabel.setText("Plugins", juce::dontSendNotification);
    pluginsLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    pluginsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pluginsLabel);

    rescanPluginsButton.setButtonText("Rescan Plugins");
    rescanPluginsButton.setTooltip("Scan for new/updated plugins, same as at app launch");
    rescanPluginsButton.onClick = [this] { if (onRescanRequested) onRescanRequested(); };
    addAndMakeVisible(rescanPluginsButton);

    setSize(540, 600);
}

SettingsComponent::~SettingsComponent() {}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Measured approximation of where AudioDeviceSelectorComponent's own
    // "Test" button (Output row, below) sits - that's a JUCE built-in
    // component whose internal layout isn't exposed, so this is a fixed
    // inset calibrated against a screenshot rather than a shared value;
    // nudge this one number if it drifts as the dialog width changes.
    const int audioSelectorRightInset = 28;

    recordingLabel.setBounds(area.removeFromTop(20));

    auto folderRow = area.removeFromTop(26);
    folderRow.removeFromRight(audioSelectorRightInset);
    chooseFolderButton.setBounds(folderRow.removeFromRight(90));
    folderRow.removeFromRight(6);
    recordingsFolderPathLabel.setBounds(folderRow);

    area.removeFromTop(6);
    auto silenceRow = area.removeFromTop(26);
    silenceTimeoutLabel.setBounds(silenceRow.removeFromLeft(180));
    silenceRow.removeFromRight(audioSelectorRightInset);
    silenceTimeoutSlider.setBounds(silenceRow);

    // Heading beside the button now, not above it - same row, left-aligned
    // the same way the Recording/Auto-stop labels are (fixed-width column,
    // matching silenceTimeoutLabel's own 180px).
    area.removeFromTop(10);
    auto pluginsRow = area.removeFromTop(26);
    pluginsLabel.setBounds(pluginsRow.removeFromLeft(180));
    rescanPluginsButton.setBounds(pluginsRow.removeFromLeft(160));

    area.removeFromTop(10);
    audioSettingsViewport.setBounds(area);

    // Width drives the selector's internal proportional layout and needs
    // updating on every pass (e.g. the dialog got resized); height is its
    // own self-sizing business (see the audioSettingsViewport comment in
    // the header) - pass its current height back in as a no-op starting
    // point rather than guessing one here.
    auto contentWidth = audioSettingsViewport.getWidth() - audioSettingsViewport.getScrollBarThickness();
    selector->setSize(juce::jmax(1, contentWidth), selector->getHeight());
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
