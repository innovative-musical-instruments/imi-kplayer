#include "SessionFormat.h"

const juce::StringArray& SessionFormat::getKnownTopLevelFields()
{
    static const juce::StringArray fields {
        "formatVersion", "appVersion", "createdAt", "sessionName",
        "audioDeviceStateXml", "masterVolume", "tempo", "channels"
    };
    return fields;
}

juce::var SessionFormat::extractExtraFields(const juce::var& parsed)
{
    auto* extras = new juce::DynamicObject();

    if (auto* obj = parsed.getDynamicObject())
        for (auto& prop : obj->getProperties())
            if (! getKnownTopLevelFields().contains(prop.name.toString()))
                extras->setProperty(prop.name, prop.value);

    return juce::var(extras);
}

void SessionFormat::mergeExtraFields(juce::DynamicObject& root, const juce::var& extraFields)
{
    if (auto* extras = extraFields.getDynamicObject())
        for (auto& prop : extras->getProperties())
            if (! root.hasProperty(prop.name))
                root.setProperty(prop.name, prop.value);
}

int SessionFormat::resolveLoadedFormatVersion(int fileFormatVersion)
{
    return fileFormatVersion > SessionMigrator::kCurrentFormatVersion ? fileFormatVersion
                                                                       : SessionMigrator::kCurrentFormatVersion;
}

bool SessionFormat::backupExistingFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return true;

    return file.copyFileTo(file.getSiblingFile(file.getFileName() + ".bak"));
}
