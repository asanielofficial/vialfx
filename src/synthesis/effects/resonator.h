// VialFX - Resonator effect. Licensed under GPL-3.0-or-later.
#pragma once

#include "processor.h"
#include "resonator_midi.h"

#include <vector>

class Tuning;

namespace vital {

  // Tuned resonator: the input rings at a pitch taken from the Tune/Fine knobs or from MIDI. Four styles:
  //   Comb     feedback comb with a damped loop (partials at whole multiples of the pitch)
  //   Modal    bank of ten resonators at bar-like, stretchable partial ratios
  //   String   the comb loop plus a pick-position notch on the input, for plucked strings
  //   Formant  five vowel-like resonators that follow the pitch
  // Up to 16 voices; every voice follows one note (and its pitch bend, slide and pressure for MPE keyboards).
  class Resonator : public Processor {
    public:
      static constexpr int kMaxVoices = 16;
      static constexpr int kMaxPartials = 10;

      enum {
        kAudio,
        kStyle,            // 0 Comb, 1 Modal, 2 String, 3 Formant
        kKeytrack,         // 0 Off (Tune around C3), 1 MIDI
        kVoices,           // 0..3 -> 1, 4, 8, 16
        kTune,             // semitones
        kFine,             // cents
        kResonance,        // 0..1
        kDamping,          // note (8..136), cutoff of the loop damping
        kStretch,          // -1..1
        kColor,            // -1..1 tilt
        kMix,              // 0..1
        kSlide,            // 0..1 how much slide (CC74) acts
        kPressure,         // 0..1 how much pressure acts
        kSlideTarget,      // 0 Damping, 1 Color, 2 Resonance
        kPressureTarget,   // 0 Resonance, 1 Mix, 2 Color
        kRefPitch,         // Hz of A4
        kBendRange,        // semitones of pitch bend for MPE note channels
        kGlide,            // 0..1
        kTuning,           // 0 12-TET, 1 scale file
        kMpe,              // 0 Off, 1 On
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      Resonator();
      ~Resonator() override { }

      Processor* clone() const override {
        VITAL_ASSERT(false);
        return nullptr;
      }

      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void setSampleRate(int sample_rate) override;
      void hardReset() override;

      // The delay lines are big (16 voices), so a copy only gets them once it is used: the chain calls this when a
      // copy joins it (message thread). A copy that was never prepared allocates on its first block instead.
      void prepare() { if (allocated_rate_ != getSampleRate()) allocate(); }

      void setMidi(ResonatorMidi* midi) { midi_ = midi; }
      void setTuning(const Tuning* tuning) { tuning_ = tuning; }

      // Frequency of the first sounding voice from the last block (for tests and readouts).
      float lastFrequency() const { return last_frequency_; }
      int lastActiveVoices() const { return last_active_voices_; }

    private:
      struct Voice {
        int id = -1;
        float t_semis = 48.0f, t_slide = 0.0f, t_pressure = 0.0f;
        bool active = false;
        bool fresh = true;
        float gain = 0.0f;
        float semis = 48.0f;
        float delay = 100.0f;
        int write = 0;
        float lp[2] = { 0.0f, 0.0f };
        float ap_x[2][3] = {}, ap_y[2][3] = {};
        float y1[2][kMaxPartials] = {}, y2[2][kMaxPartials] = {};
        std::vector<float> line[2], pick[2];
        void clear();
      };

      struct Target {
        int id;
        float semis, slide, pressure;
      };

      void allocate();
      int gatherTargets(Target* targets, int max_count, int keytrack, bool mpe, float bend_range, bool scale);
      void assignVoices(const Target* targets, int count);

      static float hermite(const std::vector<float>& buf, int mask, float read);

      ResonatorMidi* midi_;
      const Tuning* tuning_;
      Voice voices_[kMaxVoices];
      int mask_, pick_mask_;
      int allocated_rate_ = 0;
      std::vector<float> wet_[2];
      float tilt_lp_[2];
      float mix_, color_;
      float last_frequency_;
      int last_active_voices_;
  };
} // namespace vital
