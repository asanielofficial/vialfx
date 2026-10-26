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

#include "lfo_section.h"
#include "line_editor.h"
#include "open_gl_image_component.h"
#include "overlay.h"
#include "paths.h"
#include "sound_engine.h"
#include "synth_button.h"
#include "synth_slider.h"

// Popup for drawing the curve of Spectral Filter, Spectral Phaser or Spectral Flanger (one component for all three,
// opened by the pencil button on a lane). The x axis is frequency on a log scale from 20 Hz to 20 kHz; what the y
// axis means depends on the mode. Click to add points, drag to move them, double-click to remove, right-click for
// the usual line menu; the brush button switches to freehand painting. The curves are stored per lane in the
// preset.
class SpectralWarpEditor : public Overlay {
  public:
    static constexpr int kWidth = 760;
    static constexpr int kHeight = 440;
    static constexpr int kPadding = 20;
    static constexpr int kSideWidth = 130;
    static constexpr int kTextHeight = 14;
    static constexpr int kButtonHeight = 28;
    static constexpr int kNumCurves = 3;

    SpectralWarpEditor(vital::SoundEngine* engine) :
        Overlay("spectral_warp_editor"), engine_(engine), body_(Shaders::kRoundedRectangleFragment) {
      addOpenGlComponent(&body_);

      for (int lane = 0; lane < 2; ++lane) {
        for (int curve = 0; curve < kNumCurves; ++curve) {
          editors_[lane][curve] = std::make_unique<LineEditor>(engine_->getSpectralCurve(lane, curve));
          LineEditor* editor = editors_[lane][curve].get();
          editor->setGridSizeX(32);
          editor->setGridSizeY(8);
          editor->setLoop(false);
          editor->setAllowFileLoading(false);
          editor->setPaintPattern(LfoSection::getPaintPattern(LfoSection::kStep));
          addOpenGlComponent(editor);
          addOpenGlComponent(editor->getTextEditorComponent());
          editor->setVisible(false);
        }
      }

      title_ = std::make_unique<PlainTextComponent>("Title", "");
      addOpenGlComponent(title_.get());
      title_->setFontType(PlainTextComponent::kTitle);
      title_->setTextSize(kButtonHeight * 0.7f);
      title_->setJustification(Justification::centredLeft);

      help_ = std::make_unique<PlainTextComponent>("Help", "");
      addOpenGlComponent(help_.get());
      help_->setFontType(PlainTextComponent::kLight);
      help_->setTextSize(kTextHeight);
      help_->setJustification(Justification::centredLeft);

      const char* axis_labels[kNumAxisLabels] = { "100 Hz", "1 kHz", "10 kHz" };
      for (int i = 0; i < kNumAxisLabels; ++i) {
        axis_[i] = std::make_unique<PlainTextComponent>("Axis", axis_labels[i]);
        addOpenGlComponent(axis_[i].get());
        axis_[i]->setFontType(PlainTextComponent::kLight);
        axis_[i]->setTextSize(kTextHeight);
        axis_[i]->setJustification(Justification::centred);
      }

      top_label_ = std::make_unique<PlainTextComponent>("Top", "");
      addOpenGlComponent(top_label_.get());
      top_label_->setFontType(PlainTextComponent::kLight);
      top_label_->setTextSize(kTextHeight);
      top_label_->setJustification(Justification::centredRight);

      bottom_label_ = std::make_unique<PlainTextComponent>("Bottom", "");
      addOpenGlComponent(bottom_label_.get());
      bottom_label_->setFontType(PlainTextComponent::kLight);
      bottom_label_->setTextSize(kTextHeight);
      bottom_label_->setJustification(Justification::centredRight);

      rate_label_ = std::make_unique<PlainTextComponent>("Rate Label", "RATE");
      addOpenGlComponent(rate_label_.get());
      rate_label_->setFontType(PlainTextComponent::kLight);
      rate_label_->setTextSize(kTextHeight);
      rate_label_->setJustification(Justification::centred);

      rate_[0] = std::make_unique<SynthSlider>("input_spectral_rate");
      rate_[1] = std::make_unique<SynthSlider>("sidechain_spectral_rate");
      for (auto& slider : rate_) {
        addSlider(slider.get());
        slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider->setPopupPrefix("Rate: ");
        slider->setVisible(false);
      }

      paint_ = std::make_unique<OpenGlShapeButton>("paint");
      paint_->useOnColors(true);
      paint_->setClickingTogglesState(true);
      addAndMakeVisible(paint_.get());
      addOpenGlComponent(paint_->getGlComponent());
      paint_->addListener(this);
      paint_->setShape(Paths::paintBrush());

      reset_button_ = std::make_unique<OpenGlToggleButton>(TRANS("Reset"));
      reset_button_->setText("Reset curve");
      reset_button_->setUiButton(false);
      reset_button_->addListener(this);
      addAndMakeVisible(reset_button_.get());
      addOpenGlComponent(reset_button_->getGlComponent());

      close_button_ = std::make_unique<OpenGlToggleButton>(TRANS("Done"));
      close_button_->setText("Done");
      close_button_->setUiButton(true);
      close_button_->addListener(this);
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
    }

