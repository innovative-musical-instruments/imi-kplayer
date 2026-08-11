# Kadabra KPlayer — User Guide

Kadabra KPlayer is a standalone plugin-host app built around a familiar
"channel strip" mixer layout. Each channel loads an instrument plugin plus a
chain of effect inserts, and gives you gain, pan, mute/solo, MIDI routing,
and audio-input routing, all in one rack. A master bus at the right of the
rack applies its own insert chain and final output gain to the combined mix.
KPlayer also includes multitrack recording, session save/load, and MIDI
Take capture/playback, so you can build a rig, perform with it, and record
the result — all from one app.

## Table of Contents

1. [Launching KPlayer](#1-launching-kplayer)
2. [The Main Window at a Glance](#2-the-main-window-at-a-glance)
3. [Channel Strip Anatomy](#3-channel-strip-anatomy)
4. [Loading and Managing Plugins](#4-loading-and-managing-plugins)
5. [MIDI Routing](#5-midi-routing)
6. [Audio Input Routing](#6-audio-input-routing)
7. [The Master Bus](#7-the-master-bus)
8. [Tempo and MIDI Clock Sync](#8-tempo-and-midi-clock-sync)
9. [Transport: Play, Pause, and Return to Zero](#9-transport-play-pause-and-return-to-zero)
10. [Recording](#10-recording)
11. [Importing Audio to a Track](#11-importing-audio-to-a-track)
12. [Saving and Loading Sessions](#12-saving-and-loading-sessions)
13. [Show Mode vs. Work Mode](#13-show-mode-vs-work-mode)
14. [Settings](#14-settings)
15. [Adding and Removing Channels](#15-adding-and-removing-channels)
16. [Panic](#16-panic)
17. [Cross-Platform Notes](#17-cross-platform-notes)
18. [Kadabra Hardware Integration](#18-kadabra-hardware-integration)
19. [Troubleshooting](#19-troubleshooting)
20. [Keyboard Shortcuts and Menu Reference](#20-keyboard-shortcuts-and-menu-reference)

---

## 1. Launching KPlayer

When you start KPlayer, a splash screen appears briefly while your audio
devices initialize and the main window is built. Once the main window is
visible, KPlayer scans your computer for installed VST3 (and, on Mac, Audio
Unit) plugins in the background. A "Scanning plugins…" overlay covers the
window while this happens, showing the name of the plugin currently being
scanned and a progress bar. The plugin browser is unavailable until the scan
finishes — everything else in the window is visible but the overlay blocks
clicks until it's done.

On a completely fresh launch, you'll see an empty rack of 12 channels with
no plugins loaded, ready to build a rig from scratch.

**If you use Kadabra hardware:** when a Kadabra device is connected at
launch, KPlayer automatically loads your most recent session (a "recovery"
snapshot saved automatically the last time you quit with Kadabra connected)
or, if none exists yet, a factory starter session. See
[Kadabra Hardware Integration](#18-kadabra-hardware-integration) for details.

## 2. The Main Window at a Glance

The main window is organized into two areas:

- **Channel rack + Master strip** (top) — the scrollable channel rack
  holds one vertical strip per channel, each with an instrument, insert
  effects, routing, and mix controls; the master strip immediately to its
  right applies the master bus's own insert chain, output fader, and
  meters to the combined mix.
- **Global bar** (bottom, always visible, spans the full window width) —
  left to right: the IMI logo, channel count controls, a Settings button,
  and a collapse toggle for each channel's I/O rows; centered, tempo/sync
  controls with a MIDI sync device selector, a transport time readout, the
  session transport (Play/Pause, RTZ), and Record Ready; then Show/Work
  Mode, Panic, and the Tribal Tools logo.

If your channel count doesn't fit on screen, scroll the channel rack
horizontally to reach the rest — the master strip stays fixed on the
right, and the global bar stays fixed along the bottom.

## 3. Channel Strip Anatomy

Each channel strip is divided into three sections, top to bottom:

### Input section

- **Channel name** — shows "Channel N" by default. Double-click to type a
  custom name; the number prefix always stays, so you can't accidentally
  edit or delete it.
- **Audio In** — routes a live hardware input channel (or a previously
  recorded Take) into this channel. See [Audio Input Routing](#6-audio-input-routing).
- **MIDI In** — routes a MIDI input device (or a previously recorded MIDI
  Take) into this channel. See [MIDI Routing](#5-midi-routing).
- **MIDI Ch** — restricts the channel to a specific MIDI channel (1–16) or
  "All".

You can hide these three rows for every channel at once using the "Hide
I/O" button in the global bar — handy once your routing is set and you
want more vertical room for plugin slots.

### Plugins section

- **Instrument** — slot 0, the top slot. This is normally where you load
  your instrument (synth/sampler) plugin, but it also accepts audio-effect
  plugins if you prefer to use the channel purely as an effect chain fed
  by a live/recorded audio input.
- **Inserts (1–5)** — five additional effect slots, processed in order
  after the instrument slot. Insert slots also accept instrument plugins if
  you want to load one there instead — just be aware a slot after slot 0
  only receives whatever audio arrived from slot 0, so an instrument loaded
  in an insert slot may produce no sound unless something upstream is
  actually feeding it audio.

### Mix section

- **Gain fader** — vertical fader, −96 dB to +6 dB, curved so most of the
  fader's travel covers the useful range near unity gain. Double-click to
  reset to 0 dB.
- **Pan** — horizontal slider, −50 (full left) to +50 (full right).
  Double-click to reset to center.
- **Peak meter** — a vertical level meter beside the fader with clip
  indication.
- **Mute (M) / Solo (S)** — standard mute and solo buttons.
- **Arm (●)** — arms this channel for recording. See [Recording](#10-recording).

## 4. Loading and Managing Plugins

Click any empty plugin slot (instrument or insert) to open the plugin
browser directly. If a slot already has a plugin loaded, clicking it opens
a small menu instead:

- **Show Plugin / Hide Plugin** — opens or closes the plugin's own editor
  window.
- **Bypass / Un-bypass** — bypasses the slot without unloading it. A
  bypassed slot's button dims to indicate its state. The plugin's own
  editor window also shows an **Active/Bypassed** bar at the top, and
  toggling it there does the same thing.
- **Replace Plugin…** — opens the browser to swap in a different plugin,
  discarding the current one.
- **Remove Plugin** — unloads the plugin from the slot entirely.

### The plugin browser

The browser lists every plugin KPlayer found during its scan, and supports:

- **Search** — type to filter by name.
- **Sort: A-Z / Sort: Manufacturer** — toggle between alphabetical and
  grouped-by-manufacturer listing.
- **Favorites** — click the star beside any plugin to favorite it; favorited
  plugins get their own section at the top of the list.
- **Recently Used** — plugins you've loaded recently appear in their own
  section for quick access.
- **Failed to Load** — any plugin that couldn't be scanned/loaded
  successfully (e.g. a corrupted install) is listed here separately, so it
  doesn't silently disappear; you can retry loading it from here.

Double-click (or select and press Enter) to load a plugin into the slot you
opened the browser from.

## 5. MIDI Routing

Each channel's **MIDI In** selector lists:

- **None** — no MIDI input.
- Every currently connected MIDI input device.
- **Recorded Takes** (if any exist for this channel) — a previously
  recorded MIDI performance for this channel, played back instead of a
  live device. See [Recording](#10-recording).

The **MIDI Ch** selector next to it restricts the channel to a specific
MIDI channel (1–16), or "All" to accept every channel from the selected
device.

MIDI devices are hot-pluggable: connecting or disconnecting a device
updates every channel's MIDI In list immediately, with no restart needed.
If a channel's selected device becomes unavailable (unplugged, or a Take
file that's gone missing from disk), its MIDI In box turns orange as a
warning, with a tooltip explaining why — the routing itself is left alone
in case the device reappears.

## 6. Audio Input Routing

Each channel's **Audio In** selector lists:

- **None** — no audio input feeds this channel (the instrument plugin, if
  any, is the only audio source).
- Every currently active input channel on your selected audio interface.
- **Recorded Takes** (if any exist for this channel) — a previously
  recorded audio performance for this channel, played back instead of live
  input.

Selecting a Take here automatically bypasses a loaded instrument in slot 0
(so the Take is audible immediately rather than being silently masked by
the instrument), while leaving the option to un-bypass it yourself if you
want the Take feeding the instrument instead (e.g. as a vocoder carrier).
This is a one-time convenience — un-bypassing afterward is never
automatically reversed.

To enable more hardware input channels for routing, use
[Settings](#14-settings) → the audio device selector, and turn on
additional input channels there.

## 7. The Master Bus

The master strip, to the right of the channel rack, applies its own 5-slot
insert chain to the combined signal of every channel, after
each channel's own processing and before the final output stage. It works
the same way a channel's insert slots do — click to load, click a loaded
slot for the show/hide/bypass/replace/remove menu — just without an
instrument slot of its own.

Below the insert slots sit the master output fader (same curve and range
as a channel's gain fader), left/right peak meters, and a master **ARM**
button for including the master bus in a recording. See
[Recording](#10-recording).

## 8. Tempo and MIDI Clock Sync

The global bar includes a tempo display and Sync control:

- With sync off, click the BPM value to type a tempo directly (20–300 BPM).
  This tempo is shared by every channel's and the master bus's plugins.
- Click **Sync** and pick a MIDI input device to instead follow that
  device's incoming MIDI clock. While synced, the tempo display becomes
  read-only and tracks the detected clock tempo live. If the sync device
  stops sending clock, the display turns orange and holds the last known
  tempo rather than reverting to a manual value.

## 9. Transport: Play, Pause, and Return to Zero

The global bar's transport controls a shared playhead used for MIDI
Take playback (and Audio Take playback, which follows the same clock):

- **PLAY / PAUSE** — starts or pauses playback of anything currently
  routed to a Recorded Take.
- **RTZ** — returns the playhead to the very start.
- A **mm:ss** time readout shows elapsed transport time (and doubles as the
  recording-elapsed display while recording is active).

Playback is independent of recording — you can audition a recorded Take
through a different instrument with Play alone, with nothing being written
to disk.

## 10. Recording

KPlayer records every armed channel's processed output (post-fader,
post-inserts) and/or the master bus to individual WAV files, all starting
and stopping together as a single **take**.

### Arming and recording

1. Click a channel's **Arm (●)** button (or the master strip's **ARM**
   button) for every source you want captured. Armed-but-not-recording
   shows a dim red; armed-and-recording shows solid red.
2. Click the **Record Ready** button (the red dot icon in the global bar's
   transport controls). This is a two-step "Record Ready" control:
   - First click arms the take (the button blinks) and waits for you to
     press Play.
   - If the transport is already playing, recording starts immediately on
     that same click instead of waiting.
   - Clicking REC again while armed (but not yet recording) cancels back
     to idle.
   - Clicking REC while actively recording stops the take.
3. The first time you record, KPlayer asks you to choose a recordings
   folder if one isn't set yet (also settable in advance from
   [Settings](#14-settings)).

You can arm or unarm a channel while a take is already in progress —
arming mid-take starts a fresh file for that channel from that moment on
(padded with silence at the start so it still lines up sample-for-sample
with the rest of the take if you drop everything into a DAW afterward).

### Where files are written

Each take creates its own timestamped subfolder inside your configured
recordings folder (for example `2026-08-09_14-30-05`), containing:

- `Channel N.wav` for each armed channel (plus `Channel N.mid`, a Standard
  MIDI File capturing that channel's incoming MIDI notes/CCs during the
  take, if it received any).
- `Master.wav` if the master bus was armed.

These recorded files automatically show up as **Recorded Takes** in that
same channel's MIDI In and Audio In selectors afterward, ready to play back
or route into a different instrument.

### Auto-stop

A take stops automatically if:

- Every currently-recording source has been silent for longer than the
  configured silence timeout (default 60 seconds; adjustable in
  [Settings](#14-settings)).
- Free disk space on the recordings drive drops below a safety margin
  (200 MB), protecting against filling the disk on an unattended long
  recording.

Either case shows you the reason once the take stops.

## 11. Importing Audio to a Track

Use **File → Import Audio to Track…** to bring an existing audio file
(WAV, AIFF, FLAC, Ogg Vorbis, or MP3) into KPlayer as if it had been
recorded live:

1. Pick the source file.
2. Choose which channel to assign it to — an existing channel, or "New
   Track" to create a fresh one.
3. If no recordings folder is set yet, you'll be asked to choose one, same
   as the first live recording.

The file is resampled to match your session's sample rate if needed, and
mono files are upmixed to stereo. It's saved into a new take folder and
automatically selected as that channel's audio input, exactly like picking
a Recorded Take — including the same auto-bypass of a loaded instrument in
slot 0.

## 12. Saving and Loading Sessions

KPlayer sessions are saved as `.kplayer` files (also called a "KPlayer
session"). A session captures:

- Every channel's plugin chain (instrument + inserts), including each
  plugin's own internal state/preset.
- Gain, pan, mute, solo, name, and record-arm state per channel.
- Each channel's MIDI and audio input routing (including Recorded Take
  selections).
- The master bus insert chain and output volume.
- Tempo and MIDI clock sync settings.
- Your audio device configuration.
- Recordings folder and silence-timeout setting.
- Window size and the collapsed/expanded state of the channel I/O rows.

**File menu:**

- **Open Session…** — loads a `.kplayer` file, replacing the current rig.
- **Save Session** — saves to the current file, or prompts for a location
  if this session hasn't been saved yet.
- **Save Session As…** — always prompts for a new location.
- **Recent** — your last several opened/saved sessions, for quick access.

If you have unsaved changes and try to open a different session or quit,
KPlayer prompts you to Save, Discard, or Cancel (unless Show Mode is
active — see below). The window title shows an asterisk (`*`) whenever
there are unsaved changes.

Note that KPlayer never saves automatically on its own — the asterisk and
the Save/Discard/Cancel prompt are the only things standing between you and
losing changes, so save deliberately when it matters. One related, expected
behavior: if a plugin's parameters are being driven live by a controller
(e.g. hardware automation), the session keeps re-marking itself dirty for as
long as that's happening, even right after you've just saved — that's by
design, not a bug, since the plugin genuinely keeps changing state.

## 13. Show Mode vs. Work Mode

The global bar has a toggle between **Work Mode** (default) and **Show
Mode**:

- **Work Mode** — every session-discarding action (Open Session, opening a
  Recent file) prompts you to save first if there are unsaved changes.
- **Show Mode** — the same actions skip that prompt entirely and just load
  the new session directly. This is meant for live performance, where
  you're switching between several prepared sessions in a set and don't
  want a confirmation dialog interrupting the flow each time. Show Mode is
  an app-level preference (not saved inside a particular session file) and
  persists across relaunches.

Quitting the app is unaffected by this toggle either way — it still
follows the normal unsaved-changes prompt (except for the Kadabra recovery
behavior described below).

## 14. Settings

Open **Settings** from the global bar's Settings button, or **File →
Settings…**. It's a non-modal window — clicking Settings again while it's
open just closes it. Settings covers:

- **Audio & MIDI device selection** — choose your audio interface, enable
  additional input channels (up to 8) for per-channel audio routing, and
  enable/disable MIDI input devices.
- **Recording** — the recordings folder (Choose… to pick one) and the
  auto-stop-after-silence timeout, in seconds.
- **Plugins** — a **Rescan Plugins** button, which repeats the same scan
  done at launch to pick up newly installed plugins without restarting
  the app.

Channel count is not in Settings — it's controlled directly from the
global bar's +/− buttons (see below).

## 15. Adding and Removing Channels

Use the **+ / −** buttons in the global bar (labeled "Channels") to
grow or shrink the rack, from 1 up to 24 channels. KPlayer starts a fresh
session with 12.

- Growing the channel count is always immediate — new channels are empty
  and never destructive.
- Shrinking asks for confirmation first if any of the channels being
  removed still has a loaded plugin, since that plugin (and its settings)
  would be discarded.

## 16. Panic

The global bar's **PANIC** button (also **File → Panic** or
**Cmd/Ctrl+.**) immediately sends all-notes-off / all-sound-off to every
loaded instrument — useful if a stuck note or runaway plugin needs
silencing right away, without having to find and mute the specific channel.

## 17. Cross-Platform Notes

KPlayer runs on both Mac and Windows, and `.kplayer` session files are
designed to move between them. A session saved on one platform — including
its loaded VST3 plugins and their internal state — loads correctly on the
other, as long as the same plugins are installed on both machines;
KPlayer re-matches each saved plugin by its real identity rather than by
the (platform-specific) file path that was saved. One limitation: a plugin
loaded specifically as an Audio Unit (Mac-only format) has no Windows
equivalent to relink to, so an AU-loaded slot won't carry over to Windows.

## 18. Kadabra Hardware Integration

KPlayer is designed to pair with Kadabra performance hardware over MIDI.
When a MIDI device with "Kadabra" in its name is connected:

- Adding a new channel automatically assigns it to the next free MIDI
  channel (1–16) on the Kadabra device, so a fresh rig is ready to play
  without manual MIDI routing.
- Quitting KPlayer silently saves a recovery snapshot of your current
  session, and the next launch (with Kadabra still connected) silently
  reloads it automatically — so a Kadabra-driven quit/relaunch (for
  example, as part of a show or set change) picks up right where you left
  off, with no save/discard prompt in the way. If no recovery snapshot
  exists yet, KPlayer falls back to loading a factory "starter" session
  the first time.
- A set of transport, recording, tempo, and session commands are available
  as MIDI Control Change (CC) messages on the Kadabra connection, so they
  can be triggered from the hardware itself rather than the mouse. These
  are only recognized on **MIDI channel 16** of the Kadabra device's own
  port:

  | CC# | Command | Value |
  |----:|---------|-------|
  | 102 | Record | ≥64 starts/arms recording, <64 stops — mirrors the Record button |
  | 105 | Play / Pause | ≥64 plays, <64 pauses — mirrors the Play/Pause button |
  | 104 | Master Arm | ≥64 arms the master bus, <64 disarms it |
  | 100 | Tempo step | ≥64 nudges tempo up 1 BPM, <64 nudges it down 1 BPM (ignored while Tempo Sync is following an external clock) |
  | 9 | Save / Save As | nonzero saves to the current file, 0 opens Save As |
  | 3 | Open Session… | any value opens the session picker |
  | 99 | Open Starter Session | value 0 loads the factory starter session |
  | 6 | Quit | value 0 quits KPlayer, same as the Quit menu item |

  Each is level-based (like a button, not a knob) and debounced, so a
  controller that continuously re-sends the same value won't repeatedly
  re-trigger the action.

None of this affects KPlayer when no Kadabra device is connected — the
app behaves exactly as described in the rest of this guide.

## 19. Troubleshooting

**The plugin browser won't open / a slot won't respond to clicks.**
The plugin scan probably hasn't finished yet — check for the "Scanning
plugins…" overlay. On a large plugin collection this can take a while;
wait for it to complete.

**A plugin I installed isn't showing up in the browser.**
Open Settings and click **Rescan Plugins** — KPlayer only picks up new
installs when it scans, either at launch or via this button.

**A plugin shows up under "Failed to Load."**
The plugin itself failed KPlayer's scan (a corrupted install, a
crash during scanning, or an incompatible build). Try reinstalling the
plugin, or use the retry option next to its entry in that section.

**My MIDI device or Audio In selection turned orange.**
This means the channel is pointed at a MIDI device or a recorded Take that
KPlayer can't currently find — a device that's unplugged, or a Take file
that's been moved/deleted. Hover over the selector for a tooltip explaining
which. The routing itself isn't cleared automatically, in case the device
reconnects.

**Recording won't start.**
Make sure at least one channel or the master bus is armed, and that a
recordings folder is set (KPlayer will prompt you for one automatically
the first time if not).

**A session saved on my other machine loaded with missing plugins.**
Confirm the same plugin (same format — VST3 vs. Audio Unit matters, see
[Cross-Platform Notes](#17-cross-platform-notes)) is actually installed
on this machine, then try **Rescan Plugins** in Settings before reloading
the session — KPlayer can only relink to plugins its scan already knows
about.

## 20. Keyboard Shortcuts and Menu Reference

| Action | Shortcut | Menu |
|---|---|---|
| Open Session… | Cmd/Ctrl+O | File |
| Save Session | Cmd/Ctrl+S | File |
| Save Session As… | Cmd/Ctrl+Shift+S | File |
| Import Audio to Track… | — | File |
| Panic (all notes off) | Cmd/Ctrl+. | File |
| Settings… | — | File |
| Quit | Cmd/Ctrl+Q | File |
| About | — | Help |
| Help (opens online help in your browser) | — | Help |

---

*This guide covers Kadabra KPlayer's features as currently shipped. Some
capabilities described in internal design documents (e.g. a Kadabra MIDI
SysEx protocol for tighter OS/Player integration) are still in development
and aren't part of the app yet.*
