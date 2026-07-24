#pragma once
#include <atomic>
#include <map>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChannelProcessor.h"
#include "ChannelComponent.h"
#include "MasterChainProcessor.h"
#include "MasterChainComponent.h"
#include "BrandingStripComponent.h"
#include "PluginManager.h"
#include "LoadingOverlayComponent.h"
#include "SessionMigrator.h"
#include "TempoSyncComponent.h"
#include "MidiClockTempoDetector.h"

class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::MidiInputCallback,
                      private juce::Timer
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
    void showMasterChainPluginBrowser(int slotIndex, bool isReplace);

    // Called on the message thread once PluginManager's background scan finishes.
    void onScanComplete();

    // Tempo is transport-wide, applied to every channel's playhead.
    void   setGlobalTempo(double bpm);
    double getGlobalTempo() const { return currentTempo; }

    // MIDI-clock tempo sync (moved out of the Settings dialog into the
    // master column, see TempoSyncComponent) - MainComponent owns this
    // state and is the only thing that reads MIDI devices for it; the UI
    // component just reflects it. tempoSyncDeviceIdentifier empty means "no
    // sync source picked yet", independent of whether sync itself is on.
    bool isTempoSyncEnabled() const { return tempoSyncEnabled; }
    void setTempoSyncEnabled(bool enabled);
    juce::String getTempoSyncDeviceIdentifier() const { return tempoSyncDeviceIdentifier; }
    void setTempoSyncDeviceIdentifier(juce::String identifier);

    // Window size round-trips through the session the same way tempo does:
    // MainWindow (which actually owns the DocumentWindow) writes its current
    // size here right before SessionIO::saveSession(), and reads it back
    // after SessionIO::loadSession() to resize itself - MainComponent has
    // no window of its own, it's just where SessionIO expects session state
    // to live. 0 means "not set" (fresh session, or a file saved before
    // this feature existed) - callers should leave the window size alone.
    void setWindowSize(int width, int height) { savedWindowWidth = width; savedWindowHeight = height; }
    int getSavedWindowWidth() const  { return savedWindowWidth; }
    int getSavedWindowHeight() const { return savedWindowHeight; }

    // Accessors for SessionIO - it reads/writes channel and master state
    // without needing its own copy of MainComponent's internals.
    int getNumChannels() const { return (int) channelProcessors.size(); }
    ChannelProcessor& getChannelProcessor(int index) { return *channelProcessors[(size_t) index]; }
    void refreshChannelUI(int index) { channelComponents[(size_t) index]->refresh(); }

    MasterChainProcessor& getMasterChainProcessor() { return masterChainProcessor; }
    void refreshMasterChainUI() { masterChainComponent.refresh(); }

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

    // Global collapse of the Audio In/MIDI In/MIDI Ch rows on every channel
    // strip. Session-persisted, like window size - setInputSectionCollapsedState()
    // applies the state without marking dirty (used on session load);
    // toggleInputSectionCollapsed() is the user-driven path and does.
    bool isInputSectionCollapsed() const { return inputSectionCollapsed; }
    void setInputSectionCollapsedState(bool collapsed);
    void toggleInputSectionCollapsed();

private:
    // Polls every loaded plugin's parametersDirty flag (set from any thread,
    // including the audio thread mid-automation - see ChannelProcessor's
    // header comment) at a low, UI-appropriate rate and calls notifyDirty()
    // at most once per tick, no matter how many parameters changed or how
    // fast. This is deliberately not on the hot path.
    void timerCallback() override;
    static constexpr int dirtyPollIntervalMs = 300;

    juce::AudioDeviceManager& deviceManager;
    PluginManager& pluginManager;

    std::vector<std::unique_ptr<ChannelProcessor>> channelProcessors;
    std::vector<std::unique_ptr<ChannelComponent>> channelComponents;

    juce::Component channelRackContent;
    juce::Viewport  channelViewport;

    // Master bus insert chain (Increment 3) - applied once to the post-sum
    // stereo signal, after every channel but before the master volume
    // stage. masterChainComponent must be declared after masterChainProcessor
    // (member init order follows declaration order, and its constructor
    // takes a reference to it).
    MasterChainProcessor masterChainProcessor;
    MasterChainComponent masterChainComponent { masterChainProcessor };

    // IMI + Tribal Tools logos, fixed height, directly above the master
    // strip (spec item 2).
    BrandingStripComponent brandingStrip;

    // Tempo + MIDI-clock sync control, between collapseInputButton and
    // masterChainComponent in the master column (see resized()).
    TempoSyncComponent tempoSyncComponent;
    MidiClockTempoDetector tempoClockDetector;
    bool tempoSyncEnabled = false;
    juce::String tempoSyncDeviceIdentifier;

    // Single global toggle (not per-channel) that collapses/expands the
    // Audio In/MIDI In/MIDI Ch rows on every channel strip at once - lives
    // in the master column so it's visible regardless of channel-rack
    // scroll position.
    juce::TextButton collapseInputButton;
    bool inputSectionCollapsed = false;

    // Nothing renders a SettableTooltipClient's tooltip text without one of
    // these existing somewhere in the app - it watches the whole desktop
    // for hover, not just its own bounds.
    juce::TooltipWindow tooltipWindow;

    // Displayed by masterChainComponent's own fader/label, integrated
    // below its insert slots - MainComponent still owns the underlying
    // gain value and atomics, since that's what the audio callback reads.
    float        masterVolume = 1.0f;

    // Post-sum master bus peak metering (Increment 3) - same pattern as
    // ChannelProcessor's per-channel metering: audio thread only writes,
    // PeakMeterComponent's Timer is the only reader. Separate left/right
    // clip flags (rather than one combined flag) since the master strip
    // now shows two independent single-channel meters flanking the fader,
    // each consuming its own flag via exchange() - sharing one flag between
    // two independent readers would let only one of them ever observe a
    // given clip.
    std::atomic<float> masterPeakLeft      { 0.0f };
    std::atomic<float> masterPeakRight     { 0.0f };
    std::atomic<bool>  masterClipFlagLeft  { false };
    std::atomic<bool>  masterClipFlagRight { false };

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
    int savedWindowWidth  = 0;
    int savedWindowHeight = 0;

    juce::MidiDeviceListConnection midiDeviceListConnection;
    void enableAllMidiInputs();

    void addChannel(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
