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
#include "GlobalSectionComponent.h"
#include "PluginManager.h"
#include "LoadingOverlayComponent.h"
#include "SessionMigrator.h"
#include "MidiClockTempoDetector.h"
#include "RecordingManager.h"
#include "MidiTakePlayer.h"
#include "AudioTakePlayer.h"
#include "SessionTransport.h"

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

    // Called between the background scan finishing and the still-slower
    // synchronous startup work that follows it at launch (Kadabra recovery/
    // starter auto-load - see Main.cpp's scanPluginsAsync completion
    // callback) - swaps the overlay's text from "Scanning plugins..." to a
    // generic "warming up" message before that work (which blocks the
    // message thread directly, freezing the overlay's own animation/repaints
    // for its duration) begins, so what's frozen on screen doesn't wrongly
    // imply scanning is still what's happening. No-op if there's no overlay
    // up (e.g. called outside the startup path). See
    // LoadingOverlayComponent::setWarmingUp() for the rest of the reasoning.
    void showWarmingUpOverlay();

    // Settings dialog's "Rescan Plugins" button - replays exactly what
    // startup already does (same scanPluginsAsync call, same
    // LoadingOverlayComponent), so newly-installed plugins are picked up
    // the same way a fresh launch would find them. pluginsReady gates the
    // plugin browser shut for the duration, same as the startup scan.
    void rescanPlugins();

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

    // Multitrack recording (RecordingManager owns the actual engine/state -
    // see its header for the full design). Arming is per-source and purely
    // a selection; toggleRecording() is the one global transport action
    // that starts/stops every currently-armed source together as a single
    // take. Folder/timeout are session-persisted, like tempo.
    void setChannelArmed(int channelIndex, bool armed);
    bool isChannelArmed(int channelIndex) const { return recordingManager.isChannelArmed(channelIndex); }
    void setMasterArmed(bool armed);
    bool isMasterArmed() const { return recordingManager.isMasterArmed(); }

    // Starts if not currently recording, stops if it is. Returns an empty
    // string on success (including "was already in that state" no-ops), or
    // a user-facing reason on failure - callers show it directly.
    juce::String toggleRecording();
    bool isRecording() const { return recordingManager.isRecording(); }
    double getRecordingElapsedSeconds() const { return recordingManager.getRecordingElapsedSeconds(); }

    void setRecordingsFolder(const juce::File& folder) { recordingManager.setRecordingsFolder(folder); }
    juce::File getRecordingsFolder() const { return recordingManager.getRecordingsFolder(); }
    void setRecordingSilenceTimeoutSeconds(double seconds) { recordingManager.setSilenceTimeoutSeconds(seconds); }
    double getRecordingSilenceTimeoutSeconds() const { return recordingManager.getSilenceTimeoutSeconds(); }

    // Import Audio to Track (Increment E, see
    // docs/kplayer-take-recording-playback-spec.md section 9) - called by
    // Main.cpp's MainWindow once the user has picked both a source file and
    // a target channel (an existing one, or one just created via
    // setChannelCount(getNumChannels() + 1) for "New Track"). Returns an
    // empty string on success, or a user-facing error the caller shows
    // directly - same convention as toggleRecording(). On success, the
    // imported file is selected as channelIndex's audio input exactly as if
    // the user had picked it from the Audio Input Selector (see
    // ChannelComponent::selectAudioTake), including the same auto-bypass
    // convenience for a loaded slot-0 instrument.
    juce::String importAudioToChannel(int channelIndex, const juce::File& sourceFile);

    // Minimal session transport for MIDI Take playback (Increment B, see
    // docs/kplayer-take-recording-playback-spec.md and SessionTransport's
    // own header for the full design) - fully independent of
    // RecordingManager's own start/stop above; Play/Pause and Record are two
    // separate toggles driving/reading the same playhead.
    // Starts/pauses the transport; if Record Ready was armed and waiting
    // for exactly this play edge (see toggleRecordArm()), also starts
    // recording - no longer a trivial one-liner, see the .cpp.
    void toggleTransportPlaying();
    bool isTransportPlaying() const { return sessionTransport.isPlaying(); }
    void rtzTransport() { sessionTransport.rtz(); }

    // Emergency all-notes-off/all-sound-off. Message-thread setter only -
    // the actual injection happens on the audio thread at the top of the
    // next audioDeviceIOCallbackWithContext, which exchanges the flag back
    // to false once consumed (same pattern as every other MIDI-driven flag
    // in this class, just triggered from the UI instead of from MIDI).
    void triggerPanic() { panicRequested.store(true, std::memory_order_relaxed); }

    // Re-resolves channel i's midiDeviceIdentifier against the current
    // recordingsFolder: loads the corresponding MidiTakePlayer if it's a
    // take: identifier (see RecordingManager::isTakeIdentifier), unloads it
    // otherwise. Called by SessionIO::loadSession right after restoring a
    // channel's saved identifier - that path sets the identifier directly on
    // ChannelProcessor rather than through ChannelComponent's combo box, so
    // it doesn't go through onMidiTakeSelected/onMidiTakeDeselected below.
    void resolveMidiTakeSelectionForChannel(int index);
    void refreshChannelTakeList(int index) { channelComponents[(size_t) index]->refreshTakeList(); }

    // Same as the two above, but for Audio Take selection/playback
    // (Increment C, audioTakeIdentifier rather than midiDeviceIdentifier).
    void resolveAudioTakeSelectionForChannel(int index);
    void refreshChannelAudioTakeList(int index) { channelComponents[(size_t) index]->refreshAudioTakeList(); }

    // Fired whenever recording starts/stops/auto-stops, so the channel-strip
    // and master-column UI can refresh their arm/recording-active visuals
    // without polling - reason is non-empty only for an auto-stop (silence
    // timeout or low disk space), for a message the caller can show the user.
    std::function<void(juce::String reason)> onRecordingStateChanged;

    // Forwarded from GlobalSectionComponent's own callbacks - both need
    // MainWindow-level context this component doesn't have (a confirm
    // dialog for a destructive shrink, opening the Settings DialogWindow),
    // same shape as onDirty above.
    std::function<void(int newCount)> onChannelCountChangeRequested;
    std::function<void()> onSettingsRequested;

    // Same reasoning - Save/Save As live in Main.cpp's MainWindow (file
    // dialogs, currentSessionFile, recent-files list), not here. Fired from
    // MIDI (CC9 on channel 16, see timerCallback()) - see the atomic flags
    // below for the value split.
    std::function<void()> onSaveRequested;
    std::function<void()> onSaveAsRequested;

    // Same reasoning again - Open Session's file picker and loading
    // Starter.kplayer both live in Main.cpp's MainWindow. Fired from MIDI
    // (CC3 any value = open picker, CC99 value 0 = load Starter.kplayer
    // directly, see the atomic flags below).
    std::function<void()> onOpenSessionRequested;
    std::function<void()> onOpenStarterRequested;

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

    // "New Session" (File menu, Main.cpp) - drops the whole rig back to
    // exactly the blank state MainComponent starts in at launch when
    // there's no recover.kplayer/Starter.kplayer to auto-load (see Main.cpp's
    // tryAutoLoadKadabraSession() and its "no Kadabra port connected"
    // fallback): defaultChannelCount fresh, empty channels, no plugins
    // anywhere (channels or master chain), and every other session-scoped
    // field (tempo, master volume, tempo sync, recordings folder, arm
    // state, input-section collapse) back to its construction-time
    // default. Caller's job to gate this behind the usual unsaved-changes
    // prompt and clear its own notion of "current file" - this only
    // touches in-memory rig state, same division of responsibility as
    // SessionIO::loadSession().
    void resetToDefaultSession();

    bool channelHasLoadedPlugin(int index) const;

    float getMasterVolume() const { return masterVolume; }
    void  setMasterVolume(float linearGain);

    // Fired for genuine user-driven structural changes only - never during
    // SessionIO::loadSession, which mutates ChannelProcessors/state directly
    // rather than through these UI-facing paths.
    std::function<void()> onDirty;
    void notifyDirty() { if (onDirty) onDirty(); }

    // Drains every channel/master-chain plugin's parametersDirty flag
    // *without* calling notifyDirty() - called by the app-level owner right
    // after a successful save (or load) to discard any flag a plugin might
    // have set as an incidental side effect of being queried during that
    // same save/load (some plugins fire their own change-notification
    // internally from inside getStateInformation()/setStateInformation()).
    // Since the save/load itself runs synchronously on the message thread,
    // nothing else could have set the flag in that window, so discarding it
    // here is always safe.
    //
    // Deliberately no suppression window after this: live MIDI-driven
    // parameter changes (e.g. Kadabra motion control continuously steering
    // a loaded plugin) are a real, intended way to keep dirtying the
    // session mid-performance, and briefly holding back the indicator right
    // after a save would hide exactly that - see the session's earlier
    // discussion of why the "flag reappears immediately after saving"
    // behavior turned out to be correct, not a bug.
    void discardIncidentalDirtyFlags();

    // Extends the one-shot discard above into a brief window - call once,
    // right after a session load, alongside (not instead of)
    // discardIncidentalDirtyFlags(). Some plugins (HISE-based K-Samplers,
    // Kontakt, Surge XT - all with async init/sample-streaming behavior
    // already documented elsewhere in this codebase) settle on their own
    // schedule and can fire a change notification on a message-thread tick
    // *after* that one synchronous discard already ran, which the very next
    // 300ms timerCallback() poll then wrongly treats as a real edit - the
    // reported symptom this exists to fix: the dirty flag appearing right
    // after opening a session with no Kadabra motion or any other user
    // action at all. Deliberately load-only, not applied after a save - see
    // discardIncidentalDirtyFlags()'s own comment for why suppressing there
    // would hide genuine live Kadabra-driven edits mid-performance; nothing
    // equivalent is at stake right after a load, where a real edit in the
    // first second is implausible.
    void suppressPostLoadDirtyFlags();

    // Work Mode (default) vs. Show Mode: live MIDI-driven parameter changes
    // (see discardIncidentalDirtyFlags's comment just above) deliberately
    // keep dirtying the session during a performance, which is correct, but
    // means loading a different Muse mid-set hits the same Save/Discard/
    // Cancel prompt every time - fine while designing, disruptive live.
    // Show Mode makes Main.cpp's openSession()/openRecentFile() skip that
    // prompt entirely and just discard-and-load (Quit is untouched either
    // way - see silentKadabraQuit()'s own Kadabra-connected recovery path).
    // App-level, not session-persisted (the whole point is switching
    // between several .kplayer files without it flipping per-file) -
    // persisted across launches the same small-file way PluginManager
    // persists favorites/recently-used.
    bool isShowModeEnabled() const { return showModeEnabled; }
    void setShowModeEnabled(bool enabled);

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

    // Identifier of the first connected MIDI device whose name contains
    // "kadabra", or empty if none is connected right now - see the private
    // kadabraDeviceIdentifier/kadabraDeviceLock below for the caching
    // rationale. Exposed publicly (not just for this component's own
    // internal use) so Main.cpp's MainWindow can gate the Kadabra recovery/
    // starter-session feature (silentKadabraQuit()/
    // tryAutoLoadKadabraSession()) on the same live detection.
    juce::String getKadabraDeviceIdentifier() const
    {
        const juce::ScopedLock sl(kadabraDeviceLock);
        return kadabraDeviceIdentifier;
    }

