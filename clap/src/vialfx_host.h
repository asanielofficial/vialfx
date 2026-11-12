/* VialFX CLAP host layer: runs the VialFX engine without JUCE's plugin or GUI classes.
 *
 * GPL-3.0-or-later, like the rest of VialFX.
 */

#pragma once

#include "JuceHeader.h"
#include "concurrentqueue/concurrentqueue.h"
#include "synth_base.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vialfx {

  // One plugin parameter: every control of the engine that the host may automate.
  struct ParamInfo {
    uint32_t id = 0;            // stable: a hash of the engine name, so saved automation survives reordering
    std::string name;           // engine name, e.g. "filter_fx_cutoff"
    std::string display_name;   // "Filter Fx Cutoff"
    std::string module;         // first word of the name, "filter"
    float min = 0.0f, max = 1.0f, default_value = 0.0f;
    bool stepped = false;       // indexed values
    vital::Value* value = nullptr;
  };

  struct ParamChange {
    uint32_t id;
    float value;
  };

  // Block of audio for one process call. Null pointers mean "not connected".
  struct AudioBlock {
    const float* input[2] = { nullptr, nullptr };
    const float* sidechain[2] = { nullptr, nullptr };
    float* output[2] = { nullptr, nullptr };
    int num_samples = 0;
  };

  struct Transport {
    bool has_tempo = false;
    double tempo = 120.0;
    bool is_playing = false;
    bool has_beats = false;
    double song_position_beats = 0.0;
  };

  class VialfxHost : public SynthBase {
    public:
      VialfxHost();
      ~VialfxHost() override;

      // Parameters.
      const std::vector<ParamInfo>& params() const { return params_; }
      const ParamInfo* findParam(uint32_t id) const;
      const ParamInfo* findParam(const std::string& name) const;
      static uint32_t idForName(const std::string& name);
      float getParamValue(const ParamInfo& info) const;
      std::string valueToText(const ParamInfo& info, float value) const;
      bool textToValue(const ParamInfo& info, const std::string& text, float* value) const;
      // Sets a value as the host does: queued for the engine, with no notification back.
      void setParamFromHost(const ParamInfo& info, float value);
      // Sets a value as the interface does: tells the host about it as well.
      void setParamFromGui(const ParamInfo& info, float value);
      void beginGuiGesture(const ParamInfo& info);
      void endGuiGesture(const ParamInfo& info);

      // Changes the engine made itself (MIDI learn, interface edits), for the host to hear about. Audio thread.
      bool popParamChange(ParamChange& change);
      struct Gesture { uint32_t id; bool begin; };
      bool popGesture(Gesture& gesture);
      // Set when a change is waiting, so the plugin can ask the host to flush outside of processing.
      bool hasPendingOutput() const { return pending_output_.load() > 0; }

      // Audio.
      void prepare(double sample_rate, int max_frames);
      void process(const AudioBlock& block, juce::MidiBuffer& midi, const Transport& transport);

      // State.
      std::string saveState();
      bool loadState(const void* data, size_t size);

      int latencySamples() { return getEngineLatencySamples(); }

      // SynthBase.
      const CriticalSection& getCriticalSection() override { return critical_section_; }
      void pauseProcessing(bool pause) override;
      void setValueNotifyHost(const std::string& name, vital::mono_float value) override;
      void beginChangeGesture(const std::string& name) override;
      void endChangeGesture(const std::string& name) override;

    protected:
      SynthGuiInterface* getGuiInterface() override { return nullptr; }

    private:
      void applyBypassed(const AudioBlock& block);

      std::vector<ParamInfo> params_;
      std::unordered_map<uint32_t, size_t> id_lookup_;
      std::unordered_map<std::string, size_t> name_lookup_;
      vital::Value* bypass_ = nullptr;

      CriticalSection critical_section_;
      moodycamel::ConcurrentQueue<ParamChange> output_changes_;
      moodycamel::ConcurrentQueue<Gesture> output_gestures_;
      std::atomic<int> pending_output_ { 0 };
      std::atomic<bool> loading_state_ { false };

      double sample_rate_ = 44100.0;
      double last_seconds_time_ = 0.0;
      vital::poly_float input_[vital::kMaxBufferSize];
      vital::poly_float sidechain_[vital::kMaxBufferSize];
  };
}
