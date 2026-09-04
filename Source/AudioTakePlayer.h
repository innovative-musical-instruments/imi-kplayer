#pragma once
#include <atomic>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Plays a single channel's recorded Audio Take (see
// docs/kplayer-take-recording-playback-spec.md, Increment C) back into that
// channel's audio input, driven by the shared SessionTransport - the
// "play-along / backing track" workflow: fully reprocessed by the channel's
// current insert chain, unlike MIDI Take playback's "different instrument"
// workflow (see MidiTakePlayer). One instance per channel, owned by
// MainComponent in a vector parallel to channelProcessors/channelComponents/
// midiTakePlayers (see MainComponent::addChannel/setChannelCount, which
// already fully detaches the audio callback before resizing those).
//
// Much simpler than MidiTakePlayer: Audio Takes are always the WAV files
// RecordingManager already writes (no tick/tempo conversion needed), and
// unlike MIDI there's no "stuck note" analog to flush on a discontinuity -
// a plain sample-accurate reread of the file at wherever the shared
// transport's position happens to be is correct by construction every
// block, so there's no persistent playback state (cursor, active-note
// tracking) to maintain at all.
//
// Threading: loadTake()/unload() are message-thread only, and publish a
// freshly-mapped reader via an atomic pointer swap - same
// storage/published-pointer shape as MidiTakePlayer/RecordingManager's
// track publish/retire, old reader destroyed only after a drain-margin
// sleep so the audio thread can never observe a freed reader.
// renderBlock() is audio-thread only.
class AudioTakePlayer
{
public:
    // JUCE_DECLARE_NON_COPYABLE below counts as a user-declared constructor
    // (the deleted copy ctor), which suppresses the implicit default one -
    // see MidiTakePlayer.h for the same note.
    AudioTakePlayer() = default;

    // Message thread. Memory-maps the WAV file for fast random-access reads
    // straight from the audio thread (Audio Takes are always the existing
    // recorded-WAV mechanism - see RecordingManager - so WavAudioFormat
    // directly, no format detection/AudioFormatManager needed, same
    // directness as RecordingManager's own writer side). Returns false (and
    // leaves the player unloaded) on a read failure - a fresh selection
    // that fails shouldn't keep silently playing whatever was selected
    // before.
    bool loadTake(const juce::File& audioFile);

    // Message thread. Clears the player back to "nothing selected" - used
    // when the channel's input selector moves away from a Take. Same
    // publish-then-drain-then-destroy pattern as loadTake()'s swap.
    void unload();

    // Message thread only. Length of the currently loaded Take in samples
    // (0 when nothing is loaded) - used by MainComponent::
    // updateTransportRange to work out the longest selected Take, which is
    // what the transport's default Range spans. This is the file's own
    // sample count, not seconds: playback reads it straight at the
    // transport position with no rate conversion (see renderBlock), so a
    // file recorded at a different rate than the device is already played
    // back at the wrong speed - counting its raw samples is what agrees
    // with how long it will actually take to play, which is what the Range
    // needs. Deliberately a plain member rather than an atomic: only ever
    // written by loadTake()/unload() and read by the same message thread.
    juce::int64 getLengthSamples() const { return lengthSamples; }

    // Audio thread. transportPositionSamples/numSamples describe this
    // block's window on the shared SessionTransport (see
    // SessionTransport::advanceAndGetBlockStartPosition, called once per
    // callback by MainComponent - the same call already used for this
    // channel's MidiTakePlayer, if any, so MIDI and Audio Takes recorded in
    // the same take stay in lockstep). While paused, does nothing - the
    // caller's scratch buffer is already cleared each block (same
    // convention the live-hardware-input path relies on), so leaving it
    // untouched is silence. While playing, reads directly from the mapped
    // file at transportPositionSamples: JUCE's AudioFormatReader::read()
    // contract zero-fills any region before the start or past the end of
    // the file, so "selected mid-transport-run" and "stop at end of take"
    // both fall out with no extra logic here.
    void renderBlock(juce::int64 transportPositionSamples, int numSamples, bool transportIsPlaying,
                     float* const* destChannels, int numDestChannels);

private:
    void publish(std::unique_ptr<juce::MemoryMappedAudioFormatReader> newReader);

    // Message-thread-owned lifetime; the audio thread only ever touches
    // `published`, never this directly - see publish() above.
    std::unique_ptr<juce::MemoryMappedAudioFormatReader> storage;
    std::atomic<juce::MemoryMappedAudioFormatReader*> published { nullptr };

    // See getLengthSamples() - message-thread-only, kept in step with what
    // publish() last published.
    juce::int64 lengthSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTakePlayer)
};
