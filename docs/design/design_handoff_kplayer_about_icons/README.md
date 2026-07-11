# Handoff: KPlayer About Box / Splash Screen + Icon System

## Overview
This package covers three related pieces of the IMI / Kadabra / KPlayer visual system:
1. **KPlayer About box** (670×290) and **splash screen** (670×290, identical layout + progress bar) for the KPlayer host app.
2. **App & plugin icon system** — the shared two-letter-code icon design used across KPlayer and all Kadabra instrument plugins, plus rendered 1024px master art.
3. **Brand + product style guide** (`IMI Style Guide - B Pulse.dc.html`) — included for full context on colors, type, voice, and the existing plugin/host GUI conventions these two pieces slot into.

## About the Design Files
The `.dc.html` files in this bundle are **design references built in HTML** — they show intended look, copy, and layout, not production code to copy directly. The task is to **recreate these designs in KPlayer's actual codebase** (JUCE/C++ for the About box and splash screen; whatever the icon build pipeline is for the app icons) using the target environment's existing patterns — not to embed this HTML.

## Fidelity
**High-fidelity.** Colors, typography, copy, spacing, and layout are final as shown. Treat pixel values, hex codes, and copy text below as source of truth.

## Screens

### 1. KPlayer About Box (670 × 290 px)
- **Purpose**: Standard "About KPlayer" dialog, opened from the app/plugin menu.
- **Layout**: Fixed 670×290 box, `border-radius: 10px`, background `#141a26`.
  - Top bar: 8px vertical padding, background `#0e1320`, 1px bottom border `#232d42`. Contains 3 traffic-light dots (10px circles) for macOS: close `#e0563f`, min/max `#3a4356` (inactive-styled — About boxes are typically not resizable). On Windows, swap for a plain title bar with a single "×" close button, same background/border.
  - Content row: flex row, `padding: 22px 24px`, `gap: 22px`.
    - Left: icon column, fixed width 112px, centered — 96×96px KPlayer app icon (`assets/icons/kplayer-256.png`, or generate from the 1024 master, see Icon System below).
    - Right: flex column, `gap: 8px`:
      1. Title row (flex, `align-items: baseline`, `gap: 10px`): "KPLAYER" in Azonix (`font-size: 24px`, `letter-spacing: 0.02em`, color `#F5F6F7`) + "Version 0.9.0" (Inter, `12px`, color `#7b8aa3`).
      2. Welcome line (Inter, `12px`, color `#c7cfdb`, `line-height: 1.5`): "Play and perform with your **Kadabra**, at home or on stage! Compatible with your favorite MIDI controllers and audio interfaces." — "Kadabra" links to `https://www.kadabra.net`.
      3. Credits line (Inter, `11px`, color `#8a97ac`, `line-height: 1.5`): "Developed by **IMI Innovative Musical Instruments**, in partnership with **Tribal Tools**, creators of the Kadabra." — "IMI Innovative Musical Instruments" links to `https://www.innovativemusicalinstruments.com/Kplayer` (color `#dfe4ec`); "Tribal Tools" links to `https://www.tribal-tools.com` (color `#dfe4ec`).
      4. License block (Inter, `10.5px`, color `#6d7a90`, `line-height: 1.5`): "Free and open source, licensed under the GNU AGPLv3. Source & docs: " followed by a line-break, then the repo URL as a monospace link (`JetBrains Mono`, `10px`, color `#3fd9c4` / hover `#22C7B4`): `github.com/innovative-musical-instruments/IMI-KPlayer` → links to `https://github.com/innovative-musical-instruments/IMI-KPlayer`. Then another line: "Built with **JUCE**." — "JUCE" links to `https://juce.com/`.
  - Footer: `padding: 8px 24px`, background `#0e1320`, 1px top border `#232d42`, right-aligned copyright text (`10.5px`, color `#525d70`): "© 2026 IMI Innovative Musical Instruments Ltd. All rights reserved."

