# K-Player: Take Recording & Playback — Spec Addition

Status: Draft for review
Relates to: `kplayer-session-format-versioning-spec.md`, `kplayer-first-release-backlog.md`

## 1. Motivation

Audio recording currently taps the signal post-insert-chain, at the mix bus. This bakes in whatever effects were active at record time, which defeats a primary reason to record: putting the instrument aside and refining the sound afterward. The fix is to record the **raw MIDI** going into the instrument as well as the audio, and let either one be selected back in as a channel's input — same input-selector UI used today for live devices and the audio interface, just with a new category of entries.

Two distinct use cases fall out of this, and they should stay conceptually separate:

- **MIDI take → input**: raw performance, replayable through any instrument, any insert chain. This is the "put the instrument aside and fix the mix" workflow.
- **Audio take → input**: a captured performance replayed back through the live-input entry point — i.e. reprocessed by whatever's currently in the insert chain, with the user free to disable slots they don't want reapplied. This is the "play-along / backing track" workflow, not a mix-refinement workflow.

## 2. Terminology

- **Take**: a single recorded artifact, either a MIDI Take or an Audio Take.
- **MIDI Take**: raw MIDI events captured from an armed channel's live MIDI input, timestamped against the session playhead.
- **Audio Take**: the existing recorded audio file (post-insert-chain), unchanged in how it's captured.
- **Input Selector**: the existing per-channel dropdown for choosing a channel's live source (MIDI device or audio interface input). Extended to also list Takes.

## 3. Tap points (corrected)

Both input selectors inject at the **same entry point their live counterparts already use**:

- MIDI Input Selector (Live Device | MIDI Take) → feeds into the instrument, pre-insert-chain, exactly like a live MIDI device does today.
- Audio Input Selector (Audio Interface Input | Audio Take) → feeds in at the pre-insert-chain audio entry point, exactly like a live audio input does today.

This means an Audio Take routed back in is fully reprocessed by the channel's current insert chain. That's intentional — it is not a bypass path. The insert chain slots can be individually disabled by the user if reprocessing isn't wanted for a given slot.

MIDI recording taps *before* the instrument (raw notes). Audio recording taps *after* the insert chain (unchanged from today). These are different points in the chain and produce artifacts suited to different jobs — this is the core of the design, not an inconsistency.

## 4. Takes are channel-owned for now (scope decision)

Original thinking had Takes as session-level assets selectable from any channel, to support "load a different instrument and hear the same performance" from a different channel. **Decision: restrict this for first release.** Each channel's input selector only lists Takes recorded by that same channel.

This keeps the list relevant without needing a take-management UI, and lets automatic naming (channel + timestamp) fully cover the naming problem rather than needing a naming/organization scheme that handles every case. Cross-channel take selection, renaming, and flexible track assignment become a future **playlist handling** increment, where a proper UI exists to manage that complexity.

This still has implications for the versioning spec — Takes are new referenceable objects even if scoped to their own channel for now — but the reference model is simpler: a channel references its own Takes, not a session-wide registry.

## 5. Storage format

- **MIDI Takes**: Standard MIDI File (SMF) recommended over a custom blob. Well-understood, tool-interoperable, and inherently forward-compatible — a good fit given Muses need to survive across versions and possibly across tools.
- **Audio Takes**: reuse the existing recorded-audio-file mechanism already in the MVP; no format change needed.

Each Take should carry minimal metadata: originating channel, capture timestamp, tempo/time-signature at capture time (for correct sync on replay), and a user-facing name.

## 6. MIDI playback engine

Selecting a MIDI Take as a channel's input requires a small transport-synced sequencer component — this is new engine work, not just a UI change:

- **Sync source**: `KPlayerAudioPlayHead`, same as existing BPM/transport handling.
- **Start/stop**: driven by session transport state, consistent with how live input behaves during transport start/stop.
- **End-of-take behavior**: stop at the end of the take (decided). Flush note-offs / stop active voices cleanly when it ends, to avoid stuck notes. Looping is deferred to a later increment.
- **Take scope**: since Takes are channel-owned for now (see §4), a channel only ever plays back its own Takes — no cross-channel simultaneous-reference case to handle at this stage.

## 7. Feedback / reentrancy

No interlock needed. Every new recording pass writes to a fresh file rather than mutating the take currently selected as input, so there's no read/write collision — at most the engine reads Take N-1 while writing Take N concurrently. This isn't a feedback loop in the live-audio sense (mic picking up its own output); nothing reads its own write target.

## 8. Session format & Muses implications

- New referenceable object types: `MIDITake`, `AudioTake` (or extend the existing audio-file reference mechanism).
- Channel state gains a reference to a selected Take ID, scoped to that channel's own Takes (see §4).
- Muses that bundle a MIDI Take could ship a playable demo performance as part of the distributed preset — worth flagging as a nice downstream capability, not a requirement for this pass.

## 9. Suggested increment breakdown

- **Increment A** — MIDI capture only: record raw MIDI in parallel with audio for every armed channel, no playback yet. Automatic naming (channel + timestamp), no take-management UI.
- **Increment B** — MIDI Input Selector gains "Recorded Take" entries (own channel's Takes only) + minimal playback engine: single-shot, stops at end of take, no loop, no scrub.
- **Increment C** — Audio Input Selector gains "Recorded Take" entries (own channel's Takes only), reusing the existing pre-insert-chain live-audio-input entry point.
- **Increment D** (deferred, future) — playlist handling: cross-channel take selection, user-controlled naming/renaming, deleting/organizing takes, looping, scrubbing.
- **Increment E** — Import Audio to Track: new "Import Audio to Track…" item under the File menu. Opens a file selection dialog, then prompts the user to assign the imported file to an existing track or a new one. One file at a time. The result is written as a copy into the audio files folder and saved as a regular Audio Take of the target track — reuses the existing Take storage/selection model, no new concept required.
  - **Accepted formats**: WAV, AIFF, FLAC, OGG Vorbis, MP3 (decode only) — JUCE's cross-platform built-in formats. Deliberately excludes platform-specific formats (CoreAudioFormat's AAC/M4A/ALAC on macOS, WindowsMediaFormat's WMA on Windows) so the accepted-file-types behavior is identical on both platforms.
  - **Bit depth**: conversion to 32-bit float is effectively free — JUCE's `AudioFormatReader` already normalizes reads to float regardless of source bit depth.
  - **Sample rate**: real work — resample to the session's sample rate if the source file doesn't match (e.g. via `LagrangeInterpolator` / `ResamplingAudioSource`) before writing the copy.
  - **Channel handling**: all tracks are stereo (K-Player doesn't store a mono/stereo track property), so there's no real "mismatch" case to branch on. A mono source file is always upmixed to stereo by duplication on import. The only cost is some wasted disk/memory for mono sources — acceptable at this scale for this increment.

## 10. Decisions (resolved)

- **Default playback behavior**: stop at the end of the take. No loop for first release.
- **Take scope**: channel-owned — a channel only lists and plays back its own Takes. Cross-channel selection and full naming control deferred to the future playlist-handling increment (Increment D).
- **Tempo mismatch handling**: out of scope for this stage. No surfacing of session-tempo-vs-capture-tempo drift to the user yet.
