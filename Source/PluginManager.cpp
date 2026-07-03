#include "PluginManager.h"

PluginManager::PluginManager()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

   #if JUCE_MAC
    formatManager.addFormat(new juce::AudioUnitPluginFormat());
   #endif
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

    for (auto* format : formatManager.getFormats())
    {
        juce::PluginDirectoryScanner scanner(
            knownPluginList,
            *format,
            searchPath,
            true,
            juce::File()
        );

        juce::String pluginBeingScanned;
        while (scanner.scanNextFile(true, pluginBeingScanned)) {}
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
