// VialFX - Resonator effect. Licensed under GPL-3.0-or-later.
#include "resonator.h"

#include "synth_constants.h"
#include "tuning.h"

#include <algorithm>
#include <cmath>

namespace vital {

  namespace {
    constexpr float kResTwoPi = 6.28318530717959f;
    constexpr float kResLn1000 = 6.90775527898214f;      // ln(1000): -60 dB
    constexpr float kResBoost = 3.0f;                   // loudness of the wet signal against a pure power normalisation
    const float kResModalRatios[Resonator::kMaxPartials] = { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.64f, 24.82f, 32.9f, 41.8f, 51.7f };
    const float kResFormantHz[5] = { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4500.0f };
    const float kResFormantGain[5] = { 1.0f, 0.8f, 0.55f, 0.4f, 0.25f };
    const int kResVoiceCounts[4] = { 1, 4, 8, 16 };

    inline float resClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  }

  void Resonator::Voice::clear() {
    for (int c = 0; c < 2; ++c) {
      std::fill(line[c].begin(), line[c].end(), 0.0f);
      std::fill(pick[c].begin(), pick[c].end(), 0.0f);
      lp[c] = 0.0f;
      for (int k = 0; k < 3; ++k) { ap_x[c][k] = 0.0f; ap_y[c][k] = 0.0f; }
      for (int k = 0; k < kMaxPartials; ++k) { y1[c][k] = 0.0f; y2[c][k] = 0.0f; }
    }
    write = 0;
    gain = 0.0f;
  }

  Resonator::Resonator() : Processor(kNumInputs, kNumOutputs), midi_(nullptr), tuning_(nullptr), mask_(0),
                           pick_mask_(0), mix_(0.0f), color_(0.0f), last_frequency_(0.0f), last_active_voices_(0) {
    tilt_lp_[0] = tilt_lp_[1] = 0.0f;
    for (auto& w : wet_)
      w.assign(kMaxBufferSize * 8, 0.0f);
    hardReset();
  }

  void Resonator::allocate() {
    int size = 1;
    while (size < static_cast<int>(getSampleRate() / 16.0) + 8)
      size <<= 1;
    int pick_size = std::max(size / 4, 16);
    mask_ = size - 1;
    pick_mask_ = pick_size - 1;
    for (auto& voice : voices_) {
      for (int c = 0; c < 2; ++c) {
        voice.line[c].assign(size, 0.0f);
        voice.pick[c].assign(pick_size, 0.0f);
      }
      voice.id = -1;
      voice.fresh = true;
      voice.active = false;
      voice.gain = 0.0f;
      voice.write = 0;
    }
    allocated_rate_ = getSampleRate();
  }

  void Resonator::setSampleRate(int sample_rate) {
    bool was_prepared = allocated_rate_ != 0;
    Processor::setSampleRate(sample_rate);
    if (was_prepared)
      allocate();
  }

  void Resonator::hardReset() {
    for (auto& voice : voices_) {
      voice.clear();
      voice.id = -1;
      voice.fresh = true;
      voice.active = false;
    }
    tilt_lp_[0] = tilt_lp_[1] = 0.0f;
  }

