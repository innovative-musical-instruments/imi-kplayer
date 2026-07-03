#include "ChannelProcessor.h"

namespace
{
    // Plain juce::DocumentWindow::closeButtonPressed() is a no-op by default,
    // so the titlebar X does nothing unless something overrides it.
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow(const juce::String& name, ChannelProcessor& ownerIn)
            : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
              owner(ownerIn)
        {
        }

        void closeButtonPressed() override { owner.hideEditor(); }

    private:
        ChannelProcessor& owner;
    };
}

ChannelProcessor::ChannelProcessor() {}
ChannelProcessor::~ChannelProcessor() { unloadPlugin(); }

bool ChannelProcessor::loadPlugin(const juce::PluginDescription& desc,
                                   juce::AudioPluginFormatManager& formatManager,
                                   double sampleRate,
                                   int blockSize)
{
    juce::String errorMessage;

    // Pull the audio thread off the callback entirely before touching
    // `plugin` - removeAudioCallback() blocks until any in-flight audio
    // callback returns and guarantees no further calls until it's added
    // back, so processBlock() cannot race the instantiation below.
    pluginReady.store(false, std::memory_order_release);
    if (deviceManager && audioCallback)
        deviceManager->removeAudioCallback(audioCallback);

    auto newPlugin = formatManager.createPluginInstance(
        desc, sampleRate, blockSize, errorMessage);

    if (newPlugin == nullptr)
    {
        juce::Logger::outputDebugString("Failed to load plugin: " + errorMessage);
        if (deviceManager && audioCallback)
            deviceManager->addAudioCallback(audioCallback);
        return false;
    }

    // Force a plain 0-in/stereo-out bus layout *before* prepareToPlay().
    // prepareToPlay() sizes the AU/VST3 wrapper's internal render buffers
    // from whatever bus layout is active at that moment; calling
    // setPlayConfigDetails() afterwards (the old approach) doesn't touch
    // bus layout at all for bus-based formats. Instruments like HISE's
    // K-Sampler can default to extra output busses (mic/RR routing),
    // which left the AU wrapper rendering into more channels than our
    // 2-channel buffer actually has room for - an out-of-bounds write
    // that manifested as SIGBUS deep in AUBase::DoRender.
    newPlugin->disableNonMainBuses();

    // Start from the plugin's own (now mostly-disabled) layout so the
    // bus counts already match what it actually has - an instrument may
    // report zero input busses, and building a BusesLayout with an
    // assumed 1-in/1-out shape mismatches that and crashes inside
    // syncBusLayouts() when it walks a bus that doesn't exist.
    auto layout = newPlugin->getBusesLayout();

    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::disabled();

    if (layout.outputBuses.size() > 0)
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();

    if (! newPlugin->setBusesLayout(layout))
    {
        juce::Logger::outputDebugString(
            "Plugin does not support 0-in/stereo-out bus layout: " + desc.name);
        if (deviceManager && audioCallback)
            deviceManager->addAudioCallback(audioCallback);
        return false;
    }

    newPlugin->setPlayHead(&playHead);
    newPlugin->prepareToPlay(sampleRate, blockSize);

    plugin = std::move(newPlugin);
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

    // Grace period for HISE's async sample-mapping/streaming threads to
    // settle before we let the audio thread anywhere near the plugin.
    // The audio callback is still detached at this point, so this is a
    // safety margin against HISE's own background threads, not against
    // our own audio thread (that's already guaranteed above).
    juce::Thread::sleep(1000);

    pluginReady.store(true, std::memory_order_release);
    if (deviceManager && audioCallback)
        deviceManager->addAudioCallback(audioCallback);

    return true;
}

void ChannelProcessor::unloadPlugin()
{
    hideEditor();

    if (plugin == nullptr)
        return;

    pluginReady.store(false, std::memory_order_release);
    if (deviceManager && audioCallback)
        deviceManager->removeAudioCallback(audioCallback);

    plugin->setPlayHead(nullptr);
    plugin->releaseResources();

    // Wait for HISE background threads to finish before the destructor runs
    juce::Thread::sleep(500);

    plugin.reset();

    if (deviceManager && audioCallback)
        deviceManager->addAudioCallback(audioCallback);
}

juce::String ChannelProcessor::getPluginName() const
{
    if (plugin != nullptr)
        return plugin->getName();
    return "No Plugin";
}

void ChannelProcessor::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;
    if (plugin != nullptr)
        plugin->prepareToPlay(sampleRate, blockSize);
}

void ChannelProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    if (! pluginReady.load(std::memory_order_acquire) || plugin == nullptr)
    {
        buffer.clear();
        return;
    }

    if (midiChannel > 0)
    {
        juce::MidiBuffer filtered;
        for (auto meta : midi)
        {
            auto msg = meta.getMessage();
            if (msg.getChannel() == midiChannel || msg.getChannel() == 0)
                filtered.addEvent(msg, meta.samplePosition);
        }
        plugin->processBlock(buffer, filtered);
    }
    else
    {
        plugin->processBlock(buffer, midi);
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

void ChannelProcessor::showEditor()
{
    if (plugin == nullptr) return;

    hideEditor();

    if (plugin->hasEditor())
    {
        auto* ed = plugin->createEditorIfNeeded();
        if (ed == nullptr) return;

        editorWindow = std::make_unique<PluginEditorWindow>(plugin->getName(), *this);

        editorWindow->setContentOwned(ed, true);
        editorWindow->setResizable(ed->isResizable(), false);
        editorWindow->centreWithSize(ed->getWidth(), ed->getHeight());
        editorWindow->setVisible(true);
        editorWindow->setAlwaysOnTop(true);
    }
}

void ChannelProcessor::hideEditor()
{
    editorWindow.reset();
}
