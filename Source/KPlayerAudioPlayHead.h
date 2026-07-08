#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// Shared by ChannelProcessor and MasterChainProcessor: reports a live,
// externally-settable BPM to whatever plugin is loaded, so tempo-synced
// effects (delays, LFOs) track K-Player's global tempo rather than a fixed
// value. atomic<double> lets the message thread update tempo without
// touching anything the audio thread reads unsynchronized.
class KPlayerAudioPlayHead : public juce::AudioPlayHead
{
public:
    void setBpm(double newBpm) { bpm.store(newBpm, std::memory_order_relaxed); }
    double getBpm() const      { return bpm.load(std::memory_order_relaxed); }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm.load(std::memory_order_relaxed));
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
        info.setIsPlaying(true);
        info.setIsRecording(false);
        info.setIsLooping(false);
        info.setPpqPosition(0.0);
        info.setTimeInSeconds(0.0);
        info.setFrameRate(juce::AudioPlayHead::FrameRate{});
        return info;
    }

private:
    std::atomic<double> bpm { 120.0 };
};
