// VialFX - Vibrato effect. Licensed under GPL-3.0-or-later.
#include "vibrato.h"

#include "synth_constants.h"

#include <algorithm>
#include <cmath>

namespace vital {

  namespace {
    constexpr float kVibratoCenter = 0.010f;      // seconds of centre delay
    constexpr float kVibratoHeadroom = 0.0092f;   // largest swing around it
    constexpr float kVibratoTwoPi = 6.28318530717959f;
  }

  Vibrato::Vibrato() : Processor(kNumInputs, kNumOutputs), mask_(0), write_(0), offset_(0.0f),
                       amount_a_(0.0f), amount_b_(0.0f), rate_a_(5.0f), rate_b_(5.0f) {
    allocate();
    hardReset();
  }

  void Vibrato::allocate() {
    int size = 1;
    while (size < static_cast<int>(0.03f * getSampleRate()) + 8)
      size <<= 1;
    mask_ = size - 1;
    for (auto& b : buffer_)
      b.assign(size, 0.0f);
    write_ = 0;
  }

  void Vibrato::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    allocate();
  }

  void Vibrato::hardReset() {
    for (auto& b : buffer_)
      std::fill(b.begin(), b.end(), 0.0f);
    lfo_a_.reset(22222u);
    lfo_b_.reset(98765u);
    offset_ = 0.0f;
    write_ = 0;
  }

  float Vibrato::wave(int type, float p, float rnd_prev, float rnd_next) {
    switch (type) {
      case 0: return std::sin(kVibratoTwoPi * p);
      case 1: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
      case 2: return 2.0f * p - 1.0f;
      case 3: return 1.0f - 2.0f * p;
      case 4: return p < 0.5f ? 1.0f : -1.0f;
      default: {
        float s = p * p * (3.0f - 2.0f * p);
        return rnd_prev + (rnd_next - rnd_prev) * s;
      }
    }
  }

  float Vibrato::rateHz(int rate_input, int unit_input, int tempo_input) const {
    float hz;
    if (input(unit_input)->at(0)[0] >= 0.5f) {
      float index = std::min(std::max(input(tempo_input)->at(0)[0], 0.0f),
                             static_cast<float>(constants::kNumSyncedFrequencyRatios - 1));
      hz = constants::kSyncedFrequencyRatios[static_cast<int>(index + 0.3f)] * input(kBeatsPerSecond)->at(0)[0];
    }
    else
      hz = input(rate_input)->at(0)[0];   // exponential controls already deliver Hz
    return std::min(std::max(hz, 0.0f), 60.0f);
  }

  void Vibrato::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Vibrato::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    const float sample_rate = static_cast<float>(getSampleRate());
    const int type_a = std::min(std::max(static_cast<int>(input(kModA)->at(0)[0] + 0.5f), 0), 5);
    const int type_b = std::min(std::max(static_cast<int>(input(kModB)->at(0)[0] + 0.5f), 0), 5);
    const float target_rate_a = rateHz(kRateA, kUnitA, kTempoA), target_rate_b = rateHz(kRateB, kUnitB, kTempoB);
    const float target_amount_a = std::min(std::max(input(kAmountA)->at(0)[0], 0.0f), 1200.0f);
    const float target_amount_b = std::min(std::max(input(kAmountB)->at(0)[0], 0.0f), 1200.0f);
    const float smooth = 1.0f - std::exp(-1.0f / (0.02f * sample_rate));
    const float leak = kVibratoTwoPi * 0.1f / sample_rate;
    const int size = mask_ + 1;
    float cents = 0.0f;

    for (int i = 0; i < num_samples; ++i) {
      amount_a_ += (target_amount_a - amount_a_) * smooth;
      amount_b_ += (target_amount_b - amount_b_) * smooth;
      rate_a_ += (target_rate_a - rate_a_) * smooth;
      rate_b_ += (target_rate_b - rate_b_) * smooth;

      cents = amount_a_ * wave(type_a, lfo_a_.phase, lfo_a_.prev, lfo_a_.next) +
              amount_b_ * wave(type_b, lfo_b_.phase, lfo_b_.prev, lfo_b_.next);
      for (Lfo* lfo : { &lfo_a_, &lfo_b_ }) {
        float rate = lfo == &lfo_a_ ? rate_a_ : rate_b_;
        lfo->phase += rate / sample_rate;
        if (lfo->phase >= 1.0f) {
          lfo->phase -= std::floor(lfo->phase);
          lfo->prev = lfo->next;
          lfo->next = lfo->rand();
        }
      }

      // Delay slope = 1 - pitch ratio; the leak keeps the integral centred.
      const float ratio = std::exp2(cents * (1.0f / 1200.0f));
      offset_ += (1.0f - ratio) / sample_rate - offset_ * leak;
      const float swing = kVibratoHeadroom * std::tanh(offset_ / kVibratoHeadroom);
      const float delay_samples = (kVibratoCenter + swing) * sample_rate;

      float result[2];
      const float read = static_cast<float>(write_) - delay_samples;
      const float floor_read = std::floor(read), frac = read - floor_read;
      const int i1 = static_cast<int>(floor_read);
      for (int c = 0; c < 2; ++c) {
        buffer_[c][write_] = audio_in[i][c];
        // 4-point Hermite interpolation
        const float y0 = buffer_[c][(i1 - 1) & mask_], y1 = buffer_[c][i1 & mask_],
                    y2 = buffer_[c][(i1 + 1) & mask_], y3 = buffer_[c][(i1 + 2) & mask_];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        result[c] = ((c3 * frac + c2) * frac + c1) * frac + y1;
      }
      write_ = (write_ + 1) & mask_;
      audio_out[i] = poly_float(result[0], result[1], 0.0f, 0.0f);
    }
    output(kCents)->buffer[0] = poly_float(cents, cents, 0.0f, 0.0f);
  }
} // namespace vital
