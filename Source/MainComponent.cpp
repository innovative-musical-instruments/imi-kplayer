#include "MainComponent.h"
#include "PluginBrowserComponent.h"

MainComponent::MainComponent(juce::AudioDeviceManager& dm, PluginManager& pm)
    : deviceManager(dm), pluginManager(pm)
{
    for (int i = 0; i < numChannels; ++i)
    {
        auto processor = std::make_unique<ChannelProcessor>();
        processor->setName("Channel " + juce::String(i + 1));

        auto component = std::make_unique<ChannelComponent>(*processor);
        component->onLoadPlugin    = [this, i](int slot) { showPluginBrowser(i, slot, false); };
        component->onReplacePlugin = [this, i](int slot) { showPluginBrowser(i, slot, true);  };
        channelRackContent.addAndMakeVisible(component.get());

        channelProcessors.push_back(std::move(processor));
        channelComponents.push_back(std::move(component));
    }

    channelViewport.setViewedComponent(&channelRackContent, false);
    channelViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(channelViewport);

    masterVolumeLabel.setText("Master", juce::dontSendNotification);
    masterVolumeLabel.setFont(juce::Font(11.0f));
    masterVolumeLabel.setJustificationType(juce::Justification::centred);
    masterVolumeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(masterVolumeLabel);

    masterVolumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    masterVolumeSlider.setRange(-60.0, 6.0, 0.1);
    masterVolumeSlider.setValue(0.0);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    masterVolumeSlider.setTextValueSuffix(" dB");
    masterVolumeSlider.onValueChange = [this]
    {
        double dB = masterVolumeSlider.getValue();
        masterVolume = (dB <= -60.0) ? 0.0f : juce::Decibels::decibelsToGain((float) dB);
    };
    addAndMakeVisible(masterVolumeSlider);

    deviceManager.addAudioCallback(this);

    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto& input : midiInputs)
    {
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }

    loadingOverlay = std::make_unique<LoadingOverlayComponent>();
    addAndMakeVisible(loadingOverlay.get());

    setSize(900, 800);
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

void MainComponent::setGlobalTempo(double bpm)
{
    currentTempo = bpm;
    for (auto& channel : channelProcessors)
        channel->setTempo(bpm);
}

void MainComponent::showPluginBrowser(int channelIndex, int slotIndex, bool isReplace)
{
    if (!pluginsReady)
        return;

    // Slot 0 accepts instruments or audio effects; insert slots 1-5 are
    // audio-effect-only per spec, so instruments are filtered out for them.
    bool allowInstruments = (slotIndex == ChannelProcessor::slot0Index);

    PluginBrowserComponent::showAsCallOut(
        pluginManager.getPluginList(),
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

            if (loaded)
            {
                // Wait for CallOutBox to fully dismiss before opening editor
                juce::Timer::callAfterDelay(100, [this, channelIndex, slotIndex]
                {
                    channelComponents[(size_t) channelIndex]->refresh();
                    channelProcessors[(size_t) channelIndex]->showEditor(slotIndex);
                });
            }
        },
        *channelComponents[(size_t) channelIndex],
        allowInstruments
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
}

void MainComponent::audioDeviceStopped()
{
    for (auto& channel : channelProcessors)
        channel->prepareToPlay(44100.0, 512);
}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float* const*, int,
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

    masterBuffer.applyGain(masterVolume);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    auto masterArea = area.removeFromRight(90);
    masterVolumeLabel.setBounds(masterArea.removeFromTop(16));
    masterVolumeSlider.setBounds(masterArea);

    area.removeFromRight(20);

    channelViewport.setBounds(area);

    const int channelWidth = 160;
    channelRackContent.setSize(channelWidth * numChannels, area.getHeight());
    for (int i = 0; i < (int) channelComponents.size(); ++i)
        channelComponents[(size_t) i]->setBounds(i * channelWidth, 0, channelWidth, area.getHeight());

    if (loadingOverlay != nullptr)
        loadingOverlay->setBounds(getLocalBounds());
}
