#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

// Master-column control for the transport-wide tempo (moved out of the
// Settings dialog) plus MIDI-clock sync: a manually-editable BPM value when
// unsynced, or a live read-only display driven by MidiClockTempoDetector
// once a sync source is selected and locked. MainComponent owns the actual
// tempo/sync state and is the only thing that talks to MIDI devices for
// this feature - this component only displays that state and reports
// user-driven changes via its callbacks, the same relationship
// ChannelComponent has with ChannelProcessor's MIDI routing.
class TempoSyncComponent : public juce::Component,
                           private juce::Label::Listener
{
public:
    static constexpr int preferredHeight = 48;
    // Width GlobalSectionComponent's center zone reserves for this
    // component - see resized()'s own layout: row 1 is the live tempo
    // value plus a narrow (~8-character) MIDI sync device selector, row 2
    // is the Sync toggle (sized to match GlobalSectionComponent's Play
    // button) plus the "Tempo" heading.
    static constexpr int preferredWidth = 160;

    // Range a tempo may be typed or nudged to. The upper bound is
    // deliberately far above what reads as a musical tempo: Kadabra's own
    // sequencer runs well past the conventional ~300 ceiling, K-Player's
    // MIDI-clock sync has always followed it up there without complaint
    // (the detector doesn't clamp at all), and it was only the two manual
    // entry paths that refused - so typing a tempo couldn't reach one that
    // syncing arrived at perfectly happily. Kept here rather than as
    // literals at each site so the two can't drift apart again.
    static constexpr double minimumTempoBpm = 20.0;
    static constexpr double maximumTempoBpm = 1200.0;

    // Fired when the user edits the BPM value directly (only possible while
    // sync is off - the value label isn't editable while synced).
    std::function<void(double)> onTempoChanged;
    // Fired when the user clicks the Sync toggle.
    std::function<void(bool)> onSyncToggled;
    // Fired when the user picks a different sync source device; an empty
    // identifier means "None".
    std::function<void(juce::String)> onSyncDeviceChanged;

    // ---- Metronome click ----
    // The Click toggle sits directly above Sync. A left click toggles it;
    // a right click (or ctrl-click on Mac) asks for the options menu, which
    // GlobalSectionComponent's owner builds and shows - this component only
    // reports the gesture, same dumb-reflector relationship it has with
    // everything else here.
    std::function<void()> onClickToggled;
    std::function<void()> onClickOptionsRequested;
    void setClickEnabled(bool enabled);

    TempoSyncComponent();
    ~TempoSyncComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // All setters below reflect external state (session load, a sync-driven
    // tempo tick, a device list change) without firing the callbacks above -
    // the same dontSendNotification convention used throughout the rest of
    // the channel-strip UI.
    void setDisplayedTempo(double bpm);
    void setSyncEnabled(bool enabled);
    void setSyncDeviceIdentifier(const juce::String& identifier);
    // Only meaningful while sync is enabled - colours the device selector to
    // match the existing "device selected but nothing's coming through"
    // warning convention already used by each channel's own MIDI In box.
    void setSyncSignalWarning(bool noSignal);

private:
    void labelTextChanged(juce::Label*) override;
    void editorShown(juce::Label*, juce::TextEditor&) override;
    void refreshMidiDeviceList();
    int  midiDeviceItemIdFor(const juce::String& identifier) const;
    void updateTempoValueLabel();

    // A TextButton that reports right-clicks separately from left-clicks,
    // so one control can both toggle the click and open its options.
    class ClickToggleButton : public juce::TextButton
    {
    public:
        std::function<void()> onPopupMenuRequested;
        void mouseDown(const juce::MouseEvent& event) override
        {
            // isPopupMenu() covers right-click on both platforms and
            // ctrl-click on Mac, which is the gesture people actually use
            // there.
            if (event.mods.isPopupMenu())
            {
                if (onPopupMenuRequested)
                    onPopupMenuRequested();
                return;
            }
            juce::TextButton::mouseDown(event);
        }
    };

    juce::Label tempoLabel;
    juce::Label tempoValueLabel;
    ClickToggleButton clickButton;
    juce::TextButton syncButton;
    juce::ComboBox   syncDeviceBox;
    bool clickEnabled = false;
    void updateClickButton();

    // "Tempo" + its live value framed together as one visual group (see
    // paint()) - cached here in resized() rather than recomputed in
    // paint(), since it's just tempoLabel's and tempoValueLabel's combined
    // bounds with a little padding, and resized() already has both handy.
    juce::Rectangle<int> tempoFrameArea;

    juce::Array<juce::MidiDeviceInfo> availableMidiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;
    juce::String currentSyncDeviceId;

    bool   syncEnabled  = false;
    double displayedBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoSyncComponent)
};
