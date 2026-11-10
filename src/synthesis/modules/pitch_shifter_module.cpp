// VialFX - Pitch Shifter module. Licensed under GPL-3.0-or-later.
#include "pitch_shifter_module.h"

#include "pitch_shifter.h"

namespace vital {

  PitchShifterModule::PitchShifterModule() : SynthModule(0, 1), shifter_(nullptr) { }

  PitchShifterModule::~PitchShifterModule() { }

  void PitchShifterModule::init() {
    shifter_ = new PitchShifter();
    shifter_->useOutput(output());
    addIdleProcessor(shifter_);

    Value* mode = createBaseControl("pitch_shifter_mode");
    Value* compensation = createBaseControl("pitch_shifter_compensation");
    Value* formant_control = createBaseControl("pitch_shifter_formant_control");
    Output* pitch = createMonoModControl("pitch_shifter_pitch");
    Output* grain = createMonoModControl("pitch_shifter_grain");
    Output* resolution = createMonoModControl("pitch_shifter_resolution");
    Output* formant = createMonoModControl("pitch_shifter_formant");
    Output* dry_wet = createMonoModControl("pitch_shifter_dry_wet");

    shifter_->plug(mode, PitchShifter::kMode);
    shifter_->plug(compensation, PitchShifter::kCompensation);
    shifter_->plug(pitch, PitchShifter::kPitch);
    shifter_->plug(grain, PitchShifter::kGrain);
    shifter_->plug(resolution, PitchShifter::kResolution);
    shifter_->plug(formant_control, PitchShifter::kFormantControl);
    shifter_->plug(formant, PitchShifter::kFormant);
    shifter_->plug(dry_wet, PitchShifter::kDryWet);

    SynthModule::init();
  }

  void PitchShifterModule::hardReset() {
    shifter_->hardReset();
  }

  void PitchShifterModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      shifter_->hardReset();
  }

  void PitchShifterModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    shifter_->setSampleRate(sample_rate);
  }

  void PitchShifterModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    shifter_->processWithInput(audio_in, num_samples);
  }
} // namespace vital
