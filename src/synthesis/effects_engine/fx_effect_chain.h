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
#include "keytrack_voices.h"
#include "morph_module.h"
#include "flanger_module.h"
#include "freq_shifter_module.h"
#include "fx_containers.h"
#include "phaser_module.h"
#include "pitch_shifter_module.h"
#include "reverb_module.h"
#include "tremolo_module.h"
#include "utility_module.h"
#include "vibrato_module.h"
#include "synth_constants.h"
#include "synth_strings.h"

#include <array>
#include <atomic>

namespace vital {

  // The effects that follow the played note (key tracking) run one copy per held note, up to this many (see KeytrackBank).
  static constexpr int kChainKeytrackVoices = 4;

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
      static constexpr int kMaxChain = kMaxFxChainEntries;
      static constexpr int kMaxNesting = kMaxFxNesting;
      static constexpr int kMinimizedFlag = 128;

      // "chorus", "chorus_2", ...: the id the parameters of a copy start with (followed by "_").
      static std::string instanceId(int type, int instance) {
        std::string id = strings::kEffectOrder[type];
        return instance == 0 ? id : id + "_" + std::to_string(instance + 1);
      }

      static int effectId(int type, int instance) { return type * kMaxEffectInstances + instance; }
      static int typeOf(int effect_id) { return effect_id / kMaxEffectInstances; }
      static int instanceOf(int effect_id) { return effect_id % kMaxEffectInstances; }

