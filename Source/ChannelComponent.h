#pragma once
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"
#include "SelectorLookAndFeel.h"
#include "SlotButtonLookAndFeel.h"
#include "RecordingManager.h"

class ChannelComponent : public juce::Component,
                         public juce::Slider::Listener,
                         public juce::ComboBox::Listener,
                         public juce::ChangeListener,
                         public juce::Label::Listener
{
public:
    static constexpr int totalSlots = ChannelProcessor::totalSlotCount;

    // Called with the slot index (0 = instrument/effect slot, 1-5 = inserts)
    std::function<void(int slotIndex)> onLoadPlugin;
    std::function<void(int slotIndex)> onReplacePlugin;

    // Fired for every user-driven structural/parameter change made directly
    // in this component (gain/pan/mute/solo/MIDI routing, remove/bypass).
    // Load and replace are reported by the owner instead, since those only
    // complete once MainComponent's plugin browser callback runs.
    std::function<void()> onDirty;

    // Multitrack recording (see RecordingManager) - MainComponent owns the
    // arm/recording-active truth; this component only reflects it via
    // setArmed()/setRecordingActive() and reports the user clicking the arm
    // toggle. The toggle stays clickable while recording is active - arming
    // mid-take takes effect immediately (RecordingManager::setChannelArmed
    // starts a new, silence-padded file segment from that point on rather
    // than rejecting the click).
    std::function<void(bool armed)> onArmToggled;

    // MIDI Take playback (Increment B, see
    // docs/kplayer-take-recording-playback-spec.md): fired when the user
    // picks/leaves a "Recorded Takes" entry in the MIDI Input Selector.
    // ChannelComponent itself has no reference to MainComponent's
    // per-channel MidiTakePlayer array, so loading/unloading the actual
    // player is MainComponent's job - this just reports the user's choice,
    // same shape as onArmToggled above. setMidiDeviceIdentifier() is still
    // called directly by this component either way (see comboBoxChanged),
    // matching how a live-device selection is handled.
    std::function<void(const juce::File& takeFile)> onMidiTakeSelected;
    std::function<void()> onMidiTakeDeselected;

    // Audio Take playback (Increment C) - same shape as
    // onMidiTakeSelected/onMidiTakeDeselected above, but for the Audio
    // Input Selector's own "Recorded Takes" section.
    // setAudioTakeIdentifier()/setAudioInputChannelIndex() are still called
    // directly by this component either way (see comboBoxChanged).
    std::function<void(const juce::File& takeFile)> onAudioTakeSelected;
    std::function<void()> onAudioTakeDeselected;

    // deviceManager is needed to enumerate active audio input channels for
    // the audio-input selector (Increment 3 item 8) and to be notified when
    // the active device/channel set changes (AudioDeviceManager is a
    // ChangeBroadcaster) - mirrors why SettingsComponent takes one too.
    // recordingManager is needed to enumerate this channel's own recorded
    // MIDI Takes (RecordingManager::findChannelMidiTakes) for the same
    // selector's "Recorded Takes" section, and to encode/decode the take:
    // identifiers stored via ChannelProcessor::setMidiDeviceIdentifier - see
    // RecordingManager::encodeTakeIdentifier/decodeTakeIdentifier.
    // channelNumber is the fixed, 1-based, non-editable position shown as
    // "Channel N" (or the "N." prefix once a custom name is set, item 1.1)
    // - derived from the channel's position in MainComponent's vectors,
    // which channels are only ever appended to or truncated from the tail
    // of, so a given channel's number never changes after construction.
    ChannelComponent(ChannelProcessor& processor, juce::AudioDeviceManager& deviceManager,
                     RecordingManager& recordingManager, int channelNumber);
    ~ChannelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Re-scans this channel's Takes from disk and rebuilds the MIDI Input
    // Selector's "Recorded Takes" section - called by MainComponent right
    // after a recording finishes (new Take files may now exist). Safe to
    // call at any time otherwise, same as refreshMidiDeviceList().
    void refreshTakeList();

    // Same as refreshTakeList() above, but for the Audio Input Selector's
    // "Recorded Takes" section (Increment C).
    void refreshAudioTakeList();

    // Global collapse toggle (all channels move together, driven from
    // MainComponent) - hides the Audio In/MIDI In/MIDI Ch rows, leaving the
    // channel name visible. Purely a view preference, not session state.
    void setInputSectionCollapsed(bool collapsed);

    void setArmed(bool shouldBeArmed);
    void setRecordingActive(bool active);

    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* combo) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void labelTextChanged(juce::Label* label) override;
    void editorShown(juce::Label* label, juce::TextEditor& editor) override;

