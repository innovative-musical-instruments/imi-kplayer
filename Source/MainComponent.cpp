#include "MainComponent.h"
#include "PluginBrowserComponent.h"
#include <array>

namespace
{
    // First available MIDI input device whose name contains "kadabra"
    // (case-insensitive), or an empty identifier if none is connected -
    // same match ChannelComponent used to do itself for its own default,
    // centralised here now since choosing a new channel's default also
    // needs to see every *other* channel's current assignment.
    juce::String findKadabraMidiDeviceIdentifier()
    {
        for (auto& device : juce::MidiInput::getAvailableDevices())
            if (device.name.containsIgnoreCase("kadabra"))
                return device.identifier;
        return {};
    }
}

MainComponent::MainComponent(juce::AudioDeviceManager& dm, PluginManager& pm)
    : deviceManager(dm), pluginManager(pm)
{
    for (int i = 0; i < defaultChannelCount; ++i)
        addChannel(i);
    recordingManager.setChannelCount(defaultChannelCount);

    channelViewport.setViewedComponent(&channelRackContent, false);
    channelViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(channelViewport);

    masterChainComponent.onLoadPlugin    = [this](int slot) { showMasterChainPluginBrowser(slot, false); };
    masterChainComponent.onReplacePlugin = [this](int slot) { showMasterChainPluginBrowser(slot, true);  };
    masterChainComponent.onDirty         = [this] { notifyDirty(); };
    masterChainComponent.onVolumeChanged = [this](float linearGain) { masterVolume = linearGain; notifyDirty(); };
    masterChainProcessor.onBypassChanged = [this](int) { masterChainComponent.refresh(); };
    masterChainComponent.onMasterArmToggled = [this](bool armed) { setMasterArmed(armed); notifyDirty(); };
    masterChainComponent.setLevelMeterSources(&masterPeakLeft, &masterPeakRight,
                                              &masterClipFlagLeft, &masterClipFlagRight);
    addAndMakeVisible(masterChainComponent);

    // Both need MainWindow-level context this component doesn't have
    // (a confirm dialog for a destructive shrink, opening the Settings
    // DialogWindow) - just forwarded up to whatever Main.cpp wires there.
    globalSection.onChannelCountChangeRequested = [this](int newCount)
    {
        if (onChannelCountChangeRequested) onChannelCountChangeRequested(newCount);
    };
    globalSection.onSettingsRequested = [this] { if (onSettingsRequested) onSettingsRequested(); };
    globalSection.onCollapseToggled   = [this] { toggleInputSectionCollapsed(); };
    globalSection.onPlayPauseClicked  = [this] { toggleTransportPlaying(); };
    globalSection.onRtzClicked        = [this] { rtzTransport(); };
    globalSection.onPanicClicked      = [this] { triggerPanic(); };
    // Record Ready click routing (idle -> armed -> recording -> idle) -
    // see toggleRecordArm()'s own comment for the full state machine.
    globalSection.onRecordButtonClicked = [this] { toggleRecordArm(); };
    globalSection.onShowModeToggled = [this] { setShowModeEnabled(! showModeEnabled); };
    addAndMakeVisible(globalSection);

    // Loaded here (not via an in-class initializer, since globalSection
    // itself needs to already exist to receive the pushed value) - app-
    // level persisted setting, see isShowModeEnabled()'s own comment.
    setShowModeEnabled(getShowModeFile().loadFileAsString().trim() == "1");

    // Manual edits only apply while sync is off (TempoSyncComponent itself
    // won't even let the value label be edited while synced, but the guard
    // here is what actually matters). Sync-on/off and device changes are
    // deliberate user actions - unlike the sync-driven tempo ticks in
    // timerCallback(), they do mark the session dirty.
    auto& tempoSync = globalSection.getTempoSyncComponent();
    tempoSync.onTempoChanged = [this](double bpm)
    {
        if (tempoSyncEnabled) return;
        setGlobalTempo(bpm);
        notifyDirty();
    };
    tempoSync.onSyncToggled = [this](bool enabled)
    {
        setTempoSyncEnabled(enabled);
        notifyDirty();
    };
    tempoSync.onSyncDeviceChanged = [this](juce::String identifier)
    {
        setTempoSyncDeviceIdentifier(std::move(identifier));
        notifyDirty();
    };

    // Fires from RecordingManager's silence/disk-space watchdog (see
    // timerCallback() -> pollForAutoStop()) - refresh the arm/recording
    // visuals the same way a manual stop would, and forward the reason so
    // the app-level owner (Main.cpp) can show it to the user.
    recordingManager.onAutoStopped = [this](juce::String reason)
    {
        refreshRecordingUI();
        if (onRecordingStateChanged) onRecordingStateChanged(reason);
    };

    deviceManager.addAudioCallback(this);

    enableAllMidiInputs();
    refreshKadabraDeviceIdentifier();

    // Devices connected/disconnected after launch never got a callback
    // registered at all otherwise - re-enable/register on every device-list
    // change rather than only once at startup.
    midiDeviceListConnection = juce::MidiDeviceListConnection::make([this]
    {
        enableAllMidiInputs();
        refreshKadabraDeviceIdentifier();
    });

    loadingOverlay = std::make_unique<LoadingOverlayComponent>();
    addAndMakeVisible(loadingOverlay.get());

    setSize(1152, 800);

    startTimer(dirtyPollIntervalMs);
}

