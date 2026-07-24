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

    channelViewport.setViewedComponent(&channelRackContent, false);
    channelViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(channelViewport);

    masterChainComponent.onLoadPlugin    = [this](int slot) { showMasterChainPluginBrowser(slot, false); };
    masterChainComponent.onReplacePlugin = [this](int slot) { showMasterChainPluginBrowser(slot, true);  };
    masterChainComponent.onDirty         = [this] { notifyDirty(); };
    masterChainComponent.onVolumeChanged = [this](float linearGain) { masterVolume = linearGain; notifyDirty(); };
    masterChainProcessor.onBypassChanged = [this](int) { masterChainComponent.refresh(); };
    masterChainComponent.setLevelMeterSources(&masterPeakLeft, &masterPeakRight,
                                              &masterClipFlagLeft, &masterClipFlagRight);
    addAndMakeVisible(masterChainComponent);
    addAndMakeVisible(brandingStrip);

    collapseInputButton.setButtonText("Hide Channel I/O's");
    collapseInputButton.onClick = [this] { toggleInputSectionCollapsed(); };
    addAndMakeVisible(collapseInputButton);

    // Manual edits only apply while sync is off (TempoSyncComponent itself
    // won't even let the value label be edited while synced, but the guard
    // here is what actually matters). Sync-on/off and device changes are
    // deliberate user actions - unlike the sync-driven tempo ticks in
    // timerCallback(), they do mark the session dirty.
    tempoSyncComponent.onTempoChanged = [this](double bpm)
    {
        if (tempoSyncEnabled) return;
        setGlobalTempo(bpm);
        notifyDirty();
    };
    tempoSyncComponent.onSyncToggled = [this](bool enabled)
    {
        setTempoSyncEnabled(enabled);
        notifyDirty();
    };
    tempoSyncComponent.onSyncDeviceChanged = [this](juce::String identifier)
    {
        setTempoSyncDeviceIdentifier(std::move(identifier));
        notifyDirty();
    };
    addAndMakeVisible(tempoSyncComponent);

    deviceManager.addAudioCallback(this);

    enableAllMidiInputs();

    // Devices connected/disconnected after launch never got a callback
    // registered at all otherwise - re-enable/register on every device-list
    // change rather than only once at startup.
    midiDeviceListConnection = juce::MidiDeviceListConnection::make([this] { enableAllMidiInputs(); });

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

    auto component = std::make_unique<ChannelComponent>(*processor, deviceManager, index + 1);
    component->onLoadPlugin    = [this, index](int slot) { showPluginBrowser(index, slot, false); };
    component->onReplacePlugin = [this, index](int slot) { showPluginBrowser(index, slot, true);  };
    component->onDirty         = [this] { notifyDirty(); };
    processor->onBypassChanged = [this, index](int) { channelComponents[(size_t) index]->refresh(); };
    component->setInputSectionCollapsed(inputSectionCollapsed);
    channelRackContent.addAndMakeVisible(component.get());

    channelProcessors.push_back(std::move(processor));
    channelComponents.push_back(std::move(component));
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
    }

    deviceManager.addAudioCallback(this);
    resized();
}

void MainComponent::setInputSectionCollapsedState(bool collapsed)
{
    inputSectionCollapsed = collapsed;
    collapseInputButton.setButtonText(inputSectionCollapsed ? "Show Channel I/O's" : "Hide Channel I/O's");
    for (auto& c : channelComponents)
        c->setInputSectionCollapsed(inputSectionCollapsed);
}

void MainComponent::toggleInputSectionCollapsed()
{
    setInputSectionCollapsedState(! inputSectionCollapsed);
    notifyDirty();
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
    // addMidiInputDeviceCallback() removes any existing registration for the
    // same (identifier, callback) pair before re-adding, so it's safe to
    // call this repeatedly for devices that were already enabled.
    for (auto& input : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }
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
}

void MainComponent::timerCallback()
{
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
    }
    anyDirty |= masterChainProcessor.consumeParametersDirtyFlag();

    if (anyDirty)
        notifyDirty();

    if (tempoSyncEnabled && tempoSyncDeviceIdentifier.isNotEmpty())
    {
        bool hasSignal = tempoClockDetector.hasSignal();
        tempoSyncComponent.setSyncSignalWarning(! hasSignal);

        // Holds the last-known tempo (rather than reverting to the manual
        // value) once the signal drops, per the earlier design discussion -
        // setGlobalTempo() just isn't called again until pulses resume.
        if (hasSignal && tempoClockDetector.hasLockedTempo())
            setGlobalTempo(tempoClockDetector.getBpm());
    }
}

void MainComponent::setGlobalTempo(double bpm)
{
    currentTempo = bpm;
    for (auto& channel : channelProcessors)
        channel->setTempo(bpm);
    masterChainProcessor.setTempo(bpm);
    tempoSyncComponent.setDisplayedTempo(bpm);
}