### 2. KPlayer Splash Screen (670 × 290 px)
Identical box, size, and content to the About box above, with two changes:
- **Top bar** is replaced with a full-width progress bar: track background `#232d42`, 4px tall, fill color `#22C7B4` (teal), currently shown at 62% — wire to actual load progress.
- **Footer** left side (where the About box has empty space) now reads "Launching KPlayer..." in `JetBrains Mono`, `12px`, `letter-spacing: 0.06em`, uppercase, color `#22C7B4`. Copyright stays right-aligned, same as About box.
- No close button / traffic lights (splash isn't a closable window).
- Splash auto-dismisses once the main app window is ready — no user interaction.

## Design Tokens (this package)
- **Colors**: Ink `#0F1116` · Panel `#171A21` / dialog panel `#141a26` · dialog chrome `#0e1320` · borders `#232d42` / `#2a3446` · Muted `#9AA3AE` · Paper `#F5F6F7` · Pulse Teal `#22C7B4` (link/active default `#3fd9c4`, hover `#22C7B4`) · Pulse Orange `#FF8A3D` · Signal Blue `#3B5A8A` (KPlayer/host-app accent, gradient `#4a6da3 → #2e4468`).
- **Typography**: Display/wordmark = **Azonix** (`assets/Azonix.otf`, licensed font — embed via `@font-face`), ALL CAPS only. Body/UI = **Inter** (Google Fonts, weights 400–700). Monospace labels/links = **JetBrains Mono**.
- **Radius**: 10px dialog corners, 6–8px inner controls, fully-rounded (pill) buttons.
- **Icon frame**: 3–4px solid border, color matches the icon's letter color (see Icon System).

## Icon System
Shared identity across every KPlayer/plugin binary:
- A bold **two-letter code** set in Azonix, centered, on the instrument's full accent color background (not a dark shade — needs to read at 16px).
- The **IMI logo mark** (`assets/imi-logo-white.png`) centered directly below the code, same relative size on every icon.
- A **frame** (3–4px) matching the letter color, except **Grand** which gets a white frame for contrast against its light background (and an inverted/black IMI mark).
- Codes used: **KP** (KPlayer, Signal Blue gradient `#4a6da3→#2e4468`), **ED** (Electronic Drumkit, black `#0F1116` bg, graphite `#8a8f99` letters/frame), **EP** (Electric Piano, violet gradient `#8a5aa8→#5a2e78`), **GP** (Grand, silver gradient `#e2e5ea→#b7bcc6`, dark `#24262a` letters, white frame + inverted logo), **PN** (Percussion, indigo gradient `#6a54a8→#402e78`), **AD** (Acoustic Drums, maroon gradient `#8a4a5a→#5a1f2e`). Two accents (**Pulse Teal** `#3fd9c4→#0f8a7c` and **Pulse Orange** `#ffab6b→#d96a1f`) are reserved for future instruments.
- **1024px master art** for all 8 is in `assets/icons/*-1024.png` — full-bleed square with the rounded-frame look already baked in.
- **Platform packaging**:
  - **macOS (.icns)**: Deliver full-bleed 1024×1024 square art with content kept in the center ~82% safe area (macOS auto-applies its own squircle mask + shadow — do not pre-mask). Export 16/32/128/256/512/1024 (@1x/@2x) into one `.icns`.
  - **Windows (.ico)**: Windows does **not** auto-mask, so bake a ~14% rounded-rect into the art (already done in the master PNGs). Export 16/24/32/48/256px flattened into one `.ico`; include a plain-square fallback at 16/24 since rounding disappears at that size anyway.

## Assets
- `assets/Azonix.otf` — licensed display font, used for all product wordmarks/codes.
- `assets/imi-logo-white.png` — IMI logotype, white-on-transparent (invert for use on light backgrounds).
- `assets/icons/*-1024.png` — 1024px master icon art, one per instrument + KPlayer + 2 reserved accents.
- `assets/icons/kplayer-256.png` — pre-scaled 256px KPlayer icon used in the About box / splash mockup.
- `assets/gui-*.png` — reference screenshots of the existing plugin GUIs and host mixer app (for context only, not new design work).

## Files in this package
- `KPlayer About and Splash.dc.html` — the About box + splash screen design (open directly in a browser to view).
- `Kadabra App Icons Mockup.dc.html` — the full icon system grid + platform packaging notes.
- `IMI Style Guide - B Pulse.dc.html` — full brand/product style guide slide deck (13 slides: brand, logo, color, type, voice, UI components, plugin GUI anatomy, controls, instrument accents, host app language, icon system, imagery, IMI/Kadabra/Tribal-Tools architecture). Use arrow keys or the thumbnail rail to navigate.
- `screenshots/` — static PNG captures of the key screens for quick reference without opening the HTML files.
