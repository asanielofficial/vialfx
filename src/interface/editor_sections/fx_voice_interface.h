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
#include "fx_input_section.h"
#include "synth_section.h"

// VOICE tab of the effect build: the main input and sidechain input panels on top, and the two filters
// side by side below them, where the synth shows its filters under the oscillators. The filters' source
// buttons are INPUT and SIDECHAIN, and the serial buttons (FIL1/FIL2) still choose between parallel and
// serial routing.
class FxVoiceInterface : public SynthSection, public FilterSection::Listener, public FxInputSection::Listener {
  public:
    FxVoiceInterface(const vital::output_map& mono_modulations, const vital::output_map& poly_modulations) :
        SynthSection("synthesis") {
      main_input_ = std::make_unique<FxInputSection>("INPUT", "input");
      main_input_->addListener(this);
      addSubSection(main_input_.get());

      sidechain_input_ = std::make_unique<FxInputSection>("SIDECHAIN", "sidechain");
      sidechain_input_->addListener(this);
      addSubSection(sidechain_input_.get());

      filter_section_1_ = std::make_unique<FilterSection>(1, mono_modulations, poly_modulations);
      filter_section_1_->useAudioInputs();
      filter_section_1_->addListener(this);
      addSubSection(filter_section_1_.get());

      filter_section_2_ = std::make_unique<FilterSection>(2, mono_modulations, poly_modulations);
      filter_section_2_->useAudioInputs();
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

      // Input panels are as tall as the synth's oscillator panels; the filters get what is left.
      int panel_margin = main_input_->findValue(Skin::kWidgetMargin);
      int panel_height = 2 * (int)main_input_->getKnobSectionHeight() - panel_margin;
      int inputs_height = 2 * panel_height + padding;
      if (inputs_height > getHeight() / 2) {
        panel_height = (getHeight() / 2 - padding) / 2;
        inputs_height = 2 * panel_height + padding;
      }

      main_input_->setBounds(0, 0, getWidth(), panel_height);
      sidechain_input_->setBounds(0, panel_height + padding, getWidth(), panel_height);

      int filter_y = inputs_height + padding;
      int filter_height = getHeight() - filter_y;
      filter_section_1_->setBounds(0, filter_y, width_left, filter_height);
      filter_section_2_->setBounds(width_left + padding, filter_y, width_right, filter_height);
      SynthSection::resized();
    }

    void setFocus() { grabKeyboardFocus(); }

    void filterSerialSelected(FilterSection* section) override {
      if (section == filter_section_1_.get())
        filter_section_2_->clearFilterInput();
      else
        filter_section_1_->clearFilterInput();
    }

    // The filters' INPUT (index 0) and SIDECHAIN (index 1) buttons edit the matching input's destination.
    void oscInputToggled(FilterSection* section, int index, bool on) override {
      int filter_index = section == filter_section_1_.get() ? 0 : 1;
      FxInputSection* input = index == 0 ? main_input_.get() : sidechain_input_.get();
      input->toggleFilterInput(filter_index, on);
    }

    void sampleInputToggled(FilterSection* section, bool on) override { }

    // ...and the destination dropdowns light the filters' buttons.
    void inputDestinationChanged(FxInputSection* section, int destination) override {
      bool filter1_on = destination == vital::constants::kFilter1 || destination == vital::constants::kDualFilters;
      bool filter2_on = destination == vital::constants::kFilter2 || destination == vital::constants::kDualFilters;
      int index = section == main_input_.get() ? 0 : 1;
      filter_section_1_->setOscillatorInput(index, filter1_on);
      filter_section_2_->setOscillatorInput(index, filter2_on);
    }

    FilterSection* getFilterSection1() { return filter_section_1_.get(); }
    FilterSection* getFilterSection2() { return filter_section_2_.get(); }

  private:
    std::unique_ptr<FxInputSection> main_input_;
    std::unique_ptr<FxInputSection> sidechain_input_;
    std::unique_ptr<FilterSection> filter_section_1_;
    std::unique_ptr<FilterSection> filter_section_2_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxVoiceInterface)
};
