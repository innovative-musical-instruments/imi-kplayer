# Out-of-Process Plugin Scanning — Design Notes — 2026-08-07

Design discussion captured for later implementation. Nothing here is built
yet. Follows live testing of the scan-bulletproofing/live-status work
shipped earlier the same session (see `PluginManager.h`'s
`getCurrentlyScanningPluginName()`/`getScanProgress()`/
`getPluginsSkippedByLastCrash()`), which surfaced a hard architecture
ceiling in the current in-process design.

## Motivation

Scanning today runs entirely in-process, synchronously on the message
thread — required for HISE/Kontakt's main-thread-only init work, see
`PluginManager::scanPlugins()`'s own comment. That has three compounding
costs:

1. **A crashing plugin takes the whole app down.** Recovery only happens on
   the *next* launch, via the dead-man's-pedal auto-blacklist — the current
   session is lost, and the user gets no warning it's about to happen.
2. **A hung plugin (never crashes, never returns) freezes the whole app**
   with no recourse but a force-quit. Same dead-man's-pedal recovery kicks
   in afterward, but only via manual intervention, with zero signal
   beforehand to tell the user whether to wait or force-quit.
3. **The scan overlay can't animate during a single slow plugin's scan.**
   Confirmed live on 2026-08-07: a WaveShell plugin's scan held the message
   thread long enough that the overlay's name label lagged by a full step
   (fixed — see the "announce before scanning" restructure in
   `PluginManager::scanPlugins()`) and, even after that fix plus adding a
   progress-bar shimmer, the shimmer (and in principle the spinner too)
   still can't visibly move *during* one slow plugin's blocking scan — only
   between files. That's a ceiling on the current design, not a bug in the
   shimmer.

Moving actual plugin instantiation into a separate child process removes
all three at once: a crash only kills the child (near-instant same-session
recovery instead of "next launch"), a hang gets a real timeout + kill
instead of relying on the user to notice and force-quit, and the main
process's message thread is never blocked by plugin work, so the overlay
can animate continuously and honestly for the whole scan.

## Prior art already in the JUCE SDK we're building against

`juce_events` ships `juce::ChildProcessCoordinator` / `juce::ChildProcessWorker`
(`modules/juce_events/interprocess/juce_ConnectedChildProcess.h`) for
exactly this coordinator/worker pattern — the same mechanism JUCE's own
AudioPluginHost example app uses for its own out-of-process scanning. No
bespoke IPC layer needed.

- `ChildProcessWorker::initialiseFromCommandLine(commandLine, uniqueID, timeoutMs)`
  — called early in worker-mode startup; returns true if this process
  instance was launched as a worker (matching command-line marker), and
  connects back to the coordinator.
- `ChildProcessCoordinator::launchWorkerProcess(executable, uniqueID, timeoutMs, streamFlags)`
  — spawns `executableToLaunch` (can be our own app binary) with the
  marker, and connects.
- Two-way messaging: `sendMessageToWorker`/`handleMessageFromWorker` and
  `sendMessageToCoordinator`/`handleMessageFromCoordinator`, each taking/
  receiving a raw `MemoryBlock` — we define our own small message schema on
  top (see below).
- **Built-in heartbeat/timeout**: if the worker doesn't ping within
  `timeoutMs`, the coordinator's `handleConnectionLost()` fires
  automatically — a free, JUCE-maintained hang watchdog, not something we'd
  need to build ourselves.
- `ChildProcessCoordinator::killWorkerProcess()` — explicit, immediate
  kill, doesn't block waiting for the child to exit. This is the actual
  mechanism for "force past a hung plugin automatically" that the
  in-process design has no equivalent of at all.

## Proposed shape

### 1. Worker mode entry point

`KPlayerApplication::initialise()` (`Source/Main.cpp`) gets a check right
at the top, before any of today's splash/device-init/window-construction
work:

```cpp
if (workerProcess.initialiseFromCommandLine(getCommandLineParameters(), "KPlayerPluginScan"))
    return; // this process instance IS the worker - it never shows a window
```

A small `ScanWorker : public juce::ChildProcessWorker` owns exactly one
`AudioPluginFormatManager` (same two formats as today: VST3, plus AU on
Mac) and responds to messages from the coordinator.

### 2. Message protocol (small, fixed set)

Coordinator → Worker:
- `ScanOneFile { format name, file-or-identifier string }` — "please scan
  exactly this one plugin and reply"
- `Quit` — clean shutdown between batches if the worker is recycled (see
  §3 below)

Worker → Coordinator:
- `ScanResult { file-or-identifier, success flag, serialized
  PluginDescription(s) found (a bundle can contain several, e.g. a
  multi-plugin VST3), or an error string }`

