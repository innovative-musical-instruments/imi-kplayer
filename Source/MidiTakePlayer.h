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

    // Message thread. Reads the .mid file, converts every event's tick
    // timestamp to a *sample* timestamp up front using the fixed
    // ticksPerQuarterNote (matching RecordingManager's capture-side
    // constant) and the given bpm - K-Player has one session-wide tempo and
    // deliberately doesn't handle capture/playback tempo drift (spec
    // section 10), so any tempo meta-events embedded in the file are
    // ignored; playback always uses the current session tempo. Returns
    // false (and leaves the player unloaded) on a read failure - a fresh
    // selection that fails shouldn't keep silently playing whatever was
    // selected before.
    bool loadTake(const juce::File& midiFile, double sampleRate, double bpm);

    // Message thread. Clears the player back to "nothing selected" - used
    // when the channel's input selector moves away from a Take. Same
    // publish-then-drain-then-destroy pattern as loadTake()'s swap.
    void unload();

    static constexpr int ticksPerQuarterNote = 960;

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
                     bool transportIsPlaying, juce::MidiBuffer& outputMidi);

private:
    struct LoadedTake
    {
        juce::MidiMessageSequence sequence; // timestamps already converted to samples
    };

    // Message-thread-owned lifetime; the audio thread only ever touches
    // `published`, never this directly - see publish() below.
    std::unique_ptr<LoadedTake> storage;
    std::atomic<LoadedTake*> published { nullptr };

    void publish(std::unique_ptr<LoadedTake> newTake);

    // Audio-thread-only, persists across renderBlock() calls - see the
    // discontinuity-detection comment above.
    const LoadedTake* lastRenderedTake = nullptr;
    juce::int64 lastWindowEnd = -1;
    bool wasPlayingLastBlock = false;
    std::array<bool, 16 * 128> activeNotes {};

    void flushActiveNotes(juce::MidiBuffer& outputMidi, int atSampleOffset);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTakePlayer)
};
