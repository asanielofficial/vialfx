// VialFX - FX Bin and Parallel Split containers of the effect chain. Licensed under GPL-3.0-or-later.
#pragma once

#include "synth_constants.h"
#include "synth_module.h"

#include <algorithm>
#include <string>

namespace vital {

  // The entries of the effect chain. An effect is 1 + effectId (+ kMinimizedFlag, which only the interface looks
  // at). Containers are marker entries (kind in the bits from 12 up, the container's index in bits 0-6, the minimized
  // flag in bit 7 and, for splits, the branch slot in bits 8-11):
  //   BinBegin(b) ... BinEnd(b)
  //   SplitBegin(s, slot) ... Branch(s, slot) ... SplitEnd(s)      (the first branch starts at SplitBegin)
  // A branch slot names the branch's controls ("psplit_<s+1>_b<slot+1>_level"...), so they stay with a branch when
  // branches are added or removed.
  namespace chain_token {
    constexpr int kMinimizedFlag = 128;
    constexpr int kBinBegin = 1 << 12;
    constexpr int kBinEnd = 2 << 12;
    constexpr int kSplitBegin = 3 << 12;
    constexpr int kBranch = 4 << 12;
    constexpr int kSplitEnd = 5 << 12;

    inline bool isMarker(int token) { return token >= kBinBegin; }
    inline bool isEffect(int token) { return token > 0 && token < kBinBegin; }
    inline int kind(int token) { return token & ~0xFFF; }
    inline int index(int token) { return token & 0x7F; }
    inline int slot(int token) { return (token >> 8) & 0xF; }
    inline bool minimized(int token) { return (token & kMinimizedFlag) != 0; }

    inline int binBegin(int bin, bool minimized = false) { return kBinBegin | bin | (minimized ? kMinimizedFlag : 0); }
    inline int binEnd(int bin) { return kBinEnd | bin; }
    inline int splitBegin(int split, int slot, bool minimized = false) {
      return kSplitBegin | (slot << 8) | split | (minimized ? kMinimizedFlag : 0);
    }
    inline int branch(int split, int slot) { return kBranch | (slot << 8) | split; }
    inline int splitEnd(int split) { return kSplitEnd | split; }

