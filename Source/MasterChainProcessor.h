#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "KPlayerAudioPlayHead.h"

// The master bus insert chain (Increment 3 item 7): 5 slots applied once to
// the post-sum stereo signal, after every channel's own processing.
// Instrument plugins are allowed here too (the user's choice) but since the
// master chain has no "instrument slot" concept, a loaded instrument won't
// receive MIDI and will just see whatever the post-sum signal passes
// through, if anything. Deliberately not a ChannelProcessor reused
// wholesale - ChannelProcessor::processBlock clears its buffer when slot 0
// (the instrument slot) is empty, which is correct for a channel with
// nothing loaded but would silently wipe the master sum every block here.
//
// Implements AudioProcessorListener for the same reason as ChannelProcessor
// - see its header comment for the full rationale and the thread-safety
// note on why the callbacks below only ever do a single relaxed atomic
// store.
class MasterChainProcessor : public juce::AudioProcessorListener
{
public:
    static constexpr int numSlots = 5;

    MasterChainProcessor();
    ~MasterChainProcessor();

    // Instruments are allowed in any master-chain slot - see the class
    // comment above for the caveat about MIDI/audio passthrough.
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

    juce::PluginDescription getPluginDescription(int slotIndex) const;
    juce::MemoryBlock getPluginState(int slotIndex) const;

    void setBypassed(int slotIndex, bool shouldBeBypassed);
    bool isBypassed(int slotIndex) const;

    // See ChannelProcessor::onBypassChanged for the rationale - fired at the
    // end of setBypassed() regardless of whether the slot's context menu or
    // the plugin's own editor window triggered the change.
    std::function<void(int slotIndex)> onBypassChanged;

    // Processes buffer in place, in series, starting from whatever the
    // caller already put in it (the channel-rack sum) - never clears it.
    void processBlock(juce::AudioBuffer<float>& buffer);

    void prepareToPlay(double sampleRate, int blockSize);

    void   setTempo(double bpm) { playHead.setBpm(bpm); }
    double getTempo() const     { return playHead.getBpm(); }

    void showEditor(int slotIndex);
    void hideEditor(int slotIndex);

    bool consumeParametersDirtyFlag() { return parametersDirty.exchange(false, std::memory_order_relaxed); }

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override
    {
        parametersDirty.store(true, std::memory_order_relaxed);
    }

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override
    {
        parametersDirty.store(true, std::memory_order_relaxed);
    }

    std::atomic<bool> parametersDirty { false };

    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::DocumentWindow>      editorWindow;
        std::atomic<bool> ready { false };
        bool bypassed = false;
    };

    std::array<Slot, numSlots> slots;
    KPlayerAudioPlayHead playHead;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainProcessor)
};
