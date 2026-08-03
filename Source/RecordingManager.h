#pragma once
#include <array>
#include <atomic>
#include <cstdint>
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

    // Enumerates every MIDI Take ever recorded for a channel (see Increment
    // A/RecordingTrack's MidiCapture): scans every take-folder under
    // recordingsFolder for "Channel <N>*.mid" files (the "*" covers the
    // rare same-second-collision suffix uniqueTakeFile() adds), newest-first
    // by take-folder name - which is already a sortable timestamp, see
    // startRecording()'s "%Y-%m-%d_%H-%M-%S" folder naming. Message thread
    // only (does real file I/O); used by ChannelComponent to populate the
    // "Recorded Takes" section of its MIDI Input Selector.
    juce::Array<juce::File> findChannelMidiTakes(int channelIndex) const;

    // MIDI Take identifiers (Increment B): a channel's MIDI Input Selector
    // reuses ChannelProcessor's existing midiDeviceIdentifier string field
    // to also reference a Take file, distinguished by this prefix - avoids
    // adding a whole parallel "input source" concept for a single new case.
    // The path is stored *relative* to recordingsFolder (not absolute) so a
    // saved session's Take selection survives the Mac/Windows sync workflow
    // this project uses (see CLAUDE.md) - an absolute path baked in would
    // resolve to nothing on the other machine.
    static constexpr const char* takeIdentifierPrefix = "take:";
    static bool isTakeIdentifier(const juce::String& identifier) { return identifier.startsWith(takeIdentifierPrefix); }
    juce::String encodeTakeIdentifier(const juce::File& takeFile) const;
    // Returns a null File() if identifier isn't a take: identifier - does
    // NOT check the file actually exists (callers already have their own
    // "missing" warning treatment, same as a disconnected live device).
    juce::File decodeTakeIdentifier(const juce::String& identifier) const;

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
    // bpm is snapshotted into the channel's MIDI Take capture (see
    // MidiCapture below) if armed while already recording - K-Player has a
    // single session-wide tempo (see MainComponent::currentTempo), not a
    // per-channel one, and mid-take tempo changes aren't a supported
    // scenario (see docs/kplayer-take-recording-playback-spec.md section
    // 10), so one snapshot at arm time is enough to convert this take's
    // captured sample offsets to MIDI ticks.
    void setChannelArmed(int channelIndex, bool armed, double bpm = 120.0);
    bool isChannelArmed(int channelIndex) const;
    void setMasterArmed(bool armed);
    bool isMasterArmed() const { return masterArmed; }

    // ---- Transport (message thread only) ----
    // Returns an empty string on success, or a user-facing reason on
    // failure (no folder set, nothing armed, folder not writable, etc.) -
    // callers show this directly rather than a generic failure message.
    // bpm: see setChannelArmed() above - same single-session-tempo snapshot,
    // used for every channel track's MIDI Take capture started here.
    juce::String startRecording(double sampleRate, int numChannelChannels, int numMasterChannels, double bpm = 120.0);
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

    // MIDI Take capture (see docs/kplayer-take-recording-playback-spec.md):
    // called with the same raw per-channel MIDI buffer MainComponent is
    // about to hand to ChannelProcessor::processBlock - must be called
    // *before* that, since a loaded plugin is free to mutate the buffer
    // it's given. Cheap no-op when not recording or the channel isn't
    // armed, same as writeChannelBlock above. Only captures channel
    // voice/mode messages (note on/off, CC, pitch bend, aftertouch, program
    // change - anything <= 3 raw bytes); SysEx and other larger messages
    // are silently dropped, which keeps every captured event a fixed-size,
    // allocation-free struct so this can push into a lock-free ring buffer
    // straight from the audio thread (see MidiCapture below). A full ring
    // (audio thread producing faster than pollMidiCapture() can drain) also
    // just silently drops events rather than blocking - same "don't
    // interrupt an in-progress take over a non-fatal capture hiccup"
    // philosophy as a failed track-creation elsewhere in this class.
    void writeChannelMidiBlock(int channelIndex, const juce::MidiBuffer& midi);

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

    // ---- Message thread, polled (e.g. from the same Timer as pollForAutoStop) ----
    // Drains every armed channel's lock-free MIDI ring buffer (see
    // writeChannelMidiBlock/MidiCapture) into its MidiMessageSequence.
    // Doesn't write anything to disk itself - that only happens once a
    // track is finalized (see drainAndFinalizeMidi()) - this just keeps the
    // ring buffer from filling up between then.
    void pollMidiCapture();

