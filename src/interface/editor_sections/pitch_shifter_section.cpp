// VialFX - Pitch Shifter section. Licensed under GPL-3.0-or-later.
#include "pitch_shifter_section.h"

#include "fonts.h"
#include "open_gl_image_component.h"
#include "open_gl_line_renderer.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

#include <cmath>

// Small picture of what the knobs do to the sound: two formant humps on a log frequency axis.
//   dim line     = where the humps would land with no correction (they ride along with the pitch)
//   bright line  = where they end up (original place, or moved by the formant knob)
class PitchShifterViewer : public OpenGlLineRenderer {
  public:
    static constexpr int kResolution = 72;

    PitchShifterViewer() : OpenGlLineRenderer(kResolution), pitch_(0.0f), formant_(0.0f), formant_on_(false) {
      setFill(true);
      setFillCenter(-1.0f);
    }

    void setState(float pitch_semitones, float formant_semitones, bool formant_on) {
      pitch_ = pitch_semitones;
      formant_ = formant_semitones;
      formant_on_ = formant_on;
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      setLineWidth(findValue(Skin::kWidgetLineWidth));
      float fill_fade = findValue(Skin::kWidgetFillFade);

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);
      setViewPort(open_gl);

      if (std::fabs(pitch_) > 0.05f) {
        drawCurve(pitch_);
        setColor(findColour(Skin::kWidgetPrimary2, true).withMultipliedAlpha(0.5f));
        setFillColors(Colours::transparentBlack, Colours::transparentBlack);
        OpenGlLineRenderer::render(open_gl, animate);
      }

      drawCurve(formant_on_ ? formant_ : 0.0f);
      Colour fill_to = findColour(Skin::kWidgetSecondary1, true);
      setColor(findColour(Skin::kWidgetPrimary1, true));
      setFillColors(fill_to.withMultipliedAlpha(1.0f - fill_fade), fill_to);
      OpenGlLineRenderer::render(open_gl, animate);
    }

  private:
    static float hump(float x, float center, float width) {
      float d = (x - center) / width;
      return std::exp(-d * d);
    }

    void drawCurve(float shift_semitones) {
      const float width = getWidth(), height = getHeight();
      const float shift_octaves = shift_semitones / 12.0f;
      for (int i = 0; i < kResolution; ++i) {
        float t = i / (kResolution - 1.0f);
        float octave = 7.0f + 6.0f * t;   // 128 Hz .. 8 kHz
        float v = 0.12f + 0.62f * hump(octave, std::log2(550.0f) + shift_octaves, 0.55f) +
                  0.50f * hump(octave, std::log2(1700.0f) + shift_octaves, 0.5f);
        setXAt(i, t * width);
        setYAt(i, height * (1.0f - 0.9f * std::min(v, 1.0f)) - 0.02f * height);
      }
    }

    float pitch_, formant_;
    bool formant_on_;
};

PitchShifterSection::PitchShifterSection(String name, const std::string& id) : SynthSection(name), id_(id) {
  mode_ = std::make_unique<TextSelector>((id_ + "_mode"));
  addSlider(mode_.get());
  mode_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  mode_->setLookAndFeel(TextLookAndFeel::instance());
  mode_->setLongStringLookup(strings::kPitchShifterModeNames);

  compensation_ = std::make_unique<TextSelector>((id_ + "_compensation"));
  addSlider(compensation_.get());
  compensation_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  compensation_->setLookAndFeel(TextLookAndFeel::instance());
  compensation_->setLongStringLookup(strings::kPitchShifterCompensationNames);

  pitch_ = std::make_unique<SynthSlider>((id_ + "_pitch"));
  addSlider(pitch_.get());
  pitch_->setSliderStyle(Slider::LinearBar);
  pitch_->setBipolar();
  pitch_->snapToValue(true, 0.0);
  pitch_->setPopupPlacement(BubbleComponent::above);

  formant_control_ = std::make_unique<SynthButton>((id_ + "_formant_control"));
  addButton(formant_control_.get());
  formant_control_->setText("Formant Control");
  formant_control_->setLookAndFeel(TextLookAndFeel::instance());

  grain_ = std::make_unique<SynthSlider>((id_ + "_grain"));
  addSlider(grain_.get());
  grain_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  resolution_ = std::make_unique<SynthSlider>((id_ + "_resolution"));
  addSlider(resolution_.get());
  resolution_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  formant_ = std::make_unique<SynthSlider>((id_ + "_formant"));
  addSlider(formant_.get());
  formant_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  formant_->setBipolar();
  formant_->snapToValue(true, 0.0);

  mix_ = std::make_unique<SynthSlider>((id_ + "_dry_wet"));
  addSlider(mix_.get());
  mix_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  pitch_text_ = std::make_unique<PlainTextComponent>("pitch_text", "+0.00 st");
  addOpenGlComponent(pitch_text_.get());
  pitch_text_->setFontType(PlainTextComponent::kRegular);

  slot_label_ = std::make_unique<PlainTextComponent>("slot_label", "GRAIN");
  addOpenGlComponent(slot_label_.get());
  slot_label_->setFontType(PlainTextComponent::kRegular);

  viewer_ = std::make_unique<PitchShifterViewer>();
  addOpenGlComponent(viewer_.get());

  on_ = std::make_unique<SynthButton>((id_ + "_on"));
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kPitchShifter);
  updateControls();
}

