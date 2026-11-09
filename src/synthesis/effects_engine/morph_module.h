/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "synth_module.h"
#include "spectral_morph_processor.h"
#include "wave_warp.h"

#include <atomic>

namespace vital {

  // The Morph effect: the spectral morph and the distortion warp of the input lanes as a chain effect, in either
  // order (morph_warp_first).
  //
  // Only the modes that are self contained are offered: the spectral modes from None to Time Skew and Spectral Gate,
  // and the warps from None to Pulse. The others need something an effect does not have: a drawn curve (Spectral
  // Filter, Phaser, Flanger), a modulator (Vocode, FM, RM) or a wavetable (the two Table modes).
  //
  // The effect chain runs oversampled, so the FFT is made larger by the same factor: the bins keep their width in Hz
  // and the latency in host samples is the one of the lanes.
  class MorphModule : public SynthModule {
    public:
      enum {
        kAudio,
        kFftSize,
        kNumInputs
      };

      // Everything but Vocode (it needs the sidechain as its modulator).
      static bool isAllowedSpectral(int mode) {
        return mode >= SpectralMorphProcessor::kNone && mode < SpectralMorphProcessor::kNumModes &&
               mode != SpectralMorphProcessor::kVocode;
      }

      // Everything but FM and RM (they need the sidechain as their modulator).
      static bool isAllowedWarp(int mode) {
        return mode >= WaveWarp::kNone && mode < WaveWarp::kNumModes && mode != WaveWarp::kFm && mode != WaveWarp::kRm;
      }

      static int safeSpectral(float value) {
        int mode = static_cast<int>(value + 0.5f);
        return isAllowedSpectral(mode) ? mode : static_cast<int>(SpectralMorphProcessor::kNone);
      }

      static int safeWarp(float value) {
        int mode = static_cast<int>(value + 0.5f);
        return isAllowedWarp(mode) ? mode : static_cast<int>(WaveWarp::kNone);
      }

      MorphModule() : SynthModule(kNumInputs, 1) { }

      void init() override {
        spectral_mode_ = createBaseControl("morph_spectral_morph_type");
        warp_mode_ = createBaseControl("morph_distortion_type");
        warp_first_ = createBaseControl("morph_warp_first");
        Value* dice = createBaseControl("morph_random_dice");
        Output* spectral_amount = createMonoModControl("morph_spectral_morph_amount");
        Output* shuffle = createMonoModControl("morph_random_shuffle");
        Output* warp_amount = createMonoModControl("morph_distortion_amount");

        spectral_ = new SpectralMorphProcessor(false);
        spectral_->plug(&spectral_in_, SpectralMorphProcessor::kAudio);
        spectral_->plug(&spectral_mode_out_, SpectralMorphProcessor::kMode);
        spectral_->plug(spectral_amount, SpectralMorphProcessor::kAmount);
        spectral_->plug(&fft_out_, SpectralMorphProcessor::kFftSize);
        spectral_->plug(&engaged_out_, SpectralMorphProcessor::kEngaged);
        spectral_->plug(shuffle, SpectralMorphProcessor::kShuffle);
        spectral_->plug(dice, SpectralMorphProcessor::kDice);
        spectral_->plug(createMonoModControl("morph_spectral_position"), SpectralMorphProcessor::kPosition);
        spectral_->plug(createMonoModControl("morph_spectral_gain"), SpectralMorphProcessor::kGain);
        spectral_->plug(createMonoModControl("morph_spectral_density"), SpectralMorphProcessor::kDensity);
        spectral_->plug(createMonoModControl("morph_spectral_resonance"), SpectralMorphProcessor::kResonance);
        spectral_->plug(createMonoModControl("morph_spectral_spread"), SpectralMorphProcessor::kSpread);
        spectral_->plug(createMonoModControl("morph_spectral_depth"), SpectralMorphProcessor::kDepth);
        spectral_->plug(createMonoModControl("morph_spectral_phase"), SpectralMorphProcessor::kPhase);
        spectral_->plug(createMonoModControl("morph_spectral_table_mix"), SpectralMorphProcessor::kTableMix);
        spectral_->plug(createMonoModControl("morph_table_position"), SpectralMorphProcessor::kTablePosition);
        spectral_->plug(createMonoModControl("morph_table_gain"), SpectralMorphProcessor::kTableGain);
        spectral_->plug(createMonoModControl("morph_table_resonance"), SpectralMorphProcessor::kTableResonance);
        addIdleProcessor(spectral_);

        warp_ = new WaveWarp(false);
        warp_->plug(&warp_in_, WaveWarp::kAudio);
        warp_->plug(&warp_mode_out_, WaveWarp::kMode);
        warp_->plug(warp_amount, WaveWarp::kAmount);
        warp_->plug(createMonoModControl("morph_distortion_phase"), WaveWarp::kPhase);
        warp_->plug(createMonoModControl("morph_distortion_bias"), WaveWarp::kBias);
        warp_->plug(createMonoModControl("morph_distortion_smooth"), WaveWarp::kSmooth);
        warp_->plug(createMonoModControl("morph_distortion_drive"), WaveWarp::kDrive);
        warp_->plug(createBaseControl("morph_distortion_dc_filter"), WaveWarp::kDcFilter);
        warp_->plug(createMonoModControl("morph_distortion_mix"), WaveWarp::kMix);
        addIdleProcessor(warp_);

        SynthModule::init();
      }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        SynthModule::process(num_samples);

