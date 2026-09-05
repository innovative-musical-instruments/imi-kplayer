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
    timeDisplayLabel.setFont(juce::Font(13.0f, juce::Font::bold));
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

    // ---- Range cluster, on the row above Play/Rec/RTZ ----
    loopButton.setButtonText("LOOP");
    loopButton.onClick = [this] { if (onLoopToggled) onLoopToggled(); };
    addAndMakeVisible(loopButton);

    fullButton.setButtonText("FULL");
    fullButton.onClick = [this] { if (onFullToggled) onFullToggled(); };
    addAndMakeVisible(fullButton);

    // A caption, not a control - it sits between the two things it names,
    // with an arrow pointing at each (see RangeCaptionComponent::paint).
    addAndMakeVisible(rangeCaption);

    configureRangeField(rangeStartField, "Range Start", onRangeStartEdited);
    configureRangeField(rangeEndField, "Range End", onRangeEndEdited);

    // "CAPTURE" is a sentinel TransportButtonLookAndFeel dispatches on (like
    // "PLAY"/"REC" above) - it draws an up arrow, pointing at the field this
    // button feeds.
    captureStartButton.setButtonText("CAPTURE");
    captureStartButton.setTooltip("Set the range start to the current playhead position");
    captureStartButton.setLookAndFeel(transportButtonLookAndFeel.get());
    captureStartButton.onClick = [this] { if (onCaptureRangeStart) onCaptureRangeStart(); };
    addAndMakeVisible(captureStartButton);

    captureEndButton.setButtonText("CAPTURE");
    captureEndButton.setTooltip("Set the range end to the current playhead position");
    captureEndButton.setLookAndFeel(transportButtonLookAndFeel.get());
    captureEndButton.onClick = [this] { if (onCaptureRangeEnd) onCaptureRangeEnd(); };
    addAndMakeVisible(captureEndButton);

    setRangeControlsEnabled(false);
    setRangeValues(0, 0, false);
    setFullState(false, false);
    updateLoopButton();

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
    captureStartButton.setLookAndFeel(nullptr);
    captureEndButton.setLookAndFeel(nullptr);
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

juce::String GlobalSectionComponent::formatSeconds(int seconds)
{
    seconds = juce::jmax(0, seconds);
    return juce::String::formatted("%02d:%02d", seconds / 60, seconds % 60);
}

void GlobalSectionComponent::configureRangeField(juce::Label& field, const juce::String& name,
                                                  std::function<void(int)>& callback)
{
    field.setJustificationType(juce::Justification::centred);
    field.setColour(juce::Label::backgroundColourId, juce::Colour(0xff14141f));
    field.setColour(juce::Label::textColourId, juce::Colours::white);
    field.setFont(juce::Font(12.0f));
    // Double-click rather than single: this bar gets clicked at speed in a
    // dark venue, and a stray single click dropping a range point into an
    // edit box mid-set would be its own small disaster. The capture buttons
    // below are the fast path; typing is the precise one.
    field.setEditable(false, true, false);
    field.setTooltip(name + " - Double Click to enter a value or click the arrow below");

    field.onTextChange = [this, &field, &callback]
    {
        auto text = field.getText().trim();
        int seconds = -1;

        if (text.containsChar(':'))
        {
            auto minutesPart = text.upToFirstOccurrenceOf(":", false, false).trim();
            auto secondsPart = text.fromFirstOccurrenceOf(":", false, false).trim();
            if (minutesPart.containsOnly("0123456789") && secondsPart.containsOnly("0123456789")
                && minutesPart.isNotEmpty() && secondsPart.isNotEmpty())
                seconds = minutesPart.getIntValue() * 60 + secondsPart.getIntValue();
        }
        else if (text.isNotEmpty() && text.containsOnly("0123456789"))
        {
            seconds = text.getIntValue();
        }

        if (seconds >= 0 && callback)
        {
            // The owner decides what the typed value actually becomes (it
            // may refuse an inverted range) and pushes the outcome back
            // through setRangeValues() - so nothing is written back here.
            callback(seconds);
        }
        else
        {
            // Unparseable, or nobody listening: revert rather than leaving
            // the field showing something that isn't the range.
            setRangeValues(displayedRangeStartSeconds, displayedRangeEndSeconds, rangeValuesGhosted);
        }
    };

    addAndMakeVisible(field);
}

void GlobalSectionComponent::RangeCaptionComponent::setDimmed(bool shouldBeDimmed)
{
    if (dimmed == shouldBeDimmed)
        return;
    dimmed = shouldBeDimmed;
    repaint();
}

