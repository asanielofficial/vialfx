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

#include "open_gl_image_component.h"
#include "paths.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_constants.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "fx_tables.h"

#include <functional>

// The Morph effect: the spectral morph and the distortion warp of the input lanes, as an effect. The same two big
// knobs with their mode dropdowns and the swap button between them, without the level, pan, routing and spectrum
// of a lane. Everything is centred in the (shorter) effect, so the panel stays small.
//
// Every mode of the lanes is offered except Vocode, FM and RM (they need the sidechain lane as their modulator). The
// drawable modes (Spectral Filter, Phaser, Flanger) get a pencil, and the table modes a cogwheel and a pencil: they
// open their editors as attachments below the effect (the effect list makes room, see FxEffectSlot).
class FxMorphSection : public SynthSection {
  public:
    // How tall the effect is, in knob sections (a normal effect takes two).
    static float heightInKnobSections() { return 1.45f; }

    // The editors that dock below the effect.
    enum Attachment {
      kCurveEditor,
      kSpectralTableMenu,
      kDistortTableMenu,
      kNumAttachments
    };

    FxMorphSection(String title, const std::string& id, int instance) :
        SynthSection(title), id_(id), instance_(instance) {
      spectral_morph_ = std::make_unique<SynthSlider>(id + "_spectral_morph_amount");
      addSlider(spectral_morph_.get());
      spectral_morph_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      spectral_morph_->setKnobSizeScale(1.3f);
      spectral_morph_->setPopupPrefix("Spectral Morph: ");

      distortion_ = std::make_unique<SynthSlider>(id + "_distortion_amount");
      addSlider(distortion_.get());
      distortion_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      distortion_->setKnobSizeScale(1.3f);
      distortion_->setPopupPrefix("Distortion: ");

      on_ = std::make_unique<SynthButton>(id + "_on");
      addButton(on_.get());
      setActivator(on_.get());

      spectral_selector_ = makeSelector("Spectral Mode");
      spectral_text_ = std::make_unique<PlainTextComponent>("Spectral Text", strings::kFxSpectralNames[0]);
      addOpenGlComponent(spectral_text_.get());
      prev_spectral_ = makeArrow("Prev Spectral", Paths::prev());
      next_spectral_ = makeArrow("Next Spectral", Paths::next());

      warp_selector_ = makeSelector("Warp Mode");
      warp_text_ = std::make_unique<PlainTextComponent>("Warp Text", strings::kWarpNames[0]);
      addOpenGlComponent(warp_text_.get());
      prev_warp_ = makeArrow("Prev Warp", Paths::prev());
      next_warp_ = makeArrow("Next Warp", Paths::next());

      // Random Amplitudes: the dice and the shuffle bar sit beside the spectral knob.
      shuffle_ = std::make_unique<SynthSlider>(id + "_random_shuffle");
      addSlider(shuffle_.get());
      shuffle_->setSliderStyle(Slider::LinearBar);
      shuffle_->setPopupPlacement(BubbleComponent::above);
      shuffle_->setModulationPlacement(BubbleComponent::above);
      shuffle_->setPopupPrefix("Shuffle: ");
      shuffle_->setDoubleClickReturnValue(true, 0.0);
      shuffle_->setVisible(false);

      dice_button_ = std::make_unique<OpenGlShapeButton>("Dice");
      addAndMakeVisible(dice_button_.get());
      addOpenGlComponent(dice_button_->getGlComponent());
      dice_button_->addListener(this);
      dice_button_->setShape(dicePath());
      dice_button_->setVisible(false);

      swap_button_ = std::make_unique<OpenGlShapeButton>("Swap Order");
      addAndMakeVisible(swap_button_.get());
      addOpenGlComponent(swap_button_->getGlComponent());
      swap_button_->addListener(this);
      swap_button_->setShape(swapPath());
      swap_button_->setTooltip("Swap the order: spectral morph then distortion, or distortion then spectral morph");

      // Drawable modes: a pencil opens the curve editor; Spectral Table: a cogwheel (top right) opens the settings
      // menu and a pencil (top left) the wavetable editor; the same two for the Distort Table on the warp knob.
      curve_button_ = makeTool("Spectral Tool", Paths::pencil());
      spectral_edit_button_ = makeTool("Spectral Table Editor", Paths::pencil());
      warp_tool_button_ = makeTool("Table Settings", Paths::gear());
      warp_edit_button_ = makeTool("Table Editor", Paths::pencil());

      setSkinOverride(Skin::kMorph);
    }

    ~FxMorphSection() override { }

