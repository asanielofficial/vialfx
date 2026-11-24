// VialFX - MIDI / MPE note tracker for the Resonator. Licensed under GPL-3.0-or-later.
#pragma once

#include <cstdint>

namespace vital {

  // Keeps the notes the Resonator should ring at. The engine forwards its MIDI events here (audio thread only), the
  // Resonator reads them once per block. Each entry is one note on one channel with its own bend, slide (CC74) and
  // pressure, so a MPE keyboard gives every finger its own pitch and expression.
  class ResonatorMidi {
    public:
      static constexpr int kChannels = 16;
      static constexpr int kEntries = 24;

      struct Entry {
        bool used = false;
        bool held = false;
        int note = 0;
        int channel = 0;
        float velocity = 0.0f;
        float poly_pressure = 0.0f;
        uint64_t order = 0;
        int id = 0;
      };

      ResonatorMidi() { reset(); }

      void reset() {
        for (auto& e : entries_) e = Entry();
        for (int c = 0; c < kChannels; ++c) { bend_[c] = 0.0f; slide_[c] = 0.0f; pressure_[c] = 0.0f; }
        zone_bend_ = 0.0f;
        counter_ = 0;
        next_id_ = 1;
      }

      void noteOn(int note, float velocity, int channel) {
        channel = clampChannel(channel);
        Entry* e = find(note, channel);
        if (!e) e = allocate();
        e->used = true;
        e->held = true;
        e->note = note;
        e->channel = channel;
        e->velocity = velocity;
        e->poly_pressure = 0.0f;
        e->order = ++counter_;
        e->id = next_id_++;
      }

      void noteOff(int note, int channel) {
        Entry* e = find(note, clampChannel(channel));
        if (e) e->held = false;
      }

      void allNotesOff(int channel) {
        for (auto& e : entries_)
          if (e.used && e.channel == channel) e.held = false;
      }

      void allNotesOffRange(int from, int to) {
        for (auto& e : entries_)
          if (e.used && e.channel >= from && e.channel <= to) e.held = false;
      }

      void allSoundsOff() { for (auto& e : entries_) e.held = false; }

      void setPitchWheel(float value, int channel) { bend_[clampChannel(channel)] = value; }

      // A range of one channel is a plain bend; a wider range is the MPE master channel's bend for every member.
      void setZonedPitchWheel(float value, int from, int to) {
        if (from == to) bend_[clampChannel(from)] = value;
        else if (to - from > 1) zone_bend_ = value;
      }

      void setChannelSlide(int channel, float value) { slide_[clampChannel(channel)] = value; }
      void setChannelPressure(int channel, float value) { pressure_[clampChannel(channel)] = value; }

      void setPolyPressure(int note, int channel, float value) {
        Entry* e = find(note, clampChannel(channel));
        if (e) e->poly_pressure = value;
      }

      // The notes to sound: the newest held ones (at most max_count, newest first). Nothing held: the newest released
      // note keeps ringing, like a filter's key tracking holding the last note. Returns how many entries were found.
      int collect(int max_count, const Entry** out) const {
        int count = 0;
        for (int pass = 0; pass < max_count; ++pass) {
          const Entry* best = nullptr;
          for (const auto& e : entries_) {
            if (!e.used || !e.held) continue;
            bool taken = false;
            for (int i = 0; i < count; ++i) taken = taken || out[i] == &e;
            if (taken) continue;
            if (!best || e.order > best->order) best = &e;
          }
          if (!best) break;
          out[count++] = best;
        }
        if (count == 0) {
          const Entry* best = nullptr;
          for (const auto& e : entries_)
            if (e.used && (!best || e.order > best->order)) best = &e;
          if (best) out[count++] = best;
        }
        return count;
      }

      float bend(int channel) const { return bend_[clampChannel(channel)]; }
      float zoneBend() const { return zone_bend_; }
      float slide(int channel) const { return slide_[clampChannel(channel)]; }
      float pressure(int channel) const { return pressure_[clampChannel(channel)]; }

    private:
      static int clampChannel(int c) { return c < 0 ? 0 : (c >= kChannels ? kChannels - 1 : c); }

      Entry* find(int note, int channel) {
        for (auto& e : entries_)
          if (e.used && e.note == note && e.channel == channel) return &e;
        return nullptr;
      }

      Entry* allocate() {
        Entry* oldest = &entries_[0];
        for (auto& e : entries_) {
          if (!e.used) return &e;
          bool better = (!e.held && oldest->held) || (e.held == oldest->held && e.order < oldest->order);
          if (better) oldest = &e;
        }
        return oldest;
      }

      Entry entries_[kEntries];
      float bend_[kChannels], slide_[kChannels], pressure_[kChannels];
      float zone_bend_;
      uint64_t counter_;
      int next_id_;
  };
} // namespace vital
