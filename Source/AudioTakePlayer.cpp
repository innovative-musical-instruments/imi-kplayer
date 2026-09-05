#include "AudioTakePlayer.h"

void AudioTakePlayer::publish(std::unique_ptr<juce::MemoryMappedAudioFormatReader> newReader)
{
    auto* raw = newReader.get();
    auto old = std::move(storage);
    storage = std::move(newReader);
    published.store(raw, std::memory_order_release);

    // See RecordingManager::retireTrack for the same drain-margin pattern:
    // by the time this sleep returns, any audio callback that had already
    // loaded the old pointer has finished using it, so it's safe to destroy.
    if (old != nullptr)
    {
        juce::Thread::sleep(50);
        old.reset();
    }
}

bool AudioTakePlayer::loadTake(const juce::File& audioFile)
{
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::MemoryMappedAudioFormatReader> reader(wavFormat.createMemoryMappedReader(audioFile));
    if (reader == nullptr || ! reader->mapEntireFile())
    {
        unload();
        return false;
    }

    lengthSamples = reader->lengthInSamples;

    publish(std::move(reader));
    return true;
}

void AudioTakePlayer::unload()
{
    lengthSamples = 0;
    publish(nullptr);
}

void AudioTakePlayer::renderBlock(juce::int64 transportPositionSamples, int numSamples, bool transportIsPlaying,
                                  float* const* destChannels, int numDestChannels)
{
    if (! transportIsPlaying)
        return;

    auto* reader = published.load(std::memory_order_acquire);
    if (reader == nullptr)
        return;

    reader->read(destChannels, numDestChannels, transportPositionSamples, numSamples);
}
