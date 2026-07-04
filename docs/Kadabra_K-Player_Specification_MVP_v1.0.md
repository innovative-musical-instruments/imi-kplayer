# KADABRA K-PLAYER

Product Specification — MVP v1.0

IMI K-Samplers — Kadabra Instrument Suite
June 2026

| | |
|---|---|
| **Product Name** | Kadabra K-Player |
| **Version** | MVP 1.0 |
| **Platform** | macOS + Windows (cross-platform from day one) |
| **Framework** | JUCE (C++) |
| **Plugin Formats** | VST3 (all platforms) + AU (macOS only) |
| **Status** | Pre-development — Specification Phase |
| **Author** | Vinch |

> **Revision note (2026-07-04):** converted from the original `.docx` to Markdown so the spec can be tracked and diffed in git alongside the code. Section 3 updated to change the insert chain from 8 to 5 slots and to make the instrument slot (slot 0) accept either an instrument or an audio-effect plugin, while insert slots 1–5 accept audio-effect plugins only. See the changelog at the end of this document.

## 1. Overview

Kadabra K-Player is a standalone plugin-hosting application that complements the IMI K-Samplers instrument suite. It provides a compact, performance-oriented environment for live MIDI playback through VST3/AU instruments — designed specifically around the K-Samplers workflow, but general enough to host any compatible third-party plugin.

The application is not a full DAW. It does not record audio or MIDI in its MVP form, nor does it have a timeline, clips, or an arranger. Its purpose is to be the best possible live playback host for keyboard players and producers who want to trigger the K-Samplers instruments in real time, with full per-channel signal processing and a clean, focused interface.

### 1.1 Motivation

The K-Samplers instruments (Kadabra Grand, Electric Piano, Electronic Drumkit, Acoustic Drums, Percussion) are built in HISE and released as standalone instruments and VST3/AU plugins. Musicians who want to play multiple K-Samplers simultaneously currently have no dedicated environment for doing so — they must either load them into a full DAW (heavyweight, complex) or run them as separate standalone instances (no shared routing, no per-channel effects).

K-Player solves this by providing a lightweight, purpose-built host: 16 channels, each with a MIDI input assignment, a plugin slot that accepts an instrument or an audio effect, and 5 additional insert effect slots. All channels mix to a single stereo master output.

### 1.2 Out of Scope for MVP

- Audio recording or MIDI recording
- A timeline or arranger view
- Audio clip playback
- MIDI-only channels (no instrument slot)
- Send/return routing or bus channels
- Automation lanes
- Multi-output routing (all channels sum to stereo master in MVP)
- Plugin sandboxing or crash recovery
- AAX format (post-MVP)
- MIDI-FX processing slot(s) ahead of the instrument (deferred — see §10.1)

## 2. Architecture & Technology

### 2.1 Framework

The application is built entirely on JUCE (C++17 minimum). JUCE is chosen for:

- First-class VST3 hosting via `juce::AudioPluginHost` infrastructure
- Native AU hosting on macOS via the AudioUnit wrapper
- Cross-platform audio device management (ASIO on Windows, CoreAudio on macOS)
- Cross-platform MIDI device enumeration and management
- A single codebase targeting both macOS and Windows

### 2.2 Plugin Format Support

| Format | Support |
|---|---|
| VST3 | Supported on macOS and Windows |
| AU (Audio Units) | Supported on macOS only |
| AAX | Not in MVP scope |
| VST2 | Not supported (deprecated) |

### 2.3 Audio Engine

- Audio I/O via JUCE `AudioDeviceManager`
- CoreAudio on macOS; WASAPI and ASIO on Windows
- Configurable sample rate and buffer size via a Preferences panel
- All channel processing occurs in a single audio callback on the audio thread
- Each channel's instrument and inserts are processed in series: Instrument/Effect (Slot 0) → Insert 1 → … → Insert 5 → Master Mix
- Final stereo mix is sent to the selected output device

### 2.4 MIDI Engine

- MIDI I/O via JUCE `MidiInput` / `MidiOutput`
- Each channel is assigned one MIDI device + one MIDI channel (1–16)
- Incoming MIDI is filtered per channel by device and channel number, then forwarded to the channel's slot-0 plugin
- Multiple channels may share the same MIDI device, listening on different MIDI channel numbers
- MIDI devices are enumerated at launch; a rescan is available from the Preferences panel

### 2.5 Session Persistence

