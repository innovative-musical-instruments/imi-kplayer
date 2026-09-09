#include "TempoSyncComponent.h"

TempoSyncComponent::TempoSyncComponent()
{
    clickButton.setButtonText("CLICK");
    clickButton.onClick = [this] { if (onClickToggled) onClickToggled(); };
    clickButton.onPopupMenuRequested = [this] { if (onClickOptionsRequested) onClickOptionsRequested(); };
    addAndMakeVisible(clickButton);
    updateClickButton();

    tempoLabel.setText("Tempo", juce::dontSendNotification);
    tempoLabel.setFont(juce::Font(11.0f));
    tempoLabel.setJustificationType(juce::Justification::centredLeft);
    tempoLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(tempoLabel);

    tempoValueLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    tempoValueLabel.setJustificationType(juce::Justification::centredRight);
    tempoValueLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    tempoValueLabel.setEditable(false, true, false);
    tempoValueLabel.addListener(this);
    updateTempoValueLabel();
    addAndMakeVisible(tempoValueLabel);

    syncButton.setButtonText("Sync");
    syncButton.setClickingTogglesState(true);
    syncButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    syncButton.onClick = [this]
    {
        syncEnabled = syncButton.getToggleState();
        tempoValueLabel.setEditable(false, ! syncEnabled, false);
        if (onSyncToggled) onSyncToggled(syncEnabled);
    };
    addAndMakeVisible(syncButton);

    availableMidiInputs = juce::MidiInput::getAvailableDevices();
    syncDeviceBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        syncDeviceBox.addItem(availableMidiInputs[i].name, i + 2);
    syncDeviceBox.setSelectedId(1, juce::dontSendNotification);
    syncDeviceBox.onChange = [this]
    {
        int selected = syncDeviceBox.getSelectedId();
        juce::String identifier;
        if (selected > 1)
        {
            int index = selected - 2;
            if (index >= 0 && index < availableMidiInputs.size())
                identifier = availableMidiInputs[index].identifier;
        }
        currentSyncDeviceId = identifier;
        if (onSyncDeviceChanged) onSyncDeviceChanged(identifier);
    };
    addAndMakeVisible(syncDeviceBox);

    // Devices connecting/disconnecting after construction need to rebuild
    // this dropdown - event-driven, same mechanism ChannelComponent's own
    // MIDI In box uses.
    midiDeviceListConnection = juce::MidiDeviceListConnection::make([this] { refreshMidiDeviceList(); });
}

TempoSyncComponent::~TempoSyncComponent() = default;

void TempoSyncComponent::paint(juce::Graphics& g)
{
    // Same bordered-box convention used throughout the rest of the app
    // (ChannelComponent, MasterChainComponent, GlobalSectionComponent
    // itself) - drawn tightly around just the "Tempo" heading + its live
    // value, visually grouping that pair rather than the whole component.
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(tempoFrameArea.toFloat(), 6.0f, 1.5f);
}

void TempoSyncComponent::resized()
{
    // 2x2 grid, both rows sharing the same left-column width (64, matching
    // GlobalSectionComponent's Play button) so Click sits directly above
    // Sync, and the "Tempo" heading and its value sit together directly
    // above the port selector they share a column with:
    //   [Click]  Tempo 120.0
    //   [Sync]   [None v]
    auto area = getLocalBounds();

    // Both rows the same height, deliberately: JUCE derives a TextButton's
    // text size from the button's height (LookAndFeel_V4::getTextButtonFont,
    // jmin(16, height * 0.6)), so rows of 20 and 24 put Click at 12pt
    // directly above Sync at 14.4pt - the same typeface at two sizes, which
    // reads as two different fonts. Equal rows land both on 13.2, matching
    // LOOP/FULL on their own 22px row in GlobalSectionComponent.
    auto row1 = area.removeFromTop(22);
    clickButton.setBounds(row1.removeFromLeft(64));
    row1.removeFromLeft(6);

    // The frame now groups the heading with its value only, not the whole
    // row - Click is its own control and has no business inside it.
    tempoFrameArea = row1.expanded(2, 2);
    tempoLabel.setBounds(row1.removeFromLeft(34));
    tempoValueLabel.setBounds(row1);

    area.removeFromTop(4);

    auto row2 = area.removeFromTop(22);
    syncButton.setBounds(row2.removeFromLeft(64));
    row2.removeFromLeft(6);
    syncDeviceBox.setBounds(row2);
}

