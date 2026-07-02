#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    void scanPlugins();
    juce::KnownPluginList& getPluginList()         { return knownPluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::File getPluginCacheFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManager)
};
