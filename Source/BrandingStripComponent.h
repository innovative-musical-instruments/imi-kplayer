#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// A single white-on-transparent brand logo (IMI or Tribal Tools), scaled to
// fit its bounds while preserving aspect ratio. Used twice by
// GlobalSectionComponent - one instance per logo, pinned to opposite ends
// of the horizontal global bar (IMI on the far left, Tribal Tools on the
// far right) - rather than one combined side-by-side strip, so each logo
// can be positioned independently within that bar's layout.
class BrandingStripComponent : public juce::Component
{
public:
    enum class Logo { imi, tribal };

    explicit BrandingStripComponent(Logo which)
    {
        image = (which == Logo::imi)
                  ? juce::ImageCache::getFromMemory(BinaryData::imilogowhite_png, BinaryData::imilogowhite_pngSize)
                  : juce::ImageCache::getFromMemory(BinaryData::tribaltoolslogo_png, BinaryData::tribaltoolslogo_pngSize);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(4, 6);

        if (image.isValid())
            g.drawImage(image, area.toFloat(), juce::RectanglePlacement::centred);
    }

private:
    juce::Image image;
};
