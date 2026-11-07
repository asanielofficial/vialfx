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
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// The effect build's effects tab: a list of effects in any order, several copies of each effect type.
//
//  - The left panel is the chain: an "ADD EFFECT" button at the top and one row per effect, which can be dragged to
//    reorder. It is blank until effects are added, and scrolls when there are more rows than fit.
//  - The effects themselves are on the right (also scrolling). Each has its on/off button and an "x" to remove it;
//    double-click the title or background to minimize it to a title bar (name and symbol) and again to expand.
//    Right-click offers Duplicate (the copy has the same settings).
//
// Every copy of every effect exists in the engine and in this interface from the start (a pool of
// vital::kMaxEffectInstances per type), so all of their parameters are real controls that can be automated and
// modulated; adding an effect takes the first unused copy of its type.

#include "JuceHeader.h"

#include "chorus_section.h"
#include "compressor_section.h"
#include "delay_section.h"
#include "distortion_section.h"
#include "equalizer_section.h"
#include "filter_section.h"
#include "flanger_section.h"
#include "fx_effect_chain.h"
#include "phaser_section.h"
#include "reverb_section.h"

#include "default_look_and_feel.h"
#include "fonts.h"
#include "open_gl_image.h"
#include "open_gl_image_component.h"
#include "open_gl_multi_quad.h"
#include "paths.h"
#include "synth_button.h"
#include "synth_constants.h"
#include "synth_gui_interface.h"
#include "synth_section.h"
#include "text_look_and_feel.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <vector>

namespace fx_ui {
  constexpr int kNumTypes = vital::constants::kNumEffects;
  constexpr int kMaxInstances = vital::kMaxEffectInstances;
  constexpr int kPoolSize = kNumTypes * kMaxInstances;

  inline std::string typeTitle(int type) {
    static const char* kTitles[kNumTypes] = { "CHORUS", "COMPRESSOR", "DELAY", "DISTORTION", "EQ", "FILTER",
                                              "FLANGER", "PHASER", "REVERB" };
    return kTitles[type];
  }

  inline std::string typeMenuName(int type) {
    static const char* kNames[kNumTypes] = { "Chorus", "Compressor", "Delay", "Distortion", "EQ", "Filter",
                                             "Flanger", "Phaser", "Reverb" };
    return kNames[type];
  }

  inline std::string instanceTitle(int type, int instance) {
    return typeTitle(type) + (instance > 0 ? " " + std::to_string(instance + 1) : "");
  }

  inline Path typeIcon(int type) {
    switch (type) {
      case vital::constants::kChorus: return Paths::chorus();
      case vital::constants::kCompressor: return Paths::compressor();
      case vital::constants::kDelay: return Paths::delay();
      case vital::constants::kDistortion: return Paths::distortion();
      case vital::constants::kEq: return Paths::equalizer();
      case vital::constants::kFilterFx: return Paths::effectsFilter();
      case vital::constants::kFlanger: return Paths::flanger();
      case vital::constants::kPhaser: return Paths::phaser();
      case vital::constants::kReverb: return Paths::reverb();
      default: return Path();
    }
  }

  inline int typeOf(int effect_id) { return effect_id / kMaxInstances; }
  inline int instanceOf(int effect_id) { return effect_id % kMaxInstances; }
  inline std::string idOf(int effect_id) { return vital::FxEffectChain::instanceId(typeOf(effect_id), instanceOf(effect_id)); }

  // Chain entries hold 1 + effect id, plus a flag for minimized effects.
  constexpr int kMinimizedFlag = vital::FxEffectChain::kMinimizedFlag;
  inline int entryId(int entry) { return ((entry - 1) & (kMinimizedFlag - 1)); }
  inline bool entryMinimized(int entry) { return (entry & kMinimizedFlag) != 0; }
  inline int makeEntry(int effect_id, bool minimized) { return 1 + effect_id + (minimized ? kMinimizedFlag : 0); }

  // True if the parameter (control) belongs to copy `instance` of `type` ("chorus_dry_wet" is copy 1 of Chorus,
  // "chorus_2_dry_wet" copy 2).
  inline bool belongsTo(const std::string& name, int type, int instance) {
    std::string prefix = vital::FxEffectChain::instanceId(type, instance) + "_";
    if (name.compare(0, prefix.size(), prefix) != 0)
      return false;
    if (instance > 0)
      return true;

    // Copy 1 keeps the plain names: exclude the names of the other copies ("chorus_2_...").
    const char* rest = name.c_str() + prefix.size();
    int digits = 0;
    while (std::isdigit(static_cast<unsigned char>(rest[digits])))
      ++digits;
    return !(digits > 0 && rest[digits] == '_');
  }
}

