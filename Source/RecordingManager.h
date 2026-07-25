#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Multitrack recording engine: writes each armed channel's post-processing
// stereo signal (see ChannelProcessor::processBlock) and/or the master bus
// to its own WAV file for the duration of one "take", started/stopped
// together via a single global transport rather than per-source (so every
// file in a take stays sample-aligned for later reassembly in a DAW).
// Arming/unarming a source *while a take is already running* takes effect
// immediately - see setChannelArmed()'s comment.
//
// Deliberately owns no reference to ChannelProcessor/MasterChainProcessor -
// MainComponent's audio callback already has direct access to each
// channel's processed buffer and the final master buffer each block, so
// this just gets handed those directly, keeping the engine/UI classes
// untouched by recording concerns entirely.
//
// Threading: writeChannelBlock/writeMasterBlock/noteBlockProcessed are
// called from the audio thread only, and only ever touch the lock-free
// ThreadedWriter FIFOs (safe for that) plus relaxed atomics - never file
// I/O directly. Every other method (message thread only) does the actual
// file creation/finalization; see publishTrack()/retireTrack() for how a
// single track can be safely added/removed while other tracks keep
// recording, and stopRecording() for the whole-take equivalent.
class RecordingManager
{
public:
    // Mirrors MainComponent::maxChannels - a fixed upper bound lets the
    // audio-thread-visible track slots below live in a plain std::array
    // (std::atomic isn't movable/copyable, so a std::vector<std::atomic<...>>
    // that needs to grow isn't an option) sized once and never reallocated,
    // which is what makes live-arming a channel *while already recording*
    // safe without any risk around resizing out from under the audio thread.
    static constexpr int maxTracks = 24;

    RecordingManager();
    ~RecordingManager();

    // ---- Configuration (session-persisted by the owner) ----
    void setRecordingsFolder(const juce::File& folder) { recordingsFolder = folder; }
    juce::File getRecordingsFolder() const { return recordingsFolder; }

    void setSilenceTimeoutSeconds(double seconds) { silenceTimeoutSeconds = juce::jmax(1.0, seconds); }
    double getSilenceTimeoutSeconds() const { return silenceTimeoutSeconds; }

    // ---- Arm state (message thread only) ----
    // Mirrors MainComponent's channel vector: only ever grows/shrinks from
    // the tail, called from the same place channel count changes. Retires
    // (finalizes) any track beyond the new count that was still recording -
    // a defensive edge case, since shrinking the channel count mid-take
    // isn't really a supported workflow.
    void setChannelCount(int count);

    // Arming/unarming while a take is already in progress takes effect
    // right away: arming starts a new file for that source, silence-padded
    // at the front to match how far the take has already progressed, so
    // every file in the take - whichever moment its source was armed -
    // still starts at sample 0 and lines up with the rest when dropped into
    // another DAW with no manual alignment needed. Unarming finalizes and
    // closes its file. Both are safe to call at any time, recording or not.
    void setChannelArmed(int channelIndex, bool armed);
    bool isChannelArmed(int channelIndex) const;
    void setMasterArmed(bool armed);
    bool isMasterArmed() const { return masterArmed; }

    // ---- Transport (message thread only) ----
    // Returns an empty string on success, or a user-facing reason on
    // failure (no folder set, nothing armed, folder not writable, etc.) -
    // callers show this directly rather than a generic failure message.
    juce::String startRecording(double sampleRate, int numChannelChannels, int numMasterChannels);
    void stopRecording();
    bool isRecording() const { return recording.load(std::memory_order_acquire); }
    double getRecordingElapsedSeconds() const;

    // ---- Audio thread ----
    // Both are cheap no-ops when not recording or the given source isn't
    // armed/didn't get a track created - safe to call unconditionally every
    // block for every channel, matching how MainComponent already touches
    // every channel unconditionally each callback.
    void writeChannelBlock(int channelIndex, const juce::AudioBuffer<float>& buffer);
    void writeMasterBlock(const juce::AudioBuffer<float>& buffer);

    // Call exactly once per audio callback, after every writeChannelBlock/
    // writeMasterBlock call for that callback has already happened - folds
    // this block's peak (across whichever sources were just written) into
    // the whole-take silence watchdog and advances the elapsed-time counter.
    // Sample rate isn't a parameter here - recordingSampleRate (fixed for
    // the duration of a take, published via the same acquire/release on
    // `recording` that guards the track pointers) is what samples-to-seconds
    // conversion uses, done lazily in pollForAutoStop rather than per block.
    void noteBlockProcessed(int numSamples);

