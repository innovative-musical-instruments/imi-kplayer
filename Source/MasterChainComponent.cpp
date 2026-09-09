#include "MasterChainComponent.h"
#include "AudioInputUtils.h"
#include <cmath>

MasterChainComponent::MasterChainComponent(MasterChainProcessor& p, juce::AudioDeviceManager& dm,
                                           RecordingManager& rm)
    : processor(p), deviceManager(dm), recordingManager(rm)
{
    // Same look as each channel's own (editable) name heading -
    // ChannelComponent::channelNameLabel - just not editable here; the
    // master strip has nothing to rename.
    titleLabel.setText("Master Section", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
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

    // Bulk Bypass/Activate menu (v0.9.8) - see showBypassMenu(). Explicit
    // colours needed here (unlike the slot/ARM buttons, which get theirs
    // from a dedicated LookAndFeel or their own on/off state) since a
    // plain juce::TextButton otherwise falls back to LookAndFeel_V4's
    // default grey, which reads as visually foreign against the rest of
    // this dark-navy panel - same "off" colours armButton/armAllButton
    // already use, to match the selectors right above it.
    bypassMenuButton.setButtonText("Byp/Act.");
    bypassMenuButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3e));
    bypassMenuButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    bypassMenuButton.onClick = [this] { showBypassMenu(); };
    addAndMakeVisible(bypassMenuButton);

    // Bulk Audio In/MIDI In selectors (v0.9.8) - same look as each
    // channel's own selectors, just broadcasting to every channel at once
    // instead of setting one channel's own input. See onAudioInputSelected/
    // onMidiInputSelected's header comment for what "selecting" here means.
    selectorLookAndFeel = std::make_unique<SelectorLookAndFeel>();

    audioInLabel.setText("Audio In", juce::dontSendNotification);
    audioInLabel.setFont(juce::Font(11.0f));
    audioInLabel.setJustificationType(juce::Justification::centredLeft);
    audioInLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(audioInLabel);

    // Never shows a persisted selection (see setSelectedId(0, ...) at the
    // end of onChange below) - this is a one-shot "apply to every channel"
    // action, not a value that could represent N channels' potentially-
    // differing inputs. setTextWhenNothingSelected() is what displays here
    // whenever nothing is selected, i.e. essentially always.
    audioInBox.setTextWhenNothingSelected("Set All Audio In");
    availableAudioInputNames = AudioInputUtils::getActiveAudioInputChannelNames(deviceManager);
    availableAudioTakeGroups = recordingManager.findAudioTakeGroups();
    rebuildAudioInBox();
    audioInBox.onChange = [this]
    {
        int selected = audioInBox.getSelectedId();
        if (selected == 0)
            return; // our own setSelectedId(0, ...) reset below - not a user pick
        if (selected >= takeIdBase)
        {
            int index = selected - takeIdBase;
            if (index >= 0 && index < availableAudioTakeGroups.size())
                if (onAudioInputSelected) onAudioInputSelected(-1, availableAudioTakeGroups.getReference(index));
        }
        else
        {
            int channelIndex = selected <= 1 ? -1 : selected - 2;
            if (onAudioInputSelected) onAudioInputSelected(channelIndex, {});
        }
        audioInBox.setSelectedId(0, juce::dontSendNotification);
    };
    audioInBox.setLookAndFeel(selectorLookAndFeel.get());
    addAndMakeVisible(audioInBox);

    deviceManager.addChangeListener(this);

    midiInLabel.setText("MIDI In", juce::dontSendNotification);
    midiInLabel.setFont(juce::Font(11.0f));
    midiInLabel.setJustificationType(juce::Justification::centredLeft);
    midiInLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(midiInLabel);

    midiInBox.setTextWhenNothingSelected("Set All MIDI In");
    availableMidiInputs = juce::MidiInput::getAvailableDevices();
    availableMidiTakeGroups = recordingManager.findMidiTakeGroups();
    rebuildMidiInBox();
    midiInBox.onChange = [this]
    {
        int selected = midiInBox.getSelectedId();
        if (selected == 0)
            return;
        if (selected >= takeIdBase)
        {
            int index = selected - takeIdBase;
            if (index >= 0 && index < availableMidiTakeGroups.size())
                if (onMidiInputSelected) onMidiInputSelected({}, availableMidiTakeGroups.getReference(index));
        }
        else
        {
            juce::String deviceIdentifier;
            int index = selected - 2;
            if (selected > 1 && index >= 0 && index < availableMidiInputs.size())
                deviceIdentifier = availableMidiInputs[index].identifier;
            if (onMidiInputSelected) onMidiInputSelected(deviceIdentifier, {});
        }
        midiInBox.setSelectedId(0, juce::dontSendNotification);
    };
    midiInBox.setLookAndFeel(selectorLookAndFeel.get());
    addAndMakeVisible(midiInBox);

    // Same event-driven hotplug handling as ChannelComponent's own
    // midiDeviceListConnection - reflected immediately, not polled.
    midiDeviceListConnection = juce::MidiDeviceListConnection::make([this] { refreshMidiInputDevices(); });

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

    // Same dot glyph as a channel's own arm button (ChannelComponent) - now
    // that ARM shares its row with Arm All below, there isn't room left for
    // the spelled-out "ARM" label.
    armButton.setButtonText(juce::String::charToString((juce::juce_wchar) 0x25CF)); // "●"
    armButton.onClick = [this]
    {
        armed = ! armed;
        updateArmButton();
        if (onMasterArmToggled) onMasterArmToggled(armed);
    };
    addAndMakeVisible(armButton);

    // Arm All - a pure toggle-the-aggregate action, not a second piece of
    // independently-tracked state; MainComponent::toggleArmAll() decides
    // arm-everyone vs. unarm-everyone from the live truth at click time,
    // and setArmAllState() below is the only thing that ever changes how
    // this button looks.
    armAllButton.setButtonText("All");
    armAllButton.onClick = [this] { if (onArmAllClicked) onArmAllClicked(); };
    addAndMakeVisible(armAllButton);

    updateArmButton();
    updateArmAllButton();
}

