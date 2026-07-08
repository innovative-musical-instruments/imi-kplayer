#pragma once
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"

class ChannelComponent : public juce::Component,
                         public juce::Slider::Listener,
                         public juce::ComboBox::Listener,
                         public juce::ChangeListener
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

    // deviceManager is needed to enumerate active audio input channels for
    // the audio-input selector (Increment 3 item 8) and to be notified when
    // the active device/channel set changes (AudioDeviceManager is a
    // ChangeBroadcaster) - mirrors why SettingsComponent takes one too.
    ChannelComponent(ChannelProcessor& processor, juce::AudioDeviceManager& deviceManager);
    ~ChannelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* combo) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void refreshMidiDeviceList();
    void updateMidiDeviceWarning();
    void refreshAudioInputList();
    void updateAudioInputWarning();

    ChannelProcessor& processor;
    juce::AudioDeviceManager& deviceManager;

    std::unique_ptr<ConsoleFaderLookAndFeel> gainFaderLookAndFeel;
    std::unique_ptr<ConsoleFaderLookAndFeel> panFaderLookAndFeel;

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

    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};
