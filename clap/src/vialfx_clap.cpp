#include "vialfx_clap.h"

#include "load_save.h"

#include <clap/ext/gui.h>
#include <cstring>

#ifndef VIALFX_VERSION
#define VIALFX_VERSION "1.4-visage-test"
#endif

namespace vialfx {

  static const char* kFeatures[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

  const clap_plugin_descriptor* VialfxClapPlugin::descriptor() {
    static const clap_plugin_descriptor desc = {
      CLAP_VERSION, "com.asanielofficial.vialfx", "VialFX", "asanielofficial",
      "https://github.com/asanielofficial/vialfx", "", "", VIALFX_VERSION,
      "Modular effects processor", kFeatures
    };
    return &desc;
  }

  VialfxClapPlugin::VialfxClapPlugin(const clap_host* host) : ClapPluginBase(descriptor(), host) { }
  VialfxClapPlugin::~VialfxClapPlugin() { }

  bool VialfxClapPlugin::init() noexcept {
    juce_init_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    synth_ = std::make_unique<VialfxHost>();
    for (const ParamInfo& p : synth_->params()) {
      GuiParam g;
      g.id = p.id; g.name = p.name; g.display_name = p.display_name; g.module = p.module;
      g.min = p.min; g.max = p.max; g.default_value = p.default_value; g.stepped = p.stepped;
      gui_params_.push_back(g);
    }
    return true;
  }

  bool VialfxClapPlugin::activate(double sample_rate, uint32_t, uint32_t max_frames) noexcept {
    sample_rate_ = sample_rate;
    synth_->prepare(sample_rate, (int)max_frames);
    reported_latency_ = synth_->latencySamples();
    active_ = true;
    return true;
  }

  void VialfxClapPlugin::deactivate() noexcept { active_ = false; }
  void VialfxClapPlugin::reset() noexcept { }

  uint32_t VialfxClapPlugin::latencyGet() const noexcept { return (uint32_t)std::max(0, reported_latency_); }
  uint32_t VialfxClapPlugin::tailGet() const noexcept { return 0; }

  void VialfxClapPlugin::onMainThread() noexcept {
    if (latency_dirty_.exchange(false) && _host.canUseLatency())
      _host.latencyChanged();
  }

  bool VialfxClapPlugin::audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept {
    std::memset(info, 0, sizeof(*info));
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    if (is_input && index == 0) {
      info->id = 0; std::strncpy(info->name, "Input", sizeof(info->name) - 1);
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
      return true;
    }
    if (is_input && index == 1) {
      info->id = 1; std::strncpy(info->name, "Sidechain", sizeof(info->name) - 1);
      return true;
    }
    if (!is_input && index == 0) {
      info->id = 2; std::strncpy(info->name, "Output", sizeof(info->name) - 1);
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
      return true;
    }
    return false;
  }

  bool VialfxClapPlugin::notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept {
    if (!is_input || index != 0)
      return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Notes", sizeof(info->name) - 1);
    return true;
  }

  uint32_t VialfxClapPlugin::paramsCount() const noexcept { return (uint32_t)gui_params_.size(); }

  bool VialfxClapPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept {
    if (index >= gui_params_.size())
      return false;
    const GuiParam& p = gui_params_[index];
    std::memset(info, 0, sizeof(*info));
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.default_value;
    std::strncpy(info->name, p.display_name.c_str(), sizeof(info->name) - 1);
    std::strncpy(info->module, p.module.c_str(), sizeof(info->module) - 1);
    return true;
  }

  bool VialfxClapPlugin::paramsValue(clap_id id, double* value) noexcept {
    const ParamInfo* p = synth_->findParam((uint32_t)id);
    if (!p) return false;
    *value = synth_->getParamValue(*p);
    return true;
  }

  bool VialfxClapPlugin::paramsValueToText(clap_id id, double value, char* text, uint32_t size) noexcept {
    const ParamInfo* p = synth_->findParam((uint32_t)id);
    if (!p) return false;
    std::string s = synth_->valueToText(*p, (float)value);
    std::strncpy(text, s.c_str(), size - 1);
    text[size - 1] = 0;
    return true;
  }

  bool VialfxClapPlugin::paramsTextToValue(clap_id id, const char* text, double* value) noexcept {
    const ParamInfo* p = synth_->findParam((uint32_t)id);
    if (!p) return false;
    float v = 0.0f;
    if (!synth_->textToValue(*p, text, &v)) return false;
    *value = v;
    return true;
  }

  void VialfxClapPlugin::handleInputEvents(const clap_input_events* in) {
    uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
      const clap_event_header_t* h = in->get(in, i);
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
        continue;
      int time = (int)h->time;
      switch (h->type) {
        case CLAP_EVENT_NOTE_ON: {
          auto* e = (const clap_event_note_t*)h;
          midi_.addEvent(juce::MidiMessage::noteOn(e->channel + 1, e->key, (float)e->velocity), time);
          break;
        }
        case CLAP_EVENT_NOTE_OFF: {
          auto* e = (const clap_event_note_t*)h;
          midi_.addEvent(juce::MidiMessage::noteOff(e->channel + 1, e->key, (float)e->velocity), time);
          break;
        }
        case CLAP_EVENT_MIDI: {
          auto* e = (const clap_event_midi_t*)h;
          midi_.addEvent(juce::MidiMessage(e->data[0], e->data[1], e->data[2]), time);
          break;
        }
        case CLAP_EVENT_PARAM_VALUE: {
          auto* e = (const clap_event_param_value_t*)h;
          if (const ParamInfo* p = synth_->findParam((uint32_t)e->param_id))
            synth_->setParamFromHost(*p, (float)e->value);
          break;
        }
        case CLAP_EVENT_TRANSPORT: {
          auto* e = (const clap_event_transport_t*)h;
          transport_.has_tempo = (e->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
          transport_.tempo = e->tempo;
          transport_.is_playing = (e->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
          transport_.has_beats = (e->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
          transport_.song_position_beats = (double)e->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
          break;
        }
        default: break;
      }
    }
  }

  void VialfxClapPlugin::writeOutputEvents(const clap_output_events* out) {
    VialfxHost::Gesture g;
    while (synth_->popGesture(g)) {
      clap_event_param_gesture_t e = {};
      e.header.size = sizeof(e);
      e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      e.header.type = g.begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
      e.param_id = g.id;
      out->try_push(out, &e.header);
    }
    ParamChange c;
    while (synth_->popParamChange(c)) {
      clap_event_param_value_t e = {};
      e.header.size = sizeof(e);
      e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      e.header.type = CLAP_EVENT_PARAM_VALUE;
      e.param_id = c.id;
      e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
      e.value = c.value;
      out->try_push(out, &e.header);
    }
  }

  void VialfxClapPlugin::paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept {
    handleInputEvents(in);
    writeOutputEvents(out);
    midi_.clear();
  }

  clap_process_status VialfxClapPlugin::process(const clap_process* p) noexcept {
    midi_.clear();
    if (p->transport) {
      transport_.has_tempo = (p->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
      transport_.tempo = p->transport->tempo;
      transport_.is_playing = (p->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
      transport_.has_beats = (p->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
      transport_.song_position_beats = (double)p->transport->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
    }
    handleInputEvents(p->in_events);

    AudioBlock block;
    block.num_samples = (int)p->frames_count;
    if (p->audio_inputs_count > 0 && p->audio_inputs[0].channel_count > 0) {
      const clap_audio_buffer_t& b = p->audio_inputs[0];
      block.input[0] = b.data32[0];
      block.input[1] = b.channel_count > 1 ? b.data32[1] : b.data32[0];
    }
    if (p->audio_inputs_count > 1 && p->audio_inputs[1].channel_count > 0) {
      const clap_audio_buffer_t& b = p->audio_inputs[1];
      block.sidechain[0] = b.data32[0];
      block.sidechain[1] = b.channel_count > 1 ? b.data32[1] : b.data32[0];
    }
    if (p->audio_outputs_count > 0 && p->audio_outputs[0].channel_count > 0) {
      const clap_audio_buffer_t& b = p->audio_outputs[0];
      block.output[0] = b.data32[0];
      block.output[1] = b.channel_count > 1 ? b.data32[1] : b.data32[0];
    }

    synth_->process(block, midi_, transport_);

    writeOutputEvents(p->out_events);

    int latency = synth_->latencySamples();
    if (latency != reported_latency_) {
      reported_latency_ = latency;
      latency_dirty_ = true;
      _host.requestCallback();
    }
    return CLAP_PROCESS_CONTINUE;
  }

  bool VialfxClapPlugin::stateSave(const clap_ostream* stream) noexcept {
    std::string s = synth_->saveState();
    const char* data = s.data();
    size_t left = s.size();
    while (left > 0) {
      int64_t n = stream->write(stream, data, left);
      if (n <= 0) return false;
      data += n; left -= (size_t)n;
    }
    return true;
  }

  bool VialfxClapPlugin::stateLoad(const clap_istream* stream) noexcept {
    std::string s;
    char buf[4096];
    for (;;) {
      int64_t n = stream->read(stream, buf, sizeof(buf));
      if (n < 0) return false;
      if (n == 0) break;
      s.append(buf, (size_t)n);
    }
    bool ok = synth_->loadState(s.data(), s.size());
    if (ok && _host.canUseParams())
      _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
    return ok;
  }

  // ---- GUI ----
  static const char* nativeApi() {
#if defined(_WIN32)
    return CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#else
    return CLAP_WINDOW_API_X11;
#endif
  }

  bool VialfxClapPlugin::guiIsApiSupported(const char* api, bool is_floating) noexcept {
    return !is_floating && std::strcmp(api, nativeApi()) == 0;
  }
  bool VialfxClapPlugin::guiGetPreferredApi(const char** api, bool* is_floating) noexcept {
    *api = nativeApi(); *is_floating = false; return true;
  }
  bool VialfxClapPlugin::guiCreate(const char* api, bool is_floating) noexcept {
    if (!guiIsApiSupported(api, is_floating)) return false;
    gui_ = createGuiWindow(this);
    if (!gui_) return false;
    gui_->on_resized = [this]() {
      if (_host.canUseGui())
        _host.guiRequestResize(gui_->width(), gui_->height());
    };
    return true;
  }
  void VialfxClapPlugin::guiDestroy() noexcept {
    if (gui_) { gui_->close(); gui_.reset(); }
  }
  bool VialfxClapPlugin::guiSetParent(const clap_window* window) noexcept {
    return gui_ && gui_->attach(window->ptr);
  }
  bool VialfxClapPlugin::guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = gui_ ? gui_->fixedAspectRatio() : true;
    float r = gui_ ? gui_->aspectRatio() : 1.0f;
    hints->aspect_ratio_width = (uint32_t)(r * 1000.0f);
    hints->aspect_ratio_height = 1000;
    return true;
  }
  bool VialfxClapPlugin::guiAdjustSize(uint32_t* w, uint32_t* h) noexcept {
    if (!gui_) return false;
    gui_->adjustSize(w, h);
    return true;
  }
  bool VialfxClapPlugin::guiSetSize(uint32_t w, uint32_t h) noexcept {
    if (!gui_) return false;
    gui_->setSizeFromHost(w, h);
    return true;
  }
  bool VialfxClapPlugin::guiGetSize(uint32_t* w, uint32_t* h) noexcept {
    if (!gui_) return false;
    *w = gui_->width(); *h = gui_->height();
    return true;
  }
#ifdef __linux__
  void VialfxClapPlugin::onPosixFd(int, clap_posix_fd_flags_t) noexcept {
    if (gui_) gui_->processPosixFd();
  }
#endif

  // ---- GuiHost ----
  float VialfxClapPlugin::value(uint32_t id) const {
    const ParamInfo* p = synth_->findParam(id);
    return p ? synth_->getParamValue(*p) : 0.0f;
  }
  std::string VialfxClapPlugin::valueText(uint32_t id, float v) const {
    const ParamInfo* p = synth_->findParam(id);
    return p ? synth_->valueToText(*p, v) : std::string();
  }
  void VialfxClapPlugin::beginGesture(uint32_t id) {
    if (const ParamInfo* p = synth_->findParam(id)) synth_->beginGuiGesture(*p);
    if (_host.canUseParams()) _host.paramsRequestFlush();
  }
  void VialfxClapPlugin::setValue(uint32_t id, float v) {
    if (const ParamInfo* p = synth_->findParam(id)) synth_->setParamFromGui(*p, v);
    if (_host.canUseParams()) _host.paramsRequestFlush();
  }
  void VialfxClapPlugin::endGesture(uint32_t id) {
    if (const ParamInfo* p = synth_->findParam(id)) synth_->endGuiGesture(*p);
    if (_host.canUseParams()) _host.paramsRequestFlush();
  }
  std::string VialfxClapPlugin::presetName() const { return synth_->getPresetName().toStdString(); }
  std::string VialfxClapPlugin::version() const { return VIALFX_VERSION; }
}
