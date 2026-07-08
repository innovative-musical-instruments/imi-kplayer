#include "PluginManager.h"

PluginManager::PluginManager()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

   #if JUCE_MAC
    formatManager.addFormat(new juce::AudioUnitPluginFormat());
   #endif

    loadFavoritesAndRecent();
}

PluginManager::~PluginManager()
{
    // scanPlugins() has no cancellation checkpoints, so we can't interrupt it early -
    // wait for it to finish rather than destroying formatManager/knownPluginList out from under it.
    if (scanThread != nullptr)
        scanThread->stopThread(-1);
}

juce::File PluginManager::getPluginCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("IMI")
               .getChildFile("KPlayer")
               .getChildFile("plugin_cache.xml");
}

juce::File PluginManager::getFavoritesFile()
{
    return getPluginCacheFile().getSiblingFile("plugin_favorites.txt");
}

juce::File PluginManager::getRecentlyUsedFile()
{
    return getPluginCacheFile().getSiblingFile("plugin_recent.txt");
}

void PluginManager::loadFavoritesAndRecent()
{
    auto favFile = getFavoritesFile();
    if (favFile.existsAsFile())
        favorites.addLines(favFile.loadFileAsString());
    favorites.removeEmptyStrings();

    auto recentFile = getRecentlyUsedFile();
    if (recentFile.existsAsFile())
        recentlyUsed.addLines(recentFile.loadFileAsString());
    recentlyUsed.removeEmptyStrings();
}

void PluginManager::saveFavorites()
{
    getFavoritesFile().getParentDirectory().createDirectory();
    getFavoritesFile().replaceWithText(favorites.joinIntoString("\n"));
}

void PluginManager::saveRecentlyUsed()
{
    getRecentlyUsedFile().getParentDirectory().createDirectory();
    getRecentlyUsedFile().replaceWithText(recentlyUsed.joinIntoString("\n"));
}

bool PluginManager::isFavorite(const juce::String& identifierString) const
{
    return favorites.contains(identifierString);
}

void PluginManager::setFavorite(const juce::String& identifierString, bool shouldBeFavorite)
{
    bool changed;
    if (shouldBeFavorite)
    {
        changed = favorites.addIfNotAlreadyThere(identifierString);
    }
    else
    {
        changed = favorites.contains(identifierString);
        favorites.removeString(identifierString);
    }

    if (changed)
        saveFavorites();
}

void PluginManager::noteRecentlyUsed(const juce::String& identifierString)
{
    recentlyUsed.removeString(identifierString);
    recentlyUsed.insert(0, identifierString);
    while (recentlyUsed.size() > maxRecentlyUsed)
        recentlyUsed.remove(recentlyUsed.size() - 1);
    saveRecentlyUsed();
}

void PluginManager::scanPlugins()
{
    auto cacheFile = getPluginCacheFile();
    if (cacheFile.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse(cacheFile))
            knownPluginList.recreateFromXml(*xml);
    }

    juce::FileSearchPath searchPath;

   #if JUCE_MAC
    searchPath.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins/VST3"));
    searchPath.add(juce::File("/Library/Audio/Plug-Ins/Components"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins/Components"));
   #elif JUCE_WINDOWS
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
   #endif

    auto deadMansPedalFile = getPluginCacheFile().getSiblingFile("plugin_scan_deadmanspedal.txt");

    for (auto* format : formatManager.getFormats())
    {
        juce::PluginDirectoryScanner scanner(
            knownPluginList,
            *format,
            searchPath,
            true,
            deadMansPedalFile
        );

        // Some plugins (e.g. Kontakt's VST3) do main-thread-only work - such as
        // querying macOS text input sources - as part of their module init, and
        // will hit a dispatch_assert_queue crash if that happens off the message
        // thread. Run each scan step there and block this thread until it's done,
        // so the scan is still orchestrated in the background (window shows
        // immediately, UI stays responsive between plugins) without touching
        // plugin internals from the wrong thread.
        juce::String pluginBeingScanned;
        bool more = true;
        while (more)
        {
            juce::WaitableEvent stepDone;
            juce::MessageManager::callAsync([&scanner, &pluginBeingScanned, &more, &stepDone]
            {
                more = scanner.scanNextFile(true, pluginBeingScanned);
                stepDone.signal();
            });
            stepDone.wait();
        }
    }

    cacheFile.getParentDirectory().createDirectory();
    if (auto xml = knownPluginList.createXml())
        xml->writeTo(cacheFile);
}

void PluginManager::scanPluginsAsync(std::function<void()> onComplete)
{
    scanThread = std::make_unique<ScanThread>(*this, std::move(onComplete));
    scanThread->startThread();
}
