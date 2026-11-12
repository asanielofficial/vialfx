#pragma once

#include "gui_bridge.h"
#include "vialfx_host.h"

#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/plugin.hh>

#include <atomic>
#include <memory>

namespace vialfx {

  using ClapPluginBase = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                               clap::helpers::CheckingLevel::Minimal>;

  class VialfxClapPlugin : public ClapPluginBase, public GuiHost {
    public:
      static const clap_plugin_descriptor* descriptor();

      explicit VialfxClapPlugin(const clap_host* host);
      ~VialfxClapPlugin() override;

      // clap_plugin
      bool init() noexcept override;
      bool activate(double sample_rate, uint32_t min_frames, uint32_t max_frames) noexcept override;
      void deactivate() noexcept override;
      bool startProcessing() noexcept override { return true; }
      clap_process_status process(const clap_process* process) noexcept override;
      void reset() noexcept override;
      void onMainThread() noexcept override;

      // latency / tail / state
      bool implementsLatency() const noexcept override { return true; }
      uint32_t latencyGet() const noexcept override;
      bool implementsTail() const noexcept override { return true; }
      uint32_t tailGet() const noexcept override;
      bool implementsState() const noexcept override { return true; }
      bool stateSave(const clap_ostream* stream) noexcept override;
      bool stateLoad(const clap_istream* stream) noexcept override;

      // ports
      bool implementsAudioPorts() const noexcept override { return true; }
      uint32_t audioPortsCount(bool is_input) const noexcept override { return is_input ? 2 : 1; }
      bool audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept override;
      bool implementsNotePorts() const noexcept override { return true; }
      uint32_t notePortsCount(bool is_input) const noexcept override { return is_input ? 1 : 0; }
      bool notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept override;

      // params
      bool implementsParams() const noexcept override { return true; }
      uint32_t paramsCount() const noexcept override;
      bool paramsInfo(uint32_t index, clap_param_info* info) const noexcept override;
      bool paramsValue(clap_id id, double* value) noexcept override;
      bool paramsValueToText(clap_id id, double value, char* text, uint32_t size) noexcept override;
      bool paramsTextToValue(clap_id id, const char* text, double* value) noexcept override;
      void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

      // gui
      bool implementsGui() const noexcept override { return true; }
      bool guiIsApiSupported(const char* api, bool is_floating) noexcept override;
      bool guiGetPreferredApi(const char** api, bool* is_floating) noexcept override;
      bool guiCreate(const char* api, bool is_floating) noexcept override;
      void guiDestroy() noexcept override;
      bool guiSetScale(double scale) noexcept override { return false; }
      bool guiShow() noexcept override { return gui_ != nullptr; }
      bool guiHide() noexcept override { return gui_ != nullptr; }
      bool guiSetParent(const clap_window* window) noexcept override;
      bool guiCanResize() const noexcept override { return true; }
      bool guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept override;
      bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept override;
      bool guiSetSize(uint32_t width, uint32_t height) noexcept override;
      bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override;
#ifdef __linux__
      bool implementsPosixFdSupport() const noexcept override { return true; }
      void onPosixFd(int fd, clap_posix_fd_flags_t flags) noexcept override;
#endif

      // GuiHost
      const std::vector<GuiParam>& params() const override { return gui_params_; }
      float value(uint32_t id) const override;
      std::string valueText(uint32_t id, float value) const override;
      void beginGesture(uint32_t id) override;
      void setValue(uint32_t id, float value) override;
      void endGesture(uint32_t id) override;
      std::string presetName() const override;
      std::string version() const override;

    private:
      void handleInputEvents(const clap_input_events* in);
      void writeOutputEvents(const clap_output_events* out);

      std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce_init_;
      std::unique_ptr<VialfxHost> synth_;
      std::vector<GuiParam> gui_params_;
      std::unique_ptr<GuiWindow> gui_;

      double sample_rate_ = 44100.0;
      bool active_ = false;
      std::atomic<bool> latency_dirty_ { false };
      int reported_latency_ = 0;
      Transport transport_;
      juce::MidiBuffer midi_;
  };
}