private:
    // Polls every loaded plugin's parametersDirty flag (set from any thread,
    // including the audio thread mid-automation - see ChannelProcessor's
    // header comment) at a low, UI-appropriate rate and calls notifyDirty()
    // at most once per tick, no matter how many parameters changed or how
    // fast. This is deliberately not on the hot path.
    void timerCallback() override;
    static constexpr int dirtyPollIntervalMs = 300;

    // 0 = no active suppression window; otherwise a juce::Time::
    // getMillisecondCounter() deadline - see suppressPostLoadDirtyFlags()'s
    // own comment. timerCallback() keeps draining (not just single-shot
    // discarding) every plugin's parametersDirty flag without acting on it
    // until this deadline passes.
    juce::int64 postLoadDirtySuppressionEndMs = 0;
    static constexpr int postLoadDirtySuppressionMs = 1000;

    juce::AudioDeviceManager& deviceManager;
    PluginManager& pluginManager;

    std::vector<std::unique_ptr<ChannelProcessor>> channelProcessors;
    std::vector<std::unique_ptr<ChannelComponent>> channelComponents;

    // MIDI Take playback (Increment B) - one player per channel, parallel to
    // channelProcessors/channelComponents above and resized alongside them
    // in addChannel()/setChannelCount() (which already fully detaches the
    // audio callback before resizing those - see MidiTakePlayer's own header
    // comment for why that means this doesn't need RecordingManager's
    // fixed-size lock-free-array treatment). sessionTransport is the shared
    // playhead every player renders against - see its own header for the
    // full design.
    std::vector<std::unique_ptr<MidiTakePlayer>> midiTakePlayers;
    // Audio Take playback (Increment C) - same parallel-vector treatment as
    // midiTakePlayers above, driven by the same sessionTransport so a
    // channel's MIDI and Audio Takes from the same take stay in lockstep.
    std::vector<std::unique_ptr<AudioTakePlayer>> audioTakePlayers;
    SessionTransport sessionTransport;
    void loadMidiTakeForChannel(int index, const juce::File& file);
    void unloadMidiTakeForChannel(int index);
    void loadAudioTakeForChannel(int index, const juce::File& file);
    void unloadAudioTakeForChannel(int index);

    juce::Component channelRackContent;
    juce::Viewport  channelViewport;

    // Master bus insert chain (Increment 3) - applied once to the post-sum
    // stereo signal, after every channel but before the master volume
    // stage. masterChainComponent must be declared after masterChainProcessor
    // (member init order follows declaration order, and its constructor
    // takes a reference to it).
    MasterChainProcessor masterChainProcessor;
    MasterChainComponent masterChainComponent { masterChainProcessor };

    // Rightmost strip: branding, channel count, Settings access, the I/O
    // collapse toggle, tempo/sync (owns the actual TempoSyncComponent -
    // see getTempoSyncComponent()), transport, Record Ready, and Panic.
    // Constructed with the same starting values SettingsComponent used to
    // seed its own channel-count slider with.
    GlobalSectionComponent globalSection { defaultChannelCount, maxChannels };

    MidiClockTempoDetector tempoClockDetector;
    bool tempoSyncEnabled = false;
    juce::String tempoSyncDeviceIdentifier;

    RecordingManager recordingManager;
    // Kept alive for the duration of the lazy folder-picker prompt shown
    // when Record is clicked with no recordings folder configured yet (see
    // toggleRecording()'s caller in the constructor) - JUCE's FileChooser
    // must outlive the async dialog itself.
    std::unique_ptr<juce::FileChooser> recordingFolderChooser;

    // Master-level MIDI commands (arm = CC104, record = CC102, play = CC105,
    // quit = CC6 value 0, tempo step = CC100, save/save-as = CC9, open
    // session picker = CC3, open Starter.kplayer = CC99 value 0), reserved
    // on MIDI channel 16 of the Kadabra port specifically - written from
    // the audio thread in
    // audioDeviceIOCallbackWithContext(), consumed by timerCallback() the
    // same exchange-and-reset pattern as every other MIDI-driven flag in
    // this app. Record/Play are deliberately separate CCs (not one combined
    // control) - each is a level-based mirror of its own GUI button
    // (toggleRecordArm()/toggleTransportPlaying()), so a Kadabra hardware
    // combo that wants "arm then play" just sends both.
    std::atomic<bool> masterArmChangedByMidi { false };
    std::atomic<bool> pendingMasterArmValueFromMidi { false };
    std::atomic<bool> recordStateChangedByMidi { false };
    std::atomic<bool> pendingRecordStateValueFromMidi { false };
    std::atomic<bool> playStateChangedByMidi { false };
    std::atomic<bool> pendingPlayStateValueFromMidi { false };

    // CC6 value 0 - a one-shot trigger (not a level, unlike the above),
    // same shape as panicRequested below: set once on the audio thread,
    // exchanged-and-consumed once in timerCallback(), which routes it
    // through the exact same juce::JUCEApplication::systemRequestedQuit()
    // choke point every other quit path (Cmd+Q, Dock quit, File > Quit,
    // window close) already funnels through - guarantees this behaves
    // identically to those, including the silent-save-to-recover.kplayer
    // path when Kadabra is connected (which it always is here, by
    // definition - this only ever arrives on the Kadabra port).
    std::atomic<bool> quitRequestedByMidi { false };

    // CC9 - another one-shot trigger, same shape as quitRequestedByMidi:
    // value 0 -> Save As, any other value -> Save. Split into two flags
    // rather than one flag + a pending value, since the value only ever
    // matters at the instant the message arrives (which action to fire),
    // not as an ongoing state to compare against.
    std::atomic<bool> saveRequestedByMidi { false };
    std::atomic<bool> saveAsRequestedByMidi { false };

    // CC3 (any value) - Open Session's file picker. CC99 value 0 - load
    // Starter.kplayer directly, no picker (unlike CC3, only the exact
    // value 0 triggers it, no "else" action for other values).
    std::atomic<bool> openSessionRequestedByMidi { false };
    std::atomic<bool> openStarterRequestedByMidi { false };

    // Shared debounce for the one-shot MIDI commands above (quit/save/
    // save-as/open-session/open-starter, not the level-based arm/record/
    // play/tempo-step ones, which are already idempotent by construction).
    // A single momentary hardware button can send a press value and a
    // release value of 0 as two separate CC messages for what's really one
    // user gesture - CC9 in particular splits its action by exactly that
    // value (0 vs nonzero), so an unguarded press+release pair would fire
    // Save AND Save As back to back from one press. debounceOneShotMidiAction()
    // just ignores any one-shot trigger arriving within
    // oneShotMidiDebounceMs of the last one that fired, regardless of
    // which command it was - simple, and these are all occasional,
    // deliberate actions where a short shared cooldown is no real loss.
    static constexpr int oneShotMidiDebounceMs = 250;
    juce::uint32 lastOneShotMidiActionMs = 0;
    bool debounceOneShotMidiAction();

    // CC100 - each message is a discrete +1/-1 BPM step (value >= 64 =
    // +1, < 64 = -1), not a level like the commands above - a fast-spun
    // hardware encoder can send several ticks within one poll window, so
    // this accumulates net steps (fetch_add on the audio thread) rather
    // than just remembering the latest value, which would silently drop
    // all but one tick. Applying the whole net delta at once in
    // timerCallback() is mathematically identical to rounding-then-±1 per
    // individual tick, since every step after the first rounding is
    // already a whole number.
    std::atomic<int> tempoStepAccumulator { 0 };

    // Set by triggerPanic() (message thread, e.g. the master panic button or
    // a menu command), consumed and reset at the top of the very next audio
    // callback - see audioDeviceIOCallbackWithContext.
    std::atomic<bool> panicRequested { false };

    // Record Ready state machine (globalSection.recordButton's third state,
    // between idle and actually recording) - purely message-thread/click-
    // driven, no audio-thread access needed, so a plain bool is enough. See
    // toggleRecordArm()/startArmedRecording() for the actual transitions.
    bool recordArmedForNextPlay = false;
    void toggleRecordArm();
    void startArmedRecording();

    // Cached identifier of the first connected MIDI device whose name
    // contains "kadabra" (see findKadabraMidiDeviceIdentifier() in
    // MainComponent.cpp), refreshed on construction and on every device-list
    // change - reading juce::String directly on the audio thread isn't
    // safe (it's not lock-free), so this mirrors ChannelProcessor's own
    // midiDeviceIdentifier/midiDeviceLock pattern rather than re-deriving it
    // from juce::MidiInput::getAvailableDevices() (allocates, far too heavy
    // to call every audio block) each time it's needed there.
    mutable juce::CriticalSection kadabraDeviceLock;
    juce::String kadabraDeviceIdentifier;
    void refreshKadabraDeviceIdentifier();

    // Single global toggle (not per-channel) that collapses/expands the
    // Audio In/MIDI In/MIDI Ch rows on every channel strip at once - the
    // actual button lives in globalSection (see getInputSectionCollapsed()
    // callback wiring in the constructor), this just tracks the state.
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

    bool showModeEnabled = false;
    static juce::File getShowModeFile();

    std::unique_ptr<LoadingOverlayComponent> loadingOverlay;
    bool pluginsReady = false;

    // Set by showWarmingUpOverlay() - stops timerCallback()'s live scan-
    // status poll from overwriting the overlay's "warming up" message back
    // to a scanning one (the poll can't actually fire during the blocking
    // work that follows anyway, since that runs on this same message
    // thread, but this keeps the two states from ever fighting if that
    // ever changes).
    bool overlayShowingWarmup = false;

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
    // Snapshot of device identifiers seen the last time enableAllMidiInputs()
    // ran - lets it tell "just disappeared" apart from "was never here",
    // which is what makes a later reconnect actually reopen the MIDI stream
    // rather than silently no-op (see enableAllMidiInputs()'s own comment).
    juce::StringArray previouslyAvailableMidiInputIds;

    void addChannel(int index);
    void refreshRecordingUI();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
