#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelProcessor.h"

class ChannelComponent : public juce::Component,
                         public juce::Slider::Listener,
                         public juce::ComboBox::Listener
{
public:
    std::function<void()> onLoadPlugin;
    std::function<void()> onReplacePlugin;

    explicit ChannelComponent(ChannelProcessor& processor);
    ~ChannelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* combo) override;

private:
    ChannelProcessor& processor;

    juce::TextButton pluginSlotButton;
    juce::ComboBox   midiChannelBox;
    juce::Slider     gainSlider;
    juce::Slider     panSlider;
    juce::Label      gainLabel;
    juce::Label      panLabel;
    juce::Label      midiLabel;
    juce::Label      pluginLabel;

    void showPluginSlotMenu();
    void updateSlotButton();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};
