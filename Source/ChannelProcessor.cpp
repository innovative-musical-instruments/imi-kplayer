#include "ChannelProcessor.h"
#include "PluginEditorWindow.h"

ChannelProcessor::ChannelProcessor() {}
ChannelProcessor::~ChannelProcessor()
{
    for (int i = 0; i < totalSlotCount; ++i)
        unloadPlugin(i);
}

bool ChannelProcessor::loadPlugin(int slotIndex,
                                   const juce::PluginDescription& desc,
                                   juce::AudioPluginFormatManager& formatManager,
                                   double sampleRate,
                                   int blockSize,
                                   const juce::MemoryBlock* initialState)
{
    jassert(slotIndex >= 0 && slotIndex < totalSlotCount);

    auto& slot = slots[(size_t) slotIndex];

    juce::String errorMessage;

    // Tell the audio thread not to touch this slot, then sleep long enough
    // to guarantee at least one audio callback has observed that before we
    // mutate `plugin` below - deliberately not detaching the shared device
    // callback here, since that would silence every channel, not just this
    // slot (see the comment on `Slot::ready` in the header).
    slot.ready.store(false, std::memory_order_release);
    juce::Thread::sleep(50);

    auto newPlugin = formatManager.createPluginInstance(
        desc, sampleRate, blockSize, errorMessage);

    if (newPlugin == nullptr)
    {
        juce::Logger::outputDebugString("Failed to load plugin: " + errorMessage);
        return false;
    }

    // Force a plain bus layout *before* prepareToPlay(). prepareToPlay()
    // sizes the AU/VST3 wrapper's internal render buffers from whatever
    // bus layout is active at that moment; calling setPlayConfigDetails()
    // afterwards (the old approach) doesn't touch bus layout at all for
    // bus-based formats. Instruments like HISE's K-Sampler can default to
    // extra output busses (mic/RR routing), which left the AU wrapper
    // rendering into more channels than our 2-channel buffer actually has
    // room for - an out-of-bounds write that manifested as SIGBUS deep in
    // AUBase::DoRender.
    newPlugin->disableNonMainBuses();

    // Start from the plugin's own (now mostly-disabled) layout so the bus
    // counts already match what it actually has - an instrument may report
    // zero input busses, and building a BusesLayout with an assumed
    // 1-in/1-out shape mismatches that and crashes inside syncBusLayouts()
    // when it walks a bus that doesn't exist.
    auto layout = newPlugin->getBusesLayout();

    // Every slot that actually has an input bus gets it enabled as stereo,
    // instruments included - Increment 3's per-channel audio input selector
    // can feed a live hardware signal into slot 0 alongside MIDI (e.g.
    // vocoding through an instrument like Surge XT), and a plugin that
    // doesn't care about audio input just sees silence, same as before.
    // Plugins with zero input busses (e.g. K-Sampler/HISE) are unaffected
    // either way - there's nothing here to enable.
    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();

    if (layout.outputBuses.size() > 0)
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();

    if (! newPlugin->setBusesLayout(layout))
    {
        juce::Logger::outputDebugString(
            "Plugin does not support the required bus layout: " + desc.name);
        return false;
    }

    newPlugin->setPlayHead(&playHead);
    newPlugin->prepareToPlay(sampleRate, blockSize);

    if (initialState != nullptr && initialState->getSize() > 0)
        newPlugin->setStateInformation(initialState->getData(), (int) initialState->getSize());

    slot.plugin = std::move(newPlugin);
    slot.plugin->addListener(this);
    slot.bypassed = false;
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

    // Grace period for HISE's async sample-mapping/streaming threads to
    // settle before we let the audio thread anywhere near the plugin.
    juce::Thread::sleep(1000);

    slot.ready.store(true, std::memory_order_release);

    return true;
}

void ChannelProcessor::unloadPlugin(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < totalSlotCount);
    auto& slot = slots[(size_t) slotIndex];

    hideEditor(slotIndex);

    if (slot.plugin == nullptr)
        return;

    // See loadPlugin() - sleep is the drain margin, not a device-callback
    // detach, so other channels keep processing uninterrupted.
    slot.ready.store(false, std::memory_order_release);
    juce::Thread::sleep(50);

    slot.plugin->removeListener(this);
    slot.plugin->setPlayHead(nullptr);
    slot.plugin->releaseResources();

    // Wait for HISE background threads to finish before the destructor runs
    juce::Thread::sleep(500);

    slot.plugin.reset();
}

bool ChannelProcessor::hasPlugin(int slotIndex) const
{
    return slots[(size_t) slotIndex].plugin != nullptr;
}

