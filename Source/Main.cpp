#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MainComponent.h"
#include "PreferencesComponent.h"
#include "PluginManager.h"
#include "PluginBrowserComponent.h"
#include "SessionIO.h"

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

    enum CommandIDs
    {
        cmdOpenSession = 1,
        cmdSaveSession,
        cmdSaveSessionAs,
        cmdPreferences,
        cmdQuit
    };

    struct MainWindow : public juce::DocumentWindow,
                        public juce::MenuBarModel,
                        public juce::ApplicationCommandTarget
    {
        juce::AudioDeviceManager& deviceManager;
        PluginManager& pluginManager;
        MainComponent* mainComponent = nullptr;
        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::File currentSessionFile;
        juce::ApplicationCommandManager commandManager;

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
            centreWithSize(900, 800);

            commandManager.registerAllCommandsForTarget(this);
            commandManager.setFirstCommandTarget(this);
            addKeyListener(commandManager.getKeyMappings());

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
            {
                menu.addCommandItem(&commandManager, cmdOpenSession);
                menu.addCommandItem(&commandManager, cmdSaveSession);
                menu.addCommandItem(&commandManager, cmdSaveSessionAs);
                menu.addSeparator();
                menu.addCommandItem(&commandManager, cmdQuit);
            }
            else if (index == 1)
            {
                menu.addCommandItem(&commandManager, cmdPreferences);
            }
            return menu;
        }

        void menuItemSelected(int, int) override {}

        // juce::ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

        void getAllCommands(juce::Array<juce::CommandID>& commands) override
        {
            commands.addArray({ cmdOpenSession, cmdSaveSession, cmdSaveSessionAs, cmdPreferences, cmdQuit });
        }

        void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override
        {
            switch (commandID)
            {
                case cmdOpenSession:
                    result.setInfo("Open Session...", "Open a saved session", "File", 0);
                    result.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
                    break;
                case cmdSaveSession:
                    result.setInfo("Save Session", "Save the current session", "File", 0);
                    result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
                    break;
                case cmdSaveSessionAs:
                    result.setInfo("Save Session As...", "Save the current session to a new file", "File", 0);
                    result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
                    break;
                case cmdPreferences:
                    result.setInfo("Audio & MIDI...", "Open audio/MIDI preferences", "Preferences", 0);
                    break;
                case cmdQuit:
                    result.setInfo("Quit", "Quit the application", "File", 0);
                    result.addDefaultKeypress('q', juce::ModifierKeys::commandModifier);
                    break;
                default:
                    break;
            }
        }

        bool perform(const juce::ApplicationCommandTarget::InvocationInfo& info) override
        {
            switch (info.commandID)
            {
                case cmdOpenSession:   openSession();      return true;
                case cmdSaveSession:   saveSession();       return true;
                case cmdSaveSessionAs: saveSessionAs();      return true;
                case cmdPreferences:   showPreferences();    return true;
                case cmdQuit:          juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;
                default: return false;
            }
        }

        void openSession()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Open Session", juce::File(), "*.kplayer");

            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile() &&
                        SessionIO::loadSession(file, *mainComponent, pluginManager, deviceManager))
                    {
                        currentSessionFile = file;
                    }
                });
        }

        // Saves over currentSessionFile if one is already known, otherwise
        // falls back to the same file-picker flow as "Save Session As...".
        void saveSession()
        {
            if (currentSessionFile != juce::File())
                SessionIO::saveSession(currentSessionFile, *mainComponent, deviceManager);
            else
                saveSessionAs();
        }

        void saveSessionAs()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Save Session As", juce::File(), "*.kplayer");

            fileChooser->launchAsync(
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File())
                        return;

                    if (! file.hasFileExtension(".kplayer"))
                        file = file.withFileExtension(".kplayer");

                    if (SessionIO::saveSession(file, *mainComponent, deviceManager))
                        currentSessionFile = file;
                });
        }

        void showPreferences()
        {
            auto* prefs = new PreferencesComponent(deviceManager, mainComponent->getGlobalTempo(),
                                                    [this](double bpm) { mainComponent->setGlobalTempo(bpm); });
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
