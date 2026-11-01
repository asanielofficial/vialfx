/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "synth_module.h"

#include "chorus_module.h"
#include "compressor_module.h"
#include "delay_module.h"
#include "distortion_module.h"
#include "equalizer_module.h"
#include "filter_module.h"
#include "flanger_module.h"
#include "phaser_module.h"
#include "reverb_module.h"
#include "synth_constants.h"
#include "synth_strings.h"

#include <array>
#include <atomic>

namespace vital {

  // The effect build's effect chain: any number of effects (up to kMaxEffectInstances of each type) in any order.
  //
  // Every copy of an effect exists in the engine from the start (a pool, so every parameter is a real, automatable
  // and modulatable control); only the copies listed in the chain are processed. The chain is a list of slots; a
  // slot holds 0 (empty) or 1 + effectId (+ kMinimizedFlag, which only the interface looks at).
  // effectId = type * kMaxEffectInstances + instance, the type being a constants::Effect.
  class FxEffectChain : public SynthModule {
    public:
      enum {
        kAudio,
        kNumInputs
      };

      static constexpr int kNumTypes = constants::kNumEffects;
      static constexpr int kPoolSize = kNumTypes * kMaxEffectInstances;
      static constexpr int kMaxChain = kPoolSize;
      static constexpr int kMinimizedFlag = 128;

      // "chorus", "chorus_2", ...: the id the parameters of a copy start with (followed by "_").
      static std::string instanceId(int type, int instance) {
        std::string id = strings::kEffectOrder[type];
        return instance == 0 ? id : id + "_" + std::to_string(instance + 1);
      }

      static int effectId(int type, int instance) { return type * kMaxEffectInstances + instance; }
      static int typeOf(int effect_id) { return effect_id / kMaxEffectInstances; }
      static int instanceOf(int effect_id) { return effect_id % kMaxEffectInstances; }

      FxEffectChain(const Output* beats_per_second, const Output* keytrack) :
          SynthModule(kNumInputs, 1), beats_per_second_(beats_per_second), keytrack_(keytrack) {
        for (auto& slot : slots_)
          slot.store(0);

        for (int type = 0; type < kNumTypes; ++type) {
          for (int instance = 0; instance < kMaxEffectInstances; ++instance) {
            int id = effectId(type, instance);
            std::string name = instanceId(type, instance);
            std::string base = strings::kEffectOrder[type];
            // Copy n of an effect is the same code with its controls renamed.
            ControlRemapScope remap(instance == 0 ? "" : base + "_", instance == 0 ? "" : name + "_");
            SynthModule* module = createEffectModule(type, name, id);
            VITAL_ASSERT(module);

            addSubmodule(module);
            addProcessor(module);
            effects_[id] = module;
            effects_on_[id] = createBaseControl(name + "_on");
            active_[id] = false;
          }
        }
      }

      void process(int num_samples) override {
        processWithInput(input(kAudio)->source->buffer, num_samples);
      }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        bool in_chain[kPoolSize] = {};
        int order[kMaxChain];
        int count = 0;
        for (auto& slot : slots_) {
          int value = slot.load(std::memory_order_relaxed);
          if (value <= 0)
            continue;

          int id = ((value - 1) & (kMinimizedFlag - 1));
          if (id >= kPoolSize || in_chain[id])
            continue;

          in_chain[id] = true;
          order[count++] = id;
        }

        // Effects that left the chain stop running (and forget their tails).
        for (int id = 0; id < kPoolSize; ++id) {
          if (!in_chain[id] && effects_[id]->enabled())
            effects_[id]->enable(false);
        }

        for (int i = 0; i < count; ++i) {
          VITAL_ASSERT(utils::isFinite(audio_in, num_samples));

          int id = order[i];
          bool on = effects_on_[id]->value();
          if (on != effects_[id]->enabled())
            effects_[id]->enable(on);

          if (on) {
            effects_[id]->processWithInput(audio_in, num_samples);
            audio_in = effects_[id]->output(0)->buffer;
          }
        }

