// VialFX - Frequency Shifter section. Licensed under GPL-3.0-or-later.
#include "freq_shifter_section.h"

#include "fonts.h"
#include "open_gl_image_component.h"
#include "open_gl_multi_quad.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

#include <cmath>

// LED dot at the left of the All-pass pill (on = lit, off = dim).
class FreqShifterLeds : public OpenGlMultiQuad {
  public:
    FreqShifterLeds(SynthButton* button, bool on) :
        OpenGlMultiQuad(1, Shaders::kCircleFragment), button_(button), on_(on) {
      setTargetComponent(this);
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      const float w = getWidth(), h = getHeight();
      if (w <= 0.0f || h <= 0.0f || button_->getToggleState() != on_)
        return;

      Rectangle<int> bounds = button_->getBounds();
      float led = std::round(bounds.getHeight() * 0.34f);
      float x = bounds.getX() - std::round(bounds.getHeight() * 0.57f);
      float y = bounds.getY() + std::round((bounds.getHeight() - led) * 0.5f);
      setQuad(0, 2.0f * x / w - 1.0f, 1.0f - 2.0f * (y + led) / h, 2.0f * led / w, 2.0f * led / h);
      setNumQuads(1);
      setColor(on_ ? findColour(Skin::kIconButtonOn, true) : findColour(Skin::kIconButtonOff, true));
      OpenGlMultiQuad::render(open_gl, animate);
    }

  private:
    SynthButton* button_;
    bool on_;
};

FreqShifterSection::FreqShifterSection(String name, const std::string& id) : SynthSection(name), id_(id) {
  auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& param, bool bipolar) {
    slider = std::make_unique<SynthSlider>(param);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    if (bipolar) {
      slider->setBipolar();
      slider->snapToValue(true, 0.0);
    }
  };
  knob(shift_, (id_ + "_shift"), true);
  knob(frequency_, (id_ + "_frequency"), false);
  knob(period_, (id_ + "_period"), false);
  knob(tempo_, (id_ + "_tempo"), false);
  knob(transpose_, (id_ + "_keytrack_transpose"), false);
  knob(feedback_, (id_ + "_feedback"), false);
  knob(fb_direction_, (id_ + "_fb_direction"), true);
  knob(mix_, (id_ + "_dry_wet"), false);

  unit_ = std::make_unique<TextSelector>((id_ + "_unit"));
  addSlider(unit_.get());
  unit_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  unit_->setLookAndFeel(TextLookAndFeel::instance());
  unit_->setLongStringLookup(strings::kFreqShifterUnitNames);

  quality_ = std::make_unique<TextSelector>((id_ + "_quality"));
  addSlider(quality_.get());
  quality_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  quality_->setLookAndFeel(TextLookAndFeel::instance());
  quality_->setLongStringLookup(strings::kFreqShifterQualityNames);
  quality_->setTooltip("Type of filter being used:\nRough: 6-pole Hilbert, closer to analog devices\n"
                       "Normal: 12-pole Hilbert\nBetter: 18-pole Hilbert\n"
                       "Excellent: 24-pole Hilbert, with higher accuracy and more phase smearing");

  stereo_ = std::make_unique<SynthSlider>((id_ + "_stereo"));
  addSlider(stereo_.get());
  stereo_->setSliderStyle(Slider::LinearBar);
  stereo_->setBipolar();
  stereo_->snapToValue(true, 0.0);
  stereo_->setPopupPlacement(BubbleComponent::above);

  allpass_ = std::make_unique<SynthButton>((id_ + "_allpass"));
  addButton(allpass_.get());
  allpass_->setText("All-pass");
  allpass_->setNoBackground();
  allpass_->setJustification(Justification::centred);

  range_label_ = std::make_unique<PlainTextComponent>("range_label", "RANGE");
  addOpenGlComponent(range_label_.get());
  range_label_->setFontType(PlainTextComponent::kRegular);

  led_on_ = std::make_unique<FreqShifterLeds>(allpass_.get(), true);
  addOpenGlComponent(led_on_.get());
  led_off_ = std::make_unique<FreqShifterLeds>(allpass_.get(), false);
  addOpenGlComponent(led_off_.get());

  on_ = std::make_unique<SynthButton>((id_ + "_on"));
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kFreqShifter);
}

