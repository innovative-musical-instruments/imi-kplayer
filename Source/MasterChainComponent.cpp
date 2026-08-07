#include "MasterChainComponent.h"
#include <cmath>

MasterChainComponent::MasterChainComponent(MasterChainProcessor& p)
    : processor(p)
{
    titleLabel.setText("Master Section", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(11.0f));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(titleLabel);

    slotButtonLookAndFeel = std::make_unique<SlotButtonLookAndFeel>();
    for (int i = 0; i < numSlots; ++i)
    {
        auto& button = slotButtons[(size_t) i];
        button.onClick = [this, i] { showPluginSlotMenu(i); };
        button.setLookAndFeel(slotButtonLookAndFeel.get());
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
    // Same quadratic taper shape as the channel gain fader, scaled to this
    // slider's own +6/-60dB range: dB(t) = 6 - 66*t^2, t = fraction of
    // travel down from the top. See
    // docs/KPlayer_Refinement_Spec_2026-07-11.md section 1.3.
    juce::NormalisableRange<double> volumeRange(
        -60.0, 6.0,
        [](double start, double end, double normalised)
        {
            double t = 1.0 - normalised;
            return end - (end - start) * t * t;
        },
        [](double start, double end, double value)
        {
            double t = std::sqrt((end - value) / (end - start));
            return 1.0 - t;
        });
    volumeRange.interval = 0.1;
    volumeSlider.setNormalisableRange(volumeRange);
    volumeSlider.setValue(0.0, juce::dontSendNotification);
    volumeSlider.setDoubleClickReturnValue(true, 0.0);
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

    armButton.setButtonText("ARM");
    armButton.onClick = [this]
    {
        armed = ! armed;
        updateArmAndRecordButtons();
        if (onMasterArmToggled) onMasterArmToggled(armed);
    };
    addAndMakeVisible(armButton);

    recordButton.setButtonText(juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC");
    recordButton.onClick = [this] { if (onRecordButtonClicked) onRecordButtonClicked(); };
    addAndMakeVisible(recordButton);

    updateArmAndRecordButtons();

    // Icons are vector-drawn by TransportButtonLookAndFeel rather than
    // Unicode glyphs (see that class's header comment for why) - the button
    // text below is a sentinel it dispatches on, not shown to the user.
    transportButtonLookAndFeel = std::make_unique<TransportButtonLookAndFeel>();

    playPauseButton.onClick = [this]
    {
        transportPlaying = ! transportPlaying;
        updateTransportButtons();
        if (onPlayPauseClicked) onPlayPauseClicked();
    };
    playPauseButton.setLookAndFeel(transportButtonLookAndFeel.get());
    addAndMakeVisible(playPauseButton);

    rtzButton.setButtonText("RTZ");
    rtzButton.setTooltip("Return to zero");
    rtzButton.onClick = [this] { if (onRtzClicked) onRtzClicked(); };
    rtzButton.setLookAndFeel(transportButtonLookAndFeel.get());
    addAndMakeVisible(rtzButton);

    updateTransportButtons();

    panicButton.setButtonText("PANIC");
    panicButton.setTooltip("All notes off / all sound off - immediately silences every loaded instrument");
    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7a2020));
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicButton.onClick = [this] { if (onPanicClicked) onPanicClicked(); };
    addAndMakeVisible(panicButton);
}

MasterChainComponent::~MasterChainComponent()
{
    volumeSlider.setLookAndFeel(nullptr);
    for (auto& button : slotButtons)
        button.setLookAndFeel(nullptr);
    playPauseButton.setLookAndFeel(nullptr);
    rtzButton.setLookAndFeel(nullptr);
}

void MasterChainComponent::setVolume(float linearGain)
{
    volumeSlider.setValue(juce::Decibels::gainToDecibels(linearGain, -60.0f),
                          juce::dontSendNotification);
}

void MasterChainComponent::setArmed(bool shouldBeArmed)
{
    armed = shouldBeArmed;
    updateArmAndRecordButtons();
}

void MasterChainComponent::setRecordingActive(bool active)
{
    recordingActive = active;
    // Arming/unarming while active takes effect immediately (see
    // RecordingManager::setMasterArmed), same as a channel's own arm button.
    updateArmAndRecordButtons();
}