    void paintBackground(Graphics& g) override {
      if (getWidth() == 0)
        return;

      SynthSection::paintBackground(g);

      setLabelFont(g);
      // The labels under the two big knobs are their mode selectors.
      g.setColour(findColour(Skin::kTextComponentBackground, true));
      g.fillRoundedRectangle(spectral_label_bounds_.toFloat(), findValue(Skin::kLabelBackgroundRounding));
      g.fillRoundedRectangle(warp_label_bounds_.toFloat(), findValue(Skin::kLabelBackgroundRounding));
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      SynthSection::resized();

      const int margin = getWidgetMargin();
      const int label_height = findValue(Skin::kLabelBackgroundHeight);
      const int title_width = getTitleWidth();
      const int knob_height = std::max(1, getHeight() - label_height - 2 * margin);
      const int swap_size = label_height;
      const int gap = swap_size + 2 * margin;

      // Everything sits in one group in the middle of the space right of the title: the spectral knob, the swap
      // button and the warp knob, each knob with its mode selector underneath. The group stops growing at a sensible
      // width so a wide effect does not stretch it.
      const int space = std::max(0, getWidth() - title_width - 2 * margin);
      const int cell_width = std::max(1, std::min((space - gap) / 2, static_cast<int>(knob_height * 3.0f)));
      const int group_width = 2 * cell_width + gap;
      const int left = title_width + margin + (space - group_width) / 2;

      Rectangle<int> spectral_cell(left, margin, cell_width, knob_height);
      Rectangle<int> warp_cell(left + cell_width + gap, margin, cell_width, knob_height);
      spectral_morph_->setBounds(spectral_cell);
      distortion_->setBounds(warp_cell);

      layoutSelector(spectral_morph_.get(), spectral_label_bounds_, spectral_text_.get(), prev_spectral_.get(),
                     next_spectral_.get(), spectral_selector_.get());
      layoutSelector(distortion_.get(), warp_label_bounds_, warp_text_.get(), prev_warp_.get(), next_warp_.get(),
                     warp_selector_.get());

      // The swap button sits between the two selectors, on their row.
      swap_button_->setBounds(left + cell_width + (gap - swap_size) / 2, spectral_label_bounds_.getY(),
                              swap_size, swap_size);

      // Random Amplitudes: the knob gives up a strip on each side (the dice and shuffle on the right, the same
      // empty strip on the left) so the knob stays above the middle of its selector.
      int tool_size = std::max(1, static_cast<int>(knob_height * 0.24f));
      curve_button_->setBounds(spectral_cell.getRight() - tool_size, spectral_cell.getY(), tool_size, tool_size);
      spectral_edit_button_->setBounds(spectral_cell.getX(), spectral_cell.getY(), tool_size, tool_size);
      warp_tool_button_->setBounds(warp_cell.getRight() - tool_size, warp_cell.getY(), tool_size, tool_size);
      warp_edit_button_->setBounds(warp_cell.getX(), warp_cell.getY(), tool_size, tool_size);

      if (current_spectral_ == kRandomAmplitudes) {
        int strip = std::min(cell_width / 4, std::max(1, static_cast<int>(knob_height * 0.5f)));
        spectral_morph_->setBounds(spectral_cell.withTrimmedLeft(strip).withTrimmedRight(strip));
        int centre = spectral_morph_->getRight() + strip / 2;
        int dice_size = std::max(1, std::min(strip - margin, static_cast<int>(knob_height * 0.22f)));
        int bar_height = std::max(14, static_cast<int>(knob_height * 0.16f));
        int bar_width = std::max(1, strip - margin);
        int block = dice_size + margin + bar_height;
        int tool_y = spectral_cell.getY() + (knob_height - block) / 2;
        dice_button_->setBounds(centre - dice_size / 2, tool_y, dice_size, dice_size);
        shuffle_->setBounds(centre - bar_width / 2, dice_button_->getBottom() + margin, bar_width, bar_height);
      }
      else {
        dice_button_->setBounds(0, 0, 0, 0);
        shuffle_->setBounds(0, 0, 0, 0);
      }
    }

