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

#include "synth_constants.h"

namespace vial {
  class CombFilter;
  
  class CombModule : public SynthModule {
    public:
      static constexpr int kMaxFeedbackSamples = 25000;

      enum {
        kAudio,
        kReset,
        kMidiCutoff,
        kMidiBlendTranspose,
        kFilterCutoffBlend,
        kStyle,
        kResonance,
        kMidi,
        kNumInputs
      };

      CombModule();
      virtual ~CombModule() { }

      void init() override;
      void reset(poly_mask reset_mask) override;
      void hardReset() override;
      virtual Processor* clone() const override { return new CombModule(*this); }

    protected:
      CombFilter* comb_filter_;

    JUCE_LEAK_DETECTOR(CombModule)
  };
} // namespace vial

