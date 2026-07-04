#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PluginManager.h"

class MainComponent;

// Reads/writes the .kplayer session JSON format designed in
// docs/Kadabra_K-Player_Specification_MVP_v1.0.md section 6.
namespace SessionIO
{
    bool saveSession(const juce::File& file,
                     MainComponent& mainComponent,
                     juce::AudioDeviceManager& deviceManager);

    bool loadSession(const juce::File& file,
                     MainComponent& mainComponent,
                     PluginManager& pluginManager,
                     juce::AudioDeviceManager& deviceManager);
}
