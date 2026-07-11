#include "MainComponent.h"
#include "PluginBrowserComponent.h"

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
    masterChainComponent.setLevelMeterSources(&masterPeakLeft, &masterPeakRight,
                                              &masterClipFlagLeft, &masterClipFlagRight);
    addAndMakeVisible(masterChainComponent);
    addAndMakeVisible(brandingStrip);

    collapseInputButton.setButtonText("Hide Channel I/O's");
    collapseInputButton.onClick = [this] { toggleInputSectionCollapsed(); };
    addAndMakeVisible(collapseInputButton);

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

    // getName() is now purely an optional custom name (item 1.1) - the
    // fixed, non-editable "Channel N" number is derived from position and
    // handed to the component separately, not stored on the processor.
    auto component = std::make_unique<ChannelComponent>(*processor, deviceManager, index + 1);
    component->onLoadPlugin    = [this, index](int slot) { showPluginBrowser(index, slot, false); };
    component->onReplacePlugin = [this, index](int slot) { showPluginBrowser(index, slot, true);  };
    component->onDirty         = [this] { notifyDirty(); };
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
    for (auto& processor : channelProcessors)
        anyDirty |= processor->consumeParametersDirtyFlag();
    anyDirty |= masterChainProcessor.consumeParametersDirtyFlag();

    if (anyDirty)
        notifyDirty();
}

void MainComponent::setGlobalTempo(double bpm)
{
    currentTempo = bpm;
    for (auto& channel : channelProcessors)
        channel->setTempo(bpm);
    masterChainProcessor.setTempo(bpm);
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
