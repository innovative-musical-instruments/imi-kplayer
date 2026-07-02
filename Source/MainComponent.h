#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChannelProcessor.h"
#include "ChannelComponent.h"
#include "PluginManager.h"

class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::MidiInputCallback
{
public:
    MainComponent(juce::AudioDeviceManager& dm, PluginManager& pm);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override;

    void showPluginBrowser(bool isReplace);

    ChannelProcessor& getChannelProcessor() { return channelProcessor; }

private:
    juce::AudioDeviceManager& deviceManager;
    PluginManager& pluginManager;

    ChannelProcessor channelProcessor;
    std::unique_ptr<ChannelComponent> channelComponent;

    juce::MidiBuffer   pendingMidi;
    juce::CriticalSection midiLock;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
