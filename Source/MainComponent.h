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
#include "SessionMigrator.h"

class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::MidiInputCallback
{
public:
    static constexpr int maxChannels          = 24;
    static constexpr int defaultChannelCount  = 12;

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

    // Bulk resize (Increment 2). Clamped to [1, maxChannels]; growing adds
    // fresh empty channels, shrinking discards the truncated ones (and any
    // plugins loaded in them) - callers are responsible for confirming that
    // with the user first. Briefly detaches the audio callback while the
    // channel vectors are rebuilt, since audioDeviceIOCallbackWithContext
    // reads them directly on the audio thread with no lock.
    void setChannelCount(int newCount);

    bool channelHasLoadedPlugin(int index) const;

    float getMasterVolume() const { return masterVolume; }
    void  setMasterVolume(float linearGain);

    // Fired for genuine user-driven structural changes only - never during
    // SessionIO::loadSession, which mutates ChannelProcessors/state directly
    // rather than through these UI-facing paths.
    std::function<void()> onDirty;
    void notifyDirty() { if (onDirty) onDirty(); }

    // Session-format round-trip bookkeeping for SessionIO (spec §4.5/§5):
    // remembers the formatVersion and any unrecognized top-level fields from
    // the last loaded file, so re-saving a newer-than-supported file doesn't
    // silently downgrade it or drop data this app version doesn't understand.
    // Defaults to "brand new session, nothing loaded yet" - current version,
    // no extra fields.
    int  getLastLoadedFormatVersion() const { return lastLoadedFormatVersion; }
    void setLastLoadedFormatVersion(int formatVersion) { lastLoadedFormatVersion = formatVersion; }

    juce::var getLastLoadedExtraFields() const { return lastLoadedExtraFields; }
    void setLastLoadedExtraFields(juce::var extraFields) { lastLoadedExtraFields = std::move(extraFields); }

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

    int       lastLoadedFormatVersion = SessionMigrator::kCurrentFormatVersion;
    juce::var lastLoadedExtraFields;

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

    void addChannel(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
