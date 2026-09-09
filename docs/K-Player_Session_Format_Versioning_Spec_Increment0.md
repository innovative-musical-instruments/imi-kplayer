# K-Player Session Format Versioning & Migration — Spec (Increment 0)

**Status:** Draft for implementation
**Priority:** Increment 0 — ahead of master-chain and audio-input work, since those changes should be the first real migration rather than an unversioned drift.
**Owner context:** imi-kplayer, `.kplayer` session file format.

## 1. Motivation

Beta testers will create and share **Muses** — playable Kadabra session presets, ready to load and customize. As K-Player's session schema grows (master bus inserts, audio input routing, future features), older Muse files must keep loading correctly. This spec defines a versioning and migration system so schema changes never silently break previously saved or distributed sessions.

Two separate risk surfaces exist, and this spec addresses only the first:

1. **Host schema** (channel list, insert-slot assignments, channel count, routing, future master-chain/audio-input fields) — fully owned by K-Player. **This is what this spec versions.**
2. **Per-plugin state blobs** (VST3/AU chunks, HISE XML state) — opaque data K-Player stores and hands back to the plugin. Their forward-compatibility is owned by the plugin itself, not by K-Player. Out of scope here, but worth remembering as a distinct failure mode.

## 2. Goals

- A newer app can always tell what shape an older file is in, and upgrade it in memory before use.
- An older app opening a newer file degrades gracefully rather than crashing.
- Every schema change is captured as a small, testable migration step.
- Testers' Muse files are never silently overwritten or corrupted by the act of loading.
- A cheap backup-on-save safety net, bundled in while touching the save path.

## 3. Top-level file shape

