# VialFX

VialFX is an audio effect plugin: a filter, modulation and effects-rack processor with a sidechain input,
built from GPLv3 synthesizer source code (see [NOTICE.md](NOTICE.md) for the origin of the code).

> **Status:** early development. The plugin shell builds on Linux (VST3) in CI and is being checked with
> pluginval. Filters in front of the effects rack, the two-lane layout and the spectral modes are planned
> in later updates.

## Building (Linux, VST3)

```
sudo apt-get install build-essential pkg-config libasound2-dev libfreetype6-dev libgl1-mesa-dev \
  libcurl4-openssl-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev
make -C effects/builds/linux_vst VST3 CONFIG=Release AR=gcc-ar SIMDFLAGS="-msse2" GLFLAGS=""
```

The result is `effects/builds/linux_vst/build/VialFX.vst3`. The same steps run in
`.github/workflows/build-linux.yml`.

## Online features

VialFX does not connect to any web service. Account login, content/pack downloads, preset store links, the
text-to-wavetable service and update checks are disabled in the effects build.

## License

GPLv3, see [LICENSE](LICENSE). If you distribute builds you must comply with that license, including making
the corresponding source available.

Additional conditions inherited with the source code are listed in [NOTICE.md](NOTICE.md).
