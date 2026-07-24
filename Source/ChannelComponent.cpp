#include "ChannelComponent.h"
#include <algorithm>

namespace
{
    // Names of the device's *active* input channels, in the same order as
    // audioDeviceIOCallbackWithContext's inputChannelData array - i.e. the
    // Nth active channel in ascending order, not the device's full channel
    // list (which includes channels the user hasn't enabled).
    juce::StringArray getActiveAudioInputChannelNames(juce::AudioDeviceManager& deviceManager)
    {
        juce::StringArray names;
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            auto allNames = device->getInputChannelNames();
            auto active   = device->getActiveInputChannels();
            for (int i = 0; i < allNames.size(); ++i)
                if (active[i])
                    names.add(allNames[i]);
        }
        return names;
    }
}

ChannelComponent::ChannelComponent(ChannelProcessor& p, juce::AudioDeviceManager& dm, int channelNum)
    : processor(p), deviceManager(dm), channelNumber(channelNum)
{
    // Gain caps are twice as tall as the base size; pan caps are half as
    // wide - both cosmetic-only, per user request, not a track-length change.
    gainFaderLookAndFeel = std::make_unique<ConsoleFaderLookAndFeel>(2.0f, 1.0f);
    panFaderLookAndFeel  = std::make_unique<ConsoleFaderLookAndFeel>(0.5f, 1.0f);
    selectorLookAndFeel  = std::make_unique<SelectorLookAndFeel>();

    channelNameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    channelNameLabel.setJustificationType(juce::Justification::centred);
    channelNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    // Item 1.1: double-click to rename. Only the custom name is ever
    // editable - the "N." number prefix is redrawn by updateChannelNameLabel()
    // after every edit, never present in the editor itself (see editorShown()).
    channelNameLabel.setEditable(false, true, false);
    channelNameLabel.addListener(this);
    updateChannelNameLabel();
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

    audioInLabel.setText("Audio In", juce::dontSendNotification);
    audioInLabel.setFont(juce::Font(11.0f));
    audioInLabel.setJustificationType(juce::Justification::centredLeft);
    audioInLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(audioInLabel);

    availableAudioInputNames = getActiveAudioInputChannelNames(deviceManager);
    audioInputBox.addItem("None", 1);
    for (int i = 0; i < availableAudioInputNames.size(); ++i)
        audioInputBox.addItem(availableAudioInputNames[i], i + 2);
    audioInputBox.setSelectedId(1, juce::dontSendNotification);
    audioInputBox.addListener(this);
    audioInputBox.setLookAndFeel(selectorLookAndFeel.get());
    addAndMakeVisible(audioInputBox);

    deviceManager.addChangeListener(this);

    midiInLabel.setText("MIDI In", juce::dontSendNotification);
    midiInLabel.setFont(juce::Font(11.0f));
    midiInLabel.setJustificationType(juce::Justification::centredLeft);
    midiInLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(midiInLabel);

    availableMidiInputs = juce::MidiInput::getAvailableDevices();
    midiDeviceBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        midiDeviceBox.addItem(availableMidiInputs[i].name, i + 2);

    // Reflects whatever's already on the processor rather than deciding a
    // default itself - MainComponent::addChannel() decides that (it alone
    // can see every other channel's current MIDI device/channel, needed to
    // find the next free Kadabra channel) before constructing this component,
    // and a session load overrides it again afterward the same way.
    midiDeviceBox.setSelectedId(midiDeviceItemIdFor(processor.getMidiDeviceIdentifier()), juce::dontSendNotification);
    midiDeviceBox.addListener(this);
    midiDeviceBox.setLookAndFeel(selectorLookAndFeel.get());
    addAndMakeVisible(midiDeviceBox);

    // Devices connecting/disconnecting after construction need to update
    // this channel's dropdown and warning state - event-driven rather than
    // polling, so it's reflected immediately, not after up to a few seconds.
    midiDeviceListConnection = juce::MidiDeviceListConnection::make([this]
    {
        refreshMidiDeviceList();
        updateMidiDeviceWarning();
    });

    midiLabel.setText("MIDI Ch", juce::dontSendNotification);
    midiLabel.setFont(juce::Font(11.0f));
    midiLabel.setJustificationType(juce::Justification::centredLeft);
    midiLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(midiLabel);

    midiChannelBox.addItem("All", 1);
    for (int i = 1; i <= 16; ++i)
        midiChannelBox.addItem(juce::String(i), i + 1);
    // Same rationale as midiDeviceBox above - reflects whatever
    // MainComponent::addChannel() already set on the processor (its next-free-
    // Kadabra-channel scan, or "All" if it left the default untouched).
    int initialChannel = processor.getMidiChannel();
    midiChannelBox.setSelectedId(initialChannel >= 1 && initialChannel <= 16 ? initialChannel + 1 : 1,
                                 juce::dontSendNotification);
    midiChannelBox.addListener(this);
    midiChannelBox.setLookAndFeel(selectorLookAndFeel.get());
    addAndMakeVisible(midiChannelBox);

    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setFont(juce::Font(11.0f));
    gainLabel.setJustificationType(juce::Justification::centred);
    gainLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(gainLabel);

    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    // Quadratic taper: dB(t) = -96 * t^2, t = fraction of travel down from the
    // top (unity gain). Gives -6dB at a quarter down, -24dB at halfway, so
    // the musically useful range near unity gets far more of the fader's
    // travel than a plain linear-dB mapping would. See
    // docs/KPlayer_Refinement_Spec_2026-07-11.md section 1.3. The curve
    // itself lives on ChannelProcessor (shared with MIDI CC7 gain control)
    // so a hardware fader move lands on the exact same mapping as dragging
    // this slider to the equivalent position.
    juce::NormalisableRange<double> gainRange(
        -96.0, 0.0,
        [](double, double, double normalised) { return ChannelProcessor::normalisedToGainDb(normalised); },
        [](double, double, double value) { return ChannelProcessor::gainDbToNormalised(value); });
    gainRange.interval = 0.1;
    gainSlider.setNormalisableRange(gainRange);
    gainSlider.setValue(0.0, juce::dontSendNotification);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    gainSlider.setTextValueSuffix(" dB");
    gainSlider.addListener(this);
    gainSlider.setLookAndFeel(gainFaderLookAndFeel.get());
    addAndMakeVisible(gainSlider);

    panLabel.setText("Pan", juce::dontSendNotification);
    panLabel.setFont(juce::Font(11.0f));
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(panLabel);

    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    panSlider.addListener(this);
    panSlider.setLookAndFeel(panFaderLookAndFeel.get());
    addAndMakeVisible(panSlider);

    levelMeter.setSources(processor.getPeakLevelLeftPtr(),
                           processor.getPeakLevelRightPtr(),
                           processor.getClipFlagPtr());
    addAndMakeVisible(levelMeter);

    muteButton.setButtonText("M");
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    muteButton.onClick = [this]
    {
        processor.setMuted(muteButton.getToggleState());
        if (onDirty) onDirty();
    };
    addAndMakeVisible(muteButton);

    soloButton.setButtonText("S");
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.onClick = [this]
    {
        processor.setSolo(soloButton.getToggleState());
        if (onDirty) onDirty();
    };
    addAndMakeVisible(soloButton);

    setSize(80, 700);

    updateMidiDeviceWarning();
    updateAudioInputWarning();
}

