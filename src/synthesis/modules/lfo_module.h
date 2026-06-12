/* Copyright 2013-2019 Matt Tytel
 *
 * vial is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vial is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vial.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "synth_module.h"

class LineGenerator;

namespace vial {
  class SynthLfo;

  class LfoModule : public SynthModule {
    public:
      enum {
        kNoteTrigger,
        kNoteCount,
        kMidi,
        kNumInputs
      };

      enum {
        kValue,
        kOscPhase,
        kOscFrequency,
        kNumOutputs
      };

      LfoModule(const std::string& prefix, LineGenerator* line_generator, const Output* beats_per_second);
      virtual ~LfoModule() { }

      void init() override;
      virtual Processor* clone() const override { return new LfoModule(*this); }
      void correctToTime(double seconds) override;
      void setControlRate(bool control_rate) override;

    protected:
      std::string prefix_;
      SynthLfo* lfo_;
      const Output* beats_per_second_;

      JUCE_LEAK_DETECTOR(LfoModule)
  };
} // namespace vial

