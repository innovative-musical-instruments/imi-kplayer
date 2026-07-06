#include <juce_core/juce_core.h>
#include "SessionFormat.h"

class SessionFormatTests : public juce::UnitTest
{
public:
    SessionFormatTests() : juce::UnitTest("SessionFormat", "SessionFormat") {}

    void runTest() override
    {
        testCase("extractExtraFields keeps only unrecognized top-level keys", [&]
        {
            auto parsed = juce::JSON::parse(R"({
                "formatVersion": 1,
                "sessionName": "known",
                "masterChain": [1, 2, 3],
                "audioInputs": "future field"
            })");

            auto extras = SessionFormat::extractExtraFields(parsed);
            auto* obj = extras.getDynamicObject();
            expect(obj != nullptr);

            expect(! extras.hasProperty("formatVersion"));
            expect(! extras.hasProperty("sessionName"));
            expect(extras.hasProperty("masterChain"));
            expect(extras.hasProperty("audioInputs"));
            expectEquals((int) obj->getProperties().size(), 2);
        });

        testCase("mergeExtraFields adds unknown fields but never overwrites a known one", [&]
        {
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            root->setProperty("formatVersion", 1);
            root->setProperty("sessionName", "current-save");

            auto* extraObj = new juce::DynamicObject();
            extraObj->setProperty("formatVersion", 999);   // stale - must not win
            extraObj->setProperty("masterChain", juce::Array<juce::var>());
            juce::var extras(extraObj);

            SessionFormat::mergeExtraFields(*root, extras);

            expectEquals((int) root->getProperty("formatVersion"), 1);
            expect(root->hasProperty("masterChain"));
        });

        testCase("resolveLoadedFormatVersion clamps to current unless the file is newer", [&]
        {
            expectEquals(SessionFormat::resolveLoadedFormatVersion(0), SessionMigrator::kCurrentFormatVersion);
            expectEquals(SessionFormat::resolveLoadedFormatVersion(SessionMigrator::kCurrentFormatVersion),
                         SessionMigrator::kCurrentFormatVersion);
            expectEquals(SessionFormat::resolveLoadedFormatVersion(999), 999);
        });

        testCase("backupExistingFile copies the prior file to <name>.kplayer.bak", [&]
        {
            juce::TemporaryFile temp(".kplayer");
            auto& file = temp.getFile();

            expect(SessionFormat::backupExistingFile(file));   // nothing on disk yet - not a failure

            file.replaceWithText("original contents");
            auto backup = file.getSiblingFile(file.getFileName() + ".bak");
            backup.deleteFile();

            expect(SessionFormat::backupExistingFile(file));
            expect(backup.existsAsFile());
            expectEquals(backup.loadFileAsString(), juce::String("original contents"));

            backup.deleteFile();
        });
    }
};

static SessionFormatTests sessionFormatTests;
