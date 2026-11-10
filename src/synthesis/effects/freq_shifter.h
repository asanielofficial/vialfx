// VialFX - Frequency Shifter effect. Licensed under GPL-3.0-or-later.
#pragma once

#include "processor.h"

namespace vital {

  // Single-sideband frequency shifter (Hilbert transform + quadrature oscillator) with a feedback loop that can
  // be shifted again (FB direction) and smeared by an all-pass chain.
  class FreqShifter : public Processor {
    public:
      enum {
        kAudio,
        kShift,           // -1 .. 1, fraction of the range
        kStereo,          // -1 .. 1, offsets the shift between left and right
        kFeedback,
        kFbDirection,     // -1 .. 1, shift of the feedback path as a fraction of the range
        kAllpass,
        kDryWet,
        kUnit,            // 0 Hertz, 1 Seconds, 2 Tempo, 3 Dotted, 4 Triplets, 5 Keytrack
        kFrequency,       // Hz
        kPeriod,          // seconds
        kTempo,           // tempo index
        kBeatsPerSecond,
        kMidi,            // keytrack note
        kKeytrackTranspose,
        kQuality,         // 0 Rough (6 poles), 1 Normal (12), 2 Better (18), 3 Excellent (24)
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      FreqShifter();
      ~FreqShifter() override { }

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void hardReset() override;

    private:
      struct Allpass2 {
        float a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float tick(float x) {
          float y = a2 * (x + y2) - x2;
          x2 = x1; x1 = x; y2 = y1; y1 = y;
          return y;
        }
      };

      // 90 degree phase splitter: a pair of all-pass chains with elliptic half-band coefficients.
      struct Hilbert {
        static constexpr int kMaxSections = 12;
        Allpass2 slow[kMaxSections], fast[kMaxSections];
        int sections = 6;
        int quality = -1;
        float delayed = 0.0f;
        Hilbert();
        void setQuality(int new_quality);
        void tick(float x, float& real, float& imag);
        void reset();
      };

      struct Allpass1 {
        float x1 = 0.0f, y1 = 0.0f;
        float tick(float x, float a) {
          float y = -a * x + x1 + a * y1;
          x1 = x; y1 = y;
          return y;
        }
      };

      float rangeHz() const;

      Hilbert main_[2], feedback_shifter_[2];
      Allpass1 smear_[2][4];
      float feedback_state_[2];
      double phase_[2], fb_phase_[2];
      float shift_hz_[2], fb_hz_[2];
      void updateQuality(int quality);

      float wet_, feedback_, stereo_, shift_, fb_dir_, range_hz_;
  };
} // namespace vital
