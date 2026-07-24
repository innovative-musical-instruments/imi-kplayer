#pragma once
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Derives a live BPM from an external device's MIDI Clock stream (0xF8,
// 24 pulses per quarter note) by averaging a full quarter note's worth of
// inter-pulse intervals each time - smooths out MIDI jitter without a
// separate "lock" state, at the cost of up to one quarter note of latency
// before first reporting a tempo (and again after every Start/Continue,
// which resets the average so a tempo change at a transport boundary
// doesn't get blended with the previous section's pulses).
//
// pushMessage() is only ever called from the single MIDI callback thread
// that owns this detector's already-device-filtered messages (never the
// audio thread). getBpm()/hasLockedTempo()/hasSignal() are polled from the
// message thread (a UI Timer) to drive the live tempo display and the "no
// signal" warning - that single-writer/many-reader split is why the shared
// state below is plain relaxed atomics rather than needing a lock.
class MidiClockTempoDetector
{
public:
    static constexpr int pulsesPerQuarterNote = 24;

    // Comfortably above the per-pulse interval at any musically plausible
    // tempo (20-300bpm -> roughly 8ms-100ms per pulse) - anything slower
    // than this means the source stopped sending clock, not "it's just a
    // slow tempo".
    static constexpr double signalTimeoutSeconds = 1.0;

    void pushMessage(const juce::MidiMessage& msg)
    {
        if (msg.isMidiStart() || msg.isMidiContinue())
        {
            pendingIntervalCount = 0;
            pendingIntervalSum   = 0.0;
            havePreviousPulse    = false;
            return;
        }

        if (! msg.isMidiClock())
            return;

        auto now = msg.getTimeStamp();

        if (havePreviousPulse)
        {
            auto interval = now - previousPulseTime;
            if (interval > 0.0)
            {
                pendingIntervalSum += interval;
                if (++pendingIntervalCount >= pulsesPerQuarterNote)
                {
                    auto averageInterval = pendingIntervalSum / (double) pendingIntervalCount;
                    bpm.store(60.0 / (averageInterval * pulsesPerQuarterNote), std::memory_order_relaxed);
                    locked.store(true, std::memory_order_relaxed);
                    pendingIntervalSum   = 0.0;
                    pendingIntervalCount = 0;
                }
            }
        }

        previousPulseTime = now;
        havePreviousPulse  = true;
        lastPulseTimestamp.store(now, std::memory_order_relaxed);
    }

    // Discards any in-progress averaging and the "locked" state - used when
    // the sync source device changes, so a stray interval spanning two
    // different devices' pulse streams can't get baked into the average.
    // Deliberately leaves the last-computed bpm alone: harmless, since
    // hasLockedTempo() will read false until pulses from the new source
    // re-lock it.
    void reset()
    {
        pendingIntervalCount = 0;
        pendingIntervalSum   = 0.0;
        havePreviousPulse    = false;
        locked.store(false, std::memory_order_relaxed);
        lastPulseTimestamp.store(0.0, std::memory_order_relaxed);
    }

    // Polled from the message thread. False once the most recent pulse is
    // older than signalTimeoutSeconds (or none has ever arrived) - callers
    // should hold their last-known BPM display/applied tempo rather than
    // reset or revert it.
    bool hasSignal() const
    {
        auto last = lastPulseTimestamp.load(std::memory_order_relaxed);
        return last > 0.0 && (juce::Time::getMillisecondCounterHiRes() * 0.001 - last) < signalTimeoutSeconds;
    }

    // Only meaningful once a full quarter note of pulses has been averaged
    // since construction, the last reset(), or the last Start/Continue.
    bool hasLockedTempo() const { return locked.load(std::memory_order_relaxed); }
    double getBpm() const       { return bpm.load(std::memory_order_relaxed); }

private:
    // Only ever touched from the single MIDI callback thread that calls
    // pushMessage() - deliberately not atomic.
    int    pendingIntervalCount = 0;
    double pendingIntervalSum   = 0.0;
    double previousPulseTime    = 0.0;
    bool   havePreviousPulse    = false;

    std::atomic<double> bpm                { 120.0 };
    std::atomic<bool>   locked             { false };
    std::atomic<double> lastPulseTimestamp { 0.0 };
};
