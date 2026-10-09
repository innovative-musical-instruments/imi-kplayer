#include "PluginManager.h"
#include <cstdlib>
#include <string>
#include <vector>

PluginManager::PluginManager()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

   #if JUCE_MAC
    formatManager.addFormat(new juce::AudioUnitPluginFormat());
   #endif

    loadFavoritesAndRecent();
}

PluginManager::~PluginManager()
{
    // scanPlugins() has no cancellation checkpoints, so we can't interrupt it early -
    // wait for it to finish rather than destroying formatManager/knownPluginList out from under it.
    if (scanThread != nullptr)
        scanThread->stopThread(-1);
}

juce::File PluginManager::getPluginCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("IMI")
               .getChildFile("KPlayer")
               .getChildFile("plugin_cache.xml");
}

juce::File PluginManager::getFavoritesFile()
{
    return getPluginCacheFile().getSiblingFile("plugin_favorites.txt");
}

juce::File PluginManager::getRecentlyUsedFile()
{
    return getPluginCacheFile().getSiblingFile("plugin_recent.txt");
}

void PluginManager::loadFavoritesAndRecent()
{
    auto favFile = getFavoritesFile();
    if (favFile.existsAsFile())
        favorites.addLines(favFile.loadFileAsString());
    favorites.removeEmptyStrings();

    auto recentFile = getRecentlyUsedFile();
    if (recentFile.existsAsFile())
        recentlyUsed.addLines(recentFile.loadFileAsString());
    recentlyUsed.removeEmptyStrings();
}

void PluginManager::saveFavorites()
{
    getFavoritesFile().getParentDirectory().createDirectory();
    getFavoritesFile().replaceWithText(favorites.joinIntoString("\n"));
}

void PluginManager::saveRecentlyUsed()
{
    getRecentlyUsedFile().getParentDirectory().createDirectory();
    getRecentlyUsedFile().replaceWithText(recentlyUsed.joinIntoString("\n"));
}

bool PluginManager::isFavorite(const juce::String& identifierString) const
{
    return favorites.contains(identifierString);
}

void PluginManager::setFavorite(const juce::String& identifierString, bool shouldBeFavorite)
{
    bool changed;
    if (shouldBeFavorite)
    {
        changed = favorites.addIfNotAlreadyThere(identifierString);
    }
    else
    {
        changed = favorites.contains(identifierString);
        favorites.removeString(identifierString);
    }

    if (changed)
        saveFavorites();
}

void PluginManager::noteRecentlyUsed(const juce::String& identifierString)
{
    recentlyUsed.removeString(identifierString);
    recentlyUsed.insert(0, identifierString);
    while (recentlyUsed.size() > maxRecentlyUsed)
        recentlyUsed.remove(recentlyUsed.size() - 1);
    saveRecentlyUsed();
}

//==============================================================================
// Out-of-process scanning.
//
// Parent and worker talk over the worker's stdout, one line per event, each
// prefixed so any stray text a plugin prints can be ignored:
//     @@KPSCAN@@BEGIN\t<index>            about to load file <index>
//     @@KPSCAN@@DONE\t<index>\t<xml>      finished it (xml = <DESCS> of what it found)
//     @@KPSCAN@@END                       whole list finished
// A BEGIN with no matching DONE when the pipe closes (or the watchdog fires)
// identifies the plugin that crashed or hung.
namespace
{
    const juce::String scanWorkerFlag = "--kplayer-scan-worker";
    const juce::String scanLinePrefix = "@@KPSCAN@@";

    void writeWorkerLine(const juce::String& payload)
    {
        auto line = (scanLinePrefix + payload + "\n").toUTF8();
        fwrite(line.getAddress(), 1, line.length(), stdout);
        fflush(stdout);
    }

   #if JUCE_WINDOWS
    extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);
   #endif

    // Kills the child if it goes quiet for too long (a plugin stuck on a
    // licence dialog, say).
    class ScanWatchdog : public juce::Thread
    {
    public:
        ScanWatchdog(juce::ChildProcess& childIn, std::atomic<juce::int64>& lastActivityIn, juce::int64 timeoutIn)
            : juce::Thread("PluginScanWatchdog"), child(childIn), lastActivity(lastActivityIn), timeoutMs(timeoutIn) {}

        void run() override
        {
            while (! threadShouldExit())
            {
                wait(250);
                if (child.isRunning() && juce::Time::currentTimeMillis() - lastActivity.load() > timeoutMs)
                {
                    timedOut = true;
                    child.kill();
                    return;
                }
            }
        }

        std::atomic<bool> timedOut { false };

    private:
        juce::ChildProcess& child;
        std::atomic<juce::int64>& lastActivity;
        juce::int64 timeoutMs;
    };
}