    void setAllValues(vital::control_map& controls) override {
      SynthSection::setAllValues(controls);
      auto spectral = controls.find(id_ + "_spectral_morph_type");
      if (spectral != controls.end() && spectral->second)
        current_spectral_ = safeSpectral(static_cast<int>(spectral->second->value() + 0.5f));
      spectral_text_->setText(strings::kFxSpectralNames[current_spectral_]);
      updateSpectralKnob();

      auto warp = controls.find(id_ + "_distortion_type");
      if (warp != controls.end() && warp->second)
        current_warp_ = safeWarp(static_cast<int>(warp->second->value() + 0.5f));
      warp_text_->setText(strings::kWarpNames[current_warp_]);
      updateWarpKnob();

      auto order = controls.find(id_ + "_warp_first");
      if (order != controls.end() && order->second) {
        warp_first_ = order->second->value() > 0.5f;
        swap_button_->useOnColors(warp_first_);
      }
      updateToolButton();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == swap_button_.get()) {
        setWarpFirst(!warp_first_);
      }
      else if (clicked_button == spectral_selector_.get()) {
        PopupItems options;
        for (int i = 0; i < kNumSpectralModes; ++i)
          options.addItem(spectralModes()[i], strings::kFxSpectralNames[spectralModes()[i]]);

        Point<int> position(spectral_selector_->getX(), spectral_selector_->getBottom());
        showPopupSelector(this, position, options, [=](int selection) { setSpectral(selection); });
      }
      else if (clicked_button == warp_selector_.get()) {
        PopupItems options;
        for (int i = 0; i < kNumWarpModes; ++i)
          options.addItem(warpModes()[i], strings::kWarpNames[warpModes()[i]]);

        Point<int> position(warp_selector_->getX(), warp_selector_->getBottom());
        showPopupSelector(this, position, options, [=](int selection) { setWarp(selection); });
      }
      else if (clicked_button == prev_spectral_.get())
        setSpectral(stepSpectral(-1));
      else if (clicked_button == next_spectral_.get())
        setSpectral(stepSpectral(1));
      else if (clicked_button == prev_warp_.get())
        setWarp(stepWarp(-1));
      else if (clicked_button == next_warp_.get())
        setWarp(stepWarp(1));
      else if (clicked_button == curve_button_.get()) {
        if (isDrawableMode() && attachment_callback_)
          attachment_callback_(kCurveEditor);
        else if (isSpectralTableMode() && attachment_callback_)
          attachment_callback_(kSpectralTableMenu);
      }
      else if (clicked_button == spectral_edit_button_.get()) {
        if (isSpectralTableMode() && table_editor_callback_)
          table_editor_callback_(vital::FxTables::morphSlot(instance_, true));
      }
      else if (clicked_button == warp_tool_button_.get()) {
        if (isWarpTableMode() && attachment_callback_)
          attachment_callback_(kDistortTableMenu);
      }
      else if (clicked_button == warp_edit_button_.get()) {
        if (isWarpTableMode() && table_editor_callback_)
          table_editor_callback_(vital::FxTables::morphSlot(instance_, false));
      }
      else if (clicked_button == dice_button_.get())
        rollDice();
      else
        SynthSection::buttonClicked(clicked_button);
    }

    // The editors the effect docks below itself are owned by the slot: it is told what to open.
    void setAttachmentCallback(std::function<void(int)> callback) { attachment_callback_ = callback; }
    // Called with the wavetable slot to edit (the pencils of the table modes).
    void setTableEditorCallback(std::function<void(int)> callback) { table_editor_callback_ = callback; }
    // Called whenever the spectral or warp mode changed (the slot closes the editors that no longer apply).
    void setModesChangedCallback(std::function<void()> callback) { modes_changed_callback_ = callback; }

    SynthSlider* getSpectralSlider() { return spectral_morph_.get(); }
    SynthSlider* getDistortionSlider() { return distortion_.get(); }

    bool isDrawableMode() const {
      return current_spectral_ >= kFirstDrawableMode && current_spectral_ < kFirstDrawableMode + 3;
    }
    bool isSpectralTableMode() const { return current_spectral_ == kSpectralTableMode; }
    bool isWarpTableMode() const { return current_warp_ == kWarpTableMode; }
    // 0 = Spectral Filter, 1 = Spectral Phaser, 2 = Spectral Flanger (when isDrawableMode()).
    int curveIndex() const { return current_spectral_ - kFirstDrawableMode; }

  private:
    // Menu order of the spectral modes (Spectral Gate is stored as 16, listed after Time Skew); the stored numbers
    // are the lanes'. Vocode (14) is not offered, and the warps leave out FM and RM (7, 8).
    enum {
      kNumSpectralModes = 16,
      kNumWarpModes = 8,
      kRandomAmplitudes = 5,
      kFirstDrawableMode = 11,   // Spectral Filter, Phaser, Flanger
      kSpectralTableMode = 15,
      kWarpTableMode = 9
    };

    static const int* spectralModes() {
      static const int kModes[kNumSpectralModes] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 11, 12, 13, 15 };
      return kModes;
    }

    static const int* warpModes() {
      static const int kModes[kNumWarpModes] = { 0, 1, 2, 3, 4, 5, 6, 9 };
      return kModes;
    }

    int stepWarp(int direction) const {
      int position = 0;
      for (int i = 0; i < kNumWarpModes; ++i) {
        if (warpModes()[i] == current_warp_)
          position = i;
      }
      return warpModes()[(position + direction + kNumWarpModes) % kNumWarpModes];
    }

    static int safeSpectral(int mode) {
      for (int i = 0; i < kNumSpectralModes; ++i) {
        if (spectralModes()[i] == mode)
          return mode;
      }
      return 0;
    }

    static int safeWarp(int mode) {
      for (int i = 0; i < kNumWarpModes; ++i) {
        if (warpModes()[i] == mode)
          return mode;
      }
      return 0;
    }

    // Formant Scale, Harmonic / Inharmonic Stretch and Phase Disperse have a centred (bipolar) amount.
    // The three drawable modes are centred too.
    static bool isBipolarSpectral(int mode) {
      return mode == 1 || mode == 2 || mode == 3 || mode == 8 || (mode >= kFirstDrawableMode && mode <= 13);
    }
    // Sync, Formant, Bend and Squeeze.
    static bool isBipolarWarp(int mode) { return mode == 1 || mode == 2 || mode == 4 || mode == 5; }

    int stepSpectral(int direction) const {
      int position = 0;
      for (int i = 0; i < kNumSpectralModes; ++i) {
        if (spectralModes()[i] == current_spectral_)
          position = i;
      }
      return spectralModes()[(position + direction + kNumSpectralModes) % kNumSpectralModes];
    }

    std::unique_ptr<ShapeButton> makeSelector(const String& name) {
      auto selector = std::make_unique<ShapeButton>(name, Colour(0xff666666), Colour(0xffaaaaaa), Colour(0xff888888));
      addAndMakeVisible(selector.get());
      selector->addListener(this);
      selector->setTriggeredOnMouseDown(true);
      selector->setShape(Path(), true, true, true);
      return selector;
    }

    std::unique_ptr<OpenGlShapeButton> makeTool(const String& name, const Path& shape) {
      auto tool = makeArrow(name, shape);
      tool->setVisible(false);
      return tool;
    }

    std::unique_ptr<OpenGlShapeButton> makeArrow(const String& name, const Path& shape) {
      auto arrow = std::make_unique<OpenGlShapeButton>(name);
      addAndMakeVisible(arrow.get());
      addOpenGlComponent(arrow->getGlComponent());
      arrow->addListener(this);
      arrow->setShape(shape);
      return arrow;
    }

    // The mode selector under a knob: the label strip with a previous and a next arrow and the menu between them.
    void layoutSelector(SynthSlider* knob, Rectangle<int>& label_bounds, PlainTextComponent* text,
                        OpenGlShapeButton* prev, OpenGlShapeButton* next, ShapeButton* selector) {
      label_bounds = getLabelBackgroundBounds(knob);
      int browse = label_bounds.getHeight();
      text->setBounds(label_bounds);
      text->setTextSize(findValue(Skin::kLabelHeight));
      text->setColor(findColour(Skin::kBodyText, true));
      prev->setBounds(label_bounds.getX(), label_bounds.getY(), browse, browse);
      next->setBounds(label_bounds.getRight() - browse, label_bounds.getY(), browse, browse);
      selector->setBounds(prev->getRight(), label_bounds.getY(), std::max(0, next->getX() - prev->getRight()), browse);
    }

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

    // Two opposing half arrows, the swap icon.
    static Path swapPath() {
      Path half;
      half.addRectangle(0.08f, 0.40f, 0.84f, 0.07f);
      half.addTriangle(0.94f, 0.47f, 0.62f, 0.47f, 0.62f, 0.22f);
      Path other = half;
      other.applyTransform(AffineTransform::rotation(MathConstants<float>::pi, 0.5f, 0.5f));
      half.addPath(other);
      return half;
    }

    void updateSpectralKnob() {
      curve_button_->setVisible(isDrawableMode() || isSpectralTableMode());
      curve_button_->setShape(isDrawableMode() ? Paths::pencil() : Paths::gear());
      spectral_edit_button_->setVisible(isSpectralTableMode());
      bool bipolar = isBipolarSpectral(current_spectral_);
      spectral_morph_->setBipolar(bipolar);
      spectral_morph_->setDoubleClickReturnValue(true, bipolar ? 0.5f : 0.0f);
      spectral_morph_->redoImage();
    }

    void updateWarpKnob() {
      warp_tool_button_->setVisible(isWarpTableMode());
      warp_edit_button_->setVisible(isWarpTableMode());
      bool bipolar = isBipolarWarp(current_warp_);
      distortion_->setBipolar(bipolar);
      distortion_->setDoubleClickReturnValue(true, bipolar ? 0.5f : 0.0f);
      distortion_->redoImage();
    }

    // The dice and the shuffle bar only exist in Random Amplitudes.
    void updateToolButton() {
      bool dice = current_spectral_ == kRandomAmplitudes;
      dice_button_->setVisible(dice);
      shuffle_->setVisible(dice);
      resized();
      modulationLayoutChanged();
      repaintBackground();   // the knob shadow is part of the cached background and follows the knob
    }

    void setSpectral(int mode) {
      bool was_bipolar = isBipolarSpectral(current_spectral_);
      current_spectral_ = safeSpectral(mode);
      spectral_text_->setText(strings::kFxSpectralNames[current_spectral_]);
      updateSpectralKnob();

      if (was_bipolar != isBipolarSpectral(current_spectral_))
        spectral_morph_->setValue(isBipolarSpectral(current_spectral_) ? 0.5f : 0.0f, sendNotificationSync);
      updateToolButton();

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth())
        parent->getSynth()->valueChangedInternal(id_ + "_spectral_morph_type", current_spectral_);
      if (modes_changed_callback_)
        modes_changed_callback_();
    }

    void setWarp(int mode) {
      bool was_bipolar = isBipolarWarp(current_warp_);
      current_warp_ = safeWarp(mode);
      warp_text_->setText(strings::kWarpNames[current_warp_]);
      updateWarpKnob();

      // Moving between a centred and a zero-based knob would leave a surprising amount, so go back to neutral.
      if (was_bipolar != isBipolarWarp(current_warp_))
        distortion_->setValue(isBipolarWarp(current_warp_) ? 0.5f : 0.0f, sendNotificationSync);

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth())
        parent->getSynth()->valueChangedInternal(id_ + "_distortion_type", current_warp_);
      if (modes_changed_callback_)
        modes_changed_callback_();
    }

    void setWarpFirst(bool warp_first) {
      warp_first_ = warp_first;
      swap_button_->useOnColors(warp_first_);

      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent && parent->getSynth()) {
        parent->getSynth()->valueChangedInternal(id_ + "_warp_first", warp_first_ ? 1.0f : 0.0f);
        parent->notifyChange();
      }
    }

    // The dice is a switch parameter: every flip scrambles the pattern to a new seed.
    void rollDice() {
      SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
      if (parent == nullptr || parent->getSynth() == nullptr)
        return;

      std::string name = id_ + "_random_dice";
      auto& controls = parent->getSynth()->getControls();
      auto control = controls.find(name);
      float current = control != controls.end() && control->second ? control->second->value() : 0.0f;
      parent->getSynth()->valueChangedInternal(name, current > 0.5f ? 0.0f : 1.0f);
      parent->notifyChange();
    }

    std::string id_;
    int instance_ = 0;
    std::function<void(int)> attachment_callback_;
    std::function<void(int)> table_editor_callback_;
    std::function<void()> modes_changed_callback_;
    int current_spectral_ = 0;
    int current_warp_ = 0;
    bool warp_first_ = false;
    Rectangle<int> spectral_label_bounds_;
    Rectangle<int> warp_label_bounds_;

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthSlider> spectral_morph_;
    std::unique_ptr<SynthSlider> distortion_;
    std::unique_ptr<SynthSlider> shuffle_;
    std::unique_ptr<OpenGlShapeButton> dice_button_;
    std::unique_ptr<OpenGlShapeButton> curve_button_;
    std::unique_ptr<OpenGlShapeButton> spectral_edit_button_;
    std::unique_ptr<OpenGlShapeButton> warp_tool_button_;
    std::unique_ptr<OpenGlShapeButton> warp_edit_button_;
    std::unique_ptr<OpenGlShapeButton> swap_button_;
    std::unique_ptr<ShapeButton> spectral_selector_;
    std::unique_ptr<PlainTextComponent> spectral_text_;
    std::unique_ptr<OpenGlShapeButton> prev_spectral_;
    std::unique_ptr<OpenGlShapeButton> next_spectral_;
    std::unique_ptr<ShapeButton> warp_selector_;
    std::unique_ptr<PlainTextComponent> warp_text_;
    std::unique_ptr<OpenGlShapeButton> prev_warp_;
    std::unique_ptr<OpenGlShapeButton> next_warp_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxMorphSection)
};