PitchShifterSection::~PitchShifterSection() { }

void PitchShifterSection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);
  setLabelFont(g);

  drawTextComponentBackground(g, mode_->getBounds(), true);
  drawTextComponentBackground(g, compensation_->getBounds(), true);
  drawLabelForComponent(g, TRANS("MODE"), mode_.get(), true);
  drawLabelForComponent(g, TRANS("COMPENSATION"), compensation_.get(), true);
  drawLabelBackgroundForComponent(g, grain_->getWidth() ? static_cast<Component*>(grain_.get())
                                                          : static_cast<Component*>(resolution_.get()));
  drawLabelForComponent(g, TRANS("MIX"), mix_.get());
  drawLabelForComponent(g, TRANS("FORMANT"), formant_.get());
}

void PitchShifterSection::layoutSlot() {
  const bool spectral = static_cast<int>(mode_->getValue()) == 3;
  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;
  Rectangle<int> slot(std::round(178 * ratio_x), std::round(65.5f * ratio_y),
                      std::round(84 * ratio_x), std::round(65.5f * ratio_y));
  grain_->setBounds(spectral ? Rectangle<int>() : slot);
  resolution_->setBounds(spectral ? slot : Rectangle<int>());

  slot_label_->setText(spectral ? "RESOLUTION" : "GRAIN");
  slot_label_->setBounds(getLabelBackgroundBounds(slot));
}

void PitchShifterSection::resized() {
  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };

  mode_->setBounds(rect(37.5f, 5.5f, 130.0f, 60.0f));
  compensation_->setBounds(rect(37.5f, 71.0f, 130.0f, 60.0f));
  pitch_->setBounds(rect(202.0f, 18.0f, 240.0f, 24.0f));
  pitch_text_->setBounds(rect(251.0f, 44.0f, 142.0f, 18.0f));
  formant_control_->setBounds(rect(456.0f, 18.0f, 110.0f, 26.0f));
  mix_->setBounds(rect(270.0f, 65.5f, 84.0f, 65.5f));
  viewer_->setBounds(rect(366.0f, 68.0f, 92.0f, 62.0f));
  formant_->setBounds(rect(469.0f, 65.5f, 84.0f, 65.5f));

  Colour body_text = findColour(Skin::kBodyText, true);
  pitch_text_->setColor(body_text);
  pitch_text_->setTextSize(findValue(Skin::kTextComponentFontSize));
  slot_label_->setColor(body_text);
  slot_label_->setTextSize(findValue(Skin::kLabelHeight));

  SynthSection::resized();
  layoutSlot();
}

void PitchShifterSection::updateReadout() {
  float semitones = static_cast<float>(pitch_->getValue());
  String text = (semitones >= 0.0f ? "+" : "") + String(semitones, 2) + " st";
  pitch_text_->setText(text);
  viewer_->setState(semitones, static_cast<float>(formant_->getValue()), formant_control_->getToggleState());
}

void PitchShifterSection::updateControls() {
  formant_->setActive(formant_control_->getToggleState() && isActive());
  updateReadout();
}

void PitchShifterSection::setActive(bool active) {
  SynthSection::setActive(active);
  updateControls();
}

void PitchShifterSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  if (changed_slider == mode_.get()) {
    layoutSlot();
    repaintBackground();
  }
  if (changed_slider == pitch_.get() || changed_slider == formant_.get())
    updateReadout();
}

void PitchShifterSection::buttonClicked(Button* clicked_button) {
  SynthSection::buttonClicked(clicked_button);
  if (clicked_button == formant_control_.get())
    updateControls();
}

void PitchShifterSection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  layoutSlot();
  updateControls();
  repaintBackground();
}
