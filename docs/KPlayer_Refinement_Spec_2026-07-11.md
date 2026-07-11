# KPlayer Refinement Spec — Session: July 11, 2026

Target repo: `innovative-musical-instruments/IMI-KPlayer` (JUCE, AGPLv3)
Purpose: implementation-ready backlog for a Claude Code session. Six items, independent unless noted.

---

## 1. Channel strip layout — resectioning

Reorganize each channel strip into three clearly delineated sections, top to bottom:

### 1.1 Input section (top)
- **Channel name** — same position as current. Add rename support: user can edit the name inline. The **channel number stays fixed and ordered** (Channel 1, 2, 3...) regardless of the custom name — number and name are separate fields, number is not editable. **Lower priority — do this last, doesn't need to land today.**
- Audio Input selector
- MIDI Input selector
- MIDI Channel selector

### 1.2 Plugins section
- Instrument selector
- Inserts

### 1.3 Mix section
- Gain
- Pan
- Mute and Solo

**Gain slider taper (new, added in review 2026-07-11):** Current channel gain slider (`ChannelComponent.cpp`, `gainSlider.setRange(-96.0, 0.0, 0.1)`) is a plain linear-dB mapping with no skew — the musically useful 0 to -12dB zone is squeezed into ~12% of the fader's travel, giving coarse control near unity.

Target curve, measured as fraction of travel `t` down from the top (0dB/unity) to the bottom (-96dB floor):

```
dB(t) = -96 × t²
```

- t = 0.25 (quarter down from top) → **-6dB**
- t = 0.5 (halfway down) → **-24dB**

This gives much finer resolution near unity gain and compresses the rarely-used low-attenuation range near the floor. Requires a custom `juce::NormalisableRange` with `convertFrom0To1`/`convertTo0To1` lambdas — JUCE's built-in `setSkewFactorFromMidPoint` is a single-parameter power curve and cannot satisfy both anchor points simultaneously.

**Also applies to the Master bus volume fader** (`MasterChainComponent.cpp`, `volumeSlider.setRange(-60.0, 6.0, 0.1)`) — same taper shape, same relative anchor fractions (t=0.25, t=0.5), scaled to its own range (+6dB top, -60dB floor):

```
dB(t) = 6 - 66 × t²
```

- t = 0.25 → ≈ **+1.9dB**
- t = 0.5 → **-10.5dB**

These master-fader values are a direct mathematical extrapolation of the same curve shape (not independently confirmed against a specific feel) — worth a quick listen/feel check once implemented, since the master range has +6dB of headroom above unity that the channel fader doesn't.

### 1.4 Selector dropdown arrows (applies to all selectors above)
- Current popup-menu arrow graphic is too wide.
- Reduce width by ~50%.
- Align closer to the right edge of the selector, with only a few pixels of padding.

**Open question for implementation:** should the custom channel name persist in saved sessions/presets? (Likely yes, but confirm before wiring up storage.)

---

## 2. Corporate branding

- Display the **IMI logo** and **Tribal Tools logo**, fixed size, side by side.
- Position: directly above the Master section.

**Assets (added in review 2026-07-11):**
- IMI logo: `docs/design/design_handoff_kplayer_about_icons/assets/imi-logo-white.png` — white-on-transparent, 792×440.
- Tribal Tools logo: `docs/design/design_handoff_kplayer_about_icons/assets/tribal-tools-logo.png` — white-on-transparent, 1170×594. Sourced from `/Volumes/Vinch2T/IMI/New Kadabra Instruments Dev/GFX/TribalLogo.jpg` (opaque JPEG, solid `#050914` background); background programmatically removed via unpremultiply-against-known-bg (clean anti-aliased edges, verified by compositing over a dark panel). Original JPEG kept alongside as `tribal-tools-logo.jpg` for provenance.

Both logos are now transparent PNGs and ready to place side by side above the Master section.

---

## 3. Remove plugin-removal warning (mostly)