// One effect in the right-hand list: the effect's own section, with its extra buttons.
class FxEffectSlot : public SynthSection, public Value::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void effectRemoveRequested(int effect_id) = 0;
        virtual void effectMinimizeToggled(int effect_id) = 0;
        virtual void effectDuplicateRequested(int effect_id) = 0;
        virtual void effectPowerChanged(int effect_id) = 0;
    };

    FxEffectSlot(int effect_id, const vital::output_map& mono_modulations) :
        SynthSection(fx_ui::idOf(effect_id) + "_slot"), effect_id_(effect_id), minimized_(false) {
      int type = fx_ui::typeOf(effect_id);
      int instance = fx_ui::instanceOf(effect_id);
      std::string id = fx_ui::idOf(effect_id);
      std::string title = fx_ui::instanceTitle(type, instance);
      icon_ = fx_ui::typeIcon(type);

      switch (type) {
        case vital::constants::kChorus:
          section_ = std::make_unique<ChorusSection>(title, id, mono_modulations);
          break;
        case vital::constants::kCompressor:
          section_ = std::make_unique<CompressorSection>(title, id);
          break;
        case vital::constants::kDelay:
          section_ = std::make_unique<DelaySection>(title, id, mono_modulations);
          break;
        case vital::constants::kDistortion:
          section_ = std::make_unique<DistortionSection>(title, id, mono_modulations);
          break;
        case vital::constants::kEq:
          section_ = std::make_unique<EqualizerSection>(title, id, mono_modulations);
          break;
        case vital::constants::kFilterFx:
          section_ = std::make_unique<FilterSection>(String(id.substr(std::string("filter_").size())),
                                                     mono_modulations);
          break;
        case vital::constants::kFlanger:
          section_ = std::make_unique<FlangerSection>(title, id, mono_modulations);
          break;
        case vital::constants::kPhaser:
          section_ = std::make_unique<PhaserSection>(title, id, mono_modulations);
          break;
        default:
          section_ = std::make_unique<ReverbSection>(title, id, mono_modulations);
          break;
      }
      section_->setName(title);
      addSubSection(section_.get());

      close_button_ = std::make_unique<OpenGlShapeButton>("remove");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(boldExitX());

      power_button_ = std::make_unique<OpenGlToggleButton>("minimized_power");
      addAndMakeVisible(power_button_.get());
      addOpenGlComponent(power_button_->getGlComponent());
      power_button_->addListener(this);
      power_button_->setPowerButton();
      power_button_->setVisible(false);

      if (section_->activator())
        section_->activator()->getToggleStateValue().addListener(this);

      addMouseListener(this, true);
      setOpaque(false);
      setSkinOverride(Skin::kAllEffects);
    }

    ~FxEffectSlot() override {
      if (section_ && section_->activator())
        section_->activator()->getToggleStateValue().removeListener(this);
    }

    int effectId() const { return effect_id_; }
    bool isOn() const { return section_->activator() && section_->activator()->getToggleState(); }
    bool isMinimized() const { return minimized_; }
    SynthSection* section() { return section_.get(); }
    void addListener(Listener* listener) { listeners_.push_back(listener); }

    // The height the effect takes when minimized: its title bar.
    int minimizedHeight() { return static_cast<int>(getTitleWidth() * 1.35f); }

    void setMinimized(bool minimized) {
      if (minimized_ == minimized)
        return;

      minimized_ = minimized;
      section_->setVisible(!minimized);
      power_button_->setVisible(minimized);
      close_button_->setVisible(minimized);
      power_button_->setToggleState(isOn(), dontSendNotification);
      resized();
      repaintBackground();
    }

    void paintBackground(Graphics& g) override {
      if (!minimized_) {
        paintChildrenBackgrounds(g);
        return;
      }

      // Minimized: just the name and the symbol, in the effect's colors.
      paintBody(g);
      int title_width = getTitleWidth();
      Colour color = section_->findColour(isOn() ? Skin::kWidgetPrimary1 : Skin::kWidgetPrimaryDisabled, true);
      g.setColour(color);
      float icon_size = title_width * 0.7f;
      Rectangle<float> icon_bounds(title_width + getWidgetMargin(), (getHeight() - icon_size) / 2.0f,
                                   icon_size, icon_size);
      g.fillPath(icon_, icon_.getTransformToScaleToFit(icon_bounds, true));

      g.setColour(findColour(Skin::kHeadingText, true));
      int text_x = static_cast<int>(icon_bounds.getRight()) + getWidgetMargin();
      int text_width = std::max(0, static_cast<int>(close_button_->getX() - getWidgetMargin() - text_x));

      // Long names get a smaller font so they stay clear of the remove button.
      Font font = Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 14.0f);
      float full_width = font.getStringWidthFloat(section_->getName());
      if (text_width > 0 && full_width > text_width)
        font = font.withPointHeight(std::max(size_ratio_ * 6.0f, size_ratio_ * 14.0f * text_width / full_width));
      g.setFont(font);
      g.drawText(section_->getName(), text_x, 0, text_width, getHeight(), Justification::centredLeft, true);
      paintChildrenBackgrounds(g);
    }

    void resized() override {
      int title_width = getTitleWidth();
      int small = std::max(8, title_width / 2);

      if (minimized_) {
        int power_size = std::min(title_width, getHeight());
        power_button_->setBounds(static_cast<int>(getPowerButtonOffset()), (getHeight() - power_size) / 2,
                                 power_size, power_size);
        int button_size = std::max(8, static_cast<int>(title_width * 0.6f));
        int y = (getHeight() - button_size) / 2;
        close_button_->setBounds(getWidth() - button_size - getWidgetMargin(), y, button_size, button_size);
      }
      else {
        section_->setBounds(getLocalBounds());
        // Expanded effects have no remove button (select them in the list to remove or duplicate them).
        close_button_->setVisible(false);
        section_->setHeadingTopInset(0);
      }
      SynthSection::resized();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == close_button_.get()) {
        for (Listener* listener : listeners_)
          listener->effectRemoveRequested(effect_id_);
      }
      else if (clicked_button == power_button_.get()) {
        if (section_->activator())
          section_->activator()->setToggleState(power_button_->getToggleState(), sendNotification);
      }
      else
        SynthSection::buttonClicked(clicked_button);
    }

    // The on/off button was changed (by hand, by the host or by a preset).
    void valueChanged(Value& value) override {
      power_button_->setToggleState(isOn(), dontSendNotification);
      if (minimized_)
        repaintBackground();
      for (Listener* listener : listeners_)
        listener->effectPowerChanged(effect_id_);
    }

    // Right-click on the effect's background or title: duplicate, minimize, remove.
    // Double-click on the effect's background or title: minimize / expand.
    void mouseDoubleClick(const MouseEvent& e) override {
      // This slot listens to itself (to see its children's clicks), so a click on the slot's own surface arrives
      // twice: ignore the repeat or the effect would minimize and expand again at once.
      if (e.eventTime == last_event_time_)
        return;
      last_event_time_ = e.eventTime;

      if (e.mods.isPopupMenu() || !hitsBackground(e))
        return;

      for (Listener* listener : listeners_)
        listener->effectMinimizeToggled(effect_id_);
    }

    bool hitsBackground(const MouseEvent& e) const {
      Component* hit = e.originalComponent;
      // The minimized bar has no controls but the power and remove buttons; anything else on it counts.
      if (minimized_)
        return !(dynamic_cast<Slider*>(hit) || dynamic_cast<Button*>(hit) || dynamic_cast<TextEditor*>(hit));

      return !(dynamic_cast<Slider*>(hit) || dynamic_cast<Button*>(hit) || dynamic_cast<TextEditor*>(hit) ||
               (hit != this && hit != section_.get() && dynamic_cast<SynthSection*>(hit) == nullptr &&
                dynamic_cast<OpenGlComponent*>(hit) != nullptr));
    }

    void mouseDown(const MouseEvent& e) override {
      if (!e.mods.isPopupMenu() || !hitsBackground(e) || e.eventTime == last_event_time_)
        return;
      last_event_time_ = e.eventTime;

      PopupItems options;
      options.addItem(kDuplicate, "Duplicate");
      options.addItem(kMinimize, minimized_ ? "Expand" : "Minimize");
      options.addItem(-1, "");
      options.addItem(kRemove, "Remove");
      showPopupSelector(this, e.getPosition(), options, [this](int selection) {
        for (Listener* listener : listeners_) {
          if (selection == kDuplicate)
            listener->effectDuplicateRequested(effect_id_);
          else if (selection == kMinimize)
            listener->effectMinimizeToggled(effect_id_);
          else if (selection == kRemove)
            listener->effectRemoveRequested(effect_id_);
        }
      });
    }

  private:
    enum MenuId {
      kDuplicate = 1,
      kMinimize,
      kRemove
    };

    // A bolder remove cross than the stock exit shape.
    static Path boldExitX() {
      Path outline;
      outline.startNewSubPath(0.2f, 0.2f);
      outline.lineTo(0.8f, 0.8f);
      outline.startNewSubPath(0.2f, 0.8f);
      outline.lineTo(0.8f, 0.2f);

      Path path;
      PathStrokeType stroke(0.14f, PathStrokeType::curved, PathStrokeType::rounded);
      stroke.createStrokedPath(path, outline);
      path.addLineSegment(Line<float>(0.0f, 0.0f, 0.0f, 0.0f), 0.2f);
      path.addLineSegment(Line<float>(1.0f, 1.0f, 1.0f, 1.0f), 0.2f);
      return path;
    }

    int effect_id_;
    bool minimized_;
    Time last_event_time_;
    Path icon_;
    std::unique_ptr<SynthSection> section_;
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::unique_ptr<OpenGlToggleButton> power_button_;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEffectSlot)
};

