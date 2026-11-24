// VialFX - Resonator settings panel. Licensed under GPL-3.0-or-later.
#pragma once

#include "JuceHeader.h"

#include "paths.h"
#include "synth_button.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

#include <functional>

// The settings of one Resonator copy, docked below the effect like the table menus of the Morph effect (opened with
// the cogwheel on the effect): how pitch is read from MIDI (tuning, MPE, bend range, glide) and what the MPE slide
// and pressure gestures act on. Every copy has its own settings.
class ResonatorSettingsPanel : public SynthSection {
  public:
    ResonatorSettingsPanel(const std::string& id, const std::string& subtitle) :
        SynthSection("resonator_settings_" + id) {
      auto selector = [&](std::unique_ptr<TextSelector>& s, const std::string& param, const std::string* names,
                          const char* label, const char* tooltip) {
        s = std::make_unique<TextSelector>(param);
        addSlider(s.get());
        s->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        s->setLookAndFeel(TextLookAndFeel::instance());
        s->setLongStringLookup(names);
        s->setTooltip(tooltip);
        selectors_.push_back({ s.get(), label });
      };
      selector(tuning_, id + "_tuning", strings::kResonatorTuningNames, "TUNING",
               "12-TET: equal temperament\nScale file: follow the tuning loaded in the main tuning menu "
               "(.scl / .kbm), so the resonances land on microtonal notes");
      selector(mpe_, id + "_mpe", strings::kResonatorMpeNames, "MPE",
               "On: each note's own channel bends it by the bend range below (MPE keyboards and controllers)\n"
               "Off: pitch bend moves every note by two semitones");
      selector(slide_target_, id + "_slide_target", strings::kResonatorSlideTargetNames, "SLIDE TARGET",
               "What the slide gesture (CC74) changes, by the Slide amount on the effect");
      selector(pressure_target_, id + "_pressure_target", strings::kResonatorPressureTargetNames, "PRESSURE TARGET",
               "What pressure (channel or poly aftertouch) changes, by the Pressure amount on the effect");

      auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& param, const char* label,
                      const char* prefix) {
        slider = std::make_unique<SynthSlider>(param);
        addSlider(slider.get());
        slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider->setPopupPrefix(prefix);
        knobs_.push_back({ slider.get(), label });
      };
      knob(ref_pitch_, id + "_ref_pitch", "A4 PITCH", "A4: ");
      knob(bend_range_, id + "_bend_range", "BEND RANGE", "Bend range: ");
      knob(glide_, id + "_glide", "GLIDE", "Glide: ");
      ref_pitch_->setTooltip("The pitch of A4 in Hz");
      bend_range_->setTooltip("Semitones of pitch bend on an MPE note channel (48 is the MPE default)");
      glide_->setTooltip("How slowly the resonances follow a change of pitch");

      title_ = "RESONATOR SETTINGS  -  " + subtitle;

      close_button_ = std::make_unique<OpenGlShapeButton>("close");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(Paths::exitX());

      setSkinOverride(Skin::kResonator);
    }

    ~ResonatorSettingsPanel() override { }

    // Called when the panel is shown or hidden (the slot grows or shrinks).
    void setVisibilityCallback(std::function<void(bool)> callback) { visibility_callback_ = callback; }

    void setVisible(bool should_be_visible) override {
      bool changed = should_be_visible != isVisible();
      SynthSection::setVisible(should_be_visible);
      if (changed) {
        repaintBackground();
        if (visibility_callback_)
          visibility_callback_(should_be_visible);
      }
    }

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);
      paintKnobShadows(g);

      setLabelFont(g);
      g.setColour(findColour(Skin::kBodyText, true));
      int margin = getWidgetMargin();
      g.drawText(title_, margin * 2, margin, getWidth() - 4 * margin - title_height_, title_height_,
                 Justification::centredLeft);

      for (auto& item : selectors_) {
        drawTextComponentBackground(g, item.first->getBounds(), true);
        drawLabelForComponent(g, item.second, item.first, true);
      }
      for (auto& item : knobs_)
        drawLabelForComponent(g, item.second, item.first);
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      int margin = getWidgetMargin();
      title_height_ = std::max(1, static_cast<int>(size_ratio_ * 28));
      close_button_->setBounds(getWidth() - margin - title_height_, margin, title_height_, title_height_);

      // One row: the four selectors (each as tall as a text selector) then the three knobs.
      int top = margin * 2 + title_height_;
      int row_height = std::max(1, getHeight() - top - margin);
      int available = getWidth() - 2 * margin;
      int selector_width = static_cast<int>(available * 0.5f / selectors_.size());
      int x = margin;
      int selector_height = std::min(row_height, static_cast<int>(size_ratio_ * 40));
      int selector_y = top + (row_height - selector_height) / 2 - static_cast<int>(size_ratio_ * 6);
      for (auto& item : selectors_) {
        item.first->setBounds(x + margin, selector_y, selector_width - 2 * margin, selector_height);
        x += selector_width;
      }
      int knob_width = (getWidth() - margin - x) / static_cast<int>(knobs_.size());
      for (auto& item : knobs_) {
        item.first->setBounds(x, top, knob_width, row_height);
        x += knob_width;
      }
      SynthSection::resized();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get())
        setVisible(false);
      else
        SynthSection::buttonClicked(clicked_button);
    }

  private:
    std::string title_;
    int title_height_ = 0;
    std::unique_ptr<TextSelector> tuning_, mpe_, slide_target_, pressure_target_;
    std::unique_ptr<SynthSlider> ref_pitch_, bend_range_, glide_;
    std::vector<std::pair<Component*, const char*>> selectors_;
    std::vector<std::pair<SynthSlider*, const char*>> knobs_;
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::function<void(bool)> visibility_callback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonatorSettingsPanel)
};
