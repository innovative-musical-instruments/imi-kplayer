#include "SessionIO.h"
#include "MainComponent.h"
#include "ChannelProcessor.h"
#include "MasterChainProcessor.h"
#include "RecordingManager.h"
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

    // A saved MIDI input identifier is a platform- (and often installation-)
    // specific string - JUCE's CoreMIDI backend hands back a different
    // identifier shape than its Windows backends do, so "Kadabra" saved on
    // Mac never matches any live device's identifier on Windows, even
    // though the exact same hardware is connected under the exact same
    // name. Same class of problem as resolveLocalDescription() below solves
    // for plugin paths - re-resolve against this machine's own currently
    // available MIDI inputs, matched by name (the one thing that actually
    // travels across platforms) whenever the raw identifier doesn't match
    // directly. Identifier match is tried first (cheap same-machine
    // fast path, and avoids ambiguity if the name were ever visually
    // identical to another device's saved name), name is the fallback.
    // Empty (no device selected) and Take references (see
    // RecordingManager::isTakeIdentifier - encoded into this same field for
    // channels' MIDI routing) are passed through unchanged, both being
    // things this shouldn't touch. Falls through to the saved identifier
    // verbatim if neither matches - same "None selected" outcome as before
    // this fix for a genuinely unplugged/renamed device.
    juce::String resolveLocalMidiDeviceIdentifier(const juce::String& savedIdentifier,
                                                   const juce::String& savedName)
    {
        if (savedIdentifier.isEmpty() || RecordingManager::isTakeIdentifier(savedIdentifier))
            return savedIdentifier;

        auto devices = juce::MidiInput::getAvailableDevices();

        for (auto& device : devices)
            if (device.identifier == savedIdentifier)
                return savedIdentifier;

        if (savedName.isNotEmpty())
            for (auto& device : devices)
                if (device.name == savedName)
                    return device.identifier;

        return savedIdentifier;
    }

    // Templated so it works for any slot host with this method shape -
    // ChannelProcessor and MasterChainProcessor both qualify, and the
    // master chain (Increment 3) reuses this rather than duplicating it.
    template <typename SlotHost>
    juce::var pluginSlotToVar(SlotHost& processor, int slotIndex)
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

    // A saved PluginDescription's fileOrIdentifier is an absolute,
    // machine-specific install path (e.g. "C:\Program Files\Common
    // Files\VST3\Foo.vst3" on Windows vs "/Library/Audio/Plug-Ins/VST3/
    // Foo.vst3" on Mac) - never resolvable as-is on a different machine or
    // platform, even when the exact same plugin is installed there. Find
    // the equivalent entry in this machine's own scanned plugin list
    // (correct local paths) instead, matched by real plugin identity
    // rather than path.
    //
    // uniqueId is the field to match on: JUCE introduced it specifically
    // to fix VST3 plugins with matching FUIDs producing different legacy
    // uid values per platform, and it's confirmed stable across a Mac/
    // Windows pair of K-Player session files for the same plugin. Fall
    // back to name+manufacturer+format for the rare case uniqueId is
    // unpopulated (e.g. an older scan cache) or a plugin was rebuilt with
    // a different id per platform.
    juce::PluginDescription resolveLocalDescription(const juce::PluginDescription& saved,
                                                     const juce::KnownPluginList& knownPluginList)
    {
        auto candidates = knownPluginList.getTypes();

        for (auto& candidate : candidates)
            if (candidate.pluginFormatName == saved.pluginFormatName
                && candidate.uniqueId != 0
                && candidate.uniqueId == saved.uniqueId)
                return candidate;

        for (auto& candidate : candidates)
            if (candidate.pluginFormatName == saved.pluginFormatName
                && candidate.name == saved.name
                && candidate.manufacturerName == saved.manufacturerName)
                return candidate;

        // No local match - fall through with the saved description
        // untouched (e.g. same machine, plugin never moved).
        return saved;
    }

    // Delta load (see docs/KPlayer_Session_Save_Load_Design_2026-07-25.md
    // Part B): the caller no longer unloads this slot before calling here
    // (see applyChannelVar/applyMasterChainVar below) - this decides for
    // itself whether the existing instance can be kept:
    //   - saved slot is empty -> unload whatever's here, if anything.
    //   - same plugin identity already loaded, identical state -> true
    //     no-op, not even a setStateInformation() call.
    //   - same plugin identity, different state -> push the new state into
    //     the running instance in place (updatePluginState()), skipping
    //     destroy+recreate entirely.
    //   - anything else (different plugin, or nothing loaded yet) -> fall
    //     back to the full unload+load path, unchanged from before.
    template <typename SlotHost>
    void applyPluginSlotVar(SlotHost& processor, int slotIndex, const juce::var& v,
                            juce::AudioPluginFormatManager& formatManager,
                            const juce::KnownPluginList& knownPluginList,
                            double sampleRate, int blockSize)
    {
        if (! v.isObject())
        {
            if (processor.hasPlugin(slotIndex))
                processor.unloadPlugin(slotIndex);
            return;
        }

        auto xmlString = v.getProperty("pluginDescriptionXml", juce::String()).toString();
        auto xml = juce::XmlDocument::parse(xmlString);
        if (xml == nullptr)
            return;

        juce::PluginDescription savedDesc;
        if (! savedDesc.loadFromXml(*xml))
            return;

        auto state = decodeBase64(v.getProperty("stateBlob", juce::String()).toString());
        auto localDesc = resolveLocalDescription(savedDesc, knownPluginList);
        bool targetBypassed = (bool) v.getProperty("isBypassed", false);

        if (processor.hasPlugin(slotIndex)
            && processor.getPluginDescription(slotIndex).createIdentifierString() == localDesc.createIdentifierString())
        {
            auto currentState = processor.getPluginState(slotIndex);
            if (currentState != state)
                processor.updatePluginState(slotIndex, state);
            // else: identical plugin, identical state already loaded -
            // nothing to push, not even setStateInformation().

            processor.setBypassed(slotIndex, targetBypassed);
            return;
        }

        if (processor.hasPlugin(slotIndex))
            processor.unloadPlugin(slotIndex);

        bool loaded = processor.loadPlugin(slotIndex, localDesc, formatManager, sampleRate, blockSize, &state);
        if (! loaded && localDesc.fileOrIdentifier != savedDesc.fileOrIdentifier)
        {
            // Relinked entry still failed to load (e.g. a stale plugin
            // cache) - retry with the saved description verbatim, same as
            // pre-relink behaviour, rather than giving up early.
            loaded = processor.loadPlugin(slotIndex, savedDesc, formatManager, sampleRate, blockSize, &state);
        }

        if (loaded)
            processor.setBypassed(slotIndex, targetBypassed);
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

        // Index into the audio device's active input channels, or -1 for
        // none (Increment 3 item 8) - matches the MVP spec's per-channel
        // `audioInput` field, not the top-level `audioInputs` array the
        // v1->v2 migration reserved as a placeholder (that array shape
        // turned out not to fit; it's left in the schema inert/unused).
        obj->setProperty("audioInputChannel", processor.getAudioInputChannelIndex());
        obj->setProperty("audioTakeIdentifier", processor.getAudioTakeIdentifier());

        obj->setProperty("slot0Plugin", pluginSlotToVar(processor, ChannelProcessor::slot0Index));

        juce::Array<juce::var> inserts;
        for (int i = 1; i < ChannelProcessor::totalSlotCount; ++i)
            inserts.add(pluginSlotToVar(processor, i));
        obj->setProperty("insertPlugins", inserts);

        return juce::var(obj);
    }

    void applyChannelVar(ChannelProcessor& processor, const juce::var& v,
                         juce::AudioPluginFormatManager& formatManager,
                         const juce::KnownPluginList& knownPluginList,
                         double sampleRate, int blockSize, int channelIndex)
    {
        if (! v.isObject())
            return;

        auto idString = v.getProperty("id", juce::String()).toString();
        if (idString.isNotEmpty())
            processor.setId(juce::Uuid(idString));

        // Sessions saved before the rename feature (item 1.1) stamped
        // "Channel N" into this field unconditionally - treat that exact
        // pattern as "no custom name" on load rather than showing a
        // redundant "1. Channel 1"-style label. No formatVersion bump
        // needed: this is a display-layer reinterpretation of an existing
        // field, not a schema shape change.
        auto loadedName = v.getProperty("name", juce::String()).toString();
        if (loadedName == ("Channel " + juce::String(channelIndex + 1)))
            loadedName.clear();
        processor.setName(loadedName);
        processor.setMuted((bool) v.getProperty("mute", false));
        processor.setSolo((bool) v.getProperty("solo", false));
        processor.setGain((float) (double) v.getProperty("volume", 1.0));
        processor.setPan((float) (double) v.getProperty("pan", 0.0));
        processor.setMidiChannel((int) v.getProperty("midiChannel", 0));
        auto savedMidiDeviceId   = v.getProperty("midiDeviceIdentifier", juce::String()).toString();
        auto savedMidiDeviceName = v.getProperty("midiDeviceName", juce::String()).toString();
        processor.setMidiDeviceIdentifier(resolveLocalMidiDeviceIdentifier(savedMidiDeviceId, savedMidiDeviceName));
        processor.setAudioInputChannelIndex((int) v.getProperty("audioInputChannel", -1));
        processor.setAudioTakeIdentifier(v.getProperty("audioTakeIdentifier", juce::String()).toString());

        // Loading a session onto an already-populated channel replaces
        // whatever was there - but per-slot now (see applyPluginSlotVar's
        // own comment), not via a blanket unload-everything-first pass, so
        // a slot whose target plugin+state already matches what's running
        // never pays a destroy+recreate cost at all.
        applyPluginSlotVar(processor, ChannelProcessor::slot0Index,
                          v.getProperty("slot0Plugin", juce::var()),
                          formatManager, knownPluginList, sampleRate, blockSize);

        auto* insertsArray = v.getProperty("insertPlugins", juce::var()).getArray();
        for (int i = 0; i < ChannelProcessor::numInsertSlots; ++i)
        {
            // Missing/short array (older or hand-edited file) - treat any
            // slot beyond what was actually saved as empty, same as a
            // present-but-null entry would be.
            auto slotVar = (insertsArray != nullptr && i < insertsArray->size())
                             ? insertsArray->getReference(i) : juce::var();
            applyPluginSlotVar(processor, i + 1, slotVar,
                              formatManager, knownPluginList, sampleRate, blockSize);
        }
    }

    juce::var masterChainToVar(MasterChainProcessor& processor)
    {
        juce::Array<juce::var> slots;
        for (int i = 0; i < MasterChainProcessor::numSlots; ++i)
            slots.add(pluginSlotToVar(processor, i));
        return slots;
    }

    void applyMasterChainVar(MasterChainProcessor& processor, const juce::var& v,
                             juce::AudioPluginFormatManager& formatManager,
                             const juce::KnownPluginList& knownPluginList,
                             double sampleRate, int blockSize)
    {
        // Per-slot, same as applyChannelVar - no blanket pre-unload.
        auto* array = v.getArray();
        for (int i = 0; i < MasterChainProcessor::numSlots; ++i)
        {
            auto slotVar = (array != nullptr && i < array->size()) ? array->getReference(i) : juce::var();
            applyPluginSlotVar(processor, i, slotVar,
                              formatManager, knownPluginList, sampleRate, blockSize);
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
    root->setProperty("tempoSyncEnabled", mainComponent.isTempoSyncEnabled());
    root->setProperty("tempoSyncDeviceIdentifier", mainComponent.getTempoSyncDeviceIdentifier());
    root->setProperty("tempoSyncDeviceName", resolveMidiDeviceName(mainComponent.getTempoSyncDeviceIdentifier()));

    root->setProperty("recordingsFolder", mainComponent.getRecordingsFolder().getFullPathName());
    root->setProperty("recordingSilenceTimeoutSeconds", mainComponent.getRecordingSilenceTimeoutSeconds());
    root->setProperty("masterArmed", mainComponent.isMasterArmed());

    juce::Array<juce::var> channelArray;
    for (int i = 0; i < mainComponent.getNumChannels(); ++i)
    {
        auto channelVar = channelToVar(mainComponent.getChannelProcessor(i));
        if (auto* obj = channelVar.getDynamicObject())
            obj->setProperty("armed", mainComponent.isChannelArmed(i));
        channelArray.add(channelVar);
    }
    root->setProperty("channels", channelArray);

    root->setProperty("masterChain", masterChainToVar(mainComponent.getMasterChainProcessor()));

    root->setProperty("windowWidth", mainComponent.getSavedWindowWidth());
    root->setProperty("windowHeight", mainComponent.getSavedWindowHeight());
    root->setProperty("inputSectionCollapsed", mainComponent.isInputSectionCollapsed());

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

    // Delta load (see docs/KPlayer_Session_Save_Load_Design_2026-07-25.md
    // Part B): AudioDeviceManager::initialise() always briefly stops/
    // restarts the audio callback, regardless of whether anything about
    // the device config actually changed - an audible dropout on every
    // load/song-switch even for the common case of staying on the same
    // interface throughout a set. Skipped entirely when the saved XML
    // matches the live device's own current state string-for-string (a
    // simple text comparison - same "position-based, simplest correct
    // approach" philosophy as the plugin-slot matching above; a
    // semantically-identical-but-differently-formatted XML string would
    // still trigger a reinit, which is an acceptable, safe-by-default
    // false negative rather than a risk of skipping a real device change).
    auto deviceXmlString = parsed.getProperty("audioDeviceStateXml", juce::String()).toString();
    if (deviceXmlString.isNotEmpty())
    {
        juce::String currentDeviceXmlString;
        if (auto currentXml = deviceManager.createStateXml())
            currentDeviceXmlString = currentXml->toString();

        if (currentDeviceXmlString != deviceXmlString)
            if (auto deviceXml = juce::XmlDocument::parse(deviceXmlString))
                deviceManager.initialise(0, 2, deviceXml.get(), true);
    }

    mainComponent.setMasterVolume((float) (double) parsed.getProperty("masterVolume", 1.0));
    mainComponent.setGlobalTempo((double) parsed.getProperty("tempo", 120.0));
    {
        auto savedSyncId   = parsed.getProperty("tempoSyncDeviceIdentifier", juce::String()).toString();
        auto savedSyncName = parsed.getProperty("tempoSyncDeviceName", juce::String()).toString();
        mainComponent.setTempoSyncDeviceIdentifier(resolveLocalMidiDeviceIdentifier(savedSyncId, savedSyncName));
    }
    mainComponent.setTempoSyncEnabled((bool) parsed.getProperty("tempoSyncEnabled", false));

    mainComponent.setRecordingsFolder(juce::File(parsed.getProperty("recordingsFolder", juce::String()).toString()));
    // Picks up this session's Take groups for the Master section's bulk
    // Audio In/MIDI In selectors - same timing as refreshChannelTakeList()/
    // refreshChannelAudioTakeList() below, just once for the whole session
    // rather than per channel.
    mainComponent.refreshMasterTakeGroups();
    mainComponent.setRecordingSilenceTimeoutSeconds((double) parsed.getProperty("recordingSilenceTimeoutSeconds", 60.0));
    mainComponent.setMasterArmed((bool) parsed.getProperty("masterArmed", false));

    mainComponent.setWindowSize((int) parsed.getProperty("windowWidth", 0),
                                (int) parsed.getProperty("windowHeight", 0));
    mainComponent.setInputSectionCollapsedState((bool) parsed.getProperty("inputSectionCollapsed", false));

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
            auto& channelVar = channelArray->getReference(i);
            applyChannelVar(mainComponent.getChannelProcessor(i), channelVar,
                            pluginManager.getFormatManager(), pluginManager.getPluginList(),
                            sampleRate, blockSize, i);
            mainComponent.setChannelArmed(i, (bool) channelVar.getProperty("armed", false));
            // A saved midiDeviceIdentifier may reference a Take (see
            // RecordingManager::isTakeIdentifier) - applyChannelVar() above
            // set it directly on ChannelProcessor, bypassing
            // ChannelComponent's onMidiTakeSelected/onMidiTakeDeselected, so
            // the corresponding MidiTakePlayer needs loading explicitly.
            // refreshChannelTakeList() picks up this session's recordingsFolder
            // (already set above) before refreshChannelUI() reflects the
            // selection in the combo box.
            mainComponent.refreshChannelTakeList(i);
            mainComponent.resolveMidiTakeSelectionForChannel(i);
            // Same treatment for a saved audioTakeIdentifier (Increment C).
            mainComponent.refreshChannelAudioTakeList(i);
            mainComponent.resolveAudioTakeSelectionForChannel(i);
            mainComponent.refreshChannelUI(i);
        }
    }

    applyMasterChainVar(mainComponent.getMasterChainProcessor(),
                        parsed.getProperty("masterChain", juce::var()),
                        pluginManager.getFormatManager(), pluginManager.getPluginList(),
                        sampleRate, blockSize);
    mainComponent.refreshMasterChainUI();

    return true;
}
