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
        // Vocode settings (read by the main lane only).
        kVocodeBands,
        kVocodeAttack,
        kVocodeRelease,
        kVocodeFormant,
        kVocodeFlatten,
        kVocodeNoise,
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
        kNumModes
      };

      static constexpr uint32_t kDefaultSeed = 4;
      static constexpr int kNumFftSizes = 4;
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
        }
        work_.assign(2 * kMaxFftSize + 4, 0.0f);
        mod_work_.assign(2 * kMaxFftSize + 4, 0.0f);
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
        if (uses_modulator_) {
          vocode_bands_ = std::max(4, std::min(static_cast<int>(input(kVocodeBands)->source->buffer[0][0] + 0.5f),
                                               int(kMaxVocodeBands)));
          // Attack and release arrive as log2 of seconds.
          vocode_attack_ = std::exp2(std::max(-12.0f, std::min(input(kVocodeAttack)->source->buffer[0][0], 1.0f)));
          vocode_release_ = std::exp2(std::max(-12.0f, std::min(input(kVocodeRelease)->source->buffer[0][0], 2.0f)));
          vocode_formant_ = std::max(-1.0f, std::min(input(kVocodeFormant)->source->buffer[0][0], 1.0f));
          vocode_flatten_ = std::max(0.0f, std::min(input(kVocodeFlatten)->source->buffer[0][0], 1.0f));
          vocode_noise_ = std::max(0.0f, std::min(input(kVocodeNoise)->source->buffer[0][0], 1.0f));
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

        float target_mix = neutral ? 0.0f : 1.0f;
        float mix_step = (target_mix - mix_) / std::max(num_samples, 1);

        const poly_float* audio = input(kAudio)->source->buffer;
        const poly_float* modulator = uses_modulator_ ? input(kModulator)->source->buffer : nullptr;
        poly_float* out = output()->buffer;
        const int n = size_;
        const int hop = n / kOverlap;

        for (int i = 0; i < num_samples; ++i) {
          mix_ += mix_step;
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
            result[c] = dry + (wet - dry) * mix_;
          }
          out[i] = poly_float(result[0], result[1]);

          if (++hop_count_ >= hop) {
            hop_count_ = 0;
            processFrame(0);
            processFrame(1);
            // The position is a modulatable parameter (an LFO can drive it); glide towards it between frames.
            position_ += (position_target_ - position_) * 0.4f;
          }
        }
        mix_ = target_mix;
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
        float venv[kMaxVocodeBands];
        int write = 0;
        int read = 0;
        int history_pos = 0;
        int history_filled = 0;
      };

      void setFftSize(int index) {
        fft_index_ = index;
        size_ = fftSizeFromIndex(index);
        for (Channel& ch : channels_) {
          std::fill(ch.in.begin(), ch.in.end(), 0.0f);
          std::fill(ch.mod.begin(), ch.mod.end(), 0.0f);
          std::fill(ch.acc.begin(), ch.acc.end(), 0.0f);
          std::fill(ch.history.begin(), ch.history.end(), 0.0f);
          std::fill(ch.drift.begin(), ch.drift.end(), 0.0f);
          std::fill(ch.venv, ch.venv + kMaxVocodeBands, 0.0f);
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
          return 0.7f;
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
          const float norm = (1.0f - feedback) * 0.8f * 2.0f * gain_;
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
          // A regenerating comb per bin: H = (1 + d e^(-j w t)) / (1 - r e^(-j w t)).
          //  - t, the delay, sweeps exponentially from 0.2 ms to 10 ms with the position slider (even steps per octave,
          //    so an LFO on it sounds natural). The right channel runs 15% longer, so the two channels' notches never line
          //    up: a wide stereo flange from a mono source.
          //  - d, the depth, is the drawn curve (morph 0: flat 70%). r = 0.8 d is the regeneration: deep notches and
          //    ringing peaks. Drawing less depth at some frequencies leaves those untouched.
          //  - The comb is tilted: higher frequencies see a slightly shorter delay (up to 25% at 20 kHz), so the notches
          //    crowd towards the low end and the sweep has a metallic, jet-like edge instead of a plain comb.
          //  - The level is made up (peaks up to 1.3) so the effect does not just get quieter.
          float morph = std::fabs(a);
          bool inverted = a < 0.0f;
          // Detail controls: density = tilt (0 .. 50% shorter delay at 20 kHz, 25% by default), resonance = regeneration
          // (0 .. 0.95 of the depth, 0.8 by default), spread = how much longer the right channel's delay is (0 .. 30%,
          // 15% by default), gain = output level.
          float base_delay = 0.0002f * powf(50.0f, position()) * sample_rate * (c == 0 ? 1.0f : 1.0f + 0.3f * spread_);
          const float tilt_amount = 0.5f * density_;
          const float resonance_scale = std::min(0.95f, 1.6f * resonance_);
          const float out_gain = 2.0f * gain_;
          for (int k = 1; k < bins; ++k) {
            float depth = 0.7f + (drawnCurve(2, k, bins, sample_rate, inverted) - 0.7f) * morph;
            depth = std::max(0.0f, std::min(depth, 1.0f));
            float resonance = resonance_scale * depth;
            float tilt = 1.0f - tilt_amount * defaultCurve(1, k, bins, sample_rate);
            float angle = -2.0f * kPi_ * k * base_delay * tilt / n;
            float cs = cosf(angle), sn = sinf(angle);
            float num_re = 1.0f + depth * cs, num_im = depth * sn;
            float den_re = 1.0f - resonance * cs, den_im = -resonance * sn;
            float den_norm = out_gain * (1.0f + 0.3f * depth) * (1.0f - resonance) / ((1.0f + depth) * (den_re * den_re + den_im * den_im));
            float h_re = (num_re * den_re + num_im * den_im) * den_norm;
            float h_im = (num_im * den_re - num_re * den_im) * den_norm;
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
          // The modulator's spectrum.
          for (int i = 0; i < n; ++i)
            mod_work_[i] = window_[i] * ch.mod[(ch.write + i) % n];
          std::fill(mod_work_.begin() + n, mod_work_.begin() + 2 * n, 0.0f);
          fft->transformRealForward(mod_work_.data());

          // Prefix sums of the power of the carrier (this lane) and of the modulator, so the power of any bin range
          // is two lookups.
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

          // Log-spaced bands from 70 Hz to 14 kHz (or 45% of the sample rate).
          const int bands = vocode_bands_;
          const float bin_width = nyquist / bins;
          const float low_hz = 70.0f;
          const float high_hz = std::min(14000.0f, 0.45f * sample_rate);
          const float ratio = powf(2.0f, vocode_formant_);   // modulator bands are read at f / ratio
          const float hop_seconds = static_cast<float>(n / kOverlap) / sample_rate;
          const float attack_coef = expf(-hop_seconds / std::max(vocode_attack_, 1e-4f));
          const float release_coef = expf(-hop_seconds / std::max(vocode_release_, 1e-4f));
          const float whole_level = rangeLevel(carrier_sum, 0, bins + 1);
          const float flatten = vocode_flatten_;
          const float depth = std::min(a, 1.0f);

          float centre[kMaxVocodeBands];
          float log_gain[kMaxVocodeBands];
          float level[kMaxVocodeBands];
          for (int b = 0; b < bands; ++b) {
            float lo_hz = low_hz * powf(high_hz / low_hz, static_cast<float>(b) / bands);
            float hi_hz = low_hz * powf(high_hz / low_hz, static_cast<float>(b + 1) / bands);
            int lo = std::max(0, std::min(static_cast<int>(lo_hz / bin_width), bins));
            int hi = std::max(lo + 1, std::min(static_cast<int>(std::ceil(hi_hz / bin_width)), bins + 1));
            int mod_lo = std::max(0, std::min(static_cast<int>(lo_hz / ratio / bin_width), bins));
            int mod_hi = std::max(mod_lo + 1, std::min(static_cast<int>(std::ceil(hi_hz / ratio / bin_width)), bins + 1));
            centre[b] = std::sqrt(lo_hz * hi_hz) / bin_width;

            float modulator_level = rangeLevel(modulator_sum, mod_lo, mod_hi);
            float carrier_level = std::max(rangeLevel(carrier_sum, lo, hi), 0.03f * whole_level);
            float& follower = ch.venv[b];
            follower = modulator_level + (follower - modulator_level) *
                       (modulator_level > follower ? attack_coef : release_coef);
            level[b] = follower;

            // flatten 0: the carrier's band is scaled by the modulator's level; flatten 1: the band is levelled first,
            // so every band carries exactly the modulator's level whatever the carrier does there.
            float reference = powf(carrier_level, flatten) * powf(std::max(whole_level, 1e-9f), 1.0f - flatten);
            float gain = std::min(follower / std::max(reference, 1e-9f), 4.0f * float(kMaxGain));
            log_gain[b] = log2f(std::max(gain, 1e-4f));
          }

          // Per bin: the band gains are interpolated between band centres (in log gain), so there are no steps.
          int band = 0;
          for (int k = 0; k <= bins; ++k) {
            while (band + 1 < bands && centre[band + 1] <= k)
              ++band;
            int next = std::min(band + 1, bands - 1);
            float t = 0.0f;
            if (next != band && k > centre[band])
              t = std::min(1.0f, (k - centre[band]) / std::max(centre[next] - centre[band], 1e-3f));
            float lg = log_gain[band] + (log_gain[next] - log_gain[band]) * t;
            float gain = exp2f(lg * depth);
            float re = spec[2 * k] * gain, im = spec[2 * k + 1] * gain;

            // Consonant noise: a hiss as loud as the modulator's band, fading in between 1.5 kHz and 5 kHz.
            if (vocode_noise_ > 0.0f) {
              float frequency = k * bin_width;
              float fade = std::max(0.0f, std::min((frequency - 1500.0f) / 3500.0f, 1.0f));
              if (fade > 0.0f) {
                float band_level = level[band] + (level[next] - level[band]) * t;
                vocode_rng_ = vocode_rng_ * 1664525u + 1013904223u;
                float angle = (vocode_rng_ >> 8) * (2.0f * kPi_ / 16777216.0f);
                float magnitude = 0.7f * vocode_noise_ * fade * band_level * depth;
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
      int mode_ = kNone;
      float amount_ = 0.0f;
      float mix_ = 0.0f;
      float position_target_ = 0.5f;
      float position_ = 0.5f;
      uint32_t seed_ = kDefaultSeed;
      float shuffle_ = 0.0f;
      float gain_ = 0.5f, density_ = 0.5f, resonance_ = 0.5f, spread_ = 0.5f;
      int vocode_bands_ = 24;
      float vocode_attack_ = 0.004f;
      float vocode_release_ = 0.06f;
      float vocode_formant_ = 0.0f;
      float vocode_flatten_ = 0.5f;
      float vocode_noise_ = 0.25f;
      uint32_t vocode_rng_ = 12345u;
      float last_dice_ = -1.0f;
      bool dice_sync_ = false;
      bool random_valid_[2] = { false, false };
      uint32_t random_seed_[2] = { 0, 0 };
      int random_index_[2] = { 0, 0 };
      const LineGenerator* curves_[3] = { nullptr, nullptr, nullptr };
      int latency_ = 1024;
      Channel channels_[2];
      std::vector<float> window_, work_, mod_work_, mag_, env_, env2_, prefix_, phase_;
      std::vector<float> random_[2];
      std::vector<float> vocode_power_[2];   // prefix sums of the power of the carrier [0] and the modulator [1]
      std::shared_ptr<FourierTransform> transforms_[kNumFftSizes];

      JUCE_LEAK_DETECTOR(SpectralMorphProcessor)
  };
} // namespace vital
