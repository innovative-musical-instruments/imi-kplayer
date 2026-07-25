#include "RecordingManager.h"

RecordingManager::RecordingManager()
{
    for (auto& p : channelTrackPublished)
        p.store(nullptr, std::memory_order_relaxed);

    backgroundThread.startThread();
}

RecordingManager::~RecordingManager()
{
    if (isRecording())
        stopRecording();
    backgroundThread.stopThread(2000);
}

void RecordingManager::setChannelCount(int count)
{
    // Retire any track beyond the new count that was still recording -
    // shrinking the channel count mid-take isn't a supported workflow, but
    // this keeps it from leaking a dangling file handle rather than
    // crashing or corrupting anything.
    for (int i = count; i < (int) channelArmed.size() && i < maxTracks; ++i)
        retireTrack(channelTrackStorage[(size_t) i], channelTrackPublished[(size_t) i]);

    channelArmed.resize((size_t) count, false);
}

void RecordingManager::setChannelArmed(int channelIndex, bool armed)
{
    if (channelIndex < 0 || channelIndex >= (int) channelArmed.size() || channelIndex >= maxTracks)
        return;
    if (channelArmed[(size_t) channelIndex] == armed)
        return;

    channelArmed[(size_t) channelIndex] = armed;

    if (! isRecording())
        return;

    if (armed)
    {
        auto file = currentTakeFolder.getChildFile("Channel " + juce::String(channelIndex + 1) + ".wav");
        auto track = createTrack(file, recordingSampleRate, recordingNumChannelChannels);
        // A failure here (e.g. disk full mid-take) just means this one
        // source silently isn't captured - same failure mode as any other
        // file-system error during a take, not worth interrupting the rest
        // of an in-progress recording over.
        if (track != nullptr)
        {
            catchUpTrackToLive(*track, recordingNumChannelChannels);
            publishTrack(channelTrackStorage[(size_t) channelIndex],
                        channelTrackPublished[(size_t) channelIndex], std::move(track));
        }
    }
    else
    {
        retireTrack(channelTrackStorage[(size_t) channelIndex], channelTrackPublished[(size_t) channelIndex]);
    }
}

bool RecordingManager::isChannelArmed(int channelIndex) const
{
    if (channelIndex >= 0 && channelIndex < (int) channelArmed.size())
        return channelArmed[(size_t) channelIndex];
    return false;
}

void RecordingManager::setMasterArmed(bool armed)
{
    if (masterArmed == armed)
        return;
    masterArmed = armed;

    if (! isRecording())
        return;

    if (armed)
    {
        auto file = currentTakeFolder.getChildFile("Master.wav");
        auto track = createTrack(file, recordingSampleRate, recordingNumMasterChannels);
        if (track != nullptr)
        {
            catchUpTrackToLive(*track, recordingNumMasterChannels);
            publishTrack(masterTrackStorage, masterTrackPublished, std::move(track));
        }
    }
    else
    {
        retireTrack(masterTrackStorage, masterTrackPublished);
    }
}

std::unique_ptr<RecordingManager::RecordingTrack> RecordingManager::createTrack(
    const juce::File& file, double sampleRate, int numChannels)
{
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return nullptr;

    juce::WavAudioFormat wavFormat;
    auto options = juce::AudioFormatWriter::Options{}
                        .withSampleRate(sampleRate)
                        .withNumChannels(numChannels)
                        .withBitsPerSample(32)
                        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    auto writer = wavFormat.createWriterFor(stream, options);
    if (writer == nullptr)
        return nullptr;

    auto track = std::make_unique<RecordingTrack>();
    // numSamplesToBuffer: ~0.75s at 44.1kHz stereo - generous enough that a
    // brief background-thread stall doesn't drop blocks, tiny relative to
    // typical available RAM.
    track->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), backgroundThread, 32768);
    return track;
}

