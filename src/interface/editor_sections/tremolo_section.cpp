// VialFX - Tremolo section. Licensed under GPL-3.0-or-later.
#include "tremolo_section.h"

#include "fonts.h"
#include "open_gl_line_renderer.h"
#include "open_gl_multi_quad.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_gui_interface.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"
#include "tremolo.h"

#include <cmath>
#include <vector>

// Resulting gain (A + B) over two cycles of the slowest active modulator.
class TremoloViewer : public OpenGlLineRenderer {
  public:
    static constexpr int kResolution = 160;

    TremoloViewer() : OpenGlLineRenderer(kResolution), mod_a_(nullptr), mod_b_(nullptr), rate_a_(nullptr),
                      rate_b_(nullptr), tempo_a_(nullptr), tempo_b_(nullptr), amount_a_(nullptr), amount_b_(nullptr),
                      unit_a_(nullptr), unit_b_(nullptr) {
      setFill(true);
      setFillCenter(-1.0f);
    }

    void setControls(TextSelector* mod_a, TextSelector* mod_b, SynthSlider* rate_a, SynthSlider* rate_b,
                     SynthSlider* tempo_a, SynthSlider* tempo_b, SynthSlider* amount_a, SynthSlider* amount_b,
                     TextSelector* unit_a, TextSelector* unit_b) {
      mod_a_ = mod_a; mod_b_ = mod_b; rate_a_ = rate_a; rate_b_ = rate_b; tempo_a_ = tempo_a; tempo_b_ = tempo_b;
      amount_a_ = amount_a; amount_b_ = amount_b; unit_a_ = unit_a; unit_b_ = unit_b;
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
    static float hash(int n) {
      float s = std::sin(n * 12.9898f) * 43758.5453f;
      return (s - std::floor(s)) * 2.0f - 1.0f;
    }

    float rateHz(SynthSlider* rate, SynthSlider* tempo, TextSelector* unit) const {
      if (unit->getValue() >= 0.5) {   // assume 120 bpm for the picture
        int index = std::min(std::max(static_cast<int>(tempo->getValue() + 0.3), 0), vital::constants::kNumSyncedFrequencyRatios - 1);
        return std::max(vital::constants::kSyncedFrequencyRatios[index] * 2.0f, 0.01f);
      }
      return std::pow(2.0f, static_cast<float>(rate->getValue()));
    }

    float shape(int type, float cycles) const {
      int index = static_cast<int>(std::floor(cycles));
      float p = cycles - index;
      float rnd_prev = hash(index), rnd_next = hash(index + 1);
      return vital::Tremolo::shape(type, p, rnd_prev, rnd_next);
    }

    void drawCurve() {
      const float width = getWidth(), height = getHeight();
      if (!mod_a_)
        return;
      float amount_a = static_cast<float>(amount_a_->getValue()), amount_b = static_cast<float>(amount_b_->getValue());
      float rate_a = rateHz(rate_a_, tempo_a_, unit_a_), rate_b = rateHz(rate_b_, tempo_b_, unit_b_);
      int type_a = static_cast<int>(mod_a_->getValue()), type_b = static_cast<int>(mod_b_->getValue());
      bool use_a = amount_a > 0.005f, use_b = amount_b > 0.005f;
      float slowest = use_a && use_b ? std::min(rate_a, rate_b) : (use_b ? rate_b : rate_a);
      const float duration = 2.0f / std::max(slowest, 0.01f);
      for (int i = 0; i < kResolution; ++i) {
        float t = duration * i / (kResolution - 1.0f);
        float gain = 1.0f - amount_a * (1.0f - shape(type_a, rate_a * t)) - amount_b * (1.0f - shape(type_b, rate_b * t));
        setXAt(i, width * i / (kResolution - 1.0f));
        setYAt(i, height * (0.93f - 0.86f * std::max(gain, 0.0f)));
      }
    }

    TextSelector *mod_a_, *mod_b_;
    SynthSlider *rate_a_, *rate_b_, *tempo_a_, *tempo_b_, *amount_a_, *amount_b_;
    TextSelector *unit_a_, *unit_b_;
};

// Vertical meter: current gain, full height = unity, empty = silence.
class TremoloMeter : public OpenGlMultiQuad {
  public:
    explicit TremoloMeter(const std::string& id) : OpenGlMultiQuad(1, Shaders::kRoundedRectangleFragment), id_(id), parent_(nullptr), cents_(nullptr), smoothed_(1.0f) {
      setTargetComponent(this);
    }

