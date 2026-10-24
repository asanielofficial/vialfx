/* VialFX: processors that bring the plugin's input audio into the effects engine.
 *
 * Signal flow:  audio in -> AudioFilterRouter -> FiltersModule (Filter 1 / Filter 2) -> + raw bypass
 *               -> AudioUpsampler -> effects rack
 *
 * This file is part of VialFX and is licensed under the GNU General Public License v3 or later.
 */

#pragma once

#include "processor.h"
#include "synth_constants.h"
#include "utils.h"
#include "value.h"

namespace vital {

  // One input "lane": on/off switch, level and pan in front of the routing. The gain changes are ramped over
  // the block so switching, level and pan modulation never click.
  //
  // level is the final linear gain (the engine control already applies the knob's quadratic curve);
  // pan is -1..1 with an equal-power law that is exactly unity per side at the centre.
  class AudioLane : public Processor {
    public:
      enum {
        kAudio,
        kOn,
        kLevel,
        kPan,
        kNumInputs
      };

      AudioLane() : Processor(kNumInputs, 1), gain_(0.0f), first_block_(true) { }

      Processor* clone() const override { return new AudioLane(*this); }

      void process(int num_samples) override {
        const poly_float* audio = input(kAudio)->source->buffer;
        poly_float* out = output()->buffer;

        float on = input(kOn)->source->buffer[0][0];
        float level = input(kLevel)->source->buffer[0][0];
        float pan = input(kPan)->source->buffer[0][0];
        pan = utils::clamp(pan, -1.0f, 1.0f);

        float angle = (pan + 1.0f) * 0.25f * kPi;
        float amp = on > 0.5f ? std::max(level, 0.0f) * kSqrt2 : 0.0f;
        poly_float target(amp * cosf(angle), amp * sinf(angle));

        if (first_block_) {
          gain_ = target;
          first_block_ = false;
        }

        poly_float step = (target - gain_) * (1.0f / std::max(num_samples, 1));
        poly_float gain = gain_;
        for (int i = 0; i < num_samples; ++i) {
          gain += step;
          out[i] = audio[i] * gain;
        }
        gain_ = target;
      }

    private:
      poly_float gain_;
      bool first_block_;

      JUCE_LEAK_DETECTOR(AudioLane)
  };

  // Routes the two audio inputs (main and sidechain) to the filters, the effects, or straight to the output.
  //
  // Each input has a destination, the same list the synth uses for its oscillators:
  //  - Filter 1 / Filter 2 / Filter 1+2: the input feeds those filters (a filter that is switched off is
  //    skipped; if every chosen filter is off the input goes to the effects instead of vanishing).
  //  - Effects:     the input skips the filters and enters the effect rack.
  //  - Direct out:  the input skips the filters and the effect rack and is added at the output.
  //
  // Serial routing (a filter's FIL1/FIL2 switch) is applied afterwards, exactly as before:
  //  - Serial Filter 1 -> 2: Filter 2's "filter input" switch is on: it hears Filter 1 rather than the input.
  //  - Serial Filter 2 -> 1: Filter 1's "filter input" switch is on: it hears Filter 2 rather than the input.
  //
  // This mirrors how FiltersModule combines its two inputs, so the filters module itself is unchanged.
  class AudioFilterRouter : public Processor {
    public:
      enum {
        kAudio,
        kSidechain,
        kNumInputs
      };

      enum {
        kToFilter1,
        kToFilter2,
        kRawOut,
        kDirectOut,
        kNumOutputs
      };

      AudioFilterRouter() : Processor(kNumInputs, kNumOutputs),
                            filter_1_on_(nullptr), filter_2_on_(nullptr),
                            filter_1_from_filter_2_(nullptr), filter_2_from_filter_1_(nullptr),
                            main_destination_(nullptr), sidechain_destination_(nullptr) { }

      Processor* clone() const override { return new AudioFilterRouter(*this); }

      void setControls(const Value* filter_1_on, const Value* filter_2_on,
                       const Value* filter_1_from_filter_2, const Value* filter_2_from_filter_1) {
        filter_1_on_ = filter_1_on;
        filter_2_on_ = filter_2_on;
        filter_1_from_filter_2_ = filter_1_from_filter_2;
        filter_2_from_filter_1_ = filter_2_from_filter_1;
      }

      void setDestinations(const Value* main_destination, const Value* sidechain_destination) {
        main_destination_ = main_destination;
        sidechain_destination_ = sidechain_destination;
      }

      void process(int num_samples) override {
        const poly_float* main = input(kAudio)->source->buffer;
        const poly_float* side = input(kSidechain)->source->buffer;
        poly_float* to_filter_1 = output(kToFilter1)->buffer;
        poly_float* to_filter_2 = output(kToFilter2)->buffer;
        poly_float* raw = output(kRawOut)->buffer;
        poly_float* direct = output(kDirectOut)->buffer;

        if (filter_1_on_ == nullptr || filter_2_on_ == nullptr ||
            filter_1_from_filter_2_ == nullptr || filter_2_from_filter_1_ == nullptr ||
            main_destination_ == nullptr || sidechain_destination_ == nullptr) {
          utils::zeroBuffer(to_filter_1, num_samples);
          utils::zeroBuffer(to_filter_2, num_samples);
          utils::copyBuffer(raw, main, num_samples);
          utils::zeroBuffer(direct, num_samples);
          return;
        }

        // Same truthiness the filters use themselves (FilterModule: on > 0.5, FiltersModule: != 0).
        bool filter_1_on = filter_1_on_->value() > 0.5f;
        bool filter_2_on = filter_2_on_->value() > 0.5f;

        utils::zeroBuffer(to_filter_1, num_samples);
        utils::zeroBuffer(to_filter_2, num_samples);
        utils::zeroBuffer(raw, num_samples);
        utils::zeroBuffer(direct, num_samples);

        const poly_float* sources[2] = { main, side };
        const Value* destinations[2] = { main_destination_, sidechain_destination_ };
        for (int i = 0; i < 2; ++i) {
          int destination = static_cast<int>(destinations[i]->value());
          bool wants_1 = destination == constants::kFilter1 || destination == constants::kDualFilters;
          bool wants_2 = destination == constants::kFilter2 || destination == constants::kDualFilters;
          bool to_1 = wants_1 && filter_1_on;
          bool to_2 = wants_2 && filter_2_on;

          if (destination == constants::kDirectOut)
            addBuffer(direct, sources[i], num_samples);
          else if (to_1 || to_2) {
            if (to_1)
              addBuffer(to_filter_1, sources[i], num_samples);
            if (to_2)
              addBuffer(to_filter_2, sources[i], num_samples);
          }
          else
            addBuffer(raw, sources[i], num_samples);
        }

        // Serial routing: the filter being fed by the other filter stops listening to the inputs.
        bool serial_backward = filter_1_from_filter_2_->value() != 0.0f && filter_1_on;
        bool serial_forward = !serial_backward && filter_2_from_filter_1_->value() != 0.0f && filter_2_on;
        if (serial_backward && filter_2_on)
          utils::zeroBuffer(to_filter_1, num_samples);
        else if (serial_forward && filter_1_on)
          utils::zeroBuffer(to_filter_2, num_samples);
      }

    private:
      static void addBuffer(poly_float* destination, const poly_float* source, int num_samples) {
        for (int i = 0; i < num_samples; ++i)
          destination[i] += source[i];
      }

      const Value* filter_1_on_;
      const Value* filter_2_on_;
      const Value* filter_1_from_filter_2_;
      const Value* filter_2_from_filter_1_;
      const Value* main_destination_;
      const Value* sidechain_destination_;

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
