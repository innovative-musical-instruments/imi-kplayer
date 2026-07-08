#include "SettingsComponent.h"

SettingsComponent::SettingsComponent(juce::AudioDeviceManager& dm,
                                            double initialTempo,
                                            std::function<void(double)> onTempoChangedIn,
                                            int initialChannelCount,
                                            int maxChannelCount,
                                            std::function<void(int)> onChannelCountChangedIn)
    : deviceManager(dm), onTempoChanged(std::move(onTempoChangedIn)),
      onChannelCountChanged(std::move(onChannelCountChangedIn))
{
    selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager,
        0,    // min input channels
        0,    // max input channels
        2,    // min output channels
        2,    // max output channels
        true, // show MIDI input
        false,// show MIDI output
        false,// stereo pair
        false // hide advanced options
    );
    addAndMakeVisible(selector.get());

    tempoLabel.setText("Tempo (BPM)", juce::dontSendNotification);
    addAndMakeVisible(tempoLabel);

    tempoSlider.setRange(20.0, 300.0, 0.1);
    tempoSlider.setValue(initialTempo, juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    tempoSlider.onValueChange = [this] { if (onTempoChanged) onTempoChanged(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

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

    setSize(500, 490);
}

SettingsComponent::~SettingsComponent() {}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto tempoRow = area.removeFromTop(30);
    tempoLabel.setBounds(tempoRow.removeFromLeft(100));
    tempoSlider.setBounds(tempoRow);

    area.removeFromTop(10);

    auto channelRow = area.removeFromTop(30);
    channelCountLabel.setBounds(channelRow.removeFromLeft(100));
    channelCountSlider.setBounds(channelRow);

    area.removeFromTop(10);
    selector->setBounds(area);
}
