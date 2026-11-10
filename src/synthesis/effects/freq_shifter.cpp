// VialFX - Frequency Shifter effect. Licensed under GPL-3.0-or-later.
#include "freq_shifter.h"

#include "synth_constants.h"

#include <algorithm>
#include <cmath>

namespace vital {

  // Polyphase all-pass coefficients (a2 of each z^-2 section) for 6/12/18/24 pole Hilbert pairs, ascending;
  // even entries form the real (fast) chain, odd entries the imaginary (slow) chain. Elliptic half-band design.
  static const float kHilbertCoefs[4][24] = {
    { 0.161706483f, 0.479394679f, 0.733435564f, 0.877692791f, 0.949293770f, 0.986297829f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },   // 6 poles, transition 0.0050 fs
    { 0.069378339f, 0.242674540f, 0.447819049f, 0.628616430f, 0.763524572f, 0.854654604f, 0.912671677f, 0.948383257f, 0.970031323f, 0.983210194f, 0.991548564f, 0.997429966f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },   // 12 poles, transition 0.0015 fs
    { 0.036425986f, 0.135642378f, 0.272934097f, 0.420702718f, 0.558012756f, 0.673774854f, 0.765057319f, 0.833777900f, 0.883883607f, 0.919625640f, 0.944751153f, 0.962252939f, 0.974391528f, 0.982819827f, 0.988726604f, 0.992965085f, 0.996157734f, 0.998779040f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },   // 18 poles, transition 0.0010 fs
    { 0.026612399f, 0.101002259f, 0.209064978f, 0.333284413f, 0.457936561f, 0.572061753f, 0.669928688f, 0.749948511f, 0.813134299f, 0.861771904f, 0.898521786f, 0.925917450f, 0.946142082f, 0.960969454f, 0.971787675f, 0.979656439f, 0.985371426f, 0.989523691f, 0.992549621f, 0.994770915f, 0.996425708f, 0.997692489f, 0.998708508f, 0.999584052f },   // 24 poles, transition 0.0004 fs
  };

  FreqShifter::Hilbert::Hilbert() {
    setQuality(1);
  }

  void FreqShifter::Hilbert::setQuality(int new_quality) {
    new_quality = std::min(std::max(new_quality, 0), 3);
    if (new_quality == quality)
      return;
    quality = new_quality;
    sections = 3 * (new_quality + 1);   // per chain: 3, 6, 9, 12 (6, 12, 18, 24 poles in total)
    for (int i = 0; i < sections; ++i) {
      fast[i].a2 = kHilbertCoefs[quality][2 * i];
      slow[i].a2 = kHilbertCoefs[quality][2 * i + 1];
    }
    reset();
  }

  void FreqShifter::Hilbert::tick(float x, float& real, float& imag) {
    float u = delayed;
    delayed = x;
    for (int i = 0; i < sections; ++i)
      u = slow[i].tick(u);
    float v = x;
    for (int i = 0; i < sections; ++i)
      v = fast[i].tick(v);
    real = v;
    imag = u;
  }

  void FreqShifter::Hilbert::reset() {
    delayed = 0.0f;
    for (auto& s : slow) { s.x1 = s.x2 = s.y1 = s.y2 = 0.0f; }
    for (auto& s : fast) { s.x1 = s.x2 = s.y1 = s.y2 = 0.0f; }
  }

  FreqShifter::FreqShifter() : Processor(kNumInputs, kNumOutputs), wet_(0.5f), feedback_(0.0f), stereo_(0.0f),
                               shift_(0.0f), fb_dir_(0.0f), range_hz_(100.0f) {
    hardReset();
  }

