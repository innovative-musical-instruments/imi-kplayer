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
    static constexpr int timeWidth             = 64;
    // Play/Rec/RTZ, and (independently, in TempoSyncComponent) its Sync
    // button - all sized to match so the whole transport cluster reads as
    // one consistent size.
    static constexpr int transportButtonWidth  = 64;
    static constexpr int zoneGap               = 8;

    static constexpr int leftZoneWidth   = logoWidth + zoneGap + channelsWidth + zoneGap
                                          + settingsWidth + zoneGap + hideIOWidth;
    static constexpr int rightZoneWidth  = panicWidth + zoneGap + showModeWidth + zoneGap + logoWidth;
    static constexpr int centerZoneWidth = TempoSyncComponent::preferredWidth + zoneGap + timeWidth
                                          + zoneGap + transportButtonWidth * 3 + zoneGap * 2;

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

    juce::TextButton showModeButton;
    bool showModeEnabled = false;

    juce::TextButton panicButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalSectionComponent)
};
