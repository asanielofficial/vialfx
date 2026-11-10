// VialFX - Frequency Shifter module. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class FreqShifter;

  class FreqShifterModule : public SynthModule {
    public:
      FreqShifterModule(const Output* beats_per_second, const Output* keytrack);
      virtual ~FreqShifterModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new FreqShifterModule(*this); }

    protected:
      FreqShifter* shifter_;
      const Output* beats_per_second_;
      const Output* keytrack_;

      JUCE_LEAK_DETECTOR(FreqShifterModule)
  };
} // namespace vital
