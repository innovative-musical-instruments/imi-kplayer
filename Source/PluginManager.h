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

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::File getPluginCacheFile();

    juce::StringArray favorites;
    juce::StringArray recentlyUsed;
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