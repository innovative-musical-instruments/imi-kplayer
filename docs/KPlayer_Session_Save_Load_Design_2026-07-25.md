# Session Save/Load Design Notes — 2026-07-25

Design discussion captured for later implementation. Nothing here is built yet.
Goal: make KPlayer and Kadabra OS feel like one integrated system, and make
session load/save fast enough for live use.

## A. Kadabra-integrated launch/quit

**Goal:** after installing the full Kadabra stack (Kadabra OS, KSamplers,
KPlugins, KPlayer), launching Kadabra OS should launch KPlayer already in a
playable state, and quitting should never interrupt a live performer with a
save prompt.

**Detection:** Kadabra OS passes a launch flag (e.g. `--kadabra-launch`) when
it starts KPlayer as a child process. Absent that flag, KPlayer behaves
exactly as it does today — no folder-sniffing, no ambiguity. (Considered and
rejected: inferring "Kadabra-equipped" from the mere presence of a Factory
Presets folder — too indirect, doesn't distinguish "installed once" from
"launched by Kadabra OS right now".)

**Two new locations**, both new — no code exists for either yet. Likely
siblings under the existing `~/Library/Application Support/IMI/KPlayer/` root
already used for `recent_sessions.txt` and the plugin scan cache
(`Source/Main.cpp`, `Source/PluginManager.cpp`):
- **Factory Presets** — read-only, vendor-controlled, holds `Start.kplayer`
  and presumably other shipped content. Safe to overwrite on Kadabra
  reinstall/update.
- **Current-state folder** (separate from the above, deliberately) — holds
  `last.kplayer`, owned and silently rewritten by KPlayer. Kept apart from
  Factory Presets so a Kadabra reinstall doesn't wipe the user's last live
  session.

**Launch resolution order** (Kadabra-launched only):
1. `last.kplayer` exists in the current-state folder → load it.
2. Else `Start.kplayer` exists in Factory Presets → load it.
3. Else → today's behavior (empty, ready to build/load).

**Quit behavior** (Kadabra-launched only): silently save to `last.kplayer`
and quit, bypassing `MainWindow::confirmDiscardUnsavedChanges()` in
`Source/Main.cpp` entirely (that's the single choke point every quit path —
Cmd+Q, Dock quit, File > Quit, window close — already funnels through, via
`systemRequestedQuit()`). Standalone (non-Kadabra) launches keep today's
Save/Discard/Cancel prompt untouched — this is an *additional* path, not a
replacement of the existing one.

## B. Fast load/save via diffing — DONE (2026-08-07)

Implemented and reviewed against the actual code before building (see
"Backlog review habit") - the root cause below was re-confirmed unchanged
by everything else built in the intervening two weeks (Panic/Rescan/scan
bulletproofing, the Master-strip/Global-section split). One part of the
original proposal below turned out to need correcting before implementing,
noted inline.

**Confirmed root cause** (not just "plugin loading is inherently slow"):
- `ChannelProcessor::loadPlugin()` has a deliberate `Thread::sleep(1000)`
  (HISE async-streaming settle margin, from earlier crash-fix work) plus a
  50ms drain margin — **~1050ms per plugin, unconditionally**, every load.
  `unloadPlugin()` adds another ~550ms (50ms drain + 500ms HISE
  background-thread wait) if the slot was occupied.
- `SessionIO::applyChannelVar()` (and the master-chain equivalent)
  **unconditionally unloads and reloads every slot on every channel**, with
  zero comparison against what's already loaded — even when the incoming
  session wants the identical plugin in the identical slot.
- `SessionIO::loadSession()` is fully synchronous on the message thread (no
  backgrounding), and unconditionally calls `deviceManager.initialise(...)`
  whenever the session has any device XML, regardless of whether it matches
  the currently-active device state.
- Worst case: a fully populated 24-channel × 6-slot rig could serially block
  the message thread for well over a minute, almost entirely in safety-margin
  sleeps, not actual plugin work.

**Implemented fix — diff before mutating anything**, in `SessionIO.cpp`'s
`applyPluginSlotVar()` (shared by `applyChannelVar()`/`applyMasterChainVar()`,
both of which no longer blanket-unload every slot before applying anything —
each slot decides for itself now):

1. Saved slot is empty → unload whatever's currently there, if anything.
2. Same plugin identity already loaded in this exact slot
   (`PluginDescription::createIdentifierString()`, position-based match —
   same channel index + same slot index) **and** byte-identical state
   (`ChannelProcessor::getPluginState()` vs. the saved blob) → true no-op,
   not even a `setStateInformation()` call.
3. Same plugin identity, **different** state → push the new state into the
   running instance in place via the new
   `ChannelProcessor`/`MasterChainProcessor::updatePluginState()`, skipping
   `createPluginInstance()`, bus renegotiation, and `prepareToPlay()`
   entirely — the actual win, since those (not `setStateInformation()`
   itself) are what the destroy+recreate path pays for.
4. Anything else (different plugin, or empty either side) → today's full
   unload+load path, unchanged, only for that one slot.
5. Same idea one level up: `deviceManager.initialise(...)` is now skipped
   when the new session's device XML string-matches the current live
   state (`SessionIO::loadSession()`) — avoids an audible dropout on every
   load/song-switch, not just time. (Channel-count resize was already
   skipped-if-unchanged before this pass — `MainComponent::setChannelCount()`
   already early-returns on `newCount == oldCount`; the original doc listed
   this as still-needed work, it wasn't.)

**Correction to the original proposal, made before implementing (worth
recording since it changes the actual behavior from what's written above in
the original design)**: step 2 above ("skip *both* safety-margin sleeps
completely... those exist specifically around creating/destroying an
instance, not around updating one that's already alive") was only half
right. `ChannelProcessor::loadPlugin()`'s 1000ms sleep sits *after*
`setStateInformation()`, not just after construction — its real job is
letting HISE's async sample-streaming settle before the audio thread
touches the plugin again, and calling `setStateInformation()` with a
*different* patch on an already-loaded instance could in principle
re-trigger that same async work, not just fresh instantiation.

What resolved it: this app's actual instrument roster (K-Sampler and
friends) loads its sample data once, at instantiation — not in response to
a later `setStateInformation()` call carrying a different patch. So for the
plugins actually in use here, there's no async streaming work happening in
the fast path to wait out, and the settle sleep can be dropped for it
entirely — only the same 50ms drain margin `loadPlugin()`/`unloadPlugin()`
already use is kept (`slot.ready` gates whether the audio thread touches the
plugin at all, so once that's observably false, `setStateInformation()` is
safe with no further wait). **This is a roster-specific assumption, not a
general guarantee** — `updatePluginState()`'s header comment flags it
explicitly; revisit if a plugin that *does* reload sample data per-patch
(some third-party HISE-based instruments do) ever ends up in a slot this
path can reach.

**Matching strategy:** position-based (same channel index + same slot index
+ same plugin identity) — simplest correct approach, covers the real
workflow this was built for (switching between Muses built on the same
rig — same channel count, same plugins in the same slots, different mix/
insert settings; e.g. 5 setlist songs all using the same K-Sampler
instances with different KChannel EQ/comp per song). Matching an instance
across *different* slot positions was considered and dropped — real
bookkeeping complexity (avoiding double-claiming one instance) for a
workflow that doesn't come up here.

**Honest expectation-setting, updated:** for this app's actual plugins, the
fast path is now close to genuinely instant (tens of ms per matching slot,
almost entirely the 50ms drain margin) rather than the original doc's more
conservative "engine overhead only, plugin's own patch-swap work still
costs whatever it costs" framing — because that patch-swap work turned out
not to be async/slow for this roster in the first place.

## Related: MIDI SysEx for OS↔Player communication

Raised in the same conversation, in the context of the channel-16-for-master
MIDI design (see the MIDI command work from this session — CC102/CC104
reserved on channel 16 of the Kadabra port for master record start/stop and
arm/disarm).

**Recommendation:** use SysEx (`0xF0...0xF7`) for structured, occasional,
and/or bidirectional OS↔Player communication — session load/save requests,
status reports, anything needing more than a single 0-127 byte or needing to
carry an identifier/filename — while keeping CC for what it's already doing
well (rapid, continuous, low-latency per-channel control like motion-driven
gain).

Why it directly helps here: SysEx isn't scoped to any of the 16 MIDI
channels, so it sidesteps the channel-16-reservation design entirely for any
future OS-level commands. It also carries arbitrary-length payloads (session
identifiers, exact values, multi-field commands), and is the natural vehicle
if KPlayer ever needs to report status *back* to Kadabra OS (there is
currently no `MidiOutput` code anywhere in the app — that would be new
plumbing, straightforward with JUCE's `MidiOutput`/
`MidiMessage::createSysExMessage`).

Tradeoffs: needs a small defined message schema (command-type + version byte
+ payload) rather than a one-line CC handler — more upfront design, but pays
off once there are more than a couple of structured commands. Needs a
manufacturer-ID byte prefix; since this is a closed Kadabra↔KPlayer protocol
(not general MIDI gear needing MMA registration), a private-use ID is fine.

## Status

B (fast load/save) is done — see its section above. A (launch/quit
integration) and the MIDI SysEx section above remain unimplemented; revisit
A when ready to schedule it, it's what makes the two apps feel like one
system.
