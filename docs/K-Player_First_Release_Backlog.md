# K-Player — First Shipping Release Backlog

**Status:** Working backlog, post-MVP. K-Player has since passed the
release-ready bar as a public beta prerelease (v0.9.4, distributed through
Tribal-Tools' Installer — see `imi-common-docs/strategy/master-backlog.md`
§1.3/§7); this backlog now tracks quality/functionality refinement toward
the broader IMI Free release, not the original "first release to beta
testers" milestone.
**Context:** MVP functional prototype is complete. This backlog covers refinements toward the first release to beta testers, sequenced in incremental, independently-shippable stages.

**Priority principle (2026-08-07):** this release serves Kadabra owners
first. Ship the minimal set of highest-value, most-robust features for that
audience before chasing broader subscriber-acquisition/PR value — the free
release hopefully becomes a subscriber magnet and a PR aid too, but that's
a hoped-for side effect, not what decides what goes in. Concretely: prefer
targeted robustness/stability work (see the delta session load and Panic/
scan-bulletproofing work already shipped) over bigger architecture bets
whose payoff is speculative or aimed at features Kadabra owners aren't
asking for yet.

---

## Increment 0 — Session format versioning & migration — DONE (`31120ea`)

Ahead of everything else, since Increment 3 below adds new fields to the session schema — those additions should be the first real migration, not more unversioned drift. Full spec: `docs/K-Player_Session_Format_Versioning_Spec_Increment0.md`.

1. `formatVersion` field on all session files, migration-chain scaffolding (`SessionMigrator`) — done: `Source/SessionMigrator.h/.cpp`, `migrate_v0_to_v1` for every pre-spec file.
2. Tolerant parsing of unknown fields; graceful handling of newer-file-than-app — done: `Source/SessionFormat.h/.cpp` (`extractExtraFields`/`mergeExtraFields`), "Newer Session Format" alert in `SessionIO::loadSession`, no silent downgrade on save.
3. Backup-on-save (`.kplayer.bak`) — done: `SessionFormat::backupExistingFile`.
4. Fixture-based regression tests per format version — done: first test infra in the repo (`IMI_KPlayer_Tests`, a `juce_core`-only console app), fixtures in `test/fixtures/session-formats/`.

