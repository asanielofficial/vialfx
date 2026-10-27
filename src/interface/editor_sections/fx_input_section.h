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

#include "oscilloscope.h"
#include "open_gl_image_component.h"
#include "paths.h"
#include "skin.h"
#include "sound_engine.h"
#include "spectral_warp_editor.h"
#include "synth_button.h"
#include "synth_constants.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"

// Panel for one audio input of the effect build ("INPUT" or "SIDECHAIN"), laid out like an oscillator
// panel: title on the left, a live spectrum display in the middle with the routing dropdown under it, and
// the two big knobs on the right (spectral morph and distortion warp), each with its mode selector underneath.
//
// The dropdown picks where the input goes (Filter 1, Filter 2, Filter 1+2, Effects or Direct Out), the same
// list the synth uses for its oscillators; the filters' INPUT / SIDECHAIN buttons mirror it.
class FxInputSection : public SynthSection {
  public:
    static constexpr float kKnobAreaWidthRatio = 0.38f;
    static constexpr float kLevelPanWidthRatio = 0.19f;
    static constexpr int kNumDestinations = vital::constants::kNumSourceDestinations;

    class Listener {
      public:
        virtual ~Listener() { }
        virtual void inputDestinationChanged(FxInputSection* section, int destination) = 0;
    };

