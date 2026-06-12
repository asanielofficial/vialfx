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
