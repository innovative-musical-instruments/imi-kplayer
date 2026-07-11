#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MainComponent.h"
#include "SettingsComponent.h"
#include "PluginManager.h"
#include "PluginBrowserComponent.h"
#include "SessionIO.h"
#include "AboutScreenComponent.h"

class KPlayerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
        { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override
        { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise(const juce::String&) override
    {
        // Splash covers the blank/delay period before the main window
        // exists (device init + MainWindow construction, both synchronous)
        // - separate from the LoadingOverlayComponent shown inside the
        // already-visible main window during the async plugin scan below.
        // Deferred one message-loop tick via callAsync so the splash
        // actually gets a paint before the blocking work starts.
        splashScreen = std::make_unique<AboutScreenComponent>(true);
        splashScreen->setProgress(0.1f);
        splashScreen->addToDesktop(juce::ComponentPeer::windowHasDropShadow
                                   | juce::ComponentPeer::windowIsTemporary);
        splashScreen->centreWithSize(AboutScreenComponent::fixedWidth, AboutScreenComponent::fixedHeight);
        splashScreen->setVisible(true);
        splashScreen->toFront(false);

        juce::MessageManager::callAsync([this]
        {
            // Requests 2 input channels too (was 0) so per-channel audio
            // input routing (Increment 3 item 8) has something live to
            // select by default; the user can add more via Settings >
            // Audio & MIDI.
            deviceManager.initialiseWithDefaultDevices(2, 2);
            if (splashScreen != nullptr)
                splashScreen->setProgress(0.5f);

            mainWindow.reset(new MainWindow(getApplicationName(),
                                            deviceManager,
                                            pluginManager));

            if (splashScreen != nullptr)
                splashScreen->setProgress(1.0f);
            splashScreen.reset();

            pluginManager.scanPluginsAsync([this]
            {
                if (mainWindow != nullptr)
                    mainWindow->mainComponent->onScanComplete();
            });
        });
    }

    void shutdown() override { mainWindow = nullptr; splashScreen = nullptr; }
    void systemRequestedQuit() override { quit(); }

    enum CommandIDs
    {
        cmdOpenSession = 1,
        cmdSaveSession,
        cmdSaveSessionAs,
        cmdSettings,
        cmdAbout,
        cmdHelp,
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
        bool sessionDirty = false;
        juce::ApplicationCommandManager commandManager;
        juce::Component::SafePointer<SettingsComponent> activeSettings;

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
            mainComponent->onDirty = [this] { markDirty(); };
            setContentOwned(mainComponent, true);
            setResizable(true, true);
            centreWithSize(900, 800);
            updateWindowTitle();

            commandManager.registerAllCommandsForTarget(this);
            commandManager.setFirstCommandTarget(this);
            addKeyListener(commandManager.getKeyMappings());

            setVisible(true);
        }

        ~MainWindow() override { setMenuBar(nullptr); }

        juce::StringArray getMenuBarNames() override
        {
            return { "File", "Settings", "Help" };
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
                menu.addCommandItem(&commandManager, cmdSettings);
            }
            else if (index == 2)
            {
                menu.addCommandItem(&commandManager, cmdAbout);
                menu.addCommandItem(&commandManager, cmdHelp);
            }
            return menu;
        }

        void menuItemSelected(int, int) override {}

        // juce::ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

        void getAllCommands(juce::Array<juce::CommandID>& commands) override
        {
            commands.addArray({ cmdOpenSession, cmdSaveSession, cmdSaveSessionAs, cmdSettings,
                                cmdAbout, cmdHelp, cmdQuit });
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
                case cmdSettings:
                    result.setInfo("Audio & MIDI...", "Open audio/MIDI settings", "Settings", 0);
                    break;
                case cmdAbout:
                    result.setInfo("About", "About KPlayer", "Help", 0);
                    break;
                case cmdHelp:
                    result.setInfo("Help", "Open the KPlayer user guide and video tutorials", "Help", 0);
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
                case cmdSettings:      showSettings();       return true;
                case cmdAbout:         showAboutDialog();    return true;
                case cmdHelp:          openHelpWebsite();    return true;
                case cmdQuit:          juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;
                default: return false;
            }
        }

        void markDirty()
        {
            sessionDirty = true;
            updateWindowTitle();
        }

        void clearDirty()
        {
            sessionDirty = false;
            updateWindowTitle();
        }

        void updateWindowTitle()
        {
            auto name = currentSessionFile != juce::File() ? currentSessionFile.getFileNameWithoutExtension()
                                                             : juce::String("Untitled Session");
            setName(name + (sessionDirty ? " *" : ""));
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
                        clearDirty();
                    }
                });
        }

        // Saves over currentSessionFile if one is already known, otherwise
        // falls back to the same file-picker flow as "Save Session As...".
        void saveSession()
        {
            if (currentSessionFile != juce::File())
            {
                if (SessionIO::saveSession(currentSessionFile, *mainComponent, deviceManager))
                    clearDirty();
            }
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
                    {
                        currentSessionFile = file;
                        clearDirty();
                    }
                });
        }

        void showSettings()
        {
            auto* settings = new SettingsComponent(deviceManager, mainComponent->getGlobalTempo(),
                                                    [this](double bpm) { mainComponent->setGlobalTempo(bpm); markDirty(); },
                                                    mainComponent->getNumChannels(),
                                                    MainComponent::maxChannels,
                                                    [this](int newCount) { requestChannelCountChange(newCount); });
            activeSettings = settings;

            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(settings);
            opts.dialogTitle = "Settings";
            opts.dialogBackgroundColour =
                getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = false;
            opts.launchAsync();
        }

        // The About box draws its own fake title bar (traffic lights /
        // Windows close x) to match the plugin GUIs' own About boxes, so
        // this dialog has no native title bar of its own - the fake close
        // control is wired to actually close it via onCloseRequested.
        void showAboutDialog()
        {
            auto* about = new AboutScreenComponent();
            juce::Component::SafePointer<juce::DialogWindow> dialogWindow;

            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(about);
            opts.dialogTitle = "About KPlayer";
            opts.dialogBackgroundColour = juce::Colour(0xff141a26);
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = false;
            opts.resizable = false;
            dialogWindow = opts.launchAsync();

            about->onCloseRequested = [dialogWindow]
            {
                if (dialogWindow != nullptr)
                    dialogWindow->exitModalState(0);
            };
        }

        void openHelpWebsite()
        {
            juce::URL("https://www.innovativemusicalinstruments.com/Kplayer/help").launchInDefaultBrowser();
        }

        // Grow applies immediately (adding empty channels is never
        // destructive). Shrink first checks whether any channel above the
        // new count has a loaded plugin and, if so, confirms before
        // truncating - same OK/Cancel pattern as ChannelComponent's
        // replace/remove-plugin guards (Increment 1).
        void requestChannelCountChange(int newCount)
        {
            int oldCount = mainComponent->getNumChannels();
            if (newCount == oldCount)
                return;

            if (newCount > oldCount)
            {
                mainComponent->setChannelCount(newCount);
                markDirty();
                return;
            }

            bool anyLoaded = false;
            for (int i = newCount; i < oldCount; ++i)
                if (mainComponent->channelHasLoadedPlugin(i))
                {
                    anyLoaded = true;
                    break;
                }

            if (! anyLoaded)
            {
                mainComponent->setChannelCount(newCount);
                markDirty();
                return;
            }

            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions::makeOptionsOkCancel(
                    juce::MessageBoxIconType::WarningIcon,
                    "Reduce Channel Count",
                    "Channels " + juce::String(newCount + 1) + "-" + juce::String(oldCount)
                        + " have loaded plugins that will be removed. Continue?",
                    "Reduce", "Cancel", this),
                [this, newCount, oldCount](int confirmResult)
                {
                    if (confirmResult == 1)
                    {
                        mainComponent->setChannelCount(newCount);
                        markDirty();
                    }
                    else if (activeSettings != nullptr)
                    {
                        activeSettings->setDisplayedChannelCount(oldCount);
                    }
                });
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
    std::unique_ptr<AboutScreenComponent> splashScreen;
};

START_JUCE_APPLICATION(KPlayerApplication)
