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
#include "paths.h"
#include "preset_selector.h"
#include "sound_engine.h"
#include "synth_button.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

#include <functional>

// Editor for the curve of Spectral Filter, Spectral Phaser or Spectral Flanger (one component for all three, opened by
// the pencil button on a lane). It docks over the filters, laid out like an LFO editor: paint brush and pattern, grid
// sizes, smoothing and a close button along the top, the curve below, and a position slider along the bottom.
//
// The x axis is frequency on a log scale from 20 Hz to 20 kHz; what the y axis means depends on the mode. Click to
// add points, drag to move them, double-click to remove, right-click for the usual line menu.
//
// The position slider is an ordinary modulatable parameter: drag an LFO (or any source) onto it. What it moves
// depends on the mode: the frequency of the swept filter, how far the phase curve is applied, the flanger delay.
// The curves are stored per lane in the preset.
class SpectralWarpEditor : public SynthSection, public PresetSelector::Listener {
  public:
    static constexpr int kPadding = 8;
    static constexpr int kTopHeight = 28;
    static constexpr int kTextHeight = 14;
    static constexpr int kSliderHeight = 16;
    static constexpr int kNumCurves = 3;
    static constexpr int kDefaultGridSizeX = 32;
    static constexpr int kDefaultGridSizeY = 8;

    SpectralWarpEditor(vital::SoundEngine* engine) :
        SynthSection("spectral_warp_editor"), engine_(engine), body_(Shaders::kRoundedRectangleFragment) {
      addOpenGlComponent(&body_);

      for (int lane = 0; lane < 2; ++lane) {
        for (int curve = 0; curve < kNumCurves; ++curve) {
          editors_[lane][curve] = std::make_unique<SpectralCurveEditor>(engine_->getSpectralCurve(lane, curve));
          SpectralCurveEditor* editor = editors_[lane][curve].get();
          editor->setGridSizeX(kDefaultGridSizeX);
          editor->setGridSizeY(kDefaultGridSizeY);
          editor->setLoop(false);
          editor->setAllowFileLoading(false);
          editor->setPaintPattern(LfoSection::getPaintPattern(LfoSection::kDown));
          addOpenGlComponent(editor);
          addOpenGlComponent(editor->getTextEditorComponent());
          editor->setVisible(false);
        }
      }

      paint_ = std::make_unique<OpenGlShapeButton>("paint");
      paint_->useOnColors(true);
      paint_->setClickingTogglesState(true);
      addAndMakeVisible(paint_.get());
      addOpenGlComponent(paint_->getGlComponent());
      paint_->addListener(this);
      paint_->setShape(Paths::paintBrush());

      paint_pattern_ = std::make_unique<PaintPatternSelector>("paint_pattern");
      addSlider(paint_pattern_.get());
      paint_pattern_->setRange(0.0f, LfoSection::kNumPaintPatterns - 1.0f, 1.0f);
      paint_pattern_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      paint_pattern_->setStringLookup(strings::kPaintPatternNames);
      paint_pattern_->setLookAndFeel(TextLookAndFeel::instance());
      paint_pattern_->setLongStringLookup(strings::kPaintPatternNames);
      paint_pattern_->setTextHeightPercentage(0.45f);
      paint_pattern_->setActive(false);
      paint_pattern_->overrideValue(Skin::kTextComponentOffset, 0.0f);
      paint_pattern_->setValue(LfoSection::kDown);

      grid_size_x_ = makeGridSlider("spectral_grid_size_x", LineEditor::kMaxGridSizeX, kDefaultGridSizeX, "X Grid");
      grid_size_y_ = makeGridSlider("spectral_grid_size_y", LineEditor::kMaxGridSizeY, kDefaultGridSizeY, "Y Grid");

      smooth_ = std::make_unique<OpenGlShapeButton>("smooth");
      smooth_->useOnColors(true);
      smooth_->setClickingTogglesState(true);
      addAndMakeVisible(smooth_.get());
      addOpenGlComponent(smooth_->getGlComponent());
      smooth_->addListener(this);
      smooth_->setShape(Paths::halfSinCurve());

      // Like the LFO's name bar: the current shape, with arrows to step through the shapes offered for the mode.
      preset_selector_ = std::make_unique<PresetSelector>();
      addSubSection(preset_selector_.get());
      preset_selector_->addListener(this);

      help_ = std::make_unique<PlainTextComponent>("Help", "");
      addOpenGlComponent(help_.get());
      help_->setFontType(PlainTextComponent::kLight);
      help_->setJustification(Justification::centredLeft);

      const char* axis_labels[kNumAxisLabels] = { "100 Hz", "1 kHz", "10 kHz" };
      for (int i = 0; i < kNumAxisLabels; ++i) {
        axis_[i] = std::make_unique<PlainTextComponent>("Axis", axis_labels[i]);
        addOpenGlComponent(axis_[i].get());
        axis_[i]->setFontType(PlainTextComponent::kLight);
        axis_[i]->setJustification(Justification::centred);
      }

      top_label_ = std::make_unique<PlainTextComponent>("Top", "");
      addOpenGlComponent(top_label_.get());
      top_label_->setFontType(PlainTextComponent::kLight);
      top_label_->setJustification(Justification::centredRight);

      bottom_label_ = std::make_unique<PlainTextComponent>("Bottom", "");
      addOpenGlComponent(bottom_label_.get());
      bottom_label_->setFontType(PlainTextComponent::kLight);
      bottom_label_->setJustification(Justification::centredRight);

      // The position slider along the bottom. One per lane (each is a modulation target of its own).
      position_[0] = std::make_unique<SynthSlider>("input_spectral_position");
      position_[1] = std::make_unique<SynthSlider>("sidechain_spectral_position");
      for (auto& slider : position_) {
        addSlider(slider.get());
        slider->setSliderStyle(Slider::LinearBar);
        slider->setPopupPlacement(BubbleComponent::above);
        slider->setModulationPlacement(BubbleComponent::above);
        slider->setPopupPrefix("Position: ");
        slider->setDoubleClickReturnValue(true, 0.5);
        slider->setVisible(false);
      }

      reset_button_ = std::make_unique<OpenGlToggleButton>(TRANS("Reset"));
      reset_button_->setText("Reset");
      reset_button_->setUiButton(false);
      reset_button_->addListener(this);
      addAndMakeVisible(reset_button_.get());
      addOpenGlComponent(reset_button_->getGlComponent());

      close_button_ = std::make_unique<OpenGlShapeButton>("close");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(Paths::exitX());

      setSkinOverride(Skin::kLfo);
    }

