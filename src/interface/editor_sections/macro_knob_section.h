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

#include "JuceHeader.h"
#include "synth_section.h"
#include "synth_constants.h"

class MacroLabel;
class SingleMacroSection;

class MacroKnobSection : public SynthSection {
  public:
    // Shows macros first_macro .. first_macro + num_macros - 1.
    MacroKnobSection(String name, int first_macro, int num_macros);
    ~MacroKnobSection();

    void paintBackground(Graphics& g) override;
    void resized() override;

  private:
    std::vector<std::unique_ptr<SingleMacroSection>> macros_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroKnobSection)
};

