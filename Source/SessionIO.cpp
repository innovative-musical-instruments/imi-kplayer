#include "SessionIO.h"
#include "MainComponent.h"
#include "ChannelProcessor.h"
#include "SessionMigrator.h"
#include "SessionFormat.h"

namespace
{
    juce::MemoryBlock decodeBase64(const juce::String& text)
    {
        juce::MemoryOutputStream stream;
        juce::Base64::convertFromBase64(stream, text);
        return stream.getMemoryBlock();
    }

    juce::String resolveMidiDeviceName(const juce::String& identifier)
    {
        if (identifier.isEmpty())
            return {};

        for (auto& device : juce::MidiInput::getAvailableDevices())
            if (device.identifier == identifier)
                return device.name;

        return {};
    }

    juce::var pluginSlotToVar(ChannelProcessor& processor, int slotIndex)
    {
        if (! processor.hasPlugin(slotIndex))
            return {};

        auto desc = processor.getPluginDescription(slotIndex);
        auto descXml = desc.createXml();

        auto* obj = new juce::DynamicObject();
        obj->setProperty("pluginDescriptionXml", descXml != nullptr ? descXml->toString() : juce::String());
        obj->setProperty("isBypassed", processor.isBypassed(slotIndex));

        auto stateBlock = processor.getPluginState(slotIndex);
        obj->setProperty("stateBlob", juce::Base64::toBase64(stateBlock.getData(), stateBlock.getSize()));

        return juce::var(obj);
    }

    void applyPluginSlotVar(ChannelProcessor& processor, int slotIndex, const juce::var& v,
                            juce::AudioPluginFormatManager& formatManager,
                            double sampleRate, int blockSize)
    {
        if (! v.isObject())
            return;

        auto xmlString = v.getProperty("pluginDescriptionXml", juce::String()).toString();
        auto xml = juce::XmlDocument::parse(xmlString);
        if (xml == nullptr)
            return;

        juce::PluginDescription desc;
        if (! desc.loadFromXml(*xml))
            return;

        auto state = decodeBase64(v.getProperty("stateBlob", juce::String()).toString());

        bool loaded = processor.loadPlugin(slotIndex, desc, formatManager, sampleRate, blockSize, &state);
        if (loaded)
            processor.setBypassed(slotIndex, (bool) v.getProperty("isBypassed", false));
    }

    juce::var channelToVar(ChannelProcessor& processor)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", processor.getId().toString());
        obj->setProperty("type", "INSTRUMENT");
        obj->setProperty("name", processor.getName());
        obj->setProperty("color", "#3D5A80");
        obj->setProperty("enabled", true);
        obj->setProperty("mute", processor.isMuted());
        obj->setProperty("solo", processor.isSolo());
        obj->setProperty("volume", processor.getGain());
        obj->setProperty("pan", processor.getPan());

        auto deviceId = processor.getMidiDeviceIdentifier();
        obj->setProperty("midiDeviceIdentifier", deviceId);
        obj->setProperty("midiDeviceName", resolveMidiDeviceName(deviceId));
        obj->setProperty("midiChannel", processor.getMidiChannel());

        obj->setProperty("slot0Plugin", pluginSlotToVar(processor, ChannelProcessor::slot0Index));

        juce::Array<juce::var> inserts;
        for (int i = 1; i < ChannelProcessor::totalSlotCount; ++i)
            inserts.add(pluginSlotToVar(processor, i));
        obj->setProperty("insertPlugins", inserts);

        return juce::var(obj);
    }

    void applyChannelVar(ChannelProcessor& processor, const juce::var& v,
                         juce::AudioPluginFormatManager& formatManager,
                         double sampleRate, int blockSize)
    {
        if (! v.isObject())
            return;

        auto idString = v.getProperty("id", juce::String()).toString();
        if (idString.isNotEmpty())
            processor.setId(juce::Uuid(idString));

        processor.setName(v.getProperty("name", juce::String()).toString());
        processor.setMuted((bool) v.getProperty("mute", false));
        processor.setSolo((bool) v.getProperty("solo", false));
        processor.setGain((float) (double) v.getProperty("volume", 1.0));
        processor.setPan((float) (double) v.getProperty("pan", 0.0));
        processor.setMidiChannel((int) v.getProperty("midiChannel", 0));
        processor.setMidiDeviceIdentifier(v.getProperty("midiDeviceIdentifier", juce::String()).toString());

        // Loading a session onto an already-populated channel replaces
        // whatever was there.
        for (int slot = 0; slot < ChannelProcessor::totalSlotCount; ++slot)
            processor.unloadPlugin(slot);

        applyPluginSlotVar(processor, ChannelProcessor::slot0Index,
                          v.getProperty("slot0Plugin", juce::var()),
                          formatManager, sampleRate, blockSize);

        if (auto* insertsArray = v.getProperty("insertPlugins", juce::var()).getArray())
        {
            int count = juce::jmin((int) insertsArray->size(), ChannelProcessor::numInsertSlots);
            for (int i = 0; i < count; ++i)
                applyPluginSlotVar(processor, i + 1, insertsArray->getReference(i),
                                  formatManager, sampleRate, blockSize);
        }
    }
}

