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
  //   Phase Disperse (bipolar)  quadratic phase: a frequency-dependent delay (chirp), either direction
  //   Shepard Tone              octave-shifted copies crossfaded so the spectrum glides without end
  //   Spectral Time Skew        group delay that grows with the log of the frequency: high frequencies arrive late
  //   Vocode (bipolar)          imposes the spectral envelope of the other lane (the modulator) on this lane;
  //                             distance from the centre is the depth, up = fine bands, down = wide bands.
  //                             Only the main lane has a modulator. If the modulator lane is off, Vocode removes the sound as the depth rises.
  class SpectralMorphProcessor : public Processor {
    public:
      enum {
        kAudio,
        kMode,
        kAmount,
        kFftSize,
        kModulator,
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
        kVocode,
        kNumModes
      };

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
               mode == kPhaseDisperse || mode == kVocode;
      }

      // Knob 0..1 -> -1..1 for bipolar modes, unchanged otherwise.
      static float effectiveAmount(int mode, float knob) {
        knob = std::max(0.0f, std::min(knob, 1.0f));
        return isBipolar(mode) ? 2.0f * knob - 1.0f : knob;
      }

      static bool isNeutral(int mode, float amount) {
        if (mode <= kNone || mode >= kNumModes)
          return true;
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
        prefix_.assign(kMaxFftSize / 2 + 3, 0.0f);
        phase_.assign(kMaxFftSize / 2 + 2, 0.0f);
        for (int i = 0; i < kNumFftSizes; ++i)
          transforms_[i] = std::make_shared<FourierTransform>(kMinFftBits + i);
        setFftSize(kDefaultFftIndex);
        buildRandomTable(kDefaultSeed);
      }

      Processor* clone() const override { return new SpectralMorphProcessor(*this); }

      // Latency in samples at the current FFT size. Safe to call from any thread.
      int latencySamples() const { return latency_; }

      void setSeed(int seed) { buildRandomTable(seed); }

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
          }
        }
        mix_ = target_mix;
      }

    private:
      static constexpr int kDefaultSeed = 4;
      static constexpr float kPi_ = 3.14159265358979323846f;

      static constexpr int kHistoryFrames = 16;

      struct Channel {
        std::vector<float> in, mod, acc;
        // Spectral Time Skew: the last frames' spectra. Phase Disperse: each bin's accumulated phase drift.
        std::vector<float> history, drift;
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

      void buildRandomTable(int seed) {
        random_.resize(kMaxFftSize / 2 + 2);
        uint32_t state = 0x9E3779B9u * (uint32_t(seed) + 1u);
        for (float& r : random_) {
          state = state * 1664525u + 1013904223u;
          r = (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }
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
          for (int k = 0; k <= bins; ++k) {
            float gain = (1.0f + a) * std::max(1.0f - a - a * random_[k], 0.0f);
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
        case kPhaseDisperse: {
          // A quadratic phase (a chirp that smears transients) plus a slow drift: every bin is shifted in
          // frequency by an amount that grows with frequency (up to 100 Hz at the top), so sustained notes
          // lose their harmonic alignment and shimmer, not only the transients.
          float strength = a * kPi_ * n / 6.0f;
          float hop_seconds = static_cast<float>(n / kOverlap) / sample_rate;
          for (int k = 1; k <= bins; ++k) {
            float x = static_cast<float>(k) / bins;
            float drift = ch.drift[k] + 2.0f * kPi_ * a * 100.0f * x * hop_seconds;
            drift = fmodf(drift, 2.0f * kPi_);
            ch.drift[k] = drift;
            rotate(spec, k, strength * x * x + drift);
          }
          break;
        }
        case kTimeSkew: {
          // Frequency-dependent group delay: each bin is read from an older frame, the higher the frequency
          // the older (up to 16 hops, 4 FFT sizes, at full amount). Highs lag behind the lows, on sustained
          // sounds as well as on transients.
          int stride = n + 2;
          float* slot = ch.history.data() + ch.history_pos * stride;
          std::copy(spec, spec + stride, slot);
          float norm = 1.0f / std::log2(1.0f + bins);
          float max_hops = a * (kHistoryFrames - 1);
          for (int k = 1; k <= bins; ++k) {
            int hops = static_cast<int>(max_hops * std::log2(1.0f + k) * norm + 0.5f);
            hops = std::min(hops, ch.history_filled);
            int index = (ch.history_pos - hops + kHistoryFrames) % kHistoryFrames;
            const float* old = ch.history.data() + index * stride;
            spec[2 * k] = old[2 * k];
            spec[2 * k + 1] = old[2 * k + 1];
          }
          ch.history_pos = (ch.history_pos + 1) % kHistoryFrames;
          ch.history_filled = std::min(ch.history_filled + 1, kHistoryFrames - 1);
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
          for (int i = 0; i < n; ++i)
            mod_work_[i] = window_[i] * ch.mod[(ch.write + i) % n];
          std::fill(mod_work_.begin() + n, mod_work_.begin() + 2 * n, 0.0f);
          fft->transformRealForward(mod_work_.data());
          computePolar(mod_work_.data());
          // mag_ now holds the modulator; envelope of both.
          int half = a > 0.0f ? std::max(1, n / 1024) : std::max(2, n / 32);
          smoothEnvelope(mag_.data(), env2_.data(), bins, half);
          for (int k = 0; k <= bins; ++k) {
            float re = spec[2 * k], im = spec[2 * k + 1];
            mag_[k] = sqrtf(re * re + im * im);
          }
          smoothEnvelope(mag_.data(), env_.data(), bins, half);
          float depth = std::min(std::fabs(a), 1.0f);
          for (int k = 0; k <= bins; ++k) {
            float gain = env2_[k] / (env_[k] + 1e-9f);
            gain = std::max(1e-3f, std::min(gain, 4.0f * float(kMaxGain)));
            gain = powf(gain, depth);  // interpolated in dB, so partial depths are clearly audible
            spec[2 * k] *= gain;
            spec[2 * k + 1] *= gain;
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
      int latency_ = 1024;
      Channel channels_[2];
      std::vector<float> window_, work_, mod_work_, mag_, env_, env2_, prefix_, phase_, random_;
      std::shared_ptr<FourierTransform> transforms_[kNumFftSizes];

      JUCE_LEAK_DETECTOR(SpectralMorphProcessor)
  };
} // namespace vital
