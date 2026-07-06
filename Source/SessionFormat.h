#pragma once
#include <juce_core/juce_core.h>
#include "SessionMigrator.h"

// Pure, juce_core-only helpers for the load/save-time bookkeeping described
// in docs/K-Player_Session_Format_Versioning_Spec_Increment0.md §4.4/§4.5/§5.
// Kept separate from SessionIO.cpp (which also depends on the audio/GUI
// stack via MainComponent) so this logic can be unit-tested in isolation.
namespace SessionFormat
{
    // Any top-level key not in this set is treated as unrecognized and must
    // be preserved via extractExtraFields/mergeExtraFields rather than
    // silently dropped.
    const juce::StringArray& getKnownTopLevelFields();

    // Copies every top-level key from parsed that isn't in
    // getKnownTopLevelFields() into a fresh DynamicObject, verbatim.
    juce::var extractExtraFields(const juce::var& parsed);

    // Merges extraFields into root, skipping any key root already has
    // (known fields always win over a stale preserved extra).
    void mergeExtraFields(juce::DynamicObject& root, const juce::var& extraFields);

    // What formatVersion to remember (and later re-save) for a just-loaded
    // file: the file's own version if it's newer than this app understands,
    // otherwise kCurrentFormatVersion (the in-memory model has been fully
    // migrated up to current). See spec §4.5/§5 rule 1b.
    int resolveLoadedFormatVersion(int fileFormatVersion);

    // Backup-on-save: if file exists, copies it to "<name>.kplayer.bak"
    // alongside it before it gets overwritten. Returns true if there was
    // nothing to back up, or the backup copy succeeded. A failed backup
    // shouldn't block the primary save, so callers may ignore a false return.
    bool backupExistingFile(const juce::File& file);
}
