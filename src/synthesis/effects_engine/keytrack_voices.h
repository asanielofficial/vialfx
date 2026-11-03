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

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace vital {

  // The notes that are held (or sustained), newest first. Fed by the sound engine; read by the keytrack banks.
  class KeytrackNotes {
    public:
      static constexpr int kMaxNotes = 32;

      void noteOn(int note) {
        remove(note);
        if (count_ == kMaxNotes)
          remove(entries_[0].note);  // the oldest

        entries_[count_++] = { note, false };
        last_note_ = note;
      }

      void noteOff(int note) {
        for (int i = 0; i < count_; ++i) {
          if (entries_[i].note != note)
            continue;

          if (sustain_)
            entries_[i].released = true;
          else
            erase(i);
          return;
        }
      }

      void sustainOn() { sustain_ = true; }

      void sustainOff() {
        sustain_ = false;
        for (int i = count_ - 1; i >= 0; --i) {
          if (entries_[i].released)
            erase(i);
        }
      }

      void allNotesOff() { count_ = 0; }

      // Writes up to max_notes notes, newest first, and returns how many.
      int collect(int max_notes, int* notes) const {
        int n = std::min(max_notes, count_);
        for (int i = 0; i < n; ++i)
          notes[i] = entries_[count_ - 1 - i].note;
        return n;
      }

      int count() const { return count_; }
      int lastNote() const { return last_note_; }

    private:
      struct Entry {
        int note;
        bool released;
      };

      void remove(int note) {
        for (int i = 0; i < count_; ++i) {
          if (entries_[i].note == note) {
            erase(i);
            return;
          }
        }
      }

      void erase(int index) {
        for (int i = index; i < count_ - 1; ++i)
          entries_[i] = entries_[i + 1];
        --count_;
      }

      Entry entries_[kMaxNotes];
      int count_ = 0;
      int last_note_ = 60;
      bool sustain_ = false;
  };

  // Polyphonic key tracking by parallel processing. The bank holds a processing module (the primary, which follows
  // the last played note like before) and a few aliases of it (see ControlAliasScope): copies with their own state that
  // share its controls. Every held note gets an alias that is tracked to that note, and the aliases are crossfaded in
  // and out as notes come and go. The result is the average of the copies (so holding more notes doesn't make the
  // sound louder); with no note held the primary is used. CPU use grows with the number of notes held, up to the number
  // of aliases.
  //
  // The aliases only run while the effect actually key tracks (see addGate); otherwise the bank is just the primary.
  class KeytrackBank : public SynthModule {
    public:
      static constexpr int kMaxVoices = 8;
      static constexpr mono_float kAttackSeconds = 0.008f;
      static constexpr mono_float kReleaseSeconds = 0.15f;

      // make_alias builds one alias (the ControlAliasScope is already set) tracking the given outputs.
      using AliasFactory = std::function<SynthModule*(Output* keytrack, Output* midi)>;

      // midi_offset: the primary's key tracking (note relative to the center note). run_primary: false when the
      // primary is processed elsewhere (plug it into the bank's input so the bank runs after it).
      KeytrackBank(SynthModule* primary, bool primary_has_input, int num_voices, const KeytrackNotes* notes,
                   const Output* midi_offset, const AliasFactory& make_alias, bool run_primary = true) :
          SynthModule(1, 1), primary_(primary), primary_has_input_(primary_has_input), run_primary_(run_primary),
          num_voices_(std::min(num_voices, kMaxVoices)), notes_(notes),
          midi_offset_(midi_offset), bent_midi_(nullptr), current_note_(nullptr) {
        for (int i = 0; i < kMaxVoices; ++i) {
          keytrack_outputs_[i].buffer[0] = 0.0f;
          midi_outputs_[i].buffer[0] = 0.0f;
        }

        for (int i = 0; i < num_voices_; ++i) {
          ControlAliasScope alias_scope(primary);
          SynthModule* alias = make_alias(&keytrack_outputs_[i], &midi_outputs_[i]);
          VITAL_ASSERT(alias);
          aliases_[i] = alias;
          addProcessor(alias);
        }
      }

      // bent_midi: the played note (pitch bend included); current_note: the (portamento) note the primary follows,
      // to tell the other notes apart from it. They only exist once the engine is initialized.
      void setNoteOutputs(const Output* bent_midi, const Output* current_note) {
        bent_midi_ = bent_midi;
        current_note_ = current_note;
      }

      // The effect only needs voices while this control has this value (or, with kNotZero, any non-zero value).
      void addGate(const std::string& control_name, bool not_zero, mono_float value = 0.0f) {
        gates_.push_back({ control_name, nullptr, not_zero, value, false });
      }

      // The aliases build parts of themselves in their init(), so the alias scope has to be active then too.
      void init() override {
        ControlAliasScope alias_scope(primary_);
        SynthModule::init();
      }

      void process(int num_samples) override { processWithInput(nullptr, num_samples); }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        if (run_primary_) {
          if (primary_has_input_)
            primary_->processWithInput(audio_in, num_samples);
          else
            primary_->process(num_samples);
        }

        const poly_float* primary_buffer = primary_->output(0)->buffer;
        poly_float* out = output()->buffer;

        if (!isActive() || bent_midi_ == nullptr || current_note_ == nullptr) {
          for (int v = 0; v < num_voices_; ++v)
            voices_[v] = Voice();
          primary_gain_ = 1.0f;
          utils::copyBuffer(out, primary_buffer, num_samples);
          return;
        }

        updateVoices(num_samples);

        // Gather what takes part in the mix.
        const poly_float* buffers[kMaxVoices + 1];
        float start[kMaxVoices + 1];
        float end[kMaxVoices + 1];
        int count = 0;
        buffers[count] = primary_buffer;
        start[count] = primary_start_gain_;
        end[count++] = primary_gain_;

        for (int v = 0; v < num_voices_; ++v) {
          Voice& voice = voices_[v];
          if (voice.note < 0)
            continue;

          if (voice.restart) {
            aliases_[v]->hardReset();
            aliases_[v]->enable(false);
            aliases_[v]->enable(true);
            voice.restart = false;
          }

          if (primary_has_input_)
            aliases_[v]->processWithInput(audio_in, num_samples);
          else
            aliases_[v]->process(num_samples);

          buffers[count] = aliases_[v]->output(0)->buffer;
          start[count] = voice.start_gain;
          end[count++] = voice.gain;
        }

        float inverse_samples = 1.0f / num_samples;
        for (int i = 0; i < num_samples; ++i) {
          float t = (i + 1) * inverse_samples;
          poly_float sum = 0.0f;
          float gain_sum = 0.0f;
          for (int c = 0; c < count; ++c) {
            float gain = start[c] + (end[c] - start[c]) * t;
            sum += buffers[c][i] * gain;
            gain_sum += gain;
          }
          out[i] = sum * (1.0f / std::max(gain_sum, 0.001f));
        }

        // Voices that faded out are done.
        for (int v = 0; v < num_voices_; ++v) {
          Voice& voice = voices_[v];
          if (voice.note >= 0 && !voice.wanted && voice.gain <= 0.0f)
            voice.note = -1;
        }
      }

      void hardReset() override {
        primary_->hardReset();
        for (int v = 0; v < num_voices_; ++v)
          aliases_[v]->hardReset();
      }

      void correctToTime(double seconds) override {
        for (int v = 0; v < num_voices_; ++v)
          aliases_[v]->correctToTime(seconds);
      }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      SynthModule* primary() { return primary_; }
      int numVoices() const { return num_voices_; }
      bool isActive() {
        if (gates_.empty())
          return true;

        for (Gate& gate : gates_) {
          if (gate.control == nullptr && !gate.missing) {
            gate.control = primary_->findControl(gate.name);
            gate.missing = gate.control == nullptr;
          }
          if (gate.control == nullptr)
            continue;

          mono_float value = gate.control->value();
          if (gate.not_zero ? value != 0.0f : value == gate.value)
            return true;
        }
        return false;
      }

    private:
      struct Voice {
        int note = -1;
        bool wanted = false;
        bool restart = false;
        float gain = 0.0f;
        float start_gain = 0.0f;
      };

      struct Gate {
        std::string name;
        Value* control;
        bool not_zero;
        mono_float value;
        bool missing;
      };

      void updateVoices(int num_samples) {
        int wanted_notes[kMaxVoices];
        int num_wanted = notes_->collect(num_voices_, wanted_notes);

        for (int v = 0; v < num_voices_; ++v)
          voices_[v].wanted = false;

        // Notes that already have a voice keep it.
        bool has_voice[kMaxVoices] = {};
        for (int w = 0; w < num_wanted; ++w) {
          for (int v = 0; v < num_voices_; ++v) {
            if (voices_[v].note == wanted_notes[w] && !voices_[v].wanted) {
              voices_[v].wanted = true;
              has_voice[w] = true;
              break;
            }
          }
        }

        // New notes take a free voice, or the quietest one that is fading out.
        for (int w = 0; w < num_wanted; ++w) {
          if (has_voice[w])
            continue;

          int choice = -1;
          for (int v = 0; v < num_voices_; ++v) {
            if (voices_[v].note < 0) {
              choice = v;
              break;
            }
          }
          if (choice < 0) {
            float quietest = 2.0f;
            for (int v = 0; v < num_voices_; ++v) {
              if (!voices_[v].wanted && voices_[v].gain < quietest) {
                quietest = voices_[v].gain;
                choice = v;
              }
            }
          }
          if (choice < 0)
            continue;

          Voice& voice = voices_[choice];
          voice.note = wanted_notes[w];
          voice.wanted = true;
          voice.restart = true;
          voice.gain = 0.0f;
        }

        float up_step = num_samples / (getSampleRate() * kAttackSeconds);
        float down_step = num_samples / (getSampleRate() * kReleaseSeconds);

        // The primary covers for the notes: it fades out as soon as one is held and back in when none are.
        primary_start_gain_ = primary_gain_;
        if (num_wanted > 0)
          primary_gain_ = std::max(0.0f, primary_gain_ - up_step);
        else
          primary_gain_ = std::min(1.0f, primary_gain_ + down_step);

        poly_float offset = midi_offset_->buffer[0];
        poly_float midi = bent_midi_->buffer[0];
        float current = current_note_->buffer[0][0];
        int last_note = notes_->lastNote();

        for (int v = 0; v < num_voices_; ++v) {
          Voice& voice = voices_[v];
          voice.start_gain = voice.gain;
          if (voice.note < 0)
            continue;

          if (voice.wanted)
            voice.gain = std::min(1.0f, voice.gain + up_step);
          else
            voice.gain = std::max(0.0f, voice.gain - down_step);

          // The newest note is the one the primary follows (portamento included); the others sit where they were played.
          float difference = voice.note == last_note ? 0.0f : voice.note - current;
          keytrack_outputs_[v].buffer[0] = offset + difference;
          midi_outputs_[v].buffer[0] = midi + difference;
        }
      }

      SynthModule* primary_;
      bool primary_has_input_;
      bool run_primary_;
      int num_voices_;
      const KeytrackNotes* notes_;
      const Output* midi_offset_;
      const Output* bent_midi_;
      const Output* current_note_;

      SynthModule* aliases_[kMaxVoices] = {};
      Voice voices_[kMaxVoices];
      float primary_gain_ = 1.0f;
      float primary_start_gain_ = 1.0f;
      cr::Output keytrack_outputs_[kMaxVoices];
      cr::Output midi_outputs_[kMaxVoices];
      std::vector<Gate> gates_;

      JUCE_LEAK_DETECTOR(KeytrackBank)
  };
} // namespace vital