void MainComponent::addChannel(int index)
{
    auto processor = std::make_unique<ChannelProcessor>();
    processor->setTempo(currentTempo);
    processor->prepareToPlay(currentSampleRate, currentBlockSize);

    // getName() is still purely an optional custom name (item 1.1) - but the
    // fixed, non-editable "Channel N" number is now also mirrored onto the
    // processor itself (setChannelNumber) so showEditor() can put it in a
    // loaded plugin's window title, not just on the ChannelComponent label.
    processor->setChannelNumber(index + 1);

    // Default a fresh channel to the next free Kadabra MIDI channel: scan
    // every already-existing channel's *current* device+channel assignment
    // (not a running counter) so this stays correct no matter how channels
    // were added/removed/reassigned before now. Falls back to MIDI In =
    // None / Ch = All (ChannelComponent's own construction-time default)
    // once all 16 Kadabra channels are already claimed, or if no Kadabra
    // port is connected at all.
    auto kadabraId = findKadabraMidiDeviceIdentifier();
    if (kadabraId.isNotEmpty())
    {
        std::array<bool, 16> claimed {};
        for (auto& existing : channelProcessors)
        {
            int ch = existing->getMidiChannel();
            if (ch >= 1 && ch <= 16 && existing->getMidiDeviceIdentifier() == kadabraId)
                claimed[(size_t) (ch - 1)] = true;
        }

        for (int i = 0; i < 16; ++i)
        {
            if (! claimed[(size_t) i])
            {
                processor->setMidiDeviceIdentifier(kadabraId);
                processor->setMidiChannel(i + 1);
                break;
            }
        }
    }

    auto component = std::make_unique<ChannelComponent>(*processor, deviceManager, recordingManager, index + 1);
    component->onLoadPlugin    = [this, index](int slot) { showPluginBrowser(index, slot, false); };
    component->onReplacePlugin = [this, index](int slot) { showPluginBrowser(index, slot, true);  };
    component->onDirty         = [this] { notifyDirty(); };
    processor->onBypassChanged = [this, index](int) { channelComponents[(size_t) index]->refresh(); };
    component->onArmToggled    = [this, index](bool armed) { setChannelArmed(index, armed); notifyDirty(); };
    component->onMidiTakeSelected   = [this, index](const juce::File& file) { loadMidiTakeForChannel(index, file); };
    component->onMidiTakeDeselected = [this, index] { unloadMidiTakeForChannel(index); };
    component->onAudioTakeSelected   = [this, index](const juce::File& file) { loadAudioTakeForChannel(index, file); };
    component->onAudioTakeDeselected = [this, index] { unloadAudioTakeForChannel(index); };
    component->setInputSectionCollapsed(inputSectionCollapsed);
    channelRackContent.addAndMakeVisible(component.get());

    channelProcessors.push_back(std::move(processor));
    channelComponents.push_back(std::move(component));
    midiTakePlayers.push_back(std::make_unique<MidiTakePlayer>());
    audioTakePlayers.push_back(std::make_unique<AudioTakePlayer>());
}

void MainComponent::setChannelCount(int newCount)
{
    newCount = juce::jlimit(1, maxChannels, newCount);
    int oldCount = (int) channelProcessors.size();
    if (newCount == oldCount)
        return;

    // audioDeviceIOCallbackWithContext walks channelProcessors directly on
    // the audio thread with no lock, so the vectors can't be resized while
    // callbacks are still arriving - detach for the (brief) rebuild.
    deviceManager.removeAudioCallback(this);

    if (newCount > oldCount)
    {
        for (int i = oldCount; i < newCount; ++i)
            addChannel(i);
    }
    else
    {
        // Shrinking unique_ptrs destroys the dropped ChannelComponents,
        // which removes them from channelRackContent automatically.
        channelComponents.resize((size_t) newCount);
        channelProcessors.resize((size_t) newCount);
        midiTakePlayers.resize((size_t) newCount);
        audioTakePlayers.resize((size_t) newCount);
    }

    recordingManager.setChannelCount(newCount);

    deviceManager.addAudioCallback(this);
    globalSection.setChannelCount(newCount);
    resized();
}

void MainComponent::setInputSectionCollapsedState(bool collapsed)
{
    inputSectionCollapsed = collapsed;
    globalSection.setInputSectionCollapsed(inputSectionCollapsed);
    for (auto& c : channelComponents)
        c->setInputSectionCollapsed(inputSectionCollapsed);
}

void MainComponent::toggleInputSectionCollapsed()
{
    setInputSectionCollapsedState(! inputSectionCollapsed);
    notifyDirty();
}

juce::File MainComponent::getShowModeFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("IMI").getChildFile("KPlayer").getChildFile("show_mode.txt");
}

