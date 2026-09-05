# Kadabra K-Player — Release Notes

## v0.9.9 — 2026-09-05

Transport work: a Range to play and loop a section of a take, a metronome
click, and a tempo that finally takes effect when you set it.

### New Features

- **Range** — the transport now plays a bounded span of the session rather
  than advancing forever into silence past the end of the material. A
  second row above Play/Rec/RTZ carries **LOOP** (wrap at the range end
  instead of stopping there), **FULL** (temporarily play the whole of the
  recorded material while remembering your own range), the range start/end
  fields, and a capture button under each that pulls the current playhead
  into it. The range is always present whenever any channel has a Take
  selected, defaulting to the whole of the longest one, and **RTZ now
  returns to the range start** rather than to zero. Resolution is
  deliberately whole seconds: the start rounds down and the end rounds up,
  so the range always contains what you gestured at. Range points and the
  LOOP toggle persist in the session; FULL deliberately doesn't, since it
  is a temporary view of the material rather than a setting.
- **Metronome click** — a click or an unpitched noise burst generated
  inside K-Player, taking no MIDI channel and no plugin slot. It follows
  the session tempo and the transport, and is mixed in after the master
  chain and after the recording tap, so it never runs through your master
  inserts and can never end up in a recording. The **CLICK** toggle sits
  above Sync; right-click (or ctrl-click) it for sound, resolution (2/1
  through 1/8) and volume (0 to -18 dB). Settings persist in the session.
- **The transport time readout is editable** — double-click it and type a
  time to jump the playhead there, so finding a spot in a long take
  doesn't mean listening through it. Clamped into the current range, and
  disabled while recording.
- **Tempo can now be set up to 1200 BPM** — Kadabra's own sequencer runs
  well past the conventional ~300 ceiling and MIDI-clock sync has always
  followed it up there, so a tempo you could arrive at by syncing was one
  you were not allowed to type.

### Fixes

- **Changing the tempo did nothing to a MIDI Take until it was
  reselected** — a Take was converted from ticks to samples once, when it
  was selected, against whatever the tempo happened to be at that moment.
  Changing the tempo afterwards had no effect at all, and a Take selected
  after MIDI-clock sync had moved the tempo played at the wrong speed with
  nothing on screen to indicate why. Playback now converts per block
  against the live tempo, so a change is audible immediately and applies
  progressively from wherever playback has reached rather than re-timing
  what has already played. Found while testing this release: a take
  recorded at 300 BPM was playing back at 0.76x after the tempo had been
  caught from a Kadabra clock.
- **Recorded MIDI files didn't record the tempo they were played at** —
  every timestamp in a `.mid` Take is derived from the tempo at record
  time, but that number was thrown away, so the file was only
  interpretable by someone who happened to remember it and any other
  software opening it assumed 120. Each `.mid` now carries a tempo
  meta-event. Playback inside K-Player still follows the session tempo,
  so this changes nothing about how a Take sounds here.

### Notes

- Session `formatVersion` is now **5**, with one appended migration step.
  Older sessions load unchanged: they carry no range (so it spans whatever
  material their Takes turn out to be, exactly as before) and the click
  defaults to off.
- The minimum window width grows from 1132 to 1204 px to fit the Range
  controls.

## v0.9.8 — 2026-08-27

Three Master section workflow additions driven by real live-use experience,
all aimed at doing to the whole rig at once what previously required
clicking through every channel individually.

### New Features

- **Arm All** — the master ARM button is now two buttons: the same master
  arm toggle as before (now a dot glyph, matching a channel's own arm
  button, since it shares its row with the new button next to it), plus
  **All**, which arms or unarms every channel and the master together in
  one click. Arm All's own on/off look is never independently tracked - it
  always reflects whether every channel + master genuinely are all armed
  right now, recomputed after every arm change (by click or MIDI) and after
  any channel-count resize, so it can't drift out of sync with reality.
