# Kadabra K-Player — Release Notes

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
