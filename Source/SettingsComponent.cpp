#include "SettingsComponent.h"

SettingsComponent::SettingsComponent(juce::AudioDeviceManager& dm,
                                            int initialChannelCount,
                                            int maxChannelCount,
                                            std::function<void(int)> onChannelCountChangedIn)
    : deviceManager(dm),
      onChannelCountChanged(std::move(onChannelCountChangedIn))
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

    setSize(500, 450);
}

SettingsComponent::~SettingsComponent() {}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto channelRow = area.removeFromTop(30);
    channelCountLabel.setBounds(channelRow.removeFromLeft(100));
    channelCountSlider.setBounds(channelRow);

    area.removeFromTop(10);
    selector->setBounds(area);
}