ChannelComponent::~ChannelComponent()
{
    deviceManager.removeChangeListener(this);
    gainSlider.setLookAndFeel(nullptr);
    panSlider.setLookAndFeel(nullptr);
    audioInputBox.setLookAndFeel(nullptr);
    midiDeviceBox.setLookAndFeel(nullptr);
    midiChannelBox.setLookAndFeel(nullptr);
}

void ChannelComponent::showPluginSlotMenu(int slotIndex)
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

    updateChannelNameLabel();

    float gainDb = juce::Decibels::gainToDecibels(processor.getGain(), -96.0f);
    gainSlider.setValue(gainDb, juce::dontSendNotification);
    panSlider.setValue((double) processor.getPan(), juce::dontSendNotification);

    int midiChannel = processor.getMidiChannel();
    midiChannelBox.setSelectedId(midiChannel <= 0 ? 1 : midiChannel + 1, juce::dontSendNotification);

    auto deviceId = processor.getMidiDeviceIdentifier();
    int deviceItemId = 1;
    for (int i = 0; i < availableMidiInputs.size(); ++i)
    {
        if (availableMidiInputs[i].identifier == deviceId)
        {
            deviceItemId = i + 2;
            break;
        }
    }
    midiDeviceBox.setSelectedId(deviceItemId, juce::dontSendNotification);

    int audioInputIndex = processor.getAudioInputChannelIndex();
    audioInputBox.setSelectedId(
        (audioInputIndex >= 0 && audioInputIndex < availableAudioInputNames.size()) ? audioInputIndex + 2 : 1,
        juce::dontSendNotification);

    muteButton.setToggleState(processor.isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(processor.isSolo(), juce::dontSendNotification);

    updateMidiDeviceWarning();
    updateAudioInputWarning();
}