// A row of the chain list (left panel).
class FxEffectRow : public SynthSection {
  public:
    FxEffectRow() : SynthSection("effect_row"), effect_id_(-1), on_(false), minimized_(false), dragging_(false) {
      setInterceptsMouseClicks(false, false);
      setSkinOverride(Skin::kAllEffects);
      background_ = std::make_unique<OpenGlImageComponent>("background");
      addOpenGlComponent(background_.get());
      background_->setComponent(this);
      background_->paintEntireComponent(false);
    }

    void setEffect(int effect_id, bool on, bool minimized) {
      if (effect_id_ == effect_id && on_ == on && minimized_ == minimized)
        return;

      effect_id_ = effect_id;
      on_ = on;
      minimized_ = minimized;
      if (effect_id >= 0) {
        icon_ = fx_ui::typeIcon(fx_ui::typeOf(effect_id));
        title_ = fx_ui::instanceTitle(fx_ui::typeOf(effect_id), fx_ui::instanceOf(effect_id));
      }
      redraw();
    }

    int effectId() const { return effect_id_; }
    void setDragging(bool dragging) { dragging_ = dragging; redraw(); }
    void setSelection(bool selecting, bool selected) {
      if (selecting_ == selecting && selected_ == selected)
        return;

      selecting_ = selecting;
      selected_ = selected;
      redraw();
    }
    void redraw() { background_->redrawImage(true); }

    void paintBackground(Graphics& g) override { }

    void paint(Graphics& g) override {
      static constexpr float kIconSize = 0.55f;

      g.setColour(getParentComponent() ? getParentComponent()->findColour(Skin::kBody, true) : Colours::black);
      float round_amount = findValue(Skin::kBodyRounding);
      g.fillRoundedRectangle(0.0f, 0.0f, getWidth(), getHeight(), round_amount);

      g.setColour(findColour(on_ ? Skin::kPowerButtonOn : Skin::kPowerButtonOff, true));
      g.drawRoundedRectangle(0.5f, 0.5f, getWidth() - 1.0f, getHeight() - 1.0f, round_amount, dragging_ ? 2.0f : 1.0f);

      g.setFont(Fonts::instance()->proportional_regular().withPointHeight(size_ratio_ * 12.0f));
      float margin = getHeight() * 0.3f;
      float text_x = margin;
      if (selecting_) {
        float box = getHeight() * 0.4f;
        Rectangle<float> check_box(margin, (getHeight() - box) / 2.0f, box, box);
        Colour accent = findColour(Skin::kWidgetPrimary1, true);
        g.setColour(selected_ ? accent : findColour(Skin::kBodyText, true).withMultipliedAlpha(0.6f));
        if (selected_) {
          g.fillRoundedRectangle(check_box, 3.0f);
          g.setColour(findColour(Skin::kBody, true));
          Path tick;
          tick.startNewSubPath(check_box.getX() + box * 0.22f, check_box.getY() + box * 0.52f);
          tick.lineTo(check_box.getX() + box * 0.42f, check_box.getY() + box * 0.72f);
          tick.lineTo(check_box.getX() + box * 0.78f, check_box.getY() + box * 0.28f);
          g.strokePath(tick, PathStrokeType(std::max(1.5f, box * 0.14f), PathStrokeType::curved, PathStrokeType::rounded));
        }
        else
          g.drawRoundedRectangle(check_box, 3.0f, 1.5f);
        text_x = check_box.getRight() + margin * 0.6f;
      }
      String text = String(title_).toLowerCase();
      text = text.substring(0, 1).toUpperCase() + text.substring(1);
      g.setColour(findColour(on_ ? Skin::kBodyText : Skin::kHeadingText, true).withMultipliedAlpha(on_ ? 1.0f : 0.6f));
      g.drawText(text, text_x, 0, getWidth() - text_x - margin - getHeight(), getHeight(),
                 Justification::centredLeft, true);

      float icon_width = kIconSize * getHeight();
      Rectangle<float> icon_bounds(getWidth() - icon_width - margin, (getHeight() - icon_width) / 2.0f,
                                   icon_width, icon_width);
      g.fillPath(icon_, icon_.getTransformToScaleToFit(icon_bounds, true));
    }

