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

## B. Fast load/save via diffing

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

**Proposed fix — diff before mutating anything:**
1. Before touching any live state, compare the new session's plugin layout
   against what's currently loaded, per channel and per slot (same channel
   index + same slot index + same plugin identity, e.g.
   `PluginDescription::createIdentifierString()`).
2. Where they match: skip `unloadPlugin`/`loadPlugin` entirely. Keep the
   running instance and push the new session's saved patch/parameter state
   via `setStateInformation()` (with the same `ready`-flag audio-thread-safety
   gating already used elsewhere for slot mutation). This is the actual win —
   it skips instantiation *and* both safety-margin sleeps completely, since
   those exist specifically around creating/destroying an instance, not
   around updating one that's already alive and settled.
3. Where they don't match (different plugin, or empty either side): fall back
   to today's unload+load path, but only for that one slot.
4. Same idea one level up: skip the channel-count resize if unchanged, and
   skip `deviceManager.initialise(...)` if the new session's device XML
   matches the current live state (also avoids an audible dropout on every
   load, not just time).

**Matching strategy for v1:** position-based (same channel index + same slot
index + same plugin identity) — simplest correct approach, almost certainly
covers the real workflow (switching between Muses built on the same
underlying rig, same plugins in the same slots, different settings).
Matching an instance across *different* slot positions is a plausible future
refinement but adds real bookkeeping complexity (avoiding double-claiming one
instance) — only build it if position-based turns out insufficient in
practice.

**Honest expectation-setting:** this makes the *engine overhead* near-instant
for matching slots. It can't make a genuinely different patch on the same
plugin type load faster if the plugin itself does real work applying it (e.g.
a sample-based instrument swapping resident samples) — that cost lives inside
the plugin, not in KPlayer's loading logic.

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

Nothing in this document is implemented. Revisit when ready to schedule
either piece — B (fast load/save) is probably the higher-leverage one given
Kadabra is used live, but A (launch/quit integration) is what makes the two
apps feel like one system.
