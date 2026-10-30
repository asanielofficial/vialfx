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
        kEngaged,   // 1 while either lane uses a spectral mode: then every lane delays by one FFT size; 0: no delay at all
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
          channels_[c].vout.assign(kMaxFftSize, 0.0f);
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

      // The wavetable of filter responses for Spectral Table (owned by the engine).
      void setTable(Wavetable* table) { table_ = table; }

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
          warm_samples_ = 0;
        }
        else if (idle_) {
          idle_ = false;
          if (mode == kVocode)
            warm_samples_ = size_;
          else
            warm_hops_ = kOverlap;
        }
        const bool is_vocode = mode == kVocode;
        if (is_vocode != vocode_prev_) {
          // Moving between the filterbank and an STFT mode: the one that was idle has to fill its delay first.
          vocode_prev_ = is_vocode;
          if (!neutral && !idle_) {
            if (is_vocode)
              warm_samples_ = size_;
            else
              warm_hops_ = kOverlap;
          }
        }
        if (is_vocode)
          warm_hops_ = 0;
        const bool vocoding = is_vocode && uses_modulator_ && (!neutral || mix_ > 0.0f);
        if (vocoding)
          updateVocoderCoefficients(static_cast<float>(std::max(getSampleRate(), 8000)));

        // The dry/wet fade runs at a fixed rate per sample (64 samples end to end), whatever the host's block size.
        const float kMixStep = 1.0f / 64.0f;
        const float kBlendStep = 1.0f / 256.0f;
        const bool engaged = input(kEngaged)->source->buffer[0][0] > 0.5f;

        const poly_float* audio = input(kAudio)->source->buffer;
        const poly_float* modulator = uses_modulator_ ? input(kModulator)->source->buffer : nullptr;
        poly_float* out = output()->buffer;
        const int n = size_;
        const int hop = n / kOverlap;
        const bool reads_table = mode == kSpectralTable && table_ != nullptr && !neutral;
        if (reads_table) {
          table_->markUsed();   // tells the editing thread the table is being read
          table_data_ = table_->getAllActiveData();
        }

        for (int i = 0; i < num_samples; ++i) {
          const float target_mix = (neutral || warm_hops_ > 0 || warm_samples_ > 0) ? 0.0f : 1.0f;
          mix_ += std::max(-kMixStep, std::min(target_mix - mix_, kMixStep));
          delay_blend_ += std::max(-kBlendStep, std::min((engaged ? 1.0f : 0.0f) - delay_blend_, kBlendStep));
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
            // Vocode: the filterbank runs in the time domain and its output waits in a ring, so it comes out exactly n
            // samples late like every other mode. The ring is kept silent otherwise.
            if (vocoding) {
              float voiced = vocodeSample(c, audio[i][c], modulator[i][c]);
              float old = ch.vout[ch.write];
              ch.vout[ch.write] = voiced;
              wet = dry + (old - dry) * std::min(amount_, 1.0f);
            }
            else
              ch.vout[ch.write] = 0.0f;
            ch.write = (ch.write + 1) % n;
            if (c == 0) {
              float magnitude = std::fabs(audio[i][0]) + std::fabs(audio[i][1]);
              if (modulator)
                magnitude += std::fabs(modulator[i][0]) + std::fabs(modulator[i][1]);
              silent_run_ = magnitude < 1.0e-10f ? std::min(silent_run_ + 1, 1 << 30) : 0;
            }
            // Engaged: the delayed signal (dry or effect). Not engaged: the input itself, with no delay. The change
            // between the two is a short crossfade.
            float delayed = dry + (wet - dry) * mix_;
            result[c] = audio[i][c] + (delayed - audio[i][c]) * delay_blend_;
          }
          out[i] = poly_float(result[0], result[1]);
          if (warm_samples_ > 0)
            --warm_samples_;

          if (++hop_count_ >= hop) {
            hop_count_ = 0;
            // Skipped: a neutral stage, and a whole ring of digital silence (every frame would be silent too).
            if (!idle_ && silent_run_ < n && !is_vocode) {
              processFrame(0);
              processFrame(1);
              if (warm_hops_ > 0)
                --warm_hops_;
            }
            // The position is a modulatable parameter (an LFO can drive it); glide towards it between frames.
            position_ += (position_target_ - position_) * 0.4f;
          }
        }
        if (reads_table) {
          table_->markUnused();
          table_data_ = nullptr;
        }
      }

    private:
      static constexpr float kPi_ = 3.14159265358979323846f;

      static constexpr int kHistoryFrames = 16;
      static constexpr int kMaxVocodeBands = 64;

      struct Channel {
        std::vector<float> in, mod, acc, vout;
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


      // ---- Vocoder: a classic filterbank --------------------------------------------------------------------------
      // The carrier (this lane) and the modulator (the sidechain lane) go through the same bank of log-spaced band-pass
      // filters (two cascaded biquads each, 80 Hz .. 12 kHz). The modulator's band levels are followed (attack /
      // release) and multiplied onto the carrier's bands, which are summed. There are no FFT frames, so transients
      // stay sharp. Formant shifts which modulator band drives which carrier band. Whitening evens out the
      // modulator's long-term spectrum (a voice falls off steeply, which is what made earlier versions muffled),
      // emphasis lifts the highs, flatten levels the carrier's bands first, noise adds a hiss in the top bands
      // that follows the modulator's highs (consonants), and the gate silences the output when the modulator does.
      struct VocoderChannel {
        float carrier_state[kMaxVocodeBands][4] = {};
        float modulator_state[kMaxVocodeBands][4] = {};
        float envelope[kMaxVocodeBands] = {};
        float carrier_envelope[kMaxVocodeBands] = {};
        float average[kMaxVocodeBands] = {};
        float band_gain[kMaxVocodeBands] = {};
        float hiss_level = 0.0f;
        float open_target = 1.0f, open = 1.0f;
        float peak = 0.0f;
        float hiss_low[2] = { 0.0f, 0.0f };
        int tick = 0;
      };

      static void designBandPass(float frequency, float octaves, float sample_rate, float* out) {
        float w0 = 2.0f * kPi_ * frequency / sample_rate;
        float s = std::sin(w0);
        float alpha = s * std::sinh(0.34657359f * octaves * w0 / s);
        float a0 = 1.0f + alpha;
        out[0] = alpha / a0;
        out[1] = -2.0f * std::cos(w0) / a0;
        out[2] = (1.0f - alpha) / a0;
      }

      void updateVocoderCoefficients(float sample_rate) {
        if (vocode_bands_ == coeff_bands_ && vocode_formant_ == coeff_formant_ && sample_rate == coeff_rate_ &&
            vocode_attack_ == coeff_attack_ && vocode_release_ == coeff_release_ && vocode_emphasis_ == coeff_emphasis_)
          return;
        coeff_bands_ = vocode_bands_;
        coeff_formant_ = vocode_formant_;
        coeff_rate_ = sample_rate;
        coeff_attack_ = vocode_attack_;
        coeff_release_ = vocode_release_;
        coeff_emphasis_ = vocode_emphasis_;
        const int bands = vocode_bands_;
        const float low = 80.0f, high = std::min(12000.0f, 0.45f * sample_rate);
        const float spacing = std::log2(high / low) / bands;
        const float ratio = std::exp2(vocode_formant_);
        for (int b = 0; b < bands; ++b) {
          float frequency = low * std::exp2((b + 0.5f) * spacing);
          designBandPass(frequency, spacing * 1.6f, sample_rate, synth_coef_[b]);
          float shifted = std::max(40.0f, std::min(frequency / ratio, 0.45f * sample_rate));
          designBandPass(shifted, spacing * 1.6f, sample_rate, analysis_coef_[b]);
          band_frequency_[b] = frequency;
          // The follower's times, no shorter than about a period of the band (the rectified band signal ripples).
          float floor_time = 0.6f / shifted;
          attack_coef_[b] = 1.0f - std::exp(-1.0f / (std::max(vocode_attack_, floor_time) * sample_rate));
          release_coef_[b] = 1.0f - std::exp(-1.0f / (std::max(vocode_release_, floor_time) * sample_rate));
          emphasis_gain_[b] = std::pow(std::max(frequency, 300.0f) / 1000.0f, vocode_emphasis_);
        }
        carrier_coef_ = 1.0f - std::exp(-1.0f / (0.02f * sample_rate));
      }

      float vocodeSample(int c, float carrier, float modulator) {
        VocoderChannel& v = vocoder_[c];
        const int bands = vocode_bands_;
        float sum = 0.0f;
        for (int b = 0; b < bands; ++b) {
          const float* ac = analysis_coef_[b];
          const float* sc = synth_coef_[b];
          float* mz = v.modulator_state[b];
          float* cz = v.carrier_state[b];
          float m = modulator, x = carrier;
          for (int stage = 0; stage < 2; ++stage) {
            float y = ac[0] * m + mz[2 * stage];
            mz[2 * stage] = -ac[1] * y + mz[2 * stage + 1];
            mz[2 * stage + 1] = -ac[0] * m - ac[2] * y;
            m = y;
            float z = sc[0] * x + cz[2 * stage];
            cz[2 * stage] = -sc[1] * z + cz[2 * stage + 1];
            cz[2 * stage + 1] = -sc[0] * x - sc[2] * z;
            x = z;
          }
          float level = std::fabs(m);
          v.envelope[b] += (level - v.envelope[b]) * (level > v.envelope[b] ? attack_coef_[b] : release_coef_[b]);
          v.carrier_envelope[b] += (std::fabs(x) - v.carrier_envelope[b]) * carrier_coef_;
          sum += x * v.envelope[b] * v.band_gain[b];
        }

        if (++v.tick >= kVocodeControlRate) {
          v.tick = 0;
          vocodeControl(v);
        }
        v.open += (v.open_target - v.open) * 0.01f;

        // Consonant hiss: white noise above 3 kHz, as loud as the modulator's highs.
        float hiss = 0.0f;
        if (vocode_noise_ > 0.0f) {
          vocode_rng_ = vocode_rng_ * 1664525u + 1013904223u;
          float noise = (vocode_rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
          float lowpass_coef = 1.0f - std::exp(-2.0f * kPi_ * 3000.0f / coeff_rate_);
          v.hiss_low[0] += (noise - v.hiss_low[0]) * lowpass_coef;
          float once = noise - v.hiss_low[0];
          v.hiss_low[1] += (once - v.hiss_low[1]) * lowpass_coef;
          hiss = (once - v.hiss_low[1]) * v.hiss_level * vocode_noise_;
        }
        return (sum + hiss) * v.open * std::exp2(vocode_level_db_ / 6.0206f);
      }

      // Every kVocodeControlRate samples: the slow work (whitening, flatten, gate, hiss level).
      void vocodeControl(VocoderChannel& v) {
        const int bands = vocode_bands_;
        float total = 0.0f, carrier_total = 0.0f;
        for (int b = 0; b < bands; ++b) {
          total += v.envelope[b];
          carrier_total += v.carrier_envelope[b];
        }
        const float carrier_mean = carrier_total / bands;
        const float follow = 1.0f - std::exp(-static_cast<float>(kVocodeControlRate) / coeff_rate_);   // about a second
        const float average_coef = follow;
        float mean_average = 0.0f;
        for (int b = 0; b < bands; ++b) {
          v.average[b] += (v.envelope[b] - v.average[b]) * average_coef;
          mean_average += v.average[b];
        }
        mean_average /= bands;
        const float eps = 1.0e-3f * mean_average + 1.0e-7f;
        float centre = 0.0f;
        for (int b = 0; b < bands; ++b)
          centre += std::log2(v.average[b] + eps);
        centre /= bands;

        float hiss_sum = 0.0f;
        int hiss_bands = 0;
        for (int b = 0; b < bands; ++b) {
          float whiten = std::exp2(std::max(-2.0f, std::min((centre - std::log2(v.average[b] + eps)) * vocode_whiten_, 3.0f)));
          // The output band is the carrier's band scaled so that it carries the modulator's level: gain = envelope /
          // reference. Flatten 0: the reference is the carrier's mean level over all bands, so the carrier keeps its own
          // spectral shape and takes the modulator's on top. Flatten 1: the reference is the carrier's level in that very
          // band, so every band is levelled first and the result follows the modulator alone. A quiet carrier stays quiet.
          const float global = std::max(carrier_mean, 1.0e-3f);
          const float local = std::max(v.carrier_envelope[b], std::max(0.03f * carrier_mean, 1.0e-4f));
          v.band_gain[b] = whiten * emphasis_gain_[b] * std::pow(local, -vocode_flatten_) *
                           std::pow(global, -(1.0f - vocode_flatten_));
          if (band_frequency_[b] > 3000.0f) {
            hiss_sum += v.envelope[b] * whiten * emphasis_gain_[b];
            ++hiss_bands;
          }
        }
        v.hiss_level = hiss_bands ? hiss_sum / hiss_bands * std::sqrt(static_cast<float>(hiss_bands)) * 2.2f : 0.0f;

        // Gate: below a threshold under the modulator's recent peak, the output fades out.
        if (vocode_gate_ > 0.0f) {
          v.peak = std::max(total, v.peak * std::exp(-static_cast<float>(kVocodeControlRate) / (2.0f * coeff_rate_)));
          float threshold = v.peak * std::pow(10.0f, -(80.0f - 50.0f * vocode_gate_) / 20.0f);
          v.open_target = std::max(0.0f, std::min((total - 0.5f * threshold) / std::max(0.5f * threshold, 1.0e-9f), 1.0f));
        }
        else
          v.open_target = 1.0f;
      }

      // Drops everything the modes keep between frames (not the audio ring, which is the delay line).
      void clearState() {
        vocoder_[0] = VocoderChannel();
        vocoder_[1] = VocoderChannel();
        for (Channel& ch : channels_) {
          std::fill(ch.acc.begin(), ch.acc.end(), 0.0f);
          std::fill(ch.vout.begin(), ch.vout.end(), 0.0f);
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
          std::fill(ch.vout.begin(), ch.vout.end(), 0.0f);
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
            float db = FxTables::spectralDb(table_data_, octave, table_position) * depth;
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
        case kVocode:
          break;   // the vocoder is a time-domain filterbank (vocodeSample), not an STFT mode
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

      static constexpr int kVocodeControlRate = 32;
      VocoderChannel vocoder_[2];
      float synth_coef_[kMaxVocodeBands][3] = {};
      float analysis_coef_[kMaxVocodeBands][3] = {};
      float attack_coef_[kMaxVocodeBands] = {};
      float release_coef_[kMaxVocodeBands] = {};
      float emphasis_gain_[kMaxVocodeBands] = {};
      float band_frequency_[kMaxVocodeBands] = {};
      float carrier_coef_ = 0.01f;
      int coeff_bands_ = -1;
      float coeff_formant_ = 0.0f, coeff_rate_ = 44100.0f, coeff_attack_ = -1.0f, coeff_release_ = -1.0f,
            coeff_emphasis_ = -1.0f;
      bool uses_modulator_;
      Wavetable* table_ = nullptr;
      const Wavetable::WavetableData* table_data_ = nullptr;
      int fft_index_ = -1;
      int size_ = 1024;
      int hop_count_ = 0;
      int warm_samples_ = 0;   // vocoder: samples still to run before its output fades in
      bool vocode_prev_ = false;
      bool idle_ = true;       // neutral: no FFT frames run
      int warm_hops_ = 0;      // frames still to run before the wet signal fades in
      int silent_run_ = 0;     // consecutive samples of digital silence on the inputs
      int mode_ = kNone;
      float amount_ = 0.0f;
      float mix_ = 0.0f;
      float delay_blend_ = 0.0f;   // 0: output is the input (no latency), 1: delayed by one FFT size
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
