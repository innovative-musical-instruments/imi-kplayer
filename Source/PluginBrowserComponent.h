#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class PluginBrowserComponent : public juce::Component
{
public:
    using PluginSelectedCallback = std::function<void(const juce::PluginDescription&)>;

    // allowInstruments: pass false to filter instrument plugins out of the
    // list (used for insert slots, which are audio-effect-only per spec).
    PluginBrowserComponent(juce::KnownPluginList& list,
                           PluginSelectedCallback callback,
                           bool allowInstruments = true);
    ~PluginBrowserComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Launch as a CallOutBox anchored to a component
    static void showAsCallOut(juce::KnownPluginList& list,
                              PluginSelectedCallback callback,
                              juce::Component& anchorComponent,
                              bool allowInstruments = true);

private:
    juce::KnownPluginList& pluginList;
    PluginSelectedCallback onPluginSelected;
    bool allowInstruments;

    juce::TextEditor searchBox;
    juce::ListBox    listBox;

    struct PluginListModel : public juce::ListBoxModel
    {
        juce::Array<juce::PluginDescription> items;
        std::function<void(const juce::PluginDescription&)> onSelected;

        int getNumRows() override { return items.size(); }

        void paintListBoxItem(int row, juce::Graphics& g,
                              int w, int h, bool selected) override
        {
            if (selected)
                g.fillAll(juce::Colour(0xff3d5a80));
            else
                g.fillAll(row % 2 == 0 ? juce::Colour(0xff1e1e2e)
                                        : juce::Colour(0xff252535));

            g.setColour(juce::Colours::white);
            g.setFont(13.0f);

            if (row < items.size())
            {
                auto& desc = items.getReference(row);
                auto text  = desc.name + "  [" + desc.pluginFormatName + "]";
                g.drawText(text, 8, 0, w - 16, h,
                           juce::Justification::centredLeft);
            }
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            if (row >= 0 && row < items.size() && onSelected)
                onSelected(items.getReference(row));
        }

        void returnKeyPressed(int row) override
        {
            if (row >= 0 && row < items.size() && onSelected)
                onSelected(items.getReference(row));
        }
    };

    PluginListModel listModel;
    void updateList(const juce::String& filter = {});

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserComponent)
};
