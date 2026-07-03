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

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::File getPluginCacheFile();

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