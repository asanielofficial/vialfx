// VialFX - Tremolo effect. Licensed under GPL-3.0-or-later.
#pragma once

#include "processor.h"

namespace vital {

  // Amplitude tremolo: two LFOs (A and B) each pull the gain down by up to their depth; the reductions add.
  // Each LFO is unipolar (full level at its peak, deepest cut at its trough), so 100% depth reaches silence.
  class Tremolo : public Processor {
    public:
      enum {
        kAudio,
        kModA,           // 0 Sine, 1 Triangle, 2 Saw Up, 3 Saw Down, 4 Square, 5 Random
        kModB,
        kRateA,          // Hz
        kRateB,
        kUnitA,          // 0 Hz, 1 tempo
        kUnitB,
        kTempoA,         // index into kSyncedFrequencyRatios
        kTempoB,
        kAmountA,        // 0 .. 1 depth
        kAmountB,
        kBeatsPerSecond,
        kNumInputs
      };

      enum {
        kAudioOut,
        kGain,           // current gain 0 .. 1 (for the meter)
        kNumOutputs
      };

      Tremolo();
      ~Tremolo() override { }

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void hardReset() override;

      // Unipolar shape, 0 (trough) .. 1 (peak).
      static float shape(int type, float phase, float rnd_prev, float rnd_next);

    private:
      struct Lfo {
        float phase = 0.0f, prev = 0.0f, next = 0.0f;
        unsigned int seed = 22222u;
        float rand() {
          seed = seed * 1664525u + 1013904223u;
          return static_cast<float>(seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }
        void reset(unsigned int s) { phase = 0.0f; prev = 0.0f; seed = s; next = rand(); }
      };

      float rateHz(int rate_input, int unit_input, int tempo_input) const;

      Lfo lfo_a_, lfo_b_;
      float amount_a_, amount_b_, rate_a_, rate_b_, gain_;
  };
} // namespace vital
