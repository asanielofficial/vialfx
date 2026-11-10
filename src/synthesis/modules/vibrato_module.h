// VialFX - Vibrato module. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class Vibrato;

  class VibratoModule : public SynthModule {
    public:
      VibratoModule(const Output* beats_per_second);
      virtual ~VibratoModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new VibratoModule(*this); }

    protected:
      Vibrato* vibrato_;
      const Output* beats_per_second_;

      JUCE_LEAK_DETECTOR(VibratoModule)
  };
} // namespace vital
