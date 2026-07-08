#include "MasterChainComponent.h"

MasterChainComponent::MasterChainComponent(MasterChainProcessor& p)
    : processor(p)
{
    titleLabel.setText("Master FX", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(11.0f));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(titleLabel);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& button = slotButtons[(size_t) i];
        button.onClick = [this, i] { showPluginSlotMenu(i); };
        addAndMakeVisible(button);
        updateSlotButton(i);
    }

    // "Like the channels but just a bit bigger" - 1.5x the channel gain
    // fader's own (already 2x) cap size, on both axes.
    volumeFaderLookAndFeel = std::make_unique<ConsoleFaderLookAndFeel>(3.0f, 1.5f);

    volumeLabel.setText("Output", juce::dontSendNotification);
    volumeLabel.setFont(juce::Font(11.0f));
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(volumeLabel);

    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setRange(-60.0, 6.0, 0.1);
    volumeSlider.setValue(0.0, juce::dontSendNotification);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    volumeSlider.setTextValueSuffix(" dB");
    volumeSlider.setLookAndFeel(volumeFaderLookAndFeel.get());
    volumeSlider.onValueChange = [this]
    {
        if (onVolumeChanged)
            onVolumeChanged(juce::Decibels::decibelsToGain((float) volumeSlider.getValue(), -60.0f));
    };
    addAndMakeVisible(volumeSlider);

    addAndMakeVisible(leftLevelMeter);
    addAndMakeVisible(rightLevelMeter);
}

MasterChainComponent::~MasterChainComponent()
{
    volumeSlider.setLookAndFeel(nullptr);
}

void MasterChainComponent::setVolume(float linearGain)
{
    volumeSlider.setValue(juce::Decibels::gainToDecibels(linearGain, -60.0f),
                          juce::dontSendNotification);
}

void MasterChainComponent::setLevelMeterSources(const std::atomic<float>* leftLevel,
                                                const std::atomic<float>* rightLevel,
                                                std::atomic<bool>* clipFlagLeft,
                                                std::atomic<bool>* clipFlagRight)
{
    leftLevelMeter.setMonoSource(leftLevel, clipFlagLeft);
    rightLevelMeter.setMonoSource(rightLevel, clipFlagRight);
}

void MasterChainComponent::showPluginSlotMenu(int slotIndex)
{
    juce::PopupMenu menu;

    if (! processor.hasPlugin(slotIndex))
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
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions::makeOptionsOkCancel(
                            juce::MessageBoxIconType::WarningIcon,
                            "Replace Plugin",
                            "Replace the plugin loaded in this slot? Its current state will be lost.",
                            "Replace", "Cancel", this),
                        [this, slotIndex](int confirmResult)
                        {
                            if (confirmResult == 1 && onReplacePlugin)
                                onReplacePlugin(slotIndex);
                        });
                    break;
                case 4:
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions::makeOptionsOkCancel(
                            juce::MessageBoxIconType::WarningIcon,
                            "Remove Plugin",
                            "Remove the plugin loaded in this slot? Its current state will be lost.",
                            "Remove", "Cancel", this),
                        [this, slotIndex](int confirmResult)
                        {
                            if (confirmResult == 1)
                            {
                                processor.unloadPlugin(slotIndex);
                                updateSlotButton(slotIndex);
                                if (onDirty) onDirty();
                            }
                        });
                    break;
                case 5:
                    processor.setBypassed(slotIndex, ! processor.isBypassed(slotIndex));
                    updateSlotButton(slotIndex);
                    if (onDirty) onDirty();
                    break;
                default:
                    break;
            }
        });
}

void MasterChainComponent::updateSlotButton(int slotIndex)
{
    auto& button = slotButtons[(size_t) slotIndex];
    juce::String prefix = juce::String(slotIndex + 1) + ". ";

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

void MasterChainComponent::refresh()
{
    for (int i = 0; i < numSlots; ++i)
        updateSlotButton(i);
}

void MasterChainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6.0f, 1.5f);
}

void MasterChainComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    titleLabel.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);

    for (int i = 0; i < numSlots; ++i)
    {
        slotButtons[(size_t) i].setBounds(area.removeFromTop(22));
        if (i != numSlots - 1)
            area.removeFromTop(3);
    }
    area.removeFromTop(16);

    auto leftMeterArea = area.removeFromLeft(16);
    area.removeFromLeft(4);
    leftLevelMeter.setBounds(leftMeterArea);

    auto rightMeterArea = area.removeFromRight(16);
    area.removeFromRight(4);
    rightLevelMeter.setBounds(rightMeterArea);

    volumeLabel.setBounds(area.removeFromTop(16));
    volumeSlider.setBounds(area);
}