  void FreqShifter::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
  }

  void FreqShifter::hardReset() {
    for (int c = 0; c < 2; ++c) {
      main_[c].reset();
      feedback_shifter_[c].reset();
      for (auto& s : smear_[c]) s = Allpass1();
      feedback_state_[c] = 0.0f;
      phase_[c] = fb_phase_[c] = 0.0;
      shift_hz_[c] = fb_hz_[c] = 0.0f;
    }
  }

  float FreqShifter::rangeHz() const {
    const int unit = static_cast<int>(input(kUnit)->at(0)[0] + 0.5f);
    float hz = 100.0f;
    switch (unit) {
      case 0:
        hz = input(kFrequency)->at(0)[0];   // exponential controls already deliver Hz
        break;
      case 1:
        hz = 1.0f / std::max(input(kPeriod)->at(0)[0], 1.0e-5f);   // seconds
        break;
      case 2: case 3: case 4: {
        float index = std::min(std::max(input(kTempo)->at(0)[0], 0.0f),
                               static_cast<float>(constants::kNumSyncedFrequencyRatios - 1));
        hz = constants::kSyncedFrequencyRatios[static_cast<int>(index + 0.3f)] * input(kBeatsPerSecond)->at(0)[0];
        if (unit == 3) hz *= 2.0f / 3.0f;
        if (unit == 4) hz *= 3.0f / 2.0f;
        break;
      }
      default: {
        float note = input(kMidi)->at(0)[0] + input(kKeytrackTranspose)->at(0)[0];
        hz = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        break;
      }
    }
    return std::min(std::max(hz, 0.0f), 0.45f * getSampleRate());
  }

  void FreqShifter::updateQuality(int quality) {
    for (int c = 0; c < 2; ++c) {
      main_[c].setQuality(quality);
      feedback_shifter_[c].setQuality(quality);
    }
  }

  void FreqShifter::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void FreqShifter::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    const float sample_rate = static_cast<float>(getSampleRate());
    const float nyquist_guard = 0.45f * sample_rate;
    const float two_pi = 6.28318530717959f;

    const float target_range = rangeHz();
    const float target_shift = std::min(std::max(input(kShift)->at(0)[0], -1.0f), 1.0f);
    const float target_stereo = std::min(std::max(input(kStereo)->at(0)[0], -1.0f), 1.0f);
    const float target_fb_dir = std::min(std::max(input(kFbDirection)->at(0)[0], -1.0f), 1.0f);
    const float target_feedback = std::min(std::max(input(kFeedback)->at(0)[0], -1.0f), 1.0f) * 0.95f;
    const float target_wet = std::min(std::max(input(kDryWet)->at(0)[0], 0.0f), 1.0f);
    updateQuality(static_cast<int>(input(kQuality)->at(0)[0] + 0.5f));
    const bool use_allpass = input(kAllpass)->at(0)[0] >= 0.5f;
    const float smooth = 1.0f - std::exp(-1.0f / (0.02f * sample_rate));

    for (int i = 0; i < num_samples; ++i) {
      range_hz_ += (target_range - range_hz_) * smooth;
      shift_ += (target_shift - shift_) * smooth;
      stereo_ += (target_stereo - stereo_) * smooth;
      fb_dir_ += (target_fb_dir - fb_dir_) * smooth;
      feedback_ += (target_feedback - feedback_) * smooth;
      wet_ += (target_wet - wet_) * smooth;

      const float base = shift_ * range_hz_, offset = stereo_ * range_hz_ * 0.25f;
      const float hz[2] = { std::min(std::max(base + offset, -nyquist_guard), nyquist_guard),
                            std::min(std::max(base - offset, -nyquist_guard), nyquist_guard) };
      const float fb_hz = std::min(std::max(fb_dir_ * range_hz_, -nyquist_guard), nyquist_guard);

      float result[2];
      for (int c = 0; c < 2; ++c) {
        const float dry = audio_in[i][c];
        float re, im;
        main_[c].tick(dry + feedback_state_[c], re, im);
        phase_[c] += two_pi * hz[c] / sample_rate;
        if (phase_[c] > two_pi) phase_[c] -= two_pi;
        if (phase_[c] < -two_pi) phase_[c] += two_pi;
        const float shifted = re * static_cast<float>(std::cos(phase_[c])) - im * static_cast<float>(std::sin(phase_[c]));

        float fb = std::tanh(shifted);
        if (use_allpass)
          for (auto& stage : smear_[c])
            fb = stage.tick(fb, 0.7f);
        if (std::fabs(fb_dir_) > 1.0e-3f) {
          float fre, fim;
          feedback_shifter_[c].tick(fb, fre, fim);
          fb_phase_[c] += two_pi * fb_hz / sample_rate;
          if (fb_phase_[c] > two_pi) fb_phase_[c] -= two_pi;
          if (fb_phase_[c] < -two_pi) fb_phase_[c] += two_pi;
          fb = fre * static_cast<float>(std::cos(fb_phase_[c])) - fim * static_cast<float>(std::sin(fb_phase_[c]));
        }
        feedback_state_[c] = fb * feedback_;
        result[c] = dry + (shifted - dry) * wet_;
      }
      audio_out[i] = poly_float(result[0], result[1], 0.0f, 0.0f);
    }
  }
} // namespace vital