void MainComponent::setShowModeEnabled(bool enabled)
{
    showModeEnabled = enabled;
    globalSection.setShowModeEnabled(showModeEnabled);

    auto file = getShowModeFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(showModeEnabled ? "1" : "0");
}

bool MainComponent::channelHasLoadedPlugin(int index) const
{
    auto& proc = *channelProcessors[(size_t) index];
    for (int slot = 0; slot < ChannelProcessor::totalSlotCount; ++slot)
        if (proc.hasPlugin(slot))
            return true;
    return false;
}

void MainComponent::enableAllMidiInputs()
{
    juce::StringArray currentIds;
    for (auto& input : juce::MidiInput::getAvailableDevices())
        currentIds.add(input.identifier);

    // A device that just disappeared (e.g. Kadabra OS quitting) needs to be
    // explicitly disabled here, not left alone: AudioDeviceManager never
    // notices the underlying hardware vanished, so isMidiInputDeviceEnabled()
    // keeps reporting true for it - which means the *next* time it
    // reconnects and we call setMidiInputDeviceEnabled(id, true) below,
    // JUCE sees "already enabled" and silently skips reopening the native
    // MIDI stream (see AudioDeviceManager::setMidiInputDeviceEnabled's
    // enabled != isMidiInputDeviceEnabled(identifier) guard) - the device
    // shows as connected again in every KPlayer UI (the identifier was never
    // cleared) but no messages actually arrive, until something forces a
    // genuine disable->enable transition (which is exactly what manually
    // toggling the device's checkbox in Settings does by hand). Disabling
    // it the moment it disappears keeps AudioDeviceManager's own state
    // accurate immediately, so the ordinary enable call in the loop below
    // is a real state change - and therefore a real reopen - the next time
    // this same identifier shows back up.
    for (auto& id : previouslyAvailableMidiInputIds)
        if (! currentIds.contains(id))
            deviceManager.setMidiInputDeviceEnabled(id, false);

    // addMidiInputDeviceCallback() removes any existing registration for the
    // same (identifier, callback) pair before re-adding, so it's safe to
    // call this repeatedly for devices that were already enabled.
    for (auto& input : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }

    previouslyAvailableMidiInputIds = currentIds;
}

void MainComponent::refreshKadabraDeviceIdentifier()
{
    auto id = findKadabraMidiDeviceIdentifier();
    const juce::ScopedLock sl(kadabraDeviceLock);
    kadabraDeviceIdentifier = id;
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback(this);

    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto& input : midiInputs)
        deviceManager.removeMidiInputDeviceCallback(input.identifier, this);
}

void MainComponent::onScanComplete()
{
    pluginsReady = true;
    loadingOverlay.reset();

    // Surface a crash from the *previous* scan (this one just finished
    // cleanly, or we wouldn't have got here at all - see PluginManager's
    // own comment on why this list is safe to read exactly once here).
    // The plugin(s) are already blacklisted, not silently missing - point
    // at the plugin browser's "Failed to Load" section (Increment 4) for
    // the retry path rather than duplicating it here.
    auto crashed = pluginManager.getPluginsSkippedByLastCrash();
    if (! crashed.isEmpty())
    {
        juce::String message = crashed.size() == 1
            ? "1 plugin crashed during a previous scan and was automatically skipped:\n\n"
            : juce::String(crashed.size()) + " plugins crashed during a previous scan and were automatically skipped:\n\n";
        message += crashed.joinIntoString("\n");
        message += "\n\nThey've been marked as failed - open the plugin browser's \"Failed to Load\" "
                    "section to retry one.";

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Plugin(s) Skipped After Crashing", message);
    }
}

void MainComponent::rescanPlugins()
{
    pluginsReady = false;
    loadingOverlay = std::make_unique<LoadingOverlayComponent>();
    addAndMakeVisible(loadingOverlay.get());
    loadingOverlay->setBounds(getLocalBounds());
    pluginManager.scanPluginsAsync([this] { onScanComplete(); });
}

