#include "ChannelProcessor.h"

namespace
{
    // Plain juce::DocumentWindow::closeButtonPressed() is a no-op by default,
    // so the titlebar X does nothing unless something overrides it.
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow(const juce::String& name, std::function<void()> onCloseIn)
            : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
              onClose(std::move(onCloseIn))
        {
        }

        void closeButtonPressed() override { if (onClose) onClose(); }

    private:
        std::function<void()> onClose;
    };
}

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

    // Insert slots are audio-effect-only per spec - enforced here at the
    // engine level so any caller (not just the plugin browser's filtered
    // list) is held to it.
    if (slotIndex != slot0Index && desc.isInstrument)
    {
        juce::Logger::outputDebugString(
            "Refusing to load instrument \"" + desc.name + "\" into insert slot "
            + juce::String(slotIndex) + " - insert slots are audio-effect-only");
        return false;
    }

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
    // when it walks a bus that doesn't exist. Instruments get a disabled
    // input bus (they're driven by MIDI only); insert-slot audio effects
    // need a real stereo input bus so they actually receive the signal.
    auto layout = newPlugin->getBusesLayout();

    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference(0) = desc.isInstrument
            ? juce::AudioChannelSet::disabled()
            : juce::AudioChannelSet::stereo();

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
    slots[(size_t) slotIndex].bypassed = shouldBeBypassed;
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
    auto& first = slots[(size_t) slot0Index];

    if (! first.ready.load(std::memory_order_acquire) || first.plugin == nullptr || first.bypassed)
    {
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

        auto* window = new PluginEditorWindow(slot.plugin->getName(),
                                              [this, slotIndex] { hideEditor(slotIndex); });
        slot.editorWindow.reset(window);

        window->setContentOwned(ed, true);
        window->setResizable(ed->isResizable(), false);
        window->centreWithSize(ed->getWidth(), ed->getHeight());
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