- Sessions are saved as JSON files with a `.kplayer` extension
- A session file stores: the complete state of all 16 channels, plugin identifiers (format, manufacturer, name, uid), plugin state blobs (binary, base64-encoded), MIDI device assignments, channel names and colors, master volume, audio device settings
- Session files are loaded via File > Open Session and saved via File > Save / Save As
- On first launch, the application opens with a blank default session
- The application prompts to save unsaved changes on quit or when opening a new session

## 3. The Channel Entity

The Channel is the core data and processing unit of K-Player. It is designed to be extensible: the MVP defines one channel type (Instrument Channel), but the architecture must accommodate future channel types (aux, audio input, MIDI-only, group/bus) without requiring significant refactoring.

### 3.1 Channel Data Model

Each channel carries the following properties:

| Field | Description |
|---|---|
| `id` | Unique UUID assigned at creation, never changes |
| `type` | Enum: `INSTRUMENT` (MVP) \| `AUX` \| `AUDIO_INPUT` \| `GROUP` (future) |
| `name` | User-editable string, default: "Channel 1" … "Channel 16" |
| `color` | User-selectable color for visual identification in the mixer |
| `enabled` | Boolean: channel is active and processing |
| `mute` | Boolean: channel audio is silenced (processing still runs) |
| `solo` | Boolean: all non-soloed channels are silenced |
| `volume` | Float 0.0–1.0, default 1.0 (displayed as 0 dB) |
| `pan` | Float -1.0 to +1.0, default 0.0 (center) |
| `midiDeviceIdentifier` | String: JUCE `MidiDeviceInfo::identifier` of the assigned MIDI input device — the stable key used for matching/reconnection (see §7.3); empty string means unassigned |
| `midiDeviceName` | String: display name of the assigned device at time of save, kept alongside the identifier as a human-readable fallback for when the identifier can't be resolved |
| `midiChannel` | Int 1–16: the MIDI channel this channel listens on |
| `slot0Plugin` | `PluginSlot` (see §3.2), nullable. Accepts **either** an instrument **or** an audio-effect plugin |
| `insertPlugins` | Array of **5** `PluginSlot` entries (see §3.2). Accepts **audio-effect plugins only** — instrument plugins are filtered out of the browser for these slots |
| `audioInput` | Mono audio input assignment (for future use; provisionally included in data model) |

> **Naming note:** this field was called `instrumentPlugin` prior to 2026-07-04. Renamed to `slot0Plugin` to reflect that it's no longer instrument-only — see §3.3 and the changelog.

### 3.2 Plugin Slot

A `PluginSlot` represents a single plugin position (slot 0 or one of the 5 inserts). It holds:

| Field | Description |
|---|---|
| `pluginDescription` | JUCE `PluginDescription`: format, name, manufacturer, uid, fileOrIdentifier, and everything else JUCE itself considers part of a plugin's identity |
| `isLoaded` | Boolean: plugin was successfully instantiated |
| `isBypassed` | Boolean: plugin is in the signal path but bypassed |
| `stateBlob` | Base64-encoded binary: the plugin's full state (preset/patch) |