void MainComponent::setTempoSyncEnabled(bool enabled)
{
    tempoSyncEnabled = enabled;
    tempoSyncComponent.setSyncEnabled(enabled);
    if (! enabled)
        tempoSyncComponent.setSyncSignalWarning(false);
}

void MainComponent::setTempoSyncDeviceIdentifier(juce::String identifier)
{
    tempoSyncDeviceIdentifier = std::move(identifier);
    tempoSyncComponent.setSyncDeviceIdentifier(tempoSyncDeviceIdentifier);

    // A stray interval spanning the old and new device's pulse streams
    // must not get baked into the average - see MidiClockTempoDetector::reset().
    tempoClockDetector.reset();
}

void MainComponent::setMasterVolume(float linearGain)
{
    masterVolume = linearGain;
    masterChainComponent.setVolume(linearGain);
}

void MainComponent::showPluginBrowser(int channelIndex, int slotIndex, bool isReplace)
{
    if (!pluginsReady)
        return;

    // Slot 0 accepts instruments or audio effects; insert slots 1-5 are
    // audio-effect-only per spec, so instruments are filtered out for them.
    bool allowInstruments = (slotIndex == ChannelProcessor::slot0Index);

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

    // Master chain slots are audio-effect-only, unlike a channel's slot 0.
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
        false
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

    bool anySolo = false;
    for (auto& channel : channelProcessors)
    {
        if (channel->isSolo())
        {
            anySolo = true;
            break;
        }
    }

    for (auto& channel : channelProcessors)
    {
        channelScratch.clear();

        // Audio input routing (Increment 3 item 8): feed the selected
        // hardware input channel into both scratch channels before the
        // plugin chain runs, alongside MIDI - a plugin that doesn't care
        // about audio input just sees silence (same as before this
        // feature), and slot 0's input bus is now always enabled to
        // receive it (see ChannelProcessor::loadPlugin).
        int inputIndex = channel->getAudioInputChannelIndex();
        if (inputIndex >= 0 && inputIndex < numInputChannels && inputChannelData[inputIndex] != nullptr)
            for (int ch = 0; ch < channelScratch.getNumChannels(); ++ch)
                channelScratch.copyFrom(ch, 0, inputChannelData[inputIndex], numSamples);

        // Each channel gets its own *copy* of its device's MIDI buffer -
        // JUCE plugins can mutate the buffer they're given, and per spec
        // 7.1 multiple channels may share one device on different channel
        // numbers, so they must not all reference the same instance.
        juce::MidiBuffer channelMidi;
        auto deviceId = channel->getMidiDeviceIdentifier();
        if (deviceId.isNotEmpty())
        {
            auto it = midiSnapshot.find(deviceId);
            if (it != midiSnapshot.end())
                channelMidi = it->second;
        }

        channel->processBlock(channelScratch, channelMidi);

        bool audible = ! channel->isMuted() && (! anySolo || channel->isSolo());
        if (audible)
            for (int ch = 0; ch < numOutputChannels; ++ch)
                masterBuffer.addFrom(ch, 0, channelScratch, ch, 0, numSamples);
    }

    masterChainProcessor.processBlock(masterBuffer);
    masterBuffer.applyGain(masterVolume);

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

    auto masterChainArea = area.removeFromRight(130);
    brandingStrip.setBounds(masterChainArea.removeFromTop(40));
    masterChainArea.removeFromTop(6);
    collapseInputButton.setBounds(masterChainArea.removeFromTop(22));
    masterChainArea.removeFromTop(6);
    tempoSyncComponent.setBounds(masterChainArea.removeFromTop(TempoSyncComponent::preferredHeight));
    masterChainArea.removeFromTop(6);

    // Align the master chain's own insert slots with the channel strips'
    // (see ChannelComponent::insertSectionStartY - computed for the
    // non-collapsed input-section case only, per that method's own comment).
    // masterOwnOffsetToSlots mirrors MasterChainComponent::resized()'s own
    // row math up to its slot loop (reduced(6) + titleLabel + gap).
    static constexpr int masterOwnOffsetToSlots = 6 + 16 + 6;
    int targetSlotsY = area.getY() + ChannelComponent::insertSectionStartY(false);
    int spacer = targetSlotsY - masterChainArea.getY() - masterOwnOffsetToSlots;
    masterChainComponent.setBounds(masterChainArea);
    masterChainComponent.setTopSpacerHeight(juce::jmax(0, spacer));

    area.removeFromRight(20);

    channelViewport.setBounds(area);

    const int channelWidth = 80;
    channelRackContent.setSize(channelWidth * (int) channelComponents.size(), area.getHeight());
    for (int i = 0; i < (int) channelComponents.size(); ++i)
        channelComponents[(size_t) i]->setBounds(i * channelWidth, 0, channelWidth, area.getHeight());

    if (loadingOverlay != nullptr)
        loadingOverlay->setBounds(getLocalBounds());
}
