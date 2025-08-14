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
#include "wave_source.h"

class ShepardToneSource : public WaveSource {
  public:
    ShepardToneSource();
    virtual ~ShepardToneSource();

    virtual void render(vial::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual bool hasKeyframes() override { return false; }

  protected: 
    std::unique_ptr<WaveSourceKeyframe> loop_frame_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShepardToneSource)
};