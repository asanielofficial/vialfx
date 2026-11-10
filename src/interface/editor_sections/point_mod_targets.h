// VialFX: targets for modulating the points of drawn lines. GPL-3.0-or-later, like the rest of the project.
//
// Each point of an LFO shape or a spectral curve has an X and a Y target. While a modulation source is being dragged,
// the targets of every visible line appear (as small knobs next to the points) and the source can be dropped on
// one, like on any knob. A target that got a modulation keeps showing next to its point with the usual amount ring;
// when the modulation is removed the target goes away.
//
// Under the hood the targets are 32 slots (the controls pointmod_1 .. pointmod_32 of the modulation matrix) that the
// engine binds to points on demand (SoundEngine::bindPointMod).

#pragma once

#include "JuceHeader.h"

#include "line_editor.h"
#include "sound_engine.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "synth_slider.h"

class PointModTargets : public SynthSection, public Timer {
  public:
    static constexpr int kNumSlots = vital::SoundEngine::kNumPointMods;
    static constexpr int kTargetSize = 14;
    static constexpr int kTargetDistance = 12;

    PointModTargets() : SynthSection("point_mod_targets") {
      for (int slot = 0; slot < kNumSlots; ++slot) {
        sliders_[slot] = std::make_unique<SynthSlider>(String(vital::SoundEngine::pointModName(slot)));
        addSlider(sliders_[slot].get());
        sliders_[slot]->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        sliders_[slot]->setInterceptsMouseClicks(false, false);
        sliders_[slot]->setVisible(false);
      }
      setInterceptsMouseClicks(false, false);
      startTimerHz(20);
    }

    ~PointModTargets() { stopTimer(); }

    // A source is being dragged: every point of every visible line gets its targets.
    void beginDrag() {
      dragging_ = true;
      vital::SoundEngine* engine = getEngine();
      if (engine == nullptr)
        return;

      for (LineEditor* editor : visibleEditors()) {
        LineGenerator* curve = editor->getModel();
        for (int i = 0; i < curve->getNumPoints(); ++i) {
          for (int axis = 0; axis < LineGenerator::kNumAxes; ++axis) {
            if (curve->pointSlot(i, axis) < 0 && engine->bindPointMod(editor->getPointModCurve(), i, axis) < 0)
              break;
          }
        }
      }
      updateTargets();
    }

    // The drag is over: the targets that did not get a modulation go away again.
    void endDrag() {
      dragging_ = false;
      releaseUnused();
      updateTargets();
    }

    void timerCallback() override {
      if (dragging_)
        return;
      releaseUnused();
      updateTargets();
    }

    void paintBackground(Graphics& g) override { }

  private:
    vital::SoundEngine* getEngine() {
      SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
      if (gui == nullptr || gui->getSynth() == nullptr)
        return nullptr;
      return gui->getSynth()->getEngine();
    }

    std::vector<LineEditor*> visibleEditors() {
      std::vector<LineEditor*> result;
      vital::SoundEngine* engine = getEngine();
      SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
      if (engine == nullptr)
        return result;

      for (LineEditor* editor : LineEditor::allEditors()) {
        int id = editor->getPointModCurve();
        if (id < 0 || !editor->isShowing() || editor->getModel() == nullptr ||
            editor->findParentComponentOfClass<SynthGuiInterface>() != gui ||
            engine->getPointModCurve(id) != editor->getModel())
          continue;
        result.push_back(editor);
      }
      return result;
    }

    // Slots that are bound but nothing modulates them any more.
    void releaseUnused() {
      vital::SoundEngine* engine = getEngine();
      SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
      if (engine == nullptr || gui == nullptr)
        return;

      for (int slot = 0; slot < kNumSlots; ++slot) {
        if (engine->pointModCurve(slot) < 0)
          continue;
        if (gui->getSynth()->getDestinationConnections(vital::SoundEngine::pointModName(slot)).empty())
          engine->unbindPointMod(slot);
      }
    }

    // Puts a target next to each bound point: X to the right of it, Y above it.
    void updateTargets() {
      bool used[kNumSlots] = {};
      float ratio = size_ratio_ > 0.0f ? size_ratio_ : 1.0f;
      int size = std::max(8, static_cast<int>(kTargetSize * ratio));
      int distance = static_cast<int>(kTargetDistance * ratio);

      for (LineEditor* editor : visibleEditors()) {
        LineGenerator* curve = editor->getModel();
        for (int i = 0; i < curve->getNumPoints(); ++i) {
          for (int axis = 0; axis < LineGenerator::kNumAxes; ++axis) {
            int slot = curve->pointSlot(i, axis);
            if (slot < 0 || slot >= kNumSlots)
              continue;

            Point<float> position = editor->getPointPixelPosition(i);
            Point<int> local = getLocalPoint(editor, position.toInt());
            if (axis == LineGenerator::kAxisX)
              local.x += distance;
            else
              local.y -= distance;
            sliders_[slot]->setBounds(local.x - size / 2, local.y - size / 2, size, size);
            used[slot] = true;
          }
        }
      }

      for (int slot = 0; slot < kNumSlots; ++slot) {
        if (sliders_[slot]->isVisible() != used[slot])
          sliders_[slot]->setVisible(used[slot]);
      }
    }

    std::unique_ptr<SynthSlider> sliders_[kNumSlots];
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PointModTargets)
};