void TempoSyncComponent::setClickEnabled(bool enabled)
{
    clickEnabled = enabled;
    updateClickButton();
}

void TempoSyncComponent::updateClickButton()
{
    // Same muted green "engaged" treatment as Show Mode, the active Play
    // button and the Range toggles.
    clickButton.setColour(juce::TextButton::buttonColourId,
                          clickEnabled ? juce::Colour(0xff2a6b3d) : juce::Colour(0xff2a2a3e));
    clickButton.setColour(juce::TextButton::textColourOffId,
                          clickEnabled ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    clickButton.setTooltip(clickEnabled
        ? "Metronome click on - right-click for sound, resolution and volume"
        : "Metronome click off - right-click for sound, resolution and volume");
}

void TempoSyncComponent::setDisplayedTempo(double bpm)
{
    displayedBpm = bpm;
    updateTempoValueLabel();
}

void TempoSyncComponent::setSyncEnabled(bool enabled)
{
    syncEnabled = enabled;
    syncButton.setToggleState(enabled, juce::dontSendNotification);
    tempoValueLabel.setEditable(false, ! syncEnabled, false);
}

void TempoSyncComponent::setSyncDeviceIdentifier(const juce::String& identifier)
{
    currentSyncDeviceId = identifier;
    syncDeviceBox.setSelectedId(midiDeviceItemIdFor(identifier), juce::dontSendNotification);
}

void TempoSyncComponent::setSyncSignalWarning(bool noSignal)
{
    syncDeviceBox.setColour(juce::ComboBox::textColourId,
                            noSignal ? juce::Colours::orange : juce::Colours::white);
    syncDeviceBox.setTooltip(noSignal ? "No MIDI clock signal from this device"
                                     : juce::String());
}

void TempoSyncComponent::labelTextChanged(juce::Label* label)
{
    if (label != &tempoValueLabel || syncEnabled)
        return;

    double bpm = juce::jlimit(minimumTempoBpm, maximumTempoBpm, tempoValueLabel.getText().getDoubleValue());
    displayedBpm = bpm;
    updateTempoValueLabel();
    if (onTempoChanged) onTempoChanged(bpm);
}

void TempoSyncComponent::editorShown(juce::Label* label, juce::TextEditor& editor)
{
    if (label == &tempoValueLabel)
        editor.setText(juce::String(displayedBpm, 1), false);
}

void TempoSyncComponent::refreshMidiDeviceList()
{
    auto freshDevices = juce::MidiInput::getAvailableDevices();
    if (freshDevices == availableMidiInputs)
        return;

    availableMidiInputs = freshDevices;

    syncDeviceBox.clear(juce::dontSendNotification);
    syncDeviceBox.addItem("None", 1);
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        syncDeviceBox.addItem(availableMidiInputs[i].name, i + 2);

    syncDeviceBox.setSelectedId(midiDeviceItemIdFor(currentSyncDeviceId), juce::dontSendNotification);
}

int TempoSyncComponent::midiDeviceItemIdFor(const juce::String& identifier) const
{
    for (int i = 0; i < availableMidiInputs.size(); ++i)
        if (availableMidiInputs[i].identifier == identifier)
            return i + 2;
    return 1;
}

void TempoSyncComponent::updateTempoValueLabel()
{
    tempoValueLabel.setText(juce::String(displayedBpm, 1), juce::dontSendNotification);
}
