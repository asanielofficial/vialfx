// VialFX - Frequency Shifter section. Licensed under GPL-3.0-or-later.
#pragma once

#include "JuceHeader.h"
#include "synth_section.h"

class FreqShifterLeds;
class PlainTextComponent;
class SynthButton;
class SynthSlider;
class TextSelector;

class FreqShifterSection : public SynthSection {
  public:
    FreqShifterSection(String name, const std::string& id);
    virtual ~FreqShifterSection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void resized() override;
    void sliderValueChanged(Slider* changed_slider) override;
    void setAllValues(vital::control_map& controls) override;

  private:
    void layoutSlot();
    SynthSlider* visibleRange() const;

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthSlider> shift_;
    std::unique_ptr<TextSelector> unit_;
    std::unique_ptr<TextSelector> quality_;
    std::unique_ptr<SynthSlider> frequency_;
    std::unique_ptr<SynthSlider> period_;
    std::unique_ptr<SynthSlider> tempo_;
    std::unique_ptr<SynthSlider> transpose_;
    std::unique_ptr<SynthSlider> stereo_;
    std::unique_ptr<SynthButton> allpass_;
    std::unique_ptr<SynthSlider> feedback_;
    std::unique_ptr<SynthSlider> fb_direction_;
    std::unique_ptr<SynthSlider> mix_;
    std::unique_ptr<PlainTextComponent> range_label_;
    std::unique_ptr<FreqShifterLeds> led_on_;
    std::unique_ptr<FreqShifterLeds> led_off_;

    std::string id_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreqShifterSection)
};