    virtual ~SpectralWarpEditor() = default;

    // lane: 0 = main input, 1 = sidechain. curve: 0 = Spectral Filter, 1 = Spectral Phaser, 2 = Spectral Flanger.
    void showCurve(int lane, int curve) {
      lane_ = std::max(0, std::min(lane, 1));
      curve_ = std::max(0, std::min(curve, kNumCurves - 1));

      for (int l = 0; l < 2; ++l) {
        rate_[l]->setVisible(l == lane_);
        for (int c = 0; c < kNumCurves; ++c)
          editors_[l][c]->setVisible(l == lane_ && c == curve_);
      }

      static const char* kNames[kNumCurves] = { "SPECTRAL FILTER", "SPECTRAL PHASER", "SPECTRAL FLANGER" };
      static const char* kHelp[kNumCurves] = {
        "Gain per frequency. The middle is 0 dB (flat). Morph 0: a swept filter. Morph 1: your curve.",
        "Phase rotation per frequency; notches form at half a turn. Morph 0: automatic sweep. Morph 1: your curve.",
        "Comb depth per frequency. Morph 0: 70% everywhere. Morph 1: your curve. The rate sweeps the delay."
      };
      static const char* kTop[kNumCurves] = { "+36 dB", "full turn", "100%" };
      static const char* kBottom[kNumCurves] = { "-36 dB", "none", "0%" };
      title_->setText(String(lane_ == 0 ? "INPUT - " : "SIDECHAIN - ").toStdString() + kNames[curve_]);
      help_->setText(kHelp[curve_]);
      top_label_->setText(kTop[curve_]);
      bottom_label_->setText(kBottom[curve_]);

      editors_[lane_][curve_]->setPaint(paint_->getToggleState());
      editors_[lane_][curve_]->resetPositions();
      setVisible(true);
    }

