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
