#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Slim always-visible bar shown above a hosted plugin's own editor, so a
// slot's active/bypassed state is visible - and toggleable - from inside
// the plugin's own window, not just the channel-strip/master-chain slot
// button. Mirrors the blue/grey convention those buttons already use.
class PluginBypassBar : public juce::Component
{
public:
    explicit PluginBypassBar(std::function<void(bool)> onToggleIn)
        : onToggle(std::move(onToggleIn))
    {
        addAndMakeVisible(button);
        updateButton();

        // The click already reflects the *new* state locally; the owning
        // processor's setBypassed() is the source of truth and will call
        // setBypassed() back on us via setBypassedIndicator() - onToggle
        // must not be re-entered from there (see setBypassed() below).
        button.onClick = [this]
        {
            bypassed = ! bypassed;
            updateButton();
            if (onToggle) onToggle(bypassed);
        };
    }

    // Called from outside (ChannelProcessor/MasterChainProcessor::setBypassed)
    // whenever the state changed via some other route - e.g. the slot's own
    // context menu - so this bar's click handler must not fire onToggle again.
    void setBypassed(bool shouldBeBypassed)
    {
        if (bypassed == shouldBeBypassed) return;
        bypassed = shouldBeBypassed;
        updateButton();
    }

    void resized() override { button.setBounds(getLocalBounds().reduced(4, 2)); }

private:
    void updateButton()
    {
        button.setButtonText(bypassed ? "Bypassed" : "Active");
        button.setColour(juce::TextButton::buttonColourId,
                          bypassed ? juce::Colour(0xff4a4a5c) : juce::Colour(0xff3d5a80));
        button.setColour(juce::TextButton::textColourOffId,
                          bypassed ? juce::Colour(0xffaaaaaa) : juce::Colours::white);
    }

    juce::TextButton button;
    std::function<void(bool)> onToggle;
    bool bypassed = false;
};

// Wraps a hosted plugin's own editor with the bypass bar above it - the one
// component a DocumentWindow's setContentOwned() can hold. Some plugins
// resize their own editor at runtime (zoom controls, collapsible panels);
// previously the editor *was* the DocumentWindow's direct content component,
// so DocumentWindow's own resizeToFitWhenContentChangesSize tracked that
// directly. Now the editor is one step removed (a child of this wrapper), so
// this listens for the editor's own resizes and grows/shrinks to match,
// which DocumentWindow then follows in turn.
class PluginEditorContent : public juce::Component,
                            private juce::ComponentListener
{
public:
    static constexpr int barHeight = 26;

    PluginEditorContent(std::unique_ptr<juce::AudioProcessorEditor> editorIn,
                        bool initiallyBypassed,
                        std::function<void(bool)> onToggleBypass)
        : bar(std::move(onToggleBypass)), editor(std::move(editorIn))
    {
        bar.setBypassed(initiallyBypassed);
        addAndMakeVisible(bar);
        addAndMakeVisible(*editor);
        editor->addComponentListener(this);
        setSize(editor->getWidth(), barHeight + editor->getHeight());
    }

    ~PluginEditorContent() override { editor->removeComponentListener(this); }

    void setBypassed(bool shouldBeBypassed) { bar.setBypassed(shouldBeBypassed); }

    void resized() override
    {
        auto area = getLocalBounds();
        bar.setBounds(area.removeFromTop(barHeight));
        editor->setBounds(area);
    }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            setSize(editor->getWidth(), barHeight + editor->getHeight());
    }

    PluginBypassBar bar;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

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

    // Takes ownership of the plugin's editor, wraps it with the bypass bar,
    // and sizes the window to fit both. onToggleBypass is expected to route
    // straight back into the owning processor's setBypassed(), which is the
    // single source of truth for the slot's bypass state.
    void setPluginEditor(std::unique_ptr<juce::AudioProcessorEditor> editor,
                        bool initiallyBypassed,
                        std::function<void(bool)> onToggleBypass)
    {
        bool resizable = editor->isResizable();

        auto* wrapper = new PluginEditorContent(std::move(editor), initiallyBypassed, std::move(onToggleBypass));
        content = wrapper;

        // true = resizeToFitWhenContentChangesSize: DocumentWindow sizes
        // itself around the wrapper's size *plus* its own title-bar/border
        // insets (getContentComponentBorder()) right here and again on every
        // future wrapper resize (see PluginEditorContent::componentMovedOrResized).
        // Passing false here previously left the window sized to exactly the
        // wrapper's content size with no border allowance, so the bottom of
        // the plugin's own editor - and part of the bypass bar - ended up
        // clipped by the title-bar height.
        setContentOwned(wrapper, true);
        setResizable(resizable, false);

        // getWidth()/getHeight() are already correct (border included) from
        // the setContentOwned() call above - just recentre at that size.
        centreWithSize(getWidth(), getHeight());
    }

    // Called by ChannelProcessor/MasterChainProcessor::setBypassed() so this
    // window's bar stays in sync when the state changes from elsewhere (e.g.
    // the slot's own context menu) - push-based, no polling.
    void setBypassedIndicator(bool bypassed)
    {
        if (content != nullptr) content->setBypassed(bypassed);
    }

private:
    std::function<void()> onClose;
    PluginEditorContent* content = nullptr; // owned by DocumentWindow via setContentOwned
};
