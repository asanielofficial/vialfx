// VialFX - Utility effect. Licensed under GPL-3.0-or-later.
#include "utility.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vital {

  namespace {
    constexpr float kUtilTwoPi = 6.28318530717959f;
    float utilClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  }

  void Utility::Biquad::setLowpass(float freq, float sample_rate) {
    float w0 = kUtilTwoPi * utilClamp(freq, 10.0f, sample_rate * 0.45f) / sample_rate;
    float c = std::cos(w0), alpha = std::sin(w0) / (2.0f * 0.70710678f);
    float a0 = 1.0f + alpha;
    b0 = (1.0f - c) * 0.5f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
  }

  void Utility::Biquad::setHighpass(float freq, float sample_rate) {
    float w0 = kUtilTwoPi * utilClamp(freq, 10.0f, sample_rate * 0.45f) / sample_rate;
    float c = std::cos(w0), alpha = std::sin(w0) / (2.0f * 0.70710678f);
    float a0 = 1.0f + alpha;
    b0 = (1.0f + c) * 0.5f / a0;
    b1 = -(1.0f + c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
  }

  Utility::Utility() : Processor(kNumInputs, kNumOutputs), last_frequency_(-1.0f), haas_pos_(0),
                       gain_(1.0f), pan_(0.0f), spread_(1.0f), haas_(0.0f), bass_amount_(0.0f) {
    hardReset();
  }

  void Utility::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    last_frequency_ = -1.0f;
  }

  void Utility::hardReset() {
    for (int c = 0; c < 2; ++c) {
      dc_x_[c] = dc_y_[c] = 0.0f;
      for (auto& stage : low_[c])
        stage.reset();
      for (auto& stage : high_[c])
        stage.reset();
    }
    std::memset(haas_buffer_, 0, sizeof(haas_buffer_));
    haas_pos_ = 0;
  }

  void Utility::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Utility::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    const float sample_rate = static_cast<float>(getSampleRate());

    const float target_gain = std::pow(10.0f, utilClamp(input(kGain)->at(0)[0], -60.0f, 30.0f) / 20.0f);
    const float target_pan = utilClamp(input(kPan)->at(0)[0], -1.0f, 1.0f);
    const float target_spread = utilClamp(input(kSpread)->at(0)[0], 0.0f, 2.0f);
    const float target_haas = utilClamp(input(kHaas)->at(0)[0], -20.0f, 20.0f) * 0.001f * sample_rate;
    const bool invert_l = input(kInvertLeft)->at(0)[0] >= 0.5f;
    const bool invert_r = input(kInvertRight)->at(0)[0] >= 0.5f;
    const bool swap = input(kSwap)->at(0)[0] >= 0.5f;
    const bool dc_filter = input(kDcFilter)->at(0)[0] >= 0.5f;
    const bool mono_bass = input(kMonoBass)->at(0)[0] >= 0.5f;
    const float frequency = utilClamp(input(kMonoFrequency)->at(0)[0], 20.0f, 1000.0f);   // exponential controls already deliver Hz

    if (std::fabs(frequency - last_frequency_) > 1.0e-3f) {
      last_frequency_ = frequency;
      for (int c = 0; c < 2; ++c)
      {
        for (auto& stage : low_[c])
          stage.setLowpass(frequency, sample_rate);
        for (auto& stage : high_[c])
          stage.setHighpass(frequency, sample_rate);
      }
    }

    const float smooth = 1.0f - std::exp(-1.0f / (0.01f * sample_rate));
    const float dc_coeff = 1.0f - kUtilTwoPi * 8.0f / sample_rate;   // one-pole high-pass at ~8 Hz
    const float target_bass = mono_bass ? 1.0f : 0.0f;

    for (int i = 0; i < num_samples; ++i) {
      gain_ += (target_gain - gain_) * smooth;
      pan_ += (target_pan - pan_) * smooth;
      spread_ += (target_spread - spread_) * smooth;
      haas_ += (target_haas - haas_) * smooth;
      bass_amount_ += (target_bass - bass_amount_) * smooth;

      float x[2] = { audio_in[i][0], audio_in[i][1] };
      if (invert_l) x[0] = -x[0];
      if (invert_r) x[1] = -x[1];
      if (swap) std::swap(x[0], x[1]);

      if (dc_filter) {
        for (int c = 0; c < 2; ++c) {
          float y = x[c] - dc_x_[c] + dc_coeff * dc_y_[c];
          dc_x_[c] = x[c];
          dc_y_[c] = y;
          x[c] = y;
        }
      }
      else {
        for (int c = 0; c < 2; ++c) {
          dc_x_[c] = x[c];
          dc_y_[c] = 0.0f;
        }
      }

      // Mono bass: split with a Linkwitz-Riley pair, sum only the low band to mono, then recombine.
      float low[2], high[2];
      for (int c = 0; c < 2; ++c) {
        low[c] = low_[c][1].tick(low_[c][0].tick(x[c]));
        high[c] = high_[c][1].tick(high_[c][0].tick(x[c]));
      }
      if (bass_amount_ > 1.0e-4f || mono_bass) {
        float mono_low = 0.5f * (low[0] + low[1]);
        for (int c = 0; c < 2; ++c)
          x[c] += bass_amount_ * ((high[c] + mono_low) - x[c]);
      }

      // Width on mid/side.
      float mid = 0.5f * (x[0] + x[1]), side = 0.5f * (x[0] - x[1]) * spread_;
      x[0] = mid + side;
      x[1] = mid - side;

      // Haas: positive delays the right channel, negative the left.
      haas_buffer_[0][haas_pos_] = x[0];
      haas_buffer_[1][haas_pos_] = x[1];
      float delay = std::fabs(haas_);
      int delayed = haas_ >= 0.0f ? 1 : 0;
      float read = static_cast<float>(haas_pos_) - delay;
      if (read < 0.0f) read += kHaasBufferSize;
      int i0 = static_cast<int>(read) & (kHaasBufferSize - 1);
      int i1 = (i0 + 1) & (kHaasBufferSize - 1);
      float frac = read - std::floor(read);
      x[delayed] = haas_buffer_[delayed][i0] + frac * (haas_buffer_[delayed][i1] - haas_buffer_[delayed][i0]);
      haas_pos_ = (haas_pos_ + 1) & (kHaasBufferSize - 1);

      // Pan as balance: the far side is attenuated, the near side keeps its level.
      float gl = pan_ > 0.0f ? 1.0f - pan_ : 1.0f;
      float gr = pan_ < 0.0f ? 1.0f + pan_ : 1.0f;
      audio_out[i] = poly_float(x[0] * gl * gain_, x[1] * gr * gain_, 0.0f, 0.0f);
    }
  }
} // namespace vital
