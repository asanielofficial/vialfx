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
#include "load_save.h"
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

// The curve editor: a LineEditor drawn the way the LFO editor draws itself (skin line colour and width, gradient fill
// under the curve, centre line), minus the animated phase. The plain LineEditor leaves the line colour, width and
// fill unset, which is why it shows only the grid and the points.
class SpectralCurveEditor : public LineEditor {
  public:
    explicit SpectralCurveEditor(LineGenerator* source) : LineEditor(source) {
      setFill(true);
      setFillCenter(-1.0f);
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      setGlPositions();
      renderGrid(open_gl, animate);

      setLineWidth(findValue(Skin::kWidgetLineWidth));
      setFillCenter(-1.0f);

      Colour fill_color = findColour(Skin::kWidgetSecondary1, true);
      float fill_fade = findValue(Skin::kWidgetFillFade);
      Colour fill_color_fade = fill_color.withMultipliedAlpha(1.0f - fill_fade);

      setIndex(0);
      setBoostAmount(0.0f);
      setFillBoostAmount(0.0f);
      setFill(true);
      setColor(findColour(Skin::kWidgetPrimary1, true));
      setFillColors(fill_color_fade, fill_color);
      drawLines(open_gl, false);

      setFill(false);
      setColor(findColour(Skin::kWidgetCenterLine, true));
      drawLines(open_gl, false);

      renderPoints(open_gl, animate);
      renderCorners(open_gl, animate);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCurveEditor)
};

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
    static constexpr int kDefaultGridSizeX = 8;
    static constexpr int kDefaultGridSizeY = 4;

    SpectralWarpEditor(vital::SoundEngine* engine) :
        SynthSection("spectral_warp_editor"), engine_(engine) {

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

      for (auto* button : { &save_button_, &open_button_ }) {
        *button = std::make_unique<OpenGlToggleButton>(button == &save_button_ ? "Save" : "Open");
        (*button)->setText(button == &save_button_ ? "Save" : "Open");
        (*button)->setUiButton(false);
        (*button)->addListener(this);
        addAndMakeVisible(button->get());
        addOpenGlComponent((*button)->getGlComponent());
      }

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

      static const char* kTop[kNumCurves] = { "+48 dB", "6 turns", "100%" };
      static const char* kBottom[kNumCurves] = { "-48 dB", "none", "0%" };
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
      paintBody(g);
      paintChildrenBackgrounds(g);

      // The same rounded pills as the LFO's top bar, and a dark well for the drawing.
      float rounding = findValue(Skin::kLabelBackgroundRounding);
      g.setColour(findColour(Skin::kTextComponentBackground, true));
      g.fillRoundedRectangle(pattern_group_.toFloat(), rounding);
      g.fillRoundedRectangle(grid_group_.toFloat(), rounding);
      for (Button* button : { reset_button_.get(), save_button_.get(), open_button_.get() })
        g.fillRoundedRectangle(button->getBounds().toFloat(), rounding);

      // The dot between the two grid sizes.
      g.setColour(findColour(Skin::kBodyText, true));
      float dot = std::max(2.0f, grid_group_.getHeight() * 0.1f);
      g.fillEllipse(grid_group_.getCentreX() - dot / 2.0f, grid_group_.getCentreY() - dot / 2.0f, dot, dot);

      g.setColour(findColour(Skin::kWidgetBackground, true).darker(0.5f));
      g.fillRoundedRectangle(draw_area_.toFloat(), rounding);
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      Colour body_text = findColour(Skin::kBodyText, true);
      for (PlainTextComponent* label : { top_label_.get(), bottom_label_.get() })
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
      save_button_->setBounds(reset_button_->getX() - margin - reset_width, y, reset_width, top_height);
      open_button_->setBounds(save_button_->getX() - margin - reset_width, y, reset_width, top_height);
      int title_x = smooth_->getRight() + margin;
      preset_selector_->setBounds(title_x, y, open_button_->getX() - margin - title_x, top_height);

      // The drawing well fills the width, with the position slider flush underneath (like the LFO's phase bar).
      int slider_y = getHeight() - margin - slider_height;
      for (auto& slider : position_)
        slider->setBounds(margin, slider_y, getWidth() - 2 * margin, slider_height);

      int editor_x = margin;
      int editor_y = y + top_height + margin;
      int editor_width = getWidth() - 2 * margin;
      int editor_height = std::max(1, slider_y - margin / 2 - editor_y);
      draw_area_ = Rectangle<int>(editor_x, editor_y, editor_width, editor_height);
      for (int l = 0; l < 2; ++l) {
        for (int c = 0; c < kNumCurves; ++c) {
          editors_[l][c]->setSizeRatio(size_ratio_);
          editors_[l][c]->setBounds(draw_area_);
        }
      }

      // Axis and range labels sit inside the well, on top of the drawing.
      int inset = margin / 2;
      int left_label_width = size_ratio_ * 60;
      int axis_y = editor_y + editor_height - text_height - inset;
      top_label_->setTextSize(text_size);
      bottom_label_->setTextSize(text_size);
      top_label_->setJustification(Justification::centredLeft);
      bottom_label_->setJustification(Justification::centredLeft);
      top_label_->setBounds(editor_x + inset, editor_y + inset, left_label_width, text_height);
      bottom_label_->setBounds(editor_x + inset, editor_y + editor_height - text_height * 2 - inset,
                               left_label_width, text_height);

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

      if (changed)
        repaintBackground();   // the pills and the drawing well are part of the cached background
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
        current_file_[lane_][curve_] = File();
        preset_selector_->setText(shape_names_[lane_][curve_]);
        editors_[lane_][curve_]->resetPositions();
        smooth_->setToggleState(editors_[lane_][curve_]->getSmooth(), dontSendNotification);
      }
      else if (clicked_button == save_button_.get())
        saveShape();
      else if (clicked_button == open_button_.get())
        openShape();
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

    // PresetSelector::Listener: step through the shape files of the open mode, or browse them with the same
    // searchable browser the LFOs use. The ready-made shapes are files too (in a "Factory" folder), next to the user's.
    void prevClicked() override { stepShape(-1); }
    void nextClicked() override { stepShape(1); }

    void textMouseDown(const MouseEvent& e) override {
      static constexpr int kBrowserWidth = 500;
      static constexpr int kBrowserHeight = 250;

      ensureFactoryShapes();
      int browser_width = std::min(static_cast<int>(kBrowserWidth * size_ratio_), getWidth() - 2);
      int browser_height = std::min(static_cast<int>(kBrowserHeight * size_ratio_), getHeight() - 2);
      Rectangle<int> bounds(preset_selector_->getRight() - browser_width, preset_selector_->getBottom(),
                            browser_width, browser_height);
      showPopupBrowser(this, bounds, { shapeDirectory() }, String(vital::kLfoExtensionsList),
                       "Spectral Shapes", "");
    }

    // SynthSection: the browser calls these.
    void loadFile(const File& file) override { loadShapeFile(file); }
    File getCurrentFile() override { return current_file_[lane_][curve_]; }

  private:
    struct Shape {
      const char* name;
      std::vector<std::pair<float, float>> points;  // (x, y) with y = 1 at the top of the display
    };

    // Ready-made shapes. x is the log-frequency axis (0 = 20 Hz, 1 = 20 kHz).
    static const std::vector<Shape>& shapesFor(int curve) {
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
      return curve == 0 ? kFilter : curve == 1 ? kPhaser : kFlanger;
    }

    static constexpr int kNumAxisLabels = 3;
    static const char* shapeExtension() { return "vialshape"; }

    // User shapes live in <user data>/Spectral Shapes/<Filter|Phaser|Flanger>; the ready-made ones in its Factory
    // folder (rewritten when missing).
    static File shapeDirectoryFor(int curve) {
      static const char* kFolders[kNumCurves] = { "Filter", "Phaser", "Flanger" };
      File directory = LoadSave::getUserDirectory().getChildFile("Spectral Shapes").getChildFile(kFolders[curve]);
      if (!directory.exists())
        directory.createDirectory();
      return directory;
    }

    File shapeDirectory() const { return shapeDirectoryFor(curve_); }

    void ensureFactoryShapes() {
      for (int curve = 0; curve < kNumCurves; ++curve) {
        File factory = shapeDirectoryFor(curve).getChildFile("Factory");
        if (!factory.exists())
          factory.createDirectory();
        for (const Shape& shape : shapesFor(curve)) {
          File file = factory.getChildFile(String(shape.name) + "." + shapeExtension());
          if (file.existsAsFile())
            continue;
          LineGenerator generator;
          generator.setNumPoints(static_cast<int>(shape.points.size()));
          for (int i = 0; i < static_cast<int>(shape.points.size()); ++i) {
            // (a line generator point is upside down: its value is 1 - y)
            generator.setPoint(i, { shape.points[i].first, 1.0f - shape.points[i].second });
            generator.setPower(i, 0.0f);
          }
          generator.render();
          file.replaceWithText(generator.stateToJson().dump());
        }
      }
    }

    Array<File> allShapeFiles() const {
      Array<File> files;
      shapeDirectory().findChildFiles(files, File::findFiles, true, String(vital::kLfoExtensionsList));
      files.sort();
      return files;
    }

    void stepShape(int direction) {
      ensureFactoryShapes();
      Array<File> files = allShapeFiles();
      if (files.isEmpty())
        return;
      int index = files.indexOf(current_file_[lane_][curve_]);
      index = index < 0 ? (direction > 0 ? 0 : files.size() - 1) : (index + direction + files.size()) % files.size();
      loadShapeFile(files[index]);
    }

    void saveShape() {
      FileChooser save_box("Save Shape", shapeDirectory(), String("*.") + shapeExtension());
      if (!save_box.browseForFileToSave(true))
        return;

      File choice = save_box.getResult().withFileExtension(shapeExtension());
      if (!choice.exists())
        choice.create();
      choice.replaceWithText(engine_->getSpectralCurve(lane_, curve_)->stateToJson().dump());
      current_file_[lane_][curve_] = choice;
      shape_names_[lane_][curve_] = choice.getFileNameWithoutExtension().toStdString();
      preset_selector_->setText(shape_names_[lane_][curve_]);
    }

    void openShape() {
      FileChooser open_box("Open Shape", shapeDirectory(), String(vital::kLfoExtensionsList));
      if (open_box.browseForFileToOpen())
        loadShapeFile(open_box.getResult());
    }

    void loadShapeFile(const File& file) {
      if (!file.existsAsFile())
        return;
      try {
        LineGenerator* curve = engine_->getSpectralCurve(lane_, curve_);
        curve->jsonToState(json::parse(file.loadFileAsString().toStdString()));
        curve->render();
      }
      catch (const std::exception&) {
        return;
      }
      current_file_[lane_][curve_] = file;
      shape_names_[lane_][curve_] = file.getFileNameWithoutExtension().toStdString();
      preset_selector_->setText(shape_names_[lane_][curve_]);
      editors_[lane_][curve_]->resetPositions();
      smooth_->setToggleState(editors_[lane_][curve_]->getSmooth(), dontSendNotification);
    }

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
    Rectangle<int> draw_area_;
    std::function<void(bool)> visibility_callback_;
    File current_file_[2][kNumCurves];

    std::unique_ptr<SpectralCurveEditor> editors_[2][kNumCurves];
    std::unique_ptr<OpenGlShapeButton> paint_;
    std::unique_ptr<PaintPatternSelector> paint_pattern_;
    std::unique_ptr<SynthSlider> grid_size_x_;
    std::unique_ptr<SynthSlider> grid_size_y_;
    std::unique_ptr<OpenGlShapeButton> smooth_;
    std::unique_ptr<PresetSelector> preset_selector_;
    std::string shape_names_[2][kNumCurves] = { { "Flat", "Ramp Up", "Flat 70%" }, { "Flat", "Ramp Up", "Flat 70%" } };
    std::unique_ptr<PlainTextComponent> axis_[kNumAxisLabels];
    std::unique_ptr<PlainTextComponent> top_label_;
    std::unique_ptr<PlainTextComponent> bottom_label_;
    std::unique_ptr<SynthSlider> position_[2];
    std::unique_ptr<OpenGlToggleButton> reset_button_;
    std::unique_ptr<OpenGlToggleButton> save_button_;
    std::unique_ptr<OpenGlToggleButton> open_button_;
    std::unique_ptr<OpenGlShapeButton> close_button_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralWarpEditor)
};