  float Resonator::hermite(const std::vector<float>& buf, int mask, float read) {
    const float floor_read = std::floor(read), frac = read - floor_read;
    const int i1 = static_cast<int>(floor_read);
    const float y0 = buf[(i1 - 1) & mask], y1 = buf[i1 & mask], y2 = buf[(i1 + 1) & mask], y3 = buf[(i1 + 2) & mask];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  int Resonator::gatherTargets(Target* targets, int max_count, int keytrack, bool mpe, float bend_range, bool scale) {
    int n = 0;
    if (keytrack == 1 && midi_) {
      const ResonatorMidi::Entry* entries[ResonatorMidi::kEntries];
      const int found = midi_->collect(std::min(max_count, static_cast<int>(kMaxVoices)), entries);
      for (int i = 0; i < found; ++i) {
        const ResonatorMidi::Entry* e = entries[i];
        float note = static_cast<float>(e->note);
        if (scale && tuning_)
          note = tuning_->convertMidiNote(e->note);
        const float range = mpe ? bend_range : 2.0f;
        const float semis = note + midi_->bend(e->channel) * range + (mpe ? midi_->zoneBend() * 2.0f : 0.0f);
        targets[n++] = { e->id, semis, midi_->slide(e->channel), std::max(midi_->pressure(e->channel), e->poly_pressure) };
      }
    }
    if (n == 0)
      targets[n++] = { -2, 48.0f, 0.0f, 0.0f };   // no MIDI yet (or key track off): C3
    return n;
  }

  void Resonator::assignVoices(const Target* targets, int count) {
    bool taken[kMaxVoices] = {};
    int slot[kMaxVoices];
    for (auto& v : voices_)
      v.active = false;
    for (int i = 0; i < count; ++i) {
      slot[i] = -1;
      for (int v = 0; v < kMaxVoices; ++v) {
        if (!taken[v] && voices_[v].id == targets[i].id) {
          slot[i] = v;
          taken[v] = true;
          break;
        }
      }
    }
    for (int i = 0; i < count; ++i) {
      if (slot[i] >= 0)
        continue;
      int best = -1;
      for (int v = 0; v < kMaxVoices; ++v)
        if (!taken[v] && (best < 0 || voices_[v].gain < voices_[best].gain))
          best = v;
      slot[i] = best;
      taken[best] = true;
    }
    for (int i = 0; i < count; ++i) {
      Voice& voice = voices_[slot[i]];
      if (voice.id != targets[i].id) {
        if (voice.gain < 0.001f) {
          voice.clear();
          voice.fresh = true;
        }
        voice.id = targets[i].id;
      }
      voice.active = true;
      voice.t_semis = targets[i].semis;
      voice.t_slide = targets[i].slide;
      voice.t_pressure = targets[i].pressure;
    }
  }

  void Resonator::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Resonator::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    prepare();
    const float sr = static_cast<float>(getSampleRate());
    const int n = std::min(num_samples, static_cast<int>(wet_[0].size()));
    auto in = [&](int k) { return input(k)->at(0)[0]; };

    const int style = static_cast<int>(resClamp(in(kStyle) + 0.5f, 0.0f, 3.0f));
    const int keytrack = in(kKeytrack) >= 0.5f ? 1 : 0;
    const int max_voices = kResVoiceCounts[static_cast<int>(resClamp(in(kVoices) + 0.5f, 0.0f, 3.0f))];
    const float tune = in(kTune), fine = in(kFine) * 0.01f;
    const float stretch = resClamp(in(kStretch), -1.0f, 1.0f);
    const float ref = resClamp(in(kRefPitch), 300.0f, 600.0f);
    const float glide = resClamp(in(kGlide), 0.0f, 1.0f);
    const float slide_amt = resClamp(in(kSlide), 0.0f, 1.0f), press_amt = resClamp(in(kPressure), 0.0f, 1.0f);
    const int slide_target = static_cast<int>(in(kSlideTarget) + 0.5f), press_target = static_cast<int>(in(kPressureTarget) + 0.5f);

    Target targets[kMaxVoices];
    const int count = gatherTargets(targets, keytrack ? max_voices : 1, keytrack, in(kMpe) >= 0.5f,
                                    resClamp(in(kBendRange), 0.0f, 96.0f), in(kTuning) >= 0.5f);
    assignVoices(targets, count);
    last_active_voices_ = count;

    auto push = [](float x, float v, float amount) { return x + amount * v * (1.0f - x); };
    float avg_slide = 0.0f, avg_press = 0.0f;
    for (int i = 0; i < count; ++i) {
      avg_slide += targets[i].slide;
      avg_press += targets[i].pressure;
    }
    avg_slide /= count;
    avg_press /= count;
    float mix_target = resClamp(in(kMix), 0.0f, 1.0f), color_target = resClamp(in(kColor), -1.0f, 1.0f);
    if (press_target == 1) mix_target = push(mix_target, avg_press, press_amt);
    if (press_target == 2) color_target = push(color_target, avg_press, press_amt);
    if (slide_target == 1) color_target = push(color_target, avg_slide, slide_amt);
    mix_target = resClamp(mix_target, 0.0f, 1.0f);
    color_target = resClamp(color_target, -1.0f, 1.0f);

    for (int c = 0; c < 2; ++c)
      std::fill(wet_[c].begin(), wet_[c].begin() + n, 0.0f);

    const float glide_tau = 0.003f + glide * 0.6f;
    const float glide_coef = 1.0f - std::exp(-static_cast<float>(n) / (sr * glide_tau));
    const float fade_in = 1.0f - std::exp(-1.0f / (0.005f * sr));
    const float fade_out = 1.0f - std::exp(-1.0f / (0.03f * sr));
    const int size = mask_ + 1;
    bool first = true;

    for (Voice& voice : voices_) {
      if (!voice.active && voice.gain < 0.001f) {
        if (voice.id != -1) { voice.clear(); voice.id = -1; voice.fresh = true; }
        continue;
      }

      // Per voice destinations: damping and resonance (slide and pressure may act on them).
      float resonance = resClamp(in(kResonance), 0.0f, 1.0f);
      float damp = resClamp((in(kDamping) - 8.0f) / 128.0f, 0.0f, 1.0f);
      if (slide_target == 0) damp = push(damp, voice.t_slide, slide_amt);
      if (slide_target == 2) resonance = push(resonance, voice.t_slide, slide_amt);
      if (press_target == 0) resonance = push(resonance, voice.t_pressure, press_amt);
      damp = resClamp(damp, 0.0f, 1.0f);
      resonance = resClamp(resonance, 0.0f, 1.0f);

      if (voice.fresh)
        voice.semis = voice.t_semis;
      else
        voice.semis += (voice.t_semis - voice.semis) * glide_coef;

      float f0 = ref * std::exp2((voice.semis + tune + fine - 69.0f) / 12.0f);
      f0 = resClamp(f0, 16.0f, std::min(16000.0f, 0.3f * sr));
      if (first) { last_frequency_ = f0; first = false; }

      const float decay = 0.01f * std::pow(500.0f, resonance);                 // seconds to -60 dB
      const float damp_hz = 440.0f * std::exp2((8.0f + 128.0f * damp - 69.0f) / 12.0f);
      const float fade = voice.active ? fade_in : fade_out;
      const float fade_target = voice.active ? 1.0f : 0.0f;

      if (style == 0 || style == 2) {
        const float fc = std::max(damp_hz, f0 * 2.0f);
        const float a_lp = resClamp(1.0f - std::exp(-kResTwoPi * fc / sr), 0.001f, 1.0f);
        const float comp_lp = std::min((1.0f - a_lp) / a_lp, 0.35f * sr / f0);
        const float ap = resClamp(-0.5f * stretch, -0.6f, 0.6f);
        const float comp_ap = 3.0f * (1.0f - ap) / (1.0f + ap);
        const float target_delay = resClamp(sr / f0 - comp_lp - comp_ap - 1.0f, 3.0f, static_cast<float>(size - 8));
        if (voice.fresh)
          voice.delay = target_delay;
        const float step = (target_delay - voice.delay) / n;
        const float g = std::min(std::exp(-kResLn1000 / (f0 * decay)), 0.9995f);
        const float scale = std::min(std::sqrt(std::max(1.0f - g * g, 1e-6f)) * kResBoost, 1.0f);
        const bool use_pick = style == 2;
        const int pick_delay = resClamp(0.2f * target_delay, 1.0f, static_cast<float>(pick_mask_));

        int w = voice.write;
        for (int i = 0; i < n; ++i) {
          voice.delay += step;
          const float read = static_cast<float>(w) - voice.delay;
          const float vg = voice.gain;
          for (int c = 0; c < 2; ++c) {
            float x = audio_in[i][c];
            if (use_pick) {
              voice.pick[c][w & pick_mask_] = x;
              x -= 0.8f * voice.pick[c][(w - pick_delay) & pick_mask_];
            }
            const float delayed = hermite(voice.line[c], mask_, read);
            voice.lp[c] += a_lp * (delayed - voice.lp[c]);
            float s = voice.lp[c];
            for (int k = 0; k < 3; ++k) {
              const float y = ap * s + voice.ap_x[c][k] - ap * voice.ap_y[c][k];
              voice.ap_x[c][k] = s;
              voice.ap_y[c][k] = y;
              s = y;
            }
            const float y = x * scale + g * s;
            voice.line[c][w & mask_] = y;
            wet_[c][i] += y * vg;
          }
          w = (w + 1) & mask_;
          voice.gain += (fade_target - voice.gain) * fade;
        }
        voice.write = w;
        voice.delay = target_delay;
      }
      else {
        float a1[kMaxPartials], a2[kMaxPartials], b0[kMaxPartials], pg[kMaxPartials];
        int np = 0;
        const float limit = std::min(18000.0f, 0.45f * sr);
        if (style == 1) {
          for (int k = 0; k < kMaxPartials; ++k) {
            const float fk = f0 * kResModalRatios[k] * (1.0f + 0.03f * stretch * k);
            if (fk >= limit)
              break;
            const float tk = decay * resClamp(damp_hz / fk, 0.05f, 1.0f);
            const float r = std::min(std::exp(-kResLn1000 / (tk * sr)), 0.99995f);
            const float w = kResTwoPi * fk / sr;
            b0[np] = 2.0f * std::sin(w) * std::sqrt(1.0f - r) * kResBoost;
            a1[np] = 2.0f * r * std::cos(w);
            a2[np] = -r * r;
            pg[np] = std::pow(static_cast<float>(k + 1), -0.6f);
            ++np;
          }
        }
        else {
          const float shift = std::pow(f0 / 130.81f, 0.75f);
          const float q = 6.0f + resonance * 90.0f;
          for (int k = 0; k < 5; ++k) {
            const float fk = kResFormantHz[k] * shift * (1.0f + 0.25f * stretch * k / 4.0f);
            if (fk >= limit)
              break;
            const float r = std::min(std::exp(-3.14159265f * (fk / q) / sr), 0.99995f);
            const float w = kResTwoPi * fk / sr;
            b0[np] = 2.0f * std::sin(w) * std::sqrt(1.0f - r) * kResBoost;
            a1[np] = 2.0f * r * std::cos(w);
            a2[np] = -r * r;
            pg[np] = kResFormantGain[k] * resClamp(damp_hz / fk, 0.2f, 1.0f);
            ++np;
          }
        }
        float norm = 0.0f;
        for (int k = 0; k < np; ++k)
          norm += pg[k] * pg[k];
        norm = norm > 0.0f ? 1.0f / std::sqrt(norm) : 0.0f;

        for (int i = 0; i < n; ++i) {
          const float vg = voice.gain * norm;
          for (int c = 0; c < 2; ++c) {
            const float x = audio_in[i][c];
            float sum = 0.0f;
            for (int k = 0; k < np; ++k) {
              const float y = b0[k] * x + a1[k] * voice.y1[c][k] + a2[k] * voice.y2[c][k];
              voice.y2[c][k] = voice.y1[c][k];
              voice.y1[c][k] = y;
              sum += pg[k] * y;
            }
            wet_[c][i] += sum * vg;
          }
          voice.gain += (fade_target - voice.gain) * fade;
        }
      }
      voice.fresh = false;
    }

    // Colour tilt, soft clip and the dry/wet mix.
    const float voice_norm = 1.0f / std::sqrt(static_cast<float>(std::max(count, 1)));
    const float tilt_coef = 1.0f - std::exp(-kResTwoPi * 1000.0f / sr);
    for (int i = 0; i < n; ++i) {
      const float t = (i + 1.0f) / n;
      const float mix = mix_ + (mix_target - mix_) * t;
      const float color = color_ + (color_target - color_) * t;
      const float low_gain = std::exp2(-color), high_gain = std::exp2(color);
      float result[2];
      for (int c = 0; c < 2; ++c) {
        const float wet = wet_[c][i] * voice_norm;
        tilt_lp_[c] += tilt_coef * (wet - tilt_lp_[c]);
        const float tilted = tilt_lp_[c] * low_gain + (wet - tilt_lp_[c]) * high_gain;
        result[c] = audio_in[i][c] * (1.0f - mix) + std::tanh(tilted) * mix;
      }
      audio_out[i] = poly_float(result[0], result[1], 0.0f, 0.0f);
    }
    mix_ = mix_target;
    color_ = color_target;
  }
} // namespace vital