- **Master Audio In / MIDI In selectors** — two new dropdowns above the
  master inserts (following the Hide I/O toggle, same as a channel's own
  selectors) broadcast a live input/device to every channel at once, or
  bulk-clear every channel back to None. They also list **Take groups** -
  one entry per past recording pass that has at least one channel's file in
  it - and applying one assigns each channel its own recording from that
  take (skipping channels that weren't recorded in it; the master channel
  itself is never touched, since it has no input of its own). Each
  dropdown is a one-shot action, not a persisted selection - it always
  snaps back to its "Set All..." placeholder afterward rather than
  displaying a value that could misrepresent per-channel state once
  channels diverge again.
- **Byp/Act. bulk bypass/activate menu** — a new button above the master
  inserts opens a menu to bypass or activate every plugin at once, or just
  one slot position across the whole rig (every channel's slot 0-5, plus
  the master bus's own inserts for slot positions 1-5, which it shares
  with channels' insert slots 1-5 one-to-one - slot 0 is the channel-only
  instrument/effect slot, so a Slot 0 action never touches the master bus).

### Fixes

- **Master Take-group selector could silently load the wrong channel's
  recording** — in a session with 10+ channels, the new Master "Set All
  Audio/MIDI In → Take group" action matched each channel's file with a
  wildcard that could also match a different channel whose number starts
  with the same digits (e.g. channel 1's pattern also matching channel
  10/12/19...), so the wrong recording could get assigned with no error
  shown. Caught in code review and fixed before this build shipped.

### Notes

- All three were verified against a real multi-channel session with loaded
  instruments and recorded Takes. Along the way, confirmed that a Debug
  config build can fail to play back a heavy session at all (frozen
  transport, silent, no crash) purely from not keeping up with the DSP
  load - the identical code played correctly once built as Release. Not a
  bug in this release; worth remembering next time playback looks broken
  while testing a Debug build specifically.

## v0.9.7 — 2026-08-16

A small, targeted fix for a live-use annoyance, plus a crash fix found
while testing it.

### Fixes