MasterChainComponent::~MasterChainComponent()
{
    deviceManager.removeChangeListener(this);
    volumeSlider.setLookAndFeel(nullptr);
    audioInBox.setLookAndFeel(nullptr);
    midiInBox.setLookAndFeel(nullptr);
    for (auto& button : slotButtons)
        button.setLookAndFeel(nullptr);
}

void MasterChainComponent::setVolume(float linearGain)
{
    volumeSlider.setValue(juce::Decibels::gainToDecibels(linearGain, -60.0f),
                          juce::dontSendNotification);
}

void MasterChainComponent::setArmed(bool shouldBeArmed)
{
    armed = shouldBeArmed;
    updateArmButton();
}

void MasterChainComponent::setArmAllState(bool allArmed)
{
    armAllState = allArmed;
    updateArmAllButton();
}

void MasterChainComponent::setInputSectionCollapsed(bool collapsed)
{
    inputSectionCollapsed = collapsed;
    resized();
}

void MasterChainComponent::refreshTakeGroups()
{
    availableAudioTakeGroups = recordingManager.findAudioTakeGroups();
    rebuildAudioInBox();
    availableMidiTakeGroups = recordingManager.findMidiTakeGroups();
    rebuildMidiInBox();
}

void MasterChainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshAudioInputNames();
}

void MasterChainComponent::refreshAudioInputNames()
{
    auto freshNames = AudioInputUtils::getActiveAudioInputChannelNames(deviceManager);
    if (freshNames == availableAudioInputNames)
        return;
    availableAudioInputNames = freshNames;
    rebuildAudioInBox();
}

void MasterChainComponent::refreshMidiInputDevices()
{
    auto freshDevices = juce::MidiInput::getAvailableDevices();
    if (freshDevices == availableMidiInputs)
        return;
    availableMidiInputs = freshDevices;
    rebuildMidiInBox();
}

