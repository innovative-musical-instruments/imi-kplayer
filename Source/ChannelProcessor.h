#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "KPlayerAudioPlayHead.h"

// A channel's plugin chain is slot 0 (instrument OR audio-effect plugin,
// per the MVP spec) followed by 5 insert slots. Insert slots also accept
// instrument plugins (not just audio effects) - loading one there is the
// user's choice and may produce no audio if the plugin doesn't pass its
// input through, since only slot 0 feeds a channel with no external input.
//
// Implements AudioProcessorListener purely to catch parameter/state changes
// made *inside* a loaded plugin's own editor (turning a knob doesn't touch
// any of our own UI, which is the only thing the existing onDirty callbacks
// see) and flag the session dirty for those too. The callback can fire from
// any thread - including the audio thread mid-automation - so it only ever
// does a single relaxed atomic store; see consumeParametersDirtyFlag().
class ChannelProcessor : public juce::AudioProcessorListener
{
public:
    static constexpr int numInsertSlots = 5;
    static constexpr int totalSlotCount = 1 + numInsertSlots;
    static constexpr int slot0Index     = 0;

    ChannelProcessor();
    ~ChannelProcessor();

    // slotIndex: 0 = instrument/effect slot, 1..numInsertSlots = insert slots.
    // Instruments are allowed in insert slots too - see the class comment
    // above for the caveat about audio passthrough.
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

    // Delta load (see docs/KPlayer_Session_Save_Load_Design_2026-07-25.md
    // Part B): pushes newState into whatever's already loaded in this slot,
    // in place, instead of the destroy+recreate loadPlugin()/unloadPlugin()
    // pair - skips formatManager.createPluginInstance(), bus renegotiation,
    // prepareToPlay(), and both those calls' safety-margin sleeps entirely.
    // Only the same 50ms drain margin loadPlugin()/unloadPlugin() also use
    // is kept, to guarantee the audio thread has observed slot.ready==false
    // before setStateInformation() runs concurrently with it - no HISE-
    // style settle sleep afterward, since (confirmed for this app's actual
    // instrument roster) K-Sampler and friends load their sample data once
    // at instantiation, not in response to a later setStateInformation()
    // call, so there's no async streaming work here to wait out. Revisit
    // this assumption if a plugin that *does* reload sample data per-patch
    // ever ends up in the roster. Caller's job to have already confirmed
    // this is actually the same plugin identity as what's loaded - this
    // just blindly pushes the new state into whatever's here.
    // No-ops (returns false) if the slot is empty.
    bool updatePluginState(int slotIndex, const juce::MemoryBlock& newState);

    bool hasPlugin(int slotIndex) const;
    bool isEditorVisible(int slotIndex) const;
    juce::String getPluginName(int slotIndex) const;

    // Empty/default if the slot has no plugin loaded.
    juce::PluginDescription getPluginDescription(int slotIndex) const;
    juce::MemoryBlock getPluginState(int slotIndex) const;

    void setBypassed(int slotIndex, bool shouldBeBypassed);
    bool isBypassed(int slotIndex) const;