    // ---- Message thread, polled (e.g. from an existing UI Timer) ----
    // Auto-stops (and reports why via onAutoStopped) once every currently-
    // recording source has been silent for getSilenceTimeoutSeconds(), or
    // if free disk space on the recordings volume drops below a safety
    // margin - the latter is the actual protection against the "left it
    // recording overnight" disk-space scenario silence alone can't catch
    // (e.g. a held drone/pad tone that's technically never silent).
    std::function<void(juce::String reason)> onAutoStopped;
    void pollForAutoStop();

private:
    struct RecordingTrack
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    };

    std::unique_ptr<RecordingTrack> createTrack(const juce::File& file, double sampleRate, int numChannels);

    // Returns folder/baseName.wav, or folder/"baseName (2).wav", (3), etc.
    // if that's already taken - never returns a path that already exists.
    // Needed because juce::File's underlying FileOutputStream opens an
    // existing file O_RDWR and appends at the end rather than truncating,
    // so reusing a path a previous segment already finalized would corrupt
    // it (a stray RIFF/fmt/data header stamped mid-file) instead of losing
    // data cleanly. This can happen two ways: re-arming a channel/master
    // mid-take (a second segment for the same source in the same take
    // folder), or two takes starting within the same wall-clock second
    // (currentTakeFolder's name only has 1-second resolution, and
    // juce::File::createDirectory() treats an already-existing directory as
    // success, so the second take would otherwise silently reuse the first
    // take's folder and filenames too).
    static juce::File uniqueTakeFile(const juce::File& folder, const juce::String& baseName);

    // Writes numSamples of silence into a freshly-created (not yet
    // published/visible to the audio thread) track, so it starts at the
    // same sample-0 as every other file in the take - see setChannelArmed()/
    // setMasterArmed() above. Retries rather than dropping samples if
    // ThreadedWriter's FIFO is momentarily full (see its own write() docs) -
    // silently losing part of the padding would leave this file short and
    // defeat the point of padding it at all. Safe to block briefly here:
    // this only ever runs on the message thread in response to a user
    // clicking an arm button, and the background thread drains far faster
    // than realtime (it's bounded by disk I/O, not playback speed), so even
    // many minutes of padding resolves in a small fraction of a second.
    static void padTrackWithSilence(RecordingTrack& track, int numChannels, juce::int64 numSamples);

    // Pads a freshly-created (not yet published) track up to the *live*
    // elapsed-sample count, in a loop rather than one snapshot-then-pad
    // call: creating the file and writing the padding itself both take real
    // wall-clock time, during which the take keeps advancing on the audio
    // thread - a single snapshot would always be stale by however long that
    // took, which is exactly what caused a live-armed track to drift out of
    // alignment with the rest of the take. Each pass re-reads the live
    // counter and pads any remaining gap; bounded iteration count since the
    // counter never stops advancing, but each pass converges fast (writing
    // to the FIFO is far quicker than realtime), so the loop settles after
    // a handful of passes with at most a few samples of residual gap.
    void catchUpTrackToLive(RecordingTrack& track, int numChannels);

    // Publishes a freshly-created track into a (storage, published) slot
    // pair - safe regardless of whether the audio thread might concurrently
    // be reading `published` in writeChannelBlock()/writeMasterBlock():
    // `storage` is fully constructed *before* the release-store into
    // `published`, so the audio thread can never observe a non-null pointer
    // to a half-built track.
    static void publishTrack(std::unique_ptr<RecordingTrack>& storage,
                             std::atomic<RecordingTrack*>& published,
                             std::unique_ptr<RecordingTrack> track);

    // Retires whatever's in a (storage, published) slot pair *while other
    // tracks may still be actively recording*: clears the published pointer
    // first (the audio thread stops touching it on its next read), sleeps
    // the same drain-margin used elsewhere in this codebase (see
    // ChannelProcessor::loadPlugin) so any in-flight callback that already
    // loaded the old pointer finishes its write, then destroys the track
    // (finalizing its file). Safe to call on an already-empty slot. Not
    // used by the whole-take stop path below - one global drain there
    // already covers every track at once, so retiring them individually
    // would just multiply the same wait needlessly.
    static void retireTrack(std::unique_ptr<RecordingTrack>& storage,
                            std::atomic<RecordingTrack*>& published);

    void teardownAllTracks();

    static constexpr float silenceThresholdLinear   = 0.001f;               // ~ -60dBFS
    static constexpr juce::int64 diskSpaceHardStopBytes = 200 * 1024 * 1024; // 200MB

    juce::TimeSliceThread backgroundThread { "KPlayer Recording" };

    juce::File recordingsFolder;
    double silenceTimeoutSeconds = 60.0;

    std::vector<bool> channelArmed;
    bool masterArmed = false;

    // Message-thread-owned lifetime; the audio thread only ever touches the
    // corresponding *Published members below, never these directly.
    std::array<std::unique_ptr<RecordingTrack>, maxTracks> channelTrackStorage;
    std::array<std::atomic<RecordingTrack*>, maxTracks> channelTrackPublished;
    std::unique_ptr<RecordingTrack> masterTrackStorage;
    std::atomic<RecordingTrack*> masterTrackPublished { nullptr };

    // Remembered from startRecording() so a channel/master armed mid-take
    // can create a track matching the rest of the take (same folder, same
    // sample rate/channel count).
    juce::File currentTakeFolder;
    int recordingNumChannelChannels = 2;
    int recordingNumMasterChannels  = 2;

    std::atomic<bool> recording { false };

    // Audio-thread-only (never touched from the message thread while
    // recording is true) - accumulated across this callback's
    // writeChannelBlock/writeMasterBlock calls, consumed and reset by
    // noteBlockProcessed at the end of the same callback.
    bool blockHadSignalThisCallback = false;

    std::atomic<juce::int64> silentSampleRunLength { 0 };
    std::atomic<juce::int64> samplesRecorded        { 0 };
    double recordingSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingManager)
};
