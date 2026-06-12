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

#include <JuceHeader.h>
#include "json/json.h"
#include "synth_constants.h"

using json = nlohmann::json;

class WavetableComponent;

namespace vial {
  class WaveFrame;
} // namespace vial

class WavetableKeyframe {
  public:
    static float linearTween(float point_from, float point_to, float t);
    static float cubicTween(float point_prev, float point_from, float point_to, float point_next,
                            float range_prev, float range, float range_next, float t);

    WavetableKeyframe() : position_(0), owner_(nullptr) { }
    virtual ~WavetableKeyframe() { }

    int index();
    int position() const { return position_; }
    void setPosition(int position) { 
      VITAL_ASSERT(position >= 0 && position < vial::kNumOscillatorWaveFrames);
      position_ = position;
    }

    virtual void copy(const WavetableKeyframe* keyframe) = 0;
    virtual void interpolate(const WavetableKeyframe* from_keyframe,
                             const WavetableKeyframe* to_keyframe, float t) = 0;
    virtual void smoothInterpolate(const WavetableKeyframe* prev_keyframe,
                                   const WavetableKeyframe* from_keyframe,
                                   const WavetableKeyframe* to_keyframe,
                                   const WavetableKeyframe* next_keyframe, float t) { }

    virtual void render(vial::WaveFrame* wave_frame) = 0;
    virtual json stateToJson();
    virtual void jsonToState(json data);

    WavetableComponent* owner() { return owner_; }
    void setOwner(WavetableComponent* owner) { owner_ = owner; }

  protected:
    int position_;
    WavetableComponent* owner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableKeyframe)
};