    // Fired at the end of setBypassed(), regardless of which caller changed
    // the state - the slot's context menu and the plugin's own editor window
    // (PluginEditorWindow::setPluginEditor's onToggleBypass) both funnel
    // through setBypassed(), so this is the one place ChannelComponent needs
    // to hook to keep its slot button in sync with either source.
    std::function<void(int slotIndex)> onBypassChanged;

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi);

    void prepareToPlay(double sampleRate, int blockSize);

    void  setGain(float g)      { gain = g; }
    float getGain() const       { return gain; }

    // Same quadratic taper as the on-screen gain fader (see
    // docs/KPlayer_Refinement_Spec_2026-07-11.md section 1.3) - shared here
    // so a MIDI CC7 gain change (see processBlock) lands on the exact same
    // curve as dragging the fader to the equivalent position, not a second,
    // independently-tuned one. Fixed to the -96dB..+6dB range - same shape
    // and headroom as the master fader's own curve (MasterChainComponent's
    // volumeRange), just expressed as the two static functions here instead
    // of an inline NormalisableRange, since this side also needs to be
    // reachable from MIDI CC7 handling below.
    static double normalisedToGainDb(double normalised)
    {
        double t = 1.0 - normalised;
        return 6.0 - 102.0 * t * t;
    }

    static double gainDbToNormalised(double dB)
    {
        double t = std::sqrt(juce::jlimit(0.0, 1.0, (6.0 - dB) / 102.0));
        return 1.0 - t;
    }
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

    // Audio Take selection (Increment C, see
    // docs/kplayer-take-recording-playback-spec.md) - a "take:"-prefixed
    // identifier (see RecordingManager::isTakeIdentifier) referencing a
    // recorded Channel-N.wav to play back through this channel's insert
    // chain instead of live hardware input. Mutually exclusive with
    // audioInputChannelIndex above by UI-layer convention (ChannelComponent
    // clears one whenever the other is set) rather than by construction -
    // this field can't just reuse the int one since a file reference can't
    // fit in a plain index. Read on the audio thread each block (same
    // "which source feeds this channel" decision as
    // getMidiDeviceIdentifier() below), so it gets the same
    // CriticalSection-guarded juce::String pattern for the same reason -
    // juce::String's ref-counting isn't safe to touch unsynchronized across
    // threads.
    void setAudioTakeIdentifier(const juce::String& identifier);
    juce::String getAudioTakeIdentifier() const;

    void   setTempo(double bpm) { playHead.setBpm(bpm); }
    double getTempo() const     { return playHead.getBpm(); }

    void showEditor(int slotIndex);
    void hideEditor(int slotIndex);

    const juce::Uuid& getId() const          { return id; }
    void setId(const juce::Uuid& newId)      { id = newId; }

    void setName(const juce::String& newName) { name = newName; }
    juce::String getName() const              { return name; }

    // Fixed 1-based position, mirroring ChannelComponent's own channelNumber
    // (see MainComponent::addChannel) - needed here too so showEditor() can
    // put "channel-N slot-M" in the plugin window's title.
    void setChannelNumber(int n) { channelNumber = n; }
    int  getChannelNumber() const { return channelNumber; }

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

    // Set (from any thread) by audioProcessorParameterChanged/audioProcessorChanged
    // below whenever a loaded plugin's parameters or non-parameter state
    // change. A message-thread Timer polls and clears this via the
    // test-and-reset below - same fire-and-forget atomic pattern as the
    // peak meters, deliberately not doing any real work in the callback
    // itself since it can arrive on the audio thread mid-automation.
    bool consumeParametersDirtyFlag() { return parametersDirty.exchange(false, std::memory_order_relaxed); }

    // Set (from the audio thread) whenever an incoming MIDI CC7 message sets
    // this channel's gain (see processBlock) - a message-thread Timer polls
    // and clears this via the same exchange-and-reset pattern as
    // consumeParametersDirtyFlag(), then refreshes the on-screen gain fader
    // to match and marks the session dirty (same as a manual fader drag).
    bool consumeGainChangedByMidi() { return gainChangedByMidi.exchange(false, std::memory_order_relaxed); }

    // Same pattern as consumeGainChangedByMidi() above, but for MIDI CC10
    // setting this channel's pan (see processBlock) - CC7 and CC10 are
    // handled independently of each other, gain-only and pan-only
    // respectively.
    bool consumePanChangedByMidi() { return panChangedByMidi.exchange(false, std::memory_order_relaxed); }

    // Set (from the audio thread) whenever an incoming MIDI CC84-89 message
    // bypasses/activates a slot (see processBlock) - same message-thread
    // Timer poll pattern as consumeGainChangedByMidi() above. This only
    // says *something* changed, not which slot; syncBypassIndicatorsFromMidi()
    // below re-syncs every slot in one cheap pass (only 6 of them) once
    // polled, since processBlock can't call setBypassed() itself - that
    // touches juce::Component state (an open editor window's bypass bar,
    // see PluginEditorWindow::setBypassedIndicator()) which must only ever
    // be touched from the message thread. slots[i].bypassed itself is
    // written directly in processBlock, the same plain-value, single-writer-
    // at-a-time convention already accepted for gain/pan (see setGain()).
    bool consumeBypassChangedByMidi() { return bypassChangedByMidi.exchange(false, std::memory_order_relaxed); }
    void syncBypassIndicatorsFromMidi();

    // Set (from the audio thread) whenever an incoming MIDI CC103 message
    // requests this channel be armed/disarmed for recording (see
    // processBlock). ChannelProcessor has no reference to RecordingManager
    // or even its own channel index, so it can only report the *request* -
    // MainComponent's message-thread Timer poll consumes this and calls its
    // own setChannelArmed(index, ...), the same path the channel's own arm
    // button uses. Returns false (armedOut left untouched) if nothing is
    // pending, matching the exchange-and-reset pattern used throughout this
    // class rather than a separate has/get pair.
    bool consumeArmChangedByMidi(bool& armedOut)
    {
        if (! armChangedByMidi.exchange(false, std::memory_order_relaxed))
            return false;
        armedOut = pendingArmValueFromMidi.load(std::memory_order_relaxed);
        return true;
    }

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
    std::atomic<bool> gainChangedByMidi { false };
    std::atomic<bool> panChangedByMidi { false };
    std::atomic<bool> bypassChangedByMidi { false };
    std::atomic<bool> armChangedByMidi { false };
    std::atomic<bool> pendingArmValueFromMidi { false };

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

    // Mirrors whether audioTakeIdentifier is currently non-empty, but as a
    // plain bool so processBlock() (audio thread) can check it without
    // locking audioTakeLock every block - same accepted-tradeoff, single-
    // writer plain-field convention already used for audioInputChannelIndex
    // above (see processBlock()'s slot-0-empty passthrough check, which
    // this extends to also cover an Audio Take source). Only ever written
    // from setAudioTakeIdentifier() on the message thread; a torn read is
    // impossible for a plain bool, so no lock is needed on this side.
    bool hasAudioTakeSelected = false;

    juce::Uuid id;
    juce::String name;
    int channelNumber = 0;
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };

    std::atomic<float> peakLevelLeft  { 0.0f };
    std::atomic<float> peakLevelRight { 0.0f };
    std::atomic<bool>  clipFlag       { false };
    void updateMetering(const juce::AudioBuffer<float>& buffer);

    mutable juce::CriticalSection midiDeviceLock;
    juce::String midiDeviceIdentifier;

    mutable juce::CriticalSection audioTakeLock;
    juce::String audioTakeIdentifier;

    void applyGainAndPan(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelProcessor)
};
