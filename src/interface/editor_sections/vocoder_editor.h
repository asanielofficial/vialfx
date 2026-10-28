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

#include "open_gl_image_component.h"
#include "paths.h"
#include "synth_button.h"
#include "synth_section.h"
#include "synth_slider.h"

#include <functional>

// The Vocoder menu: the settings of the Vocode spectral mode of the main input, docked over the filters like the
// spectral curve editor. Bands, the attack and release of the band followers, the formant shift of the modulator's
// bands, how much the carrier is levelled band by band (flatten) and the consonant noise. Formant, flatten and noise
// are modulation targets; all six are host parameters.
class VocoderEditor : public SynthSection {
  public:
    static constexpr int kNumKnobs = 10;
    static constexpr int kKnobsPerRow = 5;

    VocoderEditor() : SynthSection("vocoder_editor") {
      struct KnobInfo {
        const char* parameter;
        const char* label;
        const char* prefix;
        bool bipolar;
      };
      static const KnobInfo kKnobs[kNumKnobs] = {
        { "input_vocoder_bands", "BANDS", "Bands: ", false },
        { "input_vocoder_attack", "ATTACK", "Attack: ", false },
        { "input_vocoder_release", "RELEASE", "Release: ", false },
        { "input_vocoder_formant", "FORMANT", "Formant: ", true },
        { "input_vocoder_gate", "GATE", "Gate: ", false },
        { "input_vocoder_flatten", "FLATTEN", "Flatten: ", false },
        { "input_vocoder_whiten", "WHITEN", "Whiten: ", false },
        { "input_vocoder_emphasis", "EMPHASIS", "Emphasis: ", false },
        { "input_vocoder_noise", "NOISE", "Noise: ", false },
        { "input_vocoder_level", "LEVEL", "Level: ", true }
      };

      for (int i = 0; i < kNumKnobs; ++i) {
        knobs_[i] = std::make_unique<SynthSlider>(kKnobs[i].parameter);
        addSlider(knobs_[i].get());
        knobs_[i]->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knobs_[i]->setPopupPrefix(kKnobs[i].prefix);
        knobs_[i]->setBipolar(kKnobs[i].bipolar);
        labels_[i] = kKnobs[i].label;
      }
      knobs_[0]->setMaxDecimalPlaces(0);

      close_button_ = std::make_unique<OpenGlShapeButton>("close");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(Paths::exitX());

      setSkinOverride(Skin::kLfo);
    }

    virtual ~VocoderEditor() = default;

    // Called when the menu is shown or hidden (the filters underneath hide while it is open).
    void setVisibilityCallback(std::function<void(bool)> callback) { visibility_callback_ = callback; }

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);
      paintKnobShadows(g);

      setLabelFont(g);
      g.setColour(findColour(Skin::kBodyText, true));
      int margin = getWidgetMargin();
      g.drawText("VOCODER", margin * 2, margin, getWidth() / 2, title_height_, Justification::centredLeft);
      for (int i = 0; i < kNumKnobs; ++i)
        drawLabelForComponent(g, labels_[i], knobs_[i].get());

      // Two groups, each a rounded panel with a heading: how the modulator is analysed, and how the result sounds.
      static const char* kGroups[2] = { "ANALYSIS", "SOUND" };
      for (int r = 0; r < 2; ++r) {
        Rectangle<int> area = group_bounds_[r];
        if (area.isEmpty())
          continue;
        g.setColour(findColour(Skin::kLightenScreen, true).withMultipliedAlpha(0.5f));
        g.fillRoundedRectangle(area.toFloat(), margin);
        g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.7f));
        g.drawText(kGroups[r], area.getX() + margin, area.getY() + 2, area.getWidth() / 2, group_title_height_,
                   Justification::centredLeft);
      }
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      int margin = getWidgetMargin();
      title_height_ = std::max(1, static_cast<int>(size_ratio_ * 28));
      close_button_->setBounds(getWidth() - margin - title_height_, margin, title_height_, title_height_);

      int top = margin + title_height_ + margin;
      group_title_height_ = std::max(1, static_cast<int>(size_ratio_ * 18));
      int rows = kNumKnobs / kKnobsPerRow;
      int row_height = (getHeight() - top - margin * rows) / rows;
      for (int r = 0; r < rows; ++r) {
        group_bounds_[r] = Rectangle<int>(margin, top + r * (row_height + margin), getWidth() - 2 * margin,
                                          row_height);
        std::vector<Component*> row;
        for (int i = 0; i < kKnobsPerRow; ++i)
          row.push_back(knobs_[r * kKnobsPerRow + i].get());
        Rectangle<int> knob_area = group_bounds_[r].withTrimmedTop(group_title_height_);
        placeKnobsInArea(knob_area, row);
      }
      SynthSection::resized();
      repaintBackground();
    }

    void setVisible(bool should_be_visible) override {
      bool changed = should_be_visible != isVisible();
      SynthSection::setVisible(should_be_visible);
      if (changed)
        repaintBackground();
      if (changed && visibility_callback_)
        visibility_callback_(should_be_visible);
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else
        SynthSection::buttonClicked(clicked_button);
    }

  private:
    std::unique_ptr<SynthSlider> knobs_[kNumKnobs];
    std::string labels_[kNumKnobs];
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::function<void(bool)> visibility_callback_;
    int title_height_ = 28;
    int group_title_height_ = 18;
    Rectangle<int> group_bounds_[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderEditor)
};
