#include "MasterChainProcessor.h"
#include "PluginEditorWindow.h"

MasterChainProcessor::MasterChainProcessor() {}
MasterChainProcessor::~MasterChainProcessor()
{
    for (int i = 0; i < numSlots; ++i)
        unloadPlugin(i);
}

bool MasterChainProcessor::loadPlugin(int slotIndex,
                                       const juce::PluginDescription& desc,
                                       juce::AudioPluginFormatManager& formatManager,
                                       double sampleRate,
                                       int blockSize,
                                       const juce::MemoryBlock* initialState)
{
    jassert(slotIndex >= 0 && slotIndex < numSlots);

    if (desc.isInstrument)
    {
        juce::Logger::outputDebugString(
            "Refusing to load instrument \"" + desc.name + "\" into master chain slot "
            + juce::String(slotIndex) + " - the master chain is audio-effect-only");
        return false;
    }

    auto& slot = slots[(size_t) slotIndex];

    juce::String errorMessage;

    // See ChannelProcessor::loadPlugin for why this is a drain-margin sleep
    // rather than a device-callback detach.
    slot.ready.store(false, std::memory_order_release);
    juce::Thread::sleep(50);

    auto newPlugin = formatManager.createPluginInstance(
        desc, sampleRate, blockSize, errorMessage);

    if (newPlugin == nullptr)
    {
        juce::Logger::outputDebugString("Failed to load plugin: " + errorMessage);
        return false;
    }

    newPlugin->disableNonMainBuses();

    auto layout = newPlugin->getBusesLayout();
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
    slot.bypassed = false;
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

    juce::Thread::sleep(1000);

    slot.ready.store(true, std::memory_order_release);

    return true;
}

void MasterChainProcessor::unloadPlugin(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < numSlots);
    auto& slot = slots[(size_t) slotIndex];

    hideEditor(slotIndex);

    if (slot.plugin == nullptr)
        return;

    slot.ready.store(false, std::memory_order_release);
    juce::Thread::sleep(50);

    slot.plugin->setPlayHead(nullptr);
    slot.plugin->releaseResources();

    juce::Thread::sleep(500);

    slot.plugin.reset();
}

bool MasterChainProcessor::hasPlugin(int slotIndex) const
{
    return slots[(size_t) slotIndex].plugin != nullptr;
}

bool MasterChainProcessor::isEditorVisible(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    return slot.editorWindow != nullptr && slot.editorWindow->isVisible();
}

juce::String MasterChainProcessor::getPluginName(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        return slot.plugin->getName();
    return "No Plugin";
}

juce::PluginDescription MasterChainProcessor::getPluginDescription(int slotIndex) const
{
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        return slot.plugin->getPluginDescription();
    return {};
}

juce::MemoryBlock MasterChainProcessor::getPluginState(int slotIndex) const
{
    juce::MemoryBlock block;
    auto& slot = slots[(size_t) slotIndex];
    if (slot.plugin != nullptr)
        slot.plugin->getStateInformation(block);
    return block;
}

void MasterChainProcessor::setBypassed(int slotIndex, bool shouldBeBypassed)
{
    slots[(size_t) slotIndex].bypassed = shouldBeBypassed;
}

bool MasterChainProcessor::isBypassed(int slotIndex) const
{
    return slots[(size_t) slotIndex].bypassed;
}

void MasterChainProcessor::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;
    for (auto& slot : slots)
        if (slot.plugin != nullptr)
            slot.plugin->prepareToPlay(sampleRate, blockSize);
}

void MasterChainProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer noMidi;
    for (auto& slot : slots)
        if (slot.ready.load(std::memory_order_acquire) && slot.plugin != nullptr && ! slot.bypassed)
            slot.plugin->processBlock(buffer, noMidi);
}

void MasterChainProcessor::showEditor(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < numSlots);
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

void MasterChainProcessor::hideEditor(int slotIndex)
{
    slots[(size_t) slotIndex].editorWindow.reset();
}
