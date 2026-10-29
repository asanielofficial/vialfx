/* Copyright 2013-2019 Matt Tytel
 *
 * This file is part of VialFX, which is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "fx_tables.h"
#include "processor.h"
#include "fourier_transform.h"
#include "line_generator.h"
#include "utils.h"
#include "value.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace vital {
  // Spectral morph stage of an input lane (lane -> spectral morph -> warp).
  //
  // A short-time Fourier transform engine: Hann analysis window, 75% overlap, Hann synthesis window,
  // overlap-add. The FFT size is 256, 512, 1024 (default) or 2048. The stage ALWAYS delays the audio by exactly
  // one FFT size, whatever the mode or amount, so the latency is constant and can be reported to the host
  // (latencySamples()). With a neutral setting the output is the input delayed by that many samples.
  //
  // Modes (amount knob 0..1; bipolar modes are neutral at 0.5, the rest at 0):
  //   Formant Scale (bipolar)   moves the spectral envelope up/down up to an octave, keeping the fine structure
  //   Harmonic Stretch (bip.)   scales the whole spectrum in frequency by up to an octave (ratio 2^u)
  //   Inharmonic Stretch (bip.) like Harmonic Stretch, but the scaling grows with frequency
  //   Smear                     smears each bin's energy into the bins above it
  //   Random Amplitudes         seeded random gain per bin
  //   Low Pass / High Pass      steep spectral filters, cutoff 20 Hz .. Nyquist (exponential)
  //   Phase Disperse (bipolar)  a strong dispersive chirp (frequency-dependent delay), either direction:
  //                             up delays the highs, down delays the lows
  //   Shepard Tone              octave-shifted copies crossfaded so the spectrum glides without end
  //   Spectral Time Skew        group delay that grows with the log of the frequency: high frequencies arrive late
  //   Vocode                    a channel vocoder: the other lane (the modulator) controls the level of this lane
  //                             in a row of log-spaced bands. The knob is the depth. Settings (the Vocoder menu):
  //                             bands (4..64), attack and release of the band followers, formant shift of the
  //                             modulator's bands, carrier flatten (how much each band of this lane is levelled
  //                             first, so a tonal lane still fills every band) and noise (a hiss shaped like the
  //                             modulator's highs, for consonants). Only the main lane has a modulator. With no
  //                             modulator signal Vocode removes the sound as the depth rises.
  class SpectralMorphProcessor : public Processor {
    public:
      enum {
        kAudio,
        kMode,
        kAmount,
        kFftSize,
        kPosition,
        kShuffle,
        kDice,
        kModulator,
        // Detail controls of the three drawable modes (0..1, 0.5 = the standard behaviour).
        kGain,        // output level, 0..2x
        kDensity,     // Filter: width of the automatic peak; Phaser: notch count; Flanger: tilt
        kResonance,   // Filter: height of the automatic peak; Phaser and Flanger: feedback
        kSpread,      // Flanger: stereo spread
        kDepth,       // Flanger: how deep the notches are
        kPhase,       // Flanger: offset of the comb (0 = positive, 50% = negative / through-zero)
        // Vocode settings (read by the main lane only).
        kVocodeBands,
        kVocodeAttack,
        kVocodeRelease,
        kVocodeFormant,
        kVocodeFlatten,
        kVocodeNoise,
        kVocodeEmphasis,
        kVocodeGate,
        kVocodeLevel,
        kVocodeWhiten,
        kNumInputs
      };

      enum Mode {
        kNone,
        kFormantScale,
        kHarmonicStretch,
        kInharmonicStretch,
        kSmear,
        kRandomAmplitudes,
        kLowPass,
        kHighPass,
        kPhaseDisperse,
        kShepardTone,
        kTimeSkew,
        kSpectralFilter,   // drawable: per-bin gain curve
        kSpectralPhaser,   // drawable: per-bin phase rotation curve
        kSpectralFlanger,  // drawable: per-bin comb depth curve
        kVocode,
        kSpectralTable,   // the amount scrubs a wavetable of filter responses (FxTables)
        kNumModes
      };

      static constexpr uint32_t kDefaultSeed = 4;
      static constexpr int kNumFftSizes = 8;
      static constexpr int kMinFftBits = 8;
      static constexpr int kMaxFftSize = 1 << (kMinFftBits + kNumFftSizes - 1);
      static constexpr int kDefaultFftIndex = 2;
      static constexpr int kOverlap = 4;
      static constexpr float kMaxGain = 8.0f;

      static int fftSizeFromIndex(int index) {
        return 1 << (kMinFftBits + std::max(0, std::min(index, kNumFftSizes - 1)));
      }

      static bool isBipolar(int mode) {
        return mode == kFormantScale || mode == kHarmonicStretch || mode == kInharmonicStretch ||
               mode == kPhaseDisperse ||
               mode == kSpectralFilter || mode == kSpectralPhaser || mode == kSpectralFlanger;
      }

      // The three drawable modes: the knob is bipolar. At the centre the automatic behaviour governs; turning
      // up morphs towards the drawn curve, turning down towards the drawn curve flipped upside down. They are
      // never "neutral".
      static bool isDrawable(int mode) {
        return mode == kSpectralFilter || mode == kSpectralPhaser || mode == kSpectralFlanger;
      }

      // Knob 0..1 -> -1..1 for bipolar modes, unchanged otherwise.
      static float effectiveAmount(int mode, float knob) {
        knob = std::max(0.0f, std::min(knob, 1.0f));
        return isBipolar(mode) ? 2.0f * knob - 1.0f : knob;
      }

      static bool isNeutral(int mode, float amount) {
        if (mode <= kNone || mode >= kNumModes)
          return true;
        if (isDrawable(mode))
          return false;
        return std::fabs(amount) < 1e-4f;
      }

      explicit SpectralMorphProcessor(bool uses_modulator) :
          Processor(kNumInputs, 1), uses_modulator_(uses_modulator) {
        for (int c = 0; c < 2; ++c) {
          channels_[c].in.assign(kMaxFftSize, 0.0f);
          channels_[c].mod.assign(kMaxFftSize, 0.0f);
          channels_[c].acc.assign(kMaxFftSize, 0.0f);
          channels_[c].history.assign(kHistoryFrames * (kMaxFftSize + 2), 0.0f);
          channels_[c].drift.assign(kMaxFftSize / 2 + 2, 0.0f);
          channels_[c].venv.assign(kMaxFftSize / 2 + 2, 0.0f);
          channels_[c].vavg.assign(kMaxFftSize / 2 + 2, 0.0f);
        }
        work_.assign(2 * kMaxFftSize + 4, 0.0f);
        mod_work_.assign(2 * kMaxFftSize + 4, 0.0f);
        mod_small_.assign(2 * 256 + 4, 0.0f);
        small_sum_.assign(130, 0.0f);
        window_small_.resize(256);
        for (int i = 0; i < 256; ++i)
          window_small_[i] = 0.5f * (1.0f - cosf(2.0f * kPi_ * i / 256.0f));
        mag_.assign(kMaxFftSize / 2 + 2, 0.0f);
        env_.assign(kMaxFftSize / 2 + 2, 0.0f);
        env2_.assign(kMaxFftSize / 2 + 2, 0.0f);
        vocode_power_[0].assign(kMaxFftSize / 2 + 3, 0.0f);
        vocode_power_[1].assign(kMaxFftSize / 2 + 3, 0.0f);
        prefix_.assign(kMaxFftSize / 2 + 3, 0.0f);
        phase_.assign(kMaxFftSize / 2 + 2, 0.0f);
        for (int i = 0; i < kNumFftSizes; ++i)
          transforms_[i] = std::make_shared<FourierTransform>(kMinFftBits + i);
        setFftSize(kDefaultFftIndex);
      }

      Processor* clone() const override { return new SpectralMorphProcessor(*this); }

      // Latency in samples at the current FFT size. Safe to call from any thread.
      int latencySamples() const { return latency_; }

      // Random Amplitudes: the seed picks the per-bin gain pattern, the shuffle input (0..1, modulatable) walks through
      // a chain of 8 further patterns derived from it, blending smoothly between neighbours. The dice input is a
      // switch: every time it flips (GUI press or host automation) the seed is scrambled to a new one.
      //
      // setSeed is for loading a preset or resetting: the new seed is the baseline, and the dice position it comes
      // with does not count as a flip. Safe to call from the message thread.
      void setSeed(uint32_t seed) {
        seed_ = seed;
        dice_sync_ = true;
      }

      uint32_t seed() const { return seed_; }

      static uint32_t scrambleSeed(uint32_t seed) {
        uint32_t x = seed * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        return (x >> 22u) ^ x;
      }

      // Drawn curves (0..1 over 20 Hz..20 kHz on a log axis) for Spectral Filter, Phaser and Flanger.
      void setCurve(int which, const LineGenerator* curve) { curves_[which] = curve; }

      void process(int num_samples) override {
        int size_index = static_cast<int>(input(kFftSize)->source->buffer[0][0] + 0.5f);
        size_index = std::max(0, std::min(size_index, kNumFftSizes - 1));
        if (size_index != fft_index_)
          setFftSize(size_index);

        int mode = static_cast<int>(input(kMode)->source->buffer[0][0] + 0.5f);
        mode = std::max(0, std::min(mode, kNumModes - 1));
        if (mode == kVocode && !uses_modulator_)
          mode = kNone;
        float amount = effectiveAmount(mode, input(kAmount)->source->buffer[0][0]);
        bool neutral = isNeutral(mode, amount);
        mode_ = mode;
        amount_ = amount;
        position_target_ = std::max(0.0f, std::min(input(kPosition)->source->buffer[0][0], 1.0f));
        shuffle_ = input(kShuffle)->source->buffer[0][0];
        gain_ = std::max(0.0f, std::min(input(kGain)->source->buffer[0][0], 1.0f));
        density_ = std::max(0.0f, std::min(input(kDensity)->source->buffer[0][0], 1.0f));
        resonance_ = std::max(0.0f, std::min(input(kResonance)->source->buffer[0][0], 1.0f));
        spread_ = std::max(0.0f, std::min(input(kSpread)->source->buffer[0][0], 1.0f));
        depth_ = std::max(0.0f, std::min(input(kDepth)->source->buffer[0][0], 1.0f));
        comb_phase_ = std::max(0.0f, std::min(input(kPhase)->source->buffer[0][0], 1.0f));
        if (uses_modulator_) {
          vocode_bands_ = std::max(4, std::min(static_cast<int>(input(kVocodeBands)->source->buffer[0][0] + 0.5f),
                                               int(kMaxVocodeBands)));
          // Attack and release arrive as log2 of seconds.
          vocode_attack_ = std::exp2(std::max(-12.0f, std::min(input(kVocodeAttack)->source->buffer[0][0], 1.0f)));
          vocode_release_ = std::exp2(std::max(-12.0f, std::min(input(kVocodeRelease)->source->buffer[0][0], 2.0f)));
          vocode_formant_ = std::max(-1.0f, std::min(input(kVocodeFormant)->source->buffer[0][0], 1.0f));
          vocode_flatten_ = std::max(0.0f, std::min(input(kVocodeFlatten)->source->buffer[0][0], 1.0f));
          vocode_noise_ = std::max(0.0f, std::min(input(kVocodeNoise)->source->buffer[0][0], 1.0f));
          vocode_emphasis_ = std::max(0.0f, std::min(input(kVocodeEmphasis)->source->buffer[0][0], 1.0f));
          vocode_gate_ = std::max(0.0f, std::min(input(kVocodeGate)->source->buffer[0][0], 1.0f));
          vocode_level_db_ = std::max(-12.0f, std::min(input(kVocodeLevel)->source->buffer[0][0], 24.0f));
          vocode_whiten_ = std::max(0.0f, std::min(input(kVocodeWhiten)->source->buffer[0][0], 1.0f));
        }
        float dice = input(kDice)->source->buffer[0][0] > 0.5f ? 1.0f : 0.0f;
        if (dice_sync_ || last_dice_ < 0.0f) {
          last_dice_ = dice;
          dice_sync_ = false;
        }
        else if (dice != last_dice_) {
          last_dice_ = dice;
          seed_ = scrambleSeed(seed_);
        }

        // The FFT only runs when it has something to do. With the mode neutral (and the dry/wet fade finished) the
        // frames are skipped altogether and the stage is just the delay line; the state of the modes is cleared so a
        // later switch starts fresh. Coming back, a few frames run first (kOverlap of them, enough to fill the
        // overlap-add) while the output is still the dry delay, then the wet signal fades in.
        if (neutral) {
          if (!idle_ && mix_ <= 0.0f) {
            idle_ = true;
            clearState();
          }
          warm_hops_ = 0;
        }
        else if (idle_) {
          idle_ = false;
          warm_hops_ = kOverlap;
        }

        // The dry/wet fade runs at a fixed rate per sample (64 samples end to end), whatever the host's block size.
        const float kMixStep = 1.0f / 64.0f;

        const poly_float* audio = input(kAudio)->source->buffer;
        const poly_float* modulator = uses_modulator_ ? input(kModulator)->source->buffer : nullptr;
        poly_float* out = output()->buffer;
        const int n = size_;
        const int hop = n / kOverlap;

        for (int i = 0; i < num_samples; ++i) {
          const float target_mix = (neutral || warm_hops_ > 0) ? 0.0f : 1.0f;
          mix_ += std::max(-kMixStep, std::min(target_mix - mix_, kMixStep));
          float result[2];
          for (int c = 0; c < 2; ++c) {
            Channel& ch = channels_[c];
            // Output first: what is read from the input ring is exactly n samples old.
            float wet = ch.acc[ch.read];
            ch.acc[ch.read] = 0.0f;
            ch.read = (ch.read + 1) % n;
            float dry = ch.in[ch.write];

            ch.in[ch.write] = audio[i][c];
            ch.mod[ch.write] = modulator ? modulator[i][c] : 0.0f;
            ch.write = (ch.write + 1) % n;
            if (c == 0) {
              float magnitude = std::fabs(audio[i][0]) + std::fabs(audio[i][1]);
              if (modulator)
                magnitude += std::fabs(modulator[i][0]) + std::fabs(modulator[i][1]);
              silent_run_ = magnitude < 1.0e-10f ? std::min(silent_run_ + 1, 1 << 30) : 0;
            }
            result[c] = dry + (wet - dry) * mix_;
          }
          out[i] = poly_float(result[0], result[1]);

          if (++hop_count_ >= hop) {
            hop_count_ = 0;
            // Skipped: a neutral stage, and a whole ring of digital silence (every frame would be silent too).
            if (!idle_ && silent_run_ < n) {
              processFrame(0);
              processFrame(1);
              if (warm_hops_ > 0)
                --warm_hops_;
            }
            // The position is a modulatable parameter (an LFO can drive it); glide towards it between frames.
            position_ += (position_target_ - position_) * 0.4f;
          }
        }
      }

    private:
      static constexpr float kPi_ = 3.14159265358979323846f;

      static constexpr int kHistoryFrames = 16;
      static constexpr int kMaxVocodeBands = 64;

      struct Channel {
        std::vector<float> in, mod, acc;
        // Spectral Time Skew: the last frames' spectra. Phase Disperse: each bin's accumulated phase drift.
        std::vector<float> history, drift;
        // Vocode: the follower level of each band.
        std::vector<float> venv;
        // Vocode: the slow average level of each bin of the modulator (for the whitening).
        std::vector<float> vavg;
        bool vavg_valid = false;
        int write = 0;
        int read = 0;
        int history_pos = 0;
        int history_filled = 0;
      };

      // Drops everything the modes keep between frames (not the audio ring, which is the delay line).
      void clearState() {
        for (Channel& ch : channels_) {
          std::fill(ch.acc.begin(), ch.acc.end(), 0.0f);
          std::fill(ch.history.begin(), ch.history.end(), 0.0f);
          std::fill(ch.drift.begin(), ch.drift.end(), 0.0f);
          std::fill(ch.venv.begin(), ch.venv.end(), 0.0f);
          std::fill(ch.vavg.begin(), ch.vavg.end(), 0.0f);
          ch.vavg_valid = false;
          ch.history_pos = 0;
          ch.history_filled = 0;
        }
      }

      void setFftSize(int index) {
        fft_index_ = index;
        size_ = fftSizeFromIndex(index);
        for (Channel& ch : channels_) {
          std::fill(ch.in.begin(), ch.in.end(), 0.0f);
          std::fill(ch.mod.begin(), ch.mod.end(), 0.0f);
          std::fill(ch.acc.begin(), ch.acc.end(), 0.0f);
          std::fill(ch.history.begin(), ch.history.end(), 0.0f);
          std::fill(ch.drift.begin(), ch.drift.end(), 0.0f);
          std::fill(ch.venv.begin(), ch.venv.end(), 0.0f);
          std::fill(ch.vavg.begin(), ch.vavg.end(), 0.0f);
          ch.vavg_valid = false;
          ch.history_pos = 0;
          ch.history_filled = 0;
          ch.write = 0;
          ch.read = 0;
        }
        hop_count_ = 0;
        window_.resize(size_);
        for (int i = 0; i < size_; ++i)
          window_[i] = 0.5f * (1.0f - cosf(2.0f * kPi_ * i / size_));
        latency_ = size_;
      }

      static void fillRandomTable(std::vector<float>& table, uint32_t seed) {
        table.resize(kMaxFftSize / 2 + 2);
        uint32_t state = 0x9E3779B9u * (seed + 1u);
        for (float& r : table) {
          state = state * 1664525u + 1013904223u;
          r = (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }
      }

      // Drawn curve value (0..1) at bin k: the curve's x axis is log frequency, 20 Hz .. 20 kHz.
      // shift_octaves moves the curve along the frequency axis (the value at f is the curve's value at f / 2^shift).
      float curveAt(int which, int k, int bins, float sample_rate, float shift_octaves = 0.0f) const {
        const LineGenerator* curve = curves_[which];
        if (curve == nullptr)
          return defaultCurve(which, k, bins, sample_rate);
        float frequency = k * 0.5f * sample_rate / bins;
        float x = (std::log2(std::max(frequency, 20.0f) / 20.0f) - shift_octaves) / 9.96578428f;  // log2(1000)
        x = std::max(0.0f, std::min(x, 1.0f));
        const float* buffer = curve->getBuffer();
        float position = x * (curve->resolution() - 1);
        int i = static_cast<int>(position);
        i = std::min(i, curve->resolution() - 2);
        float t = position - i;
        return buffer[i] + (buffer[i + 1] - buffer[i]) * t;
      }

      // The drawn curve, or the same curve flipped upside down (value 1 - v) for the negative half of the knob.
      float drawnCurve(int which, int k, int bins, float sample_rate, bool inverted, float shift_octaves = 0.0f) const {
        float value = curveAt(which, k, bins, sample_rate, shift_octaves);
        return inverted ? 1.0f - value : value;
      }

      static float defaultCurve(int which, int k, int bins, float sample_rate) {
        if (which == 0)
          return 0.5f;
        if (which == 2)
          return 0.5f;
        float frequency = k * 0.5f * sample_rate / bins;
        float x = std::log2(std::max(frequency, 20.0f) / 20.0f) / 9.96578428f;
        return std::max(0.0f, std::min(x, 1.0f));
      }

      // Table number `index` of the shuffle chain for the current seed (cached in two slots by parity).
      const std::vector<float>& randomTable(int index, uint32_t seed) {
        int slot = index & 1;
        if (!random_valid_[slot] || random_seed_[slot] != seed || random_index_[slot] != index) {
          fillRandomTable(random_[slot], seed + static_cast<uint32_t>(index) * 0x632BE5ABu);
          random_seed_[slot] = seed;
          random_index_[slot] = index;
          random_valid_[slot] = true;
        }
        return random_[slot];
      }

      // Same pattern in Hz at every FFT size: bin k of `bins` reads the table at its relative position.
      static float randomAt(const std::vector<float>& a, const std::vector<float>& b, float t, int k, int bins) {
        int i = k * (kMaxFftSize / 2) / bins;
        return a[i] + (b[i] - a[i]) * t;
      }

      // Smoothed magnitude envelope (RMS over +-half_width bins) of mag[0..bins] into env.
      void smoothEnvelope(const float* mag, float* env, int bins, int half_width) {
        prefix_[0] = 0.0f;
        for (int k = 0; k <= bins; ++k)
          prefix_[k + 1] = prefix_[k] + mag[k] * mag[k];
        for (int k = 0; k <= bins; ++k) {
          int lo = std::max(0, k - half_width);
          int hi = std::min(bins, k + half_width);
          float mean = (prefix_[hi + 1] - prefix_[lo]) / (hi - lo + 1);
          env[k] = sqrtf(std::max(mean, 0.0f));
        }
      }

      // value of a per-bin table at a fractional bin.
      static float sampleAt(const float* table, int bins, float position) {
        if (position <= 0.0f)
          return table[0];
        if (position >= bins)
          return table[bins];
        int i = static_cast<int>(position);
        float t = position - i;
        return table[i] + (table[i + 1] - table[i]) * t;
      }

      // Rebuilds the spectrum so that bin k takes its content from bin source(k).
      // mag_/phase_ must hold the input's polar form; writes cartesian back into spec.
      template<class SourceFn>
      void gatherBins(float* spec, int bins, SourceFn source) {
        std::copy(mag_.begin(), mag_.begin() + bins + 1, env2_.begin());
        for (int k = 0; k <= bins; ++k) {
          float src = source(k);
          float m = (src > bins + 0.5f || src < 0.0f) ? 0.0f : sampleAt(env2_.data(), bins, src);
          int nearest = std::max(0, std::min(bins, static_cast<int>(src + 0.5f)));
          float ph = phase_[nearest];
          spec[2 * k] = m * cosf(ph);
          spec[2 * k + 1] = m * sinf(ph);
        }
        spec[1] = 0.0f;
        spec[2 * bins + 1] = 0.0f;
      }

      float position() const { return position_; }

      void rotate(float* spec, int k, float phase) {
        float c = cosf(phase), s = sinf(phase);
        float re = spec[2 * k], im = spec[2 * k + 1];
        spec[2 * k] = re * c - im * s;
        spec[2 * k + 1] = re * s + im * c;
      }

      void processFrame(int c) {
        Channel& ch = channels_[c];
        const int n = size_;
        const int bins = n / 2;
        FourierTransform* fft = transforms_[fft_index_].get();

        for (int i = 0; i < n; ++i)
          work_[i] = window_[i] * ch.in[(ch.write + i) % n];
        std::fill(work_.begin() + n, work_.begin() + 2 * n, 0.0f);
        fft->transformRealForward(work_.data());

        float* spec = work_.data();
        float a = amount_;
        float sample_rate = std::max(1.0f, static_cast<float>(getSampleRate()));
        float nyquist = 0.5f * sample_rate;

        auto computePolar = [&](float* s) {
          for (int k = 0; k <= bins; ++k) {
            float re = s[2 * k], im = s[2 * k + 1];
            mag_[k] = sqrtf(re * re + im * im);
            phase_[k] = atan2f(im, re);
          }
        };

        switch (mode_) {
        case kFormantScale: {
          computePolar(spec);
          int half = std::max(2, n / 64);
          smoothEnvelope(mag_.data(), env_.data(), bins, half);
          float ratio = powf(2.0f, a);
          for (int k = 0; k <= bins; ++k) {
            float shifted = sampleAt(env_.data(), bins, k / ratio);
            float gain = shifted / (env_[k] + 1e-9f);
            gain = std::max(0.0f, std::min(gain, float(kMaxGain)));
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
          }
          break;
        }
        case kHarmonicStretch: {
          computePolar(spec);
          float ratio = powf(2.0f, a);
          gatherBins(spec, bins, [ratio](int k) { return k / ratio; });
          break;
        }
        case kInharmonicStretch: {
          computePolar(spec);
          float ratio = powf(2.0f, a);
          float top = std::log2(static_cast<float>(bins));
          gatherBins(spec, bins, [ratio, top](int k) {
            if (k < 1)
              return 0.0f;
            return k / powf(ratio, std::log2(static_cast<float>(k)) / top);
          });
          break;
        }
        case kSmear: {
          float smear = std::min(a, 1.0f) * 0.98f;
          computePolar(spec);
          float amplitude = mag_[0] * (1.0f - smear);
          mag_[0] = amplitude;
          for (int k = 1; k <= bins; ++k) {
            float original = mag_[k];
            amplitude = original + (amplitude - original) * smear;
            mag_[k] = amplitude;
            amplitude *= (k + 0.25f) / k;
          }
          for (int k = 0; k <= bins; ++k) {
            spec[2 * k] = mag_[k] * cosf(phase_[k]);
            spec[2 * k + 1] = mag_[k] * sinf(phase_[k]);
          }
          spec[1] = 0.0f;
          spec[2 * bins + 1] = 0.0f;
          break;
        }
        case kRandomAmplitudes: {
          float place = std::max(0.0f, std::min(shuffle_, 1.0f)) * 8.0f;
          int first = std::min(static_cast<int>(place), 7);
          float blend = place - first;
          uint32_t seed = seed_;
          const std::vector<float>& table_a = randomTable(first, seed);
          const std::vector<float>& table_b = randomTable(first + 1, seed);
          for (int k = 0; k <= bins; ++k) {
            float gain = (1.0f + a) * std::max(1.0f - a - a * randomAt(table_a, table_b, blend, k, bins), 0.0f);
            gain = std::min(gain, float(kMaxGain));
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
          }
          break;
        }
        case kLowPass:
        case kHighPass: {
          float low_hz = 20.0f;
          float cutoff_hz = mode_ == kLowPass ? nyquist * powf(low_hz / nyquist, a)
                                              : low_hz * powf(nyquist / low_hz, a);
          float cutoff_bin = cutoff_hz / nyquist * bins;
          float width = std::max(2.0f, n / 256.0f);
          for (int k = 0; k <= bins; ++k) {
            float t = std::max(0.0f, std::min((cutoff_bin - k) / width + 0.5f, 1.0f));
            float gain = mode_ == kLowPass ? t : 1.0f - t;
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
          }
          break;
        }
        case kPhaseDisperse:
        case kTimeSkew: {
          // Frequency-dependent group delay, up to 15 hops (about 4 FFT sizes) at full amount. Each bin is read
          // from an older frame (whole hops) and the remainder is a linear phase, so the delay is continuous.
          //   Time Skew:      delay grows with the log of the frequency (highs lag behind the lows).
          //   Phase Disperse: a strong chirp that works both ways: up, the highs are late (delay ~ sqrt of the
          //                   frequency); down, the lows are late. On transients it turns a click into a sweep.
          int stride = n + 2;
          float* slot = ch.history.data() + ch.history_pos * stride;
          std::copy(spec, spec + stride, slot);
          const int hop = n / kOverlap;
          float max_delay = static_cast<float>((kHistoryFrames - 1) * hop);
          float log_norm = 1.0f / std::log2(1.0f + bins);
          for (int k = 1; k <= bins; ++k) {
            float curve;
            if (mode_ == kTimeSkew)
              curve = std::log2(1.0f + k) * log_norm;
            else {
              float root = sqrtf(static_cast<float>(k) / bins);
              curve = a >= 0.0f ? root : 1.0f - root;
            }
            float delay = std::fabs(a) * max_delay * curve;
            int hops = std::min(static_cast<int>(delay / hop), ch.history_filled);
            float remainder = mode_ == kTimeSkew ? 0.0f : delay - static_cast<float>(static_cast<int>(delay / hop)) * hop;
            int index = (ch.history_pos - hops + kHistoryFrames) % kHistoryFrames;
            const float* old = ch.history.data() + index * stride;
            spec[2 * k] = old[2 * k];
            spec[2 * k + 1] = old[2 * k + 1];
            if (remainder > 0.0f)
              rotate(spec, k, -2.0f * kPi_ * k * remainder / n);
          }
          ch.history_pos = (ch.history_pos + 1) % kHistoryFrames;
          ch.history_filled = std::min(ch.history_filled + 1, kHistoryFrames - 1);
          break;
        }
        case kSpectralFilter: {
          // Gain in dB per bin. Morph 0: a narrow, strongly resonant peak (+24 dB, the rest of the spectrum pulled down
          // 24 dB) whose frequency follows the position slider (100 Hz .. 12 kHz). Morph 1: the drawn curve (+-48 dB,
          // the middle is flat), moved up and down in frequency by the position slider.
          float morph = std::fabs(a);
          bool inverted = a < 0.0f;
          float centre = 100.0f * powf(120.0f, position());
          // The position slider also transposes the drawn curve along the frequency axis, +-4 octaves (centre: as drawn).
          float shift = (position() - 0.5f) * 8.0f;
          // Detail controls: the automatic peak's width (0.075 .. 1.2 octaves, 0.3 by default) and height
          // (0 .. 48 dB, 24 by default), and the output level (0 .. 2x).
          float width = 0.3f * powf(2.0f, (density_ - 0.5f) * 4.0f);
          float peak_db = 48.0f * resonance_;
          float out_gain = 2.0f * gain_;
          for (int k = 0; k <= bins; ++k) {
            float frequency = std::max(k * 0.5f * sample_rate / bins, 1.0f);
            float octaves = std::log2(frequency / centre) / width;
            float automatic = 2.0f * peak_db * expf(-0.5f * octaves * octaves) - peak_db;
            float drawn = (drawnCurve(0, k, bins, sample_rate, inverted, shift) - 0.5f) * 96.0f;
            float db = automatic + (drawn - automatic) * morph;
            float gain = std::min(powf(10.0f, db / 20.0f), 64.0f) * out_gain;
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
          }
          break;
        }
        case kSpectralTable: {
          // The knob picks a frame of the built-in table of filter responses (frame 0 is flat, so zero is neutral). The
          // position slider transposes the response along the frequency axis, +-4 octaves; Gain is the output level.
          float shift = (position() - 0.5f) * 8.0f;
          float out_gain = 2.0f * gain_;
          float depth = 2.0f * density_;
          float table_position = std::max(0.0f, std::min(a, 1.0f));
          for (int k = 0; k <= bins; ++k) {
            float frequency = std::max(k * 0.5f * sample_rate / bins, 1.0f);
            float octave = std::log2(frequency / FxTables::kMinHz) - shift;
            float db = FxTables::spectralDb(octave, table_position) * depth;
            float gain = std::min(powf(10.0f, db / 20.0f), 64.0f) * out_gain;
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
          }
          break;
        }
        case kSpectralPhaser: {
          // A resonant phaser: every bin goes through H = (1 + e^(j phi)) / (1 - r e^(j phi)), the response of a
          // phase-rotating feedback loop. Where phi is half a turn the bin is cancelled (a notch), where it is a whole
          // turn the bin is lifted (a resonant peak), so the result is a row of deep notches between sharp peaks.
          // phi = 2 pi (turns * c(f) + sweep): c is the drawn curve (0..1) and `turns` (6) sets how many notch/peak
          // pairs a full-height curve spans, so steep parts of the curve pack the notches tightly and flat parts
          // spread them out. The position slider is the sweep: moving it from 0 to 1 slides every notch onto the
          // next one, which is the phaser sweep (modulate it with an LFO). Morph 0: the curve is a log-frequency ramp
          // (an evenly spread classic phaser); morph 1: the drawn curve alone.
          // Detail controls: density = notch count (1.5 .. 24 turns, 6 by default), resonance = the feedback (0 .. 0.95,
          // 0.7 by default), gain = output level.
          const float turns = 6.0f * powf(2.0f, (density_ - 0.5f) * 4.0f);
          const float feedback = std::min(0.95f, 1.4f * resonance_);
          const float norm = sqrtf((1.0f - feedback * feedback) * 0.5f) * 2.0f * gain_;   // unit power for noise
          float morph = std::fabs(a);
          bool inverted = a < 0.0f;
          float sweep = position();
          for (int k = 1; k < bins; ++k) {
            float ramp = defaultCurve(1, k, bins, sample_rate);
            float drawn = drawnCurve(1, k, bins, sample_rate, inverted);
            float curve = ramp + (drawn - ramp) * morph;
            float phi = 2.0f * kPi_ * (turns * curve + sweep);
            float c = cosf(phi), sn = sinf(phi);
            float num_re = 1.0f + c, num_im = sn;
            float den_re = 1.0f - feedback * c, den_im = -feedback * sn;
            float den_norm = norm / (den_re * den_re + den_im * den_im);
            float h_re = (num_re * den_re + num_im * den_im) * den_norm;
            float h_im = (num_im * den_re - num_re * den_im) * den_norm;
            float re = spec[2 * k], im = spec[2 * k + 1];
            spec[2 * k] = re * h_re - im * h_im;
            spec[2 * k + 1] = re * h_im + im * h_re;
          }
          break;
        }
        case kSpectralFlanger: {
          // A bank of regenerating combs, H = (1 + d e^(j theta)) / (1 - r e^(j theta)) with theta = -2 pi f t + phase.
          //  - THE DRAWING is the delay time per frequency: the curve multiplies the delay by 1/4 .. 4 (the middle is
          //    x1, a plain comb). A ramp bends the comb so the notches crowd or spread along the spectrum, a bump
          //    pinches them around one frequency: the sweep gets a shape that no ordinary flanger has. Knob centre: the
          //    plain comb; up: your curve; down: the curve flipped.
          //  - t sweeps exponentially from 0.1 ms to 15 ms with the position slider (the right channel's delay is up to
          //    30% longer: spread).
          //  - d is the depth and r = resonance * d the regeneration (up to 0.95): deep notches and ringing peaks.
          //  - Taps (density) cascade 1 to 4 combs whose delays are incommensurate (x1, x1.37, x1.83, x2.41), so the
          //    notches multiply into a dense, metallic lattice.
          //  - Phase offsets the comb: 0 = positive (peaks at 0 Hz), 50% = negative (notches at 0 Hz, the through-zero
          //    flange), and moving it slides the whole comb up the spectrum.
          float morph = std::fabs(a);
          bool inverted = a < 0.0f;
          const float base_delay = 0.0001f * powf(150.0f, position()) * sample_rate * (c == 0 ? 1.0f : 1.0f + 0.3f * spread_);
          const float depth = std::min(1.0f, 1.6f * depth_);
          const float resonance = std::min(0.95f, 1.6f * resonance_) * depth;
          const int taps = 1 + std::min(3, static_cast<int>(density_ * 4.0f));
          const float theta_offset = 2.0f * kPi_ * comb_phase_;
          const float out_gain = 2.0f * gain_;
          // Each stage is scaled to keep the power of noise through it (peaks above 1, notches far below).
          const float stage_norm = sqrtf((1.0f - resonance * resonance) / (1.0f + depth * depth));
          static const float kTapRatio[4] = { 1.0f, 1.37f, 1.83f, 2.41f };
          for (int k = 1; k < bins; ++k) {
            float curve = drawnCurve(2, k, bins, sample_rate, inverted);
            float delay = base_delay * exp2f((curve - 0.5f) * 4.0f * morph);
            float h_re = out_gain, h_im = 0.0f;
            for (int tap = 0; tap < taps; ++tap) {
              float angle = -2.0f * kPi_ * k * delay * kTapRatio[tap] / n + theta_offset;
              float cs = cosf(angle), sn = sinf(angle);
              float num_re = 1.0f + depth * cs, num_im = depth * sn;
              float den_re = 1.0f - resonance * cs, den_im = -resonance * sn;
              float stage = stage_norm / (den_re * den_re + den_im * den_im);
              float f_re = (num_re * den_re + num_im * den_im) * stage;
              float f_im = (num_im * den_re - num_re * den_im) * stage;
              float next_re = h_re * f_re - h_im * f_im;
              h_im = h_re * f_im + h_im * f_re;
              h_re = next_re;
            }
            float re = spec[2 * k], im = spec[2 * k + 1];
            spec[2 * k] = re * h_re - im * h_im;
            spec[2 * k + 1] = re * h_im + im * h_re;
          }
          break;
        }
        case kShepardTone: {
          computePolar(spec);
          float up_weight = 0.5f * (1.0f + cosf(kPi_ * a));
          float up_octaves = a;
          float down_octaves = a - 1.0f;
          std::copy(mag_.begin(), mag_.begin() + bins + 1, env_.begin());
          for (int k = 0; k <= bins; ++k) {
            float up_src = k / powf(2.0f, up_octaves);
            float down_src = k / powf(2.0f, down_octaves);
            float up_mag = up_src > bins ? 0.0f : sampleAt(env_.data(), bins, up_src);
            float down_mag = down_src > bins ? 0.0f : sampleAt(env_.data(), bins, down_src);
            int up_i = std::max(0, std::min(bins, static_cast<int>(up_src + 0.5f)));
            int down_i = std::max(0, std::min(bins, static_cast<int>(down_src + 0.5f)));
            float re = up_weight * up_mag * cosf(phase_[up_i]) + (1.0f - up_weight) * down_mag * cosf(phase_[down_i]);
            float im = up_weight * up_mag * sinf(phase_[up_i]) + (1.0f - up_weight) * down_mag * sinf(phase_[down_i]);
            spec[2 * k] = re;
            spec[2 * k + 1] = im;
          }
          spec[1] = 0.0f;
          spec[2 * bins + 1] = 0.0f;
          break;
        }
        case kVocode: {
          // Cross-synthesis with smooth spectral envelopes. The modulator's envelope (the other lane) is measured per
          // bin over a constant-Q window (the Bands setting: 7.6 octaves / bands wide, never narrower than 3 bins), run
          // through a per-bin attack/release follower, and imposed on this lane (the carrier).
          for (int i = 0; i < n; ++i)
            mod_work_[i] = window_[i] * ch.mod[(ch.write + i) % n];
          std::fill(mod_work_.begin() + n, mod_work_.begin() + 2 * n, 0.0f);
          fft->transformRealForward(mod_work_.data());

          float* carrier_sum = vocode_power_[0].data();
          float* modulator_sum = vocode_power_[1].data();
          carrier_sum[0] = modulator_sum[0] = 0.0f;
          for (int k = 0; k <= bins; ++k) {
            float cr = spec[2 * k], ci = spec[2 * k + 1];
            float mr = mod_work_[2 * k], mi = mod_work_[2 * k + 1];
            carrier_sum[k + 1] = carrier_sum[k] + cr * cr + ci * ci;
            modulator_sum[k + 1] = modulator_sum[k] + mr * mr + mi * mi;
          }
          auto rangeLevel = [](const float* sum, int lo, int hi) {
            return sqrtf(std::max(sum[hi] - sum[lo], 0.0f) / std::max(hi - lo, 1));
          };

          const float bin_width = nyquist / bins;
          const float window_ratio = exp2f(0.5f * 7.6f / vocode_bands_);
          const float whole_level = rangeLevel(carrier_sum, 0, bins + 1);
          const float flatten = vocode_flatten_;
          const float depth = std::min(a, 1.0f);
          const float level_gain = exp2f(vocode_level_db_ / 6.0206f);

          // Constant-Q envelopes of the modulator (env2_) and of the carrier (env_). The modulator is measured twice:
          // with this stage's FFT, which resolves the lows (the voice's pitch and first formant), and with a short
          // 256-point FFT centred on the same moment, which resolves time (consonants, plosives) in the highs. They
          // are crossfaded between 400 and 800 Hz.
          for (int k = 0; k <= bins; ++k) {
            int lo = std::max(0, std::min(static_cast<int>(k / window_ratio), k - 1));
            int hi = std::min(bins, std::max(static_cast<int>(std::ceil(k * window_ratio)), k + 1));
            env2_[k] = sqrtf(std::max(modulator_sum[hi + 1] - modulator_sum[lo], 0.0f) / (hi - lo + 1));
            env_[k] = sqrtf(std::max(carrier_sum[hi + 1] - carrier_sum[lo], 0.0f) / (hi - lo + 1));
          }
          if (n > 256) {
            for (int i = 0; i < 256; ++i)
              mod_small_[i] = window_small_[i] * ch.mod[(ch.write + n / 2 - 128 + i) % n];
            std::fill(mod_small_.begin() + 256, mod_small_.begin() + 512, 0.0f);
            transforms_[0]->transformRealForward(mod_small_.data());
            small_sum_[0] = 0.0f;
            for (int j = 0; j <= 128; ++j)
              small_sum_[j + 1] = small_sum_[j] + mod_small_[2 * j] * mod_small_[2 * j] + mod_small_[2 * j + 1] * mod_small_[2 * j + 1];
            const float small_to_stage = sqrtf(n / 256.0f);   // magnitudes of the short FFT in this stage's units
            const float small_width = nyquist / 128.0f;
            for (int k = 0; k <= bins; ++k) {
              float frequency = k * bin_width;
              float fast = std::max(0.0f, std::min((frequency - 400.0f) / 400.0f, 1.0f));
              if (fast <= 0.0f)
                continue;
              float centre = frequency / small_width;
              float half = std::max(1.5f, centre * (window_ratio - 1.0f));
              int lo = std::max(0, static_cast<int>(centre - half));
              int hi = std::min(128, std::max(static_cast<int>(std::ceil(centre + half)), lo + 1));
              float level = small_to_stage * sqrtf(std::max(small_sum_[hi + 1] - small_sum_[lo], 0.0f) / (hi - lo + 1));
              env2_[k] += (level - env2_[k]) * fast;
            }
          }

          // Pre-emphasis of the modulator: speech keeps little energy in the highs, which carry the consonants.
          if (vocode_emphasis_ > 0.0f) {
            for (int k = 1; k <= bins; ++k)
              env2_[k] *= exp2f(vocode_emphasis_ * log2f(std::max(k * bin_width, 300.0f) / 1000.0f));
          }

          // Gate: below a threshold under the modulator's recent peak the whole envelope fades out (breath, hum).
          if (vocode_gate_ > 0.0f) {
            int speech_lo = std::min(bins, static_cast<int>(100.0f / bin_width));
            int speech_hi = std::min(bins + 1, static_cast<int>(8000.0f / bin_width) + 1);
            float frame_level = rangeLevel(modulator_sum, speech_lo, std::max(speech_hi, speech_lo + 1));
            float hop_seconds_peak = static_cast<float>(n / kOverlap) / sample_rate;
            vocode_peak_ = std::max(frame_level, vocode_peak_ * expf(-hop_seconds_peak / 2.0f));
            float threshold = vocode_peak_ * powf(10.0f, -(80.0f - 50.0f * vocode_gate_) / 20.0f);
            float open = std::max(0.0f, std::min((frame_level - 0.5f * threshold) / std::max(0.5f * threshold, 1e-9f), 1.0f));
            for (int k = 0; k <= bins; ++k)
              env2_[k] *= open;
          }

          // Whitening. A voice (or any real modulator) falls off steeply towards the highs, and a vocoder multiplies
          // that onto a carrier that already falls off, which is what makes the result muffled. Each bin's level is
          // compared with its own slow average (about a second) and the spectrum is tilted back towards its overall
          // average, so quiet bands count as much as loud ones and what the modulator does *over time* in each
          // band (the words) is kept. The gate keeps a silent modulator silent.
          if (vocode_whiten_ > 0.0f) {
            const float average_coef = expf(-static_cast<float>(n / kOverlap) / (sample_rate * 1.0f));
            float* average = ch.vavg.data();
            if (!ch.vavg_valid) {
              for (int k = 0; k <= bins; ++k)
                average[k] = env2_[k];
              ch.vavg_valid = true;
            }
            const float eps = 1e-6f * std::max(whole_level, 1e-6f) + 1e-12f;
            const int first = std::max(1, std::min(bins, static_cast<int>(100.0f / bin_width)));
            const int last = std::max(first + 1, std::min(bins, static_cast<int>(12000.0f / bin_width)));
            float log_sum = 0.0f;
            for (int k = 0; k <= bins; ++k)
              average[k] = env2_[k] + (average[k] - env2_[k]) * average_coef;
            for (int k = first; k <= last; ++k)
              log_sum += log2f(average[k] + eps);
            const float centre = log_sum / (last - first + 1);
            for (int k = 0; k <= bins; ++k) {
              float tilt = (centre - log2f(average[k] + eps)) * vocode_whiten_;
              env2_[k] *= exp2f(std::max(-2.0f, std::min(tilt, 3.0f)));   // at most 12 dB down / 18 dB up
            }
          }

          // Unvoiced sounds (s, f, sh, t): when the modulator has more energy in the highs than in the lows, its frame is
          // mostly noise, so the carrier's highs (a buzz) give way to hiss. Smoothed over a few frames.
          float unvoiced = 0.0f;
          if (vocode_noise_ > 0.0f) {
            int low_a = std::min(bins, static_cast<int>(100.0f / bin_width)), low_b = std::min(bins + 1, static_cast<int>(1200.0f / bin_width) + 1);
            int high_a = std::min(bins, static_cast<int>(3000.0f / bin_width)), high_b = std::min(bins + 1, static_cast<int>(8000.0f / bin_width) + 1);
            float low_sum = 0.0f, high_sum = 0.0f;
            for (int k = low_a; k < std::max(low_b, low_a + 1); ++k)
              low_sum += env2_[k];
            for (int k = high_a; k < std::max(high_b, high_a + 1); ++k)
              high_sum += env2_[k];
            float low_mean = low_sum / std::max(low_b - low_a, 1), high_mean = high_sum / std::max(high_b - high_a, 1);
            float target = std::max(0.0f, std::min((high_mean / std::max(low_mean, 1e-9f) - 0.35f) * 2.2f, 1.0f));
            if (low_mean + high_mean < 1e-6f * std::max(whole_level, 1e-9f))
              target = 0.0f;
            vocode_unvoiced_[c] += (target - vocode_unvoiced_[c]) * 0.5f;
            unvoiced = vocode_unvoiced_[c];
          }

          // Per-bin followers.
          const float hop_seconds = static_cast<float>(n / kOverlap) / sample_rate;
          const float attack_coef = expf(-hop_seconds / std::max(vocode_attack_, 1e-4f));
          const float release_coef = expf(-hop_seconds / std::max(vocode_release_, 1e-4f));
          float* follower = ch.venv.data();
          for (int k = 0; k <= bins; ++k) {
            float level = env2_[k];
            follower[k] = level + (follower[k] - level) * (level > follower[k] ? attack_coef : release_coef);
          }

          // Formant shift: this bin reads the modulator's envelope at k / ratio.
          const float ratio = powf(2.0f, vocode_formant_);
          const float floor_level = 0.03f * whole_level;
          for (int k = 0; k <= bins; ++k) {
            float modulator_level = sampleAt(follower, bins, k / ratio);
            // flatten 0: the carrier is scaled by the modulator's level; flatten 1: it is levelled first, so every bin
            // carries the modulator's level whatever the carrier does there.
            float reference = powf(std::max(env_[k], floor_level), flatten) * powf(std::max(whole_level, 1e-9f), 1.0f - flatten);
            float gain = std::min(modulator_level / std::max(reference, 1e-9f), 4.0f * float(kMaxGain)) * level_gain;
            gain = exp2f(log2f(std::max(gain, 1e-4f)) * depth);
            float high_fade = std::max(0.0f, std::min((k * bin_width - 1500.0f) / 3500.0f, 1.0f));
            gain *= 1.0f - 0.75f * unvoiced * high_fade * std::min(1.0f, 2.0f * vocode_noise_);
            float re = spec[2 * k] * gain, im = spec[2 * k + 1] * gain;

            // Consonant noise: a hiss as loud as the modulator's envelope, fading in between 1.5 kHz and 5 kHz.
            if (vocode_noise_ > 0.0f) {
              float fade = std::max(0.0f, std::min((k * bin_width - 1500.0f) / 3500.0f, 1.0f));
              if (fade > 0.0f) {
                vocode_rng_ = vocode_rng_ * 1664525u + 1013904223u;
                float angle = (vocode_rng_ >> 8) * (2.0f * kPi_ / 16777216.0f);
                float magnitude = 1.0f * vocode_noise_ * (1.0f + 2.5f * unvoiced) * fade * modulator_level * level_gain * depth;
                re += magnitude * cosf(angle);
                im += magnitude * sinf(angle);
              }
            }
            spec[2 * k] = re;
            spec[2 * k + 1] = im;
          }
          break;
        }
        default:
          break;
        }

        spec[1] = 0.0f;
        spec[2 * bins + 1] = 0.0f;
        fft->transformRealInverse(work_.data());

        // Hann * Hann at 75% overlap sums to 1.5.
        const float scale = 1.0f / 1.5f;
        for (int i = 0; i < n; ++i)
          ch.acc[(ch.read + i) % n] += work_[i] * window_[i] * scale;
      }

      bool uses_modulator_;
      int fft_index_ = -1;
      int size_ = 1024;
      int hop_count_ = 0;
      bool idle_ = true;       // neutral: no FFT frames run
      int warm_hops_ = 0;      // frames still to run before the wet signal fades in
      int silent_run_ = 0;     // consecutive samples of digital silence on the inputs
      int mode_ = kNone;
      float amount_ = 0.0f;
      float mix_ = 0.0f;
      float position_target_ = 0.5f;
      float position_ = 0.5f;
      uint32_t seed_ = kDefaultSeed;
      float shuffle_ = 0.0f;
      float gain_ = 0.5f, density_ = 0.5f, resonance_ = 0.5f, spread_ = 0.5f, depth_ = 0.5f, comb_phase_ = 0.0f;
      int vocode_bands_ = 24;
      float vocode_attack_ = 0.004f;
      float vocode_release_ = 0.06f;
      float vocode_formant_ = 0.0f;
      float vocode_flatten_ = 0.5f;
      float vocode_noise_ = 0.5f;
      float vocode_emphasis_ = 0.5f;
      float vocode_whiten_ = 0.5f;
      float vocode_gate_ = 0.0f;
      float vocode_level_db_ = 0.0f;
      float vocode_peak_ = 0.0f;
      uint32_t vocode_rng_ = 12345u;
      float last_dice_ = -1.0f;
      bool dice_sync_ = false;
      bool random_valid_[2] = { false, false };
      uint32_t random_seed_[2] = { 0, 0 };
      int random_index_[2] = { 0, 0 };
      const LineGenerator* curves_[3] = { nullptr, nullptr, nullptr };
      int latency_ = 1024;
      Channel channels_[2];
      std::vector<float> window_small_, mod_small_, small_sum_;
      float vocode_unvoiced_[2] = { 0.0f, 0.0f };
      std::vector<float> window_, work_, mod_work_, mag_, env_, env2_, prefix_, phase_;
      std::vector<float> random_[2];
      std::vector<float> vocode_power_[2];   // prefix sums of the power of the carrier [0] and the modulator [1]
      std::shared_ptr<FourierTransform> transforms_[kNumFftSizes];

      JUCE_LEAK_DETECTOR(SpectralMorphProcessor)
  };
} // namespace vital
