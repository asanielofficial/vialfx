// VialFX: the "Other" filter models (Band Reject, All-Pass, Metal Comb, Metal Reverb, Disperser, Custom).
// GPL-3.0-or-later, like the rest of the project.

#pragma once

#include "processor.h"
#include "synth_filter.h"

#include <cstdlib>
#include <string>
#include <vector>

class LineGenerator;

namespace vital {

  class OtherFilter : public Processor, public SynthFilter {
    public:
      enum OtherStyle {
        kBandReject,
        kAllPass,
        kMetalComb,
        kMetalReverb,
        kDisperser,
        kCustom,
        kNumOtherStyles
      };

      static constexpr int kMaxStages = 64;
      static constexpr int kCombDelaySize = 8192;
      static constexpr int kNumCombs = 4;
      static constexpr int kReverbAllpassSize = 2048;
      static constexpr int kNumCustomBands = 24;
      static constexpr int kSubBlock = 32;
      static constexpr int kLanes = static_cast<int>(poly_float::kSize);

      // The number of disperser stages for the drive knob (a 0..1 amount).
      static int stagesForAmount(float amount) {
        return 1 + static_cast<int>(std::round(std::min(1.0f, std::max(0.0f, amount)) * (kMaxStages - 1)));
      }

      // While a scope is alive, filters made on this thread find their curves in the table (set * 3 + curve).
      // A filter takes the set from its parameter prefix: filter_1 = 0, filter_2 = 1, filter_fx = 2, filter_fx_n = n + 1.
      class CurveScope {
        public:
          explicit CurveScope(const LineGenerator* const* table) : previous_(current()) { current() = table; }
          ~CurveScope() { current() = previous_; }
          static const LineGenerator* const*& current() {
            static thread_local const LineGenerator* const* table = nullptr;
            return table;
          }

        private:
          const LineGenerator* const* previous_;
      };

      static int curveSetForPrefix(const std::string& prefix) {
        if (prefix == "filter_1")
          return 0;
        if (prefix == "filter_2")
          return 1;
        if (prefix == "filter_fx")
          return 2;
        if (prefix.compare(0, 10, "filter_fx_") == 0)
          return 1 + std::atoi(prefix.c_str() + 10);
        return -1;
      }

      // The magnitude (in dB) the filter has at a frequency, for the response display. Disperser and All-Pass are flat.
      static float responseDb(int style, float frequency, float midi_cutoff, float resonance, float blend,
                              const LineGenerator* const* curves, float sample_rate = 44100.0f);

      // The drawn response of the Custom model, in dB, at a frequency (before the cascade of bands realises it).
      static float customTargetDb(const LineGenerator* const* curves, float morph, float shift_octaves, float depth,
                                  float frequency);

      OtherFilter();
      virtual ~OtherFilter() { }

      virtual Processor* clone() const override { return new OtherFilter(*this); }
      virtual void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void reset(poly_mask reset_mask) override;
      void hardReset() override;
      void setupFilter(const FilterState& filter_state) override { filter_state_ = filter_state; }

      // The three drawn response curves of the Custom model (owned by the engine; may stay null).
      void setCurves(const LineGenerator* a, const LineGenerator* b, const LineGenerator* c) {
        curves_[0] = a;
        curves_[1] = b;
        curves_[2] = c;
      }

    private:
      struct Biquad {
        poly_float b0, b1, b2, a1, a2, z1, z2;
        void clearState() { z1 = 0.0f; z2 = 0.0f; }
      };

      static force_inline poly_float tickBiquad(Biquad& f, poly_float x) {
        poly_float y = f.b0 * x + f.z1;
        f.z1 = f.b1 * x - f.a1 * y + f.z2;
        f.z2 = f.b2 * x - f.a2 * y;
        return y;
      }

      static force_inline poly_float fromLanes(const float* lanes) { return poly_float(poly_float::load(lanes)); }

      void ensureDelays();
      void processBiquads(const poly_float* audio_in, int num_samples, bool all_pass);
      void processComb(const poly_float* audio_in, int num_samples);
      void processReverb(const poly_float* audio_in, int num_samples);
      void processDisperser(const poly_float* audio_in, int num_samples);
      void processCustom(const poly_float* audio_in, int num_samples);
      void updateCustomCoefficients(float morph, float shift_octaves, float depth);

      int last_style_;
      mono_float sample_rate_cache_;

      Biquad biquads_[2];

      std::vector<poly_float> comb_buffer_[kNumCombs];
      poly_float comb_damp_[kNumCombs];
      int comb_write_;

      std::vector<poly_float> reverb_allpass_[2];
      int reverb_allpass_write_[2];
      float reverb_size_;

      poly_float stage_coefficients_[kMaxStages];
      poly_float stage_state_[kMaxStages];

      const LineGenerator* curves_[3];
      Biquad bands_[kNumCustomBands];
      float custom_morph_, custom_shift_, custom_depth_;
      int custom_render_sum_;

      JUCE_LEAK_DETECTOR(OtherFilter)
  };
} // namespace vital
