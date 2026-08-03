#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

// Minimal session-wide transport for MIDI Take playback (see
// docs/kplayer-take-recording-playback-spec.md, Increment B): Play/Pause +
// RTZ (return-to-zero), fully independent of RecordingManager's own
// start/stop - you can audition a Take through a new instrument with Play
// alone, or run both together to record the reamped result.
//
// Threading: same accepted-tradeoff plain-atomics shape as RecordingManager's
// `recording`/`samplesRecorded` - play()/pause()/rtz() are message-thread
// (button clicks), advanceAndGetBlockStartPosition() is audio-thread, called
// exactly once per callback (not per channel) so every channel's
// MidiTakePlayer renders against the same block-start position this
// callback. Not session-persisted - always starts paused at position 0 on
// load, same as recording state already does.
class SessionTransport
{
public:
    void play()  { playing.store(true, std::memory_order_relaxed); }
    void pause() { playing.store(false, std::memory_order_relaxed); }
    void rtz()   { positionSamples.store(0, std::memory_order_relaxed); }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }

    // Audio thread, once per callback: returns the position at the *start*
    // of this block (what MidiTakePlayer::renderBlock should render
    // against), then advances the stored position by numSamples if
    // currently playing.
    juce::int64 advanceAndGetBlockStartPosition(int numSamples)
    {
        auto blockStart = positionSamples.load(std::memory_order_relaxed);
        if (playing.load(std::memory_order_relaxed))
            positionSamples.store(blockStart + numSamples, std::memory_order_relaxed);
        return blockStart;
    }

private:
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> positionSamples { 0 };
};
