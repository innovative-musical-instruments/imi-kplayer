#include "GlobalSectionComponent.h"

GlobalSectionComponent::GlobalSectionComponent(int initialChannelCount, int maxChannelCount)
    : channelCount(initialChannelCount), maxChannels(maxChannelCount)
{
    addAndMakeVisible(brandingStrip);

    channelsLabel.setText("Channels", juce::dontSendNotification);
    channelsLabel.setFont(juce::Font(11.0f));
    channelsLabel.setJustificationType(juce::Justification::centred);
    channelsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(channelsLabel);

    channelMinusButton.setButtonText("-");
    channelMinusButton.setTooltip("Remove a channel");
    channelMinusButton.onClick = [this]
    {
        if (onChannelCountChangeRequested)
            onChannelCountChangeRequested(juce::jmax(1, channelCount - 1));
    };
    addAndMakeVisible(channelMinusButton);

    channelCountLabel.setJustificationType(juce::Justification::centred);
    channelCountLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a3e));
    channelCountLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(channelCountLabel);

    channelPlusButton.setButtonText("+");
    channelPlusButton.setTooltip("Add a channel");
    channelPlusButton.onClick = [this]
    {
        if (onChannelCountChangeRequested)
            onChannelCountChangeRequested(juce::jmin(maxChannels, channelCount + 1));
    };
    addAndMakeVisible(channelPlusButton);

    updateChannelButtons();

    gearButtonLookAndFeel = std::make_unique<GearButtonLookAndFeel>();
    settingsButton.setTooltip("Settings");
    settingsButton.setLookAndFeel(gearButtonLookAndFeel.get());
    settingsButton.onClick = [this] { if (onSettingsRequested) onSettingsRequested(); };
    addAndMakeVisible(settingsButton);

    collapseInputButton.setButtonText("Hide Channel I/O's");
    collapseInputButton.onClick = [this] { if (onCollapseToggled) onCollapseToggled(); };
    addAndMakeVisible(collapseInputButton);

    addAndMakeVisible(tempoSyncComponent);

    // Icons are vector-drawn by TransportButtonLookAndFeel rather than
    // Unicode glyphs - see that class's header comment (font-coverage gap
    // hit on Windows for the original Play/Pause/RTZ icons).
    transportButtonLookAndFeel = std::make_unique<TransportButtonLookAndFeel>();

    playPauseButton.onClick = [this] { if (onPlayPauseClicked) onPlayPauseClicked(); };
    playPauseButton.setLookAndFeel(transportButtonLookAndFeel.get());
    addAndMakeVisible(playPauseButton);

    rtzButton.setButtonText("RTZ");
    rtzButton.setTooltip("Return to zero");
    rtzButton.onClick = [this] { if (onRtzClicked) onRtzClicked(); };
    rtzButton.setLookAndFeel(transportButtonLookAndFeel.get());
    addAndMakeVisible(rtzButton);

    updateTransportButtons();

    recordButton.setButtonText(juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC");
    recordButton.onClick = [this] { if (onRecordButtonClicked) onRecordButtonClicked(); };
    addAndMakeVisible(recordButton);
    updateRecordButtonColour();

    // Neutral by default, momentary red flash on click only - see
    // onClick below. Unlike Record Ready, this reflects nothing external;
    // it's purely local, fire-and-forget visual feedback.
    panicButton.setButtonText("PANIC");
    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3e));
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    panicButton.setTooltip("All notes off / all sound off - immediately silences every loaded instrument");
    panicButton.onClick = [this]
    {
        if (onPanicClicked) onPanicClicked();

        panicButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        panicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        juce::Component::SafePointer<GlobalSectionComponent> self(this);
        juce::Timer::callAfterDelay(200, [self]
        {
            if (self == nullptr)
                return;
            self->panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3e));
            self->panicButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
        });
    };
    addAndMakeVisible(panicButton);
}