    virtual ~SpectralWarpEditor() = default;

    // Called when the editor is shown or hidden (the filters underneath hide while it is open).
    void setVisibilityCallback(std::function<void(bool)> callback) { visibility_callback_ = callback; }

    // lane: 0 = main input, 1 = sidechain. curve: 0 = Spectral Filter, 1 = Spectral Phaser, 2 = Spectral Flanger.
    void showCurve(int lane, int curve) {
      lane_ = std::max(0, std::min(lane, 1));
      curve_ = std::max(0, std::min(curve, kNumCurves - 1));

      for (int l = 0; l < 2; ++l) {
        position_[l]->setVisible(l == lane_);
        for (int c = 0; c < kNumCurves; ++c)
          editors_[l][c]->setVisible(l == lane_ && c == curve_);
      }

      static const char* kNames[kNumCurves] = { "SPECTRAL FILTER", "SPECTRAL PHASER", "SPECTRAL FLANGER" };
      static const char* kHelp[kNumCurves] = {
        "Gain per frequency (middle = 0 dB). Knob centre: a swept filter; up: your curve; down: your curve flipped. Position moves the sweep.",
        "Phase rotation per frequency; notches form at half a turn. Knob centre: position scales an automatic rotation; up: your curve; down: flipped.",
        "Comb depth per frequency. Knob centre: 70% everywhere; up: your curve; down: flipped. Position sets the delay."
      };
      static const char* kTop[kNumCurves] = { "+36 dB", "full turn", "100%" };
      static const char* kBottom[kNumCurves] = { "-36 dB", "none", "0%" };
      help_->setText(String(lane_ == 0 ? "INPUT - " : "SIDECHAIN - ").toStdString() + kNames[curve_] + ".  " +
                     kHelp[curve_]);
      preset_selector_->setText(shape_names_[lane_][curve_]);
      top_label_->setText(kTop[curve_]);
      bottom_label_->setText(kBottom[curve_]);

      SpectralCurveEditor* editor = editors_[lane_][curve_].get();
      editor->setPaint(paint_->getToggleState());
      smooth_->setToggleState(editor->getSmooth(), dontSendNotification);
      editor->resetPositions();
      setVisible(true);
    }

