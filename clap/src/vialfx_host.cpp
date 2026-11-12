#include "vialfx_host.h"

#include "load_save.h"
#include "sound_engine.h"
#include "synth_parameters.h"

#include <algorithm>
#include <cmath>

namespace vialfx {

  namespace {
    float skew(const vital::ValueDetails& details, float value) {
      switch (details.value_scale) {
        case vital::ValueDetails::kQuadratic: return value * value;
        case vital::ValueDetails::kCubic: return value * value * value;
        case vital::ValueDetails::kQuartic: value *= value; return value * value;
        case vital::ValueDetails::kExponential:
          return details.display_invert ? 1.0f / std::pow(2.0f, value) : std::pow(2.0f, value);
        case vital::ValueDetails::kSquareRoot: return std::sqrt(std::max(0.0f, value));
        default: return value;
      }
    }

    float unskew(const vital::ValueDetails& details, float value) {
      switch (details.value_scale) {
        case vital::ValueDetails::kQuadratic: return std::sqrt(std::max(0.0f, value));
        case vital::ValueDetails::kCubic: return std::cbrt(value);
        case vital::ValueDetails::kQuartic: return std::pow(std::max(0.0f, value), 0.25f);
        case vital::ValueDetails::kExponential:
          return details.display_invert ? std::log2(1.0f / std::max(1e-9f, value)) : std::log2(std::max(1e-9f, value));
        default: return value;
      }
    }

    std::string moduleOf(const std::string& name) {
      size_t underscore = name.find('_');
      if (underscore == std::string::npos || underscore == 0)
        return "global";
      return name.substr(0, underscore);
    }
  }

  uint32_t VialfxHost::idForName(const std::string& name) {
    uint32_t hash = 2166136261u;  // FNV-1a
    for (unsigned char c : name) {
      hash ^= c;
      hash *= 16777619u;
    }
    return hash;
  }

  VialfxHost::VialfxHost() {
    int num_params = vital::Parameters::getNumParameters();
    for (int i = 0; i < num_params; ++i) {
      const vital::ValueDetails* details = vital::Parameters::getDetails(i);
      auto control = controls_.find(details->name);
      if (control == controls_.end() || !control->second)
        continue;

      ParamInfo info;
      info.name = details->name;
      info.id = idForName(info.name);
      if (id_lookup_.count(info.id))
        continue;  // never happens for the current names; a clash would only drop the later one
      info.display_name = details->display_name.empty() ? details->name : details->display_name;
      info.module = moduleOf(info.name);
      info.min = details->min;
      info.max = details->max;
      info.default_value = details->default_value;
      info.stepped = details->value_scale == vital::ValueDetails::kIndexed;
      info.value = control->second;
      id_lookup_[info.id] = params_.size();
      name_lookup_[info.name] = params_.size();
      params_.push_back(info);
    }

    auto bypass = name_lookup_.find("bypass");
    if (bypass != name_lookup_.end())
      bypass_ = params_[bypass->second].value;
  }

  VialfxHost::~VialfxHost() {
    midi_manager_ = nullptr;
    keyboard_state_ = nullptr;
  }

  const ParamInfo* VialfxHost::findParam(uint32_t id) const {
    auto found = id_lookup_.find(id);
    return found == id_lookup_.end() ? nullptr : &params_[found->second];
  }

  const ParamInfo* VialfxHost::findParam(const std::string& name) const {
    auto found = name_lookup_.find(name);
    return found == name_lookup_.end() ? nullptr : &params_[found->second];
  }

  float VialfxHost::getParamValue(const ParamInfo& info) const {
    return info.value->value();
  }

  std::string VialfxHost::valueToText(const ParamInfo& info, float value) const {
    const vital::ValueDetails& details = vital::Parameters::getDetails(info.name);
    if (info.stepped)
      value = std::round(value);

    if (details.string_lookup && details.string_lookup_size > 0) {
      int last_index = std::min<int>(static_cast<int>(details.max), details.string_lookup_size - 1);
      int index = std::isfinite(value) ? static_cast<int>(value) : 0;
      return details.string_lookup[std::max(0, std::min(index, last_index))];
    }
    if (details.string_lookup)
      return "";

    float display = details.display_multiply * skew(details, value) + details.post_offset;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3g", display);
    return std::string(buffer) + details.display_units;
  }

  bool VialfxHost::textToValue(const ParamInfo& info, const std::string& text, float* value) const {
    const vital::ValueDetails& details = vital::Parameters::getDetails(info.name);
    if (details.string_lookup && details.string_lookup_size > 0) {
      for (int i = 0; i < details.string_lookup_size; ++i) {
        if (text == details.string_lookup[i]) {
          *value = static_cast<float>(i);
          return true;
        }
      }
    }

    char* end = nullptr;
    float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str())
      return false;

