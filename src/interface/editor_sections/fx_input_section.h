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

#include "synth_section.h"
#include "synth_slider.h"

// Panel for one audio input of the effect build ("INPUT" or "SIDECHAIN"), laid out like an oscillator
// panel: title on the left, the two big morph knobs on the right. The middle is left free for a display.
// The knobs are bound to parameters but do nothing yet.
class FxInputSection : public SynthSection {
  public:
    static constexpr float kKnobAreaWidthRatio = 0.38f;

    // prefix is "input" or "sidechain".
    FxInputSection(String title, const std::string& prefix) : SynthSection(title) {
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

      setSkinOverride(Skin::kOscillator);
    }

    virtual ~FxInputSection() { }

    void paintBackground(Graphics& g) override {
      if (getWidth() == 0)
        return;

      paintContainer(g);
      paintHeadingText(g);
      paintKnobShadows(g);
      paintChildrenBackgrounds(g);
      paintBorder(g);

      setLabelFont(g);
      drawLabelForComponent(g, TRANS("SPECTRAL"), spectral_morph_.get());
      drawLabelForComponent(g, TRANS("DISTORTION"), distortion_.get());
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      SynthSection::resized();

      int widget_margin = getWidgetMargin();
      int label_height = findValue(Skin::kLabelBackgroundHeight);
      int knob_width = getWidth() * kKnobAreaWidthRatio;
      int knob_y = widget_margin;
      int knob_height = getHeight() - label_height - 2 * widget_margin;

      placeKnobsInArea(Rectangle<int>(getWidth() - knob_width, knob_y, knob_width, knob_height + widget_margin),
                       { spectral_morph_.get(), distortion_.get() });
    }

  private:
    std::unique_ptr<SynthSlider> spectral_morph_;
    std::unique_ptr<SynthSlider> distortion_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxInputSection)
};
