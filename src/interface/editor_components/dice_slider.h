/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include "JuceHeader.h"
#include "synth_button.h"
#include "synth_slider.h"

#include <functional>

// The dice is a real parameter (so it can be automated, modulated and right-clicked like a knob), but it looks
// and acts like a button: a plain click rolls a new seed, and dragging does nothing.
class DiceSlider : public SynthSlider {
  public:
    explicit DiceSlider(String name) : SynthSlider(name) { }

    void setClickCallback(std::function<void()> callback) { click_callback_ = callback; }

    // The die itself is a separate (non-interactive) shape button drawn under this slider: this slider passes its
    // hover and pressed state on so the die gets the same darkening as any button.
    void linkDisplay(OpenGlShapeButton* display) {
      state_callback_ = [display](bool hover, bool down) {
        if (auto* gl = dynamic_cast<OpenGlShapeButtonComponent*>(display->getGlComponent())) {
          gl->setHover(hover);
          gl->setDown(down);
        }
      };
    }

    void mouseEnter(const MouseEvent& e) override {
      SynthSlider::mouseEnter(e);
      hover_ = true;
      notifyState();
    }
    void mouseExit(const MouseEvent& e) override {
      SynthSlider::mouseExit(e);
      hover_ = false;
      notifyState();
    }
    void mouseDown(const MouseEvent& e) override {
      if (e.mods.isPopupMenu() || e.mods.isAltDown())
        SynthSlider::mouseDown(e);
      else {
        down_ = true;
        notifyState();
      }
    }
    void drawShadow(Graphics&) override { }   // no dark disc behind the die
    void mouseDrag(const MouseEvent&) override { }
    void mouseDoubleClick(const MouseEvent&) override { }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override { }
    void mouseUp(const MouseEvent& e) override {
      down_ = false;
      notifyState();
      if (e.mods.isPopupMenu() || e.mods.isAltDown())
        return;
      if (e.mouseWasClicked() && click_callback_)
        click_callback_();
    }

  private:
    void notifyState() {
      if (state_callback_)
        state_callback_(hover_, down_);
    }

    std::function<void()> click_callback_;
    std::function<void(bool, bool)> state_callback_;
    bool hover_ = false;
    bool down_ = false;
};
