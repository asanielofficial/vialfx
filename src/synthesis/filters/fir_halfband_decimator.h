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
#include "synth_constants.h"

namespace vial {

  class FirHalfbandDecimator : public Processor {
    public:
      static constexpr int kNumTaps = 32;

      enum {
        kAudio,
        kNumInputs
      };

      FirHalfbandDecimator();
      virtual ~FirHalfbandDecimator() { }

      virtual Processor* clone() const override { return new FirHalfbandDecimator(*this); }

      void saveMemory(int num_samples);
      virtual void process(int num_samples) override;

    private:
      void reset(poly_mask reset_mask) override;

      poly_float memory_[kNumTaps / 2 - 1];
      poly_float taps_[kNumTaps / 2];

      JUCE_LEAK_DETECTOR(FirHalfbandDecimator)
  };
} // namespace vial