void RecordingManager::padTrackWithSilence(RecordingTrack& track, int numChannels, juce::int64 numSamples)
{
    if (numSamples <= 0)
        return;

    constexpr int chunkSize = 8192;
    juce::AudioBuffer<float> silence(numChannels, chunkSize);
    silence.clear();

    juce::int64 remaining = numSamples;
    while (remaining > 0)
    {
        int thisChunk = (int) juce::jmin<juce::int64>(remaining, chunkSize);
        while (! track.writer->write(silence.getArrayOfReadPointers(), thisChunk))
            juce::Thread::sleep(5);
        remaining -= thisChunk;
    }
}

void RecordingManager::catchUpTrackToLive(RecordingTrack& track, int numChannels)
{
    juce::int64 padded = 0;
    for (int pass = 0; pass < 8; ++pass)
    {
        auto target = samplesRecorded.load(std::memory_order_relaxed);
        if (target <= padded)
            return;
        padTrackWithSilence(track, numChannels, target - padded);
        padded = target;
    }
}

void RecordingManager::publishTrack(std::unique_ptr<RecordingTrack>& storage,
                                     std::atomic<RecordingTrack*>& published,
                                     std::unique_ptr<RecordingTrack> track)
{
    storage = std::move(track);
    published.store(storage.get(), std::memory_order_release);
}

void RecordingManager::retireTrack(std::unique_ptr<RecordingTrack>& storage,
                                    std::atomic<RecordingTrack*>& published)
{
    if (storage == nullptr)
        return;
    published.store(nullptr, std::memory_order_release);
    juce::Thread::sleep(50);
    storage.reset();
}

void RecordingManager::teardownAllTracks()
{
    for (size_t i = 0; i < channelTrackStorage.size(); ++i)
    {
        channelTrackPublished[i].store(nullptr, std::memory_order_relaxed);
        channelTrackStorage[i].reset();
    }
    masterTrackPublished.store(nullptr, std::memory_order_relaxed);
    masterTrackStorage.reset();
}

