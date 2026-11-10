// VialFX - Tremolo effect. Licensed under GPL-3.0-or-later.
#include "tremolo.h"

#include "synth_constants.h"
#include "vibrato.h"

#include <algorithm>
#include <cmath>

namespace vital {

  Tremolo::Tremolo() : Processor(kNumInputs, kNumOutputs), amount_a_(0.0f), amount_b_(0.0f), rate_a_(5.0f),
                       rate_b_(5.0f), gain_(1.0f) {
    hardReset();
  }

  void Tremolo::hardReset() {
    lfo_a_.reset(22222u);
    lfo_b_.reset(98765u);
    gain_ = 1.0f;
  }

  float Tremolo::shape(int type, float p, float rnd_prev, float rnd_next) {
    // Same bipolar shapes as the vibrato, folded to 0..1.
    return 0.5f + 0.5f * Vibrato::wave(type, p, rnd_prev, rnd_next);
  }

  float Tremolo::rateHz(int rate_input, int unit_input, int tempo_input) const {
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

  void Tremolo::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Tremolo::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    const float sample_rate = static_cast<float>(getSampleRate());
    const int type_a = std::min(std::max(static_cast<int>(input(kModA)->at(0)[0] + 0.5f), 0), 5);
    const int type_b = std::min(std::max(static_cast<int>(input(kModB)->at(0)[0] + 0.5f), 0), 5);
    const float target_rate_a = rateHz(kRateA, kUnitA, kTempoA), target_rate_b = rateHz(kRateB, kUnitB, kTempoB);
    const float target_amount_a = std::min(std::max(input(kAmountA)->at(0)[0], 0.0f), 1.0f);
    const float target_amount_b = std::min(std::max(input(kAmountB)->at(0)[0], 0.0f), 1.0f);
    const float smooth = 1.0f - std::exp(-1.0f / (0.02f * sample_rate));
    const float edge = 1.0f - std::exp(-1.0f / (0.0004f * sample_rate));   // 0.4 ms: takes the click off square edges

    for (int i = 0; i < num_samples; ++i) {
      amount_a_ += (target_amount_a - amount_a_) * smooth;
      amount_b_ += (target_amount_b - amount_b_) * smooth;
      rate_a_ += (target_rate_a - rate_a_) * smooth;
      rate_b_ += (target_rate_b - rate_b_) * smooth;

      const float target_gain = std::max(1.0f - amount_a_ * (1.0f - shape(type_a, lfo_a_.phase, lfo_a_.prev, lfo_a_.next))
                                              - amount_b_ * (1.0f - shape(type_b, lfo_b_.phase, lfo_b_.prev, lfo_b_.next)),
                                         0.0f);
      gain_ += (target_gain - gain_) * edge;

      for (Lfo* lfo : { &lfo_a_, &lfo_b_ }) {
        float rate = lfo == &lfo_a_ ? rate_a_ : rate_b_;
        lfo->phase += rate / sample_rate;
        if (lfo->phase >= 1.0f) {
          lfo->phase -= std::floor(lfo->phase);
          lfo->prev = lfo->next;
          lfo->next = lfo->rand();
        }
      }
      audio_out[i] = audio_in[i] * gain_;
    }
    output(kGain)->buffer[0] = poly_float(gain_, gain_, 0.0f, 0.0f);
  }
} // namespace vital
