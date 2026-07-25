#include "SessionMigrator.h"

juce::Result SessionMigrator::migrate(juce::var& sessionJson)
{
    int fileVersion = (int) sessionJson.getProperty("formatVersion", 0);

    if (fileVersion < 0 || fileVersion > kCurrentFormatVersion)
        return juce::Result::fail("Cannot migrate: unrecognized formatVersion " + juce::String(fileVersion));

    auto& chain = getMigrationChain();
    for (int v = fileVersion; v < kCurrentFormatVersion; ++v)
        sessionJson = chain[(size_t) v](sessionJson);

    return juce::Result::ok();
}

const std::vector<SessionMigrator::MigrationStep>& SessionMigrator::getMigrationChain()
{
    static const std::vector<MigrationStep> chain {
        migrate_v0_to_v1,
        migrate_v1_to_v2,
        migrate_v2_to_v3,
        migrate_v3_to_v4,
    };
    return chain;
}

// Every .kplayer file written before this spec has no formatVersion field at
// all (treated as v0) and a cosmetic "version": "1.0" string that was never
// read back on load. v1 is that same shape with formatVersion added and the
// dead field removed - no other structural change.
juce::var SessionMigrator::migrate_v0_to_v1(const juce::var& v0Json)
{
    auto upgraded = v0Json.clone();
    auto* obj = upgraded.getDynamicObject();
    jassert(obj != nullptr);

    obj->removeProperty("version");
    obj->setProperty("formatVersion", 1);
    return upgraded;
}

// Increment 3 adds the master bus insert chain and (later) per-channel
// audio input routing - both default to an empty array when absent from
// an older file, so nothing else about the shape changes.
juce::var SessionMigrator::migrate_v1_to_v2(const juce::var& v1Json)
{
    auto upgraded = v1Json.clone();
    auto* obj = upgraded.getDynamicObject();
    jassert(obj != nullptr);

    if (! upgraded.hasProperty("masterChain"))
        obj->setProperty("masterChain", juce::var(juce::Array<juce::var>()));

    if (! upgraded.hasProperty("audioInputs"))
        obj->setProperty("audioInputs", juce::var(juce::Array<juce::var>()));

    obj->setProperty("formatVersion", 2);
    return upgraded;
}

// Adds MIDI-clock tempo sync (Kadabra integration): whether it's on and
// which device it follows. Both default to off/none for any file saved
// before this feature existed - no other structural change.
juce::var SessionMigrator::migrate_v2_to_v3(const juce::var& v2Json)
{
    auto upgraded = v2Json.clone();
    auto* obj = upgraded.getDynamicObject();
    jassert(obj != nullptr);

    if (! upgraded.hasProperty("tempoSyncEnabled"))
        obj->setProperty("tempoSyncEnabled", false);

    if (! upgraded.hasProperty("tempoSyncDeviceIdentifier"))
        obj->setProperty("tempoSyncDeviceIdentifier", juce::String());

    obj->setProperty("formatVersion", 3);
    return upgraded;
}

// Adds multitrack recording (arm state + the global recordings folder/
// silence-timeout preference). Per-channel "armed" defaults to false via
// applyChannelVar's own getProperty() fallback (same as every other
// per-channel field, e.g. mute/solo) - no per-channel migration needed here,
// only the new top-level fields.
juce::var SessionMigrator::migrate_v3_to_v4(const juce::var& v3Json)
{
    auto upgraded = v3Json.clone();
    auto* obj = upgraded.getDynamicObject();
    jassert(obj != nullptr);

    if (! upgraded.hasProperty("recordingsFolder"))
        obj->setProperty("recordingsFolder", juce::String());

    if (! upgraded.hasProperty("recordingSilenceTimeoutSeconds"))
        obj->setProperty("recordingSilenceTimeoutSeconds", 60.0);

    if (! upgraded.hasProperty("masterArmed"))
        obj->setProperty("masterArmed", false);

    obj->setProperty("formatVersion", 4);
    return upgraded;
}