juce::String RecordingManager::startRecording(double sampleRate, int numChannelChannels, int numMasterChannels)
{
    if (isRecording())
        return {};

    if (recordingsFolder == juce::File())
        return "No recordings folder has been set.";

    if (! recordingsFolder.isDirectory() && ! recordingsFolder.createDirectory())
        return "Could not create or access the recordings folder: " + recordingsFolder.getFullPathName();

    bool anyArmed = masterArmed;
    for (bool armed : channelArmed)
        anyArmed = anyArmed || armed;
    if (! anyArmed)
        return "Nothing is armed to record - arm at least one channel or the master.";

    auto takeFolder = recordingsFolder.getChildFile(
        juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"));
    if (! takeFolder.createDirectory())
        return "Could not create a folder for this take: " + takeFolder.getFullPathName();

    // Nothing is "live" yet (recording is still false), so there's no
    // concurrent audio-thread access to worry about here - build straight
    // into the real storage/published slots and just tear back down on
    // failure, same effect as the old build-locally-then-publish approach
    // with less duplication now that each slot has its own safe publish.
    bool anyTrackCreated = false;

    for (size_t i = 0; i < channelArmed.size(); ++i)
    {
        if (! channelArmed[i])
            continue;

        auto file = takeFolder.getChildFile("Channel " + juce::String((int) i + 1) + ".wav");
        auto track = createTrack(file, sampleRate, numChannelChannels);
        if (track == nullptr)
        {
            teardownAllTracks();
            takeFolder.deleteRecursively();
            return "Could not create the recording file for channel " + juce::String((int) i + 1) + ".";
        }
        publishTrack(channelTrackStorage[i], channelTrackPublished[i], std::move(track));
        anyTrackCreated = true;
    }

    if (masterArmed)
    {
        auto file = takeFolder.getChildFile("Master.wav");
        auto track = createTrack(file, sampleRate, numMasterChannels);
        if (track == nullptr)
        {
            teardownAllTracks();
            takeFolder.deleteRecursively();
            return "Could not create the recording file for the master output.";
        }
        publishTrack(masterTrackStorage, masterTrackPublished, std::move(track));
        anyTrackCreated = true;
    }

    if (! anyTrackCreated)
    {
        takeFolder.deleteRecursively();
        return "Nothing is armed to record - arm at least one channel or the master.";
    }

    currentTakeFolder            = takeFolder;
    recordingNumChannelChannels  = numChannelChannels;
    recordingNumMasterChannels   = numMasterChannels;

    silentSampleRunLength.store(0, std::memory_order_relaxed);
    samplesRecorded.store(0, std::memory_order_relaxed);
    recordingSampleRate = sampleRate;

    recording.store(true, std::memory_order_release);
    return {};
}

void RecordingManager::stopRecording()
{
    if (! isRecording())
        return;

    // See ChannelProcessor::loadPlugin for the same drain-margin pattern:
    // the audio thread checks `recording` before touching any track, so a
    // brief sleep here guarantees no callback is still mid-write before the
    // ThreadedWriters (whose destructor flushes and finalizes the file) get
    // torn down below - one drain covers every track at once here, unlike
    // retireTrack()'s per-track version used for live-unarming mid-take.
    recording.store(false, std::memory_order_release);
    juce::Thread::sleep(50);

    teardownAllTracks();
}

double RecordingManager::getRecordingElapsedSeconds() const
{
    return (double) samplesRecorded.load(std::memory_order_relaxed) / recordingSampleRate;
}

void RecordingManager::writeChannelBlock(int channelIndex, const juce::AudioBuffer<float>& buffer)
{
    if (! recording.load(std::memory_order_acquire))
        return;
    if (channelIndex < 0 || channelIndex >= maxTracks)
        return;

    auto* track = channelTrackPublished[(size_t) channelIndex].load(std::memory_order_acquire);
    if (track == nullptr)
        return;

    if (buffer.getMagnitude(0, buffer.getNumSamples()) >= silenceThresholdLinear)
        blockHadSignalThisCallback = true;

    track->writer->write(buffer.getArrayOfReadPointers(), buffer.getNumSamples());
}

void RecordingManager::writeMasterBlock(const juce::AudioBuffer<float>& buffer)
{
    if (! recording.load(std::memory_order_acquire))
        return;

    auto* track = masterTrackPublished.load(std::memory_order_acquire);
    if (track == nullptr)
        return;

    if (buffer.getMagnitude(0, buffer.getNumSamples()) >= silenceThresholdLinear)
        blockHadSignalThisCallback = true;

    track->writer->write(buffer.getArrayOfReadPointers(), buffer.getNumSamples());
}

void RecordingManager::noteBlockProcessed(int numSamples)
{
    if (! recording.load(std::memory_order_acquire))
    {
        blockHadSignalThisCallback = false;
        return;
    }

    samplesRecorded.fetch_add(numSamples, std::memory_order_relaxed);

    if (blockHadSignalThisCallback)
        silentSampleRunLength.store(0, std::memory_order_relaxed);
    else
        silentSampleRunLength.fetch_add(numSamples, std::memory_order_relaxed);

    blockHadSignalThisCallback = false;
}

void RecordingManager::pollForAutoStop()
{
    if (! isRecording())
        return;

    auto silentSamples  = silentSampleRunLength.load(std::memory_order_relaxed);
    double silentSeconds = (double) silentSamples / recordingSampleRate;
    if (silentSeconds >= silenceTimeoutSeconds)
    {
        stopRecording();
        if (onAutoStopped)
            onAutoStopped("No audio detected for " + juce::String((int) silenceTimeoutSeconds)
                        + "s - recording stopped automatically.");
        return;
    }

    auto freeBytes = recordingsFolder.getBytesFreeOnVolume();
    if (freeBytes >= 0 && freeBytes < diskSpaceHardStopBytes)
    {
        stopRecording();
        if (onAutoStopped)
            onAutoStopped("Disk space is critically low - recording stopped automatically to avoid losing data.");
    }
}