Verified both by the automated test suite and by driving the built app live (fresh save writes `formatVersion: 1`; second save creates `.kplayer.bak`; opening/re-saving a hand-edited `formatVersion: 999` file with an unknown field shows the warning and doesn't downgrade or drop the field).

## Increment 1 — Session safety — DONE (`733e889`)

1. Dirty-state `*` indicator next to session name — done, but **scoped to structural changes only** (channel/insert/routing/plugin load-replace-unload-bypass, master volume, tempo); plugin-internal parameter edits (turning a knob inside a hosted plugin's own UI) do **not** mark the session dirty — that would need a new `AudioProcessorListener`/`AsyncUpdater` per loaded plugin instance, deliberately deferred as a separate chunk of work. Clears on save. Implemented in `Main.cpp` (`MainWindow::markDirty/clearDirty/updateWindowTitle`), wired down through `MainComponent`/`ChannelComponent` via an `onDirty` callback.
2. Cmd/Ctrl+S save, wired to the same dirty flag — done.
3. Destructive-action confirmation guards:
   - Reducing channel count when a channel above the new count has a loaded plugin — **deferred to Increment 2**, since there's no channel-count resize UI yet to guard.
   - Unloading/replacing a plugin in a slot — done: OK/Cancel confirmation via `AlertWindow::showAsync` in `ChannelComponent::showPluginSlotMenu`.

Verified live in the running app (mute toggle → title shows `*`; Save As → title updates to the new name with `*` cleared; plugin load/remove confirmed by the user directly).

**Bug found and fixed during verification (unrelated to dirty-tracking design, but surfaced by it):** `gainSlider`, `panSlider`, `masterVolumeSlider`, and `midiChannelBox` all called `setValue`/`setSelectedId` without `dontSendNotification` *before* their listener was attached at construction time. Since the default notification type is async, the listener callback still fired moments later (after being wired) — harmless before, but it falsely marked a brand-new session dirty once dirty-tracking existed. Fixed at all four sites to use `dontSendNotification`, matching the pattern already used correctly elsewhere (e.g. `midiDeviceBox`).

*(Revert-to-Saved was considered and dropped — redundant with re-loading the current session file.)*

## Increment 2 — Channel count finish line — DONE (`850d2da`)

4. Fix hardcoded property blocking full range; cap raised to **24** channels — done: `MainComponent::maxChannels` (was `numChannels = 12`, a fixed construction-time constant with no resize path at all). `MainComponent::defaultChannelCount` (12) is the initial rack size on a blank session.
5. Bulk resize via settings dialog — done: `MainComponent::setChannelCount()` rebuilds the channel vectors, briefly detaching the audio callback for the duration (per spec, acceptable); existing channels 1..N preserved on grow (new channels are prepared with the current sample rate/block size/tempo immediately, since the device won't fire `audioDeviceAboutToStart` again); confirm-then-truncate on shrink via the same OK/Cancel `AlertWindow::showAsync` pattern as Increment 1's plugin-slot guards, only when a channel above the new count has a loaded plugin. Also fixed a related latent bug surfaced by this work: `SessionIO::loadSession` used to silently truncate any saved session wider than the rack's *current* size instead of resizing to fit (clamped to `maxChannels`) — a loaded session now fully defines the channel count again, matching how tempo/master volume already behave.

**Also done this round (user request, not originally scoped):** renamed the "Preferences" menu/dialog to **"Settings"** throughout — `PreferencesComponent` → `SettingsComponent` (`Source/SettingsComponent.{h,cpp}`), menu bar entry, dialog title, command info strings, and the MVP spec doc. Same functionality, no behavior change.

## Increment 3 — Audio/MIDI functional features — DONE (`7213d2a`)

6. Peak meters per channel + master bus, with clip indicators — done: `Source/PeakMeterComponent.h/.cpp`, wired into `ChannelComponent`/`MasterChainComponent` via lock-free atomics owned by `ChannelProcessor`/`MainComponent`, read on a message-thread `Timer`.
   - Post-fader metering, instant attack / ~20dB-per-second decaying release; clip LED latches red on 0dBFS overshoot, clears on click or after ~1.5s with no further clips.
7. Master bus insert chain — done: `Source/MasterChainProcessor.h/.cpp` + `Source/MasterChainComponent.h/.cpp`, applied once to the post-sum stereo signal (channels → master chain → master volume → meter). `masterChain` added to the session schema via `formatVersion` 2 (`migrate_v1_to_v2`, first real migration since Increment 0). Shares the `KPlayerAudioPlayHead` concept channels already use, extracted to `Source/KPlayerAudioPlayHead.h` for reuse (along with `Source/PluginEditorWindow.h`).
   - **User-requested follow-up polish (same round):** master output fader integrated directly below the master insert slots rather than in a separate column, flanked by independent left/right peak meters (own clip flags each, to avoid a shared-atomic race); fader cap graphics reworked (`Source/ConsoleFaderLookAndFeel.h`, extracted from `ChannelComponent`) — gain caps 2x taller, pan caps 2x narrower, master cap 1.5x the channel gain cap, 50%-transparent fill with a solid outline; fixed cap clipping/text-box overlap at slider extremes by insetting `getSliderLayout()`'s bounds (affects both drawing and mouse-to-value mapping).
8. Audio input selector per channel — done: `ChannelComponent` gained an "Audio In" dropdown directly below slot 0, keyed by index into the device's *active* input channels (via a new `AudioDeviceManager&` reference + `ChangeListener`). `ChannelProcessor::processBlock` no longer clears the buffer when slot 0 is empty *and* an audio input is assigned, so live-audio-no-instrument passthrough works, not just vocoding-through-an-instrument. `ChannelProcessor::loadPlugin` now always enables slot 0's stereo input bus (was disabled for instruments) — safe for zero-input-bus plugins like K-Sampler (the `if (layout.inputBuses.size() > 0)` guard already skips them). Per-channel `audioInputChannel` field added to the session schema (settled as a per-channel int, not the top-level `audioInputs` array the v1→v2 migration reserved as a placeholder — that array is left inert/unused in the schema).
   - Required requesting real input channels from the device (`Main.cpp`: `initialiseWithDefaultDevices(2, 2)`, was `(0, 2)`) and exposing input channel selection in Settings (`AudioDeviceSelectorComponent` max input channels 0→8), plus adding the missing `NSMicrophoneUsageDescription`/`MICROPHONE_PERMISSION_ENABLED` to `CMakeLists.txt` — without it macOS silently denied all audio input at the OS level (no crash, just silence, including in JUCE's own built-in input meter).
   - Verified live by the user, including instrument-vocoding via a real vocoder plugin (Waves Morphoder) — one false alarm during testing (raw+processed signal audible together) turned out to be the audio interface's own hardware direct-monitoring feature, not a K-Player bug.

## Increment 4 — Plugin selection dialog usability — DONE (`87ad64b`)

Reviewed against the actual code before implementing (not just the backlog wording) — item 9 turned out to already be built; item 11's real gap was narrower than "consistency across slots" implied; items 12/13 from Increment 5 were pulled forward here at the user's request since they're higher-value and touch the same `PluginBrowserComponent` redesign.

9. Search/type-ahead filter — **already existed**, no work needed (`PluginBrowserComponent`'s `searchBox`, live-filters by name/manufacturer).
10. Failed-to-load / blacklisted plugin handling — done: `juce::KnownPluginList` already tracks this (`getBlacklistedFiles()`/`scanAndAddFile()`, populated automatically by `PluginDirectoryScanner`'s crash recovery) — just wasn't surfaced. Now shown as a "Failed to Load" section in the browser; double-click/Enter retries via `scanAndAddFile()` after a confirm dialog warning it may crash the app (since that's typically why it was blacklisted).
11. Replace-in-place — turned out to already be identical between channel and master-chain slots (same unload-then-load order, same confirm dialog). The real gap: if the *replacement* plugin failed to load, the old one was already unloaded by then, silently leaving the slot empty with no indication. Fixed with a "Replace Failed" alert in both `MainComponent::showPluginBrowser`/`showMasterChainPluginBrowser`.
- **Pulled forward from Increment 5 (user request, higher priority than blacklist visibility):** favorites (persistent, `PluginManager::isFavorite/setFavorite`, toggled via a star glyph on each row) and recently-used (last 10, `PluginManager::noteRecentlyUsed`, recorded only on successful load not mere selection) each get their own section at the top of the browser; a sort-mode button toggles the "All Plugins" section between A-Z and grouped-by-manufacturer. All backed by a single unified row list (`PluginBrowserComponent::Row`, mixing section headers/plugin rows/blacklisted rows) rather than several fixed sub-lists. Favorites/recently-used persist to small text files alongside the existing plugin cache (`plugin_favorites.txt`/`plugin_recent.txt`).

## Increment 5 — Polish / deferred

12. ~~Manufacturer/category grouping in plugin browser~~ — done, pulled into Increment 4 above.
13. ~~Favorites / recently-used~~ — done, pulled into Increment 4 above.
14. Incremental hot channel add/remove (menu item + keyboard shortcut) — deferred pending audio-graph work to support live topology changes without disturbing other channels ("channel autonomy": today's `MainComponent::setChannelCount()` detaches the whole audio callback for *any* resize, glitching every channel, not just the one changing — see item's discussion below).
15. Audio-to-MIDI conversion — deferred, explicitly out of scope for this release; likely a future plugin rather than a host feature
16. Full structural undo/redo — deferred; confirmation guards (Increment 1) plus this backlog's existing safety nets may be sufficient, revisit if beta feedback asks for it specifically

**Channel autonomy — discussed and deliberately held (2026-08-07):** real
benefits identified (no cross-channel glitch on add/remove, unblocks item
14 above, protects in-progress recordings on unrelated channels during a
resize, closes a remaining gap in Increment 6's delta load for
different-channel-count session switches) against a real cost (the clean
fix pre-allocates all `maxChannels` worth of channel state up front rather
than growing/shrinking a vector, a real memory/object-count tradeoff, not
free). Per the priority principle above: delta session load (Increment 6)
already delivers the main live-reliability win this was chasing for the
common case (same-channel-count song switching); channel autonomy is a
bigger architecture bet for a narrower remaining gap. Deferred, not
abandoned — revisit once the core experience is solid.

**Aux bus / further channel routing (e.g. sidechain) — same 2026-08-07
discussion, same disposition:** confirmed *not* a prerequisite-or-enabled-by
relationship with channel autonomy — one's about container lifecycle, the
other's about signal-graph shape, orthogonal concerns. Sidechain
specifically is already listed below as out of scope, and is actively
harder than "just build it": `ChannelProcessor::loadPlugin()` deliberately
calls `disableNonMainBuses()` (root cause of an earlier real crash, see
`project_kplayer_hise_crash_fixes` memory), which currently blocks exactly
the kind of extra input bus a sidechain tap would need.

## Increment 6 — Live-use robustness & flow — DONE (`4d0b89c`, `a7b35d1`, `e593929`)

Not originally scoped as a numbered increment — grew out of live testing
and direct requests in one continuous working session (2026-08-07), driven
by the priority principle above (robustness for Kadabra owners over new
surface area).

- **Panic** (all-notes-off/all-sound-off) — injected into every loaded
  instrument on the audio thread, regardless of a channel's own MIDI
  routing; fixes stuck notes left behind by an external MIDI source
  crashing mid-performance (the original trigger: a Kadabra OS crash).
- **Rescan Plugins** in Settings, reusing the exact startup scan path.
- **Scan bulletproofing**: crash-skip notification (dead-man's-pedal
  snapshot surfaced once, not repeated indefinitely — a real bug in the
  first pass, fixed same session), live scanning-plugin-name + progress
  display (fixed a real name-lag bug found via live testing — JUCE's
  `scanNextFile()` only reports the name *after* the slow work, not
  before), and Rescan now actually removes plugins that were uninstalled
  (`AudioPluginFormatManager::doesPluginStillExist`), not just adds new ones.
- **Master strip / Global section split**: `MasterChainComponent` trimmed
  to inserts/fader/ARM only; new `GlobalSectionComponent` (rightmost strip)
  holds branding, channel count (+/− box, replacing the old Settings
  slider), Settings access, I/O collapse, tempo/sync, transport, Panic, and
  the Work/Show toggle below.
- **Record Ready**: REC is now a 3-state idle→armed(blinking)→recording
  button instead of immediate start/stop — arm, then the next Play starts
  recording (or immediately, if already playing); stopping recording
  leaves playback running.
- **Delta session load** (Part B of
  `KPlayer_Session_Save_Load_Design_2026-07-25.md`, re-reviewed against
  the current code before implementing, root cause reconfirmed unchanged):
  matching plugin+state slots skip the destroy+recreate path entirely
  (`ChannelProcessor`/`MasterChainProcessor::updatePluginState()`),
  turning same-rig song-to-song switching from ~1.6s/populated slot into
  tens of milliseconds. Device reinit also now skipped when unchanged.
- **Work Mode / Show Mode**: delta load being genuinely fast surfaced a
  real flow problem — live MIDI-driven parameter dirtying (deliberate,
  existing behavior) meant every song-switch still hit a Save/Discard/
  Cancel prompt. Show Mode (Global section toggle, muted green) skips that
  prompt for session loads only; Quit is untouched either way, already
  covered by the existing Kadabra-connected silent-recovery-save path.

---

## Explicitly out of scope for this release

- Sidechain audio routing to plugin instruments
- Aux bus / additional send buses (see the channel-autonomy discussion above)
- Plugin-internal parameter undo (host undo, where it exists, is structural-only)
- Audio-to-MIDI conversion
- Plugin-internal state blob forward-compatibility (owned by the plugin/JUCE, not K-Player's schema)