        VITAL_ASSERT(utils::isFinite(audio_in, num_samples));
        utils::copyBuffer(output()->buffer, audio_in, num_samples);
      }

      void hardReset() override {
        for (int id = 0; id < kPoolSize; ++id)
          effects_[id]->hardReset();
      }

      void correctToTime(double seconds) override {
        for (int id = 0; id < kPoolSize; ++id)
          effects_[id]->correctToTime(seconds);
      }

      virtual Processor* clone() const override { return new FxEffectChain(*this); }

      // The chain (may be called from the message thread while audio runs; each slot is atomic).
      void setChain(const int* slots, int num_slots) {
        for (int i = 0; i < kMaxChain; ++i)
          slots_[i].store(i < num_slots ? slots[i] : 0);
      }

      int getChain(int* slots) const {
        int count = 0;
        for (int i = 0; i < kMaxChain; ++i) {
          int value = slots_[i].load();
          if (value > 0)
            slots[count++] = value;
        }
        return count;
      }

      SynthModule* getEffect(int type, int instance = 0) { return effects_[effectId(type, instance)]; }
      const StereoMemory* getEqualizerMemory(int instance = 0) { return equalizer_memory_[instance]; }

    protected:
      SynthModule* createEffectModule(int type, const std::string& name, int id) {
        switch (type) {
          case constants::kChorus:
            return new ChorusModule(beats_per_second_);
          case constants::kCompressor:
            return new CompressorModule();
          case constants::kDelay:
            return new DelayModule(beats_per_second_);
          case constants::kDistortion:
            return new DistortionModule();
          case constants::kEq: {
            EqualizerModule* eq = new EqualizerModule();
            equalizer_memory_[instanceOf(id)] = eq->getAudioMemory();
            return eq;
          }
          case constants::kFlanger:
            return new FlangerModule(beats_per_second_);
          case constants::kFilterFx:
            return new FilterFxModule("filter_fx" + name.substr(std::string("filter_fx").size()), keytrack_);
          case constants::kPhaser:
            return new PhaserModule(beats_per_second_);
          case constants::kReverb:
            return new ReverbModule();
          default:
            return nullptr;
        }
      }

      // One filter, as an effect.
      class FilterFxModule : public SynthModule {
        public:
          enum {
            kAudio,
            kKeytrack,
            kNumInputs
          };

          FilterFxModule(const std::string& prefix, const Output* keytrack) : SynthModule(kNumInputs, 1) {
            filter_ = new FilterModule(prefix);
            filter_->setCreateOnValue(false);
            filter_->setMono(true);
            addSubmodule(filter_);
            addProcessor(filter_);
            filter_->useOutput(output());
            filter_->plug(&input_, FilterModule::kAudio);
            filter_->plug(keytrack, FilterModule::kKeytrack);
          }

          void processWithInput(const poly_float* audio_in, int num_samples) override {
            utils::copyBuffer(input_.buffer, audio_in, num_samples);
            filter_->process(num_samples);
          }

          void setOversampleAmount(int oversampling) override {
            input_.ensureBufferSize(kMaxBufferSize * oversampling);
            SynthModule::setOversampleAmount(oversampling);
          }

        private:
          FilterModule* filter_;
          Output input_;
      };

      const Output* beats_per_second_;
      const Output* keytrack_;
      const StereoMemory* equalizer_memory_[kMaxEffectInstances] = {};
      SynthModule* effects_[kPoolSize];
      Value* effects_on_[kPoolSize];
      bool active_[kPoolSize];
      // Atomic so the interface can change the chain while audio runs (copyable, to keep clone() working).
      struct Slot {
        Slot() = default;
        Slot(const Slot& other) { value.store(other.value.load()); }
        Slot& operator=(const Slot& other) { value.store(other.value.load()); return *this; }
        void store(int v) { value.store(v); }
        int load(std::memory_order order = std::memory_order_seq_cst) const { return value.load(order); }
        std::atomic<int> value { 0 };
      };
      std::array<Slot, kMaxChain> slots_;

      JUCE_LEAK_DETECTOR(FxEffectChain)
  };
} // namespace vital
