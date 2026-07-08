#include "PluginBrowserComponent.h"
#include <algorithm>

PluginBrowserComponent::PluginBrowserComponent(PluginManager& pm,
                                               PluginSelectedCallback callback,
                                               bool allowInstrumentsIn)
    : pluginManager(pm), onPluginSelected(std::move(callback)), allowInstruments(allowInstrumentsIn),
      listModel(*this)
{
    searchBox.setTextToShowWhenEmpty("Search plugins...", juce::Colours::grey);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a3e));
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.onTextChange = [this] { updateList(searchBox.getText()); };
    addAndMakeVisible(searchBox);

    sortModeButton.setButtonText("Sort: A-Z");
    sortModeButton.onClick = [this]
    {
        sortByManufacturer = ! sortByManufacturer;
        sortModeButton.setButtonText(sortByManufacturer ? "Sort: Manufacturer" : "Sort: A-Z");
        updateList(searchBox.getText());
    };
    addAndMakeVisible(sortModeButton);

    listBox.setModel(&listModel);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e2e));
    listBox.setRowHeight(26);
    addAndMakeVisible(listBox);

    updateList();
    setSize(420, 420);
}

PluginBrowserComponent::~PluginBrowserComponent() {}

void PluginBrowserComponent::selectPlugin(const juce::PluginDescription& desc)
{
    // Schedule asynchronously so the CallOutBox can close itself naturally
    // before we do anything heavy.
    if (onPluginSelected)
    {
        auto cb = onPluginSelected;
        juce::MessageManager::callAsync([cb, desc]() mutable
        {
            cb(desc);
        });
    }

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void PluginBrowserComponent::toggleFavorite(const juce::String& identifierString)
{
    pluginManager.setFavorite(identifierString, ! pluginManager.isFavorite(identifierString));
    updateList(searchBox.getText());
}

void PluginBrowserComponent::retryBlacklistedFile(const juce::String& fileOrIdentifier)
{
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::WarningIcon,
            "Retry Failed Plugin",
            "\"" + juce::File(fileOrIdentifier).getFileName() + "\" previously failed to load and was "
            "blacklisted - often because it crashed the plugin scanner. Retrying may crash K-Player. "
            "Continue?",
            "Retry", "Cancel", this),
        [this, fileOrIdentifier](int result)
        {
            if (result != 1)
                return;

            auto& pluginList = pluginManager.getPluginList();
            pluginList.removeFromBlacklist(fileOrIdentifier);

            for (auto* format : pluginManager.getFormatManager().getFormats())
            {
                if (format->fileMightContainThisPluginType(fileOrIdentifier))
                {
                    juce::OwnedArray<juce::PluginDescription> found;
                    pluginList.scanAndAddFile(fileOrIdentifier, false, found, *format);
                    break;
                }
            }

            updateList(searchBox.getText());
        });
}

void PluginBrowserComponent::updateList(const juce::String& filter)
{
    listModel.rows.clear();

    auto& pluginList = pluginManager.getPluginList();
    auto allTypes = pluginList.getTypes();

    auto allowed = [&](const juce::PluginDescription& d)
    {
        return allowInstruments || ! d.isInstrument;
    };
    auto matchesFilter = [&](const juce::PluginDescription& d)
    {
        return filter.isEmpty() || d.name.containsIgnoreCase(filter)
                                 || d.manufacturerName.containsIgnoreCase(filter);
    };
    auto byName = [](const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        return a.name.compareIgnoreCase(b.name) < 0;
    };

    // Favorites
    juce::Array<juce::PluginDescription> favoriteDescs;
    for (auto& d : allTypes)
        if (allowed(d) && matchesFilter(d) && pluginManager.isFavorite(d.createIdentifierString()))
            favoriteDescs.add(d);
    std::sort(favoriteDescs.begin(), favoriteDescs.end(), byName);

    if (! favoriteDescs.isEmpty())
    {
        listModel.rows.add({ Row::Kind::SectionHeader, "Favorites", {}, {} });
        for (auto& d : favoriteDescs)
            listModel.rows.add({ Row::Kind::Plugin, {}, d, {} });
    }

    // Recently used - kept in recency order, not alphabetical.
    juce::Array<juce::PluginDescription> recentDescs;
    for (auto& id : pluginManager.getRecentlyUsedIdentifiers())
        for (auto& d : allTypes)
            if (d.createIdentifierString() == id && allowed(d) && matchesFilter(d))
            {
                recentDescs.add(d);
                break;
            }

    if (! recentDescs.isEmpty())
    {
        listModel.rows.add({ Row::Kind::SectionHeader, "Recently Used", {}, {} });
        for (auto& d : recentDescs)
            listModel.rows.add({ Row::Kind::Plugin, {}, d, {} });
    }

    // All plugins - flat A-Z, or grouped by manufacturer.
    juce::Array<juce::PluginDescription> allFiltered;
    for (auto& d : allTypes)
        if (allowed(d) && matchesFilter(d))
            allFiltered.add(d);

    if (sortByManufacturer)
    {
        std::sort(allFiltered.begin(), allFiltered.end(),
                  [](const juce::PluginDescription& a, const juce::PluginDescription& b)
                  {
                      auto mfrA = a.manufacturerName.isNotEmpty() ? a.manufacturerName : "Unknown";
                      auto mfrB = b.manufacturerName.isNotEmpty() ? b.manufacturerName : "Unknown";
                      int cmp = mfrA.compareIgnoreCase(mfrB);
                      return cmp != 0 ? cmp < 0 : a.name.compareIgnoreCase(b.name) < 0;
                  });

        juce::String lastManufacturer;
        for (auto& d : allFiltered)
        {
            auto mfr = d.manufacturerName.isNotEmpty() ? d.manufacturerName : "Unknown";
            if (mfr != lastManufacturer)
            {
                listModel.rows.add({ Row::Kind::SectionHeader, mfr, {}, {} });
                lastManufacturer = mfr;
            }
            listModel.rows.add({ Row::Kind::Plugin, {}, d, {} });
        }
    }
    else
    {
        std::sort(allFiltered.begin(), allFiltered.end(), byName);
        listModel.rows.add({ Row::Kind::SectionHeader, "All Plugins", {}, {} });
        for (auto& d : allFiltered)
            listModel.rows.add({ Row::Kind::Plugin, {}, d, {} });
    }

    // Failed to load / blacklisted.
    juce::StringArray blacklisted;
    for (auto& path : pluginList.getBlacklistedFiles())
        if (filter.isEmpty() || juce::File(path).getFileName().containsIgnoreCase(filter))
            blacklisted.add(path);

    if (! blacklisted.isEmpty())
    {
        listModel.rows.add({ Row::Kind::SectionHeader, "Failed to Load", {}, {} });
        for (auto& path : blacklisted)
            listModel.rows.add({ Row::Kind::Blacklisted, {}, {}, path });
    }

    listBox.updateContent();
    listBox.repaint();
}

