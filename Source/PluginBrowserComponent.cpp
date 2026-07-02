#include "PluginBrowserComponent.h"

PluginBrowserComponent::PluginBrowserComponent(juce::KnownPluginList& list,
                                               PluginSelectedCallback callback)
    : pluginList(list), onPluginSelected(std::move(callback))
{
    listModel.onSelected = [this](const juce::PluginDescription& desc)
    {
        // Schedule the callback asynchronously so the CallOutBox
        // can close itself naturally before we do anything heavy
        if (onPluginSelected)
        {
            auto cb = onPluginSelected;
            juce::MessageManager::callAsync([cb, desc]() mutable
            {
                cb(desc);
            });
        }

        // Ask the CallOutBox to dismiss itself cleanly on the message thread
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };

    searchBox.setTextToShowWhenEmpty("Search plugins...", juce::Colours::grey);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a3e));
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.onTextChange = [this] { updateList(searchBox.getText()); };
    addAndMakeVisible(searchBox);

    listBox.setModel(&listModel);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e2e));
    listBox.setRowHeight(26);
    addAndMakeVisible(listBox);

    updateList();
    setSize(400, 360);
}

PluginBrowserComponent::~PluginBrowserComponent() {}

void PluginBrowserComponent::updateList(const juce::String& filter)
{
    listModel.items.clear();
    auto types = pluginList.getTypes();
    for (auto& t : types)
    {
        if (filter.isEmpty() ||
            t.name.containsIgnoreCase(filter) ||
            t.manufacturerName.containsIgnoreCase(filter))
        {
            listModel.items.add(t);
        }
    }
    listBox.updateContent();
    listBox.repaint();
}

void PluginBrowserComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void PluginBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    searchBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    listBox.setBounds(area);
}

void PluginBrowserComponent::showAsCallOut(juce::KnownPluginList& list,
                                            PluginSelectedCallback callback,
                                            juce::Component& anchorComponent)
{
    auto* browser = new PluginBrowserComponent(list, std::move(callback));
    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(browser),
        anchorComponent.getScreenBounds(),
        nullptr
    );
}
