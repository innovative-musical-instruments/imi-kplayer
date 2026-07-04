#include "ChannelComponent.h"

namespace
{
    // Draws gain/pan as a console-mixer-style fader: a fully-highlighted
    // groove (the highlight only ever indicated the *filled* portion in
    // the default LookAndFeel, which reads as a level meter rather than a
    // fader position) plus a bigger rectangular cap with a centre grip line.
    class ConsoleFaderLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            auto trackColour = juce::Colour(0xff3d5a80);
            auto capColour   = juce::Colours::white;
            auto capBorder   = juce::Colours::black.withAlpha(0.6f);

            // Shared cross-axis size so the gain cap's width matches the
            // pan cap's height, rather than each stretching to fill its
            // own (very differently proportioned) slider bounds. Capped
            // to fit within the pan slider's actual drawable height (its
            // bounds minus the text box below it), since a cap taller
            // than that overflows/clips.
            const float crossAxisSize = 26.0f;

            if (style == juce::Slider::LinearVertical)
            {
                const float trackWidth = 6.0f;
                juce::Rectangle<float> track(x + width * 0.5f - trackWidth * 0.5f,
                                             (float) y, trackWidth, (float) height);
                g.setColour(trackColour);
                g.fillRoundedRectangle(track, trackWidth * 0.5f);

                const float capHeight = 20.0f;
                const float capWidth  = crossAxisSize;
                juce::Rectangle<float> cap(x + ((float) width - capWidth) * 0.5f,
                                          sliderPos - capHeight * 0.5f, capWidth, capHeight);
                g.setColour(capColour);
                g.fillRoundedRectangle(cap, 3.0f);
                g.setColour(capBorder);
                g.drawRoundedRectangle(cap, 3.0f, 1.0f);
                g.drawLine(cap.getX() + 4, cap.getCentreY(), cap.getRight() - 4, cap.getCentreY(), 2.0f);
            }
            else if (style == juce::Slider::LinearHorizontal)
            {
                const float trackHeight = 6.0f;
                juce::Rectangle<float> track((float) x, y + height * 0.5f - trackHeight * 0.5f,
                                             (float) width, trackHeight);
                g.setColour(trackColour);
                g.fillRoundedRectangle(track, trackHeight * 0.5f);

                const float capWidth  = 20.0f;
                const float capHeight = crossAxisSize;
                juce::Rectangle<float> cap(sliderPos - capWidth * 0.5f,
                                          y + ((float) height - capHeight) * 0.5f, capWidth, capHeight);
                g.setColour(capColour);
                g.fillRoundedRectangle(cap, 3.0f);
                g.setColour(capBorder);
                g.drawRoundedRectangle(cap, 3.0f, 1.0f);
                g.drawLine(cap.getCentreX(), cap.getY() + 4, cap.getCentreX(), cap.getBottom() - 4, 2.0f);
            }
            else
            {
                LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                 0.0f, 0.0f, style, slider);
            }
        }
    };
}

ChannelComponent::ChannelComponent(ChannelProcessor& p)
    : processor(p)
{
    faderLookAndFeel = std::make_unique<ConsoleFaderLookAndFeel>();

    channelNameLabel.setText(processor.getName(), juce::dontSendNotification);
    channelNameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    channelNameLabel.setJustificationType(juce::Justification::centred);
    channelNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(channelNameLabel);

    pluginLabel.setText("Instrument", juce::dontSendNotification);
    pluginLabel.setFont(juce::Font(11.0f));
    pluginLabel.setJustificationType(juce::Justification::centredLeft);
    pluginLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(pluginLabel);

    insertsLabel.setText("Inserts", juce::dontSendNotification);
    insertsLabel.setFont(juce::Font(11.0f));
    insertsLabel.setJustificationType(juce::Justification::centredLeft);
    insertsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(insertsLabel);

    for (int i = 0; i < totalSlots; ++i)
    {
        auto& button = slotButtons[(size_t) i];
        button.onClick = [this, i] { showPluginSlotMenu(i); };
        addAndMakeVisible(button);
        updateSlotButton(i);
    }

    midiInLabel.setText("MIDI In", juce::dontSendNotification);
    midiInLabel.setFont(juce::Font(11.0f));
    midiInLabel.setJustificationType(juce::Justification::centredLeft);
    midiInLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(midiInLabel);

    availableMidiInputs = juce::MidiInput::getAvailableDevices();
    midiDeviceBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        midiDeviceBox.addItem(availableMidiInputs[i].name, i + 2);
    midiDeviceBox.setSelectedId(1);
    midiDeviceBox.addListener(this);
    addAndMakeVisible(midiDeviceBox);

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
    gainSlider.setLookAndFeel(faderLookAndFeel.get());
    addAndMakeVisible(gainSlider);

    panLabel.setText("Pan", juce::dontSendNotification);
    panLabel.setFont(juce::Font(11.0f));
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(panLabel);

    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    panSlider.addListener(this);
    panSlider.setLookAndFeel(faderLookAndFeel.get());
    addAndMakeVisible(panSlider);

    muteButton.setButtonText("Mute");
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    muteButton.onClick = [this] { processor.setMuted(muteButton.getToggleState()); };
    addAndMakeVisible(muteButton);

    soloButton.setButtonText("Solo");
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.onClick = [this] { processor.setSolo(soloButton.getToggleState()); };
    addAndMakeVisible(soloButton);

    setSize(160, 700);
}

