#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// Plain juce::DocumentWindow::closeButtonPressed() is a no-op by default,
// so the titlebar X does nothing unless something overrides it. Shared by
// ChannelProcessor and MasterChainProcessor - both host a plugin editor in
// its own top-level window the same way.
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, std::function<void()> onCloseIn)
        : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          onClose(std::move(onCloseIn))
    {
    }

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};
