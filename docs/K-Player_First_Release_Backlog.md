# K-Player — First Shipping Release Backlog

**Status:** Working backlog, post-MVP
**Context:** MVP functional prototype is complete. This backlog covers refinements toward the first release to beta testers, sequenced in incremental, independently-shippable stages.

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

## Increment 4 — Plugin selection dialog usability — DONE

Reviewed against the actual code before implementing (not just the backlog wording) — item 9 turned out to already be built; item 11's real gap was narrower than "consistency across slots" implied; items 12/13 from Increment 5 were pulled forward here at the user's request since they're higher-value and touch the same `PluginBrowserComponent` redesign.

9. Search/type-ahead filter — **already existed**, no work needed (`PluginBrowserComponent`'s `searchBox`, live-filters by name/manufacturer).
10. Failed-to-load / blacklisted plugin handling — done: `juce::KnownPluginList` already tracks this (`getBlacklistedFiles()`/`scanAndAddFile()`, populated automatically by `PluginDirectoryScanner`'s crash recovery) — just wasn't surfaced. Now shown as a "Failed to Load" section in the browser; double-click/Enter retries via `scanAndAddFile()` after a confirm dialog warning it may crash the app (since that's typically why it was blacklisted).
11. Replace-in-place — turned out to already be identical between channel and master-chain slots (same unload-then-load order, same confirm dialog). The real gap: if the *replacement* plugin failed to load, the old one was already unloaded by then, silently leaving the slot empty with no indication. Fixed with a "Replace Failed" alert in both `MainComponent::showPluginBrowser`/`showMasterChainPluginBrowser`.
- **Pulled forward from Increment 5 (user request, higher priority than blacklist visibility):** favorites (persistent, `PluginManager::isFavorite/setFavorite`, toggled via a star glyph on each row) and recently-used (last 10, `PluginManager::noteRecentlyUsed`, recorded only on successful load not mere selection) each get their own section at the top of the browser; a sort-mode button toggles the "All Plugins" section between A-Z and grouped-by-manufacturer. All backed by a single unified row list (`PluginBrowserComponent::Row`, mixing section headers/plugin rows/blacklisted rows) rather than several fixed sub-lists. Favorites/recently-used persist to small text files alongside the existing plugin cache (`plugin_favorites.txt`/`plugin_recent.txt`).

## Increment 5 — Polish / deferred

12. ~~Manufacturer/category grouping in plugin browser~~ — done, pulled into Increment 4 above.
13. ~~Favorites / recently-used~~ — done, pulled into Increment 4 above.
14. Incremental hot channel add/remove (menu item + keyboard shortcut) — deferred pending audio-graph work to support live topology changes without disturbing other channels
15. Audio-to-MIDI conversion — deferred, explicitly out of scope for this release; likely a future plugin rather than a host feature
16. Full structural undo/redo — deferred; confirmation guards (Increment 1) plus this backlog's existing safety nets may be sufficient, revisit if beta feedback asks for it specifically

---

## Explicitly out of scope for this release

- Sidechain audio routing to plugin instruments
- Plugin-internal parameter undo (host undo, where it exists, is structural-only)
- Audio-to-MIDI conversion
- Plugin-internal state blob forward-compatibility (owned by the plugin/JUCE, not K-Player's schema)
