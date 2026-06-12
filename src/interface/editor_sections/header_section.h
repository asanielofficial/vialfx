/* Copyright 2013-2019 Matt Tytel
 *
 * vial is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vial is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vial.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <JuceHeader.h>

#include "synth_section.h"
#include "bank_exporter.h"
#include "oscilloscope.h"
#include "synth_preset_selector.h"
#include "tab_selector.h"
#include "volume_section.h"

class BankExporter;
class PresetBrowser;

class LogoButton : public Button {
  public:
    LogoButton(const String& name) : Button(name) {
      image_component_.setComponent(this);
    }

    void setPaths(const Path& letter, const Path& ring) {
      letter_ = letter;
      ring_ = ring;
    }

    void resized() override {
      const DropShadow shadow(Colours::white, 5, Point<int>(0, 0));

      if (shadow_.getWidth() == getWidth() && shadow_.getHeight() == getHeight())
        return;

      Rectangle<float> bounds = getLocalBounds().toFloat();
      letter_.applyTransform(letter_.getTransformToScaleToFit(bounds, true));
      ring_.applyTransform(ring_.getTransformToScaleToFit(bounds, true));

      shadow_ = Image(Image::SingleChannel, getWidth(), getHeight(), true);

      Graphics shadow_g(shadow_);
      shadow.drawForPath(shadow_g, letter_);
      shadow.drawForPath(shadow_g, ring_);

      redoImage();
    }

    void paintButton(Graphics& g, bool hover, bool down) override {
      Rectangle<float> bounds = getLocalBounds().toFloat();
      letter_.applyTransform(letter_.getTransformToScaleToFit(bounds, true));
      ring_.applyTransform(ring_.getTransformToScaleToFit(bounds, true));

      g.setColour(findColour(Skin::kShadow, true));
      g.drawImageAt(shadow_, 0, 0, true);

      ColourGradient letter_gradient(letter_top_color_, 0.0f, 0.0f, letter_bottom_color_, 0.0f, getHeight(), false);
      ColourGradient ring_gradient(ring_top_color_, 0.0f, 0.0f, ring_bottom_color_, 0.0f, getHeight(), false);
      g.setGradientFill(letter_gradient);
      g.fillPath(letter_);

      g.setGradientFill(ring_gradient);
      g.fillPath(ring_);

      if (hover) {
        g.setColour(findColour(Skin::kLightenScreen, true));
        g.fillEllipse(getLocalBounds().toFloat());
      }
      else if (down) {
        g.setColour(findColour(Skin::kOverlayScreen, true));
        g.fillEllipse(getLocalBounds().toFloat());
      }
    }

    void setLetterColors(Colour top, Colour bottom) {
      letter_top_color_ = top;
      letter_bottom_color_ = bottom;
      redoImage();
    }

    void setRingColors(Colour top, Colour bottom) {
      ring_top_color_ = top;
      ring_bottom_color_ = bottom;
      redoImage();
    }

    void mouseEnter(const MouseEvent& e) override {
      Button::mouseEnter(e);
      image_component_.setColor(Colour(0xffdddddd));
    }

    void mouseExit(const MouseEvent& e) override {
      Button::mouseExit(e);
      image_component_.setColor(Colours::white);
    }

    OpenGlImageComponent* getImageComponent() { return &image_component_; }
    void redoImage() { image_component_.redrawImage(true); }

  private:
    OpenGlImageComponent image_component_;

    Path letter_;
    Path ring_;

    Image shadow_;

    Colour letter_top_color_;
    Colour letter_bottom_color_;

    Colour ring_top_color_;
    Colour ring_bottom_color_;
};

class LogoSection : public SynthSection {
  public:
    static constexpr float kLogoPaddingY = 2.0f;

    class Listener {
      public:
        virtual ~Listener() { }

        virtual void showAboutSection() = 0;
    };

    LogoSection();

    void resized() override;
    void paintBackground(Graphics& g) override;
    void buttonClicked(Button* clicked_button) override;
    void addListener(Listener* listener) { listeners_.push_back(listener); }

  private:
    std::vector<Listener*> listeners_;
    std::unique_ptr<LogoButton> logo_button_;
};

class HeaderSection : public SynthSection, public SaveSection::Listener,
                      public SynthPresetSelector::Listener, public LogoSection::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }

        virtual void showAboutSection() = 0;
        virtual void deleteRequested(File preset) = 0;
        virtual void tabSelected(int index) = 0;
        virtual void clearTemporaryTab(int current_tab) = 0;
        virtual void setPresetBrowserVisibility(bool visible, int current_tab) = 0;
        virtual void setBankExporterVisibility(bool visible, int current_tab) = 0;
        virtual void bankImported() = 0;
    };

    HeaderSection();

    void paintBackground(Graphics& g) override;
    void resized() override;
    void reset() override;
    void setAllValues(vial::control_map& controls) override;
    void buttonClicked(Button* clicked_button) override;
    void sliderValueChanged(Slider* slider) override;

    void setPresetBrowserVisibility(bool visible) override;
    void setBankExporterVisibility(bool visible) override;
    void deleteRequested(File preset) override;
    void bankImported() override;
    void save(File preset) override;

    void setTemporaryTab(String name);

    void showAboutSection() override {
      for (Listener* listener : listeners_)
        listener->showAboutSection();
    }

    void setOscilloscopeMemory(const vial::poly_float* memory);
    void setAudioMemory(const vial::StereoMemory* memory);

    void notifyChange();
    void notifyFresh();
  
    void setSaveSection(SaveSection* save_section) { 
      synth_preset_selector_->setSaveSection(save_section);
      save_section->addSaveListener(this);
    }

    void setBrowser(PresetBrowser* browser) { synth_preset_selector_->setBrowser(browser); }
    void setBankExporter(BankExporter* exporter) { synth_preset_selector_->setBankExporter(exporter); }
    void addListener(Listener* listener) { listeners_.push_back(listener); }
    void setTabOffset(int offset) { tab_offset_ = offset; repaint(); }

  private:
    std::vector<Listener*> listeners_;

    std::unique_ptr<LogoSection> logo_section_;
    std::unique_ptr<TabSelector> tab_selector_;
    int tab_offset_;
    std::unique_ptr<PlainTextComponent> temporary_tab_;
    std::unique_ptr<OpenGlShapeButton> exit_temporary_button_;

    std::unique_ptr<SynthButton> view_spectrogram_;
    std::unique_ptr<Oscilloscope> oscilloscope_;
    std::unique_ptr<Spectrogram> spectrogram_;
    std::unique_ptr<SynthPresetSelector> synth_preset_selector_;
    std::unique_ptr<VolumeSection> volume_section_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderSection)
};

