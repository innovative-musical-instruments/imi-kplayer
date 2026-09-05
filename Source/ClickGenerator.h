#pragma once
#include <atomic>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiTakePlayer.h"

// The metronome click: a short synthesized tone mixed straight into the
// output, with no plugin, no MIDI channel and no audio file behind it.
//
// Timing deliberately runs on the same tick clock MidiTakePlayer uses
// rather than being derived from the transport's sample position. A MIDI
// Take's musical position is integrated block by block at the current
// tempo and only re-anchored on a genuine jump (see
// MidiTakePlayer::renderBlock), so a click derived from the raw playhead
// instead would drift away from the Take it's supposed to be counting the
// moment the tempo moved mid-run. Sharing both the tick domain and the
// re-anchor rule keeps them locked together through tempo sweeps, Range
// loop wraps and RTZ alike.
//
// Threading: the settings are message-thread writes and audio-thread reads
// through atomics, the same shape KPlayerAudioPlayHead uses for the tempo.
// renderBlock() is audio-thread only, and everything it needs to make sound
// is computed there - no allocation, no file access.
class ClickGenerator
{
public:
    // JUCE_DECLARE_NON_COPYABLE below counts as a user-declared constructor
    // (the deleted copy ctor), which suppresses the implicit default one -
    // needs restating explicitly, same as MidiTakePlayer and every other
    // class in this codebase using that macro.
    ClickGenerator() = default;

    enum class Sound { beep, click };

    // How often a click lands, held directly as ticks-per-click rather than
    // as a note-value denominator - "2/1" (two whole notes, eight beats)
    // has no denominator to be, and a plain tick count needs no decoding at
    // render time either.
    struct Resolution
    {
        const char* label;
        int ticksPerClick;
    };

    static constexpr int quarterNoteTicks = MidiTakePlayer::ticksPerQuarterNote;
    static constexpr Resolution resolutions[] =
    {
        { "2/1", quarterNoteTicks * 8 },
        { "1",   quarterNoteTicks * 4 },
        { "1/2", quarterNoteTicks * 2 },
        { "1/4", quarterNoteTicks     },
        { "1/8", quarterNoteTicks / 2 },
    };
    static constexpr int numResolutions = 5;

    static constexpr int defaultResolutionTicks = quarterNoteTicks; // 1/4
    static constexpr float defaultVolumeDb = -12.0f;

    // -18 dB is quiet enough to sit under a mix, 0 dB loud enough to cut
    // through one. The default is deliberately well below the top: the
    // click is mixed in after the master meters (so it doesn't make them
    // dance on every beat), which means its own level isn't covered by clip
    // detection - starting at -12 keeps it clear of the ceiling.
    static constexpr float minimumVolumeDb = -18.0f;
    static constexpr float maximumVolumeDb = 0.0f;

    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const                { return enabled.load(std::memory_order_relaxed); }

    void setSound(Sound newSound) { sound.store(newSound, std::memory_order_relaxed); }
    Sound getSound() const        { return sound.load(std::memory_order_relaxed); }

    void setResolutionTicks(int ticks) { resolutionTicks.store(juce::jmax(1, ticks), std::memory_order_relaxed); }
    int  getResolutionTicks() const    { return resolutionTicks.load(std::memory_order_relaxed); }

    void setVolumeDb(float db) { volumeDb.store(juce::jlimit(minimumVolumeDb, maximumVolumeDb, db), std::memory_order_relaxed); }
    float getVolumeDb() const  { return volumeDb.load(std::memory_order_relaxed); }

