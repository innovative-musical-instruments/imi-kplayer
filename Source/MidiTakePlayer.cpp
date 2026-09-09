#include "MidiTakePlayer.h"
#include <cmath>

void MidiTakePlayer::publish(std::unique_ptr<LoadedTake> newTake)
{
    auto* raw = newTake.get();
    auto old = std::move(storage);
    storage = std::move(newTake);
    published.store(raw, std::memory_order_release);

    // See RecordingManager::retireTrack for the same drain-margin pattern:
    // by the time this sleep returns, any audio callback that had already
    // loaded the old pointer has finished using it, so it's safe to destroy.
    if (old != nullptr)
    {
        juce::Thread::sleep(50);
        old.reset();
    }
}

bool MidiTakePlayer::loadTake(const juce::File& midiFile)
{
    auto stream = midiFile.createInputStream();
    if (stream == nullptr)
    {
        unload();
        return false;
    }

    juce::MidiFile midiFileReader;
    if (! midiFileReader.readFrom(*stream) || midiFileReader.getNumTracks() == 0)
    {
        unload();
        return false;
    }

    auto take = std::make_unique<LoadedTake>();

    // Timestamps stay in ticks - see loadTake()'s header comment for why
    // the tempo isn't applied here any more.
    double lastTick = 0.0;
    auto* track = midiFileReader.getTrack(0);
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        auto* holder = track->getEventPointer(i);
        lastTick = juce::jmax(lastTick, holder->message.getTimeStamp());

        // Meta events (the tempo event RecordingManager writes, and the
        // end-of-track marker JUCE appends) describe the file rather than
        // being anything to play, and have no business being pushed into a
        // plugin's MIDI buffer - but they do still count towards how long
        // the take is, which is why lastTick is taken above this.
        if (holder->message.isMetaEvent())
            continue;

        take->sequence.addEvent(holder->message);
    }

    lengthTicks = (juce::int64) std::ceil(lastTick);

    publish(std::move(take));
    return true;
}

void MidiTakePlayer::unload()
{
    lengthTicks = 0;
    publish(nullptr);
}

void MidiTakePlayer::flushActiveNotes(juce::MidiBuffer& outputMidi, int atSampleOffset)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        for (int note = 0; note < 128; ++note)
        {
            auto idx = (size_t) (ch * 128 + note);
            if (activeNotes[idx])
            {
                outputMidi.addEvent(juce::MidiMessage::noteOff(ch + 1, note), atSampleOffset);
                activeNotes[idx] = false;
            }
        }
    }
}

void MidiTakePlayer::renderBlock(juce::int64 transportPositionSamples, int numSamples,
                                 bool transportIsPlaying, double bpm, double sampleRate,
                                 juce::MidiBuffer& outputMidi)
{
    auto* take = published.load(std::memory_order_acquire);

    bool discontinuity = (take != lastRenderedTake) || (transportPositionSamples != lastWindowEnd);
    bool justPaused    = wasPlayingLastBlock && ! transportIsPlaying;
    if (discontinuity || justPaused)
        flushActiveNotes(outputMidi, 0);

    double perTick = samplesPerTick(bpm, sampleRate);

    if (discontinuity)
    {
        // A jump rather than ordinary progress - a different Take selected,
        // RTZ, a Range loop wrap, or a scrub. There's no continuous musical
        // position to carry over across a jump, so the take is re-anchored
        // to wherever the transport now is, read at the current tempo. That
        // makes seeking behave the obvious way ("the playhead says 0:30, so
        // play the take from 0:30") at the cost of the anchor disagreeing
        // with the integrated position below if the tempo moved mid-run -
        // which is exactly what a seek is for.
        positionTicks = perTick > 0.0 ? (double) transportPositionSamples / perTick : 0.0;
    }

    lastRenderedTake    = take;
    wasPlayingLastBlock = transportIsPlaying;
    // While paused the transport hands out the same position every block,
    // so recording the *end* of a window that was never played would make
    // every paused block look like a jump - and re-anchor away the musical
    // position this player had reached, which after a mid-run tempo change
    // is not the same thing. Parking the mark on the current position
    // instead means resuming reads as continuous (it is), while a genuine
    // jump while paused - RTZ, a new Take - still registers.
    lastWindowEnd       = transportIsPlaying ? transportPositionSamples + numSamples
                                              : transportPositionSamples;

    if (take == nullptr || ! transportIsPlaying || perTick <= 0.0)
        return;

    // The window is measured in ticks and advanced by however many ticks
    // this block's worth of samples covers *at the tempo right now* - so a
    // tempo change takes effect on the very next block, progressively, from
    // wherever playback has got to, rather than re-timing what has already
    // been played. A tempo swept continuously by MIDI-clock sync simply
    // sweeps the rate this window advances at.
    double windowStart = positionTicks;
    double windowEnd   = windowStart + (double) numSamples / perTick;

    auto& seq = take->sequence;
    for (int i = seq.getNextIndexAtTime(windowStart); i < seq.getNumEvents(); ++i)
    {
        double t = seq.getEventTime(i);
        if (t >= windowEnd)
            break;

        auto& msg = seq.getEventPointer(i)->message;
        int offset = juce::jlimit(0, numSamples - 1, (int) ((t - windowStart) * perTick));
        outputMidi.addEvent(msg, offset);

        if (msg.isNoteOn())
            activeNotes[(size_t) ((msg.getChannel() - 1) * 128 + msg.getNoteNumber())] = true;
        else if (msg.isNoteOff())
            activeNotes[(size_t) ((msg.getChannel() - 1) * 128 + msg.getNoteNumber())] = false;
    }

    positionTicks = windowEnd;
}
