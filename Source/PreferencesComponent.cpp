#include "PreferencesComponent.h"

PreferencesComponent::PreferencesComponent(juce::AudioDeviceManager& dm, ChannelProcessor& cp)
    : deviceManager(dm), channelProcessor(cp)
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
    tempoSlider.setValue(channelProcessor.getTempo(), juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    tempoSlider.onValueChange = [this] { channelProcessor.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    setSize(500, 450);
}

PreferencesComponent::~PreferencesComponent() {}

void PreferencesComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto tempoRow = area.removeFromTop(30);
    tempoLabel.setBounds(tempoRow.removeFromLeft(100));
    tempoSlider.setBounds(tempoRow);

    area.removeFromTop(10);
    selector->setBounds(area);
}