bool ChannelProcessor::isEditorVisible(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    return slot.editorWindow != nullptr && slot.editorWindow->isVisible();
}

juce::String ChannelProcessor::getPluginName(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        return slot.plugin->getName();
    return "No Plugin";
}

juce::PluginDescription ChannelProcessor::getPluginDescription(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        return slot.plugin->getPluginDescription();
    return {};
}

juce::MemoryBlock ChannelProcessor::getPluginState(int slotIndex) const
{
    juce::MemoryBlock block;
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        slot.plugin->getStateInformation(block);
    return block;
}

void ChannelProcessor::setBypassed(int slotIndex, bool shouldBeBypassed)
{
    auto& slot = slots[(size_t) slotIndex];
    slot.bypassed = shouldBeBypassed;

    if (auto* window = dynamic_cast<PluginEditorWindow*>(slot.editorWindow.get()))
        window->setBypassedIndicator(shouldBeBypassed);

    if (onBypassChanged) onBypassChanged(slotIndex);
}

void ChannelProcessor::syncBypassIndicatorsFromMidi()
{
    for (int i = 0; i < totalSlotCount; ++i)
    {
        auto& slot = slots[(size_t) i];
        if (auto* window = dynamic_cast<PluginEditorWindow*>(slot.editorWindow.get()))
            window->setBypassedIndicator(slot.bypassed);
    }
}

bool ChannelProcessor::isBypassed(int slotIndex) const
{
    return slots[(size_t) slotIndex].bypassed;
}

void ChannelProcessor::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;
    for (auto& slot : slots)
        if (slot.plugin != nullptr)
            slot.plugin->prepareToPlay(sampleRate, blockSize);
}

void ChannelProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    // Channel volume via MIDI CC7, pan via MIDI CC10 (handled independently
    // of CC7 - gain-only and pan-only respectively), per-slot bypass/
    // activate via MIDI CC84-89 (CC 84+slotIndex, value <64 = bypass, >=64 =
    // active), and record arm/disarm via CC103 (<64 = disarm, >=64 = arm):
    // scanned unconditionally, even with nothing loaded, since all of these
    // should be controllable regardless of what else the channel is doing.
    // Same channel-matching rule as the plugin-forwarding filter below -
    // "All" (midiChannel == 0) matches any channel, otherwise only messages
    // on midiChannel itself. `midi` here is already this channel's own
    // per-device buffer (MainComponent only hands it messages from the
    // assigned MIDI device), so no separate device check is needed.
    for (auto meta : midi)
    {
        auto msg = meta.getMessage();
        if (! msg.isController() || ! (midiChannel == 0 || msg.getChannel() == midiChannel))
            continue;

        int cc = msg.getControllerNumber();
        if (cc == 7)
        {
            double dB = normalisedToGainDb(msg.getControllerValue() / 127.0);
            setGain(dB <= -96.0 ? 0.0f : (float) juce::Decibels::decibelsToGain(dB));
            gainChangedByMidi.store(true, std::memory_order_relaxed);
        }
        else if (cc == 10)
        {
            // Standard MIDI pan curve, split at the CC's own centre point:
            // 0 = full left, 63 and 64 both = centre (the two middle values
            // of an even 128-value range), 127 = full right. Expressed
            // directly in ChannelProcessor's -1..1 pan representation - the
            // UI's -50..+50 slider scale (see ChannelComponent) is just that
            // range times 50, converted at the UI boundary only.
            int value = msg.getControllerValue();
            float p = value <= 63 ? (-1.0f + (float) value / 63.0f)
                                   : ((float) (value - 64) / 63.0f);
            setPan(p);
            panChangedByMidi.store(true, std::memory_order_relaxed);
        }
        else if (cc >= 84 && cc < 84 + totalSlotCount)
        {
            // Written directly here rather than through setBypassed() - that
            // touches juce::Component state (an open editor window's bypass
            // bar) that must only ever be touched from the message thread.
            // syncBypassIndicatorsFromMidi() finishes the job once
            // bypassChangedByMidi is polled there.
            slots[(size_t) (cc - 84)].bypassed = msg.getControllerValue() < 64;
            bypassChangedByMidi.store(true, std::memory_order_relaxed);
        }
        else if (cc == 103)
        {
            // Record arm/disarm - ChannelProcessor can't act on this itself
            // (no reference to RecordingManager or its own channel index),
            // just reports the request; see consumeArmChangedByMidi().
            pendingArmValueFromMidi.store(msg.getControllerValue() >= 64, std::memory_order_relaxed);
            armChangedByMidi.store(true, std::memory_order_relaxed);
        }
    }

    auto& first = slots[(size_t) slot0Index];

    if (! first.ready.load(std::memory_order_acquire) || first.plugin == nullptr || first.bypassed)
    {
        // Normally nothing loaded in slot 0 means silence - there's no
        // instrument to generate audio. But if this channel has a live
        // audio input or an Audio Take (Increment C) assigned, let it pass
        // straight through to the insert chain/fader instead of wiping it
        // here, covering the "live vocals/mixing, no instrument loaded"
        // case from the audio-input spec (Increment 3 item 8) - vocoding
        // through a loaded instrument already works via the branches below
        // since they never clear.
        if (audioInputChannelIndex < 0 && ! hasAudioTakeSelected)
            buffer.clear();
    }
    else if (midiChannel > 0)
    {
        juce::MidiBuffer filtered;
        for (auto meta : midi)
        {
            auto msg = meta.getMessage();
            if (msg.getChannel() == midiChannel || msg.getChannel() == 0)
                filtered.addEvent(msg, meta.samplePosition);
        }
        first.plugin->processBlock(buffer, filtered);
    }
    else
    {
        first.plugin->processBlock(buffer, midi);
    }

    juce::MidiBuffer noMidi;
    for (int i = 1; i < totalSlotCount; ++i)
    {
        auto& slot = slots[(size_t) i];
        if (slot.ready.load(std::memory_order_acquire) && slot.plugin != nullptr && ! slot.bypassed)
            slot.plugin->processBlock(buffer, noMidi);
    }

    applyGainAndPan(buffer);
    updateMetering(buffer);
}