    void init(OpenGlWrapper& open_gl) override {
      parent_ = findParentComponentOfClass<SynthGuiInterface>();
      if (parent_ && parent_->getSynth())
        cents_ = parent_->getSynth()->getStatusOutput((id_ + "_gain"));
      OpenGlMultiQuad::init(open_gl);
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      const float h = getHeight();
      if (h <= 0.0f)
        return;
      setRounding(findValue(Skin::kWidgetRoundedCorner) * 0.5f);

      float target = std::min(std::max(cents_ ? cents_->value()[0] : 1.0f, 0.0f), 1.0f);
      smoothed_ += (target - smoothed_) * 0.35f;

      setNumQuads(1);
      setColor(findColour(Skin::kWidgetBackground, true));
      setQuad(0, -1.0f, -1.0f, 2.0f, 2.0f);
      OpenGlMultiQuad::render(open_gl, animate);

      if (smoothed_ * h > 1.0f) {
        setColor(findColour(Skin::kWidgetPrimary2, true));
        setQuad(0, -1.0f, -1.0f, 2.0f, 2.0f * smoothed_);
        OpenGlMultiQuad::render(open_gl, animate);
      }
    }

  private:
    std::string id_;
    SynthGuiInterface* parent_;
    const vital::StatusOutput* cents_;
    float smoothed_;
};

namespace {
  constexpr float kTremoloW = 580.0f, kTremoloH = 180.5f;   // section size at the Tremolo's window size (44 taller than the others)
}

TremoloSection::TremoloSection(String name, const std::string& id) : SynthSection(name), id_(id) {
  auto selector = [&](std::unique_ptr<TextSelector>& s, const std::string& param, const std::string* names) {
    s = std::make_unique<TextSelector>(param);
    addSlider(s.get());
    s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    s->setLookAndFeel(TextLookAndFeel::instance());
    s->setLongStringLookup(names);
  };
  selector(mod_a_, (id_ + "_mod_a"), strings::kVibratoModNames);
  selector(mod_b_, (id_ + "_mod_b"), strings::kVibratoModNames);
  selector(unit_a_, (id_ + "_unit_a"), strings::kRateUnitNames);
  selector(unit_b_, (id_ + "_unit_b"), strings::kRateUnitNames);

  auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& param) {
    slider = std::make_unique<SynthSlider>(param);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  };
  knob(rate_a_, (id_ + "_rate_a"));
  knob(rate_b_, (id_ + "_rate_b"));
  knob(tempo_a_, (id_ + "_tempo_a"));
  knob(tempo_b_, (id_ + "_tempo_b"));
  knob(amount_a_, (id_ + "_amount_a"));
  knob(amount_b_, (id_ + "_amount_b"));

  viewer_ = std::make_unique<TremoloViewer>();
  viewer_->setControls(mod_a_.get(), mod_b_.get(), rate_a_.get(), rate_b_.get(), tempo_a_.get(), tempo_b_.get(),
                       amount_a_.get(), amount_b_.get(), unit_a_.get(), unit_b_.get());
  addOpenGlComponent(viewer_.get());

  meter_ = std::make_unique<TremoloMeter>(id_);
  addOpenGlComponent(meter_.get());

  on_ = std::make_unique<SynthButton>((id_ + "_on"));
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kTremolo);
}

TremoloSection::~TremoloSection() { }

