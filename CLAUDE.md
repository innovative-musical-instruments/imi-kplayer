# Kadabra K-Player

JUCE 9 standalone VST3/AU plugin-host app ("channel strip" style: per-channel
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

**When you add a new entry to `RELEASE_NOTES.md` (i.e. a version ships),
say so explicitly at the end of your response** — e.g. "v0.9.x shipped,
worth syncing status to imi-common-docs/strategy/master-backlog.md
Section 7." That's the prompt for Vinch to actually do the sync, instead
of it drifting for days.

## Build

JUCE 9 SDK is required separately (not vendored in this repo) — see
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

The script notarizes and staples **both** the app and the finished DMG.
The DMG half was added 2026-09-05: it used to only sign the disk image, so
`spctl` reported it "rejected, source=Unnotarized Developer ID" even though
the app inside was properly stapled — a download would hit an "unidentified
developer" warning on the image itself, and fail outright offline. The
0.9.7 and 0.9.8 disk images still have that gap if they're ever handed out
again.

Finished builds are copied by hand to `/Volumes/Vinch2T/IMI/IMI
DEV/Releases/KPlayer/` (the app bundle and the DMG), alongside the other
IMI products' release folders. Use `ditto` rather than `cp -R` for the
`.app` so the signature and extended attributes survive the copy, and
re-verify `codesign`/`stapler`/`spctl` on the *copies*, not just the
originals.

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

Release: **`scripts/build_release_windows.ps1`**. It forces a fresh
configure (`cmake --fresh`) before building Release in the same `build/`
dir (no separate release build dir on Windows, unlike Mac's
`scripts/build_release.sh` above), landing at
`build/IMI_KPlayer_artefacts/Release/Kadabra K-Player.exe`.

The fresh configure matters: JUCE bakes FILEVERSION/PRODUCTVERSION into a
resource generated **at configure time** from `project(... VERSION ...)`,
and a plain `cmake --build --config Release --target IMI_KPlayer` skips
ZERO_CHECK under the VS generator — so after a version bump it can link a
stale FILEVERSION while everything else is current (this shipped a
0.9.7-stamped "0.9.8" installer binary once, 2026-08-28). A WIN32
post-build guard (`cmake/AssertExeVersion.cmake`) now hard-fails any
`project(VERSION)` vs. linked-`.exe` mismatch on every Windows build, so a
bare `cmake --build` can no longer produce a mislabelled binary silently —
it just errors and tells you to reconfigure.

Signing (Azure Artifact Signing, IMI Ltd Organization cert — see
`imi-common-docs/decisions/2026-08-17-windows-code-signing-path.md`) is
now wired up and proven against a real build (2026-08-21):
`../imi-windows-installer/scripts/sign-windows-binary.ps1 -Path <exe>`.
Requires `az login` as `contact@innovativemusicalinstruments.com` (or
another identity with the Certificate Profile Signer role) already
cached — the script itself handles downloading the Artifact Signing
client and running/verifying `signtool`. No installer/notarization
equivalent exists yet (that's `imi-windows-installer`'s job, not yet
built) — today this just produces a signed standalone `.exe`, zipped as
`Release/Kadabra K-Player v<version> (Win64).zip` for ad-hoc
distribution, matching the KSamplers' `<Product> v<version> (Win64).zip`
convention in `common-docs/reference/ksamplers-releases.md`.

### JUCE SDK version — 9.0.1 as of 2026-08-20

Migrated from 8.0.14 after tracing a real crash to it: changing the sample
rate in Settings while input/output are two different physical devices
(forcing JUCE's `AudioIODeviceCombiner` path) could fire a CoreAudio HAL
property-change notification (`kAudioObjectPropertyOwnedObjects`/
`kAudioDevicePropertyDeviceHasChanged`) on the async
`HALC_ShellObject_Listener Queue` *after* the combiner had already been torn
down by the same reopen - `CoreAudioInternal::deviceRequestedRestart()` in
8.0.14's `juce_CoreAudio_mac.cpp` calls `owner.restart()` unconditionally,
with no liveness check (unlike its sibling `deviceDetailsChanged()`, which
*is* guarded by `callbacksAllowed`) - so the callback dereferenced freed
memory and vtable-jumped to garbage (`EXC_BAD_ACCESS`/`SIGBUS`, `PC = 0x1`).
Confirmed via a real crash report (v0.9.6, immediately reproducible) and by
reading the 8.0.14 source directly.

JUCE 9.0.0 shipped a ground-up rewrite of the macOS CoreAudio backend that
defers this kind of notification through `triggerAsyncUpdate()` onto the
message thread and gates it on an `isOpen()` check before touching
anything - architecturally the fix this bug needed. Verified locally: same
repro immediately crashes on the old 8.0.14-built release, doesn't
reproduce at all on a 9.0.1 rebuild.

The old 8.0.14 SDK is kept at `~/SDKs/JUCE-8.0.14` on this Mac for
rollback, not deleted. **The Windows machine's `C:/SDKs/JUCE` still needs
the same swap by hand** - `CMakeLists.txt`'s path itself didn't change (it
points at the same `~/SDKs/JUCE` / `C:/SDKs/JUCE` either way), only what
lives there, so there's nothing to `git pull` for this - it's a per-machine
SDK swap, easy to forget since nothing in the diff will mention it.

## Windows: catching up to v0.9.9 (written from the Mac, 2026-09-05)

macOS shipped v0.9.9 (signed, notarized, distributed). Windows has not
been built for it, so the two platforms are no longer on the same commit.
What that machine needs, in order:

1. **`git pull`** — 11 commits: the Range, the metronome click, live
   tempo, the 1200 BPM ceiling, the editable playhead readout, release
   notes and docs.
2. **Check the remote URL.** The GitHub repo is `imi-kplayer`; an older
   clone may still point at `IMI-KPlayer`, which GitHub redirects but
   which keeps resurfacing the wrong name — `git remote set-url origin
   https://github.com/innovative-musical-instruments/imi-kplayer.git`.
3. **If the checkout folder is still named `IMI_KPlayer`, rename it to
   `imi-kplayer` and delete `build/`.** CMake hard-refuses a cache created
   under a different source path ("The current CMakeCache.txt directory
   ... is different than the directory ... where CMakeCache.txt was
   created"). This bit twice on the Mac today, for both `build/` and
   `build-release/`. Deleting the build dir is the whole fix.
4. **The JUCE SDK swap may still be outstanding** — see the JUCE 9.0.1
   section above. `C:/SDKs/JUCE` needs to be 9.0.1, and nothing in a
   `git pull` will tell you if it isn't, since the path never changed.
5. **Build Release via `scripts/build_release_windows.ps1`** (it forces
   the fresh configure the version-resource guard needs — the guard will
   hard-fail a stale 0.9.8-stamped binary), sign with
   `../imi-windows-installer/scripts/sign-windows-binary.ps1`, and zip as
   `Release/Kadabra K-Player v0.9.9 (Win64).zip`.
6. **Then update `imi-common-docs/strategy/master-backlog.md` Section 7**,
   which currently records "Windows has not been built for 0.9.9 yet".

Worth actually looking at on Windows, since they were written on the Mac
with that platform's font-coverage gap in mind: the Range capture buttons
draw their up-arrow as a vector (a `"CAPTURE"` sentinel in
`TransportButtonLookAndFeel`) and the RANGE caption is plain text with
vector arrows either side — both deliberately avoid Unicode glyphs. Also
check the window at its new 1204px minimum width.

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
  `formatVersion` 5 — never edit a shipped migration step, only append new
  ones). Plugin slots relink by identity, not by the saved path: a saved
  `PluginDescription`'s `fileOrIdentifier` is an absolute, machine-specific
  install path (baked in from whichever machine saved the file) that's
  never resolvable as-is on the other platform, even for the same plugin
  actually installed there — `resolveLocalDescription()` in `SessionIO.cpp`
  re-resolves it against this machine's own scanned `PluginManager`
  plugin list first, matched by `uniqueId` (falls back to name+manufacturer+
  format). See the "Cross-platform session plugin relink" note below.
- `Source/SessionTransport.h` — the shared playhead every Take player
  renders against, and the **Range**: the [start, end) span the transport
  plays, a LOOP flag deciding whether reaching the end wraps or stops, and
  a suspend flag recording sets (a take records linearly straight past the
  range end). With LOOP off the audio thread parks on the end, pauses
  itself and raises `stoppedAtRangeEnd`, which MainComponent's timer
  consumes to resync the Play button. `MainComponent::updateTransportRange()`
  keeps it spanning the longest selected Take.
- `Source/MidiTakePlayer.{h,cpp}` / `AudioTakePlayer.{h,cpp}` — per-channel
  Take playback. **MIDI Takes are kept in ticks, not samples**, and
  converted per block against the tempo read fresh each callback, so a
  tempo change is audible immediately (it used to be baked in at load, and
  changing the tempo did nothing until the Take was reselected). The
  musical position is integrated block by block, so a change applies from
  wherever playback has reached rather than retroactively; a genuine jump
  (RTZ, a Range loop wrap, a new Take, a typed seek) re-anchors to the
  transport position at the current tempo, while pause/resume deliberately
  does not.
- `Source/ClickGenerator.h` — the metronome. Synthesized inline (no plugin,
  no MIDI, no audio file), on the same tick clock and re-anchor rule as
  `MidiTakePlayer` so click and Take can't drift apart. Mixed in as the
  very last thing in the audio callback: after the master chain, after
  `RecordingManager`'s tap (so it can never be recorded), and after the
  peak metering (so it doesn't move the meters) — which is why its default
  level is -12 dB, since it sits outside clip detection.
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