ChannelComponent::~ChannelComponent()
{
    gainSlider.setLookAndFeel(nullptr);
    panSlider.setLookAndFeel(nullptr);
}

void ChannelComponent::showPluginSlotMenu(int slotIndex)
{
    juce::PopupMenu menu;

    if (!processor.hasPlugin(slotIndex))
    {
        menu.addItem(1, "Load Plugin...");
    }
    else
    {
        menu.addItem(0, processor.getPluginName(slotIndex), false, false);
        menu.addSeparator();
        menu.addItem(2, processor.isEditorVisible(slotIndex) ? "Hide Plugin" : "Show Plugin");
        menu.addItem(5, processor.isBypassed(slotIndex) ? "Un-bypass" : "Bypass");
        menu.addSeparator();
        menu.addItem(3, "Replace Plugin...");
        menu.addSeparator();
        menu.addItem(4, "Remove Plugin");
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&slotButtons[(size_t) slotIndex]),
        [this, slotIndex](int result)
        {
            switch (result)
            {
                case 1:
                    if (onLoadPlugin) onLoadPlugin(slotIndex);
                    break;
                case 2:
                    if (processor.isEditorVisible(slotIndex))
                        processor.hideEditor(slotIndex);
                    else
                        processor.showEditor(slotIndex);
                    break;
                case 3:
                    if (onReplacePlugin) onReplacePlugin(slotIndex);
                    break;
                case 4:
                    processor.unloadPlugin(slotIndex);
                    updateSlotButton(slotIndex);
                    break;
                case 5:
                    processor.setBypassed(slotIndex, ! processor.isBypassed(slotIndex));
                    updateSlotButton(slotIndex);
                    break;
                default:
                    break;
            }
        });
}

void ChannelComponent::updateSlotButton(int slotIndex)
{
    auto& button = slotButtons[(size_t) slotIndex];
    juce::String prefix = slotIndex == 0 ? juce::String() : (juce::String(slotIndex) + ". ");

    if (processor.hasPlugin(slotIndex))
    {
        button.setButtonText(prefix + processor.getPluginName(slotIndex));
        button.setColour(juce::TextButton::buttonColourId,
                          processor.isBypassed(slotIndex) ? juce::Colour(0xff4a4a5c)
                                                           : juce::Colour(0xff3d5a80));
        button.setColour(juce::TextButton::textColourOffId,
                          processor.isBypassed(slotIndex) ? juce::Colour(0xffaaaaaa)
                                                           : juce::Colours::white);
    }
    else
    {
        button.setButtonText(prefix + "- empty -");
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3e));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    }
}

void ChannelComponent::refresh()
{
    for (int i = 0; i < totalSlots; ++i)
        updateSlotButton(i);
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
    else if (combo == &midiDeviceBox)
    {
        int selected = midiDeviceBox.getSelectedId();
        if (selected <= 1)
        {
            processor.setMidiDeviceIdentifier({});
        }
        else
        {
            int index = selected - 2;
            if (index >= 0 && index < availableMidiInputs.size())
                processor.setMidiDeviceIdentifier(availableMidiInputs[index].identifier);
        }
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

    channelNameLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    pluginLabel.setBounds(area.removeFromTop(16));
    slotButtons[0].setBounds(area.removeFromTop(26));
    area.removeFromTop(10);

    insertsLabel.setBounds(area.removeFromTop(16));
    for (int i = 1; i < totalSlots; ++i)
    {
        slotButtons[(size_t) i].setBounds(area.removeFromTop(22));
        if (i != totalSlots - 1)
            area.removeFromTop(3);
    }
    area.removeFromTop(12);

    midiInLabel.setBounds(area.removeFromTop(16));
    midiDeviceBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    midiLabel.setBounds(area.removeFromTop(16));
    midiChannelBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(16);

    auto bottomArea = area.removeFromBottom(140);

    gainLabel.setBounds(area.removeFromTop(16));
    gainSlider.setBounds(area);

    bottomArea.removeFromTop(20);
    panLabel.setBounds(bottomArea.removeFromTop(16));
    panSlider.setBounds(bottomArea.removeFromTop(44));

    bottomArea.removeFromTop(8);
    auto muteSoloArea = bottomArea.removeFromTop(24);
    muteButton.setBounds(muteSoloArea.removeFromLeft(muteSoloArea.getWidth() / 2).reduced(4, 0));
    soloButton.setBounds(muteSoloArea.reduced(4, 0));
}
