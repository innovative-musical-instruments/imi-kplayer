#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

class KPlayerAudioPlayHead : public juce::AudioPlayHead
{
public:
    void setBpm(double newBpm) { bpm.store(newBpm, std::memory_order_relaxed); }
    double getBpm() const      { return bpm.load(std::memory_order_relaxed); }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm.load(std::memory_order_relaxed));
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
        info.setIsPlaying(true);
        info.setIsRecording(false);
        info.setIsLooping(false);
        info.setPpqPosition(0.0);
        info.setTimeInSeconds(0.0);
        info.setFrameRate(juce::AudioPlayHead::FrameRate{});
        return info;
    }

private:
    std::atomic<double> bpm { 120.0 };
};

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

    void setAudioDeviceManager(juce::AudioDeviceManager* dm) { deviceManager = dm; }
    void setAudioCallback(juce::AudioIODeviceCallback* cb)   { audioCallback = cb; }

    // slotIndex: 0 = instrument/effect slot, 1..numInsertSlots = insert slots
    bool loadPlugin(int slotIndex,
                    const juce::PluginDescription& desc,
                    juce::AudioPluginFormatManager& formatManager,
                    double sampleRate,
                    int blockSize);

    void unloadPlugin(int slotIndex);
    bool hasPlugin(int slotIndex) const;
    bool isEditorVisible(int slotIndex) const;
    juce::String getPluginName(int slotIndex) const;

    void setBypassed(int slotIndex, bool shouldBeBypassed);
    bool isBypassed(int slotIndex) const;

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi);

    void prepareToPlay(double sampleRate, int blockSize);

    void setGain(float g)       { gain = g; }
    void setPan(float p)        { pan = juce::jlimit(-1.0f, 1.0f, p); }
    void setMidiChannel(int ch) { midiChannel = ch; }
    int  getMidiChannel() const { return midiChannel; }

    void   setTempo(double bpm) { playHead.setBpm(bpm); }
    double getTempo() const     { return playHead.getBpm(); }

    void showEditor(int slotIndex);
    void hideEditor(int slotIndex);

    const juce::Uuid& getId() const { return id; }

    void setName(const juce::String& newName) { name = newName; }
    juce::String getName() const              { return name; }

    void setMuted(bool shouldBeMuted) { mute.store(shouldBeMuted, std::memory_order_relaxed); }
    bool isMuted() const               { return mute.load(std::memory_order_relaxed); }

    void setSolo(bool shouldBeSolo)   { solo.store(shouldBeSolo, std::memory_order_relaxed); }
    bool isSolo() const               { return solo.load(std::memory_order_relaxed); }

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

        // Belt-and-suspenders: the audio thread checks this before touching
        // `plugin` at all. removeAudioCallback()/addAudioCallback() around
        // load/unload are what actually keep the audio thread out, but this
        // flag means a wiring mistake there degrades to silence instead of a
        // dangling-pointer crash.
        std::atomic<bool> ready { false };
        bool bypassed = false;
    };

    std::array<Slot, totalSlotCount> slots;
    KPlayerAudioPlayHead playHead;

    juce::AudioDeviceManager*    deviceManager = nullptr;
    juce::AudioIODeviceCallback* audioCallback = nullptr;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    float gain        = 1.0f;
    float pan         = 0.0f;
    int   midiChannel = 0;

    juce::Uuid id;
    juce::String name;
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };

    mutable juce::CriticalSection midiDeviceLock;
    juce::String midiDeviceIdentifier;

    void applyGainAndPan(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelProcessor)
};
