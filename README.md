# VialFX

VialFX is an audio effect plugin: two modelled filters, a nine-effect rack, LFOs and envelopes, and two audio
inputs (main and sidechain) that can each be routed to the filters, the effects or straight to the output.
It is built from GPLv3 synthesizer source code (see [NOTICE.md](NOTICE.md) for the origin of the code).

> **Status:** version 0.1.0, early development. It builds as a VST3 on Linux and Windows in CI and passes
> pluginval. The spectral morph and distortion knobs on the input panels are placeholders until v0.2 and v0.3.

## What it does today

- **Inputs:** a main input and a sidechain input, each with a live spectrum display. A dropdown under each
  display sends it to Filter 1, Filter 2, Filter 1+2, the effects rack, or direct out (skipping filters and
  effects). The INPUT and SIDECHAIN buttons on each filter mirror these dropdowns.
- **Filters:** two filters with the full set of filter models, in parallel or serial (FIL1/FIL2 buttons).
  Key tracking follows the last played MIDI note.
- **Effects rack:** chorus, compressor, delay, distortion, EQ, filter, flanger, phaser and reverb, in a
  reorderable chain.
- **Modulation:** LFOs and envelopes run without any MIDI note held; sync and the other LFO modes work.
  Notes act as a trigger and key-tracking source.
- **Presets:** save, load and browse presets from the built-in preset browser.
- **Oversampling:** 1x by default. Higher settings work but roll off the top end slightly (a better
  interpolator is planned).

## Roadmap

| Version | Content |
| --- | --- |
| v0.1 | Base FX: plugin shell, filters and effects rack (done), then the two-lane layout, GUI cleanup and modulation matrix |
| v0.2 | Distortion warp |
| v0.3 | Spectral processing |

## Building

### Linux (VST3)

```
sudo apt-get install build-essential pkg-config libasound2-dev libfreetype6-dev libgl1-mesa-dev \
  libcurl4-openssl-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev
make -C effects/builds/linux_vst VST3 CONFIG=Release AR=gcc-ar SIMDFLAGS="-msse2" GLFLAGS=""
```

The result is `effects/builds/linux_vst/build/VialFX.vst3`.

### Windows (VST3)

Open `effects/builds/vs17` with Visual Studio 2022, or use the MSBuild steps in
`.github/workflows/build-windows.yml`. Prebuilt VST3 files for both platforms are attached to each CI run
as build artifacts.

## Tests

`make -C effects/tests` builds and runs a headless engine test (no GUI, OpenGL or audio device needed). It
checks passthrough level, both filters and their routing, key tracking, every filter model, all nine effects,
oversampling, LFO and envelope behaviour without MIDI, input destinations, and CPU use. CI runs it before
every build, then validates the plugin with pluginval and checks the binary for forbidden branding.

## Online features

VialFX does not connect to any web service. Account login, content and pack downloads, preset store links,
the text-to-wavetable service and update checks are disabled in the effects build.

## License

GPLv3, see [LICENSE](LICENSE). If you distribute builds you must comply with that license, including making
the corresponding source available.

Additional conditions inherited with the source code are listed in [NOTICE.md](NOTICE.md).
