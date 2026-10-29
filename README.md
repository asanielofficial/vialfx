# VialFX

VialFX is an audio effect plugin: two modelled filters, a nine-effect rack, LFOs and envelopes, and two audio
inputs (main and sidechain) that can each be routed to the filters, the effects or straight to the output.
It is built from GPLv3 synthesizer source code (see [NOTICE.md](NOTICE.md) for the origin of the code).

> **Status:** version 0.1.0, early development. It builds as a VST3 on Linux and Windows in CI and passes
> pluginval. The spectral suite (v0.3) is in: morph engine, dice button and drawable spectral warps.

## What it does today

- **Inputs:** a main input and a sidechain input, each with a live spectrum display. A dropdown under each
  display sends it to Filter 1, Filter 2, Filter 1+2, the effects rack, or direct out (skipping filters and
  effects). The INPUT and SIDECHAIN buttons on each filter mirror these dropdowns. Each input also has an
  on/off switch, level and pan (all three can be modulated; the sidechain lane is off by default).
- **Distortion warp (v0.2):** each input has a warp mode and amount (the right-hand knob on its panel, and
  the amount is a modulation target). Modes: None (off, the default), Sync and Formant (wavefolders), Quantize (bit and sample-rate
  reduction), Bend and Squeeze (waveshapers), Pulse (asymmetric clipper), and on the main input only FM and RM,
  where the sidechain lane modulates the main lane. The sidechain lane is warped first; FM and RM on the main
  input are silent while the sidechain lane is switched off. The sidechain lane modulates at full strength
  whatever its level and pan are (like an oscillator B turned down): only its on switch gates it, and the
  level and pan apply to the signal that goes on to the filters and effects. Sync, Formant, Bend and Squeeze are bipolar like the
  original distortions (knob centre is neutral); the other modes are neutral at zero. The waveshapers work
  relative to the signal's own level, so they bite at quiet levels too. The input spectrum displays show each
  lane after its level, pan and warp.
- **Spectral morph (v0.3):** each input has a spectral mode and amount (the left-hand knob on its panel; the
  amount is a modulation target). The stage is a Hann-windowed overlap-add FFT (75% overlap) that sits
  between the lane and its warp. Modes: Formant Scale, Harmonic Stretch, Inharmonic Stretch, Smear, Random
  Amplitudes, Low Pass, High Pass, Phase Disperse (a strong two-way disperser: up delays the highs, down
  delays the lows, turning clicks into sweeps), Shepard Tone, Spectral Time Skew (a frequency-dependent group delay of up to 16 frames:
  the highs lag behind the lows), three drawable modes (below), and on the main input only Vocode, which imposes the sidechain lane's spectral envelope on
  the main lane (a time-domain filterbank vocoder: up to 64 log-spaced 4th-order band-passes, so transients stay sharp, band attack/release, formant shift, carrier flatten, modulator emphasis, gate, level, and a consonant hiss that follows the modulator's highs; whitening of the modulator's spectral tilt for clarity; settings in the grouped Vocoder menu). Formant Scale, Harmonic/Inharmonic Stretch, Phase Disperse are bipolar (knob
  centre is neutral). The FFT size is set on the ADVANCED tab (256, 512, 1024 default, 2048, then 4096, 8192, 16384, 32768 for very fine resolution at the cost of up to ~0.74 s latency) with a live
  latency readout. The stage always delays the audio by one FFT size, whatever the mode, so the latency is
  constant; it is reported to the host for delay compensation and both lanes stay aligned.
- **Dice and drawable modes (v0.3):** Random Amplitudes has a small dice button beside the spectral knob that
  picks a new seed (it is a switch parameter, `Random Dice`, that the host can automate: every flip between
  0% and 100% scrambles the pattern as if the button had been pressed), and a thin shuffle bar under it that
  is a modulation target: drag an LFO or any source onto
  it to walk smoothly through a chain of patterns derived from the seed. The pattern is the same in Hz at every
  FFT size. Spectral Filter (a resonant gain curve), Spectral Phaser (a resonant phaser whose notches follow the curve) and
  Spectral Flanger (a bank of regenerating combs whose delay time per frequency is the curve, so a drawn ramp or bump bends the comb; Position sweeps the delay) have a pencil button that docks a curve editor over the filters, drawn like
  the LFO editor (paint brush and patterns, grid sizes, smoothing, shape selector, frequency on a log axis from
  20 Hz to 20 kHz; the shape menu also saves and opens your own shapes). A bar of detail controls under the position slider (gain, plus width/peak for the filter, notches/resonance for the phaser, taps/resonance/spread/depth/phase for the flanger) is made of modulation targets too, and the curve's right-click menu has Import Shape / Export Shape. A position slider along its bottom is a modulation target as well (for Spectral Filter it transposes the drawn curve up and down in frequency by up to four octaves). The spectral knob of
  these three modes is bipolar: the centre is the automatic behaviour, up morphs to your drawn curve and down
  to the curve flipped upside down. The curves and the dice seed are saved with each lane in the preset, not as
  parameters, so they cannot be automated (the dice switch and the shuffle bar are the automatable controls).
- **Filters:** two filters with the full set of filter models, in parallel or serial (FIL1/FIL2 buttons).
  Key tracking follows the last played MIDI note.
- **Effects rack:** chorus, compressor, delay, distortion, EQ, filter, flanger, phaser and reverb, in a
  reorderable chain.
- **Modulation:** LFOs and envelopes run without any MIDI note held; sync and the other LFO modes work.
  Notes act as a trigger and key-tracking source. The full matrix is wired: LFOs, envelopes, randoms,
  macros and MIDI sources (note, velocity, aftertouch, slide, lift, mod wheel, pitch wheel) can drive lane
  level and pan, filter parameters and effect parameters.
- **Presets:** save, load and browse presets from the built-in preset browser.
- **Oversampling:** 1x by default. Higher settings work but roll off the top end slightly (a better
  interpolator is planned).

## Roadmap

| Version | Content |
| --- | --- |
| v0.1 | Base FX: plugin shell, filters and effects rack, two-lane layout, GUI cleanup and modulation matrix (done) |
| v0.2 | Distortion warp (done) |
| v0.3 | Spectral processing: morph engine, dice button and drawable spectral warps (done) |

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


## Table modes and idle FFT

- **Distort Table** (warp mode, both lanes): waveshaping by a built-in wavetable of 16 transfer curves (clean, soft, hard, folds, Chebyshev, stairs, diode, rectify, crush, ripple, chaos). The amount scrubs through the frames, 0 is clean.
- **Spectral Table** (spectral mode, both lanes): a filter driven by a built-in wavetable of 16 frequency responses (flat, sub, tilt, vowels Oo/Oh/Ah/Eh/Ee, band, combs, notch, high pass, telephone, scoop, ripples). The amount scrubs through the frames, 0 is flat; the position slider transposes the response by up to four octaves.
- Mode indices of the existing modes did not change (Distort Table is warp mode 9, Spectral Table is spectral mode 15). The sidechain menus hide FM, RM and Vocode.
- The FFT only runs while a spectral mode is active and the inputs are not digital silence. A neutral stage is just the delay line; switching a mode on runs a few frames silently first so the wet signal fades in without a dip.
- With no spectral mode chosen on either lane there is no latency at all (the plugin reports 0). Choosing a spectral mode on either lane engages the stage: both lanes then delay by one FFT size so they stay aligned, and the reported latency changes accordingly. The default oversampling is 2x.
