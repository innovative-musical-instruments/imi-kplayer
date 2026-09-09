#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// App-wide default LookAndFeel, purely to serve the IMI brand style guide's
// "Body/UI" typeface (Space Grotesk - see
// ../imi-common-docs/brand/BRAND.md) for every juce::Font that doesn't
// request a specific typeface by name - i.e. every plain juce::Font(size) /
// juce::Font(size, Font::bold) construction already scattered throughout
// the rest of this codebase (channel strips, global bar, Settings dialog,
// etc.), with zero changes needed at any of those call sites - JUCE routes
// all of them through getTypefaceForFont() below regardless.
//
// Azonix (the style guide's Display/headline font, ALL CAPS, "headlines
// and wordmarks only") is unaffected either way - AboutScreenComponent
// already builds its own juce::Font(azonixTypeface) directly for the splash
// title, which bypasses this lookup entirely rather than going through the
// default sans-serif name.
//
// Only Regular/Bold are embedded (see CMakeLists.txt) - Space Grotesk
// doesn't ship an italic in Google Fonts, and nothing in this app's UI
// requests one; an italic request just falls through to whichever of the
// two is closest, same as JUCE's own synthesized-italic fallback would
// have done anyway.
class KPlayerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KPlayerLookAndFeel()
    {
        regularTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::SpaceGroteskRegular_ttf, (size_t) BinaryData::SpaceGroteskRegular_ttfSize);
        boldTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::SpaceGroteskBold_ttf, (size_t) BinaryData::SpaceGroteskBold_ttfSize);
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        auto& target = font.isBold() ? boldTypeface : regularTypeface;
        if (target != nullptr)
            return target;

        // Embedded typeface failed to load (shouldn't happen - it's baked
        // into the binary, not read from disk) - fall back to the platform
        // default rather than drawing nothing.
        return LookAndFeel_V4::getTypefaceForFont(font);
    }

private:
    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr boldTypeface;
};
