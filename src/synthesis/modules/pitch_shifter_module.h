// VialFX - Pitch Shifter module. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class PitchShifter;

  class PitchShifterModule : public SynthModule {
    public:
      PitchShifterModule();
      virtual ~PitchShifterModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new PitchShifterModule(*this); }

    protected:
      PitchShifter* shifter_;

      JUCE_LEAK_DETECTOR(PitchShifterModule)
  };
} // namespace vital