private:
    void refreshMidiDeviceList();
    // Rebuilds midiDeviceBox from scratch (None + live devices + the
    // Recorded Takes section) and restores the selection matching
    // processor.getMidiDeviceIdentifier() - the one place that touches the
    // combo box's contents, called by both refreshMidiDeviceList() (live
    // device hotplug) and refreshTakeList() (new Take files on disk) so the
    // two sections never get rebuilt out of step with each other.
    void rebuildMidiInputBox();
    // Item id for a given identifier - live devices are 2..availableMidiInputs.size()+1,
    // Take entries are takeIdBase..takeIdBase+availableChannelTakes.size()-1,
    // "None"/not-found is 1.
    int midiDeviceItemIdFor(const juce::String& identifier) const;
    void updateMidiDeviceWarning();
    void refreshAudioInputList();
    // Same rebuild-from-scratch treatment as rebuildMidiInputBox() above,
    // for audioInputBox (None + live hardware channels + Recorded Takes) -
    // called by both refreshAudioInputList() (device hotplug) and
    // refreshAudioTakeList() (new Take files on disk).
    void rebuildAudioInputBox();
    // Item id for a given (channelIndex, takeIdentifier) selection state -
    // live hardware channels are 2..availableAudioInputNames.size()+1
    // (keyed by audioInputChannelIndex, not a stable identifier, see
    // availableAudioInputNames' own comment below), Take entries are
    // takeIdBase..takeIdBase+availableChannelAudioTakes.size()-1 (keyed by
    // processor.getAudioTakeIdentifier()), "None"/not-found is 1.
    int audioInputItemIdFor(int channelIndex, const juce::String& takeIdentifier) const;
    void updateAudioInputWarning();
    void updateChannelNameLabel();

    ChannelProcessor& processor;
    juce::AudioDeviceManager& deviceManager;
    RecordingManager& recordingManager;
    int channelNumber = 1;
    bool inputCollapsed = false;

    std::unique_ptr<ConsoleFaderLookAndFeel> gainFaderLookAndFeel;
    std::unique_ptr<ConsoleFaderLookAndFeel> panFaderLookAndFeel;
    std::unique_ptr<SelectorLookAndFeel> selectorLookAndFeel;
    std::unique_ptr<SlotButtonLookAndFeel> slotButtonLookAndFeel;

    juce::Label channelNameLabel;
    juce::Label pluginLabel;
    juce::Label insertsLabel;
    std::array<juce::TextButton, totalSlots> slotButtons;

    // Slot-0-adjacent audio input selector (Increment 3 item 8). Values are
    // keyed by index into the device's *active* input channels (matching
    // audioDeviceIOCallbackWithContext's inputChannelData order) rather than
    // a stable identifier, since individual audio channels don't have one -
    // same simplification the versioning spec called out as acceptable for
    // this feature (no sidechain routing/bus detection needed).
    juce::Label    audioInLabel;
    juce::ComboBox audioInputBox;
    juce::StringArray availableAudioInputNames;

    // Audio Take entries in audioInputBox (Increment C) - item ids
    // takeIdBase..takeIdBase+availableChannelAudioTakes.size()-1, in the
    // same order as this array. See rebuildAudioInputBox().
    juce::Array<juce::File> availableChannelAudioTakes;

    juce::Label      midiInLabel;
    juce::ComboBox   midiDeviceBox;
    juce::Array<juce::MidiDeviceInfo> availableMidiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;

    // MIDI Take entries in midiDeviceBox (Increment B) - item ids
    // takeIdBase..takeIdBase+availableChannelTakes.size()-1, in the same
    // order as this array. See rebuildMidiInputBox().
    static constexpr int takeIdBase = 10000;
    juce::Array<juce::File> availableChannelTakes;

    juce::ComboBox   midiChannelBox;
    juce::Slider     gainSlider;
    juce::Slider     panSlider;
    juce::Label      gainLabel;
    juce::Label      panLabel;
    juce::Label      midiLabel;
    PeakMeterComponent levelMeter;

    juce::TextButton muteButton;
    juce::TextButton soloButton;

    juce::TextButton armButton;
    bool armed = false;
    bool recordingActive = false;
    void updateArmButton();

    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    // Y-coordinates of the Input/Plugins section divider lines, computed in
    // resized() and drawn in paint() - keeps the two in sync without
    // duplicating the layout math.
    int inputSectionDividerY = 0;
    int pluginsSectionDividerY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};
