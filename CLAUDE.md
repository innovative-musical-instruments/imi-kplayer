# Kadabra K-Player

JUCE 8 standalone VST3/AU plugin-host app ("channel strip" style: per-channel
instrument + 5 inserts, gain/pan, MIDI routing) paired with custom "Kadabra"
performance hardware. Kadabra OS talks to K-Player primarily over MIDI today;
a MIDI SysEx protocol and tighter OS/Player launch integration are planned
(see `docs/KPlayer_Session_Save_Load_Design_2026-07-25.md`).

This project is developed on both a Mac and a Windows machine, syncing via
git. This file is the one piece of context guaranteed to be present on
either machine — prefer updating it over relying on session memory, which is
local to whichever machine wrote it.

## Cross-repo context

Shared strategy, brand, and spec docs live in `imi-common-docs`, a private
sibling repo at `../imi-common-docs` (clone alongside this repo under
`~/projects/`). It is the source of truth — do not duplicate its content
here or let local copies drift.

- `../imi-common-docs/strategy/master-backlog.md` — current priorities,
  active milestone (IMI Free release, target Sept 13 2026), and the
  Epic/Feature breakdown Section 7 tracks it under
- `../imi-common-docs/brand/` — IMI style guide
- `../imi-common-docs/specs/` — cross-product specs
- `../imi-common-docs/decisions/` — dated decision records

If a task needs current strategic context (what's in scope, what's
deferred, why), read the master backlog first rather than asking Vinch to
restate it. Note this repo is private, so — unlike the public K-Sampler
repos — it's fine for this file to reference `imi-common-docs` directly.

## Build

JUCE 8 SDK is required separately (not vendored in this repo) — see
`CMakeLists.txt`'s `if(APPLE)`/`elseif(WIN32)` branches for the expected
location on each platform: `~/SDKs/JUCE` on Mac, `C:/SDKs/JUCE` on Windows.

**Mac:**
```
cmake -B build -G Xcode -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build --config Debug
```
App lands at `build/IMI_KPlayer_artefacts/Debug/Kadabra K-Player.app`.

Release (signed + notarized, separate `build-release/` dir so day-to-day dev
in `build/` is untouched): `scripts/build_release.sh`. Builds a universal
binary (`x86_64;arm64`) by default — pass an arch (e.g. `x86_64` or `arm64`)
as `$1` to override for a single-architecture build, which lands in
`build-release-<arch>/` instead. Requires a Developer ID Application cert
and notarization credentials already stored in Keychain (`xcrun notarytool
store-credentials "kplayer-notary" ...`, a one-time, human-only, interactive
step — never something to script or automate).

**Windows:**
```
cmake -B build -G "Visual Studio 18 2026"
cmake --build build --config Debug
```
App lands at `build/IMI_KPlayer_artefacts/Debug/Kadabra K-Player.exe`.
Confirmed working with Visual Studio Community 2026 (v18) and CMake 4.3.4.
The generator name tracks whatever VS version is installed — check `cmake
--help` for the exact string if this stops matching (e.g. "Visual Studio 17
2022" on a VS2022 machine).

Release: no signed/notarized pipeline exists for Windows (unlike Mac's
`scripts/build_release.sh` above) — "build a release" here just means
`cmake --build build --config Release` in the same `build/` dir, landing at
`build/IMI_KPlayer_artefacts/Release/Kadabra K-Player.exe`. Fine for local
testing; not something to hand out as-is until a real signing setup exists.

## Architecture — where to look

- `Source/Main.cpp` — app entry, `MainWindow`, menu/save/load/quit flow,
  the unsaved-changes prompt (`confirmDiscardUnsavedChanges`).
- `Source/MainComponent.{h,cpp}` — owns the `AudioDeviceManager` audio
  callback (the audio-thread hot path), the channel/master-chain
  processors + UI, MIDI input routing and remote-control command handling,
  session save/load orchestration via `SessionIO`.
- `Source/ChannelProcessor.{h,cpp}` — one channel's plugin chain (slot 0 =
  instrument/effect, slots 1-5 = effect-only inserts), gain/pan, playhead,
  per-channel MIDI CC handling.
- `Source/MasterChainProcessor.{h,cpp}` — master bus insert chain (5
  effect-only slots), same shape as `ChannelProcessor` minus the instrument
  slot.
- `Source/RecordingManager.{h,cpp}` — multitrack WAV recording engine
  (background `ThreadedWriter`, per-channel/master arm state, silence/disk-
  space auto-stop).