void MasterChainComponent::updateArmAndRecordButtons()
{
    armButton.setColour(juce::TextButton::buttonColourId,
                        armed ? juce::Colour(0xff7a2020) : juce::Colour(0xff2a2a3e));
    armButton.setColour(juce::TextButton::textColourOffId,
                        armed ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    armButton.setTooltip(armed ? "Master is armed to record" : "Arm master output for recording");

    recordButton.setColour(juce::TextButton::buttonColourId,
                           recordingActive ? juce::Colours::red : juce::Colour(0xff2a2a3e));
    recordButton.setColour(juce::TextButton::textColourOffId,
                           recordingActive ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    recordButton.setTooltip(recordingActive ? "Stop recording" : "Start recording every armed channel/master");
}

void MasterChainComponent::updateTransportButtons()
{
    // Sentinel text TransportButtonLookAndFeel dispatches on - see its
    // header comment.
    playPauseButton.setButtonText(transportPlaying ? "PAUSE" : "PLAY");
    playPauseButton.setColour(juce::TextButton::buttonColourId,
                              transportPlaying ? juce::Colour(0xff2a6b3d) : juce::Colour(0xff2a2a3e));
    playPauseButton.setColour(juce::TextButton::textColourOffId,
                              transportPlaying ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    playPauseButton.setTooltip(transportPlaying ? "Pause MIDI Take playback" : "Play MIDI Take playback");
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
    // Empty slot: "Load Plugin..." was the only item in the menu anyway -
    // skip straight to the plugin browser instead of making the user click
    // through a one-item popup first.
    if (! processor.hasPlugin(slotIndex))
    {
        if (onLoadPlugin) onLoadPlugin(slotIndex);
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(0, processor.getPluginName(slotIndex), false, false);
    menu.addSeparator();
    menu.addItem(2, processor.isEditorVisible(slotIndex) ? "Hide Plugin" : "Show Plugin");
    menu.addItem(5, processor.isBypassed(slotIndex) ? "Un-bypass" : "Bypass");
    menu.addSeparator();
    menu.addItem(3, "Replace Plugin...");
    menu.addSeparator();
    menu.addItem(4, "Remove Plugin");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&slotButtons[(size_t) slotIndex]),
        [this, slotIndex](int result)
        {
            switch (result)
            {
                case 2:
                    if (processor.isEditorVisible(slotIndex))
                        processor.hideEditor(slotIndex);
                    else
                        processor.showEditor(slotIndex);
                    break;
                case 3:
                    if (onReplacePlugin)
                        onReplacePlugin(slotIndex);
                    break;
                case 4:
                    processor.unloadPlugin(slotIndex);
                    updateSlotButton(slotIndex);
                    if (onDirty) onDirty();
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
        button.setButtonText(prefix.trim());
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

    // ---- Panic: full-width, always the bottommost control - most
    // emergency-critical action gets the least ambiguous target, and stays
    // reachable regardless of channel-rack scroll position (master column
    // is fixed, like collapseInputButton).
    panicButton.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(6);

    // ---- Record: arm + global transport, below the fader/meters. Carved
    // from the bottom of what's left, shrinking the fader/meters below to
    // accommodate (per-request - there was no free space here otherwise).
    auto recordArea = area.removeFromBottom(24);
    area.removeFromBottom(6);
    armButton.setBounds(recordArea.removeFromLeft(recordArea.getWidth() / 2).reduced(2, 0));
    recordButton.setBounds(recordArea.reduced(2, 0));

    // ---- MIDI Take playback transport (Increment B) - Play/Pause + RTZ,
    // just above the arm/record row.
    auto transportArea = area.removeFromBottom(24);
    area.removeFromBottom(6);
    playPauseButton.setBounds(transportArea.removeFromLeft(transportArea.getWidth() / 2).reduced(2, 0));
    rtzButton.setBounds(transportArea.reduced(2, 0));

    auto leftMeterArea = area.removeFromLeft(16);
    area.removeFromLeft(4);
    leftLevelMeter.setBounds(leftMeterArea);

    auto rightMeterArea = area.removeFromRight(16);
    area.removeFromRight(4);
    rightLevelMeter.setBounds(rightMeterArea);

    volumeLabel.setBounds(area.removeFromTop(16));
    volumeSlider.setBounds(area);
}
