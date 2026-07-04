#pragma once
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelProcessor.h"

class ChannelComponent : public juce::Component,
                         public juce::Slider::Listener,
                         public juce::ComboBox::Listener
{
public:
    static constexpr int totalSlots = ChannelProcessor::totalSlotCount;

    // Called with the slot index (0 = instrument/effect slot, 1-5 = inserts)
    std::function<void(int slotIndex)> onLoadPlugin;
    std::function<void(int slotIndex)> onReplacePlugin;

    explicit ChannelComponent(ChannelProcessor& processor);
    ~ChannelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* combo) override;

private:
    void refreshMidiDeviceList();
    void updateMidiDeviceWarning();

    ChannelProcessor& processor;

    std::unique_ptr<juce::LookAndFeel_V4> faderLookAndFeel;

    juce::Label channelNameLabel;
    juce::Label pluginLabel;
    juce::Label insertsLabel;
    std::array<juce::TextButton, totalSlots> slotButtons;

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

    juce::TextButton muteButton;
    juce::TextButton soloButton;

    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};