    // prefix is "input" or "sidechain". num_warp_modes is how many distortion warp modes the lane offers
    // (the main lane has all 8; the sidechain has no modulator lane for FM and RM, so it offers 6).
    FxInputSection(String title, const std::string& prefix, int num_warp_modes = 9, int num_spectral_modes = 15) :
        SynthSection(title), prefix_(prefix), current_destination_(vital::constants::kDualFilters),
        num_warp_modes_(num_warp_modes), num_spectral_modes_(num_spectral_modes) {
      spectral_morph_ = std::make_unique<SynthSlider>(prefix + "_spectral_morph_amount");
      addSlider(spectral_morph_.get());
      spectral_morph_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      spectral_morph_->setKnobSizeScale(1.5f);
      spectral_morph_->setPopupPrefix("Spectral Morph: ");

      distortion_ = std::make_unique<SynthSlider>(prefix + "_distortion_amount");
      addSlider(distortion_.get());
      distortion_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      distortion_->setKnobSizeScale(1.5f);
      distortion_->setPopupPrefix("Distortion: ");

      level_ = std::make_unique<SynthSlider>(prefix + "_level");
      addSlider(level_.get());
      level_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      level_->setPopupPrefix("Level: ");

      pan_ = std::make_unique<SynthSlider>(prefix + "_pan");
      addSlider(pan_.get());
      pan_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      pan_->setBipolar();
      pan_->setPopupPrefix("Pan: ");

      lane_on_ = std::make_unique<SynthButton>(prefix + "_on");
      addButton(lane_on_.get());
      setActivator(lane_on_.get());

      spectrogram_ = std::make_unique<Spectrogram>();
      addOpenGlComponent(spectrogram_.get());
      spectrogram_->setMinFrequency(20.0f);
      spectrogram_->setMaxFrequency(20000.0f);
      spectrogram_->setMinDb(-60.0f);
      spectrogram_->setMaxDb(0.0f);

      destination_selector_ = std::make_unique<ShapeButton>("Destination", Colour(0xff666666),
                                                            Colour(0xffaaaaaa), Colour(0xff888888));
      addAndMakeVisible(destination_selector_.get());
      destination_selector_->addListener(this);
      destination_selector_->setTriggeredOnMouseDown(true);
      destination_selector_->setShape(Path(), true, true, true);

      destination_text_ = std::make_unique<PlainTextComponent>("Destination Text", "---");
      addOpenGlComponent(destination_text_.get());

      prev_destination_ = std::make_unique<OpenGlShapeButton>("Prev Destination");
      addAndMakeVisible(prev_destination_.get());
      addOpenGlComponent(prev_destination_->getGlComponent());
      prev_destination_->addListener(this);
      prev_destination_->setShape(Paths::prev());

      next_destination_ = std::make_unique<OpenGlShapeButton>("Next Destination");
      addAndMakeVisible(next_destination_.get());
      addOpenGlComponent(next_destination_->getGlComponent());
      next_destination_->addListener(this);
      next_destination_->setShape(Paths::next());

      warp_selector_ = std::make_unique<ShapeButton>("Warp Mode", Colour(0xff666666),
                                                     Colour(0xffaaaaaa), Colour(0xff888888));
      addAndMakeVisible(warp_selector_.get());
      warp_selector_->addListener(this);
      warp_selector_->setTriggeredOnMouseDown(true);
      warp_selector_->setShape(Path(), true, true, true);

      warp_text_ = std::make_unique<PlainTextComponent>("Warp Text", strings::kWarpNames[0]);
      addOpenGlComponent(warp_text_.get());

      prev_warp_ = std::make_unique<OpenGlShapeButton>("Prev Warp");
      addAndMakeVisible(prev_warp_.get());
      addOpenGlComponent(prev_warp_->getGlComponent());
      prev_warp_->addListener(this);
      prev_warp_->setShape(Paths::prev());

      next_warp_ = std::make_unique<OpenGlShapeButton>("Next Warp");
      addAndMakeVisible(next_warp_.get());
      addOpenGlComponent(next_warp_->getGlComponent());
      next_warp_->addListener(this);
      next_warp_->setShape(Paths::next());

      spectral_selector_ = std::make_unique<ShapeButton>("Spectral Mode", Colour(0xff666666),
                                                         Colour(0xffaaaaaa), Colour(0xff888888));
      addAndMakeVisible(spectral_selector_.get());
      spectral_selector_->addListener(this);
      spectral_selector_->setTriggeredOnMouseDown(true);
      spectral_selector_->setShape(Path(), true, true, true);

      spectral_text_ = std::make_unique<PlainTextComponent>("Spectral Text", strings::kFxSpectralNames[0]);
      addOpenGlComponent(spectral_text_.get());

      prev_spectral_ = std::make_unique<OpenGlShapeButton>("Prev Spectral");
      addAndMakeVisible(prev_spectral_.get());
      addOpenGlComponent(prev_spectral_->getGlComponent());
      prev_spectral_->addListener(this);
      prev_spectral_->setShape(Paths::prev());

      next_spectral_ = std::make_unique<OpenGlShapeButton>("Next Spectral");
      addAndMakeVisible(next_spectral_.get());
      addOpenGlComponent(next_spectral_->getGlComponent());
      next_spectral_->addListener(this);
      next_spectral_->setShape(Paths::next());

      // Random Amplitudes shuffle: a small bar under the dice, a modulation target (drag any source onto it).
      shuffle_ = std::make_unique<SynthSlider>(prefix + "_random_shuffle");
      addSlider(shuffle_.get());
      shuffle_->setSliderStyle(Slider::LinearBar);
      shuffle_->setPopupPlacement(BubbleComponent::above);
      shuffle_->setModulationPlacement(BubbleComponent::above);
      shuffle_->setPopupPrefix("Shuffle: ");
      shuffle_->setDoubleClickReturnValue(true, 0.0);
      shuffle_->setVisible(false);

      tool_button_ = std::make_unique<OpenGlShapeButton>("Spectral Tool");
      addAndMakeVisible(tool_button_.get());
      addOpenGlComponent(tool_button_->getGlComponent());
      tool_button_->addListener(this);
      tool_button_->setShape(Paths::pencil());
      tool_button_->setVisible(false);

      setSkinOverride(Skin::kOscillator);
      updateDestinationText();
    }

    virtual ~FxInputSection() { }

    void addListener(Listener* listener) { listeners_.push_back(listener); }

    // The popup that draws the Spectral Filter / Phaser / Flanger curves (owned by the full interface).
    void setSpectralEditor(SpectralWarpEditor* editor) { spectral_editor_ = editor; }

    int getDestination() const { return current_destination_; }

    // Called by the filters' INPUT / SIDECHAIN buttons.
    void toggleFilterInput(int filter_index, bool on) {
      vital::constants::SourceDestination destination = (vital::constants::SourceDestination)current_destination_;
      if (filter_index == 0)
        setDestination(vital::constants::toggleFilter1(destination, on));
      else
        setDestination(vital::constants::toggleFilter2(destination, on));
    }

