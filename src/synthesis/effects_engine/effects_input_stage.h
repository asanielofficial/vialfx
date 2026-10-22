/* VialFX: processors that bring the plugin's input audio into the effects engine.
 *
 * Signal flow:  audio in -> AudioFilterRouter -> FiltersModule (Filter 1 / Filter 2) -> + raw bypass
 *               -> AudioUpsampler -> effects rack
 *
 * This file is part of VialFX and is licensed under the GNU General Public License v3 or later.
 */

#pragma once

#include "processor.h"
#include "utils.h"
#include "value.h"

namespace vital {

  // Decides which of Filter 1 / Filter 2 hears the input audio and whether it bypasses both.
  //
  //  - Neither filter on:        input goes straight through (kRawOut).
  //  - Parallel (default):       input feeds every filter that is on; their outputs are summed.
  //  - Serial Filter 1 -> 2:     Filter 2's "filter input" switch is on: input only enters Filter 1.
  //  - Serial Filter 2 -> 1:     Filter 1's "filter input" switch is on: input only enters Filter 2.
  //
  // This mirrors how FiltersModule combines its two inputs, so the filters module itself is unchanged.
  class AudioFilterRouter : public Processor {
    public:
      enum {
        kAudio,
        kNumInputs
      };

      enum {
        kToFilter1,
        kToFilter2,
        kRawOut,
        kNumOutputs
      };

      AudioFilterRouter() : Processor(kNumInputs, kNumOutputs),
                            filter_1_on_(nullptr), filter_2_on_(nullptr),
                            filter_1_from_filter_2_(nullptr), filter_2_from_filter_1_(nullptr) { }

      Processor* clone() const override { return new AudioFilterRouter(*this); }

      void setControls(const Value* filter_1_on, const Value* filter_2_on,
                       const Value* filter_1_from_filter_2, const Value* filter_2_from_filter_1) {
        filter_1_on_ = filter_1_on;
        filter_2_on_ = filter_2_on;
        filter_1_from_filter_2_ = filter_1_from_filter_2;
        filter_2_from_filter_1_ = filter_2_from_filter_1;
      }

      void process(int num_samples) override {
        const poly_float* audio = input(kAudio)->source->buffer;
        poly_float* to_filter_1 = output(kToFilter1)->buffer;
        poly_float* to_filter_2 = output(kToFilter2)->buffer;
        poly_float* raw = output(kRawOut)->buffer;

        if (filter_1_on_ == nullptr || filter_2_on_ == nullptr ||
            filter_1_from_filter_2_ == nullptr || filter_2_from_filter_1_ == nullptr) {
          utils::zeroBuffer(to_filter_1, num_samples);
          utils::zeroBuffer(to_filter_2, num_samples);
          utils::copyBuffer(raw, audio, num_samples);
          return;
        }

        // Same truthiness the filters use themselves (FilterModule: on > 0.5, FiltersModule: != 0).
        bool filter_1_on = filter_1_on_->value() > 0.5f;
        bool filter_2_on = filter_2_on_->value() > 0.5f;
        bool serial_backward = filter_1_from_filter_2_->value() != 0.0f && filter_1_on;
        bool serial_forward = !serial_backward && filter_2_from_filter_1_->value() != 0.0f && filter_2_on;

        bool audio_to_1 = true;
        bool audio_to_2 = true;
        if (serial_backward) {
          audio_to_2 = true;
          audio_to_1 = !filter_2_on;
        }
        else if (serial_forward) {
          audio_to_1 = true;
          audio_to_2 = !filter_1_on;
        }

        if (audio_to_1)
          utils::copyBuffer(to_filter_1, audio, num_samples);
        else
          utils::zeroBuffer(to_filter_1, num_samples);

        if (audio_to_2)
          utils::copyBuffer(to_filter_2, audio, num_samples);
        else
          utils::zeroBuffer(to_filter_2, num_samples);

        if (!filter_1_on && !filter_2_on)
          utils::copyBuffer(raw, audio, num_samples);
        else
          utils::zeroBuffer(raw, num_samples);
      }

    private:
      const Value* filter_1_on_;
      const Value* filter_2_on_;
      const Value* filter_1_from_filter_2_;
      const Value* filter_2_from_filter_1_;

      JUCE_LEAK_DETECTOR(AudioFilterRouter)
  };

  // Sample-and-hold upsampler that works as an ordinary processor inside the engine graph.
  // (vital::Upsampler expects to be driven by hand with the base-rate block size, so it can't sit in the graph.)
  class AudioUpsampler : public Processor {
    public:
      enum {
        kAudio,
        kNumInputs
      };

      AudioUpsampler() : Processor(kNumInputs, 1) { }

      Processor* clone() const override { return new AudioUpsampler(*this); }

      // The router hands us the oversampled block size.
      void process(int num_samples) override {
        int oversample = getOversampleAmount();
        if (oversample < 1)
          oversample = 1;
        int base_samples = num_samples / oversample;

        const poly_float* audio_in = input(kAudio)->source->buffer;
        poly_float* destination = output()->buffer;
        for (int i = 0; i < base_samples; ++i) {
          int offset = i * oversample;
          for (int s = 0; s < oversample; ++s)
            destination[offset + s] = audio_in[i];
        }
      }

    private:
      JUCE_LEAK_DETECTOR(AudioUpsampler)
  };
} // namespace vital