    void resized() override {
      SynthSection::resized();
      background_->redrawImage(true);
    }

  private:
    int effect_id_;
    bool on_, minimized_, dragging_;
    bool selecting_ = false, selected_ = false;
    std::string title_;
    Path icon_;
    std::unique_ptr<OpenGlImageComponent> background_;
};

// Holds the rows; takes the mouse so rows can be dragged.
class FxRowContainer : public SynthSection {
  public:
    FxRowContainer() : SynthSection("rows") { setOpaque(false); setSkinOverride(Skin::kAllEffects); }

    void paintBackground(Graphics& g) override { }

    std::function<void(const MouseEvent&)> on_down, on_drag, on_up;
    void mouseDown(const MouseEvent& e) override { if (on_down) on_down(e); }
    void mouseDrag(const MouseEvent& e) override { if (on_drag) on_drag(e); }
    void mouseUp(const MouseEvent& e) override { if (on_up) on_up(e); }
};

// The left panel.
class FxEffectList : public SynthSection, public ScrollBar::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void addEffectRequested(int type) = 0;
        virtual void chainReordered(const std::vector<int>& effect_ids) = 0;
        virtual void effectSelected(int effect_id) = 0;
        virtual void effectsDuplicateRequested(const std::vector<int>& effect_ids) = 0;
        virtual void effectsRemoveRequested(const std::vector<int>& effect_ids) = 0;
    };

    FxEffectList() : SynthSection("effect_chain"), dragged_(-1), mouse_down_y_(0), dragged_start_y_(0), moved_(false) {
      select_button_ = std::make_unique<OpenGlShapeButton>("select_effects");
      addAndMakeVisible(select_button_.get());
      addOpenGlComponent(select_button_->getGlComponent());
      select_button_->addListener(this);
      select_button_->setClickingTogglesState(true);
      select_button_->useOnColors(true);
      select_button_->setShape(selectShape());

      for (OpenGlToggleButton* button : { &duplicate_button_, &delete_button_ }) {
        addChildComponent(button);
        addOpenGlComponent(button->getGlComponent());
        button->addListener(this);
        button->setLookAndFeel(TextLookAndFeel::instance());
        button->setUiButton(true);
      }
      duplicate_button_.setText("DUPLICATE");
      delete_button_.setText("DELETE");

      add_button_ = std::make_unique<OpenGlToggleButton>("add_effect");
      addAndMakeVisible(add_button_.get());
      addOpenGlComponent(add_button_->getGlComponent());
      add_button_->addListener(this);
      add_button_->setLookAndFeel(TextLookAndFeel::instance());
      add_button_->setUiButton(true);
      add_button_->setText("ADD EFFECT");

      addAndMakeVisible(viewport_);
      container_ = std::make_unique<FxRowContainer>();
      viewport_.setViewedComponent(container_.get(), false);
      viewport_.setScrollBarsShown(false, false, true, false);
      viewport_.owner = this;
      container_->on_down = [this](const MouseEvent& e) { rowMouseDown(e); };
      container_->on_drag = [this](const MouseEvent& e) { rowMouseDrag(e); };
      container_->on_up = [this](const MouseEvent& e) { rowMouseUp(e); };

      for (int i = 0; i < fx_ui::kPoolSize; ++i) {
        rows_[i] = std::make_unique<FxEffectRow>();
        container_->addSubSection(rows_[i].get());
        rows_[i]->setVisible(false);
      }
      addSubSection(container_.get(), false);

      scroll_bar_ = std::make_unique<OpenGlScrollBar>();
      scroll_bar_->setShrinkLeft(true);
      addAndMakeVisible(scroll_bar_.get());
      addOpenGlComponent(scroll_bar_->getGlComponent());
      scroll_bar_->addListener(this);

      setSkinOverride(Skin::kAllEffects);
    }

    void addListener(Listener* listener) { listeners_.push_back(listener); }

    // The chain, in order, as effect ids; on / minimized flags per effect id.
    void setChain(const std::vector<int>& effect_ids, const std::function<bool(int)>& is_on,
                  const std::function<bool(int)>& is_minimized) {
      order_ = effect_ids;
      selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [this](int id) {
        return std::find(order_.begin(), order_.end(), id) == order_.end();
      }), selected_.end());
      for (int i = 0; i < fx_ui::kPoolSize; ++i) {
        if (i < static_cast<int>(order_.size())) {
          rows_[i]->setEffect(order_[i], is_on(order_[i]), is_minimized(order_[i]));
          rows_[i]->setSelection(selecting_, isSelected(order_[i]));
          rows_[i]->setVisible(true);
        }
        else {
          rows_[i]->setEffect(-1, false, false);
          rows_[i]->setVisible(false);
        }
      }
      layoutRows();
    }

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);

      if (order_.empty()) {
        g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.45f));
        g.setFont(Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 12.0f));
        int top = add_button_->getBottom() + getWidgetMargin();
        g.drawFittedText("No effects yet", 0, top, getWidth(), static_cast<int>(rowHeight()),
                         Justification::centred, 1);
      }
    }

    void resized() override {
      ScopedLock lock(open_gl_critical_section_);
      int margin = std::max(2, static_cast<int>(getWidgetMargin()));
      int button_height = std::max(18, static_cast<int>(size_ratio_ * 30));
      select_button_->setBounds(margin, margin, button_height, button_height);
      int add_x = select_button_->getRight() + margin;
      add_button_->setBounds(add_x, margin, getWidth() - add_x - margin, button_height);
      int top = add_button_->getBottom() + margin;
      if (selecting_) {
        int half = (getWidth() - 3 * margin) / 2;
        duplicate_button_.setBounds(margin, top, half, button_height);
        delete_button_.setBounds(duplicate_button_.getRight() + margin, top, getWidth() - 3 * margin - half, button_height);
        top = duplicate_button_.getBottom() + margin;
      }
      int scroll_width = std::max(4, static_cast<int>(size_ratio_ * 6));
      viewport_.setBounds(margin, top, getWidth() - 2 * margin - scroll_width, std::max(1, getHeight() - top - margin));
      scroll_bar_->setBounds(getWidth() - margin - scroll_width, top, scroll_width, viewport_.getHeight());
      scroll_bar_->setColor(findColour(Skin::kLightenScreen, true));
      layoutRows();
      SynthSection::resized();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == select_button_.get()) {
        setSelecting(select_button_->getToggleState());
        return;
      }
      if (clicked_button == &duplicate_button_ || clicked_button == &delete_button_) {
        duplicate_button_.setToggleState(false, dontSendNotification);
        delete_button_.setToggleState(false, dontSendNotification);
        std::vector<int> ids;
        for (int id : order_) {
          if (isSelected(id))
            ids.push_back(id);
        }
        if (ids.empty())
          return;

        bool duplicate = clicked_button == &duplicate_button_;
        selected_.clear();
        for (Listener* listener : listeners_) {
          if (duplicate)
            listener->effectsDuplicateRequested(ids);
          else
            listener->effectsRemoveRequested(ids);
        }
        return;
      }
      if (clicked_button != add_button_.get()) {
        SynthSection::buttonClicked(clicked_button);
        return;
      }
      PopupItems options;
      for (int type = 0; type < fx_ui::kNumTypes; ++type)
        options.addItem(type + 1, fx_ui::typeMenuName(type));
      showPopupSelector(this, Point<int>(add_button_->getX(), add_button_->getBottom()), options,
                        [this](int selection) {
                          for (Listener* listener : listeners_)
                            listener->addEffectRequested(selection - 1);
                        });
    }

    void visibleAreaChanged(int position) {
      updateScrollBar();
      scroll_bar_->setCurrentRange(position, viewport_.getHeight());
    }

    void scrollBarMoved(ScrollBar* scroll_bar, double range_start) override {
      viewport_.setViewPosition(Point<int>(0, static_cast<int>(range_start)));
    }

    void renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) override {
      ScopedLock lock(open_gl_critical_section_);
      OpenGlComponent::setViewPort(&viewport_, open_gl);
      SynthSection::renderOpenGlComponents(open_gl, animate);
    }

    // Viewport that reports scrolling back to the list.
    class ListViewport : public Viewport {
      public:
        void visibleAreaChanged(const Rectangle<int>& visible_area) override {
          if (owner)
            owner->visibleAreaChanged(visible_area.getY());
          Viewport::visibleAreaChanged(visible_area);
        }
        FxEffectList* owner = nullptr;
    };

  private:
    // A check box with a tick: the select-multiple icon.
    static Path selectShape() {
      Path box_outline, tick_outline, path;
      box_outline.addRoundedRectangle(0.12f, 0.12f, 0.76f, 0.76f, 0.14f);
      PathStrokeType(0.09f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath(path, box_outline);
      tick_outline.startNewSubPath(0.3f, 0.52f);
      tick_outline.lineTo(0.46f, 0.68f);
      tick_outline.lineTo(0.72f, 0.32f);
      Path tick;
      PathStrokeType(0.1f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath(tick, tick_outline);
      path.addPath(tick);
      return path;
    }

    bool isSelected(int id) const { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); }

    void setSelecting(bool selecting) {
      selecting_ = selecting;
      selected_.clear();
      duplicate_button_.setVisible(selecting);
      delete_button_.setVisible(selecting);
      for (int i = 0; i < static_cast<int>(order_.size()); ++i)
        rows_[i]->setSelection(selecting_, false);
      resized();
    }

    float rowHeight() const { return std::max(18.0f, size_ratio_ * 40.0f); }
    float rowPitch() const { return rowHeight() + std::max(2.0f, size_ratio_ * 5.0f); }

    void layoutRows() {
      int pitch = static_cast<int>(rowPitch());
      int height = static_cast<int>(rowHeight());
      int width = viewport_.getWidth();
      int total = static_cast<int>(order_.size()) * pitch;
      container_->setBounds(0, 0, width, std::max(total, viewport_.getHeight()));
      for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
        if (i != dragged_)
          rows_[i]->setBounds(0, i * pitch, width, height);
        else
          rows_[i]->setSize(width, height);
      }
      updateScrollBar();
      repaintBackground();
    }

    void updateScrollBar() {
      scroll_bar_->setRangeLimits(0.0, container_->getHeight());
      scroll_bar_->setCurrentRange(viewport_.getViewPositionY(), viewport_.getHeight(), dontSendNotification);
    }

    int rowAt(int y) const {
      int index = static_cast<int>(y / rowPitch());
      return index >= 0 && index < static_cast<int>(order_.size()) ? index : -1;
    }

    void rowMouseDown(const MouseEvent& e) {
      moved_ = false;
      if (selecting_) {
        // No dragging while selecting: a click toggles the row.
        dragged_ = -1;
        select_down_ = rowAt(e.y);
        return;
      }
      dragged_ = rowAt(e.y);
      if (dragged_ < 0)
        return;

      mouse_down_y_ = e.y;
      dragged_start_y_ = rows_[dragged_]->getY();
      rows_[dragged_]->setDragging(true);
      rows_[dragged_]->toFront(false);
    }

    void rowMouseDrag(const MouseEvent& e) {
      if (dragged_ < 0)
        return;

      int pitch = static_cast<int>(rowPitch());
      int max_y = std::max(0, (static_cast<int>(order_.size()) - 1) * pitch);
      int y = std::max(0, std::min(max_y, dragged_start_y_ + e.y - mouse_down_y_));
      if (std::abs(e.y - mouse_down_y_) > 3)
        moved_ = true;
      rows_[dragged_]->setTopLeftPosition(0, y);

      int target = std::max(0, std::min(static_cast<int>(order_.size()) - 1, (y + pitch / 2) / pitch));
      if (target != dragged_) {
        // The other rows move out of the way; the dragged row keeps following the mouse.
        std::unique_ptr<FxEffectRow> row = std::move(rows_[dragged_]);
        int id = order_[dragged_];
        order_.erase(order_.begin() + dragged_);
        order_.insert(order_.begin() + target, id);
        for (int i = dragged_; i != target; i += (target > dragged_ ? 1 : -1))
          rows_[i] = std::move(rows_[i + (target > dragged_ ? 1 : -1)]);
        rows_[target] = std::move(row);
        dragged_ = target;
        int width = viewport_.getWidth();
        int height = static_cast<int>(rowHeight());
        for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
          if (i != dragged_)
            rows_[i]->setBounds(0, i * pitch, width, height);
        }
      }
    }

    void rowMouseUp(const MouseEvent& e) {
      if (selecting_) {
        int index = rowAt(e.y);
        if (index >= 0 && index == select_down_) {
          int id = order_[index];
          auto found = std::find(selected_.begin(), selected_.end(), id);
          if (found == selected_.end())
            selected_.push_back(id);
          else
            selected_.erase(found);
          rows_[index]->setSelection(true, isSelected(id));
        }
        select_down_ = -1;
        return;
      }
      if (dragged_ < 0)
        return;

      FxEffectRow* row = rows_[dragged_].get();
      row->setDragging(false);
      int id = order_[dragged_];
      dragged_ = -1;
      layoutRows();

      if (moved_) {
        for (Listener* listener : listeners_)
          listener->chainReordered(order_);
      }
      else {
        for (Listener* listener : listeners_)
          listener->effectSelected(id);
      }
    }

    std::vector<Listener*> listeners_;
    std::unique_ptr<OpenGlShapeButton> select_button_;
    OpenGlToggleButton duplicate_button_{"duplicate_selected"}, delete_button_{"delete_selected"};
    std::unique_ptr<OpenGlToggleButton> add_button_;
    ListViewport viewport_;
    std::unique_ptr<FxRowContainer> container_;
    std::unique_ptr<FxEffectRow> rows_[fx_ui::kPoolSize];
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;
    std::vector<int> order_;
    CriticalSection open_gl_critical_section_;
    int dragged_;
    int mouse_down_y_;
    int dragged_start_y_;
    bool moved_;
    bool selecting_ = false;
    int select_down_ = -1;
    std::vector<int> selected_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEffectList)
};