GlobalSectionComponent::~GlobalSectionComponent()
{
    settingsButton.setLookAndFeel(nullptr);
    playPauseButton.setLookAndFeel(nullptr);
    rtzButton.setLookAndFeel(nullptr);
}

void GlobalSectionComponent::updateChannelButtons()
{
    channelCountLabel.setText(juce::String(channelCount), juce::dontSendNotification);
    channelMinusButton.setEnabled(channelCount > 1);
    channelPlusButton.setEnabled(channelCount < maxChannels);
}

void GlobalSectionComponent::setChannelCount(int count)
{
    channelCount = juce::jlimit(1, maxChannels, count);
    updateChannelButtons();
}

void GlobalSectionComponent::setInputSectionCollapsed(bool collapsed)
{
    inputCollapsed = collapsed;
    collapseInputButton.setButtonText(inputCollapsed ? "Show Channel I/O's" : "Hide Channel I/O's");
}

void GlobalSectionComponent::setTransportPlaying(bool playing)
{
    transportPlaying = playing;
    updateTransportButtons();
}

void GlobalSectionComponent::updateTransportButtons()
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

void GlobalSectionComponent::setRecordState(RecordState state)
{
    recordState = state;

    if (recordState == RecordState::armed)
    {
        if (! isTimerRunning())
        {
            blinkPhaseOn = true;
            startTimer(300);
        }
    }
    else
    {
        stopTimer();
    }

    updateRecordButtonColour();
}

void GlobalSectionComponent::timerCallback()
{
    // Only ever running while recordState == armed - see setRecordState().
    blinkPhaseOn = ! blinkPhaseOn;
    updateRecordButtonColour();
}

void GlobalSectionComponent::updateRecordButtonColour()
{
    juce::Colour bg   = juce::Colour(0xff2a2a3e);
    juce::Colour text = juce::Colour(0xffaaaaaa);
    juce::String tooltip = "Arm to record on next Play";

    switch (recordState)
    {
        case RecordState::idle:
            break;
        case RecordState::armed:
            bg = blinkPhaseOn ? juce::Colours::red : juce::Colour(0xff7a2020);
            text = juce::Colours::white;
            tooltip = "Armed - click to cancel, or press Play to start recording";
            break;
        case RecordState::recording:
            bg = juce::Colours::red;
            text = juce::Colours::white;
            tooltip = "Recording - click to stop (playback keeps going)";
            break;
    }

    recordButton.setColour(juce::TextButton::buttonColourId, bg);
    recordButton.setColour(juce::TextButton::textColourOffId, text);
    recordButton.setTooltip(tooltip);
}

void GlobalSectionComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6.0f, 1.5f);
}

void GlobalSectionComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    brandingStrip.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);

    channelsLabel.setBounds(area.removeFromTop(16));
    auto channelsRow = area.removeFromTop(24);
    channelMinusButton.setBounds(channelsRow.removeFromLeft(28));
    channelsRow.removeFromLeft(4);
    channelPlusButton.setBounds(channelsRow.removeFromRight(28));
    channelsRow.removeFromRight(4);
    channelCountLabel.setBounds(channelsRow);
    area.removeFromTop(10);

    settingsButton.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    collapseInputButton.setBounds(area.removeFromTop(22));
    area.removeFromTop(10);

    tempoSyncComponent.setBounds(area.removeFromTop(TempoSyncComponent::preferredHeight));
    area.removeFromTop(10);

    // Panic pinned to the very bottom - always reachable regardless of how
    // much space the rest of this strip's content ends up needing.
    panicButton.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(10);

    auto transportArea = area.removeFromTop(24);
    playPauseButton.setBounds(transportArea.removeFromLeft(transportArea.getWidth() / 2).reduced(2, 0));
    rtzButton.setBounds(transportArea.reduced(2, 0));
    area.removeFromTop(6);

    recordButton.setBounds(area.removeFromTop(24));
}
