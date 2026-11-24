/* Copyright 2013-2019 Matt Tytel
 *
 * This file is part of VialFX, which is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

#include "load_save.h"
#include "open_gl_image_component.h"
#include "paths.h"
#include "preset_selector.h"
#include "synth_button.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "wavetable_3d.h"

#include <functional>

// What the table menus need from the main interface to change or save the wavetable of a slot (implemented by
// FullInterface, found through the parent chain, so this header does not depend on it).
class TableFileHost {
  public:
    virtual ~TableFileHost() { }
    virtual void tableLoadFile(int slot, const File& file) = 0;
    virtual std::string tableName(int slot) = 0;
    virtual void tableSave(int slot) = 0;
};

// The settings menu of a table mode (Spectral Table or Distort Table of one lane), docked over the filters like the
// Vocoder menu. The wavetable of the mode is shown large (the Vital wavetable display, with its 3D / 2D / SP button)
// and follows the mode's amount knob; the fine controls are along the bottom:
//   Spectral Table: gain, pitch, resonance, mix.
//   Distort Table:  phase, bias, smooth, drive, DC filter, mix.
class TableSettingsEditor : public SynthSection, public Wavetable3d::Listener, public PresetSelector::Listener {
  public:
    enum Kind { kSpectral, kDistort };
    static constexpr int kMaxKnobs = 5;

    // slot is the wavetable slot of the mode: 0 / 1 the input / sidechain Distort Table, 2 / 3 the Spectral Tables
    // (slots 4 and up belong to the Morph effect).
    // The Morph effect's copies pass their own wavetable slot (4 + 2n / 5 + 2n), prefix ("morph", "morph_2" ...) and
    // subtitle (the title says which copy).
    TableSettingsEditor(Kind kind, const std::string& prefix, int slot,
                        const vital::output_map& mono_modulations, const vital::output_map& poly_modulations,
                        const std::string& subtitle = "") :
        SynthSection("table_settings_" + prefix + (kind == kSpectral ? "_spectral" : "_distort")), kind_(kind),
        slot_(slot) {
      struct KnobInfo {
        std::string parameter;
        const char* label;
        const char* prefix;
        bool bipolar;
      };
      std::vector<KnobInfo> knobs;
      if (kind == kSpectral) {
        knobs = { { prefix + "_table_gain", "GAIN", "Gain: ", false },
                  { prefix + "_table_position", "PITCH", "Pitch: ", false },
                  { prefix + "_table_resonance", "RESONANCE", "Resonance: ", false },
                  { prefix + "_spectral_table_mix", "MIX", "Mix: ", false } };
        title_ = "SPECTRAL TABLE";
      }
      else {
        knobs = { { prefix + "_distortion_phase", "PHASE", "Phase: ", false },
                  { prefix + "_distortion_bias", "BIAS", "Bias: ", true },
                  { prefix + "_distortion_smooth", "SMOOTH", "Smooth: ", false },
                  { prefix + "_distortion_drive", "DRIVE", "Drive: ", false },
                  { prefix + "_distortion_mix", "MIX", "Mix: ", false } };
        title_ = "DISTORT TABLE";
      }
      title_ += "  -  " + (!subtitle.empty() ? subtitle : std::string(prefix == "input" ? "INPUT" : "SIDECHAIN"));

      num_knobs_ = static_cast<int>(knobs.size());
      for (int i = 0; i < num_knobs_; ++i) {
        knobs_[i] = std::make_unique<SynthSlider>(knobs[i].parameter);
        addSlider(knobs_[i].get());
        knobs_[i]->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knobs_[i]->setPopupPrefix(knobs[i].prefix);
        knobs_[i]->setBipolar(knobs[i].bipolar);
        labels_[i] = knobs[i].label;
      }

      if (kind == kDistort) {
        dc_filter_ = std::make_unique<SynthButton>(prefix + "_distortion_dc_filter");
        addButton(dc_filter_.get());
        dc_filter_->setText("DC FILTER");
        dc_filter_->setLookAndFeel(TextLookAndFeel::instance());
      }

      wavetable_ = std::make_unique<Wavetable3d>(slot, mono_modulations, poly_modulations);
      addOpenGlComponent(wavetable_.get());
      wavetable_->setRenderType(Wavetable3d::kWave3d);
      wavetable_->addListener(this);
      // The display follows the lane's amount knob, modulation included.
      wavetable_->setFrameModulation(mono_modulations, prefix + (kind == kSpectral ? "_spectral_morph_amount"
                                                                                  : "_distortion_amount"));

      dimension_button_ = std::make_unique<OpenGlToggleButton>("dimension");
      addAndMakeVisible(dimension_button_.get());
      addOpenGlComponent(dimension_button_->getGlComponent());
      dimension_button_->addListener(this);
      dimension_button_->setNoBackground();
      dimension_button_->setLookAndFeel(TextLookAndFeel::instance());
      dimension_button_->setText(strings::kWavetableDimensionNames[0]);

      close_button_ = std::make_unique<OpenGlShapeButton>("close");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(Paths::exitX());

      // The wavetable bar of Vital's oscillator: previous / next arrows around the name, a click on the name opens the
      // wavetable browser (Vital's own, with the user's wavetable folders), and SAVE writes the table to a file.
      name_bar_ = std::make_unique<PresetSelector>();
      addSubSection(name_bar_.get());
      name_bar_->addListener(this);

      save_button_ = std::make_unique<OpenGlToggleButton>("save_table");
      addAndMakeVisible(save_button_.get());
      addOpenGlComponent(save_button_->getGlComponent());
      save_button_->addListener(this);
      save_button_->setNoBackground();
      save_button_->setLookAndFeel(TextLookAndFeel::instance());
      save_button_->setText("SAVE");

      setSkinOverride(Skin::kLfo);
    }

    virtual ~TableSettingsEditor() = default;

    // The lane's amount knob (the table position) drives which frame is shown, and dragging the display moves it.
    void setFrameSlider(SynthSlider* slider) { wavetable_->setFrameSlider(slider); }

    // Called when the menu is shown or hidden (the filters underneath hide while it is open).
    void setVisibilityCallback(std::function<void(bool)> callback) { visibility_callback_ = callback; }

    // Audio files dropped on the display become the table (whole-file splice, vocode or pitch splice, by drop zone).
    void setImportCallback(std::function<bool(String, InputStream*, WavetableCreator::AudioFileLoadStyle)> callback) {
      import_callback_ = callback;
    }

    bool loadAudioAsWavetable(String name, InputStream* audio_stream,
                              WavetableCreator::AudioFileLoadStyle style) override {
      if (!import_callback_) {
        delete audio_stream;
        return false;
      }
      bool loaded = import_callback_(name, audio_stream, style);
      refreshTable();
      return loaded;
    }

    // The rest of the display's interface belongs to its right-click menu, which the effect build does not show.
    void loadWavetable(json& wavetable_data) override { }
    void loadDefaultWavetable() override { }
    void resynthesizeToWavetable() override { }
    void textToWavetable() override { }
    void saveWavetable() override { }

    // The table was edited elsewhere: redraw it.
    void refreshTable() {
      updateName();
      wavetable_->setDirty();
      repaintBackground();
    }

    void updateName() {
      if (TableFileHost* host = findParentComponentOfClass<TableFileHost>()) {
        std::string name = host->tableName(slot_);
        name_bar_->setText(name.empty() ? String("Init") : String(name));
      }
    }

    void prevClicked() override { stepFile(-1); }
    void nextClicked() override { stepFile(1); }

    void textMouseDown(const MouseEvent& e) override {
      static constexpr int kBrowserWidth = 600;
      static constexpr int kBrowserHeight = 400;
      Rectangle<int> bounds(name_bar_->getX(), name_bar_->getY(),
                            static_cast<int>(kBrowserWidth * size_ratio_), static_cast<int>(kBrowserHeight * size_ratio_));
      showPopupBrowser(this, bounds, LoadSave::getWavetableDirectories(), vital::kWavetableExtensionsList,
                       LoadSave::kWavetableFolderName, LoadSave::kAdditionalWavetableFoldersName);
    }

    // The popup browser loads the chosen file into this menu's table.
    void loadFile(const File& file) override {
      TableFileHost* host = findParentComponentOfClass<TableFileHost>();
      if (host == nullptr || !file.exists())
        return;

      current_file_ = file;
      if (file.getFileExtension() == String(".") + vital::kWavetableExtension) {
        host->tableLoadFile(slot_, file);
        refreshTable();
      }
      else
        loadAudioAsWavetable(file.getFileNameWithoutExtension(), new FileInputStream(file),
                             WavetableCreator::kWavetableSplice);
    }

    File getCurrentFile() override { return current_file_; }

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);
      paintKnobShadows(g);

      setLabelFont(g);
      g.setColour(findColour(Skin::kBodyText, true));
      int margin = getWidgetMargin();
      g.drawText(title_, margin * 2, margin, getWidth() - 4 * margin - title_height_, title_height_,
                 Justification::centredLeft);
      for (int i = 0; i < num_knobs_; ++i)
        drawLabelForComponent(g, labels_[i], knobs_[i].get());
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      int margin = getWidgetMargin();
      title_height_ = std::max(1, static_cast<int>(size_ratio_ * 28));
      close_button_->setBounds(getWidth() - margin - title_height_, margin, title_height_, title_height_);

      int top = margin + title_height_ + margin;
      int save_width = static_cast<int>(size_ratio_ * 56);
      int bar_x = margin + static_cast<int>(size_ratio_ * 260);
      int bar_right = close_button_->getX() - margin - save_width - margin;
      if (bar_right - bar_x >= static_cast<int>(size_ratio_ * 150)) {
        // Wide enough (a menu on its own): the bar sits in the title row, after the title.
        name_bar_->setBounds(bar_x, margin, bar_right - bar_x, title_height_);
        save_button_->setBounds(name_bar_->getRight() + margin, margin, save_width, title_height_);
      }
      else {
        // Split view (two menus side by side) leaves no room beside the title: the bar gets its own row below it.
        int row_width = getWidth() - 4 * margin - save_width - margin;
        name_bar_->setBounds(margin * 2, top, std::max(1, row_width), title_height_);
        save_button_->setBounds(name_bar_->getRight() + margin, top, save_width, title_height_);
        top = name_bar_->getBottom() + margin;
      }
      int knob_area_height = std::min(getHeight() / 3, static_cast<int>(size_ratio_ * 120));
      int knob_y = getHeight() - knob_area_height - margin;
      wavetable_->setBounds(margin, top, getWidth() - 2 * margin, std::max(1, knob_y - top - margin));
      int text_height = std::max(1, static_cast<int>(size_ratio_ * 22));
      dimension_button_->setBounds(wavetable_->getX() + margin, wavetable_->getBottom() - text_height - margin,
                                   text_height * 2, text_height);

      std::vector<Component*> row;
      for (int i = 0; i < num_knobs_; ++i)
        row.push_back(knobs_[i].get());
      if (dc_filter_)
        row.insert(row.end() - 1, dc_filter_.get());   // before the mix knob
      placeKnobsInArea(Rectangle<int>(0, knob_y, getWidth(), knob_area_height), row);
      if (dc_filter_) {
        // The toggle is a small text button rather than a knob: centre it in its cell, label height tall.
        Rectangle<int> cell = dc_filter_->getBounds();
        int button_height = std::max(1, static_cast<int>(size_ratio_ * 26));
        dc_filter_->setBounds(cell.getX(), cell.getCentreY() - button_height / 2, cell.getWidth(), button_height);
      }
      SynthSection::resized();

      for (Component* button : { static_cast<Component*>(name_bar_.get()), static_cast<Component*>(save_button_.get()),
                                 static_cast<Component*>(dimension_button_.get()) }) {
        button->setColour(Skin::kIconButtonOff, findColour(Skin::kUiButton, true));
        button->setColour(Skin::kIconButtonOffHover, findColour(Skin::kUiButtonHover, true));
        button->setColour(Skin::kIconButtonOffPressed, findColour(Skin::kUiButtonPressed, true));
      }
    }

    void setVisible(bool should_be_visible) override {
      bool changed = should_be_visible != isVisible();
      SynthSection::setVisible(should_be_visible);
      if (changed) {
        refreshTable();
        repaintBackground();
      }
      if (changed && visibility_callback_)
        visibility_callback_(should_be_visible);
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else if (clicked_button == save_button_.get()) {
        save_button_->setToggleState(false, dontSendNotification);
        if (TableFileHost* host = findParentComponentOfClass<TableFileHost>())
          host->tableSave(slot_);
      }
      else if (clicked_button == dimension_button_.get()) {
        int render_type = (wavetable_->getRenderType() + 1) % Wavetable3d::kNumRenderTypes;
        dimension_button_->setText(strings::kWavetableDimensionNames[render_type]);
        wavetable_->setRenderType(static_cast<Wavetable3d::RenderType>(render_type));
        wavetable_->setDirty();
        repaintBackground();
      }
      else
        SynthSection::buttonClicked(clicked_button);
    }

  private:
    void stepFile(int direction) {
      File file = LoadSave::getShiftedFile(LoadSave::kWavetableFolderName, vital::kWavetableExtensionsList,
                                           LoadSave::kAdditionalWavetableFoldersName, current_file_, direction);
      if (file.exists())
        loadFile(file);
      updatePopupBrowser(this);
    }

    Kind kind_;
    int slot_;
    File current_file_;
    std::unique_ptr<PresetSelector> name_bar_;
    std::unique_ptr<OpenGlToggleButton> save_button_;
    std::string title_;
    int num_knobs_ = 0;
    std::unique_ptr<SynthSlider> knobs_[kMaxKnobs];
    std::string labels_[kMaxKnobs];
    std::unique_ptr<SynthButton> dc_filter_;
    std::unique_ptr<Wavetable3d> wavetable_;
    std::unique_ptr<OpenGlToggleButton> dimension_button_;
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::function<void(bool)> visibility_callback_;
    std::function<bool(String, InputStream*, WavetableCreator::AudioFileLoadStyle)> import_callback_;
    int title_height_ = 28;
};