Serialized as a small `juce::var`/JSON payload inside the `MemoryBlock`,
consistent with how the rest of the app already leans on JUCE's var/JSON
serialization (session files) rather than a hand-rolled binary struct.

### 3. One child process per plugin, or one for the whole batch?

**(a) One child process per file** — maximally isolated (a crash/hang can
only ever cost one `ScanOneFile` round-trip), but pays process-launch
overhead (fork+exec, dylib loading, JUCE static init) per plugin. At this
rig's real library size (~750 plugins, per the `plugin_cache.xml` cleared
during this session's testing), that overhead adds up meaningfully.

**(b) One child process for the whole scan (or per-format batch)**, asked
to scan one file at a time via repeated `ScanOneFile` messages, only
relaunched if it crashes or a per-file timeout fires — amortizes launch
overhead across the whole scan, at the cost of a bit more bookkeeping (has
the file that just failed actually taken the worker down, or was it an
ordinary non-fatal "not a valid plugin" result that the worker survives
fine?), but still gets full crash/hang isolation per file since each
`ScanOneFile` is its own request/response pair with its own timeout.

**Recommendation: (b)** — per-process overhead at ~750 plugins is a real,
avoidable cost, and the existing dead-man's-pedal-driven "resume after a
crash, skip the bad one" pattern already gives us the exact recovery
behavior needed for (b)'s crash path — this design just makes that
recovery near-instant (same scan session) instead of "next launch".

### 4. Per-file timeout → the hang fix

Coordinator posts `ScanOneFile`. If no `ScanResult` arrives before the
per-file deadline, the coordinator calls `killWorkerProcess()`, marks that
one file failed/blacklisted (writing it into the existing
`deadMansPedalFile` — same file `PluginManager` already writes/reads for
the in-process path, so `KnownPluginList`'s blacklist stays the single
source of truth regardless of which scanning path produced it), and
launches a fresh worker to continue with the next file. No force-quit, no
lost session, no ambiguity for the user.

### 5. UI/PluginManager surface stays the same

`getCurrentlyScanningPluginName()`/`getScanProgress()`/
`getPluginsSkippedByLastCrash()` (added 2026-08-07) keep their existing
signatures — the coordinator updates the same members on each
`ScanResult`/dispatch, now genuinely on a message thread that's actually
free to repaint between *every single* `ScanOneFile` round-trip (not just
between per-format batches), which is what finally makes the shimmer/
spinner honestly continuous for the whole scan, slow plugins included.

### 6. Packaging / signing implications (Mac)

The worker is the same app binary re-invoked with a marker argument, not a
separate executable — avoids a second signing/notarization target.
`scripts/build_release.sh`'s existing hardened-runtime + notarization flow
shouldn't need new entitlements just to spawn a child process of itself;
needs verifying specifically against the notarized build once implemented
(hardened runtime's library-validation restrictions already apply to
third-party plugin loading today in the main process — this doesn't
introduce a new constraint, just relocates where that loading happens).

### 7. Windows

No hardened-runtime/notarization equivalent to worry about.
`ChildProcessCoordinator`/`Worker` are cross-platform. Worth confirming AV/
SmartScreen doesn't do anything surprising with an app re-launching itself,
but this is a well-worn pattern (browsers, Electron, etc. all do it), so
low risk.

## Open questions (needs a decision before implementing)

1. Does `ChildProcessCoordinator`'s own built-in timeout (the `timeoutMs`
   passed to `launchWorkerProcess()`, driving automatic
   `handleConnectionLost()`) already cover the per-file hang case
   adequately, or do we need our own explicit per-`ScanOneFile` deadline on
   top of it? The built-in one reads as a *connection* heartbeat in the
   header docs, not necessarily reset per-message — needs checking the
   `.cpp` implementation, not just the header, before relying on it as-is.
2. Recycling the worker across files (option b) needs confirming the
   worker-side code can safely keep going after one file's ordinary
   (non-fatal) failure — e.g. "not a valid plugin" — without extra
   bookkeeping on our part; only an actual crash/hang should trigger a
   relaunch.
3. Where does scanning-worker-mode fit alongside the existing
   `--kadabra-launch` flag design (see
   `KPlayer_Session_Save_Load_Design_2026-07-25.md` section A)? Both are
   "special command-line-triggered alternate startup paths" — want to make
   sure `KPlayerApplication::initialise()`'s command-line handling can't
   confuse the two once both exist.

## Status

Nothing here is implemented — this is a scoping pass following live
testing of the in-process scan overlay (2026-08-07 session), which
surfaced the message-thread-blocking ceiling concretely. Revisit when
ready to schedule the implementation; likely a substantial single
increment given the new worker-mode entry point, IPC protocol, and
packaging verification — not something to fold into a smaller pass.