void MainComponent::timerCallback()
{
    // Live "which plugin is it on right now" status for the scan overlay
    // (startup or Settings' Rescan button) - see PluginManager's own
    // comment on why reading these here, on the message thread, is safe.
    if (loadingOverlay != nullptr)
        loadingOverlay->setScanStatus(pluginManager.getCurrentlyScanningPluginName(),
                                       pluginManager.getScanProgress());

    // Drain every processor's flag unconditionally (not short-circuiting on
    // the first hit) so none are left set from this tick to linger into the
    // next one, then notify at most once regardless of how many fired.
    bool anyDirty = false;
    for (size_t i = 0; i < channelProcessors.size(); ++i)
    {
        auto& processor = channelProcessors[i];
        anyDirty |= processor->consumeParametersDirtyFlag();

        // A MIDI CC7 message just changed this channel's gain (see
        // ChannelProcessor::processBlock) - refresh its fader to match, and
        // mark dirty the same as a manual fader drag would (sliderValueChanged
        // always calls onDirty(), so this stays consistent with that).
        if (processor->consumeGainChangedByMidi())
        {
            channelComponents[i]->refresh();
            anyDirty = true;
        }

        // A MIDI CC10 message just changed this channel's pan (see
        // ChannelProcessor::processBlock) - same refresh-and-mark-dirty
        // treatment as CC7 gain above.
        if (processor->consumePanChangedByMidi())
        {
            channelComponents[i]->refresh();
            anyDirty = true;
        }

        // A MIDI CC84-89 message just bypassed/activated a slot (see
        // ChannelProcessor::processBlock) - sync any open editor window's
        // bypass bar and the slot buttons to match, and mark dirty the same
        // as the slot's own context-menu bypass toggle would.
        if (processor->consumeBypassChangedByMidi())
        {
            processor->syncBypassIndicatorsFromMidi();
            channelComponents[i]->refresh();
            anyDirty = true;
        }

        // A MIDI CC103 message just requested this channel be armed/disarmed
        // for recording (see ChannelProcessor::processBlock) - setChannelArmed()
        // already pushes the UI update itself, same path the channel's own
        // arm button uses, so nothing further to do here besides marking dirty.
        bool requestedArmed = false;
        if (processor->consumeArmChangedByMidi(requestedArmed))
        {
            setChannelArmed((int) i, requestedArmed);
            anyDirty = true;
        }
    }
    anyDirty |= masterChainProcessor.consumeParametersDirtyFlag();

    // Master-level commands on the Kadabra port's MIDI channel 16 (see
    // audioDeviceIOCallbackWithContext) - level-based, only acted on when
    // they actually disagree with the current state, so a continuously-
    // streaming controller (e.g. Kadabra motion) re-sending the same value
    // doesn't repeatedly re-trigger a start/stop or arm/disarm.
    if (masterArmChangedByMidi.exchange(false, std::memory_order_relaxed))
    {
        setMasterArmed(pendingMasterArmValueFromMidi.load(std::memory_order_relaxed));
        anyDirty = true;
    }

    if (recordStateChangedByMidi.exchange(false, std::memory_order_relaxed))
    {
        bool desiredRecording = pendingRecordStateValueFromMidi.load(std::memory_order_relaxed);
        if (desiredRecording != recordingManager.isRecording())
            toggleRecording(); // MIDI-triggered failures fail quietly, same as a live-arm track-creation failure - see RecordingManager::setChannelArmed's comment
    }

    if (anyDirty)
        notifyDirty();

    if (tempoSyncEnabled && tempoSyncDeviceIdentifier.isNotEmpty())
    {
        bool hasSignal = tempoClockDetector.hasSignal();
        globalSection.getTempoSyncComponent().setSyncSignalWarning(! hasSignal);

        // Holds the last-known tempo (rather than reverting to the manual
        // value) once the signal drops, per the earlier design discussion -
        // setGlobalTempo() just isn't called again until pulses resume.
        if (hasSignal && tempoClockDetector.hasLockedTempo())
            setGlobalTempo(tempoClockDetector.getBpm());
    }

    recordingManager.pollForAutoStop();
    recordingManager.pollMidiCapture();
}

void MainComponent::discardIncidentalDirtyFlags()
{
    for (auto& processor : channelProcessors)
        processor->consumeParametersDirtyFlag();
    masterChainProcessor.consumeParametersDirtyFlag();
}

void MainComponent::setGlobalTempo(double bpm)
{
    currentTempo = bpm;
    for (auto& channel : channelProcessors)
        channel->setTempo(bpm);
    masterChainProcessor.setTempo(bpm);
    globalSection.getTempoSyncComponent().setDisplayedTempo(bpm);
}

void MainComponent::setTempoSyncEnabled(bool enabled)
{
    tempoSyncEnabled = enabled;
    globalSection.getTempoSyncComponent().setSyncEnabled(enabled);
    if (! enabled)
        globalSection.getTempoSyncComponent().setSyncSignalWarning(false);
}

void MainComponent::setTempoSyncDeviceIdentifier(juce::String identifier)
{
    tempoSyncDeviceIdentifier = std::move(identifier);
    globalSection.getTempoSyncComponent().setSyncDeviceIdentifier(tempoSyncDeviceIdentifier);

    // A stray interval spanning the old and new device's pulse streams
    // must not get baked into the average - see MidiClockTempoDetector::reset().
    tempoClockDetector.reset();
}

void MainComponent::setMasterVolume(float linearGain)
{
    masterVolume = linearGain;
    masterChainComponent.setVolume(linearGain);
}

void MainComponent::setChannelArmed(int channelIndex, bool armed)
{
    // Deliberately does not call notifyDirty() itself - SessionIO::loadSession
    // calls this directly to restore saved arm state, and must not leave the
    // just-loaded session marked dirty (same convention as setGlobalTempo/
    // setTempoSyncEnabled). The UI-driven callback site marks dirty instead.
    recordingManager.setChannelArmed(channelIndex, armed, currentTempo);
    if (channelIndex >= 0 && channelIndex < (int) channelComponents.size())
        channelComponents[(size_t) channelIndex]->setArmed(armed);
}

