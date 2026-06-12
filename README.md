# Vial

## About
Vial is an open-source spectral warping wavetable synthesizer.

This is an unofficial fork of the [mtytel/vital](https://github.com/mtytel/vital) repository. Modifications are aimed at making the project more vanilla and suitable for local development.

### Changes from upstream

**Build system** (major change):
- Replaced Projucer with **CMake** — no `.jucer` files or Projucer app required
- JUCE moved from `third_party/JUCE/` to a root-level `JUCE/` directory consumed by CMake
- Tests wired into **CTest** (`cmake --build build --target VialTests`)
- Legacy VST2 SDK no longer required to build VST3

**Removed features**:
- Login UI and authentication classes
- Preset download dialog (cloud-based)
- Text to Wavetable feature (cloud-based)
- Check for updates option

**Other**:
- Skin file renamed from `default.vitalskin` → `default.vialskin`
- Commercial branding removal (WIP)
- JUCE 6 → JUCE 8 port (in progress — see [JUCE 8 Migration](#juce-8-migration) below)

## JUCE 8 Migration

This fork is in the middle of a JUCE 6.0.5 → 8.0.8 port. The codebase **builds and runs**, but several UI text-rendering regressions remain. See `docs/screenshots/` for current-state examples (`text-rendering-default-size.png`, `text-rendering-maximized.png`).

### What's working
- Clean compile on Windows (MSVC) and JUCE 8.0.8
- Standalone, VST3, and AU targets all build from a single CMake configuration
- All audio engine code unchanged — only UI/rendering touched

### Known issues
- Some text labels clip at large window sizes (e.g. `OCTAVE SCALE` → `OCTAVE`, `Trigger` → `Trigg` at maximized)
- Sideways section headings (`VOICE`, `EFFECTS`) render with vertical clipping and occasional doubled glyphs (`EFFFECTS`)
- Tab row (`VOICE / EFFECTS / MATRIX / ADVANCED`) can clip at the top of the window
- Some inactive OSC/FILTER rows render at lower opacity than intended at maximized window scale

In-progress fixes live on the `fix/juce8-text-rendering` branch — see that branch's PR for diagnostic notes.

## Building

The project uses CMake and [JUCE 8.0.8](https://juce.com/). It builds three targets from a single CMake configuration:

- **Standalone** app
- **AU** plugin (macOS only)
- **VST3** plugin

No legacy VST2 SDK is required.

### Prerequisites

- **CMake** 3.22 or later
- A C++17-capable compiler
- **macOS**: Xcode (available free from the App Store)
- **Linux**: standard build tools (`gcc`, `make`) plus JUCE's [Linux dependencies](JUCE/docs/Linux%20Dependencies.md)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Build artefacts land in `build/VialPlugin_artefacts/`.

For a debug build, replace `Release` with `Debug`. Note: debug builds are CPU-intensive and will perform poorly when playing many voices simultaneously.

### Running Tests

```bash
cmake -B build
cmake --build build --target VialTests --parallel
ctest --test-dir build --output-on-failure
```

## License
The source code is licensed under the GPLv3. If you download the source or create builds you must comply with that license.
