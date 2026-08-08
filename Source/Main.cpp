#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MainComponent.h"
#include "SettingsComponent.h"
#include "PluginManager.h"
#include "PluginBrowserComponent.h"
#include "SessionIO.h"
#include "AboutScreenComponent.h"

// See KPlayerApplication::MainWindow::showSettings() - the Settings dialog
// is deliberately non-modal, so nothing else auto-deletes it once it's
// hidden the way a modal DialogWindow would. This is what actually deletes
// it, the moment it stops being visible by any means (native close button,
// Esc, our own click-to-close toggle, Rescan's close-then-scan).
struct SettingsDialogCloseWatcher : public juce::ComponentListener
{
    std::function<void()> onClosed;

    void componentVisibilityChanged(juce::Component& comp) override
    {
        if (comp.isVisible() || onClosed == nullptr)
            return;

        // Deferred rather than deleting comp synchronously from inside its
        // own visibility-change notification - let that call stack unwind
        // first.
        auto callback = std::move(onClosed);
        onClosed = nullptr;
        juce::MessageManager::callAsync(callback);
    }
};

class KPlayerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
        { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override
        { return JUCE_APPLICATION_VERSION_STRING; }

    // Single-instance enforcement (reported anomaly: a Windows tester had
    // two K-Player processes running simultaneously, fighting over the
    // same MIDI devices). Default JUCEApplication behaviour allows
    // multiple instances; returning false here makes JUCE's own
    // moreThanOneInstanceAllowed()/anotherInstanceStarted() machinery (an
    // OS-level IPC handshake, no extra setup needed - enabled by default
    // for every platform except iOS/Android) detect an already-running
    // instance *before* initialise() ever runs for the new launch attempt
    // - the second process forwards its command line and exits immediately
    // without touching audio devices, scanning plugins, or creating a
    // window at all.
    bool moreThanOneInstanceAllowed() override { return false; }

    // Called on the *existing* instance when a second launch attempt was
    // just turned away above - bring it to the front instead of the user
    // wondering why nothing happened.
    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised(false);
            mainWindow->toFront(true);
        }
        else
        {
            // Arrived before mainWindow exists yet - still inside the brief
            // device-init/plugin-scan startup window. Remember it and honor
            // it as soon as the window actually exists (see initialise()),
            // rather than silently doing nothing.
            focusRequestPendingFromStartup = true;
        }
    }

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

            // Honor a second-launch focus request that arrived while this
            // window didn't exist yet - see anotherInstanceStarted()'s
            // else branch.
            if (focusRequestPendingFromStartup)
            {
                focusRequestPendingFromStartup = false;
                mainWindow->setMinimised(false);
                mainWindow->toFront(true);
            }

            if (splashScreen != nullptr)
                splashScreen->setProgress(1.0f);
            splashScreen.reset();

            pluginManager.scanPluginsAsync([this]
            {
                if (mainWindow != nullptr)
                {
                    // Kadabra-connected-only recovery/starter auto-load
                    // (see MainWindow::tryAutoLoadKadabraSession()) -
                    // deliberately sequenced *after* the scan completes,
                    // not before. It used to run before scanPluginsAsync()
                    // was even called, which meant SessionIO::loadSession()'s
                    // plugin relinking (SessionIO::resolveLocalDescription(),
                    // matched against PluginManager::getPluginList()) always
                    // ran against a still-empty list - PluginManager's
                    // constructor never pre-loads the plugin cache, only
                    // scanPlugins() does. Harmless on the same machine a
                    // session was saved on (the saved absolute path still
                    // resolves directly, no relink needed), but silently
                    // produced an empty rig for a cross-platform session
                    // (e.g. Mac-saved, opened fresh on Windows) - manually
                    // re-opening the same file minutes later, by which time
                    // the scan had finished, always worked, which is what
                    // gave this away. Still covered by MainComponent's
                    // LoadingOverlayComponent throughout - its dismissal via
                    // onScanComplete() below now also covers this. The
                    // overlay's text is swapped to a generic "warming up"
                    // message just before this starts (see
                    // MainComponent::showWarmingUpOverlay()), since
                    // tryAutoLoadKadabraSession() below blocks the message
                    // thread for its own duration (plugin instantiation's
                    // safety-margin sleeps) and would otherwise leave the
                    // overlay frozen mid-scan-status, wrongly implying
                    // scanning is still happening.
                    // showWarmingUpOverlay() forces a real paint of this
                    // message through before returning - see its own
                    // comment for why a plain repaint()/callAsync deferral
                    // isn't actually enough on its own.
                    mainWindow->mainComponent->showWarmingUpOverlay();
                    mainWindow->tryAutoLoadKadabraSession();
                    mainWindow->mainComponent->onScanComplete();
                }
            });
        });
    }

    void shutdown() override { mainWindow = nullptr; splashScreen = nullptr; }
    // Central choke point for every quit path (Cmd+Q, Dock quit, File >
    // Quit's cmdQuit command, and the window's close button all route here)
    // - guarding it once here covers all of them, rather than duplicating
    // the dirty check at each call site.
    void systemRequestedQuit() override
    {
        // onProceed can fire from deep inside the AlertWindow's own
        // callback/modal-dismissal call stack (immediately on Discard, or
        // after saveSession()'s completion callback on Save) - calling
        // quit() synchronously from there hung/crashed the app. Deferring
        // it to a fresh message-loop tick avoids tearing down the app
        // while JUCE is still unwinding that stack.
        if (mainWindow != nullptr)
            mainWindow->silentKadabraQuit([this]
            {
                juce::MessageManager::callAsync([this] { quit(); });
            });
        else
            quit();
    }

    enum CommandIDs
    {
        cmdOpenSession = 1,
        cmdSaveSession,
        cmdSaveSessionAs,
        cmdImportAudioTrack,
        cmdPanic,
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
        // Import Audio to Track (Increment E) - all three must outlive
        // their async operation, same reasoning as fileChooser above.
        std::unique_ptr<juce::FileChooser> importAudioChooser;
        std::unique_ptr<juce::AlertWindow> importTrackPicker;
        std::unique_ptr<juce::FileChooser> importFolderChooser;
        juce::File currentSessionFile;
        bool sessionDirty = false;
        // Starts true so the construction-time resizes above don't count
        // as user-driven; see the end of the constructor and resized().
        bool programmaticResize = true;
        juce::ApplicationCommandManager commandManager;
        // Non-null exactly while the (deliberately non-modal, see
        // showSettings()) Settings dialog is open - lets showSettings()
        // toggle it closed on a second click/menu-select instead of
        // stacking a duplicate. settingsCloseWatcher below is what actually
        // deletes the window and nulls this out once it's hidden, by any
        // means.
        juce::Component::SafePointer<juce::DialogWindow> activeSettingsDialog;
        std::unique_ptr<SettingsDialogCloseWatcher> settingsCloseWatcher;

        // File > Recent - persisted to disk (mirrors PluginManager's
        // ~/Library/IMI/KPlayer/ convention - note juce::File::userApplicationDataDirectory
        // resolves to plain ~/Library on macOS in this JUCE version, not
        // ~/Library/Application Support as the name suggests; verified
        // against juce_Files_mac.mm and the actual files on disk) so it
        // survives across launches, not just within one session.
        static constexpr int recentFilesBaseId = 1000;
        juce::RecentlyOpenedFilesList recentFiles;

        static juce::File getRecentFilesStorageFile()
        {
            return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("IMI").getChildFile("KPlayer").getChildFile("recent_sessions.txt");
        }

        // The common Kadabra space - installer-owned, shared between
        // Starter.kplayer and recover.kplayer below, deliberately separate
        // from the IMI-only root above (recent-files list/plugin cache,
        // KPlayer-internal caching unrelated to Kadabra at all). Explicitly
        // under ~/Library/Application Support on macOS, matching that
        // platform's real convention - juce::File::userApplicationDataDirectory
        // resolves to plain ~/Library on this JUCE version (see
        // getRecentFilesStorageFile()'s comment above for why the IMI root
        // doesn't have "Application Support" either), so it's appended by
        // hand here, Mac-only: Windows has no equivalent subfolder, so
        // %APPDATA% (that same call's own Windows mapping) is used as-is.
        static juce::File getKadabraCommonDirectory()
        {
            auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
           #if JUCE_MAC
            base = base.getChildFile("Application Support");
           #endif
            return base.getChildFile("Kadabra").getChildFile("K-Player");
        }

        // Recovery snapshot - silently overwritten on every quit while a
        // Kadabra MIDI port is connected (see silentKadabraQuit()) and
        // silently loaded back on the next launch under the same condition
        // (see tryAutoLoadKadabraSession()). (Caveat worth keeping in mind:
        // this assumes the Kadabra installer only ever writes/overwrites
        // Starter.kplayer by name in this folder, never wipes-and-recreates
        // it wholesale on update/reinstall - confirm that with Tribal Tools
        // if it's ever in question, since the latter would silently destroy
        // this file too.)
        static juce::File getRecoverSessionFile()
        {
            return getKadabraCommonDirectory().getChildFile("recover.kplayer");
        }

        // Read-only factory starter session, placed here by the Kadabra
        // installer (a separate company/brand from IMI) - KPlayer only ever
        // reads this file, never writes it. Used as the fallback when no
        // recover.kplayer exists yet (first run, or a corrupted/missing
        // recovery file).
        static juce::File getStarterSessionFile()
        {
            return getKadabraCommonDirectory().getChildFile("Starter.kplayer");
        }

        // First MIDI input whose name contains "kadabra" is currently
        // connected - same detection MainComponent already uses for its own
        // auto-assign/tempo-sync behavior (getKadabraDeviceIdentifier()),
        // reused here as the single condition gating both halves of the
        // recovery feature (see silentKadabraQuit() and
        // tryAutoLoadKadabraSession()), so a non-Kadabra session's state can
        // never contaminate what a reconnected Kadabra sees.
        bool isKadabraConnected() const
        {
            return mainComponent->getKadabraDeviceIdentifier().isNotEmpty();
        }

        void saveRecentFilesList()
        {
            auto file = getRecentFilesStorageFile();
            file.getParentDirectory().createDirectory();
            file.replaceWithText(recentFiles.toString());
        }

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
            recentFiles.setMaxNumberOfItems(10);
            recentFiles.restoreFromString(getRecentFilesStorageFile().loadFileAsString());

            setUsingNativeTitleBar(false);
            setMenuBar(this);
            mainComponent = new MainComponent(dm, pm);
            mainComponent->onDirty = [this] { markDirty(); };
            // Forwarded from GlobalSectionComponent's own callbacks -
            // MainComponent doesn't have the context either of these needs
            // (a confirm dialog for a destructive shrink, opening the
            // Settings DialogWindow).
            mainComponent->onChannelCountChangeRequested = [this](int newCount) { requestChannelCountChange(newCount); };
            mainComponent->onSettingsRequested = [this] { showSettings(); };
            mainComponent->onSaveRequested = [this] { saveSession(); };
            mainComponent->onSaveAsRequested = [this] { saveSessionAs(); };
            mainComponent->onOpenSessionRequested = [this] { openSession(); };
            mainComponent->onOpenStarterRequested = [this] { openStarterSession(); };
            setContentOwned(mainComponent, true);
            setResizable(true, true);
            centreWithSize(1152, 800);
            updateWindowTitle();

            commandManager.registerAllCommandsForTarget(this);
            commandManager.setFirstCommandTarget(this);
            addKeyListener(commandManager.getKeyMappings());

            setVisible(true);

            // Everything above (centreWithSize, setContentOwned's initial
            // layout, etc.) fires resized() as a side effect of construction,
            // not a user action - only start treating resizes as dirtying
            // the session once construction is actually done.
            programmaticResize = false;
        }

        ~MainWindow() override { setMenuBar(nullptr); }

        void resized() override
        {
            DocumentWindow::resized();
            if (! programmaticResize)
                markDirty();
        }

        juce::StringArray getMenuBarNames() override
        {
            return { "File", "Help" };
        }

        juce::PopupMenu getMenuForIndex(int index, const juce::String&) override
        {
            juce::PopupMenu menu;
            if (index == 0)
            {
                menu.addCommandItem(&commandManager, cmdOpenSession);
                menu.addCommandItem(&commandManager, cmdSaveSession);
                menu.addCommandItem(&commandManager, cmdSaveSessionAs);

                juce::PopupMenu recentMenu;
                recentFiles.removeNonExistentFiles();
                int numRecent = recentFiles.createPopupMenuItems(recentMenu, recentFilesBaseId, false, true);
                menu.addSubMenu("Recent", recentMenu, numRecent > 0);

                menu.addSeparator();
                menu.addCommandItem(&commandManager, cmdImportAudioTrack);

                menu.addSeparator();
                menu.addCommandItem(&commandManager, cmdPanic);

                menu.addSeparator();
                menu.addCommandItem(&commandManager, cmdSettings);

                menu.addSeparator();
                menu.addCommandItem(&commandManager, cmdQuit);
            }
            else if (index == 1)
            {
                menu.addCommandItem(&commandManager, cmdAbout);
                menu.addCommandItem(&commandManager, cmdHelp);
            }
            return menu;
        }

        void menuItemSelected(int itemID, int) override
        {
            // Recent-file items are plain PopupMenu entries (from
            // createPopupMenuItems), not ApplicationCommand items, so they
            // arrive here rather than through perform().
            if (itemID >= recentFilesBaseId && itemID < recentFilesBaseId + recentFiles.getMaxNumberOfItems())
                openRecentFile(recentFiles.getFile(itemID - recentFilesBaseId));
        }

        // juce::ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

        void getAllCommands(juce::Array<juce::CommandID>& commands) override
        {
            commands.addArray({ cmdOpenSession, cmdSaveSession, cmdSaveSessionAs, cmdImportAudioTrack,
                                cmdPanic, cmdSettings, cmdAbout, cmdHelp, cmdQuit });
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
                case cmdImportAudioTrack:
                    result.setInfo("Import Audio to Track...", "Import an audio file as a recorded Take on a channel", "File", 0);
                    break;
                case cmdPanic:
                    result.setInfo("Panic (All Notes Off)", "Immediately silence every loaded instrument - all notes off / all sound off", "File", 0);
                    result.addDefaultKeypress('.', juce::ModifierKeys::commandModifier);
                    break;
                case cmdSettings:
                    result.setInfo("Settings...", "Open Settings (toggles closed if already open)", "File", 0);
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
                case cmdImportAudioTrack: importAudioToTrack(); return true;
                case cmdPanic:         mainComponent->triggerPanic(); return true;
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

            // Save/load both run synchronously, and can incidentally leave a
            // plugin's parametersDirty flag set as a side effect of querying/
            // restoring its state (some plugins fire their own change
            // notification internally from getStateInformation()/
            // setStateInformation()) - discard it here so the very next
            // 300ms timer poll doesn't immediately re-mark the session dirty
            // for a "change" that was really just this save/load itself.
            mainComponent->discardIncidentalDirtyFlags();
        }

        void updateWindowTitle()
        {
            auto name = currentSessionFile != juce::File() ? currentSessionFile.getFileNameWithoutExtension()
                                                             : juce::String("Untitled Session");
            setName(name + (sessionDirty ? " *" : ""));
        }

        // Called instead of confirmDiscardUnsavedChanges() at quit time
        // (see KPlayerApplication::systemRequestedQuit()). Kadabra connected
        // means this quit may have been initiated by Kadabra OS through
        // automation with nobody watching the screen - so it silently
        // snapshots current state to recover.kplayer (regardless of the
        // dirty flag, and without touching currentSessionFile's own file)
        // and proceeds straight to quitting, never showing the Save/
        // Discard/Cancel dialog. Not Kadabra connected: today's dialog,
        // completely unchanged, and recover.kplayer isn't touched at all -
        // keeps a non-Kadabra session's state from ever contaminating what a
        // later-reconnected Kadabra sees.
        void silentKadabraQuit(std::function<void()> onProceed)
        {
            if (isKadabraConnected())
            {
                mainComponent->setWindowSize(getWidth(), getHeight());
                auto recoverFile = getRecoverSessionFile();
                recoverFile.getParentDirectory().createDirectory();
                SessionIO::saveSession(recoverFile, *mainComponent, deviceManager);
                onProceed();
                return;
            }

            confirmDiscardUnsavedChanges(onProceed);
        }

        // Show Mode: skip the confirm prompt entirely, running proceed()
        // straight away - it's the user's own call to make in Show Mode
        // (see MainComponent::isShowModeEnabled()'s comment for the
        // reasoning). Work Mode (default): gated behind
        // confirmDiscardUnsavedChanges() as usual. Shared by openSession(),
        // openRecentFile(), and openStarterSession() below - all three
        // wanted this exact same two-line branch.
        void proceedOrConfirmDiscard(std::function<void()> proceed)
        {
            if (mainComponent->isShowModeEnabled())
                proceed();
            else
                confirmDiscardUnsavedChanges(proceed);
        }

        // Gates any action that would discard the current session (opening
        // another one, quitting) behind a Save/Discard/Cancel prompt when
        // there are unsaved changes. Runs onProceed immediately if the
        // session isn't dirty, after a successful save if the user chose
        // Save, or not at all if they cancelled.
        void confirmDiscardUnsavedChanges(std::function<void()> onProceed)
        {
            if (! sessionDirty)
            {
                onProceed();
                return;
            }

            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions::makeOptionsYesNoCancel(
                    juce::MessageBoxIconType::WarningIcon,
                    "Unsaved Changes",
                    "You have unsaved changes in your current session.",
                    "Save", "Discard and Continue", "Cancel", this),
                [this, onProceed](int result)
                {
                    // onProceed can fire from deep inside the AlertWindow's
                    // own callback/modal-dismissal call stack (immediately
                    // on Discard, or after saveSession()'s completion
                    // callback on Save) - same hazard systemRequestedQuit()
                    // already works around for quit(). Here onProceed often
                    // launches a second modal of its own (openSession()'s
                    // FileChooser, saveSessionAs()'s FileChooser) - starting
                    // that from within the first modal's own teardown
                    // silently failed to show it on Windows rather than
                    // hanging/crashing, so defer to a fresh message-loop
                    // tick the same way.
                    if (result == 1)
                        saveSession([onProceed](bool saved) { if (saved) juce::MessageManager::callAsync(onProceed); });
                    else if (result == 2)
                        juce::MessageManager::callAsync(onProceed);
                    // result == 0 (Cancel): do nothing.
                });
        }

        void openSession()
        {
            auto proceed = [this]
            {
                // Guard against a second open/save-as trigger landing while
                // one's already in flight (more reachable now via MIDI CC3/
                // CC9 than it was mouse-only) - reassigning fileChooser here
                // would destroy the first dialog out from under the user
                // with no completion callback for their original action.
                if (fileChooser != nullptr)
                    return;

                fileChooser = std::make_unique<juce::FileChooser>(
                    "Open Session", juce::File(), "*.kplayer");

                fileChooser->launchAsync(
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this](const juce::FileChooser& fc)
                    {
                        auto file = fc.getResult();
                        fileChooser.reset();
                        if (file.existsAsFile())
                            loadSessionFile(file);
                    });
            };

            proceedOrConfirmDiscard(proceed);
        }

        void openRecentFile(const juce::File& file)
        {
            auto proceed = [this, file]
            {
                if (file.existsAsFile())
                    loadSessionFile(file);
            };

            proceedOrConfirmDiscard(proceed);
        }

        // Import Audio to Track (Increment E, see
        // docs/kplayer-take-recording-playback-spec.md section 9): pick a
        // source file, then which channel to assign it to (or a new one),
        // then hand off to MainComponent::importAudioToChannel() for the
        // actual read/resample/write work.
        void importAudioToTrack()
        {
            importAudioChooser = std::make_unique<juce::FileChooser>(
                "Import Audio to Track", juce::File(),
                "*.wav;*.wave;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

            importAudioChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                        showImportTrackPicker(file);
                });
        }

        void showImportTrackPicker(const juce::File& sourceFile)
        {
            importTrackPicker = std::make_unique<juce::AlertWindow>(
                "Import Audio to Track",
                "Choose a track for \"" + sourceFile.getFileName() + "\":",
                juce::MessageBoxIconType::NoIcon);

            int numChannels = mainComponent->getNumChannels();
            juce::StringArray items;
            // Omitted once at the channel-count ceiling - no room to grow.
            if (numChannels < MainComponent::maxChannels)
                items.add("New Track");
            for (int i = 0; i < numChannels; ++i)
                items.add("Channel " + juce::String(i + 1));

            importTrackPicker->addComboBox("track", items, "Track");
            importTrackPicker->getComboBoxComponent("track")->setSelectedItemIndex(0, juce::dontSendNotification);
            importTrackPicker->addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));
            importTrackPicker->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            bool hasNewTrackOption = numChannels < MainComponent::maxChannels;

            // deleteWhenDismissed=false deliberately - the callback below
            // still needs to read the combo box's selection, which would
            // already be destroyed by the time it runs if this were true
            // (per Component::enterModalState's own documented behaviour).
            // importTrackPicker.reset() below does the cleanup manually,
            // after reading what's needed.
            importTrackPicker->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, sourceFile, hasNewTrackOption](int result)
                {
                    int selectedIndex = -1;
                    if (auto* box = importTrackPicker != nullptr ? importTrackPicker->getComboBoxComponent("track") : nullptr)
                        selectedIndex = box->getSelectedItemIndex();
                    importTrackPicker.reset();

                    if (result != 1 || selectedIndex < 0)
                        return; // cancelled

                    int channelIndex;
                    if (hasNewTrackOption && selectedIndex == 0)
                    {
                        mainComponent->setChannelCount(mainComponent->getNumChannels() + 1);
                        channelIndex = mainComponent->getNumChannels() - 1;
                    }
                    else
                    {
                        channelIndex = hasNewTrackOption ? selectedIndex - 1 : selectedIndex;
                    }

                    performAudioImport(channelIndex, sourceFile);
                }), false);
        }

        // Lazy prompt: ask for a recordings folder right here the first
        // time it's needed, same pattern as the Record button's own prompt
        // (see onRecordButtonClicked in MainComponent.cpp) - Audio Takes
        // (imported or recorded) both live under that folder.
        void performAudioImport(int channelIndex, const juce::File& sourceFile)
        {
            if (mainComponent->getRecordingsFolder() == juce::File())
            {
                importFolderChooser = std::make_unique<juce::FileChooser>(
                    "Choose a folder for recordings",
                    juce::File::getSpecialLocation(juce::File::userMusicDirectory));

                auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
                importFolderChooser->launchAsync(flags, [this, channelIndex, sourceFile](const juce::FileChooser& chooser)
                {
                    auto result = chooser.getResult();
                    if (result == juce::File())
                        return;

                    mainComponent->setRecordingsFolder(result);
                    markDirty();
                    performAudioImport(channelIndex, sourceFile);
                });
                return;
            }

            auto error = mainComponent->importAudioToChannel(channelIndex, sourceFile);
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Import Audio to Track", error);
            else
                markDirty();
        }

        // trackAsCurrentFile=false loads the file's content without binding
        // it as "the current file" - currentSessionFile stays untouched and
        // it never enters the recent-files list, so the window title falls
        // back to "Untitled Session" and the first manual Save behaves like
        // Save As. Used for the recover/starter auto-load (see
        // tryAutoLoadKadabraSession()) so neither internal file can ever be
        // silently overwritten by an ordinary Save. Returns whether the load
        // actually succeeded, so callers can fall back to another file.
        bool loadSessionFile(const juce::File& file, bool trackAsCurrentFile = true)
        {
            if (! SessionIO::loadSession(file, *mainComponent, pluginManager, deviceManager))
                return false;

            if (trackAsCurrentFile)
            {
                currentSessionFile = file;
                recentFiles.addFile(file);
                saveRecentFilesList();
            }
            clearDirty();
            applyLoadedWindowSize();
            return true;
        }

        // Kadabra-connected-only: recover.kplayer if it exists and loads
        // successfully, else Starter.kplayer if present, else today's
        // default empty rig - unchanged for anyone without Kadabra
        // connected. Called once at launch, right after this MainWindow (and
        // its MainComponent) exists - see KPlayerApplication::initialise().
        void tryAutoLoadKadabraSession()
        {
            if (! isKadabraConnected())
                return;

            auto recoverFile = getRecoverSessionFile();
            if (recoverFile.existsAsFile() && loadSessionFile(recoverFile, false))
                return;

            auto starterFile = getStarterSessionFile();
            if (starterFile.existsAsFile())
                loadSessionFile(starterFile, false);
        }

        // MIDI-triggered (CC99 value 0, see MainComponent::
        // onOpenStarterRequested) - explicit "reload the factory starter"
        // request, as opposed to tryAutoLoadKadabraSession()'s launch-time
        // fallback above. Same loadSessionFile(..., false) call (doesn't
        // track Starter.kplayer as the current file, so a later Save
        // doesn't silently overwrite the read-only factory copy), and the
        // same Show/Work Mode discard-confirmation openSession()/
        // openRecentFile() already use.
        void openStarterSession()
        {
            auto starterFile = getStarterSessionFile();
            auto proceed = [this, starterFile]
            {
                if (starterFile.existsAsFile())
                    loadSessionFile(starterFile, false);
            };

            proceedOrConfirmDiscard(proceed);
        }

        // A saved window size is 0x0 for a fresh session or a file saved
        // before this feature existed - leave the current window size
        // alone in that case rather than snapping it to 0x0.
        void applyLoadedWindowSize()
        {
            int w = mainComponent->getSavedWindowWidth();
            int h = mainComponent->getSavedWindowHeight();
            if (w > 0 && h > 0)
            {
                programmaticResize = true;
                setSize(w, h);
                programmaticResize = false;
            }
        }

        // Saves over currentSessionFile if one is already known, otherwise
        // falls back to the same file-picker flow as "Save Session As...".
        // onComplete, if given, is called with whether the save actually
        // happened (false if it fell through to a cancelled Save As).
        void saveSession(std::function<void(bool)> onComplete = nullptr)
        {
            mainComponent->setWindowSize(getWidth(), getHeight());

            if (currentSessionFile != juce::File())
            {
                bool saved = SessionIO::saveSession(currentSessionFile, *mainComponent, deviceManager);
                if (saved)
                {
                    clearDirty();
                    recentFiles.addFile(currentSessionFile);
                    saveRecentFilesList();
                }
                if (onComplete)
                    onComplete(saved);
            }
            else
                saveSessionAs(onComplete);
        }

        void saveSessionAs(std::function<void(bool)> onComplete = nullptr)
        {
            // Same guard as openSession() - both share the fileChooser
            // member, and a concurrent trigger reassigning it mid-flight
            // would yank whichever dialog was already open.
            if (fileChooser != nullptr)
                return;

            mainComponent->setWindowSize(getWidth(), getHeight());

            fileChooser = std::make_unique<juce::FileChooser>(
                "Save Session As", juce::File(), "*.kplayer");

            fileChooser->launchAsync(
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this, onComplete](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    fileChooser.reset();
                    if (file == juce::File())
                    {
                        if (onComplete)
                            onComplete(false);
                        return;
                    }

                    if (! file.hasFileExtension(".kplayer"))
                        file = file.withFileExtension(".kplayer");

                    bool saved = SessionIO::saveSession(file, *mainComponent, deviceManager);
                    if (saved)
                    {
                        currentSessionFile = file;
                        clearDirty();
                        recentFiles.addFile(file);
                        saveRecentFilesList();
                    }
                    if (onComplete)
                        onComplete(saved);
                });
        }

        // Toggle: clicking Settings (menu item or the Global-section button)
        // while the dialog is already open closes it, same as the native
        // close button - rather than doing nothing or stacking a second one.
        //
        // Deliberately non-modal (LaunchOptions::create(), not
        // launchAsync()) - a fully modal dialog blocks click delivery to
        // the rest of the app *including* the Global-section Settings
        // button itself, which is exactly what this toggle needs to keep
        // working. Settings doesn't gate anything that needs exclusive
        // access (channel count moved out to the Global section already;
        // device selection/recordings folder/Rescan are all fine to leave
        // interactive alongside the main window).
        void showSettings()
        {
            if (activeSettingsDialog != nullptr)
            {
                activeSettingsDialog->setVisible(false);
                return;
            }

            auto* settings = new SettingsComponent(deviceManager,
                                                    mainComponent->getRecordingsFolder(),
                                                    mainComponent->getRecordingSilenceTimeoutSeconds(),
                                                    [this](juce::File folder) { mainComponent->setRecordingsFolder(folder); markDirty(); },
                                                    [this](double seconds) { mainComponent->setRecordingSilenceTimeoutSeconds(seconds); markDirty(); });

            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(settings);
            opts.dialogTitle = "Settings";
            opts.dialogBackgroundColour =
                getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            // Resizable, not just fixed-size - the audio/MIDI selector inside
            // (see SettingsComponent's audioSettingsViewport) already scrolls
            // to reach every control regardless of window size, but letting
            // the user grow the window too means they don't have to scroll
            // at all on setups with a lot of enumerated MIDI ports.
            opts.resizable = true;

            // create() just builds the window without entering modal state -
            // we own showing it and cleaning it up (below), since JUCE's
            // own auto-delete-on-close only applies to modal components.
            activeSettingsDialog = opts.create();
            activeSettingsDialog->setResizeLimits(420, 400, 900, 1200);
            activeSettingsDialog->setVisible(true);
            activeSettingsDialog->toFront(true);

            // The native close button's default handler just does
            // setVisible(false) for a non-modal DialogWindow (no
            // ModalComponentManager watching it to trigger deletion) - this
            // listener is what actually deletes the window once it's
            // hidden, by any means (native close, Esc, our own toggle
            // above, or Rescan's close-then-scan below), so there's one
            // single cleanup path regardless of which one fired.
            settingsCloseWatcher = std::make_unique<SettingsDialogCloseWatcher>();
            settingsCloseWatcher->onClosed = [this]
            {
                if (activeSettingsDialog != nullptr)
                {
                    delete activeSettingsDialog.getComponent();
                    activeSettingsDialog = nullptr;
                }
            };
            activeSettingsDialog->addComponentListener(settingsCloseWatcher.get());

            // Skips the confirm dialog by design (user request) - closes
            // Settings and reuses the exact startup-scan flow (same
            // LoadingOverlayComponent, same scanPluginsAsync call) so newly
            // installed plugins are picked up the same way a relaunch would
            // find them.
            settings->onRescanRequested = [this]
            {
                if (activeSettingsDialog != nullptr)
                    activeSettingsDialog->setVisible(false);
                mainComponent->rescanPlugins();
            };
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
                [this, newCount](int confirmResult)
                {
                    // Cancel needs no revert here (unlike the old Settings
                    // slider, which visually snapped to the new value the
                    // instant it was dragged/clicked) - GlobalSectionComponent's
                    // +/- boxes never move their own displayed count on
                    // click, only in response to MainComponent::setChannelCount()
                    // actually succeeding, so an unconfirmed shrink just
                    // never touches the display at all.
                    if (confirmResult == 1)
                    {
                        mainComponent->setChannelCount(newCount);
                        markDirty();
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

    // Set when anotherInstanceStarted() arrives before mainWindow exists
    // yet (still inside the brief device-init/plugin-scan startup window) -
    // consumed as soon as the window is actually constructed, see
    // initialise() and anotherInstanceStarted() above.
    bool focusRequestPendingFromStartup = false;
};

START_JUCE_APPLICATION(KPlayerApplication)