void MainComponent::loadMidiTakeForChannel(int index, const juce::File& file)
{
    if (index < 0 || index >= (int) midiTakePlayers.size())
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    double sampleRate = device != nullptr ? device->getCurrentSampleRate() : currentSampleRate;
    midiTakePlayers[(size_t) index]->loadTake(file, sampleRate, currentTempo);
}

void MainComponent::unloadMidiTakeForChannel(int index)
{
    if (index < 0 || index >= (int) midiTakePlayers.size())
        return;
    midiTakePlayers[(size_t) index]->unload();
}

void MainComponent::resolveMidiTakeSelectionForChannel(int index)
{
    if (index < 0 || index >= (int) channelProcessors.size())
        return;

    auto identifier = channelProcessors[(size_t) index]->getMidiDeviceIdentifier();
    if (RecordingManager::isTakeIdentifier(identifier))
        loadMidiTakeForChannel(index, recordingManager.decodeTakeIdentifier(identifier));
    else
        unloadMidiTakeForChannel(index);
}

void MainComponent::loadAudioTakeForChannel(int index, const juce::File& file)
{
    if (index < 0 || index >= (int) audioTakePlayers.size())
        return;
    audioTakePlayers[(size_t) index]->loadTake(file);
}

void MainComponent::unloadAudioTakeForChannel(int index)
{
    if (index < 0 || index >= (int) audioTakePlayers.size())
        return;
    audioTakePlayers[(size_t) index]->unload();
}

void MainComponent::resolveAudioTakeSelectionForChannel(int index)
{
    if (index < 0 || index >= (int) channelProcessors.size())
        return;

    auto identifier = channelProcessors[(size_t) index]->getAudioTakeIdentifier();
    if (RecordingManager::isTakeIdentifier(identifier))
        loadAudioTakeForChannel(index, recordingManager.decodeTakeIdentifier(identifier));
    else
        unloadAudioTakeForChannel(index);
}

juce::String MainComponent::importAudioToChannel(int channelIndex, const juce::File& sourceFile)
{
    if (channelIndex < 0 || channelIndex >= (int) channelComponents.size())
        return "Invalid target channel.";

    juce::File importedFile;
    auto error = recordingManager.importAudioTake(channelIndex, sourceFile, currentSampleRate, importedFile);
    if (error.isNotEmpty())
        return error;

    // selectAudioTake() fires onAudioTakeSelected, already wired in
    // addChannel() to loadAudioTakeForChannel() - same path a manual
    // selection in the Audio Input Selector already takes, including its
    // auto-bypass convenience for a loaded slot-0 instrument.
    channelComponents[(size_t) channelIndex]->selectAudioTake(importedFile);
    return {};
}

void MainComponent::setMasterArmed(bool armed)
{
    // See setChannelArmed() above for why this doesn't self-mark dirty.
    recordingManager.setMasterArmed(armed);
    masterChainComponent.setArmed(armed);
}

juce::String MainComponent::toggleRecording()
{
    if (recordingManager.isRecording())
    {
        recordingManager.stopRecording();
        refreshRecordingUI();
        // New MIDI/Audio Take files may now exist (RecordingManager
        // finalizes them synchronously inside stopRecording(), before
        // returning here) - let every channel's Input Selectors pick them up.
        for (int i = 0; i < (int) channelComponents.size(); ++i)
        {
            refreshChannelTakeList(i);
            refreshChannelAudioTakeList(i);
        }
        return {};
    }

    // Both writers are always requested at 2 channels - the app's audio
    // device selector (see SettingsComponent) is hardcoded to exactly 2
    // output channels, so channelScratch/masterBuffer are always stereo.
    auto* device = deviceManager.getCurrentAudioDevice();
    double sampleRate = device != nullptr ? device->getCurrentSampleRate() : currentSampleRate;

    auto error = recordingManager.startRecording(sampleRate, 2, 2, currentTempo);
    refreshRecordingUI();
    return error;
}

void MainComponent::refreshRecordingUI()
{
    bool active = recordingManager.isRecording();
    for (auto& c : channelComponents)
        c->setRecordingActive(active);

    // Whatever armed it - the Record Ready button, or a direct MIDI CC102
    // toggle bypassing it entirely (see timerCallback()) - it's live now,
    // so any pending arm-and-wait-for-play request is moot. Centralising
    // this here (the one choke point every recording start/stop already
    // funnels through) means it stays correct regardless of which path
    // triggered it, current or future.
    if (active)
        recordArmedForNextPlay = false;

    globalSection.setRecordState(active ? GlobalSectionComponent::RecordState::recording
                                        : GlobalSectionComponent::RecordState::idle);
}

void MainComponent::toggleTransportPlaying()
{
    bool wasPlaying = sessionTransport.isPlaying();
    if (wasPlaying)
        sessionTransport.pause();
    else
        sessionTransport.play();

    globalSection.setTransportPlaying(sessionTransport.isPlaying());

    // Record Ready was waiting for exactly this play edge - if it was
    // already playing when armed, toggleRecordArm() already started
    // recording directly and recordArmedForNextPlay would already be
    // false, so this can't double-fire.
    if (! wasPlaying && sessionTransport.isPlaying() && recordArmedForNextPlay)
        startArmedRecording();
}