void ChannelComponent::setInputSectionCollapsed(bool collapsed)
{
    if (inputCollapsed == collapsed)
        return;
    inputCollapsed = collapsed;
    resized();
    repaint();
}

// Fixed "Channel N" default, or "N. CustomName" once the user has renamed
// it - either way the number itself is never part of the editable text
// (see editorShown()), so it can't be edited or deleted by mistake.
void ChannelComponent::updateChannelNameLabel()
{
    auto customName = processor.getName().trim();
    auto display = customName.isEmpty()
        ? ("Channel " + juce::String(channelNumber))
        : (juce::String(channelNumber) + ". " + customName);
    channelNameLabel.setText(display, juce::dontSendNotification);
}

void ChannelComponent::editorShown(juce::Label* label, juce::TextEditor& editor)
{
    if (label == &channelNameLabel)
        editor.setText(processor.getName(), false);
}

void ChannelComponent::labelTextChanged(juce::Label* label)
{
    if (label != &channelNameLabel)
        return;

    processor.setName(channelNameLabel.getText().trim());
    updateChannelNameLabel();
    if (onDirty) onDirty();
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

    if (onDirty) onDirty();
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
        updateMidiDeviceWarning();
    }
    else if (combo == &audioInputBox)
    {
        int selected = audioInputBox.getSelectedId();
        processor.setAudioInputChannelIndex(selected <= 1 ? -1 : selected - 2);
        updateAudioInputWarning();
    }

    if (onDirty) onDirty();
}

void ChannelComponent::refreshMidiDeviceList()
{
    auto freshDevices = juce::MidiInput::getAvailableDevices();
    if (freshDevices == availableMidiInputs)
        return;

    auto currentIdentifier = processor.getMidiDeviceIdentifier();
    availableMidiInputs = freshDevices;

    midiDeviceBox.clear(juce::dontSendNotification);
    midiDeviceBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        midiDeviceBox.addItem(availableMidiInputs[i].name, i + 2);

    midiDeviceBox.setSelectedId(midiDeviceItemIdFor(currentIdentifier), juce::dontSendNotification);
}

int ChannelComponent::midiDeviceItemIdFor(const juce::String& identifier) const
{
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        if (availableMidiInputs[i].identifier == identifier)
            return i + 2;
    return 1;
}

void ChannelComponent::updateMidiDeviceWarning()
{
    auto deviceId = processor.getMidiDeviceIdentifier();
    bool missing = false;

    if (deviceId.isNotEmpty())
    {
        auto currentDevices = juce::MidiInput::getAvailableDevices();
        missing = std::none_of(currentDevices.begin(), currentDevices.end(),
                               [&](const juce::MidiDeviceInfo& d) { return d.identifier == deviceId; });
    }

    midiDeviceBox.setColour(juce::ComboBox::textColourId,
                            missing ? juce::Colours::orange : juce::Colours::white);
    midiDeviceBox.setTooltip(missing ? "This channel's MIDI device is not currently connected"
                                     : juce::String());
}

void ChannelComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshAudioInputList();
    updateAudioInputWarning();
}

