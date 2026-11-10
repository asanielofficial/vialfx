// VialFX - Pitch Shifter section. Licensed under GPL-3.0-or-later.
#pragma once

#include "JuceHeader.h"
#include "synth_section.h"

class PitchShifterViewer;
class PlainTextComponent;
class SynthButton;
class SynthSlider;
class TextSelector;

class PitchShifterSection : public SynthSection {
  public:
    PitchShifterSection(String name, const std::string& id);
    virtual ~PitchShifterSection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void resized() override;
    void setActive(bool active) override;
    void sliderValueChanged(Slider* changed_slider) override;
    void buttonClicked(Button* clicked_button) override;
    void setAllValues(vital::control_map& controls) override;

  private:
    void layoutSlot();
    void updateControls();
    void updateReadout();

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<TextSelector> mode_;
    std::unique_ptr<TextSelector> compensation_;
    std::unique_ptr<SynthSlider> pitch_;
    std::unique_ptr<SynthButton> formant_control_;
    std::unique_ptr<SynthSlider> grain_;
    std::unique_ptr<SynthSlider> resolution_;
    std::unique_ptr<SynthSlider> formant_;
    std::unique_ptr<SynthSlider> mix_;
    std::unique_ptr<PlainTextComponent> pitch_text_;
    std::unique_ptr<PlainTextComponent> slot_label_;
    std::unique_ptr<PitchShifterViewer> viewer_;

    std::string id_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchShifterSection)
};
