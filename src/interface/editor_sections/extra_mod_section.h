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
#include "modulation_tab_selector.h"
#include "synth_button.h"

class LineMapEditor;
class MacroKnobSection;
struct SynthGuiData;

class ExtraModSection : public SynthSection {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void extraModExpansionChanged(bool expanded) = 0;
    };

    ExtraModSection(String name, SynthGuiData* synth_data);
    virtual ~ExtraModSection();

    void setColumnWidth(int width) { column_width_ = width; }
    void addListener(Listener* listener) { listeners_.push_back(listener); }
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }
    // Width of the strip with the expand / retract button, next to the macro columns.
    static int getToggleWidth(int column_width) { return std::max(12, column_width / 6); }
    // Total width of the section for the given width of one macro column.
    int getTotalWidth(int column_width);
    void buttonClicked(Button* clicked_button) override;

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override;
    void resized() override;

  private:
    std::unique_ptr<ModulationTabSelector> other_modulations_;
    std::unique_ptr<MacroKnobSection> macro_knobs_;
    std::unique_ptr<MacroKnobSection> extra_macro_knobs_;
    std::unique_ptr<OpenGlShapeButton> toggle_button_;
    std::vector<Listener*> listeners_;
    bool expanded_ = false;
    int column_width_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExtraModSection)
};

