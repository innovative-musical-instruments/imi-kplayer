# Kadabra K-Player

A JUCE standalone VST3/AU plugin-host app — a "channel strip" style rack
(per-channel instrument + 5 inserts, gain/pan, MIDI routing) paired with
custom "Kadabra" performance hardware.

This repo is **private**. See `CLAUDE.md` for architecture notes and
cross-repo context (shared specs/decisions live in the sibling
`imi-common-docs` repo).

---

## Built With

- [JUCE](https://juce.com/) 9.0.1 — required separately, not vendored in
  this repo (see Build below). This is a plain JUCE SDK checkout, distinct
  from the JUCE version bundled inside HISE that the Kadabra K-Sampler
  instruments build against — the two aren't related, and a discrepancy
  in JUCE version between K-Player and the K-Samplers is expected and has
  no build/runtime implications for either.

## Build

Requires a separate JUCE 9.0.1 SDK checkout at `~/SDKs/JUCE` (Mac) or
`C:/SDKs/JUCE` (Windows) — see `CMakeLists.txt`'s `if(APPLE)`/`elseif(WIN32)`
branches. Full build/release/signing instructions are in `CLAUDE.md`.

```
cmake -B build -G Xcode                      # Mac
cmake -B build -G "Visual Studio 18 2026"     # Windows
cmake --build build --config Debug
```

## License

Proprietary — see the umbrella EULA draft in `imi-common-docs/legal/`
(**not yet legal-reviewed**). Unlike the Kadabra K-Sampler instruments
(GPL-3.0, public), this repo and its binaries are not open source.

---

## About IMI

Developed by [Innovative Musical Instruments (IMI)](https://www.innovativemusicalinstruments.com),
collaborators of [Tribal Tools](https://www.tribal-tools.com), creators
of the Kadabra — a wearable Music Workstation that straps to the
player's neck and shoulders, featuring motion sensors, touch keyboards,
and button arrays.