    void paintBackground(Graphics& g) override {
      paintChildrenBackgrounds(g);

      g.setColour(findColour(Skin::kTextComponentBackground, true));
      float rounding = findValue(Skin::kLabelBackgroundRounding);
      g.fillRoundedRectangle(pattern_group_.toFloat(), rounding);
      g.fillRoundedRectangle(grid_group_.toFloat(), rounding);
            g.fillRoundedRectangle(reset_button_->getBounds().toFloat(), rounding);
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      body_.setRounding(findValue(Skin::kBodyRounding));
      body_.setColor(findColour(Skin::kBody, true));
      body_.setBounds(getLocalBounds());

      Colour body_text = findColour(Skin::kBodyText, true);
      for (PlainTextComponent* label : { help_.get(), top_label_.get(), bottom_label_.get() })
        label->setColor(body_text);
      for (auto& label : axis_)
        label->setColor(body_text);

      int margin = size_ratio_ * kPadding;
      int top_height = size_ratio_ * kTopHeight;
      int text_height = size_ratio_ * (kTextHeight + 6);
      int slider_height = size_ratio_ * kSliderHeight;
      float text_size = kTextHeight * size_ratio_;

      // Top bar: paint brush + pattern, grid, smooth ... title ... reset, close.
      int x = margin;
      int y = margin;
      paint_->setBounds(x, y, top_height, top_height);
      int pattern_width = size_ratio_ * 80;
      paint_pattern_->setPadding(margin / 2);
      paint_pattern_->setBounds(paint_->getRight(), y, pattern_width, top_height);
      pattern_group_ = Rectangle<int>(paint_->getX(), y, paint_pattern_->getRight() - paint_->getX(), top_height);

      int grid_width = size_ratio_ * 46;
      grid_size_x_->setBounds(pattern_group_.getRight() + margin, y, grid_width, top_height);
      grid_size_y_->setBounds(grid_size_x_->getRight(), y, grid_width, top_height);
      grid_group_ = Rectangle<int>(grid_size_x_->getX(), y, grid_size_y_->getRight() - grid_size_x_->getX(), top_height);
      smooth_->setBounds(grid_group_.getRight() + margin, y, top_height, top_height);

      close_button_->setBounds(getWidth() - margin - top_height, y, top_height, top_height);
      int reset_width = size_ratio_ * 70;
      reset_button_->setBounds(close_button_->getX() - margin - reset_width, y, reset_width, top_height);
      int title_x = smooth_->getRight() + margin;
      preset_selector_->setBounds(title_x, y, reset_button_->getX() - margin - title_x, top_height);

      help_->setTextSize(text_size);
      help_->setBounds(margin, y + top_height + margin / 2, getWidth() - 2 * margin, text_height);

      // Position slider along the bottom, axis labels above it.
      int slider_y = getHeight() - margin - slider_height;
      for (auto& slider : position_)
        slider->setBounds(margin, slider_y, getWidth() - 2 * margin, slider_height);

      int axis_y = slider_y - text_height - margin / 2;
      int left_label_width = size_ratio_ * 52;
      int editor_x = margin + left_label_width;
      int editor_y = help_->getBottom() + margin / 2;
      int editor_width = getWidth() - margin - editor_x;
      int editor_height = std::max(1, axis_y - editor_y);
      for (int l = 0; l < 2; ++l) {
        for (int c = 0; c < kNumCurves; ++c) {
          editors_[l][c]->setSizeRatio(size_ratio_);
          editors_[l][c]->setBounds(editor_x, editor_y, editor_width, editor_height);
        }
      }

      top_label_->setTextSize(text_size);
      bottom_label_->setTextSize(text_size);
      top_label_->setBounds(margin, editor_y, left_label_width - 6, text_height);
      bottom_label_->setBounds(margin, editor_y + editor_height - text_height, left_label_width - 6, text_height);

      // Positions of 100 Hz, 1 kHz and 10 kHz on the 20 Hz .. 20 kHz log axis.
      static const float kAxisPositions[kNumAxisLabels] = { 0.2330f, 0.5663f, 0.8997f };
      int axis_label_width = size_ratio_ * 60;
      for (int i = 0; i < kNumAxisLabels; ++i) {
        axis_[i]->setTextSize(text_size);
        int label_x = editor_x + kAxisPositions[i] * editor_width - axis_label_width / 2;
        axis_[i]->setBounds(label_x, axis_y, axis_label_width, text_height);
      }

      SynthSection::resized();
    }

