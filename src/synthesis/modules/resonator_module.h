// VialFX - Resonator module. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

class Tuning;

namespace vital {

  class Resonator;
  class ResonatorMidi;

  class ResonatorModule : public SynthModule {
    public:
      ResonatorModule(ResonatorMidi* midi);
      virtual ~ResonatorModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new ResonatorModule(*this); }

      void setTuning(const Tuning* tuning);
      // Allocates the voices' delay lines (message thread, when the copy joins the chain).
      void prepare();
      Resonator* resonator() { return resonator_; }

    protected:
      Resonator* resonator_;
      ResonatorMidi* midi_;

      JUCE_LEAK_DETECTOR(ResonatorModule)
  };
} // namespace vital