void GlobalSectionComponent::RangeCaptionComponent::paint(juce::Graphics& g)
{
    auto colour = dimmed ? juce::Colour(0xff5a5a68) : juce::Colour(0xffaaaaaa);
    g.setColour(colour);

    auto bounds = getLocalBounds().toFloat();
    g.setFont(juce::Font(10.0f));
    g.drawText("RANGE", bounds, juce::Justification::centred, false);

    // A small triangle hard against each edge: the left one points back at
    // the Full toggle, the right one on at the start/end fields, so the
    // caption reads as naming both rather than only whatever it happens to
    // sit next to.
    float height = juce::jmin(6.0f, bounds.getHeight() * 0.34f);
    float width  = height * 0.62f;
    float centreY = bounds.getCentreY();

    juce::Path left;
    left.addTriangle(bounds.getX() + 1.0f, centreY,
                     bounds.getX() + 1.0f + width, centreY - height * 0.5f,
                     bounds.getX() + 1.0f + width, centreY + height * 0.5f);
    g.fillPath(left);

    juce::Path right;
    right.addTriangle(bounds.getRight() - 1.0f, centreY,
                      bounds.getRight() - 1.0f - width, centreY - height * 0.5f,
                      bounds.getRight() - 1.0f - width, centreY + height * 0.5f);
    g.fillPath(right);
}

void GlobalSectionComponent::applyToggleColours(juce::TextButton& button, bool on, bool enabled)
{
    // Same muted green "engaged" treatment as Show Mode and the active Play
    // button, so a lit control means the same thing everywhere in this bar.
    auto background = on ? juce::Colour(0xff2a6b3d) : juce::Colour(0xff2a2a3e);
    auto text       = on ? juce::Colours::white     : juce::Colour(0xffaaaaaa);

    if (! enabled)
    {
        background = juce::Colour(0xff21212e);
        text       = juce::Colour(0xff5a5a68);
    }

    button.setColour(juce::TextButton::buttonColourId, background);
    button.setColour(juce::TextButton::textColourOffId, text);
    button.setEnabled(enabled);
}

void GlobalSectionComponent::updateLoopButton()
{
    // LOOP is meaningless without a range, and equally meaningless while
    // recording - a record pass runs linearly straight past the range end
    // and never wraps, so the control dims to say the setting isn't in
    // force right now rather than silently lying about what will happen.
    bool available = rangeEnabled && ! recordingInProgress;
    applyToggleColours(loopButton, loopEnabled, available);
    loopButton.setTooltip(recordingInProgress
        ? "Loop is off while recording - a take records straight past the range end"
        : (loopEnabled ? "Looping the range - click to stop at the range end instead"
                       : "Stopping at the range end - click to loop the range instead"));
}

void GlobalSectionComponent::updateFullButton()
{
    applyToggleColours(fullButton, fullEnabled, fullAvailable && rangeEnabled);
    fullButton.setTooltip(fullAvailable
        ? (fullEnabled ? "Showing the whole of the recorded material - click to get your own range back"
                       : "Temporarily play the whole of the recorded material, keeping your range")
        : "Set a range first - Full swaps between your range and the whole of the material");
}

void GlobalSectionComponent::setLoopEnabled(bool enabled)
{
    loopEnabled = enabled;
    updateLoopButton();
}

void GlobalSectionComponent::setFullState(bool available, bool on)
{
    fullAvailable = available;
    fullEnabled   = on;
    updateFullButton();
}

void GlobalSectionComponent::setRecordingInProgress(bool recording)
{
    recordingInProgress = recording;
    updateLoopButton();
}

void GlobalSectionComponent::setRangeValues(int startSeconds, int endSeconds, bool ghosted)
{
    displayedRangeStartSeconds = startSeconds;
    displayedRangeEndSeconds   = endSeconds;
    rangeValuesGhosted         = ghosted;

    // Nothing selected: the fields show that there is no range rather than
    // a pair of zeroes that look like a real (empty) one.
    auto startText = rangeEnabled ? formatSeconds(startSeconds) : juce::String("--:--");
    auto endText   = rangeEnabled ? formatSeconds(endSeconds)   : juce::String("--:--");

    rangeStartField.setText(startText, juce::dontSendNotification);
    rangeEndField.setText(endText, juce::dontSendNotification);

    // Greyed and italic while Full is showing the material's bounds - these
    // are real values, they're just not the ones the user chose.
    auto textColour = ! rangeEnabled ? juce::Colour(0xff5a5a68)
                                     : (ghosted ? juce::Colour(0xff8a8a9a) : juce::Colours::white);
    auto font = juce::Font(12.0f);
    if (ghosted)
        font = font.italicised();

    for (auto* field : { &rangeStartField, &rangeEndField })
    {
        field->setColour(juce::Label::textColourId, textColour);
        field->setFont(font);
    }
}

