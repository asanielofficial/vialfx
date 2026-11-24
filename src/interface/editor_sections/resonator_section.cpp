// VialFX - Resonator section. Licensed under GPL-3.0-or-later.
#include "resonator_section.h"

#include "fonts.h"
#include "open_gl_line_renderer.h"
#include "paths.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

#include <algorithm>
#include <cmath>

// Where the resonator rings: the peaks of the chosen style at the Tune pitch (C3 + Tune), on a log frequency axis.
class ResonatorPeaksViewer : public OpenGlLineRenderer {
  public:
    static constexpr int kResolution = 200;

    ResonatorPeaksViewer() : OpenGlLineRenderer(kResolution), style_(nullptr), tune_(nullptr), resonance_(nullptr),
                             stretch_(nullptr) {
      setFill(true);
      setFillCenter(-1.0f);
    }

    void setControls(TextSelector* style, SynthSlider* tune, SynthSlider* resonance, SynthSlider* stretch) {
      style_ = style; tune_ = tune; resonance_ = resonance; stretch_ = stretch;
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      setLineWidth(findValue(Skin::kWidgetLineWidth) * 1.6f);
      float fill_fade = findValue(Skin::kWidgetFillFade);

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);
      setViewPort(open_gl);

      drawCurve();
      Colour fill_to = findColour(Skin::kWidgetSecondary1, true);
      setColor(findColour(Skin::kWidgetPrimary1, true));
      setFillColors(fill_to.withMultipliedAlpha(1.0f - fill_fade), fill_to);
      OpenGlLineRenderer::render(open_gl, animate);
    }

  private:
    static constexpr float kLowHz = 40.0f, kHighHz = 18000.0f;

    float xOf(float hz) const { return std::log(hz / kLowHz) / std::log(kHighHz / kLowHz); }

    void drawCurve() {
      const float width = getWidth(), height = getHeight();
      if (!style_ || width <= 0.0f)
        return;
      const int style = static_cast<int>(style_->getValue());
      const float resonance = static_cast<float>(resonance_->getValue());
      const float stretch = static_cast<float>(stretch_->getValue());
      const float f0 = 130.81f * std::pow(2.0f, static_cast<float>(tune_->getValue()) / 12.0f);
      const float sigma = 0.006f + 0.03f * (1.0f - resonance);       // peak width on the 0..1 axis

      struct Peak { float x, h, s; };
      std::vector<Peak> peaks;
      if (style == 0 || style == 2) {
        const int count = style == 0 ? 60 : 40;
        for (int k = 1; k <= count; ++k) {
          float hz = f0 * k * (1.0f + 0.002f * stretch * k);
          if (hz >= kHighHz) break;
          float h = style == 0 ? std::pow(static_cast<float>(k), -0.25f) * (k % 2 ? 1.0f : 0.8f)
                               : std::pow(static_cast<float>(k), -0.8f);
          peaks.push_back({ xOf(hz), h, style == 0 ? sigma : sigma * 0.7f });
        }
      }
      else if (style == 1) {
        static const float ratios[10] = { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.64f, 24.82f, 32.9f, 41.8f, 51.7f };
        for (int k = 0; k < 10; ++k) {
          float hz = f0 * ratios[k] * (1.0f + 0.03f * stretch * k);
          if (hz >= kHighHz) break;
          peaks.push_back({ xOf(hz), std::pow(static_cast<float>(k + 1), -0.45f), sigma * 1.3f });
        }
      }
      else {
        static const float formants[5] = { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4500.0f };
        static const float gains[5] = { 1.0f, 0.8f, 0.55f, 0.4f, 0.25f };
        const float shift = std::pow(f0 / 130.81f, 0.75f);
        for (int k = 0; k < 5; ++k)
          peaks.push_back({ xOf(formants[k] * shift * (1.0f + 0.25f * stretch * k / 4.0f)), gains[k], 0.03f + sigma });
      }

      for (int i = 0; i < kResolution; ++i) {
        float x = i / (kResolution - 1.0f), sum = 0.0f;
        for (const Peak& p : peaks) {
          float d = (x - p.x) / p.s;
          sum += p.h * std::exp(-d * d);
        }
        sum = std::min(sum, 1.0f);
        setXAt(i, width * x);
        setYAt(i, height * (0.92f - 0.80f * sum));
      }
    }

    TextSelector* style_;
    SynthSlider *tune_, *resonance_, *stretch_;
};

