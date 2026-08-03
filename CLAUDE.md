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
in `build/` is untouched): `scripts/build_release.sh`. Requires a Developer
ID Application cert and notarization credentials already stored in Keychain
(`xcrun notarytool store-credentials "kplayer-notary" ...`, a one-time,
human-only, interactive step — never something to script or automate).

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
  ones).
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

## Testing

User tests manually in the running app. Don't proactively run the JUCE unit
test suite (`IMI_KPlayer_Tests` target, session-format migration tests)
unless asked — it costs tokens for little benefit in this workflow.
