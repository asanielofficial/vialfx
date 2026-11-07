#pragma once

#include "synth_section.h"
#include "open_gl_image_component.h"
#include "fonts.h"

// Dashed outline (with a small name tag) drawn over the interface while the skin editor is open.
// It marks the part of the UI that belongs to a skin bin. It never takes mouse input.
class BinHighlight : public SynthSection {
  public:
    BinHighlight() : SynthSection("bin_highlight") {
      setInterceptsMouseClicks(false, false);
      setWantsKeyboardFocus(false);
      setOpaque(false);
      image_ = std::make_unique<OpenGlImageComponent>();
      addOpenGlComponent(image_.get());
      image_->setComponent(this);
      image_->paintEntireComponent(false);
      image_->setInterceptsMouseClicks(false, false);
    }

    void paintBackground(Graphics& g) override { }

    void resized() override {
      SynthSection::resized();
      image_->redrawImage(true);
    }

    void setHighlight(const std::vector<Rectangle<int>>& rects, const String& label) {
      if (rects == rects_ && label == label_)
        return;

      rects_ = rects;
      label_ = label;
      image_->redrawImage(true);
    }

    void clearHighlight() { setHighlight({}, String()); }

    void paint(Graphics& g) override {
      if (rects_.empty())
        return;

      const Colour accent(0xffa77bff);
      for (const Rectangle<int>& rect : rects_) {
        Rectangle<float> bounds = rect.toFloat().reduced(1.5f);
        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(bounds, 4.0f);

        Path outline, dashed;
        outline.addRoundedRectangle(bounds, 4.0f);
        const float dashes[2] = { 7.0f, 5.0f };
        PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(accent);
        g.fillPath(dashed);
      }

      if (label_.isEmpty())
        return;

      Font font = Fonts::instance()->proportional_light().withPointHeight(12.0f);
      g.setFont(font);
      int width = static_cast<int>(font.getStringWidthFloat(label_)) + 16;
      Rectangle<int> tag(rects_[0].getX() + 6, rects_[0].getY() + 6, width, 20);
      g.setColour(accent);
      g.fillRoundedRectangle(tag.toFloat(), 4.0f);
      g.setColour(Colour(0xff1a1426));
      g.drawText(label_, tag, Justification::centred, false);
    }

  private:
    std::vector<Rectangle<int>> rects_;
    String label_;
    std::unique_ptr<OpenGlImageComponent> image_;
};
