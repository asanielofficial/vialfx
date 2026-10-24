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
#include "synth_button.h"
#include "synth_constants.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"

// Panel for one audio input of the effect build ("INPUT" or "SIDECHAIN"), laid out like an oscillator
// panel: title on the left, a live spectrum display in the middle with the routing dropdown under it, and
// the two big morph knobs on the right. The knobs are bound to parameters but do nothing yet.
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
    FxInputSection(String title, const std::string& prefix, int num_warp_modes = 8) :
        SynthSection(title), prefix_(prefix), current_destination_(vital::constants::kDualFilters),
        num_warp_modes_(num_warp_modes) {
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

      setSkinOverride(Skin::kOscillator);
      updateDestinationText();
    }

    virtual ~FxInputSection() { }

    void addListener(Listener* listener) { listeners_.push_back(listener); }

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
      drawLabelForComponent(g, TRANS("SPECTRAL"), spectral_morph_.get());
      // The distortion knob's label is the warp mode selector.
      g.setColour(findColour(Skin::kTextComponentBackground, true));
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
    static bool isBipolarWarp(int mode) { return mode == 0 || mode == 1 || mode == 3 || mode == 4; }

    void updateWarpKnob() {
      bool bipolar = isBipolarWarp(current_warp_);
      distortion_->setBipolar(bipolar);
      distortion_->setDoubleClickReturnValue(true, bipolar ? 0.5f : 0.0f);
      distortion_->redoImage();
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
    int current_warp_ = 0;
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
    std::unique_ptr<ShapeButton> warp_selector_;
    std::unique_ptr<PlainTextComponent> warp_text_;
    std::unique_ptr<OpenGlShapeButton> prev_warp_;
    std::unique_ptr<OpenGlShapeButton> next_warp_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxInputSection)
};
