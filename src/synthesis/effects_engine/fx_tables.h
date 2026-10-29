/* Copyright 2013-2019 Matt Tytel
 *
 * This file is part of VialFX, which is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vital {
  // Built-in wavetables for the two "table" modes. A table is a row of frames and the amount knob scrubs through
  // them, interpolating between neighbouring frames (frame 0 is neutral, so a knob at zero changes nothing).
  //
  //  Distort Table: every frame is a transfer curve y = f(x) over x in -1..1 (kPoints samples). The signal, taken
  //  relative to its own level, is run through the curve.
  //  Spectral Table: every frame is a magnitude response in dB over log frequency (20 Hz .. 20 kHz, kPoints
  //  samples), applied to the spectrum like a filter.
  class FxTables {
    public:
      static constexpr int kFrames = 16;
      static constexpr int kPoints = 1024;
      static constexpr float kMinHz = 20.0f;
      static constexpr float kOctaves = 9.9658f;   // log2(20000 / 20)

      static const char* distortFrameName(int frame) {
        static const char* names[kFrames] = {
          "Clean", "Soft", "Hard", "Fold", "Double Fold", "Sine Fold", "Cheby 3", "Cheby 2", "Cheby 5",
          "Stairs", "Diode", "Rectify", "Deep Fold", "Crush", "Ripple", "Chaos" };
        return names[std::max(0, std::min(frame, kFrames - 1))];
      }

      static const char* spectralFrameName(int frame) {
        static const char* names[kFrames] = {
          "Flat", "Sub", "Tilt", "Oo", "Oh", "Ah", "Eh", "Ee",
          "Band", "Combs", "Notch", "High Pass", "Telephone", "Scoop", "Ripple", "Chaos" };
        return names[std::max(0, std::min(frame, kFrames - 1))];
      }

      // Transfer curve value for x (any level; clamped to +-1) at table position 0..1.
      static float distort(float x, float position) {
        const Tables& t = tables();
        float p = std::max(0.0f, std::min(position, 1.0f)) * (kFrames - 1);
        int frame = std::min(static_cast<int>(p), kFrames - 2);
        float blend = p - frame;
        float u = (std::max(-1.0f, std::min(x, 1.0f)) * 0.5f + 0.5f) * (kPoints - 1);
        int i = std::min(static_cast<int>(u), kPoints - 2);
        float f = u - i;
        const float* a = &t.distort[frame * kPoints];
        const float* b = &t.distort[(frame + 1) * kPoints];
        float ya = a[i] + f * (a[i + 1] - a[i]);
        float yb = b[i] + f * (b[i + 1] - b[i]);
        return ya + blend * (yb - ya);
      }

      // Gain in dB at log2(f / 20 Hz) = octave, at table position 0..1.
      static float spectralDb(float octave, float position) {
        const Tables& t = tables();
        float p = std::max(0.0f, std::min(position, 1.0f)) * (kFrames - 1);
        int frame = std::min(static_cast<int>(p), kFrames - 2);
        float blend = p - frame;
        float u = std::max(0.0f, std::min(octave / kOctaves, 1.0f)) * (kPoints - 1);
        int i = std::min(static_cast<int>(u), kPoints - 2);
        float f = u - i;
        const float* a = &t.spectral[frame * kPoints];
        const float* b = &t.spectral[(frame + 1) * kPoints];
        float ya = a[i] + f * (a[i + 1] - a[i]);
        float yb = b[i] + f * (b[i + 1] - b[i]);
        return ya + blend * (yb - ya);
      }

    private:
      struct Tables {
        std::vector<float> distort;
        std::vector<float> spectral;
      };

      static float fold(float u) {
        float w = u * 0.25f + 0.25f;
        w -= std::floor(w);
        return 1.0f - 4.0f * std::fabs(w - 0.5f);
      }

      static float bump(float octave, float hz, float width_octaves, float height_db) {
        float d = (octave - std::log2(hz / kMinHz)) / width_octaves;
        return height_db * std::exp(-0.5f * d * d);
      }

      static float distortFrame(int frame, float x) {
        const float pi = 3.14159265f;
        switch (frame) {
          case 0: return x;
          case 1: return std::tanh(2.0f * x) / std::tanh(2.0f);
          case 2: return std::max(-1.0f, std::min(3.0f * x, 1.0f));
          case 3: return fold(2.0f * x);
          case 4: return fold(4.0f * x);
          case 5: return std::sin(1.5f * pi * x);
          case 6: return 4.0f * x * x * x - 3.0f * x;
          case 7: return std::max(-1.0f, std::min(x + 0.8f * x * x, 1.0f));
          case 8: return 16.0f * std::pow(x, 5.0f) - 20.0f * std::pow(x, 3.0f) + 5.0f * x;
          case 9: return std::round(x * 4.0f) / 4.0f;
          case 10: return x > 0.0f ? std::tanh(3.0f * x) : 0.25f * x;
          case 11: return std::max(-1.0f, std::min(0.4f * x + std::fabs(x), 1.0f));
          case 12: return std::sin(4.0f * pi * x + 2.0f * std::sin(5.0f * pi * x));
          case 13: return std::copysign(std::pow(std::fabs(x), 0.3f), x);
          case 14: return 0.6f * x + 0.4f * std::sin(10.0f * pi * x);
          default: return std::sin(9.0f * x + 3.0f * x * x * x) * (0.5f + 0.5f * std::fabs(x));
        }
      }

      static float spectralFrame(int frame, float o) {
        const float pi = 3.14159265f;
        float hz = kMinHz * std::exp2(o);
        float db = 0.0f;
        switch (frame) {
          case 0: db = 0.0f; break;
          case 1: db = hz < 400.0f ? 0.0f : -24.0f * std::log2(hz / 400.0f); break;
          case 2: db = 9.0f - 18.0f * std::max(0.0f, std::min((o - 3.0f) / 5.0f, 1.0f)); break;
          case 3: db = -24.0f + bump(o, 300.0f, 0.25f, 42.0f) + bump(o, 870.0f, 0.25f, 36.0f); break;
          case 4: db = -24.0f + bump(o, 500.0f, 0.25f, 42.0f) + bump(o, 1000.0f, 0.25f, 36.0f); break;
          case 5: db = -24.0f + bump(o, 700.0f, 0.25f, 42.0f) + bump(o, 1220.0f, 0.25f, 38.0f) +
                       bump(o, 2600.0f, 0.3f, 30.0f); break;
          case 6: db = -24.0f + bump(o, 530.0f, 0.25f, 42.0f) + bump(o, 1840.0f, 0.25f, 38.0f) +
                       bump(o, 2480.0f, 0.3f, 30.0f); break;
          case 7: db = -24.0f + bump(o, 270.0f, 0.25f, 42.0f) + bump(o, 2290.0f, 0.25f, 40.0f) +
                       bump(o, 3010.0f, 0.3f, 30.0f); break;
          case 8: db = -36.0f + bump(o, 1000.0f, 0.2f, 60.0f); break;
          case 9: db = 18.0f * std::cos(2.0f * pi * 3.0f * o); break;
          case 10: db = -bump(o, 1000.0f, 0.3f, 36.0f); break;
          case 11: db = hz > 2000.0f ? 0.0f : -24.0f * std::log2(2000.0f / hz); break;
          case 12: db = -30.0f + bump(o, 1100.0f, 1.0f, 30.0f); break;
          case 13: db = 12.0f - bump(o, 800.0f, 1.0f, 30.0f); break;
          case 14: db = 18.0f * std::sin(2.0f * pi * 2.5f * o) * (0.6f + 0.4f * std::sin(0.7f * o)); break;
          default: db = 14.0f * std::sin(2.0f * pi * 1.7f * o + 0.5f) + 12.0f * std::sin(2.0f * pi * 5.3f * o + 1.3f); break;
        }
        return std::max(-48.0f, std::min(db, 48.0f));
      }

      static const Tables& tables() {
        static const Tables built = [] {
          Tables t;
          t.distort.resize(kFrames * kPoints);
          t.spectral.resize(kFrames * kPoints);
          for (int frame = 0; frame < kFrames; ++frame) {
            for (int i = 0; i < kPoints; ++i) {
              float x = 2.0f * i / (kPoints - 1) - 1.0f;
              t.distort[frame * kPoints + i] = std::max(-1.0f, std::min(distortFrame(frame, x), 1.0f));
              float o = kOctaves * i / (kPoints - 1);
              t.spectral[frame * kPoints + i] = spectralFrame(frame, o);
            }
          }
          return t;
        }();
        return built;
      }
  };
} // namespace vital