    void paintBackground(Graphics& g) override {
      if (getWidth() == 0)
        return;

      paintContainer(g);
      paintHeadingText(g);
      paintKnobShadows(g);
      paintChildrenBackgrounds(g);
      paintBorder(g);

      g.setColour(findColour(Skin::kTextComponentBackground, true));
      g.fillRoundedRectangle(destination_selector_->getBounds().toFloat(), findValue(Skin::kLabelBackgroundRounding));

      setLabelFont(g);
      drawLabelConnectionForComponents(g, level_.get(), pan_.get());
      drawLabelForComponent(g, TRANS("LEVEL"), level_.get());
      drawLabelForComponent(g, TRANS("PAN"), pan_.get());
      // The labels under the two big knobs are their mode selectors.
      g.setColour(findColour(Skin::kTextComponentBackground, true));
      g.fillRoundedRectangle(spectral_label_bounds_.toFloat(),
                             findValue(Skin::kLabelBackgroundRounding));
      g.fillRoundedRectangle(getLabelBackgroundBounds(distortion_.get()).toFloat(),
                             findValue(Skin::kLabelBackgroundRounding));
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      SynthSection::resized();

      int widget_margin = getWidgetMargin();
      int label_height = findValue(Skin::kLabelBackgroundHeight);
      int title_width = getTitleWidth();
      int knob_width = getWidth() * kKnobAreaWidthRatio;
      int knob_y = widget_margin;
      int knob_height = getHeight() - label_height - 2 * widget_margin;

      placeKnobsInArea(Rectangle<int>(getWidth() - knob_width, knob_y, knob_width, knob_height + widget_margin),
                       { spectral_morph_.get(), distortion_.get() });

      // Level and pan on the left, like an oscillator panel.
      int level_pan_width = getWidth() * kLevelPanWidthRatio;
      placeKnobsInArea(Rectangle<int>(title_width, knob_y, level_pan_width, knob_height + widget_margin),
                       { level_.get(), pan_.get() });

      // Spectrum display in the middle, routing dropdown underneath it.
      int display_x = title_width + level_pan_width + widget_margin;
      int display_width = std::max(0, getWidth() - knob_width - display_x - widget_margin);
      int destination_y = getHeight() - label_height - widget_margin;
      int display_height = std::max(0, destination_y - 2 * widget_margin);
      spectrogram_->setBounds(display_x, widget_margin, display_width, display_height);
      spectrogram_->setColour(Skin::kBody, findColour(Skin::kBackground, true));

      destination_selector_->setBounds(display_x, destination_y, display_width, label_height);
      destination_text_->setBounds(destination_selector_->getBounds());
      destination_text_->setTextSize(findValue(Skin::kLabelHeight));
      destination_text_->setColor(findColour(Skin::kBodyText, true));

      int browse_width = label_height;
      prev_destination_->setBounds(display_x, destination_y, browse_width, browse_width);
      next_destination_->setBounds(display_x + display_width - browse_width, destination_y,
                                   browse_width, browse_width);

      Rectangle<int> warp_bounds = getLabelBackgroundBounds(distortion_.get());
      int warp_browse = warp_bounds.getHeight();
      warp_text_->setBounds(warp_bounds);
      warp_text_->setTextSize(findValue(Skin::kLabelHeight));
      warp_text_->setColor(findColour(Skin::kBodyText, true));
      prev_warp_->setBounds(warp_bounds.getX(), warp_bounds.getY(), warp_browse, warp_browse);
      next_warp_->setBounds(warp_bounds.getRight() - warp_browse, warp_bounds.getY(), warp_browse, warp_browse);
      int warp_menu_x = prev_warp_->getRight();
      warp_selector_->setBounds(warp_menu_x, warp_bounds.getY(), next_warp_->getX() - warp_menu_x, warp_browse);
      Rectangle<int> spectral_bounds = getLabelBackgroundBounds(spectral_morph_.get());
      int spectral_browse = spectral_bounds.getHeight();
      spectral_text_->setBounds(spectral_bounds);
      spectral_text_->setTextSize(findValue(Skin::kLabelHeight));
      spectral_text_->setColor(findColour(Skin::kBodyText, true));
      prev_spectral_->setBounds(spectral_bounds.getX(), spectral_bounds.getY(), spectral_browse, spectral_browse);
      next_spectral_->setBounds(spectral_bounds.getRight() - spectral_browse, spectral_bounds.getY(),
                                spectral_browse, spectral_browse);
      int spectral_menu_x = prev_spectral_->getRight();
      spectral_selector_->setBounds(spectral_menu_x, spectral_bounds.getY(),
                                    next_spectral_->getX() - spectral_menu_x, spectral_browse);

      // Random Amplitudes gives up a strip to the right of the spectral knob for the dice and the shuffle bar, so
      // nothing overlaps the bar: a modulation source dragged onto the bar lands on the bar and not on the knob
      // underneath. In every other mode the knob keeps its full width. The pencil of the drawable modes sits in the
      // empty top-right corner of the knob cell, clear of the knob dial itself.
      spectral_label_bounds_ = spectral_bounds;
      Rectangle<int> knob_cell = spectral_morph_->getBounds();
      bool dice = current_spectral_ == kRandomAmplitudesMode;
      int cell_height = knob_cell.getHeight();
      if (dice) {
        int strip_width = std::min(knob_cell.getWidth() / 3,
                                   std::max(1, static_cast<int>(cell_height * 0.5f)));
        spectral_morph_->setBounds(knob_cell.withWidth(knob_cell.getWidth() - strip_width));
        int strip_centre = spectral_morph_->getRight() + strip_width / 2;
        int tool_size = std::max(1, std::min(strip_width - widget_margin, static_cast<int>(cell_height * 0.2f)));
        int bar_height = std::max(14, static_cast<int>(cell_height * 0.14f));
        int bar_width = std::max(1, strip_width - widget_margin);
        int block_height = tool_size + widget_margin + bar_height;
        int tool_y = knob_cell.getY() + (cell_height - block_height) / 2;
        tool_button_->setBounds(strip_centre - tool_size / 2, tool_y, tool_size, tool_size);
        shuffle_->setBounds(strip_centre - bar_width / 2, tool_button_->getBottom() + widget_margin,
                            bar_width, bar_height);
      }
      else {
        int tool_size = std::max(1, static_cast<int>(cell_height * 0.22f));
        tool_button_->setBounds(knob_cell.getRight() - tool_size, knob_cell.getY(), tool_size, tool_size);
        shuffle_->setBounds(0, 0, 0, 0);
      }
      connectMemory();
    }