Add a `formatVersion` integer as the **first field** in the top-level JSON object. It versions the **structure**, not the app release — it only increments when the JSON shape itself changes (a field is added, renamed, removed, or restructured), never on every app version bump. (JSON object key order isn't semantically meaningful and JUCE's writer doesn't guarantee it — "first field" is a human-readability convention for anyone hand-inspecting a file, not something the load path depends on.)

Today's actual on-disk shape (`SessionIO.cpp`) is: `version` (string, `"1.0"`, cosmetic — written but never read back on load), `appVersion`, `createdAt`, `sessionName`, `audioDeviceStateXml`, `masterVolume`, `tempo`, `channels`. **Version 1** is defined as exactly this shape, with the cosmetic `version` string dropped and replaced by the new `formatVersion` integer — no other structural change:

```json
{
  "formatVersion": 1,
  "appVersion": "0.9.2",
  "createdAt": "...",
  "sessionName": "My Session",
  "audioDeviceStateXml": "...",
  "masterVolume": 1.0,
  "tempo": 120.0,
  "channels": [ ... ]
}
```

- `formatVersion` — required, drives migration. Absence is treated as `0` (this is what every real `.kplayer` file written before this spec looks like — see §4.1's `migrate_v0_to_v1`, not a hypothetical).
- `appVersion` — informational only, string, for diagnostics/support. Never used for load-path decisions.

`channelCount`, `masterChain`, and `audioInputs` do **not** exist yet — they're illustrative of the *v2* shape Increment 3 will introduce (§8), shown here only to preview where `formatVersion` bumps next, not as part of v1.

## 4. Migration system design

### 4.1 Structure

A `SessionMigrator` component owns an ordered sequence of migration steps, each responsible for transforming the raw JSON from version `N` to version `N+1`:

```cpp
class SessionMigrator
{
public:
    // Returns the JSON upgraded to kCurrentFormatVersion, or an error.
    static juce::Result migrate (juce::var& sessionJson);

private:
    // One entry per version bump. Each function assumes it receives
    // valid (N)-shaped JSON and returns valid (N+1)-shaped JSON.
    using MigrationStep = std::function<juce::var (const juce::var&)>;

    static const std::vector<MigrationStep>& getMigrationChain();

    static juce::var migrate_v0_to_v1 (const juce::var& v0Json);
    static juce::var migrate_v1_to_v2 (const juce::var& v1Json);
    // static juce::var migrate_v2_to_v3 (const juce::var& v2Json);
    // ... appended over time, never edited retroactively
};

static constexpr int kCurrentFormatVersion = 1; // bump when a new migration is appended
```

Unlike the other steps in the chain, `migrate_v0_to_v1` isn't future scaffolding — it ships *in* Increment 0, because every `.kplayer` file that exists today is an unversioned "v0" file per the absence rule in §3. Without it, real existing Muses would hit `fileVersion == 0 < kCurrentFormatVersion`, fall into the migrate branch of §4.3, and find no step in the chain that accepts a v0 input — the whole point of this spec.

```cpp
juce::var SessionMigrator::migrate_v0_to_v1 (const juce::var& v0Json)
{
    auto upgraded = v0Json.clone();
    auto* obj = upgraded.getDynamicObject();

    obj->removeProperty ("version");          // superseded, cosmetic, never read
    obj->setProperty ("formatVersion", 1);     // structural shape is otherwise unchanged
    return upgraded;
}
```

### 4.2 Rules for writing a migration step

- **Never edit a shipped migration function.** Once `migrate_vN_to_vN+1` has shipped in a release, it is frozen. Fixes to a migration's logic are done via a *new* migration step, never by rewriting history — testers may have files that already round-tripped through the old version.
- **Each step does the minimum required transform.** E.g., v1→v2 (adding `masterChain` for Increment 3) should just insert an empty `masterChain: []` if absent — nothing else.
- **Each step is pure and synchronous.** No I/O, no plugin instantiation, no side effects — just JSON-to-JSON. This keeps them trivially unit-testable.
- **Migrations run sequentially from the file's `formatVersion` to `kCurrentFormatVersion`.** A v1 file run through v1→v2 and v2→v3 in order, never jumping straight to the latest shape.

### 4.3 Load-path pseudocode

```cpp
juce::Result loadSession (const juce::File& file, SessionState& outState)
{
    auto parseResult = juce::JSON::parse (file);
    if (! parseResult.isObject())
        return juce::Result::fail ("Could not parse session file (not valid JSON)");

    int fileVersion = parseResult.getProperty ("formatVersion", 0);

    if (fileVersion > kCurrentFormatVersion)
    {
        // Newer file than this app understands. Attempt best-effort load,
        // but tell the user plainly rather than mangling silently.
        notifyUser ("This Muse was created with a newer version of K-Player. "
                    "Some features may not load correctly.");
        // Do NOT run migrations backward. Attempt to parse using the
        // current schema's tolerant reader (see 4.4) and accept partial load.
    }
    else if (fileVersion < kCurrentFormatVersion)
    {
        auto migrateResult = SessionMigrator::migrate (parseResult);
        if (migrateResult.failed())
            return migrateResult;
    }

    return parseSessionState (parseResult, outState); // tolerant parse, see 4.4
}
```

### 4.4 Tolerant parsing of unknown fields

The final parse step (JSON → runtime `SessionState` objects) must:

- Read known fields by name, applying sensible defaults if absent (already implicitly required for the v1→v2 case: `masterChain` defaults to empty).
- **Never fail the whole parse because of an unrecognized field.** Extra/unknown keys are ignored, not treated as errors. This is what makes "older app opens a slightly-newer file" degrade gracefully instead of crashing.
- Log ignored/unknown fields at a debug level only — not surfaced to the user unless load meaningfully fails.
- **Preserve unrecognized top-level fields, don't just discard them.** `SessionState` gains an opaque `juce::var extraFields` bag: any top-level key the parser doesn't recognize is copied into it verbatim instead of being dropped. This is the difference between "ignore" (skip when reading) and "forget" (also lose it on the next write) — see §5 for why forgetting is the actual risk.

### 4.5 Newer-file-than-app handling

Already reflected in the pseudocode above: detect `fileVersion > kCurrentFormatVersion`, show a plain, non-alarming message, and attempt a best-effort load via the tolerant parser rather than refusing outright. A partially-working Muse beats a hard failure for a beta tester.

**This path must not silently downgrade the file on the next save.** Without care, the sequence "open a newer Muse in an older app → don't even touch anything → hit Save" would overwrite `formatVersion` down to `kCurrentFormatVersion` and drop everything the old app didn't understand (the very corruption this spec exists to prevent — see §5, rule 1a). Concretely: when `fileVersion > kCurrentFormatVersion` at load time, remember the file's original `formatVersion` on `SessionState` (alongside the `extraFields` bag from §4.4) and use it at save time instead of unconditionally stamping the current version.

## 5. Save-path changes

1. Write `formatVersion` and `appVersion` at save time as follows:
   - **1a. Normal case** (file was loaded at `fileVersion <= kCurrentFormatVersion` and fully migrated in memory): write `formatVersion = kCurrentFormatVersion` — the saved copy is upgraded to current, as before.
   - **1b. Newer-than-supported case** (file was loaded at `fileVersion > kCurrentFormatVersion`, per §4.5): write back the file's *original* `formatVersion`, not the app's current one, and merge the app's edits to known fields with the preserved `extraFields` bag (§4.4). The old app never fully understood this file, so it has no business declaring it downgraded to a version it does understand — that would silently destroy the newer fields it couldn't parse. This is the one case where the two-file-version scenario (older app, newer Muse) can lose no data purely from a load-edit-save round trip.
2. **Do not silently overwrite a loaded Muse file with an upgraded format on load alone.** The in-memory upgrade only touches disk when the user explicitly saves (Cmd/Ctrl+S or Save As). Today there's no autosave and no dirty-state tracking in `Main.cpp` (`saveSession()`/`saveSessionAs()` are the only write paths), so this already holds without depending on any separate dirty-state feature — just don't introduce an autosave path that violates it later.
3. **Backup-on-save:** before overwriting an existing `.kplayer` file, copy the current on-disk version to `<filename>.kplayer.bak` (single rolling backup is sufficient for now — no need for a full backup history at this stage). Cheap, unrelated-but-complementary safety net. If the backup copy fails (disk full, permissions), log it but proceed with the primary save — a missing backup shouldn't block the user from saving their work.

## 6. Regression/fixture testing plan

**Prerequisite — no test harness exists yet.** There's no `test/`/`tests/` directory, no Catch2/gtest, and no JUCE `UnitTest` runner anywhere in this repo today; this increment is what introduces one. Since the project already links JUCE, `juce::UnitTest` + `juce::UnitTestRunner` (wired into a small `juce_add_console_app` CMake target, or a `--run-tests` flag on the existing app target) avoids pulling in a new external dependency — but standing this up is real scope for Increment 0, not just "drop a fixture file in a folder."

- Maintain a `test/fixtures/session-formats/` directory in the repo with one sample `.kplayer` file per historical `formatVersion` (starting with a v0 fixture — a real pre-spec file with the legacy `"version": "1.0"` string and no `formatVersion` — and a v1 fixture, the moment this ships).
- A test suite loads every fixture through `SessionMigrator` + the full load path and asserts:
  - Load succeeds without error.
  - Expected fields are present with expected values after migration.
- **Every time the schema changes (i.e., a new migration step is added), a new fixture is added for the new version, and all prior fixtures must still pass.** This is the mechanism that actually delivers on "we keep testers' work valid" — without it, versioning is good intent that quietly rots under deadline pressure.

## 7. Acceptance criteria for Increment 0

- [ ] `formatVersion` field present in all newly saved session files, defaulted to `1` for the current schema shape.
- [ ] `SessionMigrator` scaffolding in place with a migration chain containing `migrate_v0_to_v1` (real, not a no-op — drops the legacy `version` string, adds `formatVersion: 1`) and nothing further to migrate yet at v1.
- [ ] A real pre-spec fixture (legacy `"version": "1.0"`, no `formatVersion`) loads correctly and is upgraded to v1 in memory via `migrate_v0_to_v1`.
- [ ] Tolerant parsing confirmed: a hand-edited file with an injected unknown top-level field still loads correctly.
- [ ] Unknown top-level fields survive a load → save round trip unchanged (verifies the `extraFields` passthrough from §4.4, not just that load doesn't crash).
- [ ] Newer-than-supported-version file produces the user-facing notice, attempts best-effort load (can be tested by hand-editing `formatVersion` to `999` in a fixture), and — critically — a load → save round trip with no edits does **not** downgrade `formatVersion` or drop the fields the app didn't understand (§4.5/§5 rule 1b).
- [ ] Backup-on-save (`.kplayer.bak`) implemented and verified, including that a failed backup write doesn't block the primary save.
- [ ] No silent overwrite of a loaded file's on-disk copy prior to an explicit save.
- [ ] `test/fixtures/session-formats/v0-sample.kplayer` and `v1-sample.kplayer` committed, each with a passing load test, run via a new minimal JUCE `UnitTest`-based test target (see §6 prerequisite).

## 8. Worked example — the first real migration (v1 → v2)

When Increment 3 lands (master bus inserts, audio input routing), this becomes the first real migration, exercising the system end to end:

```cpp
juce::var SessionMigrator::migrate_v1_to_v2 (const juce::var& v1Json)
{
    auto upgraded = v1Json.clone(); // or equivalent DynamicObject copy

    if (! upgraded.hasProperty ("masterChain"))
        upgraded.getDynamicObject()->setProperty ("masterChain", juce::var (juce::Array<juce::var>()));

    if (! upgraded.hasProperty ("audioInputs"))
        upgraded.getDynamicObject()->setProperty ("audioInputs", juce::var (juce::Array<juce::var>()));

    upgraded.getDynamicObject()->setProperty ("formatVersion", 2);
    return upgraded;
}
```

At that point: bump `kCurrentFormatVersion` to `2`, append this step to `getMigrationChain()`, and add a `v2-sample.kplayer` fixture alongside the existing v1 one.

## 9. Explicitly out of scope for this increment

- Plugin-internal state blob forward-compatibility (owned by plugin/JUCE, not K-Player).
- Any attempt to migrate backward (newer file, older app) beyond tolerant best-effort parsing.
- Multi-level backup history (only a single rolling `.bak` for now).
