#include "RecordingManager.h"
#include <cstring>

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

juce::Array<juce::File> RecordingManager::findChannelTakeFiles(int channelIndex, const juce::String& extension) const
{
    juce::Array<juce::File> result;
    if (! recordingsFolder.isDirectory())
        return result;

    juce::String baseName = "Channel " + juce::String(channelIndex + 1);

    juce::Array<juce::File> takeFolders;
    for (const auto& entry : juce::RangedDirectoryIterator(recordingsFolder, false, "*", juce::File::findDirectories))
        takeFolders.add(entry.getFile());
    takeFolders.sort(); // take-folder names are %Y-%m-%d_%H-%M-%S, so lexicographic order == chronological order

    for (int i = takeFolders.size(); --i >= 0;) // newest-first
        for (const auto& entry : juce::RangedDirectoryIterator(takeFolders.getReference(i), false,
                                                                baseName + "*." + extension, juce::File::findFiles))
            result.add(entry.getFile());

    return result;
}

juce::Array<juce::File> RecordingManager::findChannelMidiTakes(int channelIndex) const
{
    return findChannelTakeFiles(channelIndex, "mid");
}

juce::Array<juce::File> RecordingManager::findChannelAudioTakes(int channelIndex) const
{
    return findChannelTakeFiles(channelIndex, "wav");
}

juce::String RecordingManager::encodeTakeIdentifier(const juce::File& takeFile) const
{
    return juce::String(takeIdentifierPrefix) + takeFile.getRelativePathFrom(recordingsFolder);
}

juce::File RecordingManager::decodeTakeIdentifier(const juce::String& identifier) const
{
    if (! isTakeIdentifier(identifier))
        return {};
    return recordingsFolder.getChildFile(identifier.substring((int) juce::String(takeIdentifierPrefix).length()));
}

void RecordingManager::setChannelCount(int count)
{
    // Retire any track beyond the new count that was still recording -
    // shrinking the channel count mid-take isn't a supported workflow, but
    // this keeps it from leaking a dangling file handle rather than
    // crashing or corrupting anything.
    for (int i = count; i < (int) channelArmed.size() && i < maxTracks; ++i)
        retireTrack(channelTrackStorage[(size_t) i], channelTrackPublished[(size_t) i], recordingSampleRate);

    channelArmed.resize((size_t) count, false);
}

void RecordingManager::setChannelArmed(int channelIndex, bool armed, double bpm)
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
        auto audioFile = uniqueTakeFile(currentTakeFolder, "Channel " + juce::String(channelIndex + 1));
        auto midiFile  = uniqueTakeFile(currentTakeFolder, "Channel " + juce::String(channelIndex + 1), "mid");
        auto track = createChannelTrack(audioFile, midiFile, recordingSampleRate, recordingNumChannelChannels, bpm);
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
        retireTrack(channelTrackStorage[(size_t) channelIndex], channelTrackPublished[(size_t) channelIndex],
                    recordingSampleRate);
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
        auto file = uniqueTakeFile(currentTakeFolder, "Master");
        auto track = createTrack(file, recordingSampleRate, recordingNumMasterChannels);
        if (track != nullptr)
        {
            catchUpTrackToLive(*track, recordingNumMasterChannels);
            publishTrack(masterTrackStorage, masterTrackPublished, std::move(track));
        }
    }
    else
    {
        retireTrack(masterTrackStorage, masterTrackPublished, recordingSampleRate);
    }
}

