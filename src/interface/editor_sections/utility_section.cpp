// VialFX - Utility section. Licensed under GPL-3.0-or-later.
#include "utility_section.h"

#include "fonts.h"
#include "open_gl_multi_quad.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_gui_interface.h"
#include "synth_slider.h"
#include "utility_section.h"

#include <cmath>
#include <vector>

// LED dots at the left of each pill toggle; shows the toggles that are on (or, with on = false, off).
class UtilityLeds : public OpenGlMultiQuad {
  public:
    UtilityLeds(std::vector<SynthButton*> buttons, bool on) :
        OpenGlMultiQuad(8, Shaders::kCircleFragment), buttons_(buttons), on_(on) {
      setTargetComponent(this);
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      const float w = getWidth(), h = getHeight();
      if (w <= 0.0f || h <= 0.0f)
        return;

      int count = 0;
      for (SynthButton* button : buttons_) {
        if (button->getToggleState() != on_)
          continue;
        Rectangle<int> bounds = button->getBounds();
        float led = std::round(bounds.getHeight() * 0.34f);
        float x = bounds.getX() + std::round(bounds.getHeight() * 0.38f);
        float y = bounds.getY() + std::round((bounds.getHeight() - led) * 0.5f);
        setQuad(count++, 2.0f * x / w - 1.0f, 1.0f - 2.0f * (y + led) / h, 2.0f * led / w, 2.0f * led / h);
      }
      if (count == 0)
        return;

      setNumQuads(count);
      setColor(on_ ? Colours::white : findColour(Skin::kIconButtonOff, true));
      OpenGlMultiQuad::render(open_gl, animate);
    }

  private:
    std::vector<SynthButton*> buttons_;
    bool on_;
};

// Vertical output level meter: bar follows the output peak (-60..+6 dB), a thin line holds the recent peak and the
// bar turns red above 0 dBFS. Reads the module's peak meter status outputs.
class UtilityMeter : public OpenGlMultiQuad {
  public:
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;

    static float dbToPosition(float db) { return std::min(std::max((db - kMinDb) / (kMaxDb - kMinDb), 0.0f), 1.0f); }

    explicit UtilityMeter(const std::string& id) : OpenGlMultiQuad(1, Shaders::kRoundedRectangleFragment), id_(id), parent_(nullptr),
                     level_(nullptr), peak_(nullptr), smoothed_(0.0f) {
      setTargetComponent(this);
    }

    void init(OpenGlWrapper& open_gl) override {
      parent_ = findParentComponentOfClass<SynthGuiInterface>();
      if (parent_ && parent_->getSynth()) {
        level_ = parent_->getSynth()->getStatusOutput((id_ + "_level"));
        peak_ = parent_->getSynth()->getStatusOutput((id_ + "_peak"));
      }
      OpenGlMultiQuad::init(open_gl);
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      const float h = getHeight();
      if (h <= 0.0f)
        return;
      setRounding(findValue(Skin::kWidgetRoundedCorner) * 0.5f);

      float level = 0.0f, hold = 0.0f;
      if (level_)
        level = std::max(level_->value()[0], level_->value()[1]);
      if (peak_)
        hold = std::max(peak_->value()[0], peak_->value()[1]);
      float target = dbToPosition(vital::utils::magnitudeToDb(std::max(level, 1.0e-6f)));
      smoothed_ = target > smoothed_ ? target : smoothed_ + (target - smoothed_) * 0.2f;

      // Track.
      setNumQuads(1);
      setColor(findColour(Skin::kWidgetBackground, true));
      setQuad(0, -1.0f, -1.0f, 2.0f, 2.0f);
      OpenGlMultiQuad::render(open_gl, animate);

      // Level.
      if (smoothed_ * h > 1.0f) {
        const bool clipping = level >= 1.0f;
        setColor(clipping ? Colour(0xffff5a5a) : Colours::white);
        setQuad(0, -1.0f, -1.0f, 2.0f, 2.0f * smoothed_);
        OpenGlMultiQuad::render(open_gl, animate);
      }

      // Held peak.
      float hold_position = dbToPosition(vital::utils::magnitudeToDb(std::max(hold, 1.0e-6f)));
      if (hold_position * h > 3.0f) {
        setColor(Colours::white.withAlpha(0.85f));
        float y = -1.0f + 2.0f * hold_position;
        setQuad(0, -1.0f, y - 2.0f * 1.5f / h, 2.0f, 2.0f * 2.0f / h);
        OpenGlMultiQuad::render(open_gl, animate);
      }
    }

