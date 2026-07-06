#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

// Upgrades a parsed .kplayer session JSON (juce::var) from whatever
// formatVersion it was saved at up to kCurrentFormatVersion, one version
// bump at a time. See docs/K-Player_Session_Format_Versioning_Spec_Increment0.md.
class SessionMigrator
{
public:
    static constexpr int kCurrentFormatVersion = 1;

    // Mutates sessionJson in place, upgrading it to kCurrentFormatVersion.
    // Fails if sessionJson's formatVersion is negative or already newer than
    // kCurrentFormatVersion (that case is handled separately - see spec §4.5).
    static juce::Result migrate(juce::var& sessionJson);

private:
    using MigrationStep = std::function<juce::var(const juce::var&)>;

    // chain[N] transforms formatVersion N to N+1. Append new steps here as
    // the schema grows; never edit a step once it has shipped (spec §4.2).
    static const std::vector<MigrationStep>& getMigrationChain();

    static juce::var migrate_v0_to_v1(const juce::var& v0Json);
};