        int spectral_mode = safeSpectral(spectral_mode_->value());
        int warp_mode = safeWarp(warp_mode_->value());
        spectral_mode_out_.buffer[0] = static_cast<float>(spectral_mode);
        warp_mode_out_.buffer[0] = static_cast<float>(warp_mode);
        engaged_out_.buffer[0] = spectral_mode != SpectralMorphProcessor::kNone ? 1.0f : 0.0f;
        fft_out_.buffer[0] = static_cast<float>(scaledFftIndex(static_cast<int>(input(kFftSize)->source->buffer[0][0] + 0.5f)));

        poly_float* out = output()->buffer;
        if (warp_first_->value() > 0.5f) {
          utils::copyBuffer(warp_in_.buffer, audio_in, num_samples);
          warp_->process(num_samples);
          utils::copyBuffer(spectral_in_.buffer, warp_->output()->buffer, num_samples);
          spectral_->process(num_samples);
          utils::copyBuffer(out, spectral_->output()->buffer, num_samples);
        }
        else {
          utils::copyBuffer(spectral_in_.buffer, audio_in, num_samples);
          spectral_->process(num_samples);
          utils::copyBuffer(warp_in_.buffer, spectral_->output()->buffer, num_samples);
          warp_->process(num_samples);
          utils::copyBuffer(out, warp_->output()->buffer, num_samples);
        }
      }

      // The FFT size index at the oversampled rate: one step up per doubling of the rate.
      int scaledFftIndex(int base_index) const {
        int bits = 0;
        for (int os = getOversampleAmount(); os > 1; os >>= 1)
          ++bits;
        return std::max(0, std::min(base_index + bits, SpectralMorphProcessor::kNumFftSizes - 1));
      }

      // The delay this effect adds, in host samples, while it is on (the spectral stage delays by one FFT).
      int latencySamples(int base_index) const {
        if (spectral_mode_ == nullptr || safeSpectral(spectral_mode_->value()) == SpectralMorphProcessor::kNone)
          return 0;
        int size = SpectralMorphProcessor::fftSizeFromIndex(scaledFftIndex(base_index));
        return size / std::max(1, getOversampleAmount());
      }

      // The drawn curves (Spectral Filter, Phaser, Flanger), the wavetables of the two table modes and the Random
      // Amplitudes seed belong to the engine, which saves them with the preset.
      void setCurve(int which, const LineGenerator* curve) { spectral_->setCurve(which, curve); }
      void setTables(Wavetable* warp_table, Wavetable* spectral_table) {
        warp_->setTable(warp_table);
        spectral_->setTable(spectral_table);
      }
      uint32_t seed() const { return spectral_->seed(); }
      void setSeed(uint32_t seed) { spectral_->setSeed(seed); }

      void setOversampleAmount(int oversampling) override {
        int size = kMaxBufferSize * oversampling;
        spectral_in_.ensureBufferSize(size);
        warp_in_.ensureBufferSize(size);
        spectral_mode_out_.ensureBufferSize(size);
        warp_mode_out_.ensureBufferSize(size);
        engaged_out_.ensureBufferSize(size);
        fft_out_.ensureBufferSize(size);
        SynthModule::setOversampleAmount(oversampling);
      }

    private:
      Value* spectral_mode_ = nullptr;
      Value* warp_mode_ = nullptr;
      Value* warp_first_ = nullptr;
      SpectralMorphProcessor* spectral_ = nullptr;
      WaveWarp* warp_ = nullptr;

      Output spectral_in_, warp_in_;
      Output spectral_mode_out_, warp_mode_out_, engaged_out_, fft_out_;

      JUCE_LEAK_DETECTOR(MorphModule)
  };
} // namespace vital