void PluginBrowserComponent::PluginListModel::paintListBoxItem(int row, juce::Graphics& g,
                                                                int w, int h, bool selected)
{
    if (row < 0 || row >= rows.size())
        return;

    auto& r = rows.getReference(row);

    if (r.kind == Row::Kind::SectionHeader)
    {
        g.fillAll(juce::Colour(0xff14141f));
        g.setColour(juce::Colour(0xff8899bb));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(r.headerText, 8, 0, w - 16, h, juce::Justification::centredLeft);
        return;
    }

    if (selected)
        g.fillAll(juce::Colour(0xff3d5a80));
    else
        g.fillAll(row % 2 == 0 ? juce::Colour(0xff1e1e2e) : juce::Colour(0xff252535));

    if (r.kind == Row::Kind::Blacklisted)
    {
        g.setColour(juce::Colours::orange);
        g.setFont(13.0f);
        auto name = juce::File(r.blacklistedPath).getFileName();
        g.drawText(name + "  (failed to load - click to retry)", 8, 0, w - 16, h,
                   juce::Justification::centredLeft);
        return;
    }

    bool isFav = owner.pluginManager.isFavorite(r.plugin.createIdentifierString());
    g.setColour(isFav ? juce::Colours::yellow : juce::Colour(0xff555566));
    g.setFont(15.0f);
    g.drawText(isFav ? juce::String::fromUTF8("\xe2\x98\x85") : juce::String::fromUTF8("\xe2\x98\x86"),
               0, 0, PluginBrowserComponent::starColumnWidth, h, juce::Justification::centred);

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    auto text = r.plugin.name + "  [" + r.plugin.pluginFormatName + "]";
    g.drawText(text, PluginBrowserComponent::starColumnWidth + 4, 0,
               w - PluginBrowserComponent::starColumnWidth - 12, h,
               juce::Justification::centredLeft);
}

void PluginBrowserComponent::PluginListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= rows.size())
        return;

    auto& r = rows.getReference(row);
    if (r.kind == Row::Kind::Plugin && e.x < PluginBrowserComponent::starColumnWidth)
        owner.toggleFavorite(r.plugin.createIdentifierString());
}

void PluginBrowserComponent::PluginListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= rows.size())
        return;

    auto& r = rows.getReference(row);
    if (r.kind == Row::Kind::Plugin)
        owner.selectPlugin(r.plugin);
    else if (r.kind == Row::Kind::Blacklisted)
        owner.retryBlacklistedFile(r.blacklistedPath);
}

void PluginBrowserComponent::PluginListModel::returnKeyPressed(int row)
{
    if (row < 0 || row >= rows.size())
        return;

    auto& r = rows.getReference(row);
    if (r.kind == Row::Kind::Plugin)
        owner.selectPlugin(r.plugin);
    else if (r.kind == Row::Kind::Blacklisted)
        owner.retryBlacklistedFile(r.blacklistedPath);
}

void PluginBrowserComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void PluginBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto topRow = area.removeFromTop(28);
    sortModeButton.setBounds(topRow.removeFromRight(120));
    topRow.removeFromRight(6);
    searchBox.setBounds(topRow);

    area.removeFromTop(4);
    listBox.setBounds(area);
}

void PluginBrowserComponent::showAsCallOut(PluginManager& pluginManager,
                                            PluginSelectedCallback callback,
                                            juce::Component& anchorComponent,
                                            bool allowInstruments)
{
    auto* browser = new PluginBrowserComponent(pluginManager, std::move(callback), allowInstruments);
    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(browser),
        anchorComponent.getScreenBounds(),
        nullptr
    );
}