// The colour tilt as a gentle slope: dark on the left, bright on the right (or the other way round).
class ResonatorTiltViewer : public OpenGlLineRenderer {
  public:
    static constexpr int kResolution = 80;

    ResonatorTiltViewer() : OpenGlLineRenderer(kResolution), color_(nullptr) {
      setFill(true);
      setFillCenter(-1.0f);
    }

    void setColorSlider(SynthSlider* color) { color_ = color; }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      setLineWidth(findValue(Skin::kWidgetLineWidth) * 1.6f);
      float fill_fade = findValue(Skin::kWidgetFillFade);

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);
      setViewPort(open_gl);

      const float width = getWidth(), height = getHeight();
      const float color = color_ ? static_cast<float>(color_->getValue()) : 0.0f;
      for (int i = 0; i < kResolution; ++i) {
        float x = i / (kResolution - 1.0f);
        float s = 1.0f / (1.0f + std::exp(-(x - 0.4f) * 9.0f));
        float level = 0.5f + 0.34f * color * (2.0f * s - 1.0f);   // + color lifts the highs, lowers the lows
        setXAt(i, width * x);
        setYAt(i, height * (1.0f - level));
      }
      Colour fill_to = findColour(Skin::kWidgetSecondary1, true);
      setColor(findColour(Skin::kWidgetPrimary1, true));
      setFillColors(fill_to.withMultipliedAlpha(1.0f - fill_fade), fill_to);
      OpenGlLineRenderer::render(open_gl, animate);
    }

  private:
    SynthSlider* color_;
};

namespace {
  constexpr float kRefW = 580.0f, kRefH = 136.5f;   // the standard section size
}

ResonatorSection::ResonatorSection(String name, const std::string& id) : SynthSection(name), id_(id) {
  auto selector = [&](std::unique_ptr<TextSelector>& s, const std::string& param, const std::string* names) {
    s = std::make_unique<TextSelector>(param);
    addSlider(s.get());
    s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    s->setLookAndFeel(TextLookAndFeel::instance());
    s->setLongStringLookup(names);
  };
  selector(style_, (id_ + "_style"), strings::kResonatorStyleNames);
  selector(keytrack_, (id_ + "_keytrack"), strings::kResonatorKeytrackNames);
  selector(voices_, (id_ + "_voices"), strings::kResonatorVoiceNames);
  style_->setTooltip("Comb: feedback comb, partials at whole multiples\nModal: ten bar-like partials\n"
                     "String: comb with a pick-position notch\nFormant: five vowel-like bands that follow the pitch");
  keytrack_->setTooltip("Off: pitch from Tune and Fine (C3)\nMIDI: follow incoming notes, bend, slide and pressure");
  voices_->setTooltip("How many notes can ring at once. Each MPE finger gets its own voice.");

  auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& param, bool bipolar) {
    slider = std::make_unique<SynthSlider>(param);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    if (bipolar) {
      slider->setBipolar();
      slider->snapToValue(true, 0.0);
    }
  };
  knob(tune_, (id_ + "_tune"), true);
  knob(fine_, (id_ + "_fine"), true);
  knob(slide_, (id_ + "_slide"), false);
  knob(pressure_, (id_ + "_pressure"), false);
  knob(resonance_, (id_ + "_resonance"), false);
  knob(mix_, (id_ + "_mix"), false);
  knob(damping_, (id_ + "_damping"), false);
  knob(stretch_, (id_ + "_stretch"), true);

  color_ = std::make_unique<SynthSlider>((id_ + "_color"));
  addSlider(color_.get());
  color_->setSliderStyle(Slider::LinearBar);
  color_->setBipolar();
  color_->snapToValue(true, 0.0);
  color_->setPopupPlacement(BubbleComponent::above);
  color_->setTooltip("Color: tilts the ringing darker (left) or brighter (right)");

  peaks_ = std::make_unique<ResonatorPeaksViewer>();
  peaks_->setControls(style_.get(), tune_.get(), resonance_.get(), stretch_.get());
  addOpenGlComponent(peaks_.get());

  tilt_ = std::make_unique<ResonatorTiltViewer>();
  tilt_->setColorSlider(color_.get());
  addOpenGlComponent(tilt_.get());

  settings_ = std::make_unique<OpenGlShapeButton>("Resonator Settings");
  addAndMakeVisible(settings_.get());
  addOpenGlComponent(settings_->getGlComponent());
  settings_->addListener(this);
  settings_->setShape(Paths::gear());
  settings_->setTooltip("Settings: tuning, MPE, bend range, glide, slide and pressure targets");

  on_ = std::make_unique<SynthButton>((id_ + "_on"));
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kResonator);
}