- **Audio device no longer resets on launch** — K-Player always opened on
  whatever CoreAudio considered the system default device (e.g. a
  MacBook's built-in speakers), regardless of what interface was actually
  in use last (e.g. a Focusrite Scarlett), unless a loaded session
  happened to carry a matching saved device state. Now records the
  device/sample-rate/buffer-size setup independently of any session file
  and restores it on the next launch, reconnecting to the same interface
  if it's available and falling back to the normal default-device pick
  only if it isn't.
- **Fixed a crash when changing sample rate in Settings** — with input and
  output set to two different physical devices, changing the sample rate
  could crash K-Player outright (`SIGBUS`/`EXC_BAD_ACCESS`). Root cause was
  a use-after-free race in the JUCE 8 CoreAudio backend: a device-changed
  notification from CoreAudio could arrive on a background thread just
  after K-Player had already started tearing the audio device down for
  the sample-rate change, so it ended up acting on memory that was already
  freed. Fixed by moving to JUCE 9.0.1, which rewrote this part of the
  macOS audio backend to guard against exactly this. Confirmed with a real
  crash report: the same steps that crashed instantly on the old build no
  longer reproduce at all on this one.

## v0.9.6 — 2026-08-12

A live-performance and polish pass: insert-slot MIDI control for Kadabra
motion, a redesigned global control bar, brand typography, and a couple of
real bugs fixed along the way.

### New Features

- **Insert slots now receive MIDI** — previously only slot 0 (the
  instrument slot) got any MIDI at all; insert slots 1–5 were hard-wired to
  an empty buffer. Insert-slot plugins now get the same channel-filtered
  MIDI stream slot 0 already did (respecting the channel's assigned MIDI
  device/channel), so Kadabra's motion-controller CC output can now drive
  an effect's own MIDI-learned parameters in any insert slot, not just an
  instrument in slot 0. CC7/10/84–89/103 stay reserved for this app's own
  gain/pan/bypass/arm handling and aren't forwarded, so an insert's
  MIDI-learn can't collide with those.
- **New Session** — first item in the File menu (⌘N). Resets the whole rig
  back to the same blank state KPlayer starts in at launch with no Kadabra
  port connected: default channel count, no plugins anywhere, gain/pan/
  tempo/master volume/MIDI routing all back to their defaults.
- **Global bar redesign** — the control strip (branding, channel count,
  Settings, channel I/O collapse, tempo/sync, transport, Record Ready,
  Work/Show Mode, Panic) moved from a narrow vertical strip on the right to
  a horizontal bar spanning the full window width along the bottom: IMI
  logo/Channels/Settings/Hide I/O on the left, Tempo/Sync + transport
  centered, Work/Show Mode/Panic/Tribal Tools logo on the right. The window
  can no longer be resized narrower than what the bar's own content needs,
  so nothing overlaps.
- **Space Grotesk UI typeface** — the app's general UI text now uses the
  IMI brand style guide's Space Grotesk (Regular/Bold) instead of the
  platform default sans-serif, applied globally via a single LookAndFeel
  change. Azonix (the style guide's display/headline font) is unchanged -
  still About-screen-title only.
- **Spacebar play/pause** — with the main window focused, Space now
  toggles the shared transport playhead, same action as clicking Play/
  Pause. Doesn't interfere with typing in any text field or search box -
  a focused text editor always gets first claim on the keystroke.
- Window title now shows the `.kplayer` extension explicitly (e.g.
  `Untitled.kplayer`, `Performance Rig 1.kplayer *`) instead of a bare
  session name.
- Help menu now opens the correct KPlayer help page
  (innovativemusicalinstruments.com/kplayerhelp - was pointing at a
  dead link).

### Fixes

- **False "unsaved changes" right after opening a session** — some loaded
  plugins (HISE-based K-Samplers, Kontakt, Surge XT) do async post-load
  housekeeping that can fire a change notification on a message-thread
  tick shortly after a session finishes loading, which the dirty-flag
  poller then wrongly treated as a real edit - showing the unsaved-changes
  asterisk with zero actual user or Kadabra input. A 1-second post-load
  grace window now drains (without acting on) that flag; save behavior is
  untouched; live Kadabra-driven edits still dirty the session immediately,
  same as before.

## v0.9.5 — 2026-08-08

A live-use robustness pass: a reworked control layout, faster song
switching, a full MIDI control scheme for the Kadabra hardware, and several
real bugs fixed from tester reports.

### New Features

- **Master strip / Global section split** — the old single master column is
  now two: a dedicated Master strip (inserts, fader, ARM) next to the
  channel rack, and a Global section (branding, channel count, Settings,
  channel I/O collapse, tempo/sync, transport, Record Ready, Work/Show
  Mode, Panic) as its own rightmost strip.
- **Record Ready** — the REC button is now a 3-state idle → armed
  (blinking) → recording control instead of immediate start/stop. Arm it,
  and the next Play starts recording (or starts immediately if playback is
  already running); stopping recording leaves playback running.
- **Work Mode / Show Mode** — a new toggle in the Global section. Work Mode
  (default) keeps today's Save/Discard/Cancel prompt before any session
  load that would discard unsaved changes. Show Mode skips that prompt for
  session loads — mid-set song switching stays uninterrupted, on the
  understanding that it's the performer's call in the moment. Quit is
  unaffected either way; a Kadabra-connected quit still silently saves a
  recovery snapshot regardless of mode.
- **Delta session load** — switching between songs on the same rig no
  longer tears down and reloads every plugin from scratch. A slot with the
  same plugin already loaded gets its new state pushed in place instead
  (skipping the ~1s+ per-slot HISE/Kontakt safety sleeps entirely); only a
  genuinely different plugin does a full reload. Turns same-rig
  song-to-song switching from roughly 1.6s per populated slot into tens of
  milliseconds. Audio device reinitialization is now skipped too when the
  saved device configuration is unchanged.
- **Full MIDI control scheme, Kadabra port channel 16** — Record (CC102),
  Play (CC105), Master Arm (CC104), Quit (CC6=0), Tempo step ±1 BPM
  (CC100), Save/Save As (CC9), Open Session picker (CC3), load
  Starter.kplayer directly (CC99=0). Per-channel plugin bypass toggles
  (CC84–89 for slots 0–5) were already in place from earlier work.
- **Panic button** — sends all-notes-off/all-sound-off into every loaded
  instrument across all 16 MIDI channels, regardless of a channel's own
  routing. Fixes stuck notes left behind if an external source (e.g.
  Kadabra OS) crashes mid-performance.
- **Rescan Plugins** — a button in Settings that reuses the startup scan
  path on demand, rather than requiring an app restart to pick up newly
  installed plugins.
- **Live scan status** — during any plugin scan (startup or Rescan), the
  loading overlay now shows which plugin is currently being scanned and a
  progress indicator, instead of sitting on an unexplained blank screen
  during a slow scan.
- **Transport time readout** — a mm:ss clock in the Global section, between
  Tempo/Sync and the transport row. Doubles as a recording-elapsed display
  while Record Ready is active, since the two are always in lockstep.
- **Settings dialog** is now non-modal (click the Settings button again, or
  the window's own close button, to dismiss it) and lives under
  **File > Settings…** instead of its own menu bar entry.

### Fixes

- **Bluetooth MIDI pairing crash** — clicking "Bluetooth MIDI…" in Settings
  hard-crashed the app due to a missing `NSBluetoothAlwaysUsageDescription`
  entitlement; macOS aborts the process outright rather than denying
  access gracefully when that's absent. Found via a tester-supplied crash
  log.
- **Cross-platform Starter.kplayer relink** — a Mac-saved
  `Starter.kplayer` loaded empty on Windows via the automatic Kadabra
  recovery/starter load, but loaded correctly when manually reopened via
  File > Open Session. Root cause: the auto-load was running before the
  plugin scan had populated the list it needed to relink saved plugins
  against. Fixed by sequencing the auto-load to run only after the scan
  completes.
- **Multiple simultaneous instances on Windows** — a tester reported two
  K-Player processes running at once, fighting over the same MIDI devices.
  Launching K-Player while an instance is already running now brings the
  existing window to front instead of opening a second one.
- **Plugin scan robustness** — the scanning plugin's name no longer lags a
  step behind what's actually being scanned; Rescan now actually removes
  plugins that were uninstalled, not just adds new ones; a scan-crash
  notification no longer repeats indefinitely for a plugin that's already
  been skipped and marked failed.
- A handful of smaller robustness fixes from a code-review pass over this
  release's MIDI control changes: the one-shot MIDI commands (Quit, Save,
  Save As, Open Session, Open Starter) are now debounced against a stray
  burst of controller messages firing an action more than once; opening a
  file picker while one is already in flight (more reachable now via MIDI
  than when it was mouse-only) no longer yanks the first dialog out from
  under the user; a second launch arriving in the brief startup window
  before the main window exists is no longer silently dropped.
- **Open Session dialog not appearing on Windows** — in Work Mode, opening
  a session (or Save As) while there were unsaved changes and choosing
  "Discard and Continue" from the confirm prompt silently failed to show
  the file picker on Windows — no error, it just never appeared. Caused by
  starting a second native modal dialog from inside the first one's own
  teardown/callback stack; fixed by deferring to a fresh message-loop tick,
  the same technique already used for the equivalent quit-time case.
- **Cross-platform MIDI input relink** — a session with channels routed to
  "Kadabra" (or any other MIDI input) saved on one platform showed those
  channels as "None selected" (yellow warning) when opened on the other,
  same underlying cause as the plugin relink fixed for v0.9.4: the saved
  device identifier is a platform-specific string that never matches
  directly across Mac/Windows. Now falls back to matching by device name
  when the raw identifier doesn't match, same as plugin relinking already
  does — works retroactively on already-saved sessions, no re-save needed.
- **Startup loading message** — launching with Kadabra connected (auto-
  loading your last session) showed a frozen "Scanning plugins..." for the
  entire load, however long that took, even though the actual plugin scan
  had already finished in a fraction of that time. Now shows "Warming up
  and getting ready..." once scanning is genuinely done, so the message
  on screen matches what's actually happening.

### Known limitations

- An AU-only plugin (Mac-only format) scanned into a saved session can't
  be relinked on Windows, by design — AU doesn't exist there. None of the
  current HISE-based K-Samplers are AU-only in practice.
- Windows still has no signed/notarized release pipeline — a Windows
  "release" build today is a local `Release` config build, fine for
  testing but not for general distribution as-is.

---