    void resized() override {
      body_.setRounding(findValue(Skin::kBodyRounding));
      body_.setColor(findColour(Skin::kBody, true));

      Colour body_text = findColour(Skin::kBodyText, true);
      title_->setColor(body_text);
      help_->setColor(body_text);
      top_label_->setColor(body_text);
      bottom_label_->setColor(body_text);
      rate_label_->setColor(body_text);
      for (auto& label : axis_)
        label->setColor(body_text);

      Rectangle<int> rect = getBodyRect();
      body_.setBounds(rect);

      int padding = size_ratio_ * kPadding;
      int text_height = size_ratio_ * (kTextHeight + 6);
      int button_height = size_ratio_ * kButtonHeight;
      int side_width = size_ratio_ * kSideWidth;
      float text_size = kTextHeight * size_ratio_;

      title_->setTextSize(button_height * 0.7f);
      title_->setBounds(rect.getX() + padding, rect.getY() + padding, rect.getWidth() - 2 * padding, button_height);
      help_->setTextSize(text_size);
      help_->setBounds(rect.getX() + padding, title_->getBottom(), rect.getWidth() - 2 * padding, text_height);

      int axis_label_width = size_ratio_ * 60;
      int left_label_width = size_ratio_ * 52;
      int editor_x = rect.getX() + padding + left_label_width;
      int editor_y = help_->getBottom() + padding / 2;
      int editor_width = rect.getRight() - padding - side_width - editor_x;
      int editor_height = rect.getBottom() - padding - text_height - editor_y;
      for (int l = 0; l < 2; ++l) {
        for (int c = 0; c < kNumCurves; ++c) {
          editors_[l][c]->setSizeRatio(size_ratio_);
          editors_[l][c]->setBounds(editor_x, editor_y, editor_width, editor_height);
        }
      }

      top_label_->setTextSize(text_size);
      bottom_label_->setTextSize(text_size);
      top_label_->setBounds(rect.getX() + padding, editor_y, left_label_width - 6, text_height);
      bottom_label_->setBounds(rect.getX() + padding, editor_y + editor_height - text_height,
                               left_label_width - 6, text_height);

      // Positions of 100 Hz, 1 kHz and 10 kHz on the 20 Hz .. 20 kHz log axis.
      static const float kAxisPositions[kNumAxisLabels] = { 0.2330f, 0.5663f, 0.8997f };
      for (int i = 0; i < kNumAxisLabels; ++i) {
        axis_[i]->setTextSize(text_size);
        int x = editor_x + kAxisPositions[i] * editor_width - axis_label_width / 2;
        axis_[i]->setBounds(x, editor_y + editor_height + 2, axis_label_width, text_height);
      }

      int side_x = rect.getRight() - padding - side_width;
      int knob_size = std::min(side_width, static_cast<int>(size_ratio_ * 80));
      rate_label_->setTextSize(text_size);
      rate_label_->setBounds(side_x, editor_y, side_width, text_height);
      for (auto& slider : rate_)
        slider->setBounds(side_x + (side_width - knob_size) / 2, rate_label_->getBottom() + 4, knob_size, knob_size);

      int button_y = rate_[0]->getBottom() + padding;
      paint_->setBounds(side_x, button_y, button_height, button_height);
      reset_button_->setBounds(side_x, paint_->getBottom() + padding / 2, side_width, button_height);
      close_button_->setBounds(side_x, rect.getBottom() - padding - button_height, side_width, button_height);

      Overlay::resized();
    }

    void setVisible(bool should_be_visible) override {
      Overlay::setVisible(should_be_visible);

      if (should_be_visible) {
        Image image(Image::ARGB, 1, 1, false);
        Graphics g(image);
        paintOpenGlChildrenBackgrounds(g);
      }
    }

    void mouseUp(const MouseEvent& e) override {
      if (!getBodyRect().contains(e.getPosition()))
        setVisible(false);
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else if (clicked_button == reset_button_.get()) {
        engine_->resetSpectralCurve(lane_, curve_);
        editors_[lane_][curve_]->resetPositions();
      }
      else if (clicked_button == paint_.get())
        editors_[lane_][curve_]->setPaint(paint_->getToggleState());
      else
        Overlay::buttonClicked(clicked_button);
    }

    Rectangle<int> getBodyRect() {
      int width = std::min(static_cast<int>(kWidth * size_ratio_), getWidth());
      int height = std::min(static_cast<int>(kHeight * size_ratio_), getHeight());
      return Rectangle<int>((getWidth() - width) / 2, (getHeight() - height) / 2, width, height);
    }

  private:
    static constexpr int kNumAxisLabels = 3;

    vital::SoundEngine* engine_;
    int lane_ = 0;
    int curve_ = 0;

    OpenGlQuad body_;
    std::unique_ptr<LineEditor> editors_[2][kNumCurves];
    std::unique_ptr<PlainTextComponent> title_;
    std::unique_ptr<PlainTextComponent> help_;
    std::unique_ptr<PlainTextComponent> axis_[kNumAxisLabels];
    std::unique_ptr<PlainTextComponent> top_label_;
    std::unique_ptr<PlainTextComponent> bottom_label_;
    std::unique_ptr<PlainTextComponent> rate_label_;
    std::unique_ptr<SynthSlider> rate_[2];
    std::unique_ptr<OpenGlShapeButton> paint_;
    std::unique_ptr<OpenGlToggleButton> reset_button_;
    std::unique_ptr<OpenGlToggleButton> close_button_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralWarpEditor)
};