void MainComponent::toggleRecordArm()
{
    if (recordingManager.isRecording())
    {
        // Actively recording - REC stops it. Playback (if any) is
        // deliberately left untouched, same as the direct MIDI CC102 path
        // has always done.
        auto error = toggleRecording();
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Recording", error);
        return;
    }

    if (recordArmedForNextPlay)
    {
        // Armed but nothing has actually started yet - cancel back to idle.
        recordArmedForNextPlay = false;
        globalSection.setRecordState(GlobalSectionComponent::RecordState::idle);
        return;
    }

    recordArmedForNextPlay = true;
    if (sessionTransport.isPlaying())
        startArmedRecording(); // already rolling - no future play edge to wait for, start now
    else
        globalSection.setRecordState(GlobalSectionComponent::RecordState::armed);
}

void MainComponent::startArmedRecording()
{
    recordArmedForNextPlay = false;

    // Lazy prompt: ask for a recordings folder right here the first time
    // it's needed, rather than requiring a trip to Settings first - same
    // as the Record button used to do directly before Record Ready existed.
    if (recordingManager.getRecordingsFolder() == juce::File())
    {
        recordingFolderChooser = std::make_unique<juce::FileChooser>(
            "Choose a folder for recordings",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory));

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        recordingFolderChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            auto result = chooser.getResult();
            if (result == juce::File())
            {
                // Cancelled - nothing to record into. refreshRecordingUI()
                // never runs in this branch (toggleRecording() is never
                // reached), so the button would otherwise stay stuck on
                // "armed" - revert it explicitly here instead.
                globalSection.setRecordState(GlobalSectionComponent::RecordState::idle);
                return;
            }

            setRecordingsFolder(result);
            notifyDirty();

            auto error = toggleRecording();
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Recording", error);
        });
        return;
    }

    auto error = toggleRecording();
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Recording", error);
}

void MainComponent::showPluginBrowser(int channelIndex, int slotIndex, bool isReplace)
{
    if (!pluginsReady)
        return;

    // All channel slots (0 and insert slots 1-5) accept instrument plugins;
    // loading one into an insert slot is the user's choice and may produce
    // no audio if the plugin doesn't pass its input through.
    bool allowInstruments = true;

    PluginBrowserComponent::showAsCallOut(
        pluginManager,
        [this, channelIndex, slotIndex, isReplace](const juce::PluginDescription& desc)
        {
            auto& proc = *channelProcessors[(size_t) channelIndex];

            auto* device      = deviceManager.getCurrentAudioDevice();
            double sampleRate = device ? device->getCurrentSampleRate()        : 44100.0;
            int    blockSize  = device ? device->getCurrentBufferSizeSamples() : 512;

            if (isReplace)
                proc.unloadPlugin(slotIndex);

            bool loaded = proc.loadPlugin(
                slotIndex, desc, pluginManager.getFormatManager(), sampleRate, blockSize);

            if (isReplace || loaded)
                notifyDirty();

            if (loaded)
            {
                pluginManager.noteRecentlyUsed(desc.createIdentifierString());

                // Wait for CallOutBox to fully dismiss before opening editor
                juce::Timer::callAfterDelay(100, [this, channelIndex, slotIndex]
                {
                    channelComponents[(size_t) channelIndex]->refresh();
                    channelProcessors[(size_t) channelIndex]->showEditor(slotIndex);
                });
            }
            else if (isReplace)
            {
                // The old plugin is already unloaded by this point (see
                // above) - warn rather than leaving the slot silently empty
                // with no indication the replace failed.
                channelComponents[(size_t) channelIndex]->refresh();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Replace Failed",
                    "\"" + desc.name + "\" could not be loaded. The previous plugin in this slot has "
                    "been removed and the slot is now empty.");
            }
        },
        *channelComponents[(size_t) channelIndex],
        allowInstruments
    );
}