- Remove the confirmation dialog currently shown when removing or replacing a plugin.
- **Keep** a confirmation dialog only for one case: shrinking the mixer (reducing channel count) in a way that would delete a channel that currently has inserts loaded.

---

## 4. Help menu (new)

Add a Help menu with two items:

1. **About** — opens the About screen (see finalized text below).
2. **Help** — opens the IMI website's Help section in the default browser (user guide + video tutorials section, to be built out on the website side as needed — not a KPlayer-side dependency).

---

## 5. About screen as launch splash

- Display the About screen at app launch, before the main window is ready.
- Keep it visible until the main window is prepared to show, replacing the current blank/delay period during startup.
- Reuses the same About screen content/design from item 4.1 — same asset, two triggers (launch splash + Help > About).

---

## 6. App icon

- Icon set already created (via Claude Design) — needs to be incorporated into the build.
- Apply to: app binary icon, and platform-specific packaging (macOS `.icns`, Windows `.ico`, plus any installer icons) as applicable.

**Scope narrowed (2026-07-11):** this item covers the KPlayer host app icon only. The 5 Kadabra/KSampler instrument plugin icons are built separately via HISE, not part of this CMake/JUCE build — integration with that pipeline is a future session with its own context, not in scope here.

KPlayer's own icon is wired up: `Assets/icons/kplayer-1024.png` (full-bleed master, macOS-safe/unmasked) and `Assets/icons/kplayer-256.png` (pre-scaled) are passed to `juce_add_gui_app`'s `ICON_BIG`/`ICON_SMALL` params in `CMakeLists.txt`, which generates `AppIcon.icns` at build time (`CFBundleIconFile` in Info.plist confirmed pointing at it). Windows `.ico` uses the same mechanism but is unverified (no Windows dev machine in this session).

For reference, the full icon set (KPlayer + 5 instrument plugins) is documented below in case it's useful when the HISE integration session happens:

| File | Code | Target |
|---|---|---|
| `kplayer-1024.png` (+ `kplayer-256.png` pre-scaled) | KP | KPlayer host app icon |
| `drumkit-1024.png` | ED | Electronic Drumkit KSampler plugin |
| `electric-piano-1024.png` | EP | Electric Piano KSampler plugin |
| `grand-1024.png` | GP | Grand KSampler plugin (white frame + inverted logo — see style guide) |
| `percussion-1024.png` | PN | Percussion KSampler plugin |
| `acoustic-drums-1024.png` | AD | Acoustic Drums KSampler plugin |
| `reserved-orange-1024.png`, `reserved-teal-1024.png` | — | Reserved accents, not yet assigned to an instrument |

Platform packaging notes (macOS 82% safe-area, Windows pre-baked rounded-rect) are in the handoff README — this item now covers KPlayer's own `.icns`/`.ico` plus icon builds for the 5 plugin binaries, not just KPlayer alone.

---

## Reference: About screen text (finalized, for item 4.1 / item 5)

```
KPlayer
Version 0.9.0

Welcome to KPlayer — Play and Perform with your Kadabra, at home or on stage!
Compatible with your favorite MIDI controllers and Audio Interfaces.

Developed by IMI Innovative Musical Instruments, in partnership with Tribal Tools, creators of the Kadabra.

KPlayer is free and open source software, licensed under the GNU Affero General Public License v3 (AGPLv3). Source code and documentation are available at:
github.com/innovative-musical-instruments/IMI-KPlayer

Built with JUCE.

© 2026 IMI Innovative Musical Instruments Ltd. All rights reserved.
```

Visual design of this screen is being handled separately in Claude Design — this spec covers wiring it up (launch splash behavior + Help > About menu item), not the visual layout itself.

---

## Notes for the Claude Code session

- Items 1.2–1.4, 2, 3, 4, and 6 are independent of each other and can be tackled in any order.
- Item 1.1's channel rename feature is **lowest priority — save for last**, doesn't need to land today.
- Item 5 depends on the About screen asset existing (from the Claude Design session) before the splash behavior can be fully wired and tested.
- No licensing/legal blockers on this list — KPlayer's AGPLv3 status is already settled.