void ChannelProcessor::updateMetering(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 1)
        return;

    float peakL = buffer.getMagnitude(0, 0, buffer.getNumSamples());
    float peakR = buffer.getNumChannels() > 1
                    ? buffer.getMagnitude(1, 0, buffer.getNumSamples())
                    : peakL;

    peakLevelLeft.store(peakL, std::memory_order_relaxed);
    peakLevelRight.store(peakR, std::memory_order_relaxed);

    // 0dBFS overshoot - a one-shot flag the UI timer consumes and latches;
    // never cleared here, only ever set, so it can't miss a clip that
    // happens between UI reads.
    if (peakL >= 1.0f || peakR >= 1.0f)
        clipFlag.store(true, std::memory_order_relaxed);
}

void ChannelProcessor::applyGainAndPan(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 2)
        return;

    float leftGain  = gain * std::cos((pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f);
    float rightGain = gain * std::sin((pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f);

    buffer.applyGain(0, 0, buffer.getNumSamples(), leftGain);
    buffer.applyGain(1, 0, buffer.getNumSamples(), rightGain);
}

void ChannelProcessor::showEditor(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < totalSlotCount);
    auto& slot = slots[(size_t) slotIndex];

    if (slot.plugin == nullptr) return;

    hideEditor(slotIndex);

    if (slot.plugin->hasEditor())
    {
        auto* ed = slot.plugin->createEditorIfNeeded();
        if (ed == nullptr) return;

        auto title = slot.plugin->getName()
                        + " channel-" + juce::String(channelNumber)
                        + " slot-"    + juce::String(slotIndex);
        auto* window = new PluginEditorWindow(title,
                                              [this, slotIndex] { hideEditor(slotIndex); });
        slot.editorWindow.reset(window);

        window->setPluginEditor(std::unique_ptr<juce::AudioProcessorEditor>(ed),
                                slot.bypassed,
                                [this, slotIndex](bool b) { setBypassed(slotIndex, b); });
        window->setVisible(true);
        window->setAlwaysOnTop(true);
    }
}

void ChannelProcessor::hideEditor(int slotIndex)
{
    slots[(size_t) slotIndex].editorWindow.reset();
}

void ChannelProcessor::setMidiDeviceIdentifier(const juce::String& deviceId)
{
    const juce::ScopedLock sl(midiDeviceLock);
    midiDeviceIdentifier = deviceId;
}

juce::String ChannelProcessor::getMidiDeviceIdentifier() const
{
    const juce::ScopedLock sl(midiDeviceLock);
    return midiDeviceIdentifier;
}

void ChannelProcessor::setAudioTakeIdentifier(const juce::String& identifier)
{
    const juce::ScopedLock sl(audioTakeLock);
    audioTakeIdentifier = identifier;
    hasAudioTakeSelected = identifier.isNotEmpty();
}

juce::String ChannelProcessor::getAudioTakeIdentifier() const
{
    const juce::ScopedLock sl(audioTakeLock);
    return audioTakeIdentifier;
}