    // Makes any list of entries well formed: markers match and nest (at most kMaxFxNesting deep), every container
    // and effect appears once, a split's branch slots are different. Unusable entries are dropped. `out` holds at
    // least `count` entries; returns the new count. Safe to call on the audio thread (no allocation).
    inline int sanitize(const int* in, int count, int* out, int effect_pool_size) {
      struct Frame {
        int kind;        // kBinBegin or kSplitBegin
        int index;
        int begin_pos;
        int slots_used;
        int num_branches;
        int branch_pos[kMaxParallelBranches];
      };

      bool keep[kMaxFxChainEntries + 8] = {};
      int tokens[kMaxFxChainEntries + 8];
      count = std::min(count, kMaxFxChainEntries);
      bool effect_used[256] = {};
      bool bin_used[kMaxFxBins] = {};
      bool split_used[kMaxParallelSplits] = {};
      Frame stack[kMaxFxNesting + 1];
      int depth = 0;
      // Containers that were refused still consume their end marker.
      int refused_depth = 0;
      int refused_kind[64];
      int refused_index[64];

      for (int i = 0; i < count; ++i) {
        int token = in[i];
        tokens[i] = token;
        if (isEffect(token)) {
          int id = (token - 1) & (kMinimizedFlag - 1);
          if (id < effect_pool_size && !effect_used[id]) {
            effect_used[id] = true;
            keep[i] = true;
          }
          continue;
        }

        int k = kind(token);
        int idx = index(token);
        if (k == kBinBegin || k == kSplitBegin) {
          bool is_bin = k == kBinBegin;
          bool ok = depth < kMaxFxNesting && refused_depth == 0 &&
                    (is_bin ? idx < kMaxFxBins && !bin_used[idx] : idx < kMaxParallelSplits && !split_used[idx]);
          if (ok) {
            (is_bin ? bin_used[idx] : split_used[idx]) = true;
            Frame& frame = stack[depth++];
            frame.kind = k;
            frame.index = idx;
            frame.begin_pos = i;
            frame.slots_used = is_bin ? 0 : 1 << slot(token);
            frame.num_branches = 0;
            keep[i] = true;
          }
          else if (refused_depth < 64) {
            refused_kind[refused_depth] = k;
            refused_index[refused_depth++] = idx;
          }
        }
        else if (k == kBinEnd || k == kSplitEnd) {
          int begin_kind = k == kBinEnd ? kBinBegin : kSplitBegin;
          if (refused_depth > 0) {
            if (refused_kind[refused_depth - 1] == begin_kind && refused_index[refused_depth - 1] == idx)
              --refused_depth;
          }
          else if (depth > 0 && stack[depth - 1].kind == begin_kind && stack[depth - 1].index == idx) {
            keep[i] = true;
            --depth;
          }
        }
        else if (k == kBranch) {
          if (refused_depth == 0 && depth > 0 && stack[depth - 1].kind == kSplitBegin && stack[depth - 1].index == idx) {
            Frame& frame = stack[depth - 1];
            int s = slot(token);
            if ((frame.slots_used & (1 << s)) == 0 && frame.num_branches < kMaxParallelBranches - 1) {
              frame.slots_used |= 1 << s;
              frame.branch_pos[frame.num_branches++] = i;
              keep[i] = true;
            }
          }
        }
      }

      // Containers that never closed lose their markers (their contents stay).
      for (int d = 0; d < depth; ++d) {
        keep[stack[d].begin_pos] = false;
        for (int b = 0; b < stack[d].num_branches; ++b)
          keep[stack[d].branch_pos[b]] = false;
      }

      int written = 0;
      for (int i = 0; i < count; ++i) {
        if (keep[i])
          out[written++] = tokens[i];
      }
      return written;
    }

    // Index of the end marker that closes the container begun at `begin` (entries must be well formed).
    inline int findEnd(const int* tokens, int begin, int end) {
      int nesting = 0;
      for (int i = begin; i < end; ++i) {
        int k = kind(tokens[i]);
        if (k == kBinBegin || k == kSplitBegin)
          ++nesting;
        else if ((k == kBinEnd || k == kSplitEnd) && --nesting == 0)
          return i;
      }
      return end;
    }
  } // namespace chain_token

  // The controls and the mixing of one FX Bin: a power button and a dry/wet mix for the effects inside.
  class FxBinModule : public SynthModule {
    public:
      FxBinModule(int index) : SynthModule(0, 1), prefix_("fx_bin_" + std::to_string(index + 1)),
                               on_(nullptr), mix_(nullptr), last_mix_(1.0f) { }

      void init() override {
        on_ = createBaseControl(prefix_ + "_on");
        mix_ = createMonoModControl(prefix_ + "_mix");
        SynthModule::init();
      }

      // Refreshes the controls for this block.
      void update(int num_samples) { SynthModule::process(num_samples); }
      bool on() const { return on_ && on_->value() > 0.5f; }
      Value* onControl() { return on_; }

      // The bin's output: the unprocessed input mixed with what the effects inside made of it.
      const poly_float* blend(const poly_float* dry, const poly_float* wet, int num_samples) {
        float target = std::min(1.0f, std::max(0.0f, mix_->buffer[0][0]));
        if (target >= 0.9999f && last_mix_ >= 0.9999f) {
          last_mix_ = target;
          return wet;
        }

        poly_float* out = output()->buffer;
        float step = (target - last_mix_) / num_samples;
        float mix = last_mix_;
        for (int i = 0; i < num_samples; ++i) {
          mix += step;
          out[i] = dry[i] + (wet[i] - dry[i]) * mix;
        }
        last_mix_ = target;
        return out;
      }


    private:
      std::string prefix_;
      Value* on_;
      Output* mix_;
      float last_mix_;
  };