void GlobalSectionComponent::setRangeControlsEnabled(bool enabled)
{
    rangeEnabled = enabled;

    rangeStartField.setEditable(false, enabled, false);
    rangeEndField.setEditable(false, enabled, false);
    rangeCaption.setDimmed(! enabled);

    if (! enabled)
        setCaptureButtonsEnabled(false, false);

    updateLoopButton();
    updateFullButton();
    setRangeValues(displayedRangeStartSeconds, displayedRangeEndSeconds, rangeValuesGhosted);
}

void GlobalSectionComponent::setCaptureButtonsEnabled(bool startEnabled, bool endEnabled)
{
    // Called every timer tick as the playhead moves, so it leans on
    // Component::setEnabled()/TextButton's own no-op-when-unchanged
    // behaviour rather than repainting on every tick.
    captureStartButton.setEnabled(rangeEnabled && startEnabled);
    captureEndButton.setEnabled(rangeEnabled && endEnabled);
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

    // --- Center zone: Tempo/Sync + port selector, then the two-row
    // transport block - centered as one block within the full bar
    // (withSizeKeepingCentre) rather than left-aligned in whatever space
    // remains.
    auto centerZone = fullArea.withSizeKeepingCentre(centerZoneWidth, rowHeight);
    tempoSyncComponent.setBounds(centerZone.removeFromLeft(TempoSyncComponent::preferredWidth)
                                    .withSizeKeepingCentre(TempoSyncComponent::preferredWidth,
                                                            TempoSyncComponent::preferredHeight));
    centerZone.removeFromLeft(zoneGap);

    // The transport block: LOOP / RANGE / FULL / start / end across the top,
    // Play / Rec / RTZ / capture-arrows-around-the-readout below, every
    // column on the same transportButtonWidth grid. Its two rows come to
    // exactly TempoSyncComponent's own height, so the two blocks sit level
    // and the bar's height doesn't change.
    auto transportBlock = centerZone.removeFromLeft(transportBlockWidth)
                              .withSizeKeepingCentre(transportBlockWidth,
                                                      transportRowHeight * 2 + transportRowGap);
    auto topRow = transportBlock.removeFromTop(transportRowHeight);
    transportBlock.removeFromTop(transportRowGap);
    auto bottomRow = transportBlock;

    loopButton.setBounds(topRow.removeFromLeft(transportButtonWidth));
    topRow.removeFromLeft(zoneGap);
    fullButton.setBounds(topRow.removeFromLeft(transportButtonWidth));
    topRow.removeFromLeft(zoneGap);
    // The caption goes between the two things it names, so its arrows have
    // something to point at on both sides.
    rangeCaption.setBounds(topRow.removeFromLeft(transportButtonWidth));
    topRow.removeFromLeft(zoneGap);
    // What's left of topRow is exactly rangeGroupWidth - the span the row
    // below has to line up with.
    rangeStartField.setBounds(topRow.removeFromLeft(transportButtonWidth));
    topRow.removeFromLeft(zoneGap);
    rangeEndField.setBounds(topRow);

    playPauseButton.setBounds(bottomRow.removeFromLeft(transportButtonWidth));
    bottomRow.removeFromLeft(zoneGap);
    recordButton.setBounds(bottomRow.removeFromLeft(transportButtonWidth));
    bottomRow.removeFromLeft(zoneGap);
    rtzButton.setBounds(bottomRow.removeFromLeft(transportButtonWidth));
    bottomRow.removeFromLeft(zoneGap);

    // The one alignment rule the cluster is built on: what's left of
    // bottomRow is the same rangeGroupWidth span the two fields above fill,
    // and the capture buttons go flush against its outer edges - so each
    // arrow sits under the field it feeds, and the position readout lands
    // dead centre between them as a consequence rather than as a separate
    // adjustment.
    captureStartButton.setBounds(bottomRow.removeFromLeft(captureArrowWidth));
    captureEndButton.setBounds(bottomRow.removeFromRight(captureArrowWidth));
    timeDisplayLabel.setBounds(bottomRow.withSizeKeepingCentre(positionReadoutWidth, transportRowHeight));
}
