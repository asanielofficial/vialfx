// VialFX - Utility section. Licensed under GPL-3.0-or-later.
#pragma once

#include "JuceHeader.h"
#include "synth_section.h"

class SynthButton;
class SynthSlider;
class UtilityLeds;
class UtilityMeter;

class UtilitySection : public SynthSection {
  public:
    UtilitySection(String name, const std::string& id);
    virtual ~UtilitySection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void resized() override;
    void setActive(bool active) override;
    void buttonClicked(Button* clicked_button) override;
    void setAllValues(vital::control_map& controls) override;

  private:
    void updateControls();

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthButton> invert_left_;
    std::unique_ptr<SynthButton> invert_right_;
    std::unique_ptr<SynthButton> swap_;
    std::unique_ptr<SynthButton> dc_filter_;
    std::unique_ptr<SynthButton> mono_bass_;
    std::unique_ptr<SynthSlider> gain_;
    std::unique_ptr<SynthSlider> pan_;
    std::unique_ptr<SynthSlider> spread_;
    std::unique_ptr<SynthSlider> haas_;
    std::unique_ptr<SynthSlider> frequency_;
    std::unique_ptr<UtilityLeds> leds_on_;
    std::unique_ptr<UtilityLeds> leds_off_;
    std::unique_ptr<UtilityMeter> meter_;

    std::string id_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UtilitySection)
};