void ChannelComponent::refreshAudioInputList()
{
    auto freshNames = getActiveAudioInputChannelNames(deviceManager);
    if (freshNames == availableAudioInputNames)
        return;

    availableAudioInputNames = freshNames;

    audioInputBox.clear(juce::dontSendNotification);
    audioInputBox.addItem("None", 1);
    for (int i = 0; i < availableAudioInputNames.size(); ++i)
        audioInputBox.addItem(availableAudioInputNames[i], i + 2);

    // The stored value is a plain index into the active-channel list, not
    // a stable identifier (individual audio channels don't have one, unlike
    // MIDI devices) - just reflect whatever it now points at, or "None" if
    // out of range; updateAudioInputWarning() flags the mismatch visually
    // rather than silently reassigning it.
    int index = processor.getAudioInputChannelIndex();
    audioInputBox.setSelectedId(
        (index >= 0 && index < availableAudioInputNames.size()) ? index + 2 : 1,
        juce::dontSendNotification);
}

void ChannelComponent::updateAudioInputWarning()
{
    int index = processor.getAudioInputChannelIndex();
    bool missing = index >= 0 && index >= availableAudioInputNames.size();

    audioInputBox.setColour(juce::ComboBox::textColourId,
                            missing ? juce::Colours::orange : juce::Colours::white);
    audioInputBox.setTooltip(missing ? "This channel's audio input is not currently active"
                                     : juce::String());
}

void ChannelComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6.0f, 1.5f);

    // Section dividers between Input/Plugins/Mix, per the resectioned layout.
    g.setColour(juce::Colour(0xff3d5a80).withAlpha(0.5f));
    auto dividerBounds = getLocalBounds().reduced(6);
    g.drawHorizontalLine(inputSectionDividerY, (float) dividerBounds.getX(), (float) dividerBounds.getRight());
    g.drawHorizontalLine(pluginsSectionDividerY, (float) dividerBounds.getX(), (float) dividerBounds.getRight());
}

int ChannelComponent::insertSectionStartY(bool inputSectionCollapsed)
{
    int y = 6;             // getLocalBounds().reduced(6) top inset
    y += 18 + 6;            // channelNameLabel + gap

    if (! inputSectionCollapsed)
        y += 16 + 24 + 8    // audioInLabel, audioInputBox, gap
           + 16 + 24 + 8    // midiInLabel, midiDeviceBox, gap
           + 16 + 24;       // midiLabel, midiChannelBox

    y += 6 + 7;             // gap before/after inputSectionDividerY
    y += 16 + 26 + 10 + 16; // pluginLabel, slotButtons[0], gap, insertsLabel
    return y;
}

void ChannelComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    // ---- Input section: name, audio in, MIDI in, MIDI channel ----
    channelNameLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    // Item: global collapsible Input section - hidden widgets consume no
    // layout space, so the divider and everything below just shift up.
    audioInLabel.setVisible(! inputCollapsed);
    audioInputBox.setVisible(! inputCollapsed);
    midiInLabel.setVisible(! inputCollapsed);
    midiDeviceBox.setVisible(! inputCollapsed);
    midiLabel.setVisible(! inputCollapsed);
    midiChannelBox.setVisible(! inputCollapsed);

    if (! inputCollapsed)
    {
        audioInLabel.setBounds(area.removeFromTop(16));
        audioInputBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        midiInLabel.setBounds(area.removeFromTop(16));
        midiDeviceBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        midiLabel.setBounds(area.removeFromTop(16));
        midiChannelBox.setBounds(area.removeFromTop(24));
    }

    area.removeFromTop(6);
    inputSectionDividerY = area.getY();
    area.removeFromTop(7);

    // ---- Plugins section: instrument, inserts ----
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

    area.removeFromTop(6);
    pluginsSectionDividerY = area.getY();
    area.removeFromTop(7);

    // ---- Mix section: gain, pan, mute/solo ----
    auto bottomArea = area.removeFromBottom(140);

    auto meterArea = area.removeFromRight(16);
    area.removeFromRight(4);
    levelMeter.setBounds(meterArea);

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