    void setVisible(bool should_be_visible) override {
      bool changed = should_be_visible != isVisible();
      SynthSection::setVisible(should_be_visible);

      if (should_be_visible) {
        Image image(Image::ARGB, 1, 1, false);
        Graphics g(image);
        paintOpenGlChildrenBackgrounds(g);
      }
      if (changed && visibility_callback_)
        visibility_callback_(should_be_visible);
    }

    void sliderValueChanged(Slider* changed_slider) override {
      SynthSection::sliderValueChanged(changed_slider);

      if (changed_slider == grid_size_x_.get()) {
        for (auto& lane : editors_)
          for (auto& editor : lane)
            editor->setGridSizeX(grid_size_x_->getValue());
      }
      else if (changed_slider == grid_size_y_.get()) {
        for (auto& lane : editors_)
          for (auto& editor : lane)
            editor->setGridSizeY(grid_size_y_->getValue());
      }
      else if (changed_slider == paint_pattern_.get()) {
        for (auto& lane : editors_)
          for (auto& editor : lane)
            editor->setPaintPattern(LfoSection::getPaintPattern(static_cast<int>(paint_pattern_->getValue())));
      }
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else if (clicked_button == reset_button_.get()) {
        engine_->resetSpectralCurve(lane_, curve_);
        static const char* kDefaultNames[kNumCurves] = { "Flat", "Ramp Up", "Flat 70%" };
        shape_names_[lane_][curve_] = kDefaultNames[curve_];
        shape_index_[lane_][curve_] = 0;
        preset_selector_->setText(shape_names_[lane_][curve_]);
        editors_[lane_][curve_]->resetPositions();
        smooth_->setToggleState(editors_[lane_][curve_]->getSmooth(), dontSendNotification);
      }
      else if (clicked_button == paint_.get()) {
        for (auto& lane : editors_)
          for (auto& editor : lane)
            editor->setPaint(paint_->getToggleState());
      }
      else if (clicked_button == smooth_.get())
        editors_[lane_][curve_]->setSmooth(smooth_->getToggleState());
      else
        SynthSection::buttonClicked(clicked_button);
    }

    // PresetSelector::Listener: step through, or pick from a list of, ready-made shapes for the open mode.
    void prevClicked() override { applyShape((shape_index_[lane_][curve_] + numShapes() - 1) % numShapes()); }
    void nextClicked() override { applyShape((shape_index_[lane_][curve_] + 1) % numShapes()); }

    void textMouseDown(const MouseEvent& e) override {
      PopupItems options;
      for (int i = 0; i < numShapes(); ++i)
        options.addItem(i, shapes()[i].name);

      Point<int> position(preset_selector_->getX(), preset_selector_->getBottom());
      showPopupSelector(this, position, options, [=](int selection) { applyShape(selection); });
    }

  private:
    struct Shape {
      const char* name;
      std::vector<std::pair<float, float>> points;  // (x, y) with y = 1 at the top of the display
    };

