#pragma once
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"
#include "SelectorLookAndFeel.h"

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
    // toggle. The toggle is disabled while recording is active - arm
    // selection only applies to the *next* take, not the one in progress.
    std::function<void(bool armed)> onArmToggled;

    // deviceManager is needed to enumerate active audio input channels for
    // the audio-input selector (Increment 3 item 8) and to be notified when
    // the active device/channel set changes (AudioDeviceManager is a
    // ChangeBroadcaster) - mirrors why SettingsComponent takes one too.
    // channelNumber is the fixed, 1-based, non-editable position shown as
    // "Channel N" (or the "N." prefix once a custom name is set, item 1.1)
    // - derived from the channel's position in MainComponent's vectors,
    // which channels are only ever appended to or truncated from the tail
    // of, so a given channel's number never changes after construction.
    ChannelComponent(ChannelProcessor& processor, juce::AudioDeviceManager& deviceManager, int channelNumber);
    ~ChannelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Vertical offset (from a channel strip's own top edge) at which the
    // first *insert* slot button begins (i.e. slotButtons[1], right after
    // the "Inserts" label - not slot 0, the instrument slot, which the
    // master chain has no equivalent of). MainComponent uses this to line
    // up the master chain's own insert slots with the channels' - only
    // meaningful for the non-collapsed input section (see MainComponent's
    // caller for why). Must be kept in sync with resized()'s row math below
    // if either changes.
    static int insertSectionStartY(bool inputSectionCollapsed);

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
    // Shared by the constructor and refreshMidiDeviceList(): the midiDeviceBox
    // item id (1 = "None", 2.. = availableMidiInputs index) matching a given
    // device identifier, or 1 if it's empty/not found.
    int midiDeviceItemIdFor(const juce::String& identifier) const;
    void updateMidiDeviceWarning();
    void refreshAudioInputList();
    void updateAudioInputWarning();
    void updateChannelNameLabel();

    ChannelProcessor& processor;
    juce::AudioDeviceManager& deviceManager;
    int channelNumber = 1;
    bool inputCollapsed = false;

    std::unique_ptr<ConsoleFaderLookAndFeel> gainFaderLookAndFeel;
    std::unique_ptr<ConsoleFaderLookAndFeel> panFaderLookAndFeel;
    std::unique_ptr<SelectorLookAndFeel> selectorLookAndFeel;

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

    juce::Label      midiInLabel;
    juce::ComboBox   midiDeviceBox;
    juce::Array<juce::MidiDeviceInfo> availableMidiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;

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