> **Session format note (2026-07-04):** in the `.kplayer` JSON, `pluginDescription` is serialized via JUCE's own `PluginDescription::createXml()`/`loadFromXml()` (an XML string embedded as one JSON field), not as flat JSON keys — see §6.4. This captures every field JUCE's own plugin-matching logic considers, not just the 5 the earlier draft of this spec listed, and reuses the same mechanism the plugin scan cache already relies on (`KnownPluginList`'s own XML serialization). Editor window visibility/position persistence (`editorVisible`/`editorBounds` in the original draft) is deferred out of the first session-format pass — see §4.4 and §6.4.

### 3.3 Signal Flow per Channel (MVP)

For an Instrument Channel, audio signal flows as follows:

```
MIDI Input Device + Channel
    ↓
Slot 0: Instrument OR Audio-Effect Plugin (VST3/AU)
    ↓
Insert 1 → Insert 2 → Insert 3 → Insert 4 → Insert 5 (Audio-Effect Plugins only, VST3/AU)
    ↓
Volume & Pan
    ↓
Stereo Master Mix Bus
```

Slot 0 is flexible so a channel can, for example, host a pure audio-effect chain fed by another means in a future channel type, or host the instrument as originally designed. In the MVP, slot 0 is expected to usually hold the instrument, since MIDI is only routed into slot 0. Insert slots 1–5 never receive MIDI and only accept audio-effect plugins (instruments are filtered out of the plugin browser when opened from an insert slot).

### 3.4 Audio Format

- All channels process in stereo (two-channel float buffer)
- Mono audio input (provisional, for future use) will be upmixed to stereo before entering the insert chain
- Plugins that output more than 2 channels will be downmixed to stereo at the channel boundary

## 4. Plugin Management

### 4.1 Plugin Scanning

- On first launch, K-Player performs an automatic scan of all known VST3 and AU folder locations
- Scan results are cached to disk; subsequent launches load from cache
- A manual rescan is available from Preferences > Plugins > Rescan
- Failed plugins (crash during scan) are blacklisted and listed in a separate panel with the option to retry

### 4.2 Default Scan Locations

**macOS**
- `/Library/Audio/Plug-Ins/VST3` (system)
- `~/Library/Audio/Plug-Ins/VST3` (user)
- `/Library/Audio/Plug-Ins/Components` (AU — system)
- `~/Library/Audio/Plug-Ins/Components` (AU — user)

**Windows**
- `C:\Program Files\Common Files\VST3` (system)
- `%LOCALAPPDATA%\Programs\Common\VST3` (user)

### 4.3 Plugin Browser

Accessed by clicking an empty slot-0 or insert slot. The browser provides:

- A flat, scrollable list of all scanned plugins
- A search/filter field (by plugin name or manufacturer)
- Format indicator badge (VST3 / AU)
- Double-click or Enter to load into the slot
- The browser remembers the last search query within the session
- **Slot-aware filtering:** when opened from slot 0, both instrument and audio-effect plugins are listed; when opened from an insert slot (1–5), instrument plugins are filtered out and only audio-effect plugins are shown

### 4.4 Plugin Editor Windows

- Clicking a loaded plugin slot opens the plugin's native editor window
- Editor windows are floating (non-modal), associated with their channel and slot
- Multiple editor windows may be open simultaneously
- Editor window positions are saved per-session *(deferred — first session-format pass does not persist editor visibility/position; sessions reopen with all editor windows closed. See §6.4.)*
- Closing the main K-Player window hides but does not destroy editor windows; re-opening restores them

## 5. User Interface

### 5.1 Layout Philosophy

The UI is focused, dense, and performance-oriented. It is not a full DAW UI. The design should feel closer to a hardware rack controller or a DJ mixer than a traditional DAW: every channel is always visible, controls are compact, and the plugin slots are accessible without deep navigation.

### 5.2 Main Window Structure

The main window contains three horizontal zones:

| Zone | Contents |
|---|---|
| Toolbar (top) | Application menu access, session name, global transport (future), CPU/memory meter, master output level, master volume knob |
| Channel Strip Area (center) | 16 channel strips displayed side by side in a scrollable horizontal view |
| Master Section (right or bottom) | Master stereo output level meter, master volume fader, output device selector |

### 5.3 Channel Strip

Each channel strip is a vertical column containing (top to bottom):

- Channel header: number badge, user-editable name, color swatch
- MIDI assignment: compact display showing device abbreviation + channel number; click to reassign
- Slot 0: plugin name button (click to open browser if empty, click to open editor if loaded); bypass toggle; remove button. Accepts instrument or audio-effect plugins.
- Insert slots 1–5: five compact plugin slots in series, audio-effect plugins only; each has the same controls as slot 0
- Channel fader: vertical fader for volume
- Pan knob: stereo panning
- Level meter: real-time stereo peak meter
- Mute / Solo buttons

### 5.4 Context Menus

Right-clicking a loaded plugin slot shows:

- Open Editor
- Close Editor
- Bypass
- Copy Plugin State
- Paste Plugin State
- Replace Plugin… (opens browser)
- Remove Plugin

### 5.5 Preferences Panel

Accessible via the application menu or keyboard shortcut. Contains:

- Audio: device type, output device, sample rate, buffer size
- MIDI: list of active MIDI input devices; enable/disable per device
- Plugins: scan folder list (add/remove custom paths); Rescan button; plugin blacklist manager
- General: application theme (light/dark), channel strip width, tooltips on/off

## 6. Session File Format

### 6.1 Format

Sessions are stored as UTF-8 JSON files with the extension `.kplayer`. The format is versioned from v1.0 to allow forward migration.

### 6.2 Top-Level Structure

```json
{
  "version": "1.0",
  "appVersion": "0.1.0",
  "createdAt": "<ISO 8601 timestamp>",
  "sessionName": "My Session",
  "audioDeviceStateXml": "<AudioDeviceManager::createStateXml() as a string>",
  "masterVolume": 1.0,
  "tempo": 120.0,
  "channels": [ ... 16 channel objects ... ]
}
```

`audioDeviceStateXml` embeds JUCE's own `AudioDeviceManager::createStateXml()` output (device name, sample rate, buffer size, enabled MIDI inputs) rather than a hand-rolled set of fields — same rationale as `pluginDescription` below: JUCE already owns this serialization and `AudioDeviceManager::initialise()` takes it straight back on load. `tempo` (BPM, global/transport-wide across all channels) was missing from the original draft of this schema even though the tempo feature already exists in the app; added here alongside `masterVolume`.

### 6.3 Channel Object

```json
{
  "id": "<uuid>",
  "type": "INSTRUMENT",
  "name": "Channel 1",
  "color": "#3D5A80",
  "enabled": true,
  "mute": false,
  "solo": false,
  "volume": 1.0,
  "pan": 0.0,
  "midiDeviceIdentifier": "<JUCE MidiDeviceInfo::identifier, or empty for none>",
  "midiDeviceName": "Kadabra Virtual MIDI",
  "midiChannel": 1,
  "slot0Plugin": { <PluginSlot> | null },
  "insertPlugins": [ <PluginSlot x5> ]
}
```

`color` and `enabled` are serialized with defaults (`color` a fixed default, `enabled` always `true`) even though no UI exists yet to change them — cheaper to include from the start than to schema-migrate later once that UI is built.

### 6.4 PluginSlot Object

```json
{
  "pluginDescriptionXml": "<PLUGIN name=\"Kadabra Grand\" format=\"VST3\" manufacturer=\"IMI\" .../>",
  "isBypassed": false,
  "stateBlob": "<base64 encoded binary, from AudioProcessor::getStateInformation()>"
}
```

`pluginDescriptionXml` is the string form of `PluginDescription::createXml()` (parsed back via `juce::XmlDocument::parse()` + `PluginDescription::loadFromXml()` on load), replacing the flat `format`/`name`/`manufacturer`/`uid`/`fileOrIdentifier` fields from the original draft of this schema — see the note in §3.2. `isLoaded` isn't serialized; it's derived at load time from whether `formatManager.createPluginInstance()` succeeds. `editorVisible`/`editorBounds` from the original draft are deferred (see §4.4) — not part of the schema in this first pass.

## 7. MIDI Routing Details

### 7.1 Assignment Model

Each channel has exactly one MIDI input assignment: a (device, channel) pair. The application:

- Enumerates all available MIDI input devices at launch using JUCE `MidiInput::getAvailableDevices()`
- Opens each enabled MIDI device once, regardless of how many channels use it
- Routes incoming messages to channels by matching the device ID and MIDI channel number
- Sends only Note On, Note Off, Pitch Bend, Aftertouch, and CC messages to slot-0 plugins; clock and SysEx are filtered by default

### 7.2 Unassigned Channels

A channel with no MIDI device assigned will receive no MIDI events and produce no audio output (unless slot 0 hosts an audio-effect plugin with no MIDI-driven source ahead of it, in which case it simply passes no signal). Its slot-0 plugin is still loaded and its state is preserved. This is a valid "parked" state.

### 7.3 MIDI Device Reconnection

If a previously assigned MIDI device is not present, the channel retains its `midiDeviceIdentifier`/`midiDeviceName` assignment but shows a warning indicator. **Implemented 2026-07-04**, and revised the same day from a periodic (2s) poll to an event-driven check via JUCE's `MidiDeviceListConnection`: the channel's MIDI-In control colors and shows a tooltip immediately when the assigned identifier isn't in the currently available device list, and clears immediately when it reappears — no polling delay, no restart required. Automatic hot-plug reconnection is also implemented: `MainComponent` re-enables and re-registers a callback for every currently available MIDI input whenever the device list changes (not just once at launch), so a device plugged in or reconnected after launch works immediately without an app restart.

## 8. Master Section

### 8.1 Master Mix

All 16 channels are summed into a single stereo mix bus. Summing is additive (no auto-gain normalization). It is the user's responsibility to manage levels to avoid clipping.

### 8.2 Master Controls

- Master Volume fader: applies a final gain stage before output (range: -inf to +6 dB)
- Master Level Meter: peak + RMS stereo meter with clip indicator and peak hold
- Output Device Selector: quick-access dropdown in the toolbar (mirrors Preferences > Audio)

### 8.3 Master Inserts (Post-MVP Placeholder)

The architecture should reserve slot space for future master insert effects (limiter, EQ, compressor). These are not exposed in the MVP UI but the data model should include a `masterInsertPlugins` array of 4 slots in the session format.

## 9. Development Phases

**Phase 1 — Foundation (Audio Engine + Host)**
- JUCE project setup, CMake build system, code signing prep for both platforms
- AudioDeviceManager setup with CoreAudio + WASAPI/ASIO
- VST3 + AU scanning infrastructure, plugin cache
- Single-channel instrument hosting proof of concept
- MIDI device enumeration and routing

**Phase 2 — Channel Architecture**
- Full Channel entity (data model + audio processing graph)
- 16-channel mixer with stereo summing
- Insert chain processing (5 slots per channel)
- Mute, solo, volume, pan

**Phase 3 — Session Persistence**
- Session save/load (`.kplayer` JSON format)
- Plugin state serialization/deserialization (`stateBlob`)
- Unsaved-changes detection and prompt on quit
- Recent sessions menu

**Phase 4 — UI**
- Main window layout with toolbar, channel strips, master section
- Channel strip component (all controls)
- Plugin browser panel
- Preferences panel
- Plugin editor window management

**Phase 5 — Polish & Release Prep**
- Code signing and notarization (macOS); code signing (Windows)
- Installer creation (PKG/DMG for macOS; NSIS or WiX for Windows)
- Performance profiling and optimization
- Crash reporting integration
- User-facing documentation

## 10. Open Questions & Future Considerations

### 10.1 Decisions Deferred to Development

- UI toolkit: JUCE native components vs. a custom OpenGL/Metal renderer vs. a third-party JUCE UI library (e.g., `melatonin::inspector`, `juce_ftl`). To be decided during Phase 4.
- Plugin crash isolation: sandbox each plugin in a separate process (improves stability, significant complexity). Post-MVP.
- MIDI output: allow channels to forward processed MIDI to an output device. Post-MVP.
- Mono audio input per channel: provisionally included in the data model; UI and routing to be designed post-MVP.
- **MIDI-FX slot(s):** one or more MIDI-only processing slots ahead of slot 0, transforming incoming MIDI before it reaches the instrument. Proposed 2026-07-04; deferred — needs its own slot type (MIDI in/out only, no audio path) distinct from `PluginSlot`, and a decision on whether to restrict it to JUCE MIDI-effect plugins or allow pass-through audio-effect-shaped plugins too.

### 10.2 Future Channel Types

- Audio Input Channel: receives audio from an input device instead of a MIDI-driven instrument
- Aux/Return Channel: receives from a send bus
- Group/Bus Channel: receives the summed output of a group of channels
- MIDI-Only Channel: routes MIDI without producing audio (e.g., to an external device)

### 10.3 Future Features

- Recording: MIDI and audio capture to disk
- A timeline/arranger view for basic arrangement
- More than 16 channels (the channel entity UUID model already supports this)
- Master section insert plugins
- Setlist mode: rapid session switching for live performance
- OSC control surface support

## Changelog

- **2026-07-04:** Converted from `.docx` to Markdown (tracked in git going forward). Reduced insert slot count from 8 to 5 (§1.1, §2.3, §3.1, §3.3, §5.3, §6.3, §9 Phase 2). Renamed `instrumentPlugin` to `slot0Plugin` and made it accept either an instrument or an audio-effect plugin, while insert slots 1–5 are restricted to audio-effect plugins only (§3.1, §3.3, §4.3, §6.3, §7.1). Added MIDI-FX slot as a deferred open question (§1.2, §10.1) rather than part of the MVP insert chain.
- **2026-07-04 (session format design pass):** designed the `.kplayer` session format ahead of implementation (§6). Split `midiDevice` into `midiDeviceIdentifier` (stable match key) + `midiDeviceName` (display fallback), matching the identifier-based routing already implemented (§3.1, §6.3). Switched `PluginSlot`'s plugin identity from flat JSON fields to an embedded `PluginDescription::createXml()` string (`pluginDescriptionXml`), and `audioDevice` similarly to an embedded `AudioDeviceManager::createStateXml()` string — both reuse JUCE's own serialization instead of hand-rolled field lists (§3.2, §6.2, §6.4). Added the previously-undocumented `tempo` field at the top level (§6.2). Deferred `editorVisible`/`editorBounds` persistence out of the first implementation pass (§4.4, §6.4). Documented the MIDI-device warning indicator as implemented, with hot-plug auto-reconnect still open (§7.3).
- **2026-07-04 (session save/load implemented):** built the `.kplayer` save/load round-trip designed above, verified working end-to-end. File menu gained real Open/Save/Save As commands with keyboard shortcuts. Scaled from 4 to 12 channels with a narrower (80px) channel strip now that the multichannel architecture is validated. Implemented MIDI hot-plug auto-reconnect via `MidiDeviceListConnection` and switched the device-missing warning indicator (§7.3) from a 2s poll to the same event-driven mechanism — both now react immediately instead of requiring an app restart or waiting on a timer.

---
End of Specification — Kadabra K-Player MVP v1.0