void MainComponent::showMasterChainPluginBrowser(int slotIndex, bool isReplace)
{
    if (!pluginsReady)
        return;

    // Master chain slots also accept instrument plugins now - the user's
    // choice, even though the master chain has no MIDI routing to feed one.
    PluginBrowserComponent::showAsCallOut(
        pluginManager,
        [this, slotIndex, isReplace](const juce::PluginDescription& desc)
        {
            auto* device      = deviceManager.getCurrentAudioDevice();
            double sampleRate = device ? device->getCurrentSampleRate()        : 44100.0;
            int    blockSize  = device ? device->getCurrentBufferSizeSamples() : 512;

            if (isReplace)
                masterChainProcessor.unloadPlugin(slotIndex);

            bool loaded = masterChainProcessor.loadPlugin(
                slotIndex, desc, pluginManager.getFormatManager(), sampleRate, blockSize);

            if (isReplace || loaded)
                notifyDirty();

            if (loaded)
            {
                pluginManager.noteRecentlyUsed(desc.createIdentifierString());

                juce::Timer::callAfterDelay(100, [this, slotIndex]
                {
                    masterChainComponent.refresh();
                    masterChainProcessor.showEditor(slotIndex);
                });
            }
            else if (isReplace)
            {
                masterChainComponent.refresh();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Replace Failed",
                    "\"" + desc.name + "\" could not be loaded. The previous plugin in this slot has "
                    "been removed and the slot is now empty.");
            }
        },
        masterChainComponent,
        true
    );
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput* source,
                                               const juce::MidiMessage& msg)
{
    // MIDI clock/Start/Continue bytes on the currently-selected sync device
    // still get buffered above for channel routing like any other message -
    // this is an extra tap, not a replacement. Gated on tempoSyncEnabled too
    // so bytes from a device the user picked before but has since disabled
    // sync for can't silently keep feeding the detector.
    if (tempoSyncEnabled && source->getIdentifier() == tempoSyncDeviceIdentifier)
        tempoClockDetector.pushMessage(msg);

    const juce::ScopedLock sl(midiLock);
    pendingMidiByDevice[source->getIdentifier()].addEvent(msg, 0);
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = device->getCurrentBufferSizeSamples();
    channelScratch.setSize(2, currentBlockSize);

    for (auto& channel : channelProcessors)
        channel->prepareToPlay(currentSampleRate, currentBlockSize);
    masterChainProcessor.prepareToPlay(currentSampleRate, currentBlockSize);
}