    // Ready-made shapes. x is the log-frequency axis (0 = 20 Hz, 1 = 20 kHz).
    const std::vector<Shape>& shapes() const {
      static const std::vector<Shape> kFilter = {
        { "Flat", { { 0.0f, 0.5f }, { 1.0f, 0.5f } } },
        { "Low Pass", { { 0.0f, 0.5f }, { 0.5f, 0.5f }, { 0.72f, 0.0f }, { 1.0f, 0.0f } } },
        { "High Pass", { { 0.0f, 0.0f }, { 0.28f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 0.5f } } },
        { "Band Pass", { { 0.0f, 0.1f }, { 0.36f, 0.1f }, { 0.5f, 0.8f }, { 0.64f, 0.1f }, { 1.0f, 0.1f } } },
        { "Notch", { { 0.0f, 0.5f }, { 0.44f, 0.5f }, { 0.5f, 0.02f }, { 0.56f, 0.5f }, { 1.0f, 0.5f } } },
        { "Tilt Down", { { 0.0f, 0.8f }, { 1.0f, 0.2f } } },
        { "Tilt Up", { { 0.0f, 0.2f }, { 1.0f, 0.8f } } },
        { "Scoop", { { 0.0f, 0.7f }, { 0.5f, 0.2f }, { 1.0f, 0.7f } } }
      };
      static const std::vector<Shape> kPhaser = {
        { "Ramp Up", { { 0.0f, 0.0f }, { 1.0f, 1.0f } } },
        { "Ramp Down", { { 0.0f, 1.0f }, { 1.0f, 0.0f } } },
        { "Half Turn", { { 0.0f, 0.5f }, { 1.0f, 0.5f } } },
        { "Dome", { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } } },
        { "Zigzag", { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 0.5f, 0.0f }, { 0.75f, 1.0f }, { 1.0f, 0.0f } } },
        { "Steps", { { 0.0f, 0.2f }, { 0.33f, 0.2f }, { 0.34f, 0.5f }, { 0.66f, 0.5f }, { 0.67f, 0.9f }, { 1.0f, 0.9f } } }
      };
      static const std::vector<Shape> kFlanger = {
        { "Flat 70%", { { 0.0f, 0.7f }, { 1.0f, 0.7f } } },
        { "Full", { { 0.0f, 1.0f }, { 1.0f, 1.0f } } },
        { "Low Heavy", { { 0.0f, 1.0f }, { 1.0f, 0.2f } } },
        { "High Heavy", { { 0.0f, 0.2f }, { 1.0f, 1.0f } } },
        { "Dip", { { 0.0f, 0.9f }, { 0.5f, 0.1f }, { 1.0f, 0.9f } } },
        { "Alternating", { { 0.0f, 1.0f }, { 0.2f, 0.1f }, { 0.4f, 1.0f }, { 0.6f, 0.1f }, { 0.8f, 1.0f }, { 1.0f, 0.1f } } }
      };
      return curve_ == 0 ? kFilter : curve_ == 1 ? kPhaser : kFlanger;
    }

    int numShapes() const { return static_cast<int>(shapes().size()); }

    void applyShape(int index) {
      const Shape& shape = shapes()[index];
      LineGenerator* curve = engine_->getSpectralCurve(lane_, curve_);
      curve->setNumPoints(static_cast<int>(shape.points.size()));
      for (int i = 0; i < static_cast<int>(shape.points.size()); ++i) {
        // (a line generator point is upside down: its value is 1 - y)
        curve->setPoint(i, { shape.points[i].first, 1.0f - shape.points[i].second });
        curve->setPower(i, 0.0f);
      }
      curve->render();
      shape_index_[lane_][curve_] = index;
      shape_names_[lane_][curve_] = shape.name;
      preset_selector_->setText(shape.name);
      editors_[lane_][curve_]->resetPositions();
    }

    static constexpr int kNumAxisLabels = 3;

    std::unique_ptr<SynthSlider> makeGridSlider(const std::string& name, int max_value, int default_value,
                                                const std::string& prefix) {
      auto slider = std::make_unique<SynthSlider>(name);
      slider->setRange(1.0, max_value, 1.0);
      slider->setValue(default_value);
      slider->setLookAndFeel(TextLookAndFeel::instance());
      slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      addSlider(slider.get());
      slider->setDoubleClickReturnValue(true, default_value);
      slider->setMaxDecimalPlaces(0);
      slider->setSensitivity(0.2f);
      slider->overrideValue(Skin::kTextComponentOffset, 0.0f);
      slider->setTextHeightPercentage(0.6f);
      slider->setPopupPrefix(prefix);
      return slider;
    }

    vital::SoundEngine* engine_;
    int lane_ = 0;
    int curve_ = 0;
    Rectangle<int> pattern_group_;
    Rectangle<int> grid_group_;
    std::function<void(bool)> visibility_callback_;

    OpenGlQuad body_;
    std::unique_ptr<SpectralCurveEditor> editors_[2][kNumCurves];
    std::unique_ptr<OpenGlShapeButton> paint_;
    std::unique_ptr<PaintPatternSelector> paint_pattern_;
    std::unique_ptr<SynthSlider> grid_size_x_;
    std::unique_ptr<SynthSlider> grid_size_y_;
    std::unique_ptr<OpenGlShapeButton> smooth_;
    std::unique_ptr<PresetSelector> preset_selector_;
    std::string shape_names_[2][kNumCurves] = { { "Flat", "Ramp Up", "Flat 70%" }, { "Flat", "Ramp Up", "Flat 70%" } };
    int shape_index_[2][kNumCurves] = { { 0, 0, 0 }, { 0, 0, 0 } };
    std::unique_ptr<PlainTextComponent> help_;
    std::unique_ptr<PlainTextComponent> axis_[kNumAxisLabels];
    std::unique_ptr<PlainTextComponent> top_label_;
    std::unique_ptr<PlainTextComponent> bottom_label_;
    std::unique_ptr<SynthSlider> position_[2];
    std::unique_ptr<OpenGlToggleButton> reset_button_;
    std::unique_ptr<OpenGlShapeButton> close_button_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralWarpEditor)
};