    void visibilityChanged() override {
      SynthSection::visibilityChanged();
      connectMemory();
    }

    void parentHierarchyChanged() override {
      SynthSection::parentHierarchyChanged();
      connectMemory();
    }

    void setAllValues(vital::control_map& controls) override {
      SynthSection::setAllValues(controls);
      auto destination = controls.find(prefix_ + "_destination");
      if (destination != controls.end() && destination->second) {
        current_destination_ = clampDestination(static_cast<int>(destination->second->value()));
        notifyDestination();
      }
      auto warp = controls.find(prefix_ + "_distortion_type");
      if (warp != controls.end() && warp->second) {
        current_warp_ = clampWarp(static_cast<int>(warp->second->value()));
        warp_text_->setText(strings::kWarpNames[current_warp_]);
        updateWarpKnob();
      }
      auto spectral = controls.find(prefix_ + "_spectral_morph_type");
      if (spectral != controls.end() && spectral->second) {
        current_spectral_ = clampSpectral(static_cast<int>(spectral->second->value()));
        spectral_text_->setText(strings::kFxSpectralNames[current_spectral_]);
        updateSpectralKnob();
      }
      updateToolButton();
      connectMemory();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == destination_selector_.get()) {
        PopupItems options;
        for (int i = 0; i < kNumDestinations; ++i)
          options.addItem(i, strings::kDestinationMenuNames[i]);

        Point<int> position(destination_selector_->getX(), destination_selector_->getBottom());
        showPopupSelector(this, position, options, [=](int selection) { setDestination(selection); });
      }
      else if (clicked_button == warp_selector_.get()) {
        PopupItems options;
        for (int i = 0; i < num_warp_modes_; ++i)
          options.addItem(i, strings::kWarpNames[i]);

        Point<int> position(warp_selector_->getX(), warp_selector_->getBottom());
        showPopupSelector(this, position, options, [=](int selection) { setWarp(selection); });
      }
      else if (clicked_button == spectral_selector_.get()) {
        PopupItems options;
        for (int i = 0; i < num_spectral_modes_; ++i)
          options.addItem(i, strings::kFxSpectralNames[i]);

        Point<int> position(spectral_selector_->getX(), spectral_selector_->getBottom());
        showPopupSelector(this, position, options, [=](int selection) { setSpectral(selection); });
      }
      else if (clicked_button == tool_button_.get())
        useTool();
      else if (clicked_button == prev_spectral_.get())
        setSpectral((current_spectral_ + num_spectral_modes_ - 1) % num_spectral_modes_);
      else if (clicked_button == next_spectral_.get())
        setSpectral((current_spectral_ + 1) % num_spectral_modes_);
      else if (clicked_button == prev_warp_.get())
        setWarp((current_warp_ + num_warp_modes_ - 1) % num_warp_modes_);
      else if (clicked_button == next_warp_.get())
        setWarp((current_warp_ + 1) % num_warp_modes_);
      else if (clicked_button == prev_destination_.get())
        setDestination((current_destination_ + kNumDestinations - 1) % kNumDestinations);
      else if (clicked_button == next_destination_.get())
        setDestination((current_destination_ + 1) % kNumDestinations);
      else
        SynthSection::buttonClicked(clicked_button);
    }

  private:
    static int clampDestination(int destination) {
      return std::max(0, std::min(destination, kNumDestinations - 1));
    }

    int clampWarp(int mode) const { return std::max(0, std::min(mode, num_warp_modes_ - 1)); }

    // Sync, Formant, Bend and Squeeze have a centred (bipolar) amount knob, like the original distortions.
    static bool isBipolarWarp(int mode) { return mode == 1 || mode == 2 || mode == 4 || mode == 5; }

    void updateWarpKnob() {
      bool bipolar = isBipolarWarp(current_warp_);
      distortion_->setBipolar(bipolar);
      distortion_->setDoubleClickReturnValue(true, bipolar ? 0.5f : 0.0f);
      distortion_->redoImage();
    }

    int clampSpectral(int mode) const { return std::max(0, std::min(mode, num_spectral_modes_ - 1)); }

    // Formant Scale, Harmonic/Inharmonic Stretch, Phase Disperse, the three drawable modes are centred (bipolar).
    static bool isBipolarSpectral(int mode) {
      return mode == 1 || mode == 2 || mode == 3 || mode == 8 || (mode >= 11 && mode <= 13);
    }

    static constexpr int kRandomAmplitudesMode = 5;
    static constexpr int kFirstDrawableMode = 11;  // Spectral Filter, Phaser, Flanger

    // A die: a rounded square with five holes.
    static Path dicePath() {
      Path path;
      path.setUsingNonZeroWinding(false);
      path.addRoundedRectangle(0.15f, 0.15f, 0.7f, 0.7f, 0.14f);
      const float pips[5][2] = { { 0.3f, 0.3f }, { 0.7f, 0.3f }, { 0.5f, 0.5f }, { 0.3f, 0.7f }, { 0.7f, 0.7f } };
      for (const auto& pip : pips)
        path.addEllipse(pip[0] - 0.055f, pip[1] - 0.055f, 0.11f, 0.11f);
      return path;
    }

    bool isDrawableMode() const {
      return current_spectral_ >= kFirstDrawableMode && current_spectral_ < kFirstDrawableMode + 3;
    }

    // Dice for Random Amplitudes, pencil for the three drawable modes, nothing for the rest.
    void updateToolButton() {
      bool dice = current_spectral_ == kRandomAmplitudesMode;
      tool_button_->setVisible(dice || isDrawableMode());
      shuffle_->setVisible(dice);
      resized();
      repaintBackground();   // the knob shadow is part of the cached background and follows the knob
      if (dice)
        tool_button_->setShape(dicePath());
      else if (isDrawableMode())
        tool_button_->setShape(Paths::pencil());
    }

    int lane() const { return prefix_ == "input" ? 0 : 1; }

    void useTool() {
      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent == nullptr || parent->getSynth() == nullptr)
        return;

      if (current_spectral_ == kRandomAmplitudesMode) {
        // The dice is a switch parameter (automatable in the host): pressing it flips the switch, and every flip
        // scrambles the pattern to a new seed, which is saved with the preset.
        std::string name = prefix_ + "_random_dice";
        auto& controls = parent->getSynth()->getControls();
        auto control = controls.find(name);
        float current = control != controls.end() && control->second ? control->second->value() : 0.0f;
        parent->getSynth()->valueChangedInternal(name, current > 0.5f ? 0.0f : 1.0f);
        parent->notifyChange();
      }
      else if (isDrawableMode() && spectral_editor_)
        spectral_editor_->showCurve(lane(), current_spectral_ - kFirstDrawableMode);
    }

    void updateSpectralKnob() {
      bool bipolar = isBipolarSpectral(current_spectral_);
      spectral_morph_->setBipolar(bipolar);
      spectral_morph_->setDoubleClickReturnValue(true, bipolar ? 0.5f : 0.0f);
      spectral_morph_->redoImage();
    }

    void setSpectral(int mode) {
      bool was_bipolar = isBipolarSpectral(current_spectral_);
      current_spectral_ = clampSpectral(mode);
      spectral_text_->setText(strings::kFxSpectralNames[current_spectral_]);
      updateSpectralKnob();

      if (was_bipolar != isBipolarSpectral(current_spectral_))
        spectral_morph_->setValue(isBipolarSpectral(current_spectral_) ? 0.5f : 0.0f, sendNotificationSync);
      updateToolButton();

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth())
        parent->getSynth()->valueChangedInternal(prefix_ + "_spectral_morph_type", current_spectral_);
    }

    void setWarp(int mode) {
      bool was_bipolar = isBipolarWarp(current_warp_);
      current_warp_ = clampWarp(mode);
      warp_text_->setText(strings::kWarpNames[current_warp_]);
      updateWarpKnob();

      // Moving between a centred and a zero-based knob would leave a surprising amount, so go back to neutral.
      if (was_bipolar != isBipolarWarp(current_warp_))
        distortion_->setValue(isBipolarWarp(current_warp_) ? 0.5f : 0.0f, sendNotificationSync);

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth())
        parent->getSynth()->valueChangedInternal(prefix_ + "_distortion_type", current_warp_);
    }

    void connectMemory() {
      if (memory_connected_)
        return;

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent == nullptr || parent->getSynth() == nullptr)
        return;

      spectrogram_->setAudioMemory(prefix_ == "input" ? parent->getSynth()->getInputMemory()
                                                      : parent->getSynth()->getSidechainMemory());
      memory_connected_ = true;
    }

    void updateDestinationText() {
      destination_text_->setText(strings::kDestinationNames[clampDestination(current_destination_)]);
    }

    void notifyDestination() {
      updateDestinationText();
      for (Listener* listener : listeners_)
        listener->inputDestinationChanged(this, current_destination_);
    }

    void setDestination(int destination) {
      current_destination_ = clampDestination(destination);
      notifyDestination();

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth())
        parent->getSynth()->valueChangedInternal(prefix_ + "_destination", current_destination_);
    }

    std::string prefix_;
    int current_destination_;
    int num_warp_modes_;
    int num_spectral_modes_;
    int current_warp_ = 0;
    int current_spectral_ = 0;
    SpectralWarpEditor* spectral_editor_ = nullptr;
    Rectangle<int> spectral_label_bounds_;
    bool memory_connected_ = false;
    std::vector<Listener*> listeners_;

    std::unique_ptr<SynthSlider> level_;
    std::unique_ptr<SynthSlider> pan_;
    std::unique_ptr<SynthButton> lane_on_;
    std::unique_ptr<SynthSlider> spectral_morph_;
    std::unique_ptr<SynthSlider> distortion_;
    std::unique_ptr<Spectrogram> spectrogram_;
    std::unique_ptr<ShapeButton> destination_selector_;
    std::unique_ptr<PlainTextComponent> destination_text_;
    std::unique_ptr<OpenGlShapeButton> prev_destination_;
    std::unique_ptr<OpenGlShapeButton> next_destination_;
    std::unique_ptr<OpenGlShapeButton> tool_button_;
    std::unique_ptr<SynthSlider> shuffle_;
    std::unique_ptr<ShapeButton> spectral_selector_;
    std::unique_ptr<PlainTextComponent> spectral_text_;
    std::unique_ptr<OpenGlShapeButton> prev_spectral_;
    std::unique_ptr<OpenGlShapeButton> next_spectral_;
    std::unique_ptr<ShapeButton> warp_selector_;
    std::unique_ptr<PlainTextComponent> warp_text_;
    std::unique_ptr<OpenGlShapeButton> prev_warp_;
    std::unique_ptr<OpenGlShapeButton> next_warp_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxInputSection)
};
