#include "ChannelComponent.h"

ChannelComponent::ChannelComponent(ChannelProcessor& p)
    : processor(p)
{
    pluginLabel.setText("Instrument", juce::dontSendNotification);
    pluginLabel.setFont(juce::Font(11.0f));
    pluginLabel.setJustificationType(juce::Justification::centredLeft);
    pluginLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(pluginLabel);

    pluginSlotButton.setButtonText("- empty -");
    pluginSlotButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a3e));
    pluginSlotButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    pluginSlotButton.onClick = [this] { showPluginSlotMenu(); };
    addAndMakeVisible(pluginSlotButton);

    midiLabel.setText("MIDI Ch", juce::dontSendNotification);
    midiLabel.setFont(juce::Font(11.0f));
    midiLabel.setJustificationType(juce::Justification::centredLeft);
    midiLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(midiLabel);

    midiChannelBox.addItem("All", 1);
    for (int i = 1; i <= 16; ++i)
        midiChannelBox.addItem(juce::String(i), i + 1);
    midiChannelBox.setSelectedId(1);
    midiChannelBox.addListener(this);
    addAndMakeVisible(midiChannelBox);

    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setFont(juce::Font(11.0f));
    gainLabel.setJustificationType(juce::Justification::centred);
    gainLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(gainLabel);

    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider.setRange(-96.0, 0.0, 0.1);
    gainSlider.setValue(0.0);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    gainSlider.setTextValueSuffix(" dB");
    gainSlider.addListener(this);
    addAndMakeVisible(gainSlider);

    panLabel.setText("Pan", juce::dontSendNotification);
    panLabel.setFont(juce::Font(11.0f));
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(panLabel);

    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    panSlider.addListener(this);
    addAndMakeVisible(panSlider);

    setSize(160, 500);
}

ChannelComponent::~ChannelComponent() {}

void ChannelComponent::showPluginSlotMenu()
{
    juce::PopupMenu menu;

    if (!processor.hasPlugin())
    {
        menu.addItem(1, "Load Plugin...");
    }
    else
    {
        menu.addItem(0, processor.getPluginName(), false, false);
        menu.addSeparator();
        menu.addItem(2, processor.isEditorVisible() ? "Hide Plugin" : "Show Plugin");
        menu.addSeparator();
        menu.addItem(3, "Replace Plugin...");
        menu.addSeparator();
        menu.addItem(4, "Remove Plugin");
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&pluginSlotButton),
        [this](int result)
        {
            switch (result)
            {
                case 1:
                    if (onLoadPlugin) onLoadPlugin();
                    break;
                case 2:
                    if (processor.isEditorVisible())
                        processor.hideEditor();
                    else
                        processor.showEditor();
                    break;
                case 3:
                    if (onReplacePlugin) onReplacePlugin();
                    break;
                case 4:
                    processor.unloadPlugin();
                    updateSlotButton();
                    break;
                default:
                    break;
            }
        });
}

void ChannelComponent::updateSlotButton()
{
    if (processor.hasPlugin())
    {
        pluginSlotButton.setButtonText(processor.getPluginName());
        pluginSlotButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(0xff3d5a80));
        pluginSlotButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colours::white);
    }
    else
    {
        pluginSlotButton.setButtonText("- empty -");
        pluginSlotButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(0xff2a2a3e));
        pluginSlotButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colour(0xffaaaaaa));
    }
}

void ChannelComponent::refresh()
{
    updateSlotButton();
}

void ChannelComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &gainSlider)
    {
        float dB     = (float)gainSlider.getValue();
        float linear = (dB <= -96.0f) ? 0.0f : juce::Decibels::decibelsToGain(dB);
        processor.setGain(linear);
    }
    else if (slider == &panSlider)
    {
        processor.setPan((float)panSlider.getValue());
    }
}

void ChannelComponent::comboBoxChanged(juce::ComboBox* combo)
{
    if (combo == &midiChannelBox)
    {
        int selected = midiChannelBox.getSelectedId();
        processor.setMidiChannel(selected <= 1 ? 0 : selected - 1);
    }
}

void ChannelComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6.0f, 1.5f);
}

void ChannelComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    pluginLabel.setBounds(area.removeFromTop(16));
    pluginSlotButton.setBounds(area.removeFromTop(28));
    area.removeFromTop(12);

    midiLabel.setBounds(area.removeFromTop(16));
    midiChannelBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(16);

    auto bottomArea = area.removeFromBottom(100);
    auto panArea    = bottomArea.removeFromRight(bottomArea.getWidth() / 2);

    panLabel.setBounds(panArea.removeFromTop(16));
    panSlider.setBounds(panArea);

    gainLabel.setBounds(area.removeFromTop(16));
    gainSlider.setBounds(area);
}