    // Audio thread. Adds the click into whatever is already in the buffer -
    // called at the very end of the callback, after the master chain has
    // run and after RecordingManager has taken its copy, so the click is
    // never processed by the master inserts and can never end up in a
    // recording.
    //
    // transportPositionSamples/numSamples/transportIsPlaying describe this
    // block exactly as they do for the take players, and bpm is the same
    // once-per-callback tempo read they get - see MainComponent's audio
    // callback.
    void renderBlock(juce::int64 transportPositionSamples, int numSamples,
                     bool transportIsPlaying, double bpm, double sampleRate,
                     juce::AudioBuffer<float>& outputBuffer)
    {
        double perTick = MidiTakePlayer::samplesPerTick(bpm, sampleRate);

        // Same discontinuity test the take players make: the window not
        // picking up where the last one left off means RTZ, a Range loop
        // wrap, or a scrub.
        bool discontinuity = (transportPositionSamples != lastWindowEnd);
        if (discontinuity)
        {
            positionTicks = perTick > 0.0 ? (double) transportPositionSamples / perTick : 0.0;
            // A jump lands the click grid somewhere new, so whatever was
            // still ringing from the last one belongs to the old position.
            envelope = 0.0f;
            holdSamplesRemaining = 0;
        }

        lastWindowEnd = transportIsPlaying ? transportPositionSamples + numSamples
                                            : transportPositionSamples;

        if (! transportIsPlaying || perTick <= 0.0)
        {
            // Deliberately no tail: pausing silences the click immediately
            // rather than letting the last one ring on into the silence.
            envelope = 0.0f;
            holdSamplesRemaining = 0;
            return;
        }

        if (! enabled.load(std::memory_order_relaxed))
        {
            // Still advance the grid while switched off, so turning it back
            // on lands on the beat rather than wherever the toggle happened
            // to be clicked.
            positionTicks += (double) numSamples / perTick;
            return;
        }

        double ticksPerClick = (double) resolutionTicks.load(std::memory_order_relaxed);
        double windowStart = positionTicks;
        double windowEnd   = windowStart + (double) numSamples / perTick;

        // Where the next click falls, in ticks - the first grid line at or
        // after the start of this window.
        double nextClick = std::ceil(windowStart / ticksPerClick) * ticksPerClick;

        auto currentSound = sound.load(std::memory_order_relaxed);
        float gain = juce::Decibels::decibelsToGain(volumeDb.load(std::memory_order_relaxed));

        // Beep is a fixed 1 kHz sine - fixed rather than following the
        // tempo, since a metronome that changed pitch with the tempo would
        // be unusable. Click is unpitched noise and needs no frequency at
        // all.
        double phaseStep = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;

        // Click decays over 10ms after a short hold at full level (see
        // clickHoldSeconds) - short enough to read as a sharp transient
        // rather than a burst of noise. The cap against the gap to the next
        // click barely binds at that length, but still saves the top of the
        // tempo range: at 1/8 up near 1200 BPM the clicks are only ~25ms
        // apart, and one still sounding when the next fires would turn a
        // row of ticks into a continuous wash.
        double clickIntervalSeconds = ticksPerClick * perTick / sampleRate;
        double decaySeconds = (currentSound == Sound::beep)
                                ? 0.030
                                : juce::jmin(0.010, clickIntervalSeconds * 0.4);
        float decay = (float) std::exp(-1.0 / (sampleRate * juce::jmax(0.001, decaySeconds)));

        int numChannels = outputBuffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            double tickAtThisSample = windowStart + ((double) i / perTick);

            // Retrigger rather than accumulate: at a fine resolution and a
            // high tempo two grid lines can land inside one block, and the
            // newer one simply takes over.
            while (nextClick < windowEnd && tickAtThisSample >= nextClick)
            {
                envelope = 1.0f;
                phase    = 0.0;
                holdSamplesRemaining = (currentSound == Sound::click)
                                         ? (int) (clickHoldSeconds * sampleRate) : 0;
                nextClick += ticksPerClick;
            }

            if (envelope <= 0.0001f)
                continue;

            float sample;
            if (currentSound == Sound::beep)
            {
                sample = (float) std::sin(phase) * envelope * gain;
                phase += phaseStep;
            }
            else
            {
                // White noise, differenced against the previous sample - a
                // gentle high-pass that brightens the burst into a
                // transient rather than a low rumble, at no cost. Noise is
                // generated every sample regardless of the envelope so the
                // sequence doesn't correlate with the click grid.
                float noise = nextNoise();
                float shaped = (noise - previousNoise) * 0.5f; // back to +/-1
                previousNoise = noise;

                // Saturate rather than amplify. A 10ms burst carries little
                // energy, but it is already peaking at full scale at the
                // 0dB setting, so a plain gain boost would just clip - and
                // the click is mixed in after the master clip detection, so
                // nothing would catch it. Driving it into a tanh raises the
                // RMS (which is what "louder" actually sounds like here)
                // while the normalisation keeps the peak where it was.
                sample = std::tanh(shaped * clickDrive) / std::tanh(clickDrive) * envelope * gain;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                outputBuffer.addSample(ch, i, sample);

            // Hold at full level briefly before decaying: at 10ms the
            // difference between a click you can place and one you have to
            // strain for is mostly the first couple of milliseconds, and a
            // hold adds that energy without touching the peak.
            if (holdSamplesRemaining > 0)
                --holdSamplesRemaining;
            else
                envelope *= decay;
        }

        positionTicks = windowEnd;
    }

private:
    std::atomic<bool>  enabled    { false };
    std::atomic<Sound> sound      { Sound::click };
    std::atomic<int>   resolutionTicks { defaultResolutionTicks };
    std::atomic<float> volumeDb   { defaultVolumeDb };

    // Audio-thread-only, persisting across calls - see renderBlock().
    double positionTicks = 0.0;
    juce::int64 lastWindowEnd = -1;
    // How long the click sits at full level before it starts decaying, and
    // how hard it's driven into the saturator - the two knobs that decide
    // whether a very short transient is audible over a mix. See
    // renderBlock() for why the answer isn't simply more gain.
    static constexpr double clickHoldSeconds = 0.003;
    static constexpr float  clickDrive       = 3.0f;

    double phase = 0.0;
    float envelope = 0.0f;
    int holdSamplesRemaining = 0;

    // Cheap deterministic white noise for the click - a plain LCG rather
    // than juce::Random so nothing about it can allocate or lock on the
    // audio thread.
    juce::uint32 noiseState = 0x9e3779b9;
    float previousNoise = 0.0f;
    float nextNoise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return (float) (juce::int32) noiseState * (1.0f / 2147483648.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClickGenerator)
};
