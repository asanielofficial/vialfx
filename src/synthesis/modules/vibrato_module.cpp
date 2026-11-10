// VialFX - Vibrato module. Licensed under GPL-3.0-or-later.
#include "vibrato_module.h"

#include "vibrato.h"

namespace vital {

  VibratoModule::VibratoModule(const Output* beats_per_second) :
      SynthModule(0, 1), vibrato_(nullptr), beats_per_second_(beats_per_second) { }

  VibratoModule::~VibratoModule() { }

  void VibratoModule::init() {
    vibrato_ = new Vibrato();
    vibrato_->useOutput(output());
    addIdleProcessor(vibrato_);

    vibrato_->plug(createMonoModControl("vibrato_rate_a"), Vibrato::kRateA);
    vibrato_->plug(createMonoModControl("vibrato_rate_b"), Vibrato::kRateB);
    vibrato_->plug(createMonoModControl("vibrato_amount_a"), Vibrato::kAmountA);
    vibrato_->plug(createMonoModControl("vibrato_amount_b"), Vibrato::kAmountB);
    vibrato_->plug(createMonoModControl("vibrato_tempo_a"), Vibrato::kTempoA);
    vibrato_->plug(createMonoModControl("vibrato_tempo_b"), Vibrato::kTempoB);
    vibrato_->plug(createBaseControl("vibrato_mod_a"), Vibrato::kModA);
    vibrato_->plug(createBaseControl("vibrato_mod_b"), Vibrato::kModB);
    vibrato_->plug(createBaseControl("vibrato_unit_a"), Vibrato::kUnitA);
    vibrato_->plug(createBaseControl("vibrato_unit_b"), Vibrato::kUnitB);
    vibrato_->plug(beats_per_second_, Vibrato::kBeatsPerSecond);

    createStatusOutput("vibrato_cents", vibrato_->output(Vibrato::kCents));

    SynthModule::init();
  }

  void VibratoModule::hardReset() {
    vibrato_->hardReset();
  }

  void VibratoModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      vibrato_->hardReset();
  }

  void VibratoModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    vibrato_->setSampleRate(sample_rate);
  }

  void VibratoModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    vibrato_->processWithInput(audio_in, num_samples);
    // Sub module status outputs are not refreshed by the engine; publish the deviation for the meter here.
    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }
} // namespace vital