bool PluginManager::isScanWorkerCommandLine(const juce::String& commandLine)
{
    return commandLine.contains(scanWorkerFlag);
}

void PluginManager::runScanWorker(const juce::String& commandLine)
{
   #if JUCE_WINDOWS
    // No "program has stopped working" box if a plugin takes us down.
    SetErrorMode(0x0001 | 0x0002 | 0x8000);
   #elif JUCE_MAC
    juce::Process::setDockIconVisible(false);
   #endif

    auto tokens = juce::StringArray::fromTokens(commandLine, true);
    auto listFile = juce::File(tokens[tokens.indexOf(scanWorkerFlag) + 1].unquoted());

    juce::AudioPluginFormatManager formats;
    formats.addFormat(new juce::VST3PluginFormat());
   #if JUCE_MAC
    formats.addFormat(new juce::AudioUnitPluginFormat());
   #endif

    juce::StringArray lines;
    lines.addLines(listFile.loadFileAsString());

    juce::KnownPluginList scratchList;

    for (int i = 0; i < lines.size(); ++i)
    {
        auto formatName = lines[i].upToFirstOccurrenceOf("\t", false, false);
        auto path = lines[i].fromFirstOccurrenceOf("\t", false, false);
        if (path.isEmpty())
            continue;

        juce::AudioPluginFormat* format = nullptr;
        for (auto* f : formats.getFormats())
            if (f->getName() == formatName)
                format = f;

        writeWorkerLine("BEGIN\t" + juce::String(i));

        juce::OwnedArray<juce::PluginDescription> found;
        if (format != nullptr)
            scratchList.scanAndAddFile(path, false, found, *format);

        juce::XmlElement root("DESCS");
        for (auto* d : found)
            root.addChildElement(d->createXml().release());

        writeWorkerLine("DONE\t" + juce::String(i) + "\t"
                        + root.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()));
    }

    writeWorkerLine("END");

    // Skip every destructor and DLL detach: the plugins we just loaded were
    // never meant to be torn down in a hurry, and some crash doing it.
    std::_Exit(0);
}