void MainComponent::audioDeviceStopped()
{
    for (auto& channel : channelProcessors)
        channel->prepareToPlay(44100.0, 512);
    masterChainProcessor.prepareToPlay(44100.0, 512);
}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> masterBuffer(outputChannelData, numOutputChannels, numSamples);
    masterBuffer.clear();

    std::map<juce::String, juce::MidiBuffer> midiSnapshot;
    {
        const juce::ScopedLock sl(midiLock);
        midiSnapshot.swap(pendingMidiByDevice);
    }

    // Master-level MIDI commands: arm = CC104, record start/stop = CC102,
    // reserved on MIDI channel 16 of the Kadabra port specifically (not any
    // device's channel 16 - master has no per-channel device selector of
    // its own to scope this by, so it's tied to the same port the
    // channel-auto-assign scheme already treats as "the" Kadabra input).
    // Only reports the request here (audio thread) - timerCallback() is
    // what actually calls setMasterArmed()/toggleRecording(), since those
    // touch juce::Component state that must stay on the message thread.
    auto kadabraId = getKadabraDeviceIdentifier();
    if (kadabraId.isNotEmpty())
    {
        auto it = midiSnapshot.find(kadabraId);
        if (it != midiSnapshot.end())
        {
            for (auto meta : it->second)
            {
                auto msg = meta.getMessage();
                if (! msg.isController() || msg.getChannel() != 16)
                    continue;

                int cc = msg.getControllerNumber();
                if (cc == 104)
                {
                    pendingMasterArmValueFromMidi.store(msg.getControllerValue() >= 64, std::memory_order_relaxed);
                    masterArmChangedByMidi.store(true, std::memory_order_relaxed);
                }
                else if (cc == 102)
                {
                    pendingRecordStateValueFromMidi.store(msg.getControllerValue() >= 64, std::memory_order_relaxed);
                    recordStateChangedByMidi.store(true, std::memory_order_relaxed);
                }
            }
        }
    }

    bool anySolo = false;
    for (auto& channel : channelProcessors)
    {
        if (channel->isSolo())
        {
            anySolo = true;
            break;
        }
    }

    // Panic (see triggerPanic()): consumed once here, then injected into
    // every channel's MIDI buffer below, regardless of that channel's own
    // device/MIDI-channel routing - a stuck note can be sitting in any
    // loaded instrument, not just the one on whatever device/channel is
    // "selected" right now.
    bool doPanic = panicRequested.exchange(false, std::memory_order_relaxed);

    // MIDI Take playback (Increment B): advanced once per callback, not per
    // channel, so every channel's MidiTakePlayer renders against the same
    // block-start position this callback - see SessionTransport's header.
    auto transportBlockStart = sessionTransport.advanceAndGetBlockStartPosition(numSamples);
    bool transportIsPlaying  = sessionTransport.isPlaying();

    for (int channelIndex = 0; channelIndex < (int) channelProcessors.size(); ++channelIndex)
    {
        auto& channel = channelProcessors[(size_t) channelIndex];
        channelScratch.clear();

        // Audio input routing (Increment 3 item 8, extended by Increment C
        // for Audio Take playback): feed either a recorded Audio Take or
        // the selected hardware input channel into both scratch channels
        // before the plugin chain runs, alongside MIDI - a plugin that
        // doesn't care about audio input just sees silence (same as before
        // this feature), and slot 0's input bus is now always enabled to
        // receive it (see ChannelProcessor::loadPlugin). An Audio Take is
        // fully reprocessed by this channel's insert chain from this point
        // on - not a bypass path, see spec section 3.
        auto audioTakeIdentifier = channel->getAudioTakeIdentifier();
        if (RecordingManager::isTakeIdentifier(audioTakeIdentifier))
        {
            audioTakePlayers[(size_t) channelIndex]->renderBlock(transportBlockStart, numSamples, transportIsPlaying,
                                                                  channelScratch.getArrayOfWritePointers(),
                                                                  channelScratch.getNumChannels());
        }
        else
        {
            int inputIndex = channel->getAudioInputChannelIndex();
            if (inputIndex >= 0 && inputIndex < numInputChannels && inputChannelData[inputIndex] != nullptr)
                for (int ch = 0; ch < channelScratch.getNumChannels(); ++ch)
                    channelScratch.copyFrom(ch, 0, inputChannelData[inputIndex], numSamples);
        }

        // Each channel gets its own *copy* of its device's MIDI buffer -
        // JUCE plugins can mutate the buffer they're given, and per spec
        // 7.1 multiple channels may share one device on different channel
        // numbers, so they must not all reference the same instance.
        juce::MidiBuffer channelMidi;
        auto deviceId = channel->getMidiDeviceIdentifier();
        if (RecordingManager::isTakeIdentifier(deviceId))
        {
            midiTakePlayers[(size_t) channelIndex]->renderBlock(transportBlockStart, numSamples,
                                                                 transportIsPlaying, channelMidi);
        }
        else if (deviceId.isNotEmpty())
        {
            auto it = midiSnapshot.find(deviceId);
            if (it != midiSnapshot.end())
                channelMidi = it->second;
        }

        // MIDI Take capture (see docs/kplayer-take-recording-playback-spec.md):
        // must happen before processBlock() below, which hands this same
        // buffer to whatever plugin is loaded - plugins are free to mutate
        // the buffer they're given, so this is the last point at which
        // channelMidi is still guaranteed to be the raw, unprocessed input.
        recordingManager.writeChannelMidiBlock(channelIndex, channelMidi);

        // Injected after the MIDI Take recording tap above, deliberately -
        // a panic is a host-level emergency action, not part of the
        // performance, and shouldn't be captured into a take. All 16
        // channels covers this channel regardless of which one it's
        // actually routed to.
        if (doPanic)
            for (int midiCh = 1; midiCh <= 16; ++midiCh)
            {
                channelMidi.addEvent(juce::MidiMessage::allNotesOff(midiCh), 0);
                channelMidi.addEvent(juce::MidiMessage::allSoundOff(midiCh), 0);
            }

        channel->processBlock(channelScratch, channelMidi);

        // Tapped post-plugin-chain/gain/pan, pre-mute (see RecordingManager's
        // header comment) - a recorded take shouldn't silently gap because a
        // channel was muted for monitoring purposes only.
        recordingManager.writeChannelBlock(channelIndex, channelScratch);

        bool audible = ! channel->isMuted() && (! anySolo || channel->isSolo());
        if (audible)
            for (int ch = 0; ch < numOutputChannels; ++ch)
                masterBuffer.addFrom(ch, 0, channelScratch, ch, 0, numSamples);
    }

    masterChainProcessor.processBlock(masterBuffer);
    masterBuffer.applyGain(masterVolume);

    // Post-volume: what the master fader actually delivers, matching "the
    // master will deliver the full mix" from the feature's original ask.
    recordingManager.writeMasterBlock(masterBuffer);
    recordingManager.noteBlockProcessed(numSamples);

    float peakL = masterBuffer.getMagnitude(0, 0, numSamples);
    float peakR = numOutputChannels > 1
                    ? masterBuffer.getMagnitude(1, 0, numSamples)
                    : peakL;
    masterPeakLeft.store(peakL, std::memory_order_relaxed);
    masterPeakRight.store(peakR, std::memory_order_relaxed);
    if (peakL >= 1.0f)
        masterClipFlagLeft.store(true, std::memory_order_relaxed);
    if (peakR >= 1.0f)
        masterClipFlagRight.store(true, std::memory_order_relaxed);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    // Rightmost: Global section (branding, channel count, Settings, I/O
    // collapse, tempo/sync, transport, Record Ready, Panic) - see
    // GlobalSectionComponent's own resized() for its internal layout.
    auto globalArea = area.removeFromRight(150);
    globalSection.setBounds(globalArea);
    area.removeFromRight(12);

    // Master strip: inserts, gain fader/meters, ARM only - between the
    // channel rack and the global section.
    auto masterChainArea = area.removeFromRight(110);
    masterChainComponent.setBounds(masterChainArea);
    area.removeFromRight(20);

    channelViewport.setBounds(area);

    const int channelWidth = 80;
    channelRackContent.setSize(channelWidth * (int) channelComponents.size(), area.getHeight());
    for (int i = 0; i < (int) channelComponents.size(); ++i)
        channelComponents[(size_t) i]->setBounds(i * channelWidth, 0, channelWidth, area.getHeight());

    if (loadingOverlay != nullptr)
        loadingOverlay->setBounds(getLocalBounds());
}
