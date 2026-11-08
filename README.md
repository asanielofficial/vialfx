# VialFX

VialFX is an audio effect plugin: two modelled filters, a rack of nine effects, LFOs and envelopes, and two
audio inputs (main and sidechain) that can each be warped, spectrally morphed and routed to the filters, the
effects or straight to the output. It is built from GPLv3 synthesizer source code (see
[NOTICE.md](NOTICE.md) for the origin of the code).

> **Status:** version 1.1. VST3 builds for Linux, macOS and Windows, plus a Windows installer (.exe) and a
> macOS installer (.pkg), are produced by CI, and the plugin passes pluginval.

## Download

The current stable version is [**VialFX 1.1**](https://github.com/asanielofficial/vialfx/releases/latest): a Windows installer (.exe), a macOS installer (.pkg) and VST3 zips for Windows, macOS and Linux.

The newest `main` commit that built on all three platforms is published as the [nightly pre-release](https://github.com/asanielofficial/vialfx/releases/tag/nightly) (Linux, macOS and Windows VST3 and CLAP, plus the Windows and macOS installers, which let you choose which formats to install). These builds are unsigned and meant for testing. Every CI run also attaches its builds as artifacts.

## Features

### Inputs and routing

- A **main** and a **sidechain** input, each with a live spectrum display. A dropdown under each display sends
  it to Filter 1, Filter 2, Filter 1+2, the effects rack, or direct out (skipping filters and effects). The
  INPUT and SIDECHAIN buttons on each filter mirror these dropdowns.
- Each input has an on/off switch, level and pan, all of which can be modulated. The sidechain lane is off by
  default.
- The input spectrum displays show each lane after its level, pan, spectral mode and warp.

### Distortion warp

Each input has a warp mode and amount (the right-hand knob on its panel; the amount is a modulation target).

- **Modes:** None (default), Sync and Formant (wavefolders), Quantize (bit and sample-rate reduction), Bend and
  Squeeze (waveshapers), Pulse (asymmetric clipper), Distort Table (below), and, on the main input only, FM
  and RM, where the sidechain lane modulates the main lane.
- Sync, Formant, Bend and Squeeze are bipolar (knob centre is neutral); the other modes are neutral at zero.
  The waveshapers work relative to the signal's own level, so they also bite at quiet levels.
- The sidechain lane is warped first. It modulates FM and RM at full strength whatever its level and pan are,
  like an oscillator B turned down: only its on switch gates it, and level and pan apply to the signal that
  goes on to the filters and effects.

### Spectral morph

Each input has a spectral mode and amount (the left-hand knob on its panel; the amount is a modulation
target). The stage is a Hann-windowed overlap-add FFT (75% overlap) between the lane and its warp.

- **Modes:** Formant Scale, Harmonic Stretch, Inharmonic Stretch, Smear, Random Amplitudes, Low Pass,
  High Pass, Phase Disperse (up delays the highs, down delays the lows, turning clicks into sweeps), Shepard
  Tone, Spectral Time Skew (frequency-dependent group delay of up to 16 frames), the drawable modes and
  Spectral Table (below), and, on the main input only, Vocode.
- Formant Scale, Harmonic and Inharmonic Stretch, and Phase Disperse are bipolar (knob centre is neutral).
- **Vocode** imposes the sidechain lane's spectral envelope on the main lane. It is a time-domain filterbank
  vocoder (up to 64 log-spaced 4th-order band-passes, so transients stay sharp) with band attack/release,
  formant shift, carrier flatten, modulator emphasis, gate, level, a consonant hiss that follows the
  modulator's highs, and whitening of the modulator's spectral tilt. Its settings are in the grouped Vocoder
  menu.
- **FFT size** is set on the ADVANCED tab: 256, 512, 1024 (default), 2048, 4096, 8192, 16384 or 32768 (very
  fine resolution, up to about 0.74 s of latency), with a live latency readout.
- **Latency:** with no spectral mode chosen on either lane there is no latency (the plugin reports 0).
  Choosing one engages the stage: both lanes then delay by one FFT size so they stay aligned, and the
  reported latency changes accordingly. It is reported to the host for delay compensation.
- The FFT only runs while a spectral mode is active and the inputs are not digital silence. Switching a mode
  on runs a few frames silently first so the wet signal fades in without a dip.

### Dice and drawable modes

- **Random Amplitudes** has a small dice button beside the spectral knob that picks a new seed. It is a
  switch parameter (`Random Dice`) the host can automate: every flip between 0% and 100% scrambles the
  pattern as if the button had been pressed. A thin shuffle bar under it is a modulation target: drag an LFO
  or any source onto it to walk smoothly through a chain of patterns derived from the seed. The pattern is the
  same in Hz at every FFT size.
- **Spectral Filter** (a resonant gain curve), **Spectral Phaser** (a resonant phaser whose notches follow the
  curve) and **Spectral Flanger** (a bank of regenerating combs whose delay time per frequency is the curve)
  have a pencil button that docks a curve editor over the filters. It works like the LFO editor: paint brush
  and patterns, grid sizes, smoothing, a shape selector (the menu also saves and opens your own shapes), and
  a frequency axis that is logarithmic from 20 Hz to 20 kHz. The curve's right-click menu has Import Shape
  and Export Shape.
- A bar of detail controls under the position slider (gain, plus width/peak for the filter,
  notches/resonance for the phaser, taps/resonance/spread/depth/phase for the flanger) is made of modulation
  targets, as is the position slider itself. For Spectral Filter the position transposes the drawn curve up
  and down in frequency by up to four octaves.
- The spectral knob of these three modes is bipolar: the centre is the automatic behaviour, up morphs to your
  drawn curve and down to the curve flipped upside down.
- The curves and the dice seed are saved with each lane in the preset, not as parameters, so they cannot be
  automated (the dice switch and the shuffle bar are the automatable controls).
- **Panels:** the curve editors, the Vocoder and the table menus dock over the filters. Any two can be open
  side by side (a third replaces the one opened first), and dragging a panel's top across to the other side
  swaps them.

### Table modes

- **Distort Table** (warp mode 9, both lanes): waveshaping by a built-in wavetable of 16 transfer curves
  (clean, soft, hard, folds, Chebyshev, stairs, diode, rectify, crush, ripple, chaos). The amount scrubs
  through the frames; 0 is clean.
- **Spectral Table** (spectral mode 15, both lanes): a filter driven by a built-in wavetable of 16 frequency
  responses (flat, sub, tilt, vowels Oo/Oh/Ah/Eh/Ee, band, combs, notch, high pass, telephone, scoop,
  ripples). The amount scrubs through the frames; 0 is flat.
- The sidechain menus hide FM, RM and Vocode. Mode indices of the existing modes did not change.
- **Controls:** the pencil (top left of the knob) opens the table's wavetable editor (save, import, edit). The
  cogwheel (top right) opens the settings menu, which shows the wavetable (3D / 2D / SP button) following the
  knob, with the fine controls underneath. Audio files dropped on a table menu's wavetable display are
  imported as the table (splice, vocode or pitch splice, by drop zone).
- **Spectral Table menu:** gain, pitch (transposes the response by up to four octaves), resonance (how far the
  response reaches) and mix. These have their own parameters (`input_table_gain`, `input_table_position`,
  `input_table_resonance`, `input_spectral_table_mix`, and the `sidechain_` ones), separate from the drawable
  modes' position, gain and resonance, so each can be modulated from the matrix on its own.
- **Distort Table menu:** phase (shifts the transfer curve), bias, smooth, drive, DC filter (removes the
  offset that bias and the curve create) and mix. Drive, bias and smooth act even on the clean first frame.

### Filters

- Two filters with the full set of filter models, in parallel or serial (FIL1/FIL2 buttons).
- **Key tracking** is polyphonic: Filter FX, Delay, Chorus, Flanger and Phaser run a parallel copy per held
  note and crossfade their average. It is gated on the keytrack sync mode or a non-zero keytrack amount, and
  falls back to the last played note otherwise. The effects have transpose sliders for it.

### Effects rack

- The effects tab starts blank. **ADD EFFECT** (top of the left panel) adds a Chorus, Compressor, Delay,
  Distortion, EQ, Filter, Flanger, Phaser or Reverb to the end of the chain. The same effect can be added
  several times (up to 8 copies of each type); every copy is a full set of automatable, modulatable
  parameters named e.g. `chorus_2_dry_wet`.
- Rows in the left panel can be dragged to reorder the chain, and clicking one scrolls to that effect. Both
  panels scroll when the chain is long.
- Each effect has an on/off button, a `-` that minimizes it to a title bar (name and symbol in the effect's
  colour) and an `x` that removes it (its settings and modulations are reset). Right-click an effect for
  Duplicate (same settings), Minimize or Remove.
- The chain is saved with the preset. Presets from before the chain existed load with their enabled effects in
  their old order.

### Modulation

- LFOs and envelopes run without any MIDI note held; sync and the other LFO modes work. Notes act as a
  trigger and key-tracking source.
- The full matrix is wired: LFOs, envelopes, randoms, macros and MIDI sources (note, velocity, aftertouch,
  slide, lift, mod wheel, pitch wheel) can drive lane level and pan, filter parameters and effect parameters.
  The table and morph controls show modulation rings and meters, including when docked.

### Presets and oversampling

- Presets are `.vialfx` files (older `.vital` presets still open). Save, load and browse them from the
  built-in preset browser.
- Oversampling defaults to 2x. Higher settings work but roll off the top end slightly (a better interpolator
  is planned).

## Window and skins

- **Resizing:** the window can be resized to any shape. Sizes scale with the smaller dimension and the
  sections stretch to fill the rest. The shape is remembered.
- **Skins** are `.vialskin` files (older `.vitalskin` files still open); the default skin is the attached
  `Default.vialskin`. Pick one from the skin selector in the ADVANCED tab or use Load Skin from the preset
  menu.

### Skin Editor

Open it from the preset menu with **Open Skin Editor**. It stays on top of the plugin window and can be
resized.

- **Bins:** the interface is grouped into bins (Header, Input, Filters, Envelopes, LFO, Voice/MIDI, Effects,
  Matrix, Browser, Overlays), plus a Global bin for the base skin. The Effects bin edits every effect module together, and each module (Chorus, Compressor, Delay,
  Distortion, Equalizer, Filter, Flanger, Phaser, Reverb) also has its own sub-bin. Each bin lists its colour and value
  overrides; **+ Add override** adds more.
- **Colour picker:** hex (AARRGGBB), RGB and alpha. Undo/redo, Save and Import work on `.vialskin` files.
- **Hover outline:** while the editor is open, hovering the plugin (or a bin in the editor) draws a dashed
  outline around the matching sections. Alt-click a section to jump to its bin.
- **Glow:** every colour has Glow Strength and Glow Size knobs (0 to 1), saved in the skin. A full-screen
  glow pass makes anything drawn in a glowing colour bloom softly: text, lines, knobs, waveforms and meters,
  including section overrides of that colour. Up to 24 glowing colours are used at once. Glow can be set per
  bin as well, and knobs also get a halo for the Rotary Arc colour.
- **Gradients:** any colour can be turned into a gradient with the GRADIENT button: the colour is the first stop, with one or two more stops and an angle. Pixels drawn in that colour take their colour from the gradient across the window (global colours) or across the bin's sections (bin overrides). Gradients apply instantly and are saved in the skin.
- **Smooth editing:** colour and value edits are applied at a limited rate so the plugin window stays smooth
  while you drag; glow changes apply instantly.

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
`.github/workflows/build.yml`.

### macOS (VST3)

See `.github/workflows/build.yml` for the Xcode build steps.

## Tests

`make -C effects/tests` builds and runs a headless engine test (no GUI, OpenGL or audio device needed). It
checks passthrough level, both filters and their routing, key tracking, every filter model, all nine effects,
oversampling, LFO and envelope behaviour without MIDI, input destinations, and CPU use. CI runs it before
every Linux build, then validates the plugin with pluginval and checks the binary for forbidden branding.

## Acknowledgements

**Matt Tytel.** VialFX would not exist without him. It is built on the open-source synthesizer code he wrote and released under the GPLv3. His work on the audio engine, the modulation system and the interface gave this project a foundation far beyond what we could have built from scratch. Thank you for sharing that work openly so that others can learn from it and build on it. VialFX is an independent project and is not affiliated with or endorsed by him.

**-LightningPig** made the art for VialFX, including the logo. Thank you for giving the plugin its look.

**Every tester.** Thank you to everyone who installed early builds, reported bugs, sent screenshots and told us what felt wrong. Much of what is in 1.1 exists because of your feedback.

## License

GPLv3, see [LICENSE](LICENSE). If you distribute builds you must comply with that license, including making
the corresponding source available.

Additional conditions inherited with the source code are listed in [NOTICE.md](NOTICE.md).