  // The controls and the mixing of one Parallel Split: every branch has level, pan, mute and solo; the branches are
  // summed and mixed against the unprocessed input.
  class FxSplitModule : public SynthModule {
    public:
      static constexpr int kMaxBranches = kMaxParallelBranches;

      FxSplitModule(int index) : SynthModule(0, 1), prefix_("psplit_" + std::to_string(index + 1)),
                                 on_(nullptr), mix_(nullptr), last_mix_(1.0f), any_solo_(false) {
        accum_.ensureBufferSize(kMaxBufferSize * kMaxOversample);
        for (int b = 0; b < kMaxBranches; ++b) {
          level_[b] = pan_[b] = nullptr;
          mute_[b] = solo_[b] = nullptr;
          last_gain_[b][0] = last_gain_[b][1] = 1.0f;
        }
      }

      void init() override {
        on_ = createBaseControl(prefix_ + "_on");
        mix_ = createMonoModControl(prefix_ + "_mix");
        for (int b = 0; b < kMaxBranches; ++b) {
          std::string branch = prefix_ + "_b" + std::to_string(b + 1);
          level_[b] = createMonoModControl(branch + "_level");
          pan_[b] = createMonoModControl(branch + "_pan");
          mute_[b] = createBaseControl(branch + "_mute");
          solo_[b] = createBaseControl(branch + "_solo");
        }
        SynthModule::init();
      }

      bool on() const { return on_ && on_->value() > 0.5f; }

      // Starts a block: refreshes the controls; slot_mask has a bit for every branch slot in use.
      void begin(int num_samples, int slot_mask) {
        SynthModule::process(num_samples);
        any_solo_ = false;
        for (int b = 0; b < kMaxBranches; ++b) {
          if ((slot_mask & (1 << b)) && solo_[b]->value() > 0.5f)
            any_solo_ = true;
        }
        poly_float* sum = accum_.buffer;
        for (int i = 0; i < num_samples; ++i)
          sum[i] = 0.0f;
      }

      void addBranch(int slot, const poly_float* audio, int num_samples) {
        bool audible = mute_[slot]->value() < 0.5f && (!any_solo_ || solo_[slot]->value() > 0.5f);
        float level = std::min(2.0f, std::max(0.0f, level_[slot]->buffer[0][0]));
        float pan = std::min(1.0f, std::max(-1.0f, pan_[slot]->buffer[0][0]));
        float left = audible ? level * (pan > 0.0f ? 1.0f - pan : 1.0f) : 0.0f;
        float right = audible ? level * (pan < 0.0f ? 1.0f + pan : 1.0f) : 0.0f;

        float step_left = (left - last_gain_[slot][0]) / num_samples;
        float step_right = (right - last_gain_[slot][1]) / num_samples;
        float gain_left = last_gain_[slot][0], gain_right = last_gain_[slot][1];
        poly_float* sum = accum_.buffer;
        for (int i = 0; i < num_samples; ++i) {
          gain_left += step_left;
          gain_right += step_right;
          sum[i] += audio[i] * poly_float(gain_left, gain_right, gain_left, gain_right);
        }
        last_gain_[slot][0] = left;
        last_gain_[slot][1] = right;
      }

      const poly_float* finish(const poly_float* dry, int num_samples) {
        float target = std::min(1.0f, std::max(0.0f, mix_->buffer[0][0]));
        poly_float* out = output()->buffer;
        const poly_float* sum = accum_.buffer;
        float step = (target - last_mix_) / num_samples;
        float mix = last_mix_;
        for (int i = 0; i < num_samples; ++i) {
          mix += step;
          out[i] = dry[i] + (sum[i] - dry[i]) * mix;
        }
        last_mix_ = target;
        return out;
      }


    private:
      std::string prefix_;
      Value* on_;
      Output* mix_;
      Output* level_[kMaxBranches];
      Output* pan_[kMaxBranches];
      Value* mute_[kMaxBranches];
      Value* solo_[kMaxBranches];
      Output accum_;
      float last_mix_;
      float last_gain_[kMaxBranches][2];
      bool any_solo_;
  };
} // namespace vital
