// VialFX - Utility module. Licensed under GPL-3.0-or-later.
#include "utility_module.h"

#include "peak_meter.h"
#include "utility.h"

namespace vital {

  UtilityModule::UtilityModule() : SynthModule(0, 1), utility_(nullptr), meter_(nullptr) { }

  UtilityModule::~UtilityModule() { }

  void UtilityModule::init() {
    utility_ = new Utility();
    utility_->useOutput(output());
    addIdleProcessor(utility_);

    Output* gain = createMonoModControl("utility_gain");
    Output* pan = createMonoModControl("utility_pan");
    Output* spread = createMonoModControl("utility_spread");
    Output* haas = createMonoModControl("utility_haas");
    Output* frequency = createMonoModControl("utility_mono_frequency");
    Value* invert_left = createBaseControl("utility_invert_left");
    Value* invert_right = createBaseControl("utility_invert_right");
    Value* swap = createBaseControl("utility_swap");
    Value* dc_filter = createBaseControl("utility_dc_filter");
    Value* mono_bass = createBaseControl("utility_mono_bass");

    utility_->plug(gain, Utility::kGain);
    utility_->plug(pan, Utility::kPan);
    utility_->plug(spread, Utility::kSpread);
    utility_->plug(haas, Utility::kHaas);
    utility_->plug(invert_left, Utility::kInvertLeft);
    utility_->plug(invert_right, Utility::kInvertRight);
    utility_->plug(swap, Utility::kSwap);
    utility_->plug(dc_filter, Utility::kDcFilter);
    utility_->plug(mono_bass, Utility::kMonoBass);
    utility_->plug(frequency, Utility::kMonoFrequency);

    // Level meter on the right of the module.
    meter_ = new PeakMeter();
    meter_->plug(utility_, 0);
    addIdleProcessor(meter_);
    createStatusOutput("utility_level", meter_->output(PeakMeter::kLevel));
    createStatusOutput("utility_peak", meter_->output(PeakMeter::kMemoryPeak));

    SynthModule::init();
  }

  void UtilityModule::hardReset() {
    utility_->hardReset();
  }

  void UtilityModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (!enable)
      utility_->hardReset();
  }

  void UtilityModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    utility_->setSampleRate(sample_rate);
    meter_->setSampleRate(sample_rate);
  }

  void UtilityModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    utility_->processWithInput(audio_in, num_samples);
    meter_->process(num_samples);
    // Status outputs of sub modules are not refreshed by the engine, so publish the meter values for the UI here.
    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }
} // namespace vital
