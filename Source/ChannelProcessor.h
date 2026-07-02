#pragma once
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

class ChannelProcessor
{
public:
    ChannelProcessor();
    ~ChannelProcessor();

    void setAudioDeviceManager(juce::AudioDeviceManager* dm) { deviceManager = dm; }
    void setAudioCallback(juce::AudioIODeviceCallback* cb)   { audioCallback = cb; }

    bool loadPlugin(const juce::PluginDescription& desc,
                    juce::AudioPluginFormatManager& formatManager,
                    double sampleRate,
                    int blockSize);

    void unloadPlugin();
    bool hasPlugin() const       { return plugin != nullptr; }
    bool isEditorVisible() const { return editorWindow != nullptr
                                       && editorWindow->isVisible(); }
    juce::String getPluginName() const;

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi);

    void prepareToPlay(double sampleRate, int blockSize);

    void setGain(float g)       { gain = g; }
    void setPan(float p)        { pan = juce::jlimit(-1.0f, 1.0f, p); }
    void setMidiChannel(int ch) { midiChannel = ch; }
    int  getMidiChannel() const { return midiChannel; }

    void   setTempo(double bpm) { playHead.setBpm(bpm); }
    double getTempo() const     { return playHead.getBpm(); }

    void showEditor();
    void hideEditor();

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::DocumentWindow>      editorWindow;
    KPlayerAudioPlayHead                       playHead;

    juce::AudioDeviceManager*    deviceManager = nullptr;
    juce::AudioIODeviceCallback* audioCallback = nullptr;

    // Belt-and-suspenders: the audio thread checks this before touching
    // `plugin` at all. removeAudioCallback()/addAudioCallback() around
    // load/unload are what actually keep the audio thread out, but this
    // flag means a wiring mistake there degrades to silence instead of a
    // dangling-pointer crash.
    std::atomic<bool> pluginReady { false };

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    float gain        = 1.0f;
    float pan         = 0.0f;
    int   midiChannel = 0;

    void applyGainAndPan(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelProcessor)
};
