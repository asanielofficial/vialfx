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

#include <functional>

#include "filter_section.h"
#include "fx_input_section.h"
#include "spectral_warp_editor.h"
#include "table_settings_editor.h"
#include "synth_section.h"

// VOICE tab of the effect build: the main input and sidechain input panels on top, and the two filters
// side by side below them, where the synth shows its filters under the oscillators. The filters' source
// buttons are INPUT and SIDECHAIN, and the serial buttons (FIL1/FIL2) still choose between parallel and
// serial routing.
class FxVoiceInterface : public SynthSection, public FilterSection::Listener, public FxInputSection::Listener {
  public:
    // Called with the wavetable slot to edit when a table mode's pencil is clicked (see FxInputSection).
    void setTableEditorCallback(std::function<void(int)> callback) { table_editor_callback_ = callback; }
    void setTableImportCallback(
        std::function<bool(int, String, InputStream*, WavetableCreator::AudioFileLoadStyle)> callback) {
      table_import_callback_ = callback;
    }

    FxVoiceInterface(const vital::output_map& mono_modulations, const vital::output_map& poly_modulations,
                     vital::SoundEngine* engine) : SynthSection("synthesis") {
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

      // The curve editor for Spectral Filter / Phaser / Flanger docks over the filters (below the input panels, so
      // the lane panels and the modulation sources stay usable while it is open).
      spectral_editor_ = std::make_unique<SpectralWarpEditor>(engine);
      addSubSection(spectral_editor_.get());
      spectral_editor_->setVisible(false);
      spectral_editor_->setVisibilityCallback([this](bool shown) { panelVisibilityChanged(spectral_editor_.get(), shown); });
      vocoder_editor_ = std::make_unique<VocoderEditor>();
      addSubSection(vocoder_editor_.get());
      vocoder_editor_->setVisible(false);
      vocoder_editor_->setVisibilityCallback([this](bool shown) { panelVisibilityChanged(vocoder_editor_.get(), shown); });
      main_input_->setVocoderEditor(vocoder_editor_.get());
      for (FxInputSection* input : { main_input_.get(), sidechain_input_.get() })
        input->setTableEditorCallback([this](int index) { if (table_editor_callback_) table_editor_callback_(index); });
      // The table modes' settings menus (wavetable display and fine controls), one per lane and kind.
      for (int slot = 0; slot < 4; ++slot) {
        bool spectral = slot >= 2;
        std::string prefix = slot % 2 == 0 ? "input" : "sidechain";
        table_menus_[slot] = std::make_unique<TableSettingsEditor>(
            spectral ? TableSettingsEditor::kSpectral : TableSettingsEditor::kDistort, prefix, slot,
            mono_modulations, poly_modulations);
        addSubSection(table_menus_[slot].get());
        table_menus_[slot]->setVisible(false);
        TableSettingsEditor* menu = table_menus_[slot].get();
        menu->setVisibilityCallback([this, menu](bool shown) { panelVisibilityChanged(menu, shown); });
        FxInputSection* input = slot % 2 == 0 ? main_input_.get() : sidechain_input_.get();
        menu->setImportCallback([this, slot](String name, InputStream* stream, WavetableCreator::AudioFileLoadStyle style) {
          return table_import_callback_ ? table_import_callback_(slot, name, stream, style) : false;
        });
        table_menus_[slot]->setFrameSlider(spectral ? input->getSpectralSlider() : input->getDistortionSlider());
      }
      for (FxInputSection* input : { main_input_.get(), sidechain_input_.get() })
        input->setTableSettingsCallback([this](int slot) { showTableMenu(slot); });
      main_input_->setSpectralEditor(spectral_editor_.get());
      sidechain_input_->setSpectralEditor(spectral_editor_.get());

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

      filter_y_ = inputs_height + padding;
      filter_height_ = getHeight() - filter_y_;
      width_left_ = width_left;
      width_right_ = width_right;
      layoutPanels();
      SynthSection::resized();
    }


