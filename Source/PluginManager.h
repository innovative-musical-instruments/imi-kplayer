#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    void scanPlugins();
    void scanPluginsAsync(std::function<void()> onComplete);

    juce::KnownPluginList& getPluginList() { return knownPluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    // Favorites and recently-used are keyed by PluginDescription::createIdentifierString()
    // (stable across rescans) and persisted alongside the plugin cache so
    // they survive restarts. Both are small, infrequently-written files -
    // simplicity over performance.
    bool isFavorite(const juce::String& identifierString) const;
    void setFavorite(const juce::String& identifierString, bool shouldBeFavorite);

    // Call once a plugin actually loads successfully, not just on selection.
    void noteRecentlyUsed(const juce::String& identifierString);

    // Most-recently-used first, capped at a small fixed length.
    static constexpr int maxRecentlyUsed = 10;
    const juce::StringArray& getRecentlyUsedIdentifiers() const { return recentlyUsed; }

    // Live scan status, for showing "which plugin is it on right now" in
    // LoadingOverlayComponent during a scan (startup or Settings' Rescan
    // button - same underlying scanPlugins() call either way). Both written
    // to (inside scanPlugins()'s callAsync step lambda, which runs on the
    // message thread - see that function's comment) and read from (a
    // message-thread poll) the message thread only, just at different
    // times, so plain members are safe here with no torn-read risk at all -
    // unlike the audio-thread-shared fields elsewhere in this app, there's
    // no cross-thread access to guard against.
    juce::String getCurrentlyScanningPluginName() const { return currentlyScanningPluginName; }
    float getScanProgress() const { return currentScanProgress; }

    // Snapshot of the dead-man's-pedal file's contents taken at the very
    // start of the most recent scanPlugins() call, before JUCE's own
    // PluginDirectoryScanner starts rewriting it for this run. A non-empty
    // list here means the app didn't exit cleanly during a previous scan
    // (crash, or a force-quit during a hang) - those plugins are already
    // auto-blacklisted by that point (standard PluginDirectoryScanner
    // behaviour, see applyBlacklistingsFromDeadMansPedal); this snapshot
    // exists purely so the caller can tell the user about it once, after
    // the scan completes.
    const juce::StringArray& getPluginsSkippedByLastCrash() const { return lastCrashedPlugins; }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::File getPluginCacheFile();

    juce::StringArray favorites;
    juce::StringArray recentlyUsed;

    juce::String currentlyScanningPluginName;
    float currentScanProgress = 0.0f;
    juce::StringArray lastCrashedPlugins;
    juce::File getFavoritesFile();
    juce::File getRecentlyUsedFile();
    void loadFavoritesAndRecent();
    void saveFavorites();
    void saveRecentlyUsed();

    class ScanThread : public juce::Thread
    {
    public:
        ScanThread(PluginManager& ownerIn, std::function<void()> onCompleteIn)
            : juce::Thread("PluginScanThread"),
            owner(ownerIn),
            onComplete(std::move(onCompleteIn)) {
        }

        void run() override
        {
            owner.scanPlugins();
            juce::MessageManager::callAsync(onComplete);
        }

    private:
        PluginManager& owner;
        std::function<void()> onComplete;
    };

    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManager)
};