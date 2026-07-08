#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Draws gain/pan as a console-mixer-style fader: a fully-highlighted groove
// (the default LookAndFeel's highlight only ever indicated the *filled*
// portion, which reads as a level meter rather than a fader position) plus
// a bigger rectangular cap with a centre grip line.
//
// The cap's two dimensions are independently scalable from the base size
// (26 x 20 - cross-axis x along-axis) via the constructor:
//   - lengthScale multiplies the cap's dimension along the slider's motion
//     axis: height for a vertical (gain-style) slider, width for a
//     horizontal (pan-style) one.
//   - crossScale multiplies the perpendicular dimension.
// Shared by ChannelComponent (channel gain/pan) and MasterChainComponent
// (master output gain, at a larger scale).
class ConsoleFaderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit ConsoleFaderLookAndFeel(float lengthScale = 1.0f, float crossScale = 1.0f)
        : capLengthScale(lengthScale), capCrossScale(crossScale)
    {
    }

    // Slider derives both the drawn thumb position AND the mouse-to-value
    // mapping from this layout's sliderBounds, so insetting it here (rather
    // than just clamping where we draw) keeps dragging and drawing in sync.
    // Without this, a cap taller/wider than the default thumb clips past
    // the track's own top/left edge at the max value, and overlaps the
    // text box at the min value, since the un-inset track lets sliderPos
    // reach all the way to the bounds' edges.
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout(slider);

        if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        {
            int inset = juce::roundToInt(20.0f * capLengthScale * 0.5f);
            layout.sliderBounds = layout.sliderBounds.reduced(0, inset);
        }
        else if (slider.getSliderStyle() == juce::Slider::LinearHorizontal)
        {
            int inset = juce::roundToInt(20.0f * capLengthScale * 0.5f);
            layout.sliderBounds = layout.sliderBounds.reduced(inset, 0);
        }

        return layout;
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        auto trackColour = juce::Colour(0xff3d5a80);
        auto capColour   = juce::Colours::white;
        auto capBorder   = juce::Colours::black.withAlpha(0.6f);

        const float crossAxisSize = 26.0f * capCrossScale;

        if (style == juce::Slider::LinearVertical)
        {
            const float trackWidth = 6.0f;
            juce::Rectangle<float> track(x + width * 0.5f - trackWidth * 0.5f,
                                         (float) y, trackWidth, (float) height);
            g.setColour(trackColour);
            g.fillRoundedRectangle(track, trackWidth * 0.5f);

            const float capHeight = 20.0f * capLengthScale;
            const float capWidth  = crossAxisSize;
            juce::Rectangle<float> cap(x + ((float) width - capWidth) * 0.5f,
                                      sliderPos - capHeight * 0.5f, capWidth, capHeight);
            g.setColour(capColour.withAlpha(0.5f));
            g.fillRoundedRectangle(cap, 3.0f);
            g.setColour(capBorder);
            g.drawRoundedRectangle(cap, 3.0f, 1.0f);
            g.drawLine(cap.getX() + 4, cap.getCentreY(), cap.getRight() - 4, cap.getCentreY(), 2.0f);
        }
        else if (style == juce::Slider::LinearHorizontal)
        {
            const float trackHeight = 6.0f;
            juce::Rectangle<float> track((float) x, y + height * 0.5f - trackHeight * 0.5f,
                                         (float) width, trackHeight);
            g.setColour(trackColour);
            g.fillRoundedRectangle(track, trackHeight * 0.5f);

            const float capWidth  = 20.0f * capLengthScale;
            const float capHeight = crossAxisSize;
            juce::Rectangle<float> cap(sliderPos - capWidth * 0.5f,
                                      y + ((float) height - capHeight) * 0.5f, capWidth, capHeight);
            g.setColour(capColour.withAlpha(0.5f));
            g.fillRoundedRectangle(cap, 3.0f);
            g.setColour(capBorder);
            g.drawRoundedRectangle(cap, 3.0f, 1.0f);
            g.drawLine(cap.getCentreX(), cap.getY() + 4, cap.getCentreX(), cap.getBottom() - 4, 2.0f);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                             0.0f, 0.0f, style, slider);
        }
    }

private:
    float capLengthScale;
    float capCrossScale;
};