  private:
    std::string id_;
    SynthGuiInterface* parent_;
    const vital::StatusOutput* level_;
    const vital::StatusOutput* peak_;
    float smoothed_;
};

UtilitySection::UtilitySection(String name, const std::string& id) : SynthSection(name), id_(id) {
  auto add_toggle = [&](std::unique_ptr<SynthButton>& button, const std::string& param) {
    button = std::make_unique<SynthButton>(param);
    addButton(button.get());
    button->setText(" ");        // non-empty so the button does not fall back to its parameter name
    button->setNoBackground();   // the pill and its text are drawn by the section; the LED by UtilityLeds
  };
  add_toggle(invert_left_, (id_ + "_invert_left"));
  add_toggle(invert_right_, (id_ + "_invert_right"));
  add_toggle(swap_, (id_ + "_swap"));
  add_toggle(dc_filter_, (id_ + "_dc_filter"));
  add_toggle(mono_bass_, (id_ + "_mono_bass"));

  auto add_knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& param, bool bipolar) {
    slider = std::make_unique<SynthSlider>(param);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    if (bipolar) {
      slider->setBipolar();
      slider->snapToValue(true, 0.0);
    }
  };
  add_knob(gain_, (id_ + "_gain"), false);
  add_knob(pan_, (id_ + "_pan"), true);
  add_knob(spread_, (id_ + "_spread"), false);
  add_knob(haas_, (id_ + "_haas"), true);
  add_knob(frequency_, (id_ + "_mono_frequency"), false);
  gain_->snapToValue(true, 0.0);
  spread_->snapToValue(true, 1.0);

  std::vector<SynthButton*> toggles = { invert_left_.get(), invert_right_.get(), swap_.get(),
                                         dc_filter_.get(), mono_bass_.get() };
  leds_on_ = std::make_unique<UtilityLeds>(toggles, true);
  addOpenGlComponent(leds_on_.get());
  leds_off_ = std::make_unique<UtilityLeds>(toggles, false);
  addOpenGlComponent(leds_off_.get());

  meter_ = std::make_unique<UtilityMeter>(id_);
  addOpenGlComponent(meter_.get());

  on_ = std::make_unique<SynthButton>((id_ + "_on"));
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kUtility);
  updateControls();
}

UtilitySection::~UtilitySection() { }