FreqShifterSection::~FreqShifterSection() { }

SynthSlider* FreqShifterSection::visibleRange() const {
  switch (static_cast<int>(unit_->getValue())) {
    case 0: return frequency_.get();
    case 1: return period_.get();
    case 5: return transpose_.get();
    default: return tempo_.get();
  }
}

void FreqShifterSection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);
  setLabelFont(g);

  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;

  // Dark panel behind the SHIFT knob.
  g.setColour(findColour(Skin::kWidgetBackground, true));
  g.fillRoundedRectangle(std::round(43.0f * ratio_x), std::round(5.0f * ratio_y), std::round(121.0f * ratio_x),
                         std::round(125.0f * ratio_y), findValue(Skin::kWidgetRoundedCorner) * 1.5f);

  // All-pass pill (button + LED zone).
  Rectangle<float> pill = allpass_->getBounds().toFloat();
  pill.setLeft(pill.getX() - std::round(pill.getHeight() * 0.95f));
  g.setColour(findColour(Skin::kLabelBackground, true));
  g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);

  drawTextComponentBackground(g, unit_->getBounds(), true);
  drawTextComponentBackground(g, quality_->getBounds(), true);
  drawLabelForComponent(g, TRANS("UNIT"), unit_.get(), true);
  drawLabelForComponent(g, TRANS("QUALITY"), quality_.get(), true);
  drawLabelForComponent(g, TRANS("SHIFT"), shift_.get());
  drawLabelBackgroundForComponent(g, visibleRange());
  // Label under the slider: the helpers place a label at the bottom of a knob-sized box, so use one that ends below the bar.
  drawLabel(g, TRANS("STEREO"), Rectangle<int>(std::round(336 * ratio_x), std::round(-9 * ratio_y),
                                               std::round(84 * ratio_x), std::round(66 * ratio_y)));
  drawLabelForComponent(g, TRANS("FEEDBACK"), feedback_.get());
  drawLabelForComponent(g, TRANS("FB DIRECTION"), fb_direction_.get());
  drawLabelForComponent(g, TRANS("MIX"), mix_.get());
}

void FreqShifterSection::layoutSlot() {
  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;
  Rectangle<int> slot(std::round(185 * ratio_x), std::round(11 * ratio_y), std::round(84 * ratio_x),
                      std::round(66 * ratio_y));
  SynthSlider* visible = visibleRange();
  for (SynthSlider* s : { frequency_.get(), period_.get(), tempo_.get(), transpose_.get() })
    s->setBounds(s == visible ? slot : Rectangle<int>());
  range_label_->setBounds(getLabelBackgroundBounds(slot));
}

void FreqShifterSection::resized() {
  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };

  shift_->setBounds(rect(62.0f, 10.0f, 84.0f, 66.0f));
  quality_->setBounds(rect(55.0f, 84.0f, 98.0f, 42.0f));
  unit_->setBounds(rect(180.0f, 88.0f, 95.0f, 42.0f));
  stereo_->setBounds(rect(304.0f, 13.0f, 148.0f, 24.0f));
  allpass_->setBounds(rect(500.0f, 13.0f, 57.0f, 24.0f));
  feedback_->setBounds(rect(290.0f, 56.0f, 84.0f, 66.0f));
  fb_direction_->setBounds(rect(384.0f, 56.0f, 84.0f, 66.0f));
  mix_->setBounds(rect(479.0f, 56.0f, 84.0f, 66.0f));
  led_on_->setBounds(getLocalBounds());
  led_off_->setBounds(getLocalBounds());

  range_label_->setColor(findColour(Skin::kBodyText, true));
  range_label_->setTextSize(findValue(Skin::kLabelHeight));

  SynthSection::resized();
  layoutSlot();
}

void FreqShifterSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  if (changed_slider == unit_.get()) {
    layoutSlot();
    repaintBackground();
  }
}

void FreqShifterSection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  layoutSlot();
  repaintBackground();
}
