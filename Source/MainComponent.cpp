#include "MainComponent.h"
#include "PluginBrowserComponent.h"

MainComponent::MainComponent(juce::AudioDeviceManager& dm, PluginManager& pm)
    : deviceManager(dm), pluginManager(pm)
{
    channelComponent = std::make_unique<ChannelComponent>(channelProcessor);
    channelComponent->onLoadPlugin    = [this] { showPluginBrowser(false); };
    channelComponent->onReplacePlugin = [this] { showPluginBrowser(true);  };
    addAndMakeVisible(channelComponent.get());

    // ChannelProcessor needs these so it can pull itself off the audio
    // thread while a plugin is being loaded/unloaded.
    channelProcessor.setAudioDeviceManager(&deviceManager);
    channelProcessor.setAudioCallback(this);

    deviceManager.addAudioCallback(this);

    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto& input : midiInputs)
    {
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }

    loadingOverlay = std::make_unique<LoadingOverlayComponent>();
    addAndMakeVisible(loadingOverlay.get());

    setSize(900, 600);
}

MainComponent::~MainComponent()
{
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

void MainComponent::showPluginBrowser(bool isReplace)
{
    if (!pluginsReady)
        return;

    PluginBrowserComponent::showAsCallOut(
        pluginManager.getPluginList(),
        [this, isReplace](const juce::PluginDescription& desc)
        {
            auto* device      = deviceManager.getCurrentAudioDevice();
            double sampleRate = device ? device->getCurrentSampleRate()        : 44100.0;
            int    blockSize  = device ? device->getCurrentBufferSizeSamples() : 512;

            if (isReplace)
                channelProcessor.unloadPlugin();

            bool loaded = channelProcessor.loadPlugin(
                desc, pluginManager.getFormatManager(), sampleRate, blockSize);

            if (loaded)
            {
                // Wait for CallOutBox to fully dismiss before opening editor
                juce::Timer::callAfterDelay(100, [this]
                {
                    channelComponent->refresh();
                    channelProcessor.showEditor();
                });
            }
        },
        *channelComponent
    );
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*,
                                               const juce::MidiMessage& msg)
{
    const juce::ScopedLock sl(midiLock);
    pendingMidi.addEvent(msg, 0);
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = device->getCurrentBufferSizeSamples();
    channelProcessor.prepareToPlay(currentSampleRate, currentBlockSize);
}

void MainComponent::audioDeviceStopped()
{
    channelProcessor.prepareToPlay(44100.0, 512);
}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float* const*, int,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
    buffer.clear();

    juce::MidiBuffer midi;
    {
        const juce::ScopedLock sl(midiLock);
        midi = pendingMidi;
        pendingMidi.clear();
    }

    channelProcessor.processBlock(buffer, midi);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    channelComponent->setBounds(area.removeFromLeft(160).withHeight(500));

    if (loadingOverlay != nullptr)
        loadingOverlay->setBounds(getLocalBounds());
}
