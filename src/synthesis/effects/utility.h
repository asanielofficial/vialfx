// VialFX - Utility effect. Licensed under GPL-3.0-or-later.
// Written for the VialFX bundle; follows the structure of Vital's effects (Matt Tytel, GPL-3.0).
#pragma once

#include "processor.h"

namespace vital {

  // Gain, pan, stereo width, Haas delay, polarity invert, channel swap, DC filter and mono bass.
  // Signal order: polarity -> swap -> DC filter -> mono bass -> width -> haas -> pan -> gain.
  class Utility : public Processor {
    public:
      enum {
        kAudio,
        kGain,
        kPan,
        kSpread,
        kHaas,
        kInvertLeft,
        kInvertRight,
        kSwap,
        kDcFilter,
        kMonoBass,
        kMonoFrequency,
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      Utility();
      ~Utility() override { }

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void hardReset() override;

    private:
      static constexpr int kHaasBufferSize = 4096;   // power of two, > 20 ms at 192 kHz

      struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setLowpass(float freq, float sample_rate);
        void setHighpass(float freq, float sample_rate);
        float tick(float x) {
          float y = b0 * x + z1;
          z1 = b1 * x - a1 * y + z2;
          z2 = b2 * x - a2 * y;
          return y;
        }
        void reset() { z1 = z2 = 0.0f; }
      };

      float dc_x_[2], dc_y_[2];
      Biquad low_[2][2];     // [channel][stage]: two Butterworth sections = Linkwitz-Riley 24 dB/oct low band
      Biquad high_[2][2];    // matching high band (low + high is an all-pass, so mono-ing the lows does not comb)
      float last_frequency_;
      float haas_buffer_[2][kHaasBufferSize];
      int haas_pos_;
      float gain_, pan_, spread_, haas_, bass_amount_;
  };
} // namespace vital