ResonatorSection::~ResonatorSection() { }

void ResonatorSection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);
  setLabelFont(g);

  const float ratio_x = getWidth() / kRefW, ratio_y = getHeight() / kRefH;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<float>(std::round(x * ratio_x), std::round(y * ratio_y), std::round(w * ratio_x), std::round(h * ratio_y));
  };
  g.setColour(findColour(Skin::kWidgetBackground, true));
  const float corner = findValue(Skin::kWidgetRoundedCorner) * 1.5f;
  g.fillRoundedRectangle(rect(219.0f, 6.0f, 118.0f, 60.0f), corner);
  g.fillRoundedRectangle(rect(219.0f, 71.0f, 118.0f, 36.0f), corner);

  drawTextComponentBackground(g, style_->getBounds(), true);
  drawTextComponentBackground(g, keytrack_->getBounds(), true);
  drawTextComponentBackground(g, voices_->getBounds(), true);
  drawLabelForComponent(g, TRANS("STYLE"), style_.get(), true);
  drawLabelForComponent(g, TRANS("KEY TRACK"), keytrack_.get(), true);
  drawLabelForComponent(g, TRANS("VOICES"), voices_.get(), true);
  drawLabelForComponent(g, TRANS("TUNE"), tune_.get());
  drawLabelForComponent(g, TRANS("FINE"), fine_.get());
  drawLabelForComponent(g, TRANS("SLIDE"), slide_.get());
  drawLabelForComponent(g, TRANS("PRESSURE"), pressure_.get());
  drawLabelForComponent(g, TRANS("RESONANCE"), resonance_.get());
  drawLabelForComponent(g, TRANS("MIX"), mix_.get());
  drawLabelForComponent(g, TRANS("DAMPING"), damping_.get());
  drawLabelForComponent(g, TRANS("STRETCH"), stretch_.get());
}

void ResonatorSection::resized() {
  const float ratio_x = getWidth() / kRefW, ratio_y = getHeight() / kRefH;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };

  style_->setBounds(rect(38.0f, 6.0f, 85.0f, 40.0f));
  keytrack_->setBounds(rect(38.0f, 50.0f, 85.0f, 40.0f));
  voices_->setBounds(rect(38.0f, 94.0f, 85.0f, 40.0f));
  tune_->setBounds(rect(128.0f, 0.0f, 84.0f, 66.0f));
  fine_->setBounds(rect(128.0f, 66.0f, 84.0f, 66.0f));
  peaks_->setBounds(rect(219.0f, 6.0f, 118.0f, 60.0f));
  // The cogwheel sits in the top right corner of the peaks display.
  const int gear = std::max(8, static_cast<int>(std::round(16.0f * ratio_y)));
  settings_->setBounds(static_cast<int>(std::round(337.0f * ratio_x)) - gear - 2,
                       static_cast<int>(std::round(8.0f * ratio_y)), gear, gear);
  tilt_->setBounds(rect(219.0f, 71.0f, 118.0f, 36.0f));
  color_->setBounds(rect(219.0f, 110.0f, 118.0f, 24.0f));
  slide_->setBounds(rect(340.0f, 0.0f, 58.0f, 66.0f));
  pressure_->setBounds(rect(340.0f, 66.0f, 58.0f, 66.0f));
  resonance_->setBounds(rect(399.0f, 0.0f, 84.0f, 66.0f));
  mix_->setBounds(rect(490.0f, 0.0f, 84.0f, 66.0f));
  damping_->setBounds(rect(399.0f, 66.0f, 84.0f, 66.0f));
  stretch_->setBounds(rect(490.0f, 66.0f, 84.0f, 66.0f));

  SynthSection::resized();
}

void ResonatorSection::buttonClicked(Button* clicked_button) {
  if (clicked_button == settings_.get()) {
    if (settings_callback_)
      settings_callback_();
  }
  else
    SynthSection::buttonClicked(clicked_button);
}

void ResonatorSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
}

void ResonatorSection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  repaintBackground();
}
