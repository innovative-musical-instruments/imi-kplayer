#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// KPlayer About box content, per docs/KPlayer_Refinement_Spec_2026-07-11.md
// item 4.1/5 and the design handoff's "KPlayer About and Splash" mockup.
// Fixed 670x290. Meant to be reused as both the Help > About dialog content
// (item 4) and the launch splash (item 5, progress bar not yet wired here -
// this first pass covers the About dialog content/layout only).
//
// JUCE has no built-in widget for flowing paragraph text with inline
// clickable hyperlinks, so this lays words out itself (see
// layoutParagraph()) rather than pulling in juce_gui_extra's
// HyperlinkButton, which only supports whole-line links.
class AboutScreenComponent : public juce::Component
{
public:
    // splashMode swaps the top bar for a progress bar and the footer's
    // empty left side for "LAUNCHING KPLAYER...", and disables the fake
    // close control entirely - matches the design spec's launch-splash
    // variant, which is otherwise identical to the About dialog (item 5).
    explicit AboutScreenComponent(bool splashMode = false);
    ~AboutScreenComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Splash-mode only; 0-1, drives the top progress bar's fill.
    void setProgress(float newProgress);

    static constexpr int fixedWidth  = 670;
    static constexpr int fixedHeight = 290;

    // Fired when the fake close control (macOS traffic-light red dot, or
    // Windows titlebar x) is clicked - the host dialog isn't a native
    // titlebar, so it can't close itself without this. Never fires in
    // splash mode (no close control is drawn or hit-tested there).
    std::function<void()> onCloseRequested;

private:
    struct Word
    {
        juce::String text;
        juce::Font font;
        juce::Colour colour;
        bool isLink = false;
        juce::URL url;
        bool lineBreakBefore = false;
    };

    struct PositionedWord
    {
        juce::String text;
        juce::Font font;
        juce::Colour colour;
        juce::Rectangle<float> bounds;
        bool isLink = false;
        juce::URL url;
    };

    // Lays `words` out left-to-right within `bounds`, wrapping whenever a
    // word would overflow the right edge (or immediately, if the word's own
    // lineBreakBefore is set), and appends the positioned results to
    // positionedWords. Returns the height consumed.
    float layoutParagraph(const std::vector<Word>& words, juce::Rectangle<float> bounds, float lineHeight);

    void buildLayout();

    bool isSplash = false;
    float progress = 0.0f;

    juce::Typeface::Ptr azonixTypeface;
    juce::Image kplayerIcon;

    std::vector<PositionedWord> positionedWords;
    juce::Rectangle<float> iconBounds;
    juce::Rectangle<float> titleRowBounds;
    juce::Rectangle<float> closeControlBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutScreenComponent)
};
