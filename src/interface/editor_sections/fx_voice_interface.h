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

#include "filter_section.h"
#include "synth_section.h"

// VOICE tab of the effect build: the two filters, side by side, where the synth shows them under its
// oscillators. There are no oscillators or sample source to route, so those buttons are hidden; the
// serial buttons (FIL1/FIL2) still choose between parallel and serial routing.
class FxVoiceInterface : public SynthSection, public FilterSection::Listener {
  public:
    FxVoiceInterface(const vital::output_map& mono_modulations, const vital::output_map& poly_modulations) :
        SynthSection("synthesis") {
      filter_section_1_ = std::make_unique<FilterSection>(1, mono_modulations, poly_modulations);
      filter_section_1_->hideSourceInputs();
      filter_section_1_->addListener(this);
      addSubSection(filter_section_1_.get());

      filter_section_2_ = std::make_unique<FilterSection>(2, mono_modulations, poly_modulations);
      filter_section_2_->hideSourceInputs();
      filter_section_2_->addListener(this);
      addSubSection(filter_section_2_.get());

      setOpaque(false);
    }

    virtual ~FxVoiceInterface() { }

    void paintBackground(Graphics& g) override { paintChildrenBackgrounds(g); }

    void resized() override {
      int padding = getPadding();
      int width_left = (getWidth() - padding) / 2;
      int width_right = getWidth() - padding - width_left;

      filter_section_1_->setBounds(0, 0, width_left, getHeight());
      filter_section_2_->setBounds(width_left + padding, 0, width_right, getHeight());
      SynthSection::resized();
    }

    void setFocus() { grabKeyboardFocus(); }

    void filterSerialSelected(FilterSection* section) override {
      if (section == filter_section_1_.get())
        filter_section_2_->clearFilterInput();
      else
        filter_section_1_->clearFilterInput();
    }

    void oscInputToggled(FilterSection* section, int index, bool on) override { }
    void sampleInputToggled(FilterSection* section, bool on) override { }

    FilterSection* getFilterSection1() { return filter_section_1_.get(); }
    FilterSection* getFilterSection2() { return filter_section_2_.get(); }

  private:
    std::unique_ptr<FilterSection> filter_section_1_;
    std::unique_ptr<FilterSection> filter_section_2_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxVoiceInterface)
};