SynthSlider* TremoloSection::visibleRate(bool a) const {
  TextSelector* unit = a ? unit_a_.get() : unit_b_.get();
  if (unit->getValue() >= 0.5)
    return a ? tempo_a_.get() : tempo_b_.get();
  return a ? rate_a_.get() : rate_b_.get();
}

void TremoloSection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);
  setLabelFont(g);

  const float ratio_x = getWidth() / kTremoloW, ratio_y = getHeight() / kTremoloH;

  // Dark panel behind the graph.
  g.setColour(findColour(Skin::kWidgetBackground, true));
  g.fillRoundedRectangle(std::round(409.0f * ratio_x), std::round(7.0f * ratio_y), std::round(127.0f * ratio_x),
                         std::round(169.0f * ratio_y), findValue(Skin::kWidgetRoundedCorner) * 1.5f);

  drawTextComponentBackground(g, mod_a_->getBounds(), true);
  drawTextComponentBackground(g, mod_b_->getBounds(), true);
  drawTextComponentBackground(g, unit_a_->getBounds(), true);
  drawTextComponentBackground(g, unit_b_->getBounds(), true);
  drawLabelForComponent(g, TRANS("MOD TYPE A"), mod_a_.get(), true);
  drawLabelForComponent(g, TRANS("MOD TYPE B"), mod_b_.get(), true);
  drawLabelForComponent(g, TRANS("UNIT A"), unit_a_.get(), true);
  drawLabelForComponent(g, TRANS("UNIT B"), unit_b_.get(), true);
  drawLabelForComponent(g, TRANS("RATE A"), visibleRate(true));
  drawLabelForComponent(g, TRANS("AMOUNT A"), amount_a_.get());
  drawLabelForComponent(g, TRANS("RATE B"), visibleRate(false));
  drawLabelForComponent(g, TRANS("AMOUNT B"), amount_b_.get());
}

void TremoloSection::layoutKnobs() {
  const float ratio_x = getWidth() / kTremoloW, ratio_y = getHeight() / kTremoloH;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };
  Rectangle<int> slot_a = rect(37.0f, 65.5f, 84.0f, 65.5f), slot_b = rect(220.0f, 65.5f, 84.0f, 65.5f);
  SynthSlider* visible_a = visibleRate(true);
  SynthSlider* visible_b = visibleRate(false);
  for (SynthSlider* s : { rate_a_.get(), tempo_a_.get() })
    s->setBounds(s == visible_a ? slot_a : Rectangle<int>());
  for (SynthSlider* s : { rate_b_.get(), tempo_b_.get() })
    s->setBounds(s == visible_b ? slot_b : Rectangle<int>());
}

void TremoloSection::resized() {
  const float ratio_x = getWidth() / kTremoloW, ratio_y = getHeight() / kTremoloH;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };

  mod_a_->setBounds(rect(37.0f, 6.0f, 175.0f, 60.0f));
  mod_b_->setBounds(rect(220.0f, 6.0f, 173.0f, 60.0f));
  amount_a_->setBounds(rect(128.0f, 65.5f, 84.0f, 65.5f));
  amount_b_->setBounds(rect(310.0f, 65.5f, 84.0f, 65.5f));
  unit_a_->setBounds(rect(37.0f, 134.0f, 175.0f, 41.0f));
  unit_b_->setBounds(rect(220.0f, 134.0f, 173.0f, 41.0f));
  viewer_->setBounds(rect(409.0f, 7.0f, 127.0f, 169.0f));
  meter_->setBounds(rect(551.0f, 19.0f, 14.0f, 144.0f));

  SynthSection::resized();
  layoutKnobs();
}

void TremoloSection::setActive(bool active) {
  SynthSection::setActive(active);
}

void TremoloSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  if (changed_slider == unit_a_.get() || changed_slider == unit_b_.get()) {
    layoutKnobs();
    repaintBackground();
  }
}

void TremoloSection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  layoutKnobs();
  repaintBackground();
}