juce::File RecordingManager::uniqueTakeFile(const juce::File& folder, const juce::String& baseName,
                                             const juce::String& extension)
{
    auto candidate = folder.getChildFile(baseName + "." + extension);
    for (int suffix = 2; candidate.existsAsFile(); ++suffix)
        candidate = folder.getChildFile(baseName + " (" + juce::String(suffix) + ")." + extension);
    return candidate;
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

std::unique_ptr<RecordingManager::RecordingTrack> RecordingManager::createChannelTrack(
    const juce::File& audioFile, const juce::File& midiFile, double sampleRate, int numChannels, double bpm)
{
    auto track = createTrack(audioFile, sampleRate, numChannels);
    if (track == nullptr)
        return nullptr;

    track->midiCapture = std::make_unique<MidiCapture>();
    track->midiCapture->captureBpm = bpm;
    track->midiCapture->targetFile = midiFile;
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
                                    std::atomic<RecordingTrack*>& published,
                                    double recordingSampleRate)
{
    if (storage == nullptr)
        return;
    published.store(nullptr, std::memory_order_release);
    juce::Thread::sleep(50);
    drainAndFinalizeMidi(*storage, recordingSampleRate);
    storage.reset();
}

void RecordingManager::teardownAllTracks()
{
    for (size_t i = 0; i < channelTrackStorage.size(); ++i)
    {
        channelTrackPublished[i].store(nullptr, std::memory_order_relaxed);
        if (channelTrackStorage[i] != nullptr)
            drainAndFinalizeMidi(*channelTrackStorage[i], recordingSampleRate);
        channelTrackStorage[i].reset();
    }
    masterTrackPublished.store(nullptr, std::memory_order_relaxed);
    masterTrackStorage.reset();
}

void RecordingManager::drainMidiCapture(RecordingTrack& track, double recordingSampleRate)
{
    if (track.midiCapture == nullptr)
        return;

    auto& capture = *track.midiCapture;
    int numReady = capture.fifo.getNumReady();
    if (numReady <= 0)
        return;

    int start1, size1, start2, size2;
    capture.fifo.prepareToRead(numReady, start1, size1, start2, size2);

    auto appendEvent = [&](const CapturedMidiEvent& e)
    {
        double seconds = (double) e.takeElapsedSamples / recordingSampleRate;
        double ticks = seconds * (capture.captureBpm / 60.0) * (double) ticksPerQuarterNote;
        capture.sequence.addEvent(juce::MidiMessage(e.data, (int) e.numBytes, ticks));
    };

    for (int i = 0; i < size1; ++i)
        appendEvent(capture.ring[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        appendEvent(capture.ring[(size_t) (start2 + i)]);

    capture.fifo.finishedRead(size1 + size2);
}

void RecordingManager::drainAndFinalizeMidi(RecordingTrack& track, double recordingSampleRate)
{
    if (track.midiCapture == nullptr)
        return;

    // One last catch-up drain - by the time this runs (retireTrack()'s
    // drain-margin sleep, or teardownAllTracks() after stopRecording()'s
    // own drain sleep) the audio thread is guaranteed to have stopped
    // writing to this track, so nothing more will arrive after this.
    drainMidiCapture(track, recordingSampleRate);

    auto& capture = *track.midiCapture;
    if (capture.sequence.getNumEvents() == 0)
        return; // channel was armed but nothing played - no .mid to write

    capture.sequence.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarterNote);
    midiFile.addTrack(capture.sequence);

    if (auto stream = capture.targetFile.createOutputStream())
        midiFile.writeTo(*stream);
}

void RecordingManager::pollMidiCapture()
{
    if (! isRecording())
        return;

    for (auto& storage : channelTrackStorage)
        if (storage != nullptr)
            drainMidiCapture(*storage, recordingSampleRate);
}

juce::String RecordingManager::startRecording(double sampleRate, int numChannelChannels, int numMasterChannels, double bpm)
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

        auto audioFile = uniqueTakeFile(takeFolder, "Channel " + juce::String((int) i + 1));
        auto midiFile  = uniqueTakeFile(takeFolder, "Channel " + juce::String((int) i + 1), "mid");
        auto track = createChannelTrack(audioFile, midiFile, sampleRate, numChannelChannels, bpm);
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
        auto file = uniqueTakeFile(takeFolder, "Master");
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

void RecordingManager::writeChannelMidiBlock(int channelIndex, const juce::MidiBuffer& midi)
{
    if (! recording.load(std::memory_order_acquire))
        return;
    if (channelIndex < 0 || channelIndex >= maxTracks)
        return;

    auto* track = channelTrackPublished[(size_t) channelIndex].load(std::memory_order_acquire);
    if (track == nullptr || track->midiCapture == nullptr)
        return;

    auto& capture = *track->midiCapture;
    auto takeElapsedBeforeThisBlock = samplesRecorded.load(std::memory_order_relaxed);

    for (const auto meta : midi)
    {
        auto msg = meta.getMessage();
        int numBytes = msg.getRawDataSize();
        if (numBytes <= 0 || numBytes > 3)
            continue; // SysEx/other large messages aren't captured - see writeChannelMidiBlock's header comment

        int start1, size1, start2, size2;
        capture.fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 <= 0)
            continue; // ring buffer full - drop this event rather than block/interrupt the take

        auto& slot = capture.ring[(size_t) start1];
        slot.takeElapsedSamples = takeElapsedBeforeThisBlock + meta.samplePosition;
        slot.numBytes = (uint8_t) numBytes;
        std::memcpy(slot.data, msg.getRawData(), (size_t) numBytes);
        capture.fifo.finishedWrite(1);
    }
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