- `Source/SessionIO.cpp` / `SessionMigrator.{h,cpp}` / `SessionFormat.{h,cpp}`
  — `.kplayer` session serialization and versioned migration (current
  `formatVersion` 4 — never edit a shipped migration step, only append new
  ones). Plugin slots relink by identity, not by the saved path: a saved
  `PluginDescription`'s `fileOrIdentifier` is an absolute, machine-specific
  install path (baked in from whichever machine saved the file) that's
  never resolvable as-is on the other platform, even for the same plugin
  actually installed there — `resolveLocalDescription()` in `SessionIO.cpp`
  re-resolves it against this machine's own scanned `PluginManager`
  plugin list first, matched by `uniqueId` (falls back to name+manufacturer+
  format). See the "Cross-platform session plugin relink" note below.
- `Source/PluginManager.{h,cpp}` — VST3/AU scanning and caching.
- `docs/*.md` — MVP spec, session format versioning spec, refinement spec,
  first-release backlog, and the save/load + SysEx design notes.

## Conventions worth knowing before touching audio-thread code

- **Cross-thread state**: plain (non-atomic) fields like `gain`/`pan`/
  `bypassed` are a deliberate, accepted tradeoff — written from both the
  message thread (UI) and the audio thread (MIDI CC handling) with no lock,
  on the reasoning that a torn read of a single float/bool is harmless for
  these specific fields. Anything that touches `juce::Component` state or
  does real work (file I/O, plugin creation) must instead go through a
  "set a lightweight atomic flag on the audio thread → a message-thread
  poll consumes it and does the actual work" pattern — see
  `ChannelProcessor::consumeGainChangedByMidi`/`consumeBypassChangedByMidi`/
  `consumeArmChangedByMidi`, and `RecordingManager`'s publish/retire-with-
  drain-margin pattern for its `ThreadedWriter` handoffs.
- **Plugin load/unload has deliberate safety-margin sleeps** (`loadPlugin`
  ~1050ms, `unloadPlugin` ~550ms) to work around HISE/Kontakt async sample-
  streaming races. This is the dominant cost in session load time today —
  see `docs/KPlayer_Session_Save_Load_Design_2026-07-25.md` for the planned
  fix (diff old vs. new session, reuse running instances via
  `setStateInformation()` instead of unconditional unload+reload).
- **Recording files are never reopened for writing** — `RecordingManager`
  always resolves to a filename that doesn't already exist, since JUCE opens
  existing files and appends rather than truncating (a real bug fixed once
  already; don't reintroduce it).
- Session round-trip fields always use `getProperty(key, default)` with a
  safe default, so an older/newer file degrades gracefully rather than
  crashing.

## Cross-platform session plugin relink

Added 2026-08-05, verified both directions (Mac-saved session loaded on
Windows, and vice versa) — Surge XT/K-Sampler/KChannel instances relink and
load correctly on either platform.

**Symptom:** `.kplayer` sessions saved on one machine (with VST3
instruments/inserts in place — Surge XT, HISE-based K-Samplers, KChannel)
lost their plugins when opened on the other platform, while channel
settings (gain/pan/mute/MIDI routing — plain JSON scalars) always
round-tripped fine.

**Root cause, confirmed by diffing two real session files** (one saved on
each platform, same rig): a saved plugin slot's `PluginDescription` embeds
`fileOrIdentifier`, an absolute install path from the saving machine (e.g.
`C:\Program Files\Common Files\VST3\Surge XT.vst3` vs `/Library/Audio/
Plug-Ins/VST3/Surge XT.vst3`). `SessionIO::loadSession()` passed that
description straight to `formatManager.createPluginInstance()` with no
re-resolution — the literal path never exists on the other OS, so the load
silently failed and the slot came back empty, no error surfaced. The
plugin's real identity (`PluginDescription::uniqueId`, a VST3 FUID-derived
value) was confirmed identical across both files for the same plugin, so
the plugins genuinely were compatible — only the path lookup was broken.

**Fix:** `SessionIO.cpp`'s `resolveLocalDescription()` re-resolves a saved
plugin slot against this machine's own scanned `PluginManager::
getPluginList()` (matched by `uniqueId`, falling back to name+manufacturer+
format) before attempting to load, substituting the correct local path.

**Known limitation:** `AudioUnitPluginFormat` is Mac-only (`#if JUCE_MAC`
in `PluginManager.cpp`) — if a Mac session has a plugin that was scanned/
loaded as AU rather than VST3, no relink is possible on Windows by design
(AU doesn't exist there). None of the current HISE-based K-Samplers are
AU-only in practice, so this hasn't bitten yet.

## Testing

User tests manually in the running app. Don't proactively run the JUCE unit
test suite (`IMI_KPlayer_Tests` target, session-format migration tests)
unless asked — it costs tokens for little benefit in this workflow.