void UtilitySection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);
  setLabelFont(g);

  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;

  // Pill toggles: background and caption (the LED is drawn live on top).
  struct Pill { SynthButton* button; const char* text; };
  const Pill pills[] = { { invert_left_.get(), "L" }, { invert_right_.get(), "R" }, { swap_.get(), "Swap L/R" },
                         { dc_filter_.get(), "DC Filter" }, { mono_bass_.get(), "Mono Bass" } };
  for (const Pill& pill : pills) {
    Rectangle<float> bounds = pill.button->getBounds().toFloat();
    g.setColour(findColour(Skin::kLabelBackground, true));
    g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);

    const float h = bounds.getHeight();
    const float text_x = bounds.getX() + h * 0.38f + std::round(h * 0.34f) + h * 0.28f;
    g.setColour(findColour(Skin::kBodyText, true));
    g.setFont(Fonts::instance()->proportional_regular().withPointHeight(h * 0.50f));
    g.drawText(TRANS(pill.text), Rectangle<float>(text_x, bounds.getY(), bounds.getRight() - text_x - h * 0.2f, h),
               Justification::centredLeft, false);
  }

  g.setColour(findColour(Skin::kBodyText, true).withAlpha(0.9f));
  g.setFont(Fonts::instance()->proportional_light().withPointHeight(14.0f * ratio_y));
  g.drawText(TRANS("Polarity Invert"), Rectangle<int>(std::round(42 * ratio_x), std::round(5 * ratio_y),
                                                        std::round(92 * ratio_x), std::round(23 * ratio_y)),
             Justification::centredLeft, false);

  setLabelFont(g);
  drawLabelForComponent(g, TRANS("GAIN"), gain_.get());
  drawLabelForComponent(g, TRANS("PAN"), pan_.get());
  drawLabelForComponent(g, TRANS("SPREAD"), spread_.get());
  drawLabelForComponent(g, TRANS("HAAS"), haas_.get());
  drawLabelForComponent(g, TRANS("BASS FREQ"), frequency_.get());

  // dB scale next to the meter.
  const float meter_x = meter_->getRight() + 4.0f * ratio_x;
  g.setColour(findColour(Skin::kBodyText, true).withAlpha(0.55f));
  g.setFont(Fonts::instance()->proportional_regular().withPointHeight(7.5f * ratio_y));
  for (float db : { 0.0f, -12.0f, -24.0f, -48.0f }) {
    float position = (db - UtilityMeter::kMinDb) / (UtilityMeter::kMaxDb - UtilityMeter::kMinDb);
    float y = meter_->getBottom() - position * meter_->getHeight();
    g.fillRect(Rectangle<float>(meter_->getRight() + 1.0f, std::round(y), 2.0f * ratio_x, 1.0f));
    g.drawText(String(static_cast<int>(db)), Rectangle<float>(meter_x + 2.0f, y - 5.0f * ratio_y, 22.0f * ratio_x, 10.0f * ratio_y),
               Justification::centredLeft, false);
  }
}

void UtilitySection::resized() {
  const float ratio_x = getWidth() / 580.0f, ratio_y = getHeight() / 136.5f;
  auto rect = [&](float x, float y, float w, float h) {
    return Rectangle<int>(std::round(x * ratio_x), std::round(y * ratio_y),
                          std::round(w * ratio_x), std::round(h * ratio_y));
  };

  // Top row: polarity pair, then swap / DC filter; Mono Bass sits right above its frequency knob.
  invert_left_->setBounds(rect(136.0f, 5.0f, 38.0f, 23.0f));
  invert_right_->setBounds(rect(180.0f, 5.0f, 38.0f, 23.0f));
  swap_->setBounds(rect(232.0f, 5.0f, 76.0f, 23.0f));
  dc_filter_->setBounds(rect(316.0f, 5.0f, 80.0f, 23.0f));
  mono_bass_->setBounds(rect(404.0f, 5.0f, 86.0f, 23.0f));

  // Knob row, evenly spaced; the last one belongs to Mono Bass.
  gain_->setBounds(rect(32.0f, 52.0f, 84.0f, 66.0f));
  pan_->setBounds(rect(125.0f, 52.0f, 84.0f, 66.0f));
  spread_->setBounds(rect(218.0f, 52.0f, 84.0f, 66.0f));
  haas_->setBounds(rect(311.0f, 52.0f, 84.0f, 66.0f));
  frequency_->setBounds(rect(405.0f, 52.0f, 84.0f, 66.0f));

  meter_->setBounds(rect(521.0f, 10.0f, 12.0f, 108.0f));
  leds_on_->setBounds(getLocalBounds());
  leds_off_->setBounds(getLocalBounds());

  SynthSection::resized();
}

void UtilitySection::updateControls() {
  // The crossover only matters while Mono Bass is on.
  frequency_->setActive(mono_bass_->getToggleState() && isActive());
}

void UtilitySection::setActive(bool active) {
  SynthSection::setActive(active);
  updateControls();
}

void UtilitySection::buttonClicked(Button* clicked_button) {
  SynthSection::buttonClicked(clicked_button);
  if (clicked_button == mono_bass_.get())
    updateControls();
}

void UtilitySection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  updateControls();
}
