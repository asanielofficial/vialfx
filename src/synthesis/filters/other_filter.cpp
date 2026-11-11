// VialFX: the "Other" filter models. GPL-3.0-or-later, like the rest of the project.

#include "other_filter.h"

#include "futils.h"
#include "line_generator.h"

#include <algorithm>
#include <cmath>

namespace vital {

  namespace {
    constexpr float kOtherTwoPi = 6.283185307179586f;
    constexpr float kReverbMs[OtherFilter::kNumCombs] = { 4.7f, 6.9f, 10.1f, 13.3f };
    constexpr float kReverbAllpassMs[2] = { 1.9f, 3.1f };
    constexpr float kBandLowHz = 30.0f;
    constexpr float kBandHighHz = 18000.0f;
    constexpr float kCurveLowHz = 20.0f;
    constexpr float kCurveHighHz = 20000.0f;
    constexpr float kOtherMinDb = -60.0f;

    struct Coef { float b0, b1, b2, a1, a2; };

    Coef notchCoef(float w0, float q) {
      float cosw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q), a0 = 1.0f + alpha;
      return { 1.0f / a0, -2.0f * cosw / a0, 1.0f / a0, -2.0f * cosw / a0, (1.0f - alpha) / a0 };
    }

    Coef peakCoef(float w0, float q, float gain_db) {
      float amplitude = std::pow(10.0f, gain_db / 40.0f);
      float cosw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q), a0 = 1.0f + alpha / amplitude;
      return { (1.0f + alpha * amplitude) / a0, -2.0f * cosw / a0, (1.0f - alpha * amplitude) / a0,
               -2.0f * cosw / a0, (1.0f - alpha / amplitude) / a0 };
    }

    Coef allpassCoef(float w0, float q) {
      float cosw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q), a0 = 1.0f + alpha;
      return { (1.0f - alpha) / a0, -2.0f * cosw / a0, (1.0f + alpha) / a0, -2.0f * cosw / a0, (1.0f - alpha) / a0 };
    }

    float coefMagnitude(const Coef& c, float w) {
      float cw = std::cos(w), c2w = std::cos(2.0f * w), sw = std::sin(w), s2w = std::sin(2.0f * w);
      float nr = c.b0 + c.b1 * cw + c.b2 * c2w, ni = -(c.b1 * sw + c.b2 * s2w);
      float dr = 1.0f + c.a1 * cw + c.a2 * c2w, di = -(c.a1 * sw + c.a2 * s2w);
      return std::sqrt((nr * nr + ni * ni) / std::max(1e-12f, dr * dr + di * di));
    }

    // Band Reject: a notch whose width is the blend knob, flanked by two peaks (resonance) that sit just outside
    // the valley, like a phaser with the middle notch taken out.
    struct RejectShape { float q, offset_octaves, peak_db; };
    RejectShape rejectShape(float blend, float resonance) {
      float width = std::min(1.0f, std::max(0.0f, blend * 0.5f));
      float q = 0.25f * std::pow(120.0f, 1.0f - width);  // a narrow notch up to a very wide valley
      return { q, std::min(3.0f, 0.9f / q + 0.2f), std::min(1.0f, std::max(0.0f, resonance)) * 18.0f };
    }

    // Metal Comb: blend goes from a plain comb (0) through the metallic spacing (1) to a very inharmonic one (2).
    float combRatio(int comb, float blend) {
      static const float kPlain[4] = { 1.0f, 1.003f, 0.997f, 1.006f };
      static const float kMetal[4] = { 1.0f, 1.4142136f, 1.7320508f, 2.2360680f };
      static const float kWild[4] = { 1.0f, 1.618034f, 2.414214f, 3.732051f };
      blend = std::min(2.0f, std::max(0.0f, blend));
      if (blend < 1.0f)
        return kPlain[comb] + (kMetal[comb] - kPlain[comb]) * blend;
      return kMetal[comb] + (kWild[comb] - kMetal[comb]) * (blend - 1.0f);
    }

    // The reverb's tail is darkened by a one-pole low pass two octaves above the cutoff.
    float reverbDampHz(float midi_cutoff, float sample_rate) {
      return std::min(0.45f * sample_rate, std::max(100.0f, utils::midiNoteToFrequency(midi_cutoff) * 4.0f));
    }

    force_inline float otherClamp(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

    float otherReadCurve(const LineGenerator* curve, float phase) {
      if (curve == nullptr)
        return 1.0f;
      const mono_float* buffer = curve->getBuffer();
      int resolution = curve->resolution();
      float position = otherClamp(phase, 0.0f, 1.0f) * (resolution - 1);
      int index = std::min(resolution - 2, static_cast<int>(position));
      float t = position - index;
      return buffer[index] + (buffer[index + 1] - buffer[index]) * t;
    }
  } // namespace

  float OtherFilter::customTargetDb(const LineGenerator* const* curves, float morph, float shift_octaves,
                                     float depth, float frequency) {
    float position = otherClamp(morph, 0.0f, 2.0f);
    int from = std::min(1, static_cast<int>(position));
    float t = position - from;
    float lookup = frequency / std::pow(2.0f, shift_octaves);
    float phase = std::log(std::max(lookup, 1.0f) / kCurveLowHz) / std::log(kCurveHighHz / kCurveLowHz);
    float level = otherReadCurve(curves[from], phase) * (1.0f - t) + otherReadCurve(curves[from + 1], phase) * t;
    return kOtherMinDb * (1.0f - otherClamp(level, 0.0f, 1.0f)) * depth;
  }

  // All-Pass and Disperser do not change the level, so their display shows the phase turn instead: 0 (no turn) at
  // the bottom to 1 (the full turn of every stage) at the top.
  float OtherFilter::responsePhase(int style, float frequency, float midi_cutoff, float resonance, float blend,
                                   float sample_rate) {
    resonance = otherClamp(resonance, 0.0f, 1.0f);
    float cutoff = otherClamp(utils::midiNoteToFrequency(midi_cutoff), 10.0f, 0.45f * sample_rate);
    float w = kOtherTwoPi * std::min(frequency, 0.499f * sample_rate) / sample_rate;
    float warped = std::tan(w * 0.5f);

    if (style == kAllPass) {
      float q = 0.5f * std::pow(40.0f, resonance);
      float omega = warped / std::tan(kOtherTwoPi * cutoff / sample_rate * 0.5f);
      float turn = 2.0f * std::atan2(omega / q, 1.0f - omega * omega);  // 0 .. 2 pi per section
      return otherClamp(turn / kOtherTwoPi, 0.0f, 1.0f);
    }

    // Disperser: the display is the group delay (how long each frequency is held back), which is where the stages
    // show up. Pinched stages stack into one tall peak at the cutoff; spread stages make a low, wide plateau.
    int stages = stagesForAmount(otherClamp(blend, 0.0f, 2.0f) * 0.5f);
    float total = 0.0f;
    for (int stage = 0; stage < stages; ++stage) {
      float stage_cutoff = otherClamp(cutoff * disperserRatio(stage, stages, resonance), 10.0f,
                                      0.45f * sample_rate);
      float omega = warped / std::tan(kOtherTwoPi * stage_cutoff / sample_rate * 0.5f);
      total += 2.0f * omega / (1.0f + omega * omega);  // d(phase)/d(ln f), 1 at the stage frequency
    }
    return otherClamp(std::pow(total / stages, 0.7f), 0.0f, 1.0f);
  }

  float OtherFilter::responseDb(int style, float frequency, float midi_cutoff, float resonance, float blend,
                                const LineGenerator* const* curves, float sample_rate) {
    resonance = otherClamp(resonance, 0.0f, 1.0f);
    float cutoff = otherClamp(utils::midiNoteToFrequency(midi_cutoff), 10.0f, 0.45f * sample_rate);
    float w = kOtherTwoPi * std::min(frequency, 0.499f * sample_rate) / sample_rate;

    switch (style) {
      case kBandReject: {
        RejectShape shape = rejectShape(blend, resonance);
        float flank = std::pow(2.0f, shape.offset_octaves);
        float low = otherClamp(cutoff / flank, 10.0f, 0.45f * sample_rate);
        float high = otherClamp(cutoff * flank, 10.0f, 0.45f * sample_rate);
        float magnitude = coefMagnitude(notchCoef(kOtherTwoPi * cutoff / sample_rate, shape.q), w) *
                          coefMagnitude(peakCoef(kOtherTwoPi * low / sample_rate, 2.5f, shape.peak_db), w) *
                          coefMagnitude(peakCoef(kOtherTwoPi * high / sample_rate, 2.5f, shape.peak_db), w);
        return 20.0f * std::log10(std::max(magnitude, 1e-4f));
      }
      case kMetalComb:
      case kMetalReverb: {
        float feedback = style == kMetalComb ? 0.2f + 0.76f * resonance : 0.6f + 0.385f * resonance;
        float base = style == kMetalComb ? sample_rate / std::max(12.0f, cutoff) : 0.0f;
        float real = 0.0f, imag = 0.0f;
        // The reverb's damping filter sits in the feedback loop, so the cutoff shapes the tail's tone.
        float damp_r = 1.0f, damp_i = 0.0f;
        if (style == kMetalReverb) {
          float keep = std::exp(-kOtherTwoPi * reverbDampHz(midi_cutoff, sample_rate) / sample_rate);
          float dr = 1.0f - keep * std::cos(w), di = keep * std::sin(w);
          float norm = std::max(1e-9f, dr * dr + di * di);
          damp_r = (1.0f - keep) * dr / norm;
          damp_i = -(1.0f - keep) * di / norm;
        }
        for (int c = 0; c < kNumCombs; ++c) {
          float delay = style == kMetalComb ? base * combRatio(c, blend)
                                            : kReverbMs[c] * 0.001f * sample_rate * std::pow(2.0f, blend - 1.0f);
          float g = (c & 1) ? -feedback : feedback;
          float angle = -w * delay;
          // 1 / (1 - g * H * e^(-jwD)), with H = 1 for the comb
          float lr = std::cos(angle) * damp_r - std::sin(angle) * damp_i;
          float li = std::cos(angle) * damp_i + std::sin(angle) * damp_r;
          float dr = 1.0f - g * lr, di = -g * li;
          float norm = std::max(1e-9f, dr * dr + di * di);
          real += dr / norm;
          imag += -di / norm;
        }
        float gain = (style == kMetalComb ? 0.5f : 0.4f) * std::sqrt(1.0f - feedback * feedback);
        return 20.0f * std::log10(std::max(gain * std::sqrt(real * real + imag * imag), 1e-4f));
      }
      case kCustom: {
        float depth = 0.5f + resonance;
        float shift = (midi_cutoff - 60.0f) / 12.0f;
        return customTargetDb(curves, blend, shift, depth, frequency);
      }
      default:
        return 0.0f;
    }
  }

  OtherFilter::OtherFilter() : Processor(SynthFilter::kNumInputs, 1), last_style_(-1), sample_rate_cache_(0.0f),
                               comb_write_(0), reverb_size_(1.0f), custom_morph_(-1.0f), custom_shift_(0.0f),
                               custom_depth_(0.0f), custom_render_sum_(-1) {
    curves_[0] = curves_[1] = curves_[2] = nullptr;
    reverb_allpass_write_[0] = reverb_allpass_write_[1] = 0;
    hardReset();
  }

  void OtherFilter::ensureDelays() {
    if (comb_buffer_[0].empty()) {
      for (auto& buffer : comb_buffer_)
        buffer.assign(kCombDelaySize, poly_float(0.0f));
    }
    if (reverb_allpass_[0].empty()) {
      for (auto& buffer : reverb_allpass_)
        buffer.assign(kReverbAllpassSize, poly_float(0.0f));
    }
  }

  void OtherFilter::hardReset() {
    for (auto& f : biquads_) {
      f.b0 = 1.0f; f.b1 = 0.0f; f.b2 = 0.0f; f.a1 = 0.0f; f.a2 = 0.0f;
      f.clearState();
    }
    for (auto& f : bands_) {
      f.b0 = 1.0f; f.b1 = 0.0f; f.b2 = 0.0f; f.a1 = 0.0f; f.a2 = 0.0f;
      f.clearState();
    }
    for (auto& buffer : comb_buffer_)
      std::fill(buffer.begin(), buffer.end(), poly_float(0.0f));
    for (auto& buffer : reverb_allpass_)
      std::fill(buffer.begin(), buffer.end(), poly_float(0.0f));
    for (int i = 0; i < kNumCombs; ++i)
      comb_damp_[i] = 0.0f;
    for (int i = 0; i < kMaxStages; ++i) {
      stage_coefficients_[i] = 0.0f;
      stage_state_[i] = 0.0f;
    }
    custom_morph_ = -1.0f;
    custom_render_sum_ = -1;
  }

  void OtherFilter::reset(poly_mask reset_mask) {
    for (auto& f : biquads_) {
      f.z1 = utils::maskLoad(f.z1, 0.0f, reset_mask);
      f.z2 = utils::maskLoad(f.z2, 0.0f, reset_mask);
    }
    for (auto& f : bands_) {
      f.z1 = utils::maskLoad(f.z1, 0.0f, reset_mask);
      f.z2 = utils::maskLoad(f.z2, 0.0f, reset_mask);
    }
    for (int i = 0; i < kMaxStages; ++i)
      stage_state_[i] = utils::maskLoad(stage_state_[i], 0.0f, reset_mask);
    for (int i = 0; i < kNumCombs; ++i)
      comb_damp_[i] = utils::maskLoad(comb_damp_[i], 0.0f, reset_mask);
  }

  void OtherFilter::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void OtherFilter::processWithInput(const poly_float* audio_in, int num_samples) {
    filter_state_.loadSettings(this);
    sample_rate_cache_ = static_cast<mono_float>(getSampleRate());

    int style = std::max(0, std::min<int>(filter_state_.style, kNumOtherStyles - 1));
    if (style != last_style_) {
      hardReset();
      last_style_ = style;
    }

    poly_mask reset_mask = getResetMask(kReset);
    if (reset_mask.anyMask())
      reset(reset_mask);

    // Drive: soft-clips the input ahead of the filter (with a little makeup so it saturates rather than just gets louder).
    poly_float drive = filter_state_.drive;
    float max_drive = 1.0f, makeup_lanes[kLanes];
    for (int lane = 0; lane < kLanes; ++lane) {
      max_drive = std::max(max_drive, drive[lane]);
      makeup_lanes[lane] = 1.0f / std::sqrt(std::max(1.0f, drive[lane]));
    }
    if (max_drive > 1.001f) {
      poly_float makeup = fromLanes(makeup_lanes);
      for (int i = 0; i < num_samples; ++i)
        driven_[i] = futils::tanh(audio_in[i] * drive) * makeup;
      audio_in = driven_;
    }

    switch (style) {
      case kBandReject: processBiquads(audio_in, num_samples, false); break;
      case kAllPass: processBiquads(audio_in, num_samples, true); break;
      case kMetalComb: processComb(audio_in, num_samples); break;
      case kMetalReverb: processReverb(audio_in, num_samples); break;
      case kDisperser: processDisperser(audio_in, num_samples); break;
      default: processCustom(audio_in, num_samples); break;
    }
  }

  // Notch (Band Reject) or two cascaded second-order all-passes. Cutoff is the centre / corner, resonance the Q.
  void OtherFilter::processBiquads(const poly_float* audio_in, int num_samples, bool all_pass) {
    poly_float* out = output()->buffer;
    const poly_float* midi = filter_state_.midi_cutoff_buffer;
    float nyquist_limit = 0.45f * sample_rate_cache_;
    RejectShape shape = rejectShape(filter_state_.pass_blend[0], filter_state_.resonance_percent[0]);
    float flank = std::pow(2.0f, shape.offset_octaves);
    int sections = all_pass ? 2 : 3;

    for (int start = 0; start < num_samples; start += kSubBlock) {
      int count = std::min(static_cast<int>(kSubBlock), num_samples - start);
      poly_float midi_now = midi[start];
      float lanes[3][5][kLanes];
      for (int lane = 0; lane < kLanes; ++lane) {
        float frequency = otherClamp(utils::midiNoteToFrequency(midi_now[lane]), 10.0f, nyquist_limit);
        float w0 = kOtherTwoPi * frequency / sample_rate_cache_;
        Coef coefs[3];
        if (all_pass) {
          float q = 0.5f * std::pow(40.0f, otherClamp(filter_state_.resonance_percent[lane], 0.0f, 1.0f));
          coefs[0] = coefs[1] = allpassCoef(w0, q);
        }
        else {
          float low = otherClamp(frequency / flank, 10.0f, nyquist_limit);
          float high = otherClamp(frequency * flank, 10.0f, nyquist_limit);
          coefs[0] = notchCoef(w0, shape.q);
          coefs[1] = peakCoef(kOtherTwoPi * low / sample_rate_cache_, 2.5f, shape.peak_db);
          coefs[2] = peakCoef(kOtherTwoPi * high / sample_rate_cache_, 2.5f, shape.peak_db);
        }
        for (int s = 0; s < sections; ++s) {
          lanes[s][0][lane] = coefs[s].b0; lanes[s][1][lane] = coefs[s].b1; lanes[s][2][lane] = coefs[s].b2;
          lanes[s][3][lane] = coefs[s].a1; lanes[s][4][lane] = coefs[s].a2;
        }
      }

      for (int s = 0; s < sections; ++s) {
        biquads_[s].b0 = fromLanes(lanes[s][0]); biquads_[s].b1 = fromLanes(lanes[s][1]);
        biquads_[s].b2 = fromLanes(lanes[s][2]); biquads_[s].a1 = fromLanes(lanes[s][3]);
        biquads_[s].a2 = fromLanes(lanes[s][4]);
      }

      for (int i = start; i < start + count; ++i) {
        poly_float sample = audio_in[i];
        for (int s = 0; s < sections; ++s)
          sample = tickBiquad(biquads_[s], sample);
        out[i] = sample;
      }
    }
  }

  // Four feedback combs at inharmonic spacings (1, sqrt2, sqrt3, sqrt5 times the base period) with alternating
  // feedback signs and a damped loop: a dense, bell-like metallic ring. Cutoff is the pitch of the base period,
  // resonance the feedback, blend the loop damping.
  void OtherFilter::processComb(const poly_float* audio_in, int num_samples) {
    ensureDelays();
    poly_float* out = output()->buffer;
    const poly_float* midi = filter_state_.midi_cutoff_buffer;
    const int mask = kCombDelaySize - 1;

    for (int start = 0; start < num_samples; start += kSubBlock) {
      int count = std::min(static_cast<int>(kSubBlock), num_samples - start);
      poly_float midi_now = midi[start];
      float delays[kNumCombs][kLanes];
      for (int lane = 0; lane < kLanes; ++lane) {
        float frequency = otherClamp(utils::midiNoteToFrequency(midi_now[lane]), 12.0f, 0.45f * sample_rate_cache_);
        float base = sample_rate_cache_ / frequency;
        for (int c = 0; c < kNumCombs; ++c)
          delays[c][lane] = otherClamp(base * combRatio(c, filter_state_.pass_blend[0]), 2.0f, kCombDelaySize - 4.0f);
      }

      float feedback_amount = 0.2f + 0.76f * otherClamp(filter_state_.resonance_percent[0], 0.0f, 1.0f);
      float damping = 0.3f;
      poly_float feedback = poly_float(feedback_amount);
      poly_float lp_keep = poly_float(damping);
      poly_float lp_new = poly_float(1.0f - damping);
      poly_float output_gain = poly_float(0.5f * std::sqrt(1.0f - feedback_amount * feedback_amount));

      for (int i = start; i < start + count; ++i) {
        poly_float input_sample = audio_in[i];
        poly_float sum = 0.0f;
        for (int c = 0; c < kNumCombs; ++c) {
          float read[kLanes];
          for (int lane = 0; lane < kLanes; ++lane) {
            float position = static_cast<float>(comb_write_) - delays[c][lane];
            int whole = static_cast<int>(std::floor(position));
            float frac = position - whole;
            read[lane] = comb_buffer_[c][whole & mask][lane] * (1.0f - frac) +
                         comb_buffer_[c][(whole + 1) & mask][lane] * frac;
          }
          poly_float delayed = fromLanes(read);
          comb_damp_[c] = comb_damp_[c] * lp_keep + delayed * lp_new;
          poly_float signed_feedback = (c & 1) ? poly_float(0.0f) - feedback : feedback;
          poly_float value = input_sample + comb_damp_[c] * signed_feedback;
          comb_buffer_[c][comb_write_ & mask] = value;
          sum += value;
        }
        out[i] = sum * output_gain;
        comb_write_ = (comb_write_ + 1) & mask;
      }
    }
  }

  // A small Schroeder reverb tuned to inharmonic delay times, so the tail rings instead of washing out.
  // Cutoff is the tone of the tail, resonance the decay time, blend the size.
  void OtherFilter::processReverb(const poly_float* audio_in, int num_samples) {
    ensureDelays();
    poly_float* out = output()->buffer;
    const poly_float* midi = filter_state_.midi_cutoff_buffer;
    const int mask = kCombDelaySize - 1;
    const int allpass_mask = kReverbAllpassSize - 1;

    float target_size = std::pow(2.0f, otherClamp(filter_state_.pass_blend[0], 0.0f, 2.0f) - 1.0f);
    float feedback_amount = 0.6f + 0.385f * otherClamp(filter_state_.resonance_percent[0], 0.0f, 1.0f);
    poly_float feedback = poly_float(feedback_amount);
    poly_float output_gain = poly_float(0.4f * std::sqrt(1.0f - feedback_amount * feedback_amount));

    for (int start = 0; start < num_samples; start += kSubBlock) {
      int count = std::min(static_cast<int>(kSubBlock), num_samples - start);
      float size_step = (target_size - reverb_size_) * (1.0f / ((num_samples + kSubBlock - 1) / kSubBlock));
      reverb_size_ += size_step;

      float delays[kNumCombs];
      for (int c = 0; c < kNumCombs; ++c) {
        float samples = kReverbMs[c] * 0.001f * sample_rate_cache_ * reverb_size_;
        delays[c] = otherClamp(samples, 2.0f, kCombDelaySize - 4.0f);
      }
      float allpass_delays[2];
      for (int a = 0; a < 2; ++a)
        allpass_delays[a] = otherClamp(kReverbAllpassMs[a] * 0.001f * sample_rate_cache_, 2.0f, kReverbAllpassSize - 4.0f);

      float damp_lanes[kLanes];
      poly_float midi_now = midi[start];
      for (int lane = 0; lane < kLanes; ++lane) {
        // The cutoff is the tone of the tail.
        float frequency = reverbDampHz(midi_now[lane], sample_rate_cache_);
        damp_lanes[lane] = std::exp(-kOtherTwoPi * frequency / sample_rate_cache_);
      }
      poly_float lp_keep = fromLanes(damp_lanes);
      poly_float lp_new = poly_float(1.0f) - lp_keep;

      for (int i = start; i < start + count; ++i) {
        poly_float input_sample = audio_in[i];
        poly_float sum = 0.0f;
        for (int c = 0; c < kNumCombs; ++c) {
          float position = static_cast<float>(comb_write_) - delays[c];
          int whole = static_cast<int>(std::floor(position));
          float frac = position - whole;
          poly_float delayed = comb_buffer_[c][whole & mask] * (1.0f - frac) +
                               comb_buffer_[c][(whole + 1) & mask] * frac;
          comb_damp_[c] = comb_damp_[c] * lp_keep + delayed * lp_new;
          poly_float signed_feedback = (c & 1) ? poly_float(0.0f) - feedback : feedback;
          comb_buffer_[c][comb_write_ & mask] = input_sample + comb_damp_[c] * signed_feedback;
          sum += delayed;
        }
        poly_float value = sum * output_gain;

        for (int a = 0; a < 2; ++a) {
          int read_index = (reverb_allpass_write_[a] - static_cast<int>(allpass_delays[a])) & allpass_mask;
          poly_float delayed = reverb_allpass_[a][read_index];
          poly_float written = value + delayed * 0.6f;
          reverb_allpass_[a][reverb_allpass_write_[a]] = written;
          value = delayed - written * 0.6f;
          reverb_allpass_write_[a] = (reverb_allpass_write_[a] + 1) & allpass_mask;
        }

        out[i] = value;
        comb_write_ = (comb_write_ + 1) & mask;
      }
    }
  }

  // A chain of first-order all-passes: phase dispersion without changing the magnitude. The blend knob is the stage
  // count (1 to 256); cutoff is the centre of the dispersion and resonance pinches the stages in towards it (none = spread over eight octaves).
  void OtherFilter::processDisperser(const poly_float* audio_in, int num_samples) {
    poly_float* out = output()->buffer;
    const poly_float* midi = filter_state_.midi_cutoff_buffer;
    int stages = stagesForAmount(filter_state_.pass_blend[0] * 0.5f);
    float nyquist_limit = 0.45f * sample_rate_cache_;

    for (int start = 0; start < num_samples; start += kSubBlock * 2) {
      int count = std::min(kSubBlock * 2, num_samples - start);
      poly_float midi_now = midi[start];
      for (int stage = 0; stage < stages; ++stage) {
        float ratio = disperserRatio(stage, stages, filter_state_.resonance_percent[0]);
        float coefficients[kLanes];
        for (int lane = 0; lane < kLanes; ++lane) {
          float frequency = otherClamp(utils::midiNoteToFrequency(midi_now[lane]) * ratio, 10.0f, nyquist_limit);
          float t = std::tan(kOtherTwoPi * 0.5f * frequency / sample_rate_cache_);
          coefficients[lane] = (t - 1.0f) / (t + 1.0f);
        }
        stage_coefficients_[stage] = fromLanes(coefficients);
      }

      for (int i = start; i < start + count; ++i) {
        poly_float sample = audio_in[i];
        for (int stage = 0; stage < stages; ++stage) {
          poly_float y = stage_coefficients_[stage] * sample + stage_state_[stage];
          stage_state_[stage] = sample - stage_coefficients_[stage] * y;
          sample = y;
        }
        out[i] = sample;
      }
    }
  }

  // Three drawn frequency-response curves (top = pass, bottom = -60 dB, 20 Hz to 20 kHz left to right), blended
  // by the blend knob (0 = A, 1 = B, 2 = C) and realised as a cascade of 24 peaking bands. Cutoff slides the whole
  // response up and down in pitch, resonance sets how much of the drawn shape is applied.
  void OtherFilter::updateCustomCoefficients(float morph, float shift_octaves, float depth) {
    float sample_rate = sample_rate_cache_;
    float ratio = std::pow(kBandHighHz / kBandLowHz, 1.0f / (kNumCustomBands - 1));
    float spacing_octaves = std::log2(ratio);
    float bandwidth = spacing_octaves;
    float q = 1.0f / (2.0f * std::sinh(0.34657359f * bandwidth));

    // The drawn response at the centre of each band, then the band gains whose cascade reproduces it (neighbouring
    // peaking bands overlap, so each band's gain is corrected for what its neighbours already contribute).
    float target[kNumCustomBands], gains[kNumCustomBands];
    for (int band = 0; band < kNumCustomBands; ++band) {
      float frequency = kBandLowHz * std::pow(ratio, static_cast<float>(band));
      target[band] = customTargetDb(curves_, morph, shift_octaves, depth, frequency);
      gains[band] = target[band];
    }
    static const float kOverlap[5] = { 1.0f, 0.263f, 0.096f, 0.042f, 0.021f };
    for (int iteration = 0; iteration < 5; ++iteration) {
      for (int band = 0; band < kNumCustomBands; ++band) {
        float response = 0.0f;
        for (int other = std::max(0, band - 4); other <= std::min(kNumCustomBands - 1, band + 4); ++other)
          response += gains[other] * kOverlap[std::abs(other - band)];
        gains[band] = otherClamp(gains[band] + (target[band] - response) * 0.8f, -90.0f, 0.0f);
      }
    }

    for (int band = 0; band < kNumCustomBands; ++band) {
      float frequency = kBandLowHz * std::pow(ratio, static_cast<float>(band));
      float gain_db = gains[band];

      float amplitude = std::pow(10.0f, gain_db / 40.0f);
      float w0 = kOtherTwoPi * std::min(frequency, 0.45f * sample_rate) / sample_rate;
      float cosw = std::cos(w0);
      float alpha = std::sin(w0) / (2.0f * q);
      float a0 = 1.0f + alpha / amplitude;
      Biquad& f = bands_[band];
      f.b0 = (1.0f + alpha * amplitude) / a0;
      f.b1 = -2.0f * cosw / a0;
      f.b2 = (1.0f - alpha * amplitude) / a0;
      f.a1 = -2.0f * cosw / a0;
      f.a2 = (1.0f - alpha / amplitude) / a0;
    }
  }

  void OtherFilter::processCustom(const poly_float* audio_in, int num_samples) {
    poly_float* out = output()->buffer;
    float morph = filter_state_.pass_blend[0];
    float shift = (filter_state_.midi_cutoff_buffer[num_samples - 1][0] - 60.0f) / 12.0f;
    float depth = 0.5f + otherClamp(filter_state_.resonance_percent[0], 0.0f, 1.0f);
    int render_sum = 0;
    for (const LineGenerator* curve : curves_)
      render_sum += curve ? curve->getRenderCount() : 0;

    if (std::fabs(morph - custom_morph_) > 1e-4f || std::fabs(shift - custom_shift_) > 1e-3f ||
        std::fabs(depth - custom_depth_) > 1e-3f || render_sum != custom_render_sum_) {
      updateCustomCoefficients(morph, shift, depth);
      custom_morph_ = morph;
      custom_shift_ = shift;
      custom_depth_ = depth;
      custom_render_sum_ = render_sum;
    }

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = audio_in[i];
      for (int band = 0; band < kNumCustomBands; ++band)
        sample = tickBiquad(bands_[band], sample);
      out[i] = sample;
    }
  }
} // namespace vital
