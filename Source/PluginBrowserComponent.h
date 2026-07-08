#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginManager.h"

class PluginBrowserComponent : public juce::Component
{
public:
    using PluginSelectedCallback = std::function<void(const juce::PluginDescription&)>;

    // allowInstruments: pass false to filter instrument plugins out of the
    // list (used for insert slots, which are audio-effect-only per spec).
    PluginBrowserComponent(PluginManager& pluginManager,
                           PluginSelectedCallback callback,
                           bool allowInstruments = true);
    ~PluginBrowserComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Launch as a CallOutBox anchored to a component
    static void showAsCallOut(PluginManager& pluginManager,
                              PluginSelectedCallback callback,
                              juce::Component& anchorComponent,
                              bool allowInstruments = true);

private:
    PluginManager& pluginManager;
    PluginSelectedCallback onPluginSelected;
    bool allowInstruments;
    bool sortByManufacturer = false;

    void retryBlacklistedFile(const juce::String& fileOrIdentifier);
    void toggleFavorite(const juce::String& identifierString);
    // Schedules onPluginSelected asynchronously and dismisses the enclosing
    // CallOutBox - shared by double-click and Enter-key selection. Does NOT
    // record recently-used; that's for the caller to do only once the
    // plugin actually finishes loading (see PluginManager::noteRecentlyUsed).
    void selectPlugin(const juce::PluginDescription& desc);

    juce::TextEditor searchBox;
    juce::TextButton sortModeButton;
    juce::ListBox    listBox;

    // A single flat row list backs the ListBox, mixing non-selectable
    // section headers ("Favorites", "Recently Used", per-manufacturer
    // groups when sorted that way, "Failed to Load") with actual entries,
    // so favorites/recent/all-plugins/blacklisted can share one scrollable
    // view instead of several fixed-height sub-lists.
    struct Row
    {
        enum class Kind { SectionHeader, Plugin, Blacklisted };

        Kind kind = Kind::SectionHeader;
        juce::String headerText;
        juce::PluginDescription plugin;
        juce::String blacklistedPath;
    };

    struct PluginListModel : public juce::ListBoxModel
    {
        juce::Array<Row> rows;
        PluginBrowserComponent& owner;

        explicit PluginListModel(PluginBrowserComponent& ownerIn) : owner(ownerIn) {}

        int getNumRows() override { return rows.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;

        // A star glyph occupies the left edge of each Plugin row; clicking
        // it toggles favorite status without also selecting/loading it.
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
        void returnKeyPressed(int row) override;

        bool isRowSelectable(int row) const
        {
            return rows[row].kind != Row::Kind::SectionHeader;
        }
    };

    PluginListModel listModel;
    void updateList(const juce::String& filter = {});

    static constexpr int starColumnWidth = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserComponent)
};