void MasterChainComponent::rebuildAudioInBox()
{
    audioInBox.clear(juce::dontSendNotification);
    audioInBox.addItem("None", 1);
    for (int i = 0; i < availableAudioInputNames.size(); ++i)
        audioInBox.addItem(availableAudioInputNames[i], i + 2);

    if (! availableAudioTakeGroups.isEmpty())
    {
        audioInBox.addSeparator();
        audioInBox.addSectionHeading("Recorded Takes");
        for (int i = 0; i < availableAudioTakeGroups.size(); ++i)
            audioInBox.addItem("Take " + availableAudioTakeGroups.getReference(i).getFileName(), takeIdBase + i);
    }

    // Always rests at "nothing selected" (see the class header comment) -
    // never restores a prior pick, unlike ChannelComponent's equivalent.
    audioInBox.setSelectedId(0, juce::dontSendNotification);
}

void MasterChainComponent::rebuildMidiInBox()
{
    midiInBox.clear(juce::dontSendNotification);
    midiInBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        midiInBox.addItem(availableMidiInputs[i].name, i + 2);

    if (! availableMidiTakeGroups.isEmpty())
    {
        midiInBox.addSeparator();
        midiInBox.addSectionHeading("Recorded Takes");
        for (int i = 0; i < availableMidiTakeGroups.size(); ++i)
            midiInBox.addItem("Take " + availableMidiTakeGroups.getReference(i).getFileName(), takeIdBase + i);
    }

    midiInBox.setSelectedId(0, juce::dontSendNotification);
}

