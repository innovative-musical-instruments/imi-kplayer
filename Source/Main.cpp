#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MainComponent.h"
#include "PreferencesComponent.h"
#include "PluginManager.h"
#include "PluginBrowserComponent.h"

class KPlayerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
        { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override
        { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise(const juce::String&) override
    {
        deviceManager.initialiseWithDefaultDevices(0, 2);
        mainWindow.reset(new MainWindow(getApplicationName(),
                                        deviceManager,
                                        pluginManager));

        pluginManager.scanPluginsAsync([this]
        {
            if (mainWindow != nullptr)
                mainWindow->mainComponent->onScanComplete();
        });
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

    struct MainWindow : public juce::DocumentWindow,
                        public juce::MenuBarModel
    {
        juce::AudioDeviceManager& deviceManager;
        PluginManager& pluginManager;
        MainComponent* mainComponent = nullptr;

        MainWindow(juce::String name,
                   juce::AudioDeviceManager& dm,
                   PluginManager& pm)
            : DocumentWindow(name,
                juce::Desktop::getInstance().getDefaultLookAndFeel()
                    .findColour(juce::ResizableWindow::backgroundColourId),
                DocumentWindow::allButtons),
              deviceManager(dm),
              pluginManager(pm)
        {
            setUsingNativeTitleBar(false);
            setMenuBar(this);
            mainComponent = new MainComponent(dm, pm);
            setContentOwned(mainComponent, true);
            setResizable(true, true);
            centreWithSize(900, 600);
            setVisible(true);
        }

        ~MainWindow() override { setMenuBar(nullptr); }

        juce::StringArray getMenuBarNames() override
        {
            return { "File", "Preferences" };
        }

        juce::PopupMenu getMenuForIndex(int index, const juce::String&) override
        {
            juce::PopupMenu menu;
            if (index == 0)
                menu.addItem(1, "Quit");
            else if (index == 1)
                menu.addItem(100, "Audio & MIDI...");
            return menu;
        }

        void menuItemSelected(int itemID, int) override
        {
            if (itemID == 1)
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            if (itemID == 100)
                showPreferences();
        }

        void showPreferences()
        {
            auto* prefs = new PreferencesComponent(deviceManager, mainComponent->getChannelProcessor());
            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(prefs);
            opts.dialogTitle = "Preferences";
            opts.dialogBackgroundColour =
                getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = false;
            opts.launchAsync();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    juce::AudioDeviceManager deviceManager;
    PluginManager pluginManager;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(KPlayerApplication)