private:
    // One captured MIDI event, sized to fit any channel voice/mode message
    // (note on/off, CC, pitch bend, aftertouch, program change - see
    // writeChannelMidiBlock's comment for why SysEx isn't supported here).
    // Deliberately POD/fixed-size so a std::array of these can back a
    // lock-free juce::AbstractFifo with zero allocation on the audio
    // thread, unlike juce::MidiMessage which isn't guaranteed allocation-
    // free to copy.
    struct CapturedMidiEvent
    {
        juce::int64 takeElapsedSamples = 0; // absolute offset from take start - same basis as samplesRecorded
        uint8_t data[3] { 0, 0, 0 };
        uint8_t numBytes = 0;
    };

    // Per-channel-track MIDI Take capture state (channel tracks only -
    // master has no MIDI input concept, see spec section 4). The ring
    // buffer/fifo pair is the audio-thread-to-message-thread handoff;
    // `sequence` is message-thread-only, built up by pollMidiCapture() over
    // the life of the take and turned into a Standard MIDI File only once
    // the track is finalized (channel unarmed, or the whole take stops).
    struct MidiCapture
    {
        static constexpr int ringCapacity = 4096;
        juce::AbstractFifo fifo { ringCapacity };
        std::array<CapturedMidiEvent, ringCapacity> ring;
        juce::MidiMessageSequence sequence;
        double captureBpm = 120.0;
        juce::File targetFile;
    };

    struct RecordingTrack
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::unique_ptr<MidiCapture> midiCapture;
    };

    std::unique_ptr<RecordingTrack> createTrack(const juce::File& file, double sampleRate, int numChannels);

    // Same as createTrack() above, but also attaches a MidiCapture ready to
    // receive this channel's raw MIDI Take - used by the channel-track
    // creation call sites only (startRecording, setChannelArmed); master
    // tracks always use plain createTrack() and stay midiCapture == nullptr.
    std::unique_ptr<RecordingTrack> createChannelTrack(const juce::File& audioFile, const juce::File& midiFile,
                                                       double sampleRate, int numChannels, double bpm);

    // Returns folder/baseName.ext, or folder/"baseName (2).ext", (3), etc.
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
    static juce::File uniqueTakeFile(const juce::File& folder, const juce::String& baseName,
                                     const juce::String& extension = "wav");

    // Drains a single track's MIDI ring buffer into its sequence - shared
    // by pollMidiCapture() (periodic, keeps the ring from filling) and
    // drainAndFinalizeMidi() (one last catch-up drain right before writing
    // the file). No-op if the track has no MidiCapture.
    static void drainMidiCapture(RecordingTrack& track, double recordingSampleRate);

    // Final drain + write-to-disk step for a channel track's MIDI Take,
    // called from retireTrack()/teardownAllTracks() right before the track
    // is destroyed - by that point the audio thread is guaranteed to no
    // longer be writing to it (see those callers' own drain-margin
    // comments), so this is the one safe moment to read `sequence` and
    // finalize it into a Standard MIDI File. Writes nothing if the channel
    // was armed but received no MIDI - an empty .mid next to a real .wav
    // would just be clutter, not a useful artifact.
    static void drainAndFinalizeMidi(RecordingTrack& track, double recordingSampleRate);

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
    // would just multiply the same wait needlessly. recordingSampleRate is
    // passed in (rather than read directly, since this is static) purely to
    // convert this track's captured MIDI event offsets to ticks in
    // drainAndFinalizeMidi() below - a no-op for master tracks, which never
    // have a MidiCapture.
    static void retireTrack(std::unique_ptr<RecordingTrack>& storage,
                            std::atomic<RecordingTrack*>& published,
                            double recordingSampleRate);

    void teardownAllTracks();

    static constexpr int ticksPerQuarterNote = 960;
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