    // The spectral-side panels (curve editor, Vocoder menu, Spectral Table menus) and the Distort Table menus are
    // docked at the top of the filter area, compressed so the two filters stay visible underneath. One panel of each
    // group at a time; with one of each open they split the width like the two filters do (spectral on the left,
    // Distort Table on the right).
    static constexpr float kPanelHeightRatio = 0.62f;

    bool isSpectralPanel(SynthSection* panel) const {
      return panel == spectral_editor_.get() || panel == vocoder_editor_.get() ||
             panel == table_menus_[2].get() || panel == table_menus_[3].get();
    }

    void panelVisibilityChanged(SynthSection* panel, bool shown) {
      if (in_layout_)
        return;
      in_layout_ = true;
      if (shown) {
        std::vector<SynthSection*> all = { spectral_editor_.get(), vocoder_editor_.get(), table_menus_[0].get(),
                                           table_menus_[1].get(), table_menus_[2].get(), table_menus_[3].get() };
        for (SynthSection* other : all) {
          if (other != panel && other->isVisible() && isSpectralPanel(other) == isSpectralPanel(panel))
            other->setVisible(false);
        }
      }
      in_layout_ = false;
      layoutPanels();
    }

    SynthSection* visiblePanel(bool spectral) {
      std::vector<SynthSection*> all = { spectral_editor_.get(), vocoder_editor_.get(), table_menus_[0].get(),
                                         table_menus_[1].get(), table_menus_[2].get(), table_menus_[3].get() };
      for (SynthSection* panel : all) {
        if (panel && panel->isVisible() && isSpectralPanel(panel) == spectral)
          return panel;
      }
      return nullptr;
    }

    void layoutPanels() {
      if (filter_height_ <= 0 || getWidth() <= 0)
        return;

      int padding = getPadding();
      SynthSection* spectral = visiblePanel(true);
      SynthSection* distort = visiblePanel(false);
      bool any = spectral || distort;

      int panel_height = any ? static_cast<int>(filter_height_ * kPanelHeightRatio) : 0;
      int filters_y = any ? filter_y_ + panel_height + padding : filter_y_;
      int filters_height = getHeight() - filters_y;
      filter_section_1_->setBounds(0, filters_y, width_left_, filters_height);
      filter_section_2_->setBounds(width_left_ + padding, filters_y, width_right_, filters_height);

      std::vector<SynthSection*> all = { spectral_editor_.get(), vocoder_editor_.get(), table_menus_[0].get(),
                                         table_menus_[1].get(), table_menus_[2].get(), table_menus_[3].get() };
      for (SynthSection* panel : all) {
        if (panel == nullptr)
          continue;
        if (panel == spectral && distort)
          panel->setBounds(0, filter_y_, width_left_, panel_height);
        else if (panel == distort && spectral)
          panel->setBounds(width_left_ + padding, filter_y_, width_right_, panel_height);
        else
          panel->setBounds(0, filter_y_, getWidth(), std::max(panel_height, 1));
      }

      // Whatever the panels covered or uncovered has to be redrawn (the backgrounds are cached).
      if (filter_section_1_->isShowing())
        filter_section_1_->repaintBackground();
    }

    void setFocus() { grabKeyboardFocus(); }

    // The settings menu of a table slot (0 / 1 the input / sidechain Distort Table, 2 / 3 the Spectral Tables).
    void showTableMenu(int slot) {
      if (slot >= 0 && slot < 4 && table_menus_[slot])
        table_menus_[slot]->setVisible(true);
    }

    // The table of this slot was edited in the wavetable editor: the menu's display redraws it.
    void tableEdited(int slot) {
      if (slot >= 0 && slot < 4 && table_menus_[slot])
        table_menus_[slot]->refreshTable();
    }

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
    std::unique_ptr<SpectralWarpEditor> spectral_editor_;
    std::function<void(int)> table_editor_callback_;
    std::function<bool(int, String, InputStream*, WavetableCreator::AudioFileLoadStyle)> table_import_callback_;
    int filter_y_ = 0, filter_height_ = 0, width_left_ = 0, width_right_ = 0;
    bool in_layout_ = false;
    std::unique_ptr<VocoderEditor> vocoder_editor_;
    std::unique_ptr<TableSettingsEditor> table_menus_[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxVoiceInterface)
};
