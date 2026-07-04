#pragma once
#include <map>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChannelProcessor.h"
#include "ChannelComponent.h"
#include "PluginManager.h"
#include "LoadingOverlayComponent.h"

class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::MidiInputCallback
{
public:
    static constexpr int numChannels = 12;

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

    void showPluginBrowser(int channelIndex, int slotIndex, bool isReplace);

    // Called on the message thread once PluginManager's background scan finishes.
    void onScanComplete();

    // Tempo is transport-wide, applied to every channel's playhead.
    void   setGlobalTempo(double bpm);
    double getGlobalTempo() const { return currentTempo; }

    // Accessors for SessionIO - it reads/writes channel and master state
    // without needing its own copy of MainComponent's internals.
    int getNumChannels() const { return (int) channelProcessors.size(); }
    ChannelProcessor& getChannelProcessor(int index) { return *channelProcessors[(size_t) index]; }
    void refreshChannelUI(int index) { channelComponents[(size_t) index]->refresh(); }

    float getMasterVolume() const { return masterVolume; }
    void  setMasterVolume(float linearGain);

private:
    juce::AudioDeviceManager& deviceManager;
    PluginManager& pluginManager;

    std::vector<std::unique_ptr<ChannelProcessor>> channelProcessors;
    std::vector<std::unique_ptr<ChannelComponent>> channelComponents;

    juce::Component channelRackContent;
    juce::Viewport  channelViewport;

    // Nothing renders a SettableTooltipClient's tooltip text without one of
    // these existing somewhere in the app - it watches the whole desktop
    // for hover, not just its own bounds.
    juce::TooltipWindow tooltipWindow;

    juce::Label  masterVolumeLabel;
    juce::Slider masterVolumeSlider;
    float        masterVolume = 1.0f;

    std::unique_ptr<LoadingOverlayComponent> loadingOverlay;
    bool pluginsReady = false;

    // Keyed by MidiInput device identifier so each channel can be routed to
    // a specific (device, channel-number) pair per spec 7.1.
    std::map<juce::String, juce::MidiBuffer> pendingMidiByDevice;
    juce::CriticalSection midiLock;

    juce::AudioBuffer<float> channelScratch;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;
    double currentTempo      = 120.0;

    juce::MidiDeviceListConnection midiDeviceListConnection;
    void enableAllMidiInputs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
