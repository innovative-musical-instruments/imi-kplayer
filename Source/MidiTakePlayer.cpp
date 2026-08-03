#include "MidiTakePlayer.h"

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

bool MidiTakePlayer::loadTake(const juce::File& midiFile, double sampleRate, double bpm)
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
    double samplesPerTick = sampleRate * 60.0 / (bpm * (double) ticksPerQuarterNote);

    auto* track = midiFileReader.getTrack(0);
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        auto* holder = track->getEventPointer(i);
        double samples = holder->message.getTimeStamp() * samplesPerTick;
        take->sequence.addEvent(juce::MidiMessage(holder->message, samples));
    }

    publish(std::move(take));
    return true;
}

void MidiTakePlayer::unload()
{
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
                                 bool transportIsPlaying, juce::MidiBuffer& outputMidi)
{
    auto* take = published.load(std::memory_order_acquire);

    bool discontinuity = (take != lastRenderedTake) || (transportPositionSamples != lastWindowEnd);
    bool justPaused    = wasPlayingLastBlock && ! transportIsPlaying;
    if (discontinuity || justPaused)
        flushActiveNotes(outputMidi, 0);

    lastRenderedTake    = take;
    lastWindowEnd       = transportPositionSamples + numSamples;
    wasPlayingLastBlock = transportIsPlaying;

    if (take == nullptr || ! transportIsPlaying)
        return;

    auto& seq = take->sequence;
    double windowStart = (double) transportPositionSamples;
    double windowEnd   = (double) (transportPositionSamples + numSamples);

    for (int i = seq.getNextIndexAtTime(windowStart); i < seq.getNumEvents(); ++i)
    {
        double t = seq.getEventTime(i);
        if (t >= windowEnd)
            break;

        auto& msg = seq.getEventPointer(i)->message;
        int offset = juce::jlimit(0, numSamples - 1, (int) (t - windowStart));
        outputMidi.addEvent(msg, offset);

        if (msg.isNoteOn())
            activeNotes[(size_t) ((msg.getChannel() - 1) * 128 + msg.getNoteNumber())] = true;
        else if (msg.isNoteOff())
            activeNotes[(size_t) ((msg.getChannel() - 1) * 128 + msg.getNoteNumber())] = false;
    }
}