void MasterChainComponent::updateArmButton()
{
    armButton.setColour(juce::TextButton::buttonColourId,
                        armed ? juce::Colour(0xff7a2020) : juce::Colour(0xff2a2a3e));
    armButton.setColour(juce::TextButton::textColourOffId,
                        armed ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    armButton.setTooltip(armed ? "Master is armed to record" : "Arm master output for recording");
}

void MasterChainComponent::updateArmAllButton()
{
    armAllButton.setColour(juce::TextButton::buttonColourId,
                           armAllState ? juce::Colour(0xff7a2020) : juce::Colour(0xff2a2a3e));
    armAllButton.setColour(juce::TextButton::textColourOffId,
                           armAllState ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    armAllButton.setTooltip(armAllState ? "Every channel and the master are armed - click to unarm all"
                                        : "Arm every channel and the master to record");
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

void MasterChainComponent::showBypassMenu()
{
    // Item ids: 1/2 = All, 10+slot*2/11+slot*2 = Bypass/Activate that slot
    // - slot runs 0..5 to match ChannelProcessor's own slot numbering (0 =
    // instrument/effect, 1..5 = inserts). This component's own inserts are
    // MasterChainProcessor::numSlots (5) wide, indices 0..4, mirroring
    // channel slots 1..5 one-to-one, offset by 1 - see applyBypassToSlot().
    juce::PopupMenu menu;
    menu.addItem(1, "Bypass All");
    menu.addItem(2, "Activate All");
    menu.addSeparator();
    for (int slot = 0; slot <= 5; ++slot)
    {
        menu.addItem(10 + slot * 2, "Bypass Slot "   + juce::String(slot));
        menu.addItem(11 + slot * 2, "Activate Slot " + juce::String(slot));
        if (slot != 5)
            menu.addSeparator();
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&bypassMenuButton),
        [this](int result)
        {
            if (result == 1)      applyBypassEverywhere(true);
            else if (result == 2) applyBypassEverywhere(false);
            else if (result >= 10)
                applyBypassToSlot((result - 10) / 2, ((result - 10) % 2) == 0);
        });
}

void MasterChainComponent::applyBypassEverywhere(bool bypass)
{
    for (int i = 0; i < numSlots; ++i)
        processor.setBypassed(i, bypass);
    if (onBypassChannelsRequested) onBypassChannelsRequested(-1, bypass);
}

void MasterChainComponent::applyBypassToSlot(int channelSlotIndex, bool bypass)
{
    // channelSlotIndex 0 is the instrument/effect slot - channel-only, this
    // strip has no such slot to touch. 1..5 mirror this component's own
    // slots 0..4.
    if (channelSlotIndex >= 1)
        processor.setBypassed(channelSlotIndex - 1, bypass);
    if (onBypassChannelsRequested) onBypassChannelsRequested(channelSlotIndex, bypass);
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

    // Dimmed divider under the heading, same colour/alpha as
    // ChannelComponent's own section dividers - same look and feel as a
    // channel heading, just not editable (nothing here to rename).
    g.setColour(juce::Colour(0xff3d5a80).withAlpha(0.5f));
    auto dividerBounds = getLocalBounds().reduced(6);
    g.drawHorizontalLine(titleDividerY, (float) dividerBounds.getX(), (float) dividerBounds.getRight());
}

void MasterChainComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    // 18px + 6px gap, matching ChannelComponent::channelNameLabel's own
    // heading spacing exactly.
    titleLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    // ---- Bulk Audio In/MIDI In selectors (v0.9.8), above the inserts -
    // same collapsible treatment as ChannelComponent's own Audio In/MIDI
    // In/MIDI Channel block: hidden widgets consume no layout space, so
    // the divider and the inserts below just shift up when the global I/O
    // collapse toggle is on, exactly like a channel's own Plugins section
    // does.
    audioInLabel.setVisible(! inputSectionCollapsed);
    audioInBox.setVisible(! inputSectionCollapsed);
    midiInLabel.setVisible(! inputSectionCollapsed);
    midiInBox.setVisible(! inputSectionCollapsed);

    if (! inputSectionCollapsed)
    {
        audioInLabel.setBounds(area.removeFromTop(16));
        audioInBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        midiInLabel.setBounds(area.removeFromTop(16));
        midiInBox.setBounds(area.removeFromTop(24));

        // Filler, no widget - keeps Insert 1 level with Channel N's own
        // slot 0 when I/O is expanded (user-requested alignment). Only two
        // rows here (no per-channel MIDI Channel equivalent), so this
        // block is naturally shorter than a channel's own three-row block;
        // 30 is the gap needed to close that difference against this
        // strip's own extra content below (the Bypass/Activate button,
        // which channels have no equivalent of) - hand-computed against
        // ChannelComponent::resized()'s own numbers, not derived from the
        // rows above. The collapsed state is not pixel-exact against
        // channels' own (that persistent button still has no channel-side
        // counterpart to offset against there) - accepted so the button
        // stays visible regardless of the I/O toggle.
        area.removeFromTop(30);
    }

    area.removeFromTop(6);
    titleDividerY = area.getY();
    area.removeFromTop(7);

    // ---- Bulk Bypass/Activate menu (v0.9.8), right above the inserts it
    // (partly) controls. A little extra breathing room above it specifically
    // (on top of the divider's own 7px gap above) so it doesn't crowd the
    // divider line - not shared with ChannelComponent's matching gap, which
    // has no equivalent button to make room for.
    area.removeFromTop(6);
    bypassMenuButton.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);

    for (int i = 0; i < numSlots; ++i)
    {
        slotButtons[(size_t) i].setBounds(area.removeFromTop(22));
        if (i != numSlots - 1)
            area.removeFromTop(3);
    }
    area.removeFromTop(16);

    // ---- ARM / Arm All, below the fader/meters - global transport (Play/
    // Rec/RTZ) and Panic moved out to GlobalSectionComponent, this strip
    // only carries what's actually part of the master signal chain. Split
    // evenly - ARM keeps its own single-glyph footprint, "All" doesn't
    // need more than the same half either.
    auto armArea = area.removeFromBottom(24);
    area.removeFromBottom(6);
    armButton.setBounds(armArea.removeFromLeft(armArea.getWidth() / 2 - 2));
    armArea.removeFromLeft(4);
    armAllButton.setBounds(armArea);

    auto leftMeterArea = area.removeFromLeft(16);
    area.removeFromLeft(4);
    leftLevelMeter.setBounds(leftMeterArea);

    auto rightMeterArea = area.removeFromRight(16);
    area.removeFromRight(4);
    rightLevelMeter.setBounds(rightMeterArea);

    volumeLabel.setBounds(area.removeFromTop(16));
    volumeSlider.setBounds(area);
}