      // keytrack: the played note relative to the center note; notes: the held notes (for the per-note copies).
      FxEffectChain(const Output* beats_per_second, const Output* keytrack, const KeytrackNotes* notes) :
          SynthModule(kNumInputs, 1), beats_per_second_(beats_per_second), keytrack_(keytrack), notes_(notes) {
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
            banks_[id] = createKeytrackBank(type, name, module);
            effects_on_[id] = createBaseControl(name + "_on");
            active_[id] = false;
          }
        }

        for (int i = 0; i < kMaxFxBins; ++i) {
          bins_[i] = new FxBinModule(i);
          addSubmodule(bins_[i]);
          addProcessor(bins_[i]);
        }
        for (int i = 0; i < kMaxParallelSplits; ++i) {
          splits_[i] = new FxSplitModule(i);
          addSubmodule(splits_[i]);
          addProcessor(splits_[i]);
        }
      }

      // Connects the played note (pitch bend included) and the note the last played one glides to. Both only exist
      // once the modulation handler is initialized, so this is called after the engine's init.
      void connectNote(const Output* midi, const Output* current_note) {
        for (int id = 0; id < kPoolSize; ++id) {
          if (banks_[id] == nullptr)
            continue;

          SynthModule* module = effects_[id];
          switch (typeOf(id)) {
            case constants::kChorus: module->plug(midi, ChorusModule::kMidi); break;
            case constants::kDelay: module->plug(midi, DelayModule::kMidi); break;
            case constants::kFlanger: module->plug(midi, FlangerModule::kMidi); break;
            case constants::kPhaser: module->plug(midi, PhaserModule::kMidi); break;
            case constants::kFilterFx: module->plug(midi, FilterFxModule::kMidi); break;
            default: break;
          }
          banks_[id]->setNoteOutputs(midi, current_note);
        }
      }

      // The Morph effects need the FFT size (an index, from the engine).
      void connectMorph(const Processor* fft_size) {
        for (int instance = 0; instance < kMaxEffectInstances; ++instance)
          effects_[effectId(constants::kMorph, instance)]->plug(fft_size, MorphModule::kFftSize);
      }

      // Whether a copy of the Morph effect is in the chain (on or off). Safe from any thread.
      bool morphInChain(int instance) const {
        int wanted = effectId(constants::kMorph, instance);
        for (auto& slot : slots_) {
          int value = slot.load(std::memory_order_relaxed);
          if (chain_token::isEffect(value) && ((value - 1) & (kMinimizedFlag - 1)) == wanted)
            return true;
        }
        return false;
      }

      // The delay the Morph effects in the chain add, in host samples: effects one after the other add up, the
      // branches of a split run side by side (the longest counts), a bin that is off adds nothing.
      int morphLatencySamples(int fft_index) const {
        int tokens[kMaxChain];
        int count = loadTokens(tokens);
        return rangeLatency(tokens, 0, count, fft_index);
      }

      void process(int num_samples) override {
        processWithInput(input(kAudio)->source->buffer, num_samples);
      }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        int tokens[kMaxChain];
        int count = loadTokens(tokens);
        bool visited[kPoolSize] = {};

        audio_in = runRange(tokens, 0, count, audio_in, num_samples, visited);

        // Effects that left the chain (or sit in a bin or branch that is off) stop running and forget their tails.
        for (int id = 0; id < kPoolSize; ++id) {
          if (!visited[id] && effects_[id]->enabled())
            effects_[id]->enable(false);
        }

        VITAL_ASSERT(utils::isFinite(audio_in, num_samples));
        utils::copyBuffer(output()->buffer, audio_in, num_samples);
      }

      void hardReset() override {
        for (int id = 0; id < kPoolSize; ++id)
          banks_[id] ? banks_[id]->hardReset() : effects_[id]->hardReset();
      }

      void correctToTime(double seconds) override {
        for (int id = 0; id < kPoolSize; ++id) {
          effects_[id]->correctToTime(seconds);
          if (banks_[id])
            banks_[id]->correctToTime(seconds);
        }
      }

      virtual Processor* clone() const override { return new FxEffectChain(*this); }

      // The chain (may be called from the message thread while audio runs; each slot is atomic).
      void setChain(const int* slots, int num_slots) {
        int clean[kMaxChain];
        int count = num_slots > 0 ? chain_token::sanitize(slots, num_slots, clean, kPoolSize) : 0;
        for (int i = 0; i < kMaxChain; ++i)
          slots_[i].store(i < count ? clean[i] : 0);
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

      FxBinModule* getBin(int index) { return bins_[index]; }
      FxSplitModule* getSplit(int index) { return splits_[index]; }

      SynthModule* getEffect(int type, int instance = 0) { return effects_[effectId(type, instance)]; }
      const StereoMemory* getEqualizerMemory(int instance = 0) { return equalizer_memory_[instance]; }

    protected:
      // The slots as one well formed list (the interface may be midway through changing them).
      int loadTokens(int* tokens) const {
        int raw[kMaxChain];
        int count = 0;
        for (auto& slot : slots_) {
          int value = slot.load(std::memory_order_relaxed);
          if (value > 0)
            raw[count++] = value;
        }
        return chain_token::sanitize(raw, count, tokens, kPoolSize);
      }

      int rangeLatency(const int* tokens, int begin, int end, int fft_index) const {
        int total = 0;
        for (int i = begin; i < end; ++i) {
          int token = tokens[i];
          if (chain_token::isEffect(token)) {
            int id = (token - 1) & (kMinimizedFlag - 1);
            if (typeOf(id) == constants::kMorph && effects_on_[id]->value() >= 0.5f)
              total += static_cast<MorphModule*>(effects_[id])->latencySamples(fft_index);
          }
          else if (chain_token::kind(token) == chain_token::kBinBegin) {
            int close = chain_token::findEnd(tokens, i, end);
            if (bins_[chain_token::index(token)]->on())
              total += rangeLatency(tokens, i + 1, close, fft_index);
            i = close;
          }
          else if (chain_token::kind(token) == chain_token::kSplitBegin) {
            int close = chain_token::findEnd(tokens, i, end);
            if (splits_[chain_token::index(token)]->on()) {
              int longest = 0, branch_begin = i + 1, nesting = 0;
              for (int j = i + 1; j <= close; ++j) {
                int k = chain_token::kind(tokens[j]);
                if (k == chain_token::kBinBegin || k == chain_token::kSplitBegin)
                  ++nesting;
                else if ((k == chain_token::kBinEnd || k == chain_token::kSplitEnd) && nesting > 0)
                  --nesting;
                else if (nesting == 0 && (k == chain_token::kBranch || j == close)) {
                  longest = std::max(longest, rangeLatency(tokens, branch_begin, j, fft_index));
                  branch_begin = j + 1;
                }
              }
              total += longest;
            }
            i = close;
          }
        }
        return total;
      }

      // Runs the entries [begin, end) one after the other; returns the audio that comes out.
      const poly_float* runRange(const int* tokens, int begin, int end, const poly_float* audio_in, int num_samples,
                                 bool* visited) {
        for (int i = begin; i < end; ++i) {
          int token = tokens[i];
          if (chain_token::isEffect(token)) {
            VITAL_ASSERT(utils::isFinite(audio_in, num_samples));
            int id = (token - 1) & (kMinimizedFlag - 1);
            visited[id] = true;
            bool on = effects_on_[id]->value();
            if (on != effects_[id]->enabled())
              effects_[id]->enable(on);

            if (on) {
              SynthModule* runner = banks_[id] ? static_cast<SynthModule*>(banks_[id]) : effects_[id];
              runner->processWithInput(audio_in, num_samples);
              audio_in = runner->output(0)->buffer;
            }
          }
          else if (chain_token::kind(token) == chain_token::kBinBegin) {
            int close = chain_token::findEnd(tokens, i, end);
            FxBinModule* bin = bins_[chain_token::index(token)];
            bin->update(num_samples);
            if (bin->on()) {
              const poly_float* wet = runRange(tokens, i + 1, close, audio_in, num_samples, visited);
              audio_in = bin->blend(audio_in, wet, num_samples);
            }
            i = close;
          }
          else if (chain_token::kind(token) == chain_token::kSplitBegin) {
            int close = chain_token::findEnd(tokens, i, end);
            FxSplitModule* split = splits_[chain_token::index(token)];

            // Where each branch starts and which control slot it uses.
            int branch_begin[kMaxParallelBranches + 1];
            int branch_end[kMaxParallelBranches + 1];
            int branch_slot[kMaxParallelBranches + 1];
            int num_branches = 0, slot_mask = 0, nesting = 0;
            branch_begin[0] = i + 1;
            branch_slot[0] = chain_token::slot(token);
            for (int j = i + 1; j <= close; ++j) {
              int k = chain_token::kind(tokens[j]);
              if (k == chain_token::kBinBegin || k == chain_token::kSplitBegin)
                ++nesting;
              else if ((k == chain_token::kBinEnd || k == chain_token::kSplitEnd) && nesting > 0)
                --nesting;
              else if (nesting == 0 && (k == chain_token::kBranch || j == close)) {
                branch_end[num_branches] = j;
                slot_mask |= 1 << branch_slot[num_branches];
                ++num_branches;
                if (j < close && num_branches <= kMaxParallelBranches) {
                  branch_begin[num_branches] = j + 1;
                  branch_slot[num_branches] = chain_token::slot(tokens[j]);
                }
              }
            }

            split->begin(num_samples, slot_mask);
            if (split->on()) {
              for (int b = 0; b < num_branches && b < kMaxParallelBranches; ++b) {
                const poly_float* branch = runRange(tokens, branch_begin[b], branch_end[b], audio_in,
                                                    num_samples, visited);
                split->addBranch(branch_slot[b], branch, num_samples);
              }
              audio_in = split->finish(audio_in, num_samples);
            }
            i = close;
          }
        }
        return audio_in;
      }

      // The effects that can follow the played note get copies for the held notes. The copies share the original's
      // controls and modulation (ControlAliasScope) and only differ in the note they track.
      KeytrackBank* createKeytrackBank(int type, const std::string& name, SynthModule* module) {
        std::function<SynthModule*(Output*, Output*)> make;
        std::vector<std::pair<std::string, bool>> gates;  // control, true when "not zero" (else equals Keytrack mode)
        const Output* beats = beats_per_second_;
        bool has_input = true;

        switch (type) {
          case constants::kChorus:
            make = [beats](Output*, Output* midi) {
              ChorusModule* copy = new ChorusModule(beats);
              copy->plug(midi, ChorusModule::kMidi);
              return copy;
            };
            gates.push_back({ name + "_sync", false });
            break;
          case constants::kDelay:
            make = [beats](Output*, Output* midi) {
              // Followed notes make short delays, so the copies get a short buffer.
              DelayModule* copy = new DelayModule(beats, kKeytrackDelayTime);
              copy->plug(midi, DelayModule::kMidi);
              return copy;
            };
            gates.push_back({ name + "_sync", false });
            gates.push_back({ name + "_aux_sync", false });
            break;
          case constants::kFlanger:
            make = [beats](Output*, Output* midi) {
              FlangerModule* copy = new FlangerModule(beats);
              copy->plug(midi, FlangerModule::kMidi);
              return copy;
            };
            gates.push_back({ name + "_sync", false });
            break;
          case constants::kPhaser:
            make = [beats](Output*, Output* midi) {
              PhaserModule* copy = new PhaserModule(beats);
              copy->plug(midi, PhaserModule::kMidi);
              return copy;
            };
            gates.push_back({ name + "_sync", false });
            break;
          case constants::kFilterFx: {
            std::string prefix = "filter_fx" + name.substr(std::string("filter_fx").size());
            make = [prefix](Output* keytrack, Output* midi) {
              FilterFxModule* copy = new FilterFxModule(prefix, keytrack);
              copy->plug(midi, FilterFxModule::kMidi);
              return copy;
            };
            gates.push_back({ name + "_keytrack", true });
            break;
          }
          default:
            return nullptr;
        }

        KeytrackBank* bank = new KeytrackBank(module, has_input, kChainKeytrackVoices, notes_, keytrack_, make);
        for (auto& gate : gates) {
          if (gate.second)
            bank->addGate(gate.first, true);
          else
            bank->addGate(gate.first, false, static_cast<mono_float>(TempoChooser::kKeytrack));
        }
        addProcessor(bank);
        return bank;
      }

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
          case constants::kMorph:
            return new MorphModule();
          case constants::kPitchShifter:
            return new PitchShifterModule();
          case constants::kUtility:
            return new UtilityModule();
          case constants::kFreqShifter:
            return new FreqShifterModule(beats_per_second_, keytrack_);
          case constants::kVibrato:
            return new VibratoModule(beats_per_second_);
          case constants::kTremolo:
            return new TremoloModule(beats_per_second_);
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
            kMidi,
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
            filter_->useInput(input(kMidi), FilterModule::kMidi);
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
      static constexpr mono_float kKeytrackDelayTime = 1.0f;

      const Output* keytrack_;
      const KeytrackNotes* notes_;
      KeytrackBank* banks_[kPoolSize] = {};
      FxBinModule* bins_[kMaxFxBins] = {};
      FxSplitModule* splits_[kMaxParallelSplits] = {};
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
