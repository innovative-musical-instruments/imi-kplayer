#pragma once
#include <algorithm>
#include <atomic>
#include <juce_core/juce_core.h>

// Minimal session-wide transport for MIDI Take playback (see
// docs/kplayer-take-recording-playback-spec.md, Increment B): Play/Pause +
// RTZ (return-to-zero), fully independent of RecordingManager's own
// start/stop - you can audition a Take through a new instrument with Play
// alone, or run both together to record the reamped result.
//
// Also owns the Range: the [start, end) span of the session the transport
// plays, with LOOP deciding whether hitting the end wraps back to the start
// or stops there. A range is always present whenever any channel has a Take
// selected (MainComponent::updateTransportRange keeps it in step with what's
// selected, defaulting to the whole of the longest selected Take), which is
// what stops the playhead advancing forever into silence past the end of the
// material. Range bounds arrive here already quantized to whole seconds by
// MainComponent - the wrap itself is deliberately only block-granular (up to
// one buffer of overshoot past the end before it wraps), which is well below
// the resolution the user can set in the first place, so there's no need to
// split a render block at the range end.
//
// Threading: same accepted-tradeoff plain-atomics shape as RecordingManager's
// `recording`/`samplesRecorded` - play()/pause()/rtz()/setRange() and friends
// are message-thread (button clicks), advanceAndGetBlockStartPosition() is
// audio-thread, called exactly once per callback (not per channel) so every
// channel's MidiTakePlayer renders against the same block-start position this
// callback. The one piece of audio->message traffic is stoppedAtRangeEnd,
// consumed by MainComponent's timer to resync the Play button after the
// audio thread has paused the transport on its own - the same
// "lightweight atomic flag, message thread does the real work" pattern
// ChannelProcessor's MIDI-driven flags use. Position/playing state is not
// session-persisted - always starts paused at position 0 on load, same as
// recording state already does.
class SessionTransport
{
public:
    void play()
    {
        // Parked at the end of the range, Play means "go round again"
        // rather than "sit here doing nothing" - the natural reading of
        // pressing Play on a stopped-at-the-end transport. Skipped while
        // the range is suspended (i.e. recording): a record pass runs
        // linearly past the end and must never have its playhead teleported
        // by a pause/resume mid-take.
        if (wrapActive() && positionSamples.load(std::memory_order_relaxed) >= rangeEndSamples.load(std::memory_order_relaxed))
            positionSamples.store(rangeStartSamples.load(std::memory_order_relaxed), std::memory_order_relaxed);

        playing.store(true, std::memory_order_relaxed);
    }

    void pause() { playing.store(false, std::memory_order_relaxed); }

    // Return-to-zero, where "zero" means the start of the range - with no
    // range set that is literally 0, and with one it's the point the user
    // actually wants to keep going back to. Deliberately still honoured
    // while recording (unlike play()'s rewind above), since RTZ is an
    // explicit "jump the playhead" request rather than a side effect.
    void rtz() { positionSamples.store(hasRange() ? rangeStartSamples.load(std::memory_order_relaxed) : 0, std::memory_order_relaxed); }

    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }

    // For UI display only (e.g. a transport time readout) - samples, not
    // seconds, since this class has no sample-rate concept of its own; the
    // caller converts using whatever rate is currently live.
    juce::int64 getPositionSamples() const { return positionSamples.load(std::memory_order_relaxed); }

    // Message thread. An empty or inverted span (end <= start) is stored as
    // "no range", so callers don't have to special-case "nothing selected"
    // themselves - see hasRange().
    void setRange(juce::int64 startSamples, juce::int64 endSamples)
    {
        auto start = std::max<juce::int64>(0, startSamples);
        rangeStartSamples.store(start, std::memory_order_relaxed);
        rangeEndSamples.store(std::max(start, endSamples), std::memory_order_relaxed);
    }

    void clearRange() { setRange(0, 0); }

    juce::int64 getRangeStartSamples() const { return rangeStartSamples.load(std::memory_order_relaxed); }
    juce::int64 getRangeEndSamples()   const { return rangeEndSamples.load(std::memory_order_relaxed); }

    // Wrap at the range end rather than stopping there.
    void setLoopEnabled(bool shouldLoop) { loopEnabled.store(shouldLoop, std::memory_order_relaxed); }
    bool isLoopEnabled() const { return loopEnabled.load(std::memory_order_relaxed); }

    // Recording ignores the range entirely - it captures linearly from
    // wherever the playhead is, straight past the range end - so recording
    // suspends the wrap rather than clearing the range, which would lose
    // the user's bounds and blank the readout for the duration of the take.
    void setRangeSuspended(bool suspended) { rangeSuspended.store(suspended, std::memory_order_relaxed); }

    bool hasRange() const
    {
        return rangeEndSamples.load(std::memory_order_relaxed) > rangeStartSamples.load(std::memory_order_relaxed);
    }

    // Message thread. True once per time the audio thread stopped the
    // transport itself on reaching the range end with LOOP off; the caller
    // uses it to bring the Play button back in step with a transport that
    // paused without anyone clicking anything.
    bool consumeStoppedAtRangeEnd() { return stoppedAtRangeEnd.exchange(false, std::memory_order_relaxed); }

    // Audio thread, once per callback: returns the position at the *start*
    // of this block (what MidiTakePlayer::renderBlock should render
    // against), then advances the stored position by numSamples if
    // currently playing - wrapping back to the range start, or stopping at
    // the range end, if this block would take it past that end.
    juce::int64 advanceAndGetBlockStartPosition(int numSamples)
    {
        auto blockStart = positionSamples.load(std::memory_order_relaxed);
        if (! playing.load(std::memory_order_relaxed))
            return blockStart;

        auto next = blockStart + numSamples;

        if (wrapActive())
        {
            auto rangeEnd = rangeEndSamples.load(std::memory_order_relaxed);
            if (next >= rangeEnd)
            {
                if (loopEnabled.load(std::memory_order_relaxed))
                {
                    next = rangeStartSamples.load(std::memory_order_relaxed);
                }
                else
                {
                    // Park exactly on the end rather than wherever this
                    // block happened to land, so the readout agrees with
                    // the range the user set, and so play() above can
                    // recognise "parked at the end" reliably.
                    next = rangeEnd;
                    playing.store(false, std::memory_order_relaxed);
                    stoppedAtRangeEnd.store(true, std::memory_order_relaxed);
                }
            }
        }

        positionSamples.store(next, std::memory_order_relaxed);
        return blockStart;
    }

private:
    // Callable from either thread - only ever reads the atomics above.
    bool wrapActive() const { return hasRange() && ! rangeSuspended.load(std::memory_order_relaxed); }

    std::atomic<bool> playing { false };
    std::atomic<juce::int64> positionSamples { 0 };

    std::atomic<juce::int64> rangeStartSamples { 0 };
    std::atomic<juce::int64> rangeEndSamples   { 0 };
    std::atomic<bool> loopEnabled      { false };
    std::atomic<bool> rangeSuspended   { false };
    std::atomic<bool> stoppedAtRangeEnd { false };
};
