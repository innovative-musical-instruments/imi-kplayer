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

## Increment 2 — Channel count finish line

4. Fix hardcoded property blocking full range; cap raised to **18** channels
5. Bulk resize via settings dialog: engine rebuild with brief interruption is acceptable; existing channels 1..N preserved on grow; confirm-then-truncate on shrink

## Increment 3 — Audio/MIDI functional features

6. Peak meters per channel + master bus, with clip indicators
   - Post-insert-chain metering, lock-free atomic reporting from audio thread to UI
   - Standard ballistics: instant attack, decaying release; clip latch with manual or timed auto-clear
7. Master bus insert chain — reuse the existing channel insert-slot component, applied once to the stereo sum; needs `masterChain` in session schema (first real use of Increment 0's migration system) and the same transport context (`KPlayerAudioPlayHead`) channels already get
8. Audio input selector per channel, slot-0-adjacent, matching the visual language of a plugin slot
   - Selected hardware input feeds into the channel's signal path as a normal audio input, alongside existing MIDI input
   - Covers both live vocals/mixing (no instrument loaded) and vocoding (instrument loaded, e.g. Surge XT) with one implementation — no sidechain routing or bus-layout detection needed, since target plugins take audio-in directly and MIDI drives the carrier internally
   - Needs `audioInputs` in session schema

## Increment 4 — Plugin selection dialog usability

9. Search/type-ahead filter in the plugin browser
10. Failed-to-load / blacklisted plugin handling — visible state in the dialog, not silent disappearance
11. Replace-in-place behavior clarified and made consistent across slots

## Increment 5 — Polish / deferred

12. Manufacturer/category grouping in plugin browser
13. Favorites / recently-used
14. Incremental hot channel add/remove (menu item + keyboard shortcut) — deferred pending audio-graph work to support live topology changes without disturbing other channels
15. Audio-to-MIDI conversion — deferred, explicitly out of scope for this release; likely a future plugin rather than a host feature
16. Full structural undo/redo — deferred; confirmation guards (Increment 1) plus this backlog's existing safety nets may be sufficient, revisit if beta feedback asks for it specifically

---

## Explicitly out of scope for this release

- Sidechain audio routing to plugin instruments
- Plugin-internal parameter undo (host undo, where it exists, is structural-only)
- Audio-to-MIDI conversion
- Plugin-internal state blob forward-compatibility (owned by the plugin/JUCE, not K-Player's schema)
