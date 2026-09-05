#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrandingStripComponent.h"
#include "TempoSyncComponent.h"
#include "TransportButtonLookAndFeel.h"
#include "GearButtonLookAndFeel.h"

// The bottom, always-visible bar: branding, channel-count control, access
// to Settings, the global channel-I/O collapse toggle, tempo/sync, session
// transport (Play/Rec/RTZ), and Panic - everything that isn't scoped to a
// single channel or to the master bus signal chain (that's
// MasterChainComponent, its own separate vertical strip above this bar -
// see MainComponent::resized() for the actual layout). Laid out as three
// horizontal zones (see resized()): left (IMI logo, Channels, Settings,
// Hide/Show I/O), centered (Tempo/Sync + port selector, time display,
// Play, Rec Ready, RTZ), and right (Work/Show Mode, Panic, Tribal Tools
// logo).
class GlobalSectionComponent : public juce::Component,
                               private juce::Timer
{
public:
    enum class RecordState { idle, armed, recording };

    // Fixed bar height MainComponent::resized() reserves along the bottom
    // edge - tall enough for TempoSyncComponent's own two-row layout
    // (preferredHeight 48) plus this bar's own top/bottom padding.
    static constexpr int preferredHeight = 64;

    // Per-element widths used by resized() - named here rather than left as
    // inline literals so minimumWindowWidth below is computed from the same
    // numbers the actual layout uses and can't silently drift out of sync
    // with it (that drift is exactly what let the left/center/right zones
    // overlap at a narrow window width before this got a real minimum).
    static constexpr int logoWidth             = 50;
    static constexpr int channelsWidth         = 100;
    static constexpr int settingsWidth         = 40;
    static constexpr int hideIOWidth           = 80;
    static constexpr int panicWidth            = 70;
    static constexpr int showModeWidth         = 90;
    // Play/Rec/RTZ, LOOP/FULL above them, and (independently, in
    // TempoSyncComponent) its Sync button - all sized to match so the whole
    // transport cluster reads as one consistent size. This doubles as the
    // uniform column width the whole two-row transport block is built on:
    // it's the narrowest column that still fits an mm:ss field in a
    // monospaced font, which is why adding the Range controls didn't have
    // to move it.
    static constexpr int transportButtonWidth  = 64;
    static constexpr int zoneGap               = 8;

    // The transport block is two 22px rows with a 4px gap - 48px total,
    // exactly TempoSyncComponent's own height, so the two sit level.
    static constexpr int transportRowHeight    = 22;
    static constexpr int transportRowGap       = 4;

    // The Range start/end pair spans two grid columns plus the gap between
    // them, and the capture-arrow row below spans exactly the same width -
    // that shared span is the one alignment rule the whole cluster is built
    // on (see resized()). The arrows sit flush against its two outer edges,
    // which is what puts the position readout dead centre between them
    // rather than needing to be centred separately.
    static constexpr int rangeGroupWidth       = transportButtonWidth * 2 + zoneGap;
    static constexpr int captureArrowWidth     = 30;
    static constexpr int positionReadoutWidth  = 60;

    // Play/Rec/RTZ across the bottom row, then the range group.
    static constexpr int transportBlockWidth   = transportButtonWidth * 3 + zoneGap * 3 + rangeGroupWidth;

    static constexpr int leftZoneWidth   = logoWidth + zoneGap + channelsWidth + zoneGap
                                          + settingsWidth + zoneGap + hideIOWidth;
    static constexpr int rightZoneWidth  = panicWidth + zoneGap + showModeWidth + zoneGap + logoWidth;
    static constexpr int centerZoneWidth = TempoSyncComponent::preferredWidth + zoneGap + transportBlockWidth;

    // Smallest full window width at which the left/center/right zones
    // still fit without overlapping (the centered zone is positioned
    // relative to the *whole* bar width, not the leftover space after the
    // other two - see resized() - so a too-narrow window lets it collide
    // with whichever side zone is wider). MainWindow's constructor feeds
    // this straight into setResizeLimits(). The +40/+20/+20 account for
    // MainComponent::resized()'s own reduced(20) margin on each side, this
    // component's own reduced(10, 8) horizontal inset, and a little
    // breathing room, respectively.
    static constexpr int minimumWindowWidth =
        2 * ((leftZoneWidth > rightZoneWidth ? leftZoneWidth : rightZoneWidth) + zoneGap)
        + centerZoneWidth + 40 + 20 + 20;

    GlobalSectionComponent(int initialChannelCount, int maxChannelCount);
    ~GlobalSectionComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Fired with the requested new count (already clamped to
    // [1, maxChannelCount] and with +/- disabled at the extremes) whenever
    // the +/- boxes are clicked. The owner decides whether to actually
    // apply it (e.g. confirming a destructive shrink first) and calls
    // setChannelCount() back with the real outcome either way.
    std::function<void(int)> onChannelCountChangeRequested;
    // Reflects external state (session load, or reverting a cancelled
    // shrink) without firing the callback above.
    void setChannelCount(int count);

    std::function<void()> onSettingsRequested;

    std::function<void()> onCollapseToggled;
    void setInputSectionCollapsed(bool collapsed);

    // MainComponent owns the actual tempo/sync state and wires this
    // component's callbacks/setters directly, same relationship it already
    // had before this component existed - just reached through here now.
    TempoSyncComponent& getTempoSyncComponent() { return tempoSyncComponent; }

    // Transport time readout (mm:ss), between Tempo/Sync and the transport
    // row - see MainComponent::timerCallback() for where it's computed.
    // Doubles as a recording-elapsed display too whenever recording is
    // active, since Record Ready only ever starts recording together with
    // the transport playing - one clock covers both. Mainly there so
    // "Play with nothing selected to actually play" (silently advancing an
    // otherwise-inaudible playhead) still gives some visible feedback that
    // something is happening, rather than looking inert.
    void setDisplayedTime(const juce::String& text);

    // The readout is editable too - typing a time jumps the playhead there,
    // so finding a spot in a long Take doesn't mean listening to it. Fired
    // with the typed value already parsed to whole seconds; unparseable
    // text is reverted rather than reported.
    std::function<void(int)> onPositionEdited;

    std::function<void()> onPlayPauseClicked;
    std::function<void()> onRtzClicked;
    void setTransportPlaying(bool playing);

    // Record Ready - the state machine itself lives in MainComponent
    // (toggleRecordArm()/startArmedRecording()), this component only
    // reflects it and reports clicks: idle -> click -> armed (blinking,
    // this component's own Timer) -> click -> back to idle, or Play while
    // armed transitions to recording (solid, owner's call) -> click stops
    // it (also the owner's call, this just forwards the click either way).
    std::function<void()> onRecordButtonClicked;
    void setRecordState(RecordState state);

    // Work Mode (default) vs. Show Mode - see MainComponent::
    // setShowModeEnabled()'s comment for the full reasoning. Dumb reflector
    // like onCollapseToggled above: reports the click, owner decides and
    // pushes the actual state back via setShowModeEnabled().
    std::function<void()> onShowModeToggled;
    void setShowModeEnabled(bool enabled);

    // Momentary visual only (a brief red flash on click) - no persistent
    // state to reflect back in, unlike Record Ready/Show Mode above.
    std::function<void()> onPanicClicked;

    // ---- Range ----------------------------------------------------------
    // The span of the session the transport plays, sitting on the row above
    // Play/Rec/RTZ. Like every other control in this bar these are dumb
    // reflectors: they report the gesture and MainComponent (which owns the
    // range state and pushes it into SessionTransport) decides what it
    // means and pushes the result back through the setters below.
    //
    // LOOP wraps at the range end instead of stopping there. FULL
    // temporarily shows the material's own bounds while remembering the
    // user's range. The two capture buttons pull the current playhead into
    // the start/end field above them - the reason there's a readout between
    // them at all.
    std::function<void()> onLoopToggled;
    std::function<void()> onFullToggled;
    std::function<void()> onCaptureRangeStart;
    std::function<void()> onCaptureRangeEnd;

    // Fired with the typed value already parsed to whole seconds (mm:ss, or
    // a bare number of seconds) - never with unparseable text, which is
    // simply reverted to what was displayed before.
    std::function<void(int)> onRangeStartEdited;
    std::function<void(int)> onRangeEndEdited;

    void setLoopEnabled(bool enabled);

    // ghosted = these are the material's bounds rather than the user's own
    // range (FULL engaged), drawn greyed and italic to say so.
    void setRangeValues(int startSeconds, int endSeconds, bool ghosted);

    // No Take selected anywhere means there is nothing for a range to span,
    // so the whole cluster goes dead - LOOP included. Play/Rec/RTZ stay
    // live throughout: you can still record with no range.
    void setRangeControlsEnabled(bool enabled);

    // FULL only means something once there is a user range for it to differ
    // from, so it's disabled until then. `on` lights it.
    void setFullState(bool available, bool on);

    // Dims whichever capture button would produce an inverted range, so the
    // invalid state is unreachable rather than silently corrected.
    void setCaptureButtonsEnabled(bool startEnabled, bool endEnabled);

    // LOOP dims while recording - a record pass runs linearly past the
    // range end and never wraps - while the range values stay readable.
    void setRecordingInProgress(bool recording);

private:
    void timerCallback() override; // drives the armed-state blink only

    // Pinned to opposite ends of the bar (see resized()) rather than one
    // combined side-by-side strip - see BrandingStripComponent's own header
    // comment for why.
    BrandingStripComponent imiLogo    { BrandingStripComponent::Logo::imi };
    BrandingStripComponent tribalLogo { BrandingStripComponent::Logo::tribal };

    juce::Label channelsLabel;
    juce::TextButton channelMinusButton;
    juce::Label channelCountLabel;
    juce::TextButton channelPlusButton;
    int channelCount = 1;
    int maxChannels  = 1;
    void updateChannelButtons();

    juce::TextButton settingsButton;
    std::unique_ptr<GearButtonLookAndFeel> gearButtonLookAndFeel;

    juce::TextButton collapseInputButton;
    bool inputCollapsed = false;

    TempoSyncComponent tempoSyncComponent;
    juce::Label timeDisplayLabel;

    juce::TextButton playPauseButton;
    juce::TextButton rtzButton;
    std::unique_ptr<TransportButtonLookAndFeel> transportButtonLookAndFeel;
    bool transportPlaying = false;
    void updateTransportButtons();

    juce::TextButton recordButton;
    RecordState recordState = RecordState::idle;
    bool blinkPhaseOn = false;
    void updateRecordButtonColour();

    // The caption sitting between FULL and the range fields, with a small
    // arrow on each side pointing at the two things it labels - the Full
    // toggle to its left and the start/end pair to its right. Vector-drawn
    // rather than a Unicode arrow in a Label, for the same Windows
    // font-coverage reason TransportButtonLookAndFeel exists.
    class RangeCaptionComponent : public juce::Component
    {
    public:
        void setDimmed(bool shouldBeDimmed);
        void paint(juce::Graphics&) override;

    private:
        bool dimmed = false;
    };

    // ---- Range cluster (see the callbacks/setters above) ----
    juce::TextButton loopButton;
    juce::TextButton fullButton;
    RangeCaptionComponent rangeCaption;
    juce::Label      rangeStartField;     // editable, mm:ss
    juce::Label      rangeEndField;
    juce::TextButton captureStartButton;  // pulls the playhead into the field above it
    juce::TextButton captureEndButton;

    bool loopEnabled       = false;
    bool rangeEnabled      = false;
    bool fullAvailable     = false;
    bool fullEnabled       = false;
    bool recordingInProgress = false;

    // What setRangeValues() last put on screen, so an unparseable edit can
    // be reverted to it without the owner having to push a correction.
    // The last time pushed in by the owner's timer, so an unparseable edit
    // of the readout can be reverted to it.
    juce::String displayedTimeText { "00:00" };
    int displayedRangeStartSeconds = 0;
    int displayedRangeEndSeconds   = 0;
    bool rangeValuesGhosted        = false;

    // The two toggles share one look (same "lit" treatment as Show Mode) -
    // centralised so LOOP's extra recording-dim rule is the only difference
    // between them.
    void updateLoopButton();
    void updateFullButton();
    static void applyToggleColours(juce::TextButton& button, bool on, bool enabled);
    void configureRangeField(juce::Label& field, const juce::String& name,
                             std::function<void(int)>& callback);
    static juce::String formatSeconds(int seconds);
    // mm:ss, or a bare number of seconds. Returns false for anything else.
    static bool parseTimeSeconds(const juce::String& text, int& secondsOut);

    juce::TextButton showModeButton;
    bool showModeEnabled = false;

    juce::TextButton panicButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalSectionComponent)
};