    float multiply = details.display_multiply == 0.0f ? 1.0f : details.display_multiply;
    float result = unskew(details, (parsed - details.post_offset) / multiply);
    *value = std::max(info.min, std::min(info.max, result));
    return true;
  }

  void VialfxHost::setParamFromHost(const ParamInfo& info, float value) {
    value = std::max(info.min, std::min(info.max, info.stepped ? std::round(value) : value));
    if (info.name == "mod_wheel" || info.name == "pitch_wheel") {
      valueChanged(info.name, value);
      if (info.name == "mod_wheel")
        engine_->setModWheelAllChannels(value);
      else
        engine_->setZonedPitchWheel(value, 0, vital::kNumMidiChannels - 1);
      return;
    }
    valueChanged(info.name, value);
  }

  void VialfxHost::setParamFromGui(const ParamInfo& info, float value) {
    value = std::max(info.min, std::min(info.max, info.stepped ? std::round(value) : value));
    valueChangedInternal(info.name, value);  // sets the control and calls setValueNotifyHost
  }

  void VialfxHost::beginGuiGesture(const ParamInfo& info) { beginChangeGesture(info.name); }
  void VialfxHost::endGuiGesture(const ParamInfo& info) { endChangeGesture(info.name); }

  void VialfxHost::setValueNotifyHost(const std::string& name, vital::mono_float value) {
    if (loading_state_.load())
      return;
    const ParamInfo* info = findParam(name);
    if (info == nullptr)
      return;
    output_changes_.enqueue({ info->id, value });
    pending_output_.fetch_add(1);
  }

  void VialfxHost::beginChangeGesture(const std::string& name) {
    const ParamInfo* info = findParam(name);
    if (info) {
      output_gestures_.enqueue({ info->id, true });
      pending_output_.fetch_add(1);
    }
  }

  void VialfxHost::endChangeGesture(const std::string& name) {
    const ParamInfo* info = findParam(name);
    if (info) {
      output_gestures_.enqueue({ info->id, false });
      pending_output_.fetch_add(1);
    }
  }

  bool VialfxHost::popParamChange(ParamChange& change) {
    if (!output_changes_.try_dequeue(change))
      return false;
    pending_output_.fetch_sub(1);
    return true;
  }

  bool VialfxHost::popGesture(Gesture& gesture) {
    if (!output_gestures_.try_dequeue(gesture))
      return false;
    pending_output_.fetch_sub(1);
    return true;
  }

  void VialfxHost::pauseProcessing(bool pause) {
    if (pause)
      critical_section_.enter();
    else
      critical_section_.exit();
  }

  void VialfxHost::prepare(double sample_rate, int max_frames) {
    sample_rate_ = sample_rate;
    engine_->setSampleRate(sample_rate);
    engine_->updateAllModulationSwitches();
    midi_manager_->setSampleRate(sample_rate);
  }

  void VialfxHost::applyBypassed(const AudioBlock& block) {
    for (int channel = 0; channel < 2; ++channel) {
      if (block.output[channel] == nullptr)
        continue;
      const float* source = block.input[channel] ? block.input[channel] : block.input[0];
      if (source == nullptr)
        std::fill(block.output[channel], block.output[channel] + block.num_samples, 0.0f);
      else if (source != block.output[channel])
        std::copy(source, source + block.num_samples, block.output[channel]);
    }
  }

  void VialfxHost::process(const AudioBlock& block, juce::MidiBuffer& midi, const Transport& transport) {
    static constexpr double kSecondsPerMinute = 60.0;
    const ScopedLock lock(critical_section_);

    if (bypass_ && bypass_->value() != 0.0f) {
      applyBypassed(block);
      return;
    }

    if (transport.has_tempo && transport.tempo > 0.0)
      engine_->setBpm(transport.tempo);
    if (transport.is_playing && transport.has_beats && transport.tempo > 0.0)
      last_seconds_time_ = transport.song_position_beats / (transport.tempo / kSecondsPerMinute);

    processModulationChanges();

    // The engine writes into a JUCE buffer; hand it the host's output channels.
    float* channels[2] = { block.output[0], block.output[1] ? block.output[1] : block.output[0] };
    AudioSampleBuffer buffer(channels, 2, block.num_samples);

    bool has_sidechain = block.sidechain[0] != nullptr;
    double sample_time = 1.0 / sample_rate_;
    for (int offset = 0; offset < block.num_samples;) {
      int num_samples = std::min<int>(block.num_samples - offset, vital::kMaxBufferSize);

      engine_->correctToTime(last_seconds_time_);
      processMidi(midi, offset, offset + num_samples);

      // Input has to be taken before the engine writes the (shared) output.
      const float* left = block.input[0] ? block.input[0] + offset : nullptr;
      const float* right = block.input[1] ? block.input[1] + offset : left;
      for (int i = 0; i < num_samples; ++i)
        input_[i] = left ? vital::poly_float(left[i], right[i]) : vital::poly_float(0.0f);

      if (has_sidechain) {
        const float* side_left = block.sidechain[0] + offset;
        const float* side_right = block.sidechain[1] ? block.sidechain[1] + offset : side_left;
        for (int i = 0; i < num_samples; ++i)
          sidechain_[i] = vital::poly_float(side_left[i], side_right[i]);
      }

      processAudioWithInput(&buffer, input_, 2, num_samples, offset, has_sidechain ? sidechain_ : nullptr);

      last_seconds_time_ += num_samples * sample_time;
      offset += num_samples;
    }
  }

  std::string VialfxHost::saveState() {
    json data = LoadSave::stateToJson(this, getCriticalSection());
    data["tuning"] = getTuning()->stateToJson();
    return data.dump();
  }

  bool VialfxHost::loadState(const void* data, size_t size) {
    bool ok = true;
    loading_state_ = true;
    pauseProcessing(true);
    try {
      json json_data = json::parse(std::string(static_cast<const char*>(data), size));
      LoadSave::jsonToState(this, save_info_, json_data);
      if (json_data.count("tuning"))
        getTuning()->jsonToState(json_data["tuning"]);
    }
    catch (const json::exception&) {
      ok = false;
    }
    pauseProcessing(false);
    loading_state_ = false;
    return ok;
  }
}
