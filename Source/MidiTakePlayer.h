#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Plays a single channel's recorded MIDI Take (see
// docs/kplayer-take-recording-playback-spec.md, Increment B) back into that
// channel's instrument, driven by the shared SessionTransport. One instance
// per channel, owned by MainComponent in a vector parallel to
// channelProcessors/channelComponents (see MainComponent::addChannel/
// setChannelCount, which already fully detaches the audio callback before
// resizing those - this piggybacks on that same safe-resize window, so
// unlike RecordingManager this doesn't need a fixed-size lock-free array).
//
// Threading: loadTake()/unload() are message-thread only, and publish a
// freshly-built LoadedTake via an atomic pointer swap - same
// storage/published-pointer shape as RecordingManager's
// publishTrack()/retireTrack(), old content destroyed only after a
// drain-margin sleep so the audio thread can never observe a half-built or
// freed take. renderBlock() is audio-thread only.
class MidiTakePlayer
{
public:
    // JUCE_DECLARE_NON_COPYABLE below counts as a user-declared constructor
    // (the deleted copy ctor), which suppresses the implicit default one -
    // needs restating explicitly, same as every other class in this codebase
    // using that macro (e.g. RecordingManager, ChannelProcessor).
    MidiTakePlayer() = default;

    // Message thread. Reads the .mid file and keeps every event at its
    // original *tick* timestamp - deliberately no conversion to samples
    // here, because that conversion depends on the tempo and the tempo can
    // change at any moment (a manual edit, or MIDI-clock sync moving it
    // continuously). Converting up front is what used to freeze a take at
    // whatever tempo happened to be current when it was selected, so that
    // changing the tempo afterwards did nothing at all until the take was
    // reselected. renderBlock() now does the conversion per block instead.
    //
    // K-Player still has one session-wide tempo and still ignores any tempo
    // meta-event embedded in the file (spec section 10) - playback always
    // follows the current session tempo. The meta-event RecordingManager
    // now writes is there to record what a take was played at, and so the
    // file reads correctly in other software; it deliberately doesn't drive
    // playback here.
    //
    // Returns false (and leaves the player unloaded) on a read failure - a
    // fresh selection that fails shouldn't keep silently playing whatever
    // was selected before.
    bool loadTake(const juce::File& midiFile);

    // Message thread. Clears the player back to "nothing selected" - used
    // when the channel's input selector moves away from a Take. Same
    // publish-then-drain-then-destroy pattern as loadTake()'s swap.
    void unload();

    static constexpr int ticksPerQuarterNote = 960;

    // Message thread only. Length of the currently loaded Take in *ticks*
    // (0 when nothing is loaded) - how long that is in samples depends on
    // the current tempo, so the caller converts with samplesPerTick() below
    // (MainComponent::updateTransportRange, which needs the longest
    // selected Take to size the transport's default Range, and re-measures
    // whenever the tempo moves). Deliberately a plain member rather than an
    // atomic: only ever written by loadTake()/unload() and read by the same
    // message thread, never by renderBlock().
    juce::int64 getLengthTicks() const { return lengthTicks; }

    // How many samples one tick lasts at a given tempo - the single
    // conversion this whole class turns on, shared with callers that need
    // to reason about a take's length in wall-clock terms.
    static double samplesPerTick(double bpm, double sampleRate)
    {
        return sampleRate * 60.0 / (juce::jmax(1.0, bpm) * (double) ticksPerQuarterNote);
    }

    // Audio thread. transportPositionSamples/numSamples describe this
    // block's window on the shared SessionTransport (see
    // SessionTransport::advanceAndGetBlockStartPosition, called once per
    // callback by MainComponent - every channel's player renders against
    // the same block-start position). transportIsPlaying gates whether new
    // events are emitted; while paused nothing new plays. Appends events
    // (relative sample offsets 0..numSamples-1) into outputMidi - does not
    // clear it first, so this can be called into a buffer a live-device
    // path might otherwise have populated (in practice a channel is either
    // live-sourced or Take-sourced, never both, but this keeps the method's
    // contract simple).
    //
    // Each block's events are found fresh via a binary search
    // (MidiMessageSequence::getNextIndexAtTime) rather than an incrementally
    // advanced cursor, so there's no persistent scan position to invalidate -
    // RTZ, pausing/resuming, or selecting a Take mid-transport-run are all
    // just "a different window this time", handled uniformly. What *is*
    // tracked across calls (audio-thread-only) is which notes this player
    // left sounding, purely to flush them cleanly (avoid stuck notes) on any
    // discontinuity: the published take changing (a different Take just
    // selected, or unload() called), the window not picking up exactly where
    // the last one left off (RTZ or any other jump), or a playing->paused
    // edge (pause mid-note).
    void renderBlock(juce::int64 transportPositionSamples, int numSamples,
                     bool transportIsPlaying, double bpm, double sampleRate,
                     juce::MidiBuffer& outputMidi);

private:
    struct LoadedTake
    {
        juce::MidiMessageSequence sequence; // timestamps in ticks, as they came out of the file
    };

    // Message-thread-owned lifetime; the audio thread only ever touches
    // `published`, never this directly - see publish() below.
    std::unique_ptr<LoadedTake> storage;
    std::atomic<LoadedTake*> published { nullptr };

    // See getLengthTicks() - message-thread-only, kept in step with what
    // publish() last published.
    juce::int64 lengthTicks = 0;

    void publish(std::unique_ptr<LoadedTake> newTake);

    // Audio-thread-only, persists across renderBlock() calls - see the
    // discontinuity-detection comment above.
    const LoadedTake* lastRenderedTake = nullptr;
    juce::int64 lastWindowEnd = -1;
    bool wasPlayingLastBlock = false;

    // Where this player has got to in the take, in ticks. Integrated block
    // by block at whatever the tempo currently is, rather than derived from
    // the transport position, which is what makes a tempo change take
    // effect from the moment it is made instead of retroactively re-timing
    // everything already played. Re-anchored to the transport position (at
    // the current tempo) on any discontinuity - see renderBlock().
    double positionTicks = 0.0;
    std::array<bool, 16 * 128> activeNotes {};

    void flushActiveNotes(juce::MidiBuffer& outputMidi, int atSampleOffset);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTakePlayer)
};
