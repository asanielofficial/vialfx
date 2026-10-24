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
#include "utils.h"
#include "value.h"

#include <cmath>

namespace vital {
  // Time-domain waveshapers that sit on each input lane after its level and pan, before the filter routing.
  //
  // Eight modes, each with an amount knob from 0 to 1. Sync, Formant, Bend and Squeeze are bipolar, as in the
  // original oscillator distortions: the knob's centre (0.5) is neutral and the two directions do opposite
  // things. The other four (Quantize, Pulse, FM, RM) are unipolar: 0 is neutral. At its neutral position every
  // mode passes the audio through untouched.
  //
  //  Sync     (bipolar) up: triangle wavefolder, driven further into the folds. Down: only the positive half
  //           of the waveform folds (an asymmetric, "one-sided" fold).
  //  Formant  (bipolar) up: wavefolder windowed by the signal's own amplitude, folding the middle of the wave.
  //           Down: the opposite window, folding the peaks.
  //  Quantize bit reduction plus sample-rate reduction.
  //  Bend     (bipolar) bends one half of the waveform towards zero: up the positive half, down the negative.
  //  Squeeze  (bipolar) up: power-law squeeze that pinches low levels towards zero. Down: the opposite, which
  //           lifts low levels (a stretch).
  //  Pulse    asymmetric clipper: the positive half clips early, the negative half stays open.
  //  FM       phase modulation: the modulator lane (the sidechain) moves a short delay tap of the signal.
  //  RM       ring modulation: the signal multiplied by the modulator lane, mixed in by the amount.
  //
  // FM and RM are the only modes that use the modulator, which is the sidechain lane after its own level, pan
  // and warp (the sidechain lane always processes first). If the modulator lane is switched off, FM and RM
  // output silence. The sidechain lane itself has no modulator, so it only offers the first six modes.
  // FM delays the signal by up to kMaxFmDelaySamples * amount samples (about 1.5 ms at full amount).
  class WaveWarp : public Processor {
    public:
      enum {
        kAudio,
        kMode,
        kAmount,
        kModulator,
        kModulatorOn,
        kNumInputs
      };

      enum Mode {
        kSync,
        kFormant,
        kQuantize,
        kBend,
        kSqueeze,
        kPulse,
        kFm,
        kRm,
        kNumModes
      };

      static constexpr int kMaxFmDelaySamples = 64;
      static constexpr int kFmBufferSize = 256;  // power of two, bigger than the maximum delay plus interpolation
      static constexpr int kLanes = poly_float::kSize;
      static constexpr float kReleaseSeconds = 0.15f;
      static constexpr float kMinReference = 0.01f;   // below about -40 dB the signal is not scaled up further

      // uses_modulator: whether this instance has a modulator lane to read (the main lane has one).
      explicit WaveWarp(bool uses_modulator) : Processor(kNumInputs, 1), uses_modulator_(uses_modulator),
                                               write_index_(0), hold_counter_(0) {
        for (int lane = 0; lane < kLanes; ++lane) {
          held_[lane] = 0.0f;
          envelope_[lane] = 0.0f;
          for (int i = 0; i < kFmBufferSize; ++i)
            history_[i][lane] = 0.0f;
        }
      }

      Processor* clone() const override { return new WaveWarp(*this); }

      static bool isBipolar(int mode) {
        return mode == kSync || mode == kFormant || mode == kBend || mode == kSqueeze;
      }

      // The knob value (0..1) as the signed/unsigned amount the shapers use.
      static float effectiveAmount(int mode, float knob) {
        knob = utils::clamp(knob, 0.0f, 1.0f);
        return isBipolar(mode) ? 2.0f * knob - 1.0f : knob;
      }

      static bool isNeutral(int mode, float amount) {
        return isBipolar(mode) ? std::fabs(amount) < 1.0e-4f : amount < 1.0e-6f;
      }

      // The waveshaper for one sample of one channel, for the modes that need no state or modulator.
      // amount is signed (-1..1) for the bipolar modes and 0..1 for Pulse.
      static float shape(int mode, float x, float amount) {
        switch (mode) {
          case kSync: {
            if (amount >= 0.0f)
              return fold((1.0f + 7.0f * amount) * x);
            return x < 0.0f ? x : fold((1.0f - 7.0f * amount) * x);
          }
          case kFormant: {
            float squared = std::min(x * x, 1.0f);
            float window = amount >= 0.0f ? 1.0f - squared : squared;
            float strength = std::fabs(amount);
            float folded = fold((1.0f + 3.0f * strength) * 2.0f * x) * window;
            return x + strength * (folded - x);
          }
          case kBend: {
            // Pinch one half of the wave towards zero (positive half for amount > 0, negative for < 0).
            float power = 1.0f + 3.0f * std::fabs(amount);
            bool bent_side = amount >= 0.0f ? x > 0.0f : x < 0.0f;
            if (!bent_side)
              return x;
            float magnitude = std::fabs(x);
            return std::copysign(magnitude <= 1.0f ? std::pow(magnitude, power) : magnitude, x);
          }
          case kSqueeze: {
            float magnitude = std::fabs(x);
            if (magnitude > 1.0f)
              return x;
            float power = amount >= 0.0f ? 1.0f + 3.0f * amount : 1.0f / (1.0f + 3.0f * -amount);
            return std::copysign(std::pow(magnitude, power), x);
          }
          case kPulse: {
            float ceiling = 1.0f - 0.7f * amount;
            float y = std::min(x * (1.0f + 3.0f * amount), ceiling);
            return std::max(y, -1.0f);
          }
          default:
            return x;
        }
      }