bool SessionIO::saveSession(const juce::File& file,
                            MainComponent& mainComponent,
                            juce::AudioDeviceManager& deviceManager)
{
    // Backup-on-save: preserve the previous on-disk copy before overwriting.
    // A failed backup shouldn't block the user from saving their work.
    SessionFormat::backupExistingFile(file);

    auto* root = new juce::DynamicObject();

    // Normally this is kCurrentFormatVersion (upgraded on save, spec §5 rule
    // 1a). If the loaded file was newer than this app understands, it's the
    // file's original formatVersion instead, so a no-op load->save round
    // trip doesn't silently downgrade a Muse this app couldn't fully parse
    // (spec §4.5/§5 rule 1b).
    root->setProperty("formatVersion", mainComponent.getLastLoadedFormatVersion());
    root->setProperty("appVersion", JUCE_APPLICATION_VERSION_STRING);
    root->setProperty("createdAt", juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("sessionName", file.getFileNameWithoutExtension());

    if (auto deviceXml = deviceManager.createStateXml())
        root->setProperty("audioDeviceStateXml", deviceXml->toString());

    root->setProperty("masterVolume", mainComponent.getMasterVolume());
    root->setProperty("tempo", mainComponent.getGlobalTempo());

    juce::Array<juce::var> channelArray;
    for (int i = 0; i < mainComponent.getNumChannels(); ++i)
        channelArray.add(channelToVar(mainComponent.getChannelProcessor(i)));
    root->setProperty("channels", channelArray);

    // Round-trip any fields this app version doesn't recognize (spec §4.4);
    // known fields set above always take precedence over a stale extra.
    SessionFormat::mergeExtraFields(*root, mainComponent.getLastLoadedExtraFields());

    return file.replaceWithText(juce::JSON::toString(juce::var(root)));
}

bool SessionIO::loadSession(const juce::File& file,
                            MainComponent& mainComponent,
                            PluginManager& pluginManager,
                            juce::AudioDeviceManager& deviceManager)
{
    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (! parsed.isObject())
        return false;

    int fileFormatVersion = (int) parsed.getProperty("formatVersion", 0);

    if (fileFormatVersion > SessionMigrator::kCurrentFormatVersion)
    {
        // Newer file than this app understands. Don't migrate backward -
        // just tell the user plainly and fall through to the tolerant parse
        // below for a best-effort load (spec §4.5).
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Newer Session Format",
            "This Muse was created with a newer version of K-Player. "
            "Some features may not load correctly.");
    }
    else if (fileFormatVersion < SessionMigrator::kCurrentFormatVersion)
    {
        auto migrateResult = SessionMigrator::migrate(parsed);
        if (migrateResult.failed())
            return false;
    }

    // Remember round-trip bookkeeping for the next save (spec §4.5/§5 rule 1b):
    // a newer-than-supported file keeps its original formatVersion rather
    // than being silently stamped down to what this app understands.
    mainComponent.setLastLoadedFormatVersion(SessionFormat::resolveLoadedFormatVersion(fileFormatVersion));
    mainComponent.setLastLoadedExtraFields(SessionFormat::extractExtraFields(parsed));

    auto deviceXmlString = parsed.getProperty("audioDeviceStateXml", juce::String()).toString();
    if (deviceXmlString.isNotEmpty())
        if (auto deviceXml = juce::XmlDocument::parse(deviceXmlString))
            deviceManager.initialise(0, 2, deviceXml.get(), true);

    mainComponent.setMasterVolume((float) (double) parsed.getProperty("masterVolume", 1.0));
    mainComponent.setGlobalTempo((double) parsed.getProperty("tempo", 120.0));

    auto* device = deviceManager.getCurrentAudioDevice();
    double sampleRate = device != nullptr ? device->getCurrentSampleRate()        : 44100.0;
    int    blockSize  = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    if (auto* channelArray = parsed.getProperty("channels", juce::var()).getArray())
    {
        // A loaded session fully defines the rack, including its channel
        // count - resize to match (clamped to maxChannels) rather than
        // silently dropping channels beyond whatever the rack happened to
        // be sized at before this load.
        int count = juce::jmin((int) channelArray->size(), MainComponent::maxChannels);
        mainComponent.setChannelCount(count);
        for (int i = 0; i < count; ++i)
        {
            applyChannelVar(mainComponent.getChannelProcessor(i), channelArray->getReference(i),
                            pluginManager.getFormatManager(), sampleRate, blockSize);
            mainComponent.refreshChannelUI(i);
        }
    }

    return true;
}
