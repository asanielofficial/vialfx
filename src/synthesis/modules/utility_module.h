// VialFX - Utility module. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class Utility;
  class PeakMeter;

  class UtilityModule : public SynthModule {
    public:
      UtilityModule();
      virtual ~UtilityModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new UtilityModule(*this); }

    protected:
      Utility* utility_;
      PeakMeter* meter_;

      JUCE_LEAK_DETECTOR(UtilityModule)
  };
} // namespace vital