      void process(int num_samples) override {
        const poly_float* audio = input(kAudio)->source->buffer;
        poly_float* out = output()->buffer;

        int mode = static_cast<int>(input(kMode)->source->buffer[0][0] + 0.5f);
        mode = std::max(0, std::min(mode, kNumModes - 1));
        float amount = effectiveAmount(mode, input(kAmount)->source->buffer[0][0]);
        bool modulated_mode = mode == kFm || mode == kRm;
        bool has_modulator = uses_modulator_ && input(kModulatorOn)->source->buffer[0][0] > 0.5f;

        // The FM delay line always records, so switching to FM never replays stale audio.
        for (int i = 0; i < num_samples; ++i) {
          for (int lane = 0; lane < kLanes; ++lane)
            history_[(write_index_ + i) & (kFmBufferSize - 1)][lane] = audio[i][lane];
        }

        if (isNeutral(mode, amount) || (modulated_mode && !uses_modulator_)) {
          for (int i = 0; i < num_samples; ++i)
            out[i] = audio[i];
          write_index_ = (write_index_ + num_samples) & (kFmBufferSize - 1);
          return;
        }

        if (modulated_mode && !has_modulator) {
          for (int i = 0; i < num_samples; ++i)
            out[i] = poly_float(0.0f);
          write_index_ = (write_index_ + num_samples) & (kFmBufferSize - 1);
          return;
        }

        // The shapers are designed for a full-scale (+-1) waveform. Real audio is usually much quieter, so the
        // waveshaping modes work on the signal relative to its own peak level (a fast-attack, slow-release
        // follower) and scale the result back. The effect is then the same at any input level.
        float release = std::exp(-1.0f / (float(kReleaseSeconds) * std::max(getSampleRate(), 8000)));

        const poly_float* modulator = uses_modulator_ ? input(kModulator)->source->buffer : nullptr;
        for (int i = 0; i < num_samples; ++i) {
          float result[kLanes];
          for (int lane = 0; lane < kLanes; ++lane) {
            float x = audio[i][lane];
            if (mode == kQuantize)
              result[lane] = quantize(x, amount, lane);
            else if (mode == kFm)
              result[lane] = frequencyModulate(x, amount, utils::clamp(modulator[i][lane], -1.0f, 1.0f),
                                               (write_index_ + i) & (kFmBufferSize - 1), lane);
            else if (mode == kRm)
              result[lane] = x + amount * (x * utils::clamp(modulator[i][lane], -1.0f, 1.0f) - x);
            else {
              envelope_[lane] = std::max(std::fabs(x), envelope_[lane] * release);
              float reference = std::max(envelope_[lane], float(kMinReference));
              result[lane] = reference * shape(mode, x / reference, amount);
            }
          }
          if (mode == kQuantize)
            advanceHold(amount);
          out[i] = poly_float(result[0], result[1], result[2], result[3]);
        }
        write_index_ = (write_index_ + num_samples) & (kFmBufferSize - 1);
      }

    private:
      // Triangle fold that is the identity for |u| <= 1 and reflects at +-1.
      static float fold(float u) {
        float w = u * 0.25f + 0.25f;
        w -= std::floor(w);
        return 1.0f - 4.0f * std::fabs(w - 0.5f);
      }

      int holdPeriod(float amount) const { return 1 + static_cast<int>(amount * amount * 15.0f); }

      float quantize(float x, float amount, int lane) {
        if (hold_counter_ == 0) {
          float levels = std::pow(2.0f, 1.0f + (1.0f - amount) * 14.0f);
          held_[lane] = std::round(x * levels) / levels;
        }
        return held_[lane];
      }

      void advanceHold(float amount) {
        int period = holdPeriod(amount);
        hold_counter_ = (hold_counter_ + 1) % period;
      }

      float frequencyModulate(float x, float amount, float modulator, int index, int lane) {
        // history_[index] already holds x (written above); the tap moves back by up to amount * max samples.
        float delay = amount * kMaxFmDelaySamples * (0.5f + 0.5f * modulator);
        int whole = static_cast<int>(delay);
        float fraction = delay - whole;
        float newer = history_[(index - whole) & (kFmBufferSize - 1)][lane];
        float older = history_[(index - whole - 1) & (kFmBufferSize - 1)][lane];
        (void)x;
        return newer + fraction * (older - newer);
      }

      bool uses_modulator_;
      int write_index_;
      int hold_counter_;
      float held_[kLanes];
      float envelope_[kLanes];
      float history_[kFmBufferSize][kLanes];

      JUCE_LEAK_DETECTOR(WaveWarp)
  };
} // namespace vital