class EffectsContainer : public SynthSection {
  public:
    EffectsContainer(String name) : SynthSection(name) { }
    void paintBackground(Graphics& g) override {
      g.fillAll(findColour(Skin::kBackground, true));
      paintChildrenShadows(g);
      paintChildrenBackgrounds(g);
    }
};

class EffectsInterface : public SynthSection, public FxEffectList::Listener, public FxEffectSlot::Listener,
                         public ScrollBar::Listener, EffectsViewport::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void effectsMoved() = 0;
    };

    EffectsInterface(const vital::output_map& mono_modulations) : SynthSection("effects") {
      container_ = std::make_unique<EffectsContainer>("container");

      addAndMakeVisible(viewport_);
      viewport_.setViewedComponent(container_.get());
      viewport_.addListener(this);
      viewport_.setScrollBarsShown(false, false, true, false);

      for (int id = 0; id < fx_ui::kPoolSize; ++id) {
        slots_[id] = std::make_unique<FxEffectSlot>(id, mono_modulations);
        slots_[id]->addListener(this);
        container_->addSubSection(slots_[id].get());
        slots_[id]->setVisible(false);
      }

      chain_list_ = std::make_unique<FxEffectList>();
      addSubSection(chain_list_.get());
      chain_list_->addListener(this);

      addSubSection(container_.get(), false);

      scroll_bar_ = std::make_unique<OpenGlScrollBar>();
      scroll_bar_->setShrinkLeft(true);
      addAndMakeVisible(scroll_bar_.get());
      addOpenGlComponent(scroll_bar_->getGlComponent());
      scroll_bar_->addListener(this);

      setOpaque(false);
      setSkinOverride(Skin::kAllEffects);
    }

    ~EffectsInterface() override { }

    void paintBackground(Graphics& g) override {
      Colour background = findColour(Skin::kBackground, true);
      g.setColour(background);
      g.fillRect(getLocalBounds().withRight(getWidth() - findValue(Skin::kLargePadding) / 2));
      paintChildBackground(g, chain_list_.get());

      if (chain_.empty()) {
        g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.45f));
        g.setFont(Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 16.0f));
        g.drawText("Use ADD EFFECT to build a chain", viewport_.getBounds(), Justification::centred, true);
      }

      redoBackgroundImage();
    }

    void paintChildrenShadows(Graphics& g) override { }

    void redoBackgroundImage() {
      Colour background = findColour(Skin::kBackground, true);

      int height = std::max(container_->getHeight(), getHeight());
      int mult = getPixelMultiple();
      Image background_image = Image(Image::ARGB, std::max(1, container_->getWidth() * mult),
                                     std::max(1, height * mult), true);

      Graphics background_graphics(background_image);
      background_graphics.addTransform(AffineTransform::scale(mult));
      background_graphics.fillAll(background);
      container_->paintBackground(background_graphics);
      background_.setOwnImage(background_image);
    }

    void resized() override {
      static constexpr float kListWidthPercent = 0.2f;

      ScopedLock lock(open_gl_critical_section_);

      int list_width = getWidth() * kListWidthPercent;
      chain_list_->setBounds(0, 0, list_width, getHeight());
      chain_list_->setSizeRatio(size_ratio_);
      int large_padding = findValue(Skin::kLargePadding);
      int shadow_width = getComponentShadowWidth();
      int viewport_x = list_width + large_padding - shadow_width;
      int viewport_width = getWidth() - viewport_x - large_padding + 2 * shadow_width;
      viewport_.setBounds(viewport_x, 0, viewport_width, getHeight());
      setEffectPositions();

      scroll_bar_->setBounds(getWidth() - large_padding + 1, 0, large_padding - 2, getHeight());
      scroll_bar_->setColor(findColour(Skin::kLightenScreen, true));

      SynthSection::resized();
    }

    void reset() override {
      SynthSection::reset();
      loadChainFromSynth();
    }

    void parentHierarchyChanged() override {
      SynthSection::parentHierarchyChanged();
      if (!chain_loaded_ && findParentComponentOfClass<SynthGuiInterface>())
        loadChainFromSynth();
    }

    // ---- chain editing (all of it goes through applyChain) ----

    void addEffectRequested(int type) override {
      if (type < 0 || type >= fx_ui::kNumTypes)
        return;

      int id = firstFreeCopy(type);
      if (id < 0)
        return;  // all copies of this effect are in use

      resetEffect(id);
      setEffectOn(id, true);
      chain_.push_back(fx_ui::makeEntry(id, false));
      applyChain();
      scrollToEffect(id);
    }

    void chainReordered(const std::vector<int>& effect_ids) override {
      std::vector<int> new_chain;
      for (int id : effect_ids) {
        auto found = std::find_if(chain_.begin(), chain_.end(), [id](int entry) { return fx_ui::entryId(entry) == id; });
        if (found != chain_.end())
          new_chain.push_back(*found);
      }
      chain_ = new_chain;
      applyChain();
    }

    void effectSelected(int effect_id) override { scrollToEffect(effect_id); }

    void effectsDuplicateRequested(const std::vector<int>& effect_ids) override {
      for (int id : effect_ids)
        effectDuplicateRequested(id);
    }

    void effectsRemoveRequested(const std::vector<int>& effect_ids) override {
      for (int id : effect_ids)
        effectRemoveRequested(id);
    }

    void effectRemoveRequested(int effect_id) override {
      chain_.erase(std::remove_if(chain_.begin(), chain_.end(),
                                  [effect_id](int entry) { return fx_ui::entryId(entry) == effect_id; }),
                   chain_.end());
      // The copy goes back to the pool in its initial state, with its modulations cleared.
      resetEffect(effect_id);
      applyChain();
    }

    void effectMinimizeToggled(int effect_id) override {
      for (int& entry : chain_) {
        if (fx_ui::entryId(entry) == effect_id)
          entry = fx_ui::makeEntry(effect_id, !fx_ui::entryMinimized(entry));
      }
      applyChain();
    }

    void effectDuplicateRequested(int effect_id) override {
      int type = fx_ui::typeOf(effect_id);
      int copy = firstFreeCopy(type);
      if (copy < 0)
        return;

      copySettings(effect_id, copy);
      for (size_t i = 0; i < chain_.size(); ++i) {
        if (fx_ui::entryId(chain_[i]) == effect_id) {
          chain_.insert(chain_.begin() + i + 1, fx_ui::makeEntry(copy, false));
          break;
        }
      }
      applyChain();
      scrollToEffect(copy);
    }

    void effectPowerChanged(int effect_id) override {
      ScopedLock lock(open_gl_critical_section_);
      refreshList();
    }

    void setFocus() { grabKeyboardFocus(); }

    void setEffectPositions() {
      if (getWidth() <= 0 || getHeight() <= 0)
        return;

      int padding = getPadding();
      int large_padding = findValue(Skin::kLargePadding);
      int shadow_width = getComponentShadowWidth();
      int start_x = chain_list_->getRight() + large_padding;
      int effect_width = getWidth() - start_x - large_padding;
      int knob_section_height = getKnobSectionHeight();
      int widget_margin = findValue(Skin::kWidgetMargin);
      int effect_height = 2 * knob_section_height - widget_margin;
      int y = 0;

      Point<int> position = viewport_.getViewPosition();

      std::vector<bool> shown(fx_ui::kPoolSize, false);
      for (int entry : chain_) {
        int id = fx_ui::entryId(entry);
        if (id < 0 || id >= fx_ui::kPoolSize)
          continue;

        FxEffectSlot* slot = slots_[id].get();
        bool minimized = fx_ui::entryMinimized(entry);
        slot->setVisible(true);
        slot->setMinimized(minimized);
        int height = minimized ? slot->minimizedHeight() : effect_height;
        slot->setBounds(shadow_width, y, effect_width, height);
        y += height + padding;
        shown[id] = true;
      }

      for (int id = 0; id < fx_ui::kPoolSize; ++id) {
        if (!shown[id])
          slots_[id]->setVisible(false);
      }

      container_->setBounds(0, 0, viewport_.getWidth(), std::max(0, y - padding));
      viewport_.setViewPosition(position);

      for (Listener* listener : listeners_)
        listener->effectsMoved();

      container_->setScrollWheelEnabled(container_->getHeight() <= viewport_.getHeight());
      setScrollBarRange();
      repaintBackground();
    }

    void initOpenGlComponents(OpenGlWrapper& open_gl) override {
      background_.init(open_gl);
      SynthSection::initOpenGlComponents(open_gl);
    }

    void renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) override {
      ScopedLock lock(open_gl_critical_section_);

      OpenGlComponent::setViewPort(&viewport_, open_gl);

      float image_width = vital::utils::nextPowerOfTwo(background_.getImageWidth());
      float image_height = vital::utils::nextPowerOfTwo(background_.getImageHeight());
      int mult = getPixelMultiple();
      float width_ratio = image_width / std::max(1, container_->getWidth() * mult);
      float height_ratio = image_height / std::max(1, viewport_.getHeight() * mult);
      float y_offset = (2.0f * viewport_.getViewPositionY()) / std::max(1, getHeight());

      background_.setTopLeft(-1.0f, 1.0f + y_offset);
      background_.setTopRight(-1.0 + 2.0f * width_ratio, 1.0f + y_offset);
      background_.setBottomLeft(-1.0f, 1.0f - 2.0f * height_ratio + y_offset);
      background_.setBottomRight(-1.0 + 2.0f * width_ratio, 1.0f - 2.0f * height_ratio + y_offset);

      background_.setColor(Colours::white);
      background_.drawImage(open_gl);
      SynthSection::renderOpenGlComponents(open_gl, animate);
    }

    void destroyOpenGlComponents(OpenGlWrapper& open_gl) override {
      background_.destroy(open_gl);
      SynthSection::destroyOpenGlComponents(open_gl);
    }

    void scrollBarMoved(ScrollBar* scroll_bar, double range_start) override {
      viewport_.setViewPosition(Point<int>(0, static_cast<int>(range_start)));
    }

    void setScrollBarRange() {
      scroll_bar_->setRangeLimits(0.0, container_->getHeight());
      scroll_bar_->setCurrentRange(scroll_bar_->getCurrentRangeStart(), viewport_.getHeight(), dontSendNotification);
    }

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    void effectsScrolled(int position) override {
      setScrollBarRange();
      scroll_bar_->setCurrentRange(position, viewport_.getHeight());

      for (Listener* listener : listeners_)
        listener->effectsMoved();
    }

  private:
    bool inChain(int id) const {
      for (int entry : chain_) {
        if (fx_ui::entryId(entry) == id)
          return true;
      }
      return false;
    }

    // The first copy of the type that is not in the chain, or -1.
    int firstFreeCopy(int type) const {
      for (int copy = 0; copy < fx_ui::kMaxInstances; ++copy) {
        int id = type * fx_ui::kMaxInstances + copy;
        if (!inChain(id))
          return id;
      }
      return -1;
    }

    SynthGuiInterface* gui() { return findParentComponentOfClass<SynthGuiInterface>(); }

    void setControl(const std::string& name, vital::mono_float value) {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      parent->getSynth()->valueChangedInternal(name, value);
      parent->updateGuiControl(name, value);
    }

    void setEffectOn(int id, bool on) {
      setControl(fx_ui::idOf(id) + "_on", on ? 1.0f : 0.0f);
      syncActive(id);
    }

    // Setting a control from here does not notify the effect's section, which greys itself when off.
    void syncActive(int id) {
      SynthSection* section = slots_[id]->section();
      if (section && section->activator())
        section->setActive(section->activator()->getToggleState());
    }

    // Every parameter of the copy back to its default; its modulations removed.
    void resetEffect(int id) {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      int type = fx_ui::typeOf(id);
      int instance = fx_ui::instanceOf(id);
      std::vector<std::string> names;
      for (auto& control : parent->getSynth()->getControls()) {
        if (fx_ui::belongsTo(control.first, type, instance))
          names.push_back(control.first);
      }

      for (const std::string& name : names) {
        for (vital::ModulationConnection* connection : parent->getSynth()->getDestinationConnections(name))
          parent->disconnectModulation(connection);
        setControl(name, vital::Parameters::getDetails(name).default_value);
      }
      syncActive(id);
    }

    // Copy the settings of one copy of an effect to another copy of the same type.
    void copySettings(int from, int to) {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      int type = fx_ui::typeOf(from);
      std::string from_prefix = fx_ui::idOf(from) + "_";
      std::string to_prefix = fx_ui::idOf(to) + "_";
      vital::control_map controls = parent->getSynth()->getControls();
      for (auto& control : controls) {
        if (!fx_ui::belongsTo(control.first, type, fx_ui::instanceOf(from)))
          continue;

        std::string target = to_prefix + control.first.substr(from_prefix.size());
        if (controls.count(target))
          setControl(target, control.second->value());
      }
      syncActive(to);
    }

    void loadChainFromSynth() {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      chain_loaded_ = true;
      int entries[vital::FxEffectChain::kMaxChain];
      int count = parent->getSynth()->getEffectChain(entries);
      chain_.clear();
      for (int i = 0; i < count; ++i) {
        int id = fx_ui::entryId(entries[i]);
        if (id >= 0 && id < fx_ui::kPoolSize && !inChain(id))
          chain_.push_back(entries[i]);
      }

      ScopedLock lock(open_gl_critical_section_);
      refreshList();
      setEffectPositions();
    }

    // The chain went to the engine and to the interface.
    void applyChain() {
      SynthGuiInterface* parent = gui();
      if (parent)
        parent->getSynth()->setEffectChain(chain_.data(), static_cast<int>(chain_.size()));

      ScopedLock lock(open_gl_critical_section_);
      refreshList();
      setEffectPositions();
    }

    void refreshList() {
      std::vector<int> ids;
      for (int entry : chain_)
        ids.push_back(fx_ui::entryId(entry));

      chain_list_->setChain(ids, [this](int id) { return slots_[id]->isOn(); },
                            [this](int id) {
                              for (int entry : chain_) {
                                if (fx_ui::entryId(entry) == id)
                                  return fx_ui::entryMinimized(entry);
                              }
                              return false;
                            });
    }

    void scrollToEffect(int effect_id) {
      if (effect_id < 0 || effect_id >= fx_ui::kPoolSize || !slots_[effect_id]->isVisible())
        return;

      int y = slots_[effect_id]->getY();
      int max_y = std::max(0, container_->getHeight() - viewport_.getHeight());
      viewport_.setViewPosition(0, std::max(0, std::min(max_y, y)));
    }

    std::vector<Listener*> listeners_;
    EffectsViewport viewport_;
    std::unique_ptr<EffectsContainer> container_;
    OpenGlImage background_;
    CriticalSection open_gl_critical_section_;

    std::unique_ptr<FxEffectSlot> slots_[fx_ui::kPoolSize];
    std::unique_ptr<FxEffectList> chain_list_;
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;
    std::vector<int> chain_;
    bool chain_loaded_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsInterface)
};
