#include <juce_core/juce_core.h>
#include "SessionMigrator.h"

namespace
{
    juce::File fixturesDir()
    {
        return juce::File(KPLAYER_TEST_FIXTURES_DIR);
    }

    juce::var parseFixture(const juce::String& fileName)
    {
        auto file = fixturesDir().getChildFile(fileName);
        return juce::JSON::parse(file.loadFileAsString());
    }
}

class SessionMigratorTests : public juce::UnitTest
{
public:
    SessionMigratorTests() : juce::UnitTest("SessionMigrator", "SessionFormat") {}

    void runTest() override
    {
        testCase("v0 sample migrates to current format version", [&]
        {
            auto parsed = parseFixture("v0-sample.kplayer");
            expect(parsed.isObject());
            expect(! parsed.hasProperty("formatVersion"));
            expect(parsed.getProperty("version", juce::String()).toString() == "1.0");

            auto result = SessionMigrator::migrate(parsed);
            expect(result.wasOk());
            expectEquals((int) parsed.getProperty("formatVersion", 0), SessionMigrator::kCurrentFormatVersion);
            expect(! parsed.hasProperty("version"));

            // Only the version/formatVersion fields (and the v1->v2
            // additions below) should have changed.
            expect(parsed.getProperty("sessionName", juce::String()).toString() == "v0-sample");
            expectEquals((double) parsed.getProperty("masterVolume", 0.0), 0.8);
            expect(parsed.getProperty("masterChain", juce::var()).isArray());
            expect(parsed.getProperty("audioInputs", juce::var()).isArray());
        });

        testCase("v1 sample migrates to v2, adding empty masterChain/audioInputs", [&]
        {
            auto parsed = parseFixture("v1-sample.kplayer");
            expectEquals((int) parsed.getProperty("formatVersion", 0), 1);
            expect(! parsed.hasProperty("masterChain"));
            expect(! parsed.hasProperty("audioInputs"));

            auto result = SessionMigrator::migrate(parsed);
            expect(result.wasOk());
            expectEquals((int) parsed.getProperty("formatVersion", 0), SessionMigrator::kCurrentFormatVersion);

            auto* masterChain = parsed.getProperty("masterChain", juce::var()).getArray();
            expect(masterChain != nullptr && masterChain->isEmpty());
            auto* audioInputs = parsed.getProperty("audioInputs", juce::var()).getArray();
            expect(audioInputs != nullptr && audioInputs->isEmpty());

            // Only additive - nothing pre-existing should have changed.
            expect(parsed.getProperty("sessionName", juce::String()).toString() == "v1-sample");
            expectEquals((double) parsed.getProperty("masterVolume", 0.0), 0.8);
        });

        testCase("v2 sample is already at current format version, migrate() is a no-op", [&]
        {
            auto parsed = parseFixture("v2-sample.kplayer");
            expectEquals((int) parsed.getProperty("formatVersion", 0), SessionMigrator::kCurrentFormatVersion);

            auto result = SessionMigrator::migrate(parsed);
            expect(result.wasOk());
            expectEquals((int) parsed.getProperty("formatVersion", 0), SessionMigrator::kCurrentFormatVersion);
        });

        testCase("migrate() rejects a formatVersion newer than this app supports", [&]
        {
            juce::var parsed = juce::JSON::parse("{ \"formatVersion\": 999 }");
            auto result = SessionMigrator::migrate(parsed);
            expect(result.failed());
        });
    }
};

static SessionMigratorTests sessionMigratorTests;
