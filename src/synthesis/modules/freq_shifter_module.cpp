// VialFX - Frequency Shifter module. Licensed under GPL-3.0-or-later.
#include "freq_shifter_module.h"

#include "freq_shifter.h"

namespace vital {

  FreqShifterModule::FreqShifterModule(const Output* beats_per_second, const Output* keytrack) :
      SynthModule(0, 1), shifter_(nullptr), beats_per_second_(beats_per_second), keytrack_(keytrack) { }

  FreqShifterModule::~FreqShifterModule() { }

  void FreqShifterModule::init() {
    shifter_ = new FreqShifter();
    shifter_->useOutput(output());
    addIdleProcessor(shifter_);

    shifter_->plug(createMonoModControl("freq_shifter_shift"), FreqShifter::kShift);
    shifter_->plug(createMonoModControl("freq_shifter_stereo"), FreqShifter::kStereo);
    shifter_->plug(createMonoModControl("freq_shifter_feedback"), FreqShifter::kFeedback);
    shifter_->plug(createMonoModControl("freq_shifter_fb_direction"), FreqShifter::kFbDirection);
    shifter_->plug(createMonoModControl("freq_shifter_dry_wet"), FreqShifter::kDryWet);
    shifter_->plug(createMonoModControl("freq_shifter_frequency"), FreqShifter::kFrequency);
    shifter_->plug(createMonoModControl("freq_shifter_period"), FreqShifter::kPeriod);
    shifter_->plug(createMonoModControl("freq_shifter_tempo"), FreqShifter::kTempo);
    shifter_->plug(createMonoModControl("freq_shifter_keytrack_transpose"), FreqShifter::kKeytrackTranspose);
    shifter_->plug(createBaseControl("freq_shifter_allpass"), FreqShifter::kAllpass);
    shifter_->plug(createBaseControl("freq_shifter_unit"), FreqShifter::kUnit);
    shifter_->plug(createBaseControl("freq_shifter_quality"), FreqShifter::kQuality);
    shifter_->plug(beats_per_second_, FreqShifter::kBeatsPerSecond);
    shifter_->plug(keytrack_, FreqShifter::kMidi);

    SynthModule::init();
  }

  void FreqShifterModule::hardReset() {
    shifter_->hardReset();
  }

  void FreqShifterModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      shifter_->hardReset();
  }

  void FreqShifterModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    shifter_->setSampleRate(sample_rate);
  }

  void FreqShifterModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    shifter_->processWithInput(audio_in, num_samples);
  }
} // namespace vital
