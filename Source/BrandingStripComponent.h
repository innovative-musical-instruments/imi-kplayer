#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Fixed-size IMI + Tribal Tools logo strip, shown directly above the Master
// section (spec item 2). Both logos are white-on-transparent PNGs, drawn
// side by side at equal width, each scaled to fit its half while preserving
// aspect ratio.
class BrandingStripComponent : public juce::Component
{
public:
    BrandingStripComponent()
    {
        imiLogo    = juce::ImageCache::getFromMemory(BinaryData::imilogowhite_png, BinaryData::imilogowhite_pngSize);
        tribalLogo = juce::ImageCache::getFromMemory(BinaryData::tribaltoolslogo_png, BinaryData::tribaltoolslogo_pngSize);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(4, 6);
        auto imiArea    = area.removeFromLeft(area.getWidth() / 2).reduced(2, 0);
        auto tribalArea = area.reduced(2, 0);

        if (imiLogo.isValid())
            g.drawImage(imiLogo, imiArea.toFloat(), juce::RectanglePlacement::centred);

        if (tribalLogo.isValid())
            g.drawImage(tribalLogo, tribalArea.toFloat(), juce::RectanglePlacement::centred);
    }

private:
    juce::Image imiLogo;
    juce::Image tribalLogo;
};