bool PluginManager::scanOutOfProcess(const juce::FileSearchPath& searchPath)
{
    struct Job { juce::String format, file; };
    std::vector<Job> pending;

    for (auto* format : formatManager.getFormats())
        for (auto& file : format->searchPathsForPlugins(searchPath, true, false))
            if (! knownPluginList.getBlacklistedFiles().contains(file)
                && ! knownPluginList.isListingUpToDate(file, *format))
                pending.push_back({ format->getName(), file });

    const int total = (int) pending.size();
    int completed = 0;
    int launchFailures = 0;
    bool workerEverRan = false;

    const auto timeoutMs = (juce::int64) juce::jmax(5000, juce::SystemStats::getEnvironmentVariable(
                                                              "KPLAYER_SCAN_TIMEOUT_MS", "60000").getIntValue());
    auto cacheFile = getPluginCacheFile();
    auto saveCache = [this, &cacheFile]
    {
        cacheFile.getParentDirectory().createDirectory();
        if (auto xml = knownPluginList.createXml())
            xml->writeTo(cacheFile);
    };

    auto announce = [this, total](const juce::String& name, int completedNow)
    {
        juce::WaitableEvent announced;
        juce::MessageManager::callAsync([this, name, completedNow, total, &announced]
        {
            currentlyScanningPluginName = name;
            currentScanProgress = total > 0 ? (float) completedNow / (float) total : 0.0f;
            announced.signal();
        });
        announced.wait();
    };

    while (! pending.empty())
    {
        const auto batch = pending;

        auto listFile = juce::File::createTempFile(".txt");
        juce::String listText;
        for (auto& job : batch)
            listText << job.format << "\t" << job.file << "\n";
        listFile.replaceWithText(listText);

        juce::ChildProcess child;
        const bool started = child.start(juce::StringArray { juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
                                                             scanWorkerFlag, listFile.getFullPathName() },
                                         juce::ChildProcess::wantStdOut);
        if (! started)
        {
            listFile.deleteFile();
            return workerEverRan;
        }

        std::atomic<juce::int64> lastActivity { juce::Time::currentTimeMillis() };
        ScanWatchdog watchdog(child, lastActivity, timeoutMs);
        watchdog.startThread();

        std::string buffer;
        char chunk[4096];
        int inFlight = -1;
        int processedUpTo = -1;
        bool ended = false;

        auto handleLine = [&](juce::String line)
        {
            line = line.trimEnd();
            if (! line.startsWith(scanLinePrefix))
                return;

            auto payload = line.substring(scanLinePrefix.length());
            if (payload.startsWith("BEGIN\t"))
            {
                inFlight = payload.fromFirstOccurrenceOf("\t", false, false).getIntValue();
                if (inFlight >= 0 && inFlight < (int) batch.size())
                {
                    juce::File f(batch[(size_t) inFlight].file);
                    announce(f.exists() ? f.getFileNameWithoutExtension() : f.getFullPathName(), completed);
                }
            }
            else if (payload.startsWith("DONE\t"))
            {
                auto rest = payload.fromFirstOccurrenceOf("\t", false, false);
                auto idx = rest.upToFirstOccurrenceOf("\t", false, false).getIntValue();
                if (auto xml = juce::XmlDocument::parse(rest.fromFirstOccurrenceOf("\t", false, false)))
                    for (auto* element : xml->getChildIterator())
                    {
                        juce::PluginDescription desc;
                        if (desc.loadFromXml(*element))
                            knownPluginList.addType(desc);
                    }

                processedUpTo = juce::jmax(processedUpTo, idx);
                inFlight = -1;
                ++completed;
                if (completed % 50 == 0)
                    saveCache();
            }
            else if (payload.startsWith("END"))
            {
                ended = true;
            }
        };

        for (;;)
        {
            auto n = child.readProcessOutput(chunk, (int) sizeof(chunk));
            if (n <= 0)
                break;

            lastActivity = juce::Time::currentTimeMillis();
            buffer.append(chunk, (size_t) n);

            for (size_t pos; (pos = buffer.find('\n')) != std::string::npos;)
            {
                handleLine(juce::String::fromUTF8(buffer.data(), (int) pos));
                buffer.erase(0, pos + 1);
            }
        }

        if (! child.waitForProcessToFinish(3000))
            child.kill();
        watchdog.stopThread(2000);
        listFile.deleteFile();

        if (ended)
        {
            pending.clear();
            break;
        }

        // The worker died (or was killed) before finishing its list.
        if (inFlight >= 0 && inFlight < (int) batch.size())
        {
            auto culprit = batch[(size_t) inFlight].file;
            knownPluginList.addToBlacklist(culprit);
            lastCrashedPlugins.add(culprit);
            processedUpTo = inFlight;
            ++completed;
            workerEverRan = true;
            launchFailures = 0;
            saveCache();
        }
        else if (processedUpTo >= 0)
        {
            workerEverRan = true;
        }
        else if (++launchFailures >= 2)
        {
            // Never got as far as loading a single plugin, twice running -
            // something other than a plugin is wrong. Let the in-process
            // fallback have a go at whatever is left.
            return workerEverRan;
        }

        if (processedUpTo >= 0)
            pending.erase(pending.begin(), pending.begin() + juce::jmin(processedUpTo + 1, (int) pending.size()));
    }

    saveCache();
    return true;
}

