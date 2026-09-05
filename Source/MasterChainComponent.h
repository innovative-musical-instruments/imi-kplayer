#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MasterChainProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"
#include "SlotButtonLookAndFeel.h"
#include "SelectorLookAndFeel.h"
#include "RecordingManager.h"

// The master bus strip: the insert-slot buttons (same load/replace/remove/
// bypass popup-menu pattern as ChannelComponent's insert slots, applied
// once to the master sum instead of once per channel), the output gain
// fader and its peak meter integrated directly below them, and the master
// ARM toggle - mirroring how a channel bundles its own gain fader under its
// insert slots. Everything session-global (transport, Record Ready, tempo/
// sync, channel count, Settings, Panic) lives one strip further right, in
// GlobalSectionComponent - see MainComponent::resized() for the actual
// strip order.
class MasterChainComponent : public juce::Component,
                             public juce::ChangeListener
{
public:
    static constexpr int numSlots = MasterChainProcessor::numSlots;

    std::function<void(int slotIndex)> onLoadPlugin;
    std::function<void(int slotIndex)> onReplacePlugin;
    std::function<void()> onDirty;

    // Fired with the new linear gain whenever the user moves the fader.
    std::function<void(float linearGain)> onVolumeChanged;

    // Multitrack recording (see RecordingManager) - MainComponent owns the
    // arm truth and is the one global transport for every armed channel and
    // the master together; this component only reflects that state via
    // setArmed() and reports user clicks. The arm toggle stays clickable
    // while recording is active, same as a channel's own arm button -
    // arming mid-take takes effect immediately rather than being rejected.
    std::function<void(bool armed)> onMasterArmToggled;

    // "Arm All" - arms/unarms every channel plus the master together (see
    // MainComponent::toggleArmAll()). This component has no idea what
    // "all" means (channel count, other channels' arm state) - it only
    // reports the click; setArmAllState() below pushes back the reflected
    // aggregate, same as every other arm/reflect pair in this class.
    std::function<void()> onArmAllClicked;

    // Master "Audio In"/"MIDI In" bulk selectors (v0.9.8) - this component
    // enumerates what to offer (live inputs/devices + Take groups, see
    // RecordingManager::findAudioTakeGroups()/findMidiTakeGroups()) and
    // reports the resolved choice; MainComponent decides what it means for
    // each channel and applies it (see applyMasterAudioInputSelection()/
    // applyMasterMidiInputSelection()) since only it can see every channel.
    // A non-null takeFolder means "apply this take group" (the int/String
    // parameter is unused in that case); otherwise the int/String is the
    // live input/device to broadcast to every channel (-1 / empty = None,
    // i.e. bulk-clear). Deliberately excludes the master channel itself,
    // which has no audio/MIDI input concept of its own to set.
    std::function<void(int liveChannelIndex, const juce::File& takeFolder)> onAudioInputSelected;
    std::function<void(juce::String liveDeviceIdentifier, const juce::File& takeFolder)> onMidiInputSelected;

    // Bulk Bypass/Activate menu (v0.9.8) - "Byp/Act." applies a bypass
    // state to one slot position (or every slot) across the whole rig at
    // once. This component applies its own inserts directly (it owns
    // MasterChainProcessor already, and slot 1..5's bypass menu items
    // mirror this component's own slots 0..4 one-to-one - see
    // showBypassMenu()'s comment); onBypassChannelsRequested reports what
    // every channel should do, since only MainComponent can see them.
    // channelSlotIndex -1 means every slot (0..5); 0 means the
    // instrument/effect slot, which only exists on channels - this
    // component never touches anything itself for that one.
    std::function<void(int channelSlotIndex, bool bypass)> onBypassChannelsRequested;

    MasterChainComponent(MasterChainProcessor& processor, juce::AudioDeviceManager& deviceManager,
                         RecordingManager& recordingManager);
    ~MasterChainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Sets the displayed fader position without firing onVolumeChanged -
    // for MainComponent to push a value in (session load, external change).
    void setVolume(float linearGain);

    void setLevelMeterSources(const std::atomic<float>* leftLevel,
                              const std::atomic<float>* rightLevel,
                              std::atomic<bool>* clipFlagLeft,
                              std::atomic<bool>* clipFlagRight);

    void setArmed(bool shouldBeArmed);

    // Reflects whether every channel plus the master is currently armed -
    // purely derived/display state, never independently tracked (see
    // MainComponent::updateArmAllButtonState()), so it can't drift out of
    // sync with the truth.
    void setArmAllState(bool allArmed);

    // Global collapse (see MainComponent::setInputSectionCollapsedState) -
    // hides this strip's own Audio In/MIDI In selectors above the inserts,
    // same as it hides a channel's Audio In/MIDI In/MIDI Channel rows
    // above its Plugins section (see resized()'s comment).
    void setInputSectionCollapsed(bool collapsed);

    // Re-scans Take groups from disk and rebuilds both selectors'
    // "Recorded Takes" sections - called by MainComponent right after a
    // recording finishes and after a session load sets the recordings
    // folder, same timing as ChannelComponent's own
    // refreshTakeList()/refreshAudioTakeList().
    void refreshTakeGroups();

    // Device hotplug (audio input list) - see the constructor's
    // deviceManager.addChangeListener(this).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    // Menu built/handled here (see showBypassMenu()'s .cpp comment for the
    // item-id scheme and channel-slot-to-master-slot mapping); the two
    // apply helpers below are what a chosen menu item actually calls.
    void showBypassMenu();
    void applyBypassEverywhere(bool bypass);
    void applyBypassToSlot(int channelSlotIndex, bool bypass);

    // Rebuild-from-scratch treatment, same pattern and reasoning as
    // ChannelComponent's own rebuildAudioInputBox()/rebuildMidiInputBox() -
    // None + live inputs/devices + a "Recorded Takes" section of Take
    // *groups* (see RecordingManager::findAudioTakeGroups()/
    // findMidiTakeGroups()) - called by both the live-list refreshers below
    // and refreshTakeGroups() above so the two sections never rebuild out
    // of step. Always leaves the box at selected id 0 afterward (see
    // onChange's own comment in the .cpp) rather than restoring any
    // particular selection - this selector never represents ongoing state,
    // only a one-shot action.
    void rebuildAudioInBox();
    void rebuildMidiInBox();
    void refreshAudioInputNames();
    void refreshMidiInputDevices();

    MasterChainProcessor& processor;
    juce::AudioDeviceManager& deviceManager;
    RecordingManager& recordingManager;

    juce::Label titleLabel;
    int titleDividerY = 0;
    bool inputSectionCollapsed = false;
    std::array<juce::TextButton, numSlots> slotButtons;
    std::unique_ptr<SlotButtonLookAndFeel> slotButtonLookAndFeel;

    // Bulk Bypass/Activate menu trigger (v0.9.8), sitting right above the
    // inserts it (partly) controls.
    juce::TextButton bypassMenuButton;

    // Bulk Audio In/MIDI In selectors (v0.9.8) - item ids follow
    // ChannelComponent's own convention exactly (None = 1, live entries =
    // 2.., Take-group entries = takeIdBase..): "None" broadcasts a clear to
    // every channel, a live entry broadcasts that input/device to every
    // channel, a Take-group entry applies (only to the channels actually
    // recorded in it) via RecordingManager::findChannelFileInTakeFolder().
    // See onAudioInputSelected/onMidiInputSelected above.
    std::unique_ptr<SelectorLookAndFeel> selectorLookAndFeel;
    juce::Label      audioInLabel;
    juce::ComboBox   audioInBox;
    juce::StringArray availableAudioInputNames;
    juce::Array<juce::File> availableAudioTakeGroups;

    juce::Label      midiInLabel;
    juce::ComboBox   midiInBox;
    juce::Array<juce::MidiDeviceInfo> availableMidiInputs;
    juce::Array<juce::File> availableMidiTakeGroups;
    juce::MidiDeviceListConnection midiDeviceListConnection;

    static constexpr int takeIdBase = 10000;

    std::unique_ptr<ConsoleFaderLookAndFeel> volumeFaderLookAndFeel;
    juce::Label      volumeLabel;
    juce::Slider     volumeSlider;
    PeakMeterComponent leftLevelMeter;
    PeakMeterComponent rightLevelMeter;

    juce::TextButton armButton;
    bool armed = false;
    void updateArmButton();

    juce::TextButton armAllButton;
    bool armAllState = false;
    void updateArmAllButton();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainComponent)
};
