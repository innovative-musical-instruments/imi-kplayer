#include "GlobalSectionComponent.h"

GlobalSectionComponent::GlobalSectionComponent(int initialChannelCount, int maxChannelCount)
    : channelCount(initialChannelCount), maxChannels(maxChannelCount)
{
    addAndMakeVisible(imiLogo);
    addAndMakeVisible(tribalLogo);

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
    // Icon-only (empty text) in this bar - see GearButtonLookAndFeel's
    // drawButtonText(), which draws one big centred gear instead of its
    // small-icon-next-to-a-label mode whenever the button text is empty.
    settingsButton.setButtonText("");
    settingsButton.setTooltip("Settings");
    settingsButton.setLookAndFeel(gearButtonLookAndFeel.get());
    settingsButton.onClick = [this] { if (onSettingsRequested) onSettingsRequested(); };
    addAndMakeVisible(settingsButton);

    collapseInputButton.setButtonText("Hide I/O");
    collapseInputButton.onClick = [this] { if (onCollapseToggled) onCollapseToggled(); };
    addAndMakeVisible(collapseInputButton);

    addAndMakeVisible(tempoSyncComponent);

    timeDisplayLabel.setText("00:00", juce::dontSendNotification);
    timeDisplayLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    timeDisplayLabel.setJustificationType(juce::Justification::centred);
    timeDisplayLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(timeDisplayLabel);

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

    // "REC" is a sentinel TransportButtonLookAndFeel dispatches on (like
    // "PLAY"/"PAUSE"/"RTZ" above), not shown to the user - it draws a
    // vector-filled circle sized off the same icon bounding box the Play
    // triangle uses, so the two read as proportional rather than an
    // independently-sized glyph.
    recordButton.setButtonText("REC");
    recordButton.onClick = [this] { if (onRecordButtonClicked) onRecordButtonClicked(); };
    recordButton.setLookAndFeel(transportButtonLookAndFeel.get());
    addAndMakeVisible(recordButton);
    updateRecordButtonColour();

    showModeButton.onClick = [this] { if (onShowModeToggled) onShowModeToggled(); };
    addAndMakeVisible(showModeButton);
    setShowModeEnabled(false);

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
    recordButton.setLookAndFeel(nullptr);
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

void GlobalSectionComponent::setDisplayedTime(const juce::String& text)
{
    timeDisplayLabel.setText(text, juce::dontSendNotification);
}

void GlobalSectionComponent::setInputSectionCollapsed(bool collapsed)
{
    inputCollapsed = collapsed;
    collapseInputButton.setButtonText(inputCollapsed ? "Show I/O" : "Hide I/O");
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
    playPauseButton.setTooltip(transportPlaying ? "Pause audio and MIDI" : "Play audio and MIDI");
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

void GlobalSectionComponent::setShowModeEnabled(bool enabled)
{
    showModeEnabled = enabled;
    showModeButton.setButtonText(showModeEnabled ? "Show Mode" : "Work Mode");
    // Muted green for Show Mode - same "engaged, not alarming" shade
    // playPauseButton already uses for its active state, rather than a
    // bright/alarming colour fighting for attention with Panic.
    showModeButton.setColour(juce::TextButton::buttonColourId,
                             showModeEnabled ? juce::Colour(0xff2a6b3d) : juce::Colour(0xff2a2a3e));
    showModeButton.setColour(juce::TextButton::textColourOffId,
                             showModeEnabled ? juce::Colours::white : juce::Colour(0xffaaaaaa));
    showModeButton.setTooltip(showModeEnabled
        ? "Show Mode - loading a session discards unsaved changes without asking. Click for Work Mode."
        : "Work Mode - loading a session with unsaved changes asks first. Click for Show Mode.");
}

void GlobalSectionComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xff3d5a80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6.0f, 1.5f);
}

void GlobalSectionComponent::resized()
{
    // Three independent horizontal zones, each computed from the *full*
    // bar width rather than whatever's left over after the others - so the
    // centered transport zone stays visually centered in the window
    // regardless of how wide the left/right zones happen to be, matching a
    // conventional toolbar layout rather than "centered in the leftover
    // gap". This only stays overlap-free because MainWindow enforces
    // minimumWindowWidth via setResizeLimits() - see that constant's own
    // comment. A fixed row height (this bar's own preferredHeight minus
    // this reduced() padding) is shared by every element except
    // TempoSyncComponent and the Channels stepper, which each split their
    // own two-row internal layout out of it.
    auto fullArea = getLocalBounds().reduced(10, 8);
    const int rowHeight = fullArea.getHeight();

    // --- Left zone: IMI logo, Channels, Settings, Hide/Show I/O ---
    auto leftZone = fullArea;
    imiLogo.setBounds(leftZone.removeFromLeft(logoWidth));
    leftZone.removeFromLeft(zoneGap);

    // Channels stepper: -/+ flank a two-row stack ("Channels" heading over
    // the count, rather than side by side) so the whole control is
    // narrower.
    auto channelsArea = leftZone.removeFromLeft(channelsWidth);
    channelMinusButton.setBounds(channelsArea.removeFromLeft(22));
    channelPlusButton.setBounds(channelsArea.removeFromRight(22));
    channelsLabel.setBounds(channelsArea.removeFromTop(16));
    channelCountLabel.setBounds(channelsArea);
    leftZone.removeFromLeft(zoneGap);

    settingsButton.setBounds(leftZone.removeFromLeft(settingsWidth));
    leftZone.removeFromLeft(zoneGap);
    collapseInputButton.setBounds(leftZone.removeFromLeft(hideIOWidth));

    // --- Right zone: Work/Show Mode, Panic, Tribal Tools logo ---
    auto rightZone = fullArea;
    tribalLogo.setBounds(rightZone.removeFromRight(logoWidth));
    rightZone.removeFromRight(zoneGap);
    panicButton.setBounds(rightZone.removeFromRight(panicWidth));
    rightZone.removeFromRight(zoneGap);
    showModeButton.setBounds(rightZone.removeFromRight(showModeWidth));

    // --- Center zone: Tempo/Sync + port selector, time display, Play,
    // Rec Ready, RTZ - centered as one block within the full bar
    // (withSizeKeepingCentre) rather than left-aligned in whatever space
    // remains.
    auto centerZone = fullArea.withSizeKeepingCentre(centerZoneWidth, rowHeight);
    tempoSyncComponent.setBounds(centerZone.removeFromLeft(TempoSyncComponent::preferredWidth)
                                    .withSizeKeepingCentre(TempoSyncComponent::preferredWidth,
                                                            TempoSyncComponent::preferredHeight));
    centerZone.removeFromLeft(zoneGap);
    timeDisplayLabel.setBounds(centerZone.removeFromLeft(timeWidth));
    centerZone.removeFromLeft(zoneGap);
    playPauseButton.setBounds(centerZone.removeFromLeft(transportButtonWidth));
    centerZone.removeFromLeft(zoneGap);
    recordButton.setBounds(centerZone.removeFromLeft(transportButtonWidth));
    centerZone.removeFromLeft(zoneGap);
    rtzButton.setBounds(centerZone.removeFromLeft(transportButtonWidth));
}
