// VialFX - Vibrato effect. Licensed under GPL-3.0-or-later.
#pragma once

#include "processor.h"

#include <vector>

namespace vital {

  // Pitch vibrato: two LFOs (A and B) add together into a pitch deviation in cents; the deviation is turned into a
  // moving delay tap (delay slope = 1 - pitch ratio), so the cents amounts are exact for any rate. The 10 ms centre
  // delay leaves room for the swing; very slow rates with large amounts are limited softly by that headroom.
  class Vibrato : public Processor {
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
        kAmountA,        // cents (peak)
        kAmountB,
        kBeatsPerSecond,
        kNumInputs
      };

      enum {
        kAudioOut,
        kCents,          // current total deviation in cents (for the meter)
        kNumOutputs
      };

      Vibrato();
      ~Vibrato() override { }

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void hardReset() override;

      static float wave(int type, float phase, float rnd_prev, float rnd_next);

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
      void allocate();

      Lfo lfo_a_, lfo_b_;
      std::vector<float> buffer_[2];
      int mask_, write_;
      float offset_, amount_a_, amount_b_, rate_a_, rate_b_;
  };
} // namespace vital
