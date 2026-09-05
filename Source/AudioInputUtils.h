#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

// Shared by ChannelComponent's own Audio In selector and MasterChainComponent's
// bulk "Audio In" selector (v0.9.8) - both list the device's live input
// channels by name, in the same order.
namespace AudioInputUtils
{
    // Names of the device's *active* input channels, in the same order as
    // audioDeviceIOCallbackWithContext's inputChannelData array - i.e. the
    // Nth active channel in ascending order, not the device's full channel
    // list (which includes channels the user hasn't enabled).
    inline juce::StringArray getActiveAudioInputChannelNames(juce::AudioDeviceManager& deviceManager)
    {
        juce::StringArray names;
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            auto allNames = device->getInputChannelNames();
            auto active   = device->getActiveInputChannels();
            for (int i = 0; i < allNames.size(); ++i)
                if (active[i])
                    names.add(allNames[i]);
        }
        return names;
    }
}
