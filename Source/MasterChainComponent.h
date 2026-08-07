#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MasterChainProcessor.h"
#include "PeakMeterComponent.h"
#include "ConsoleFaderLookAndFeel.h"
#include "SlotButtonLookAndFeel.h"
#include "TransportButtonLookAndFeel.h"

// The master bus strip: the insert-slot buttons (same load/replace/remove/
// bypass popup-menu pattern as ChannelComponent's insert slots, applied
// once to the master sum instead of once per channel) with the output
// gain fader and its peak meter integrated directly below them, mirroring
// how a channel bundles its own gain fader under its insert slots.
class MasterChainComponent : public juce::Component
{
public:
    static constexpr int numSlots = MasterChainProcessor::numSlots;

    std::function<void(int slotIndex)> onLoadPlugin;
    std::function<void(int slotIndex)> onReplacePlugin;
    std::function<void()> onDirty;

    // Fired with the new linear gain whenever the user moves the fader.
    std::function<void(float linearGain)> onVolumeChanged;

    // Multitrack recording (see RecordingManager) - MainComponent owns the
    // arm/recording-active truth and is the one global transport for every
    // armed channel and the master together; this component only reflects
    // that state via setArmed()/setRecordingActive() and reports user
    // clicks. The arm toggle stays clickable while recording is active, same
    // as a channel's own arm button - arming mid-take takes effect
    // immediately rather than being rejected.
    std::function<void(bool armed)> onMasterArmToggled;
    std::function<void()> onRecordButtonClicked;

    // Minimal session transport for MIDI Take playback (Increment B, see
    // docs/kplayer-take-recording-playback-spec.md and SessionTransport's
    // own header for the full design) - independent of the arm/record
    // controls above. Not MIDI-remote-controllable in this increment
    // (unlike arm/record's CC104/102), so the Play/Pause button's visual
    // state is purely self-managed here and reported outward, same shape as
    // armButton's own onClick below - no external setter needed since
    // nothing else can change it out from under the button.
    std::function<void()> onPlayPauseClicked;
    std::function<void()> onRtzClicked;

    // Emergency all-notes-off/all-sound-off, injected into every loaded
    // instrument regardless of channel/device routing - see
    // MainComponent::triggerPanic(). Purely a fire-and-forget request, no
    // visual latch on this button (unlike arm/record) since there's no
    // ongoing state to reflect back.
    std::function<void()> onPanicClicked;

    explicit MasterChainComponent(MasterChainProcessor& processor);
    ~MasterChainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Sets the displayed fader position without firing onVolumeChanged -
    // for MainComponent to push a value in (session load, external change).
    void setVolume(float linearGain);

    void setLevelMeterSources(const std::atomic<float>* leftLevel,
                              const std::atomic<float>* rightLevel,
                              std::atomic<bool>* clipFlagLeft,
                              std::atomic<bool>* clipFlagRight);

    void setArmed(bool shouldBeArmed);
    void setRecordingActive(bool active);

private:
    void showPluginSlotMenu(int slotIndex);
    void updateSlotButton(int slotIndex);

    MasterChainProcessor& processor;

    juce::Label titleLabel;
    std::array<juce::TextButton, numSlots> slotButtons;
    std::unique_ptr<SlotButtonLookAndFeel> slotButtonLookAndFeel;

    std::unique_ptr<ConsoleFaderLookAndFeel> volumeFaderLookAndFeel;
    juce::Label      volumeLabel;
    juce::Slider     volumeSlider;
    PeakMeterComponent leftLevelMeter;
    PeakMeterComponent rightLevelMeter;

    juce::TextButton armButton;
    juce::TextButton recordButton;
    bool armed = false;
    bool recordingActive = false;
    void updateArmAndRecordButtons();

    juce::TextButton playPauseButton;
    juce::TextButton rtzButton;
    std::unique_ptr<TransportButtonLookAndFeel> transportButtonLookAndFeel;
    bool transportPlaying = false;
    void updateTransportButtons();

    juce::TextButton panicButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChainComponent)
};
