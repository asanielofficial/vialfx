// VialFX - Resonator module. Licensed under GPL-3.0-or-later.
#include "resonator_module.h"

#include "resonator.h"

namespace vital {

  ResonatorModule::ResonatorModule(ResonatorMidi* midi) : SynthModule(0, 1), resonator_(nullptr), midi_(midi) { }

  ResonatorModule::~ResonatorModule() { }

  void ResonatorModule::init() {
    resonator_ = new Resonator();
    resonator_->setMidi(midi_);
    resonator_->useOutput(output());
    addIdleProcessor(resonator_);

    resonator_->plug(createBaseControl("resonator_style"), Resonator::kStyle);
    resonator_->plug(createBaseControl("resonator_keytrack"), Resonator::kKeytrack);
    resonator_->plug(createBaseControl("resonator_voices"), Resonator::kVoices);
    resonator_->plug(createMonoModControl("resonator_tune"), Resonator::kTune);
    resonator_->plug(createMonoModControl("resonator_fine"), Resonator::kFine);
    resonator_->plug(createMonoModControl("resonator_resonance"), Resonator::kResonance);
    resonator_->plug(createMonoModControl("resonator_damping"), Resonator::kDamping);
    resonator_->plug(createMonoModControl("resonator_stretch"), Resonator::kStretch);
    resonator_->plug(createMonoModControl("resonator_color"), Resonator::kColor);
    resonator_->plug(createMonoModControl("resonator_mix"), Resonator::kMix);
    resonator_->plug(createMonoModControl("resonator_slide"), Resonator::kSlide);
    resonator_->plug(createMonoModControl("resonator_pressure"), Resonator::kPressure);
    resonator_->plug(createBaseControl("resonator_slide_target"), Resonator::kSlideTarget);
    resonator_->plug(createBaseControl("resonator_pressure_target"), Resonator::kPressureTarget);
    resonator_->plug(createBaseControl("resonator_ref_pitch"), Resonator::kRefPitch);
    resonator_->plug(createBaseControl("resonator_bend_range"), Resonator::kBendRange);
    resonator_->plug(createBaseControl("resonator_glide"), Resonator::kGlide);
    resonator_->plug(createBaseControl("resonator_tuning"), Resonator::kTuning);
    resonator_->plug(createBaseControl("resonator_mpe"), Resonator::kMpe);

    SynthModule::init();
  }

  void ResonatorModule::hardReset() {
    resonator_->hardReset();
  }

  void ResonatorModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      resonator_->hardReset();
  }

  void ResonatorModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    resonator_->setSampleRate(sample_rate);
  }

  void ResonatorModule::setTuning(const Tuning* tuning) {
    if (resonator_)
      resonator_->setTuning(tuning);
  }

  void ResonatorModule::prepare() {
    if (resonator_)
      resonator_->prepare();
  }

  void ResonatorModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    resonator_->processWithInput(audio_in, num_samples);
  }
} // namespace vital
