#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "KPlayerAudioPlayHead.h"

// A channel's plugin chain is slot 0 (instrument OR audio-effect plugin,
// per the MVP spec) followed by 5 insert slots (audio-effect plugins only).
class ChannelProcessor
{
public:
    static constexpr int numInsertSlots = 5;
    static constexpr int totalSlotCount = 1 + numInsertSlots;
    static constexpr int slot0Index     = 0;

    ChannelProcessor();
    ~ChannelProcessor();

    // slotIndex: 0 = instrument/effect slot, 1..numInsertSlots = insert slots.
    // Loading an instrument (PluginDescription::isInstrument) into an insert
    // slot is rejected here at the engine level, not just filtered out of
    // the plugin browser's list - insert slots are audio-effect-only.
    // initialState, if non-null, is applied via setStateInformation() right
    // after the plugin is prepared - used to restore a saved session's
    // per-plugin state (preset/patch) as part of instantiation.
    bool loadPlugin(int slotIndex,
                    const juce::PluginDescription& desc,
                    juce::AudioPluginFormatManager& formatManager,
                    double sampleRate,
                    int blockSize,
                    const juce::MemoryBlock* initialState = nullptr);

    void unloadPlugin(int slotIndex);
    bool hasPlugin(int slotIndex) const;
    bool isEditorVisible(int slotIndex) const;
    juce::String getPluginName(int slotIndex) const;

    // Empty/default if the slot has no plugin loaded.
    juce::PluginDescription getPluginDescription(int slotIndex) const;
    juce::MemoryBlock getPluginState(int slotIndex) const;

    void setBypassed(int slotIndex, bool shouldBeBypassed);
    bool isBypassed(int slotIndex) const;

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi);

    void prepareToPlay(double sampleRate, int blockSize);

    void  setGain(float g)      { gain = g; }
    float getGain() const       { return gain; }
    void  setPan(float p)       { pan = juce::jlimit(-1.0f, 1.0f, p); }
    float getPan() const        { return pan; }
    void setMidiChannel(int ch) { midiChannel = ch; }
    int  getMidiChannel() const { return midiChannel; }

    // Index into the audio device's *active* input channels (matching the
    // order of audioDeviceIOCallbackWithContext's inputChannelData array),
    // or -1 for "no audio input assigned". Read directly by the audio
    // thread each block (MainComponent decides what to copy into this
    // channel's scratch buffer before processBlock runs) - same plain-int,
    // no-lock convention as midiChannel above.
    void setAudioInputChannelIndex(int index) { audioInputChannelIndex = index; }
    int  getAudioInputChannelIndex() const    { return audioInputChannelIndex; }

    void   setTempo(double bpm) { playHead.setBpm(bpm); }
    double getTempo() const     { return playHead.getBpm(); }

    void showEditor(int slotIndex);
    void hideEditor(int slotIndex);

    const juce::Uuid& getId() const          { return id; }
    void setId(const juce::Uuid& newId)      { id = newId; }

    void setName(const juce::String& newName) { name = newName; }
    juce::String getName() const              { return name; }

    void setMuted(bool shouldBeMuted) { mute.store(shouldBeMuted, std::memory_order_relaxed); }
    bool isMuted() const               { return mute.load(std::memory_order_relaxed); }

    void setSolo(bool shouldBeSolo)   { solo.store(shouldBeSolo, std::memory_order_relaxed); }
    bool isSolo() const               { return solo.load(std::memory_order_relaxed); }

    // Post-fader peak metering (Increment 3): the audio thread stores the
    // current block's peak magnitude and ORs into the clip flag every
    // processBlock() call. PeakMeterComponent's own message-thread Timer
    // reads these to drive ballistics/decay and to latch/clear the clip
    // LED - nothing here needs ordering with other data, so relaxed
    // atomics are enough, same as mute/solo above.
    const std::atomic<float>* getPeakLevelLeftPtr()  const { return &peakLevelLeft;  }
    const std::atomic<float>* getPeakLevelRightPtr() const { return &peakLevelRight; }
    std::atomic<bool>*        getClipFlagPtr()             { return &clipFlag;       }

    // Read on the audio thread once per block to decide which device's MIDI
    // buffer to feed this channel, so it's guarded by its own lock rather
    // than relying on juce::String's ref-counting to be safe unsynchronized
    // across threads.
    void setMidiDeviceIdentifier(const juce::String& deviceId);
    juce::String getMidiDeviceIdentifier() const;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::DocumentWindow>      editorWindow;

        // The audio thread checks this before touching `plugin` at all.
        // load/unload set it false, sleep long enough to guarantee at least
        // one audio callback has observed that (block periods are a few ms
        // at most; the sleep is tens of ms), then safely mutate/destroy
        // `plugin` without ever detaching the shared device callback - that
        // would silence every other channel's audio too, not just this slot.
        std::atomic<bool> ready { false };
        bool bypassed = false;
    };

    std::array<Slot, totalSlotCount> slots;
    KPlayerAudioPlayHead playHead;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    float gain        = 1.0f;
    float pan         = 0.0f;
    int   midiChannel = 0;
    int   audioInputChannelIndex = -1;

    juce::Uuid id;
    juce::String name;
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };

    std::atomic<float> peakLevelLeft  { 0.0f };
    std::atomic<float> peakLevelRight { 0.0f };
    std::atomic<bool>  clipFlag       { false };
    void updateMetering(const juce::AudioBuffer<float>& buffer);

    mutable juce::CriticalSection midiDeviceLock;
    juce::String midiDeviceIdentifier;

    void applyGainAndPan(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelProcessor)
};