void PluginManager::scanPlugins()
{
    auto cacheFile = getPluginCacheFile();
    if (cacheFile.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse(cacheFile))
            knownPluginList.recreateFromXml(*xml);
    }

    juce::FileSearchPath searchPath;

   #if JUCE_MAC
    searchPath.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins/VST3"));
    searchPath.add(juce::File("/Library/Audio/Plug-Ins/Components"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins/Components"));
   #elif JUCE_WINDOWS
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
   #endif

    auto deadMansPedalFile = getPluginCacheFile().getSiblingFile("plugin_scan_deadmanspedal.txt");

    // Anything left in the pedal file was left behind by an *in-process*
    // scan (an older version, or the fallback loop below) that died
    // mid-scan. Fold it into the blacklist, remember it so the caller can
    // tell the user once, and clear it. The out-of-process scan doesn't use
    // this file - it blacklists a plugin the moment its child process dies.
    // See getPluginsSkippedByLastCrash()'s header comment.
    lastCrashedPlugins.clear();
    if (deadMansPedalFile.existsAsFile())
    {
        lastCrashedPlugins.addLines(deadMansPedalFile.loadFileAsString());
        lastCrashedPlugins.removeEmptyStrings();
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(knownPluginList, deadMansPedalFile);
        deadMansPedalFile.deleteFile();
    }

    const bool scannedOutOfProcess = scanOutOfProcess(searchPath);

    // Fallback only if the worker couldn't be launched at all (e.g. security
    // software blocking the exe from re-launching itself).
    auto inProcessFormats = formatManager.getFormats();
    if (scannedOutOfProcess)
        inProcessFormats = {};

    for (auto* format : inProcessFormats)
    {
        juce::PluginDirectoryScanner scanner(
            knownPluginList,
            *format,
            searchPath,
            true,
            deadMansPedalFile
        );

        // Some plugins (e.g. Kontakt's VST3) do main-thread-only work - such as
        // querying macOS text input sources - as part of their module init, and
        // will hit a dispatch_assert_queue crash if that happens off the message
        // thread. Run each scan step there and block this thread until it's done,
        // so the scan is still orchestrated in the background (window shows
        // immediately, UI stays responsive between plugins) without touching
        // plugin internals from the wrong thread.
        juce::String pluginBeingScanned;
        bool more = true;
        while (more)
        {
            // Announced in its own quick round-trip, separate from the
            // scanNextFile() call below - scanNextFile() only fills in the
            // plugin's name as an early internal step, but doesn't return
            // control to us until the whole (possibly slow) scan of that
            // file is done. Reading the name after the call, as this used
            // to do, meant the overlay showed the *previous* plugin's name
            // for the entire duration of the next one's scan (confirmed via
            // a WaveShell scan showing the prior plugin throughout). This
            // getNextPluginFileThatWillBeScanned()/getProgress() pair
            // reflects the file about to be scanned, and posting it as its
            // own message gives the message loop a chance to actually paint
            // it before the slow work starts.
            {
                juce::WaitableEvent announceDone;
                juce::MessageManager::callAsync([this, &scanner, &announceDone]
                {
                    currentlyScanningPluginName = scanner.getNextPluginFileThatWillBeScanned();
                    currentScanProgress = scanner.getProgress();
                    announceDone.signal();
                });
                announceDone.wait();
            }

            juce::WaitableEvent stepDone;
            juce::MessageManager::callAsync([&scanner, &pluginBeingScanned, &more, &stepDone]
            {
                more = scanner.scanNextFile(true, pluginBeingScanned);
                stepDone.signal();
            });
            stepDone.wait();
        }
    }

    currentlyScanningPluginName.clear();
    currentScanProgress = 0.0f;

    // The loop above only ever adds/updates entries it actually finds on
    // disk - nothing about it notices a plugin that's since been
    // uninstalled or moved, so without this a Rescan would pick up newly
    // installed plugins but leave stale entries for removed ones behind
    // forever. Same pattern as JUCE's own reference implementation,
    // PluginListComponent::removeMissingPlugins().
    for (auto& desc : knownPluginList.getTypes())
        if (! formatManager.doesPluginStillExist(desc))
            knownPluginList.removeType(desc);

    // Reaching this line at all means the scan finished without crashing
    // (a mid-scan crash kills the process before control ever gets back
    // here) - so anything captured into lastCrashedPlugins above has now
    // fully served its purpose: each format's PluginDirectoryScanner
    // constructor already folded those entries into knownPluginList's own
    // blacklist (applyBlacklistingsFromDeadMansPedal, persisted below via
    // the plugin cache XML, independent of this file), and the caller can
    // read lastCrashedPlugins to notify the user once. Nothing else ever
    // removes an already-skipped plugin's entry from this file, so leaving
    // it in place would make that notification fire again on every future
    // scan indefinitely - clearing it here keeps it a true "crashed just
    // now" marker instead.
    if (deadMansPedalFile.existsAsFile())
        deadMansPedalFile.deleteFile();

    cacheFile.getParentDirectory().createDirectory();
    if (auto xml = knownPluginList.createXml())
        xml->writeTo(cacheFile);
}

void PluginManager::scanPluginsAsync(std::function<void()> onComplete)
{
    scanThread = std::make_unique<ScanThread>(*this, std::move(onComplete));
    scanThread->startThread();
}
