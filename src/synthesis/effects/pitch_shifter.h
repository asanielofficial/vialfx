// VialFX - Pitch Shifter effect. Licensed under GPL-3.0-or-later.
// Written for the VialFX bundle; follows the structure of Vital's effects (Matt Tytel, GPL-3.0).
#pragma once

#include "processor.h"

#include <memory>

namespace vital {

  // Four pitch engines behind one interface:
  //   Granular       overlapping crossfaded grains read from a delay line
  //   MB-Granular    the same in three bands (longer grains in the lows)
  //   Retro          single-tap sampler-style re-pitch with short loop crossfades
  //   Spectral       phase vocoder with transient phase resets
  // The granular and spectral engines are followed by a spectral-envelope stage that either keeps the
  // original formants (formant control off) or moves them by the formant knob (formant control on).
  class PitchShifter : public Processor {
    public:
      enum {
        kAudio,
        kMode,
        kCompensation,
        kPitch,
        kGrain,
        kResolution,
        kFormantControl,
        kFormant,
        kDryWet,
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      enum Mode {
        kGranular,
        kMultibandGranular,
        kRetro,
        kSpectral,
        kNumModes
      };

      class Channel;

      PitchShifter();
      ~PitchShifter() override;

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void hardReset() override;

    private:
      void allocate(int sample_rate);

      std::unique_ptr<Channel> channels_[2];
      int allocated_rate_;
      bool ready_;
      mono_float mix_;
      mono_float neutral_;
      mono_float ratio_;
      mono_float formant_ratio_;
  };
} // namespace vital
