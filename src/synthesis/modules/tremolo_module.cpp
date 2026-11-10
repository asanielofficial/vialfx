// VialFX - Tremolo module. Licensed under GPL-3.0-or-later.
#include "tremolo_module.h"

#include "tremolo.h"

namespace vital {

  TremoloModule::TremoloModule(const Output* beats_per_second) :
      SynthModule(0, 1), tremolo_(nullptr), beats_per_second_(beats_per_second) { }

  TremoloModule::~TremoloModule() { }

  void TremoloModule::init() {
    tremolo_ = new Tremolo();
    tremolo_->useOutput(output());
    addIdleProcessor(tremolo_);

    tremolo_->plug(createMonoModControl("tremolo_rate_a"), Tremolo::kRateA);
    tremolo_->plug(createMonoModControl("tremolo_rate_b"), Tremolo::kRateB);
    tremolo_->plug(createMonoModControl("tremolo_amount_a"), Tremolo::kAmountA);
    tremolo_->plug(createMonoModControl("tremolo_amount_b"), Tremolo::kAmountB);
    tremolo_->plug(createMonoModControl("tremolo_tempo_a"), Tremolo::kTempoA);
    tremolo_->plug(createMonoModControl("tremolo_tempo_b"), Tremolo::kTempoB);
    tremolo_->plug(createBaseControl("tremolo_mod_a"), Tremolo::kModA);
    tremolo_->plug(createBaseControl("tremolo_mod_b"), Tremolo::kModB);
    tremolo_->plug(createBaseControl("tremolo_unit_a"), Tremolo::kUnitA);
    tremolo_->plug(createBaseControl("tremolo_unit_b"), Tremolo::kUnitB);
    tremolo_->plug(beats_per_second_, Tremolo::kBeatsPerSecond);

    createStatusOutput("tremolo_gain", tremolo_->output(Tremolo::kGain));

    SynthModule::init();
  }

  void TremoloModule::hardReset() {
    tremolo_->hardReset();
  }

  void TremoloModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      tremolo_->hardReset();
  }

  void TremoloModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    tremolo_->setSampleRate(sample_rate);
  }

  void TremoloModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    tremolo_->processWithInput(audio_in, num_samples);
    // Sub module status outputs are not refreshed by the engine; publish the gain for the meter here.
    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }
} // namespace vital
