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

#include "processor.h"

class LineGenerator;

namespace vial {

  class LineMap : public Processor {
    public:
      static constexpr mono_float kMaxPower = 20.0f;

      enum MapOutput {
        kValue,
        kPhase,
        kNumOutputs
      };

      LineMap(LineGenerator* source);

      virtual Processor* clone() const override { return new LineMap(*this); }
      void process(int num_samples) override;
      void process(poly_float phase);

    protected:
      poly_float offset_;
      LineGenerator* source_;

      JUCE_LEAK_DETECTOR(LineMap)
  };
} // namespace vial

