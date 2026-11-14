// VialFX - interface pieces of the FX Bin and Parallel Split containers. Licensed under GPL-3.0-or-later.
//
//  - FxBranchTabs: the multi-selector of a Parallel Split (one tab per parallel, a + button on the right, scrolls
//    when the tabs do not fit).
//  - FxBranchStrip: level, pan, mute and solo of one parallel.
//  - FxContainerPanel: the bar of an FX Bin, or the header + selector + strip of a Parallel Split.

#pragma once

#include "JuceHeader.h"

#include "fx_containers.h"
#include "fonts.h"
#include "open_gl_image_component.h"
#include "paths.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "text_look_and_feel.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fx_log {
  // Temporary diagnostics: one line per step in %TEMP%/vialfx_fxlog.txt, written (and closed) at once so the last
  // line survives a crash.
  inline void write(const String& text) {
    File file = File::getSpecialLocation(File::tempDirectory).getChildFile("vialfx_fxlog.txt");
    file.appendText(Time::getCurrentTime().toString(false, true, true, true) + " " + text + "\n");
  }
}
#define FXLOG(x) fx_log::write(String(x))

namespace fx_container_ui {
  enum Action {
    kRename = 1,
    kToggleMinimized,
    kUngroup,
    kRemove
  };

  class Listener {
    public:
      virtual ~Listener() { }
      virtual void containerAction(bool is_split, int index, int action) = 0;
      virtual void containerPowerChanged() = 0;
      virtual void splitBranchSelected(int split, int position) = 0;
      virtual void splitBranchAdded(int split) = 0;
      virtual void splitBranchRemoved(int split, int position) = 0;
  };

  inline Path minusShape() {
    Path path;
    path.addRoundedRectangle(0.15f, 0.42f, 0.7f, 0.16f, 0.05f);
    return path;
  }

  inline Path plusShape() {
    Path path;
    path.addRoundedRectangle(0.15f, 0.42f, 0.7f, 0.16f, 0.05f);
    path.addRoundedRectangle(0.42f, 0.15f, 0.16f, 0.7f, 0.05f);
    return path;
  }

  inline Path folderShape() {
    Path path;
    path.addRoundedRectangle(0.04f, 0.14f, 0.4f, 0.24f, 0.06f);
    path.addRoundedRectangle(0.04f, 0.26f, 0.92f, 0.6f, 0.08f);
    return path;
  }

  // Three lanes side by side: parallel signal paths.
  inline Path parallelShape() {
    Path path;
    path.addRoundedRectangle(0.04f, 0.14f, 0.92f, 0.16f, 0.05f);
    path.addRoundedRectangle(0.04f, 0.42f, 0.92f, 0.16f, 0.05f);
    path.addRoundedRectangle(0.04f, 0.70f, 0.92f, 0.16f, 0.05f);
    return path;
  }

  inline Path crossShape() {
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
}

// The selector of the parallels of a split: "1 2 3 ... [+]". Tabs scroll sideways when they overflow.
class FxBranchTabs : public SynthSection {
  public:
    FxBranchTabs() : SynthSection("branch_tabs") {
      background_ = std::make_unique<OpenGlImageComponent>("background");
      addOpenGlComponent(background_.get());
      background_->setComponent(this);
      background_->paintEntireComponent(false);
      setOpaque(false);
      setSkinOverride(Skin::kAllEffects);
    }

    class TabListener {
      public:
        virtual ~TabListener() { }
        virtual void tabSelected(int position) = 0;
        virtual void tabAddRequested() = 0;
        virtual void tabRemoveRequested(int position) = 0;
    };

    void setTabListener(TabListener* listener) { listener_ = listener; }

    void setState(int count, int selected) {
      FXLOG("tabs.setState " + String(count) + " " + String(selected));
      bool changed = count != count_ || selected != selected_;
      bool selection_changed = selected != selected_;
      count_ = count;
      selected_ = selected;
      if (selection_changed || count_ > 0)
        clampScroll();
      if (selection_changed)
        ensureVisible(selected_);
      if (changed)
        redraw();
    }

    void redraw() { background_->redrawImage(true); }

    void paintBackground(Graphics& g) override { }

    void paint(Graphics& g) override {
      FXLOG("tabs.paint " + String(getWidth()) + "x" + String(getHeight()));
      int h = getHeight();
      float round_amount = std::max(3.0f, h * 0.25f);
      Colour accent = findColour(Skin::kWidgetPrimary1, true);
      Colour text = findColour(Skin::kBodyText, true);

      g.setColour(getParentComponent() ? getParentComponent()->findColour(Skin::kBody, true) : Colours::black);
      g.fillRoundedRectangle(0.0f, 0.0f, getWidth(), h, round_amount);

      {
        Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(0, 0, stripWidth(), h);
        g.setFont(Fonts::instance()->proportional_regular().withPointHeight(size_ratio_ * 12.0f));
        for (int i = 0; i < count_; ++i) {
          Rectangle<float> tab = tabBounds(i).toFloat().reduced(1.0f);
          if (i == selected_) {
            g.setColour(accent.withAlpha(0.35f));
            g.fillRoundedRectangle(tab, round_amount);
            g.setColour(accent);
            g.drawRoundedRectangle(tab, round_amount, 1.5f);
          }
          else {
            g.setColour(findColour(Skin::kLightenScreen, true));
            g.fillRoundedRectangle(tab, round_amount);
          }
          g.setColour(i == selected_ ? text : text.withMultipliedAlpha(0.7f));
          g.drawText(String(i + 1), tab, Justification::centred, false);
        }
      }

      // Hints that there is more to scroll to.
      if (scroll_ > 0) {
        g.setColour(accent.withAlpha(0.6f));
        g.fillRect(0, 2, 2, h - 4);
      }
      if (scroll_ < maxScroll()) {
        g.setColour(accent.withAlpha(0.6f));
        g.fillRect(stripWidth() - 2, 2, 2, h - 4);
      }

      Rectangle<float> plus = plusBounds().toFloat().reduced(1.0f);
      g.setColour(findColour(Skin::kLightenScreen, true));
      g.fillRoundedRectangle(plus, round_amount);
      g.setColour(text);
      Path plus_shape = fx_container_ui::plusShape();
      g.fillPath(plus_shape, plus_shape.getTransformToScaleToFit(plus.reduced(plus.getWidth() * 0.18f), true));
    }

    void resized() override {
      clampScroll();
      SynthSection::resized();
      redraw();
    }

    void mouseDown(const MouseEvent& e) override {
      drag_scroll_start_ = scroll_;
      dragged_ = false;
    }

    void mouseDrag(const MouseEvent& e) override {
      if (!dragged_ && e.getDistanceFromDragStartX() > -4 && e.getDistanceFromDragStartX() < 4)
        return;
      dragged_ = true;
      setScroll(drag_scroll_start_ - e.getDistanceFromDragStartX());
    }

    void mouseUp(const MouseEvent& e) override {
      if (dragged_ || listener_ == nullptr)
        return;

      if (plusBounds().contains(e.getPosition())) {
        if (!e.mods.isPopupMenu())
          listener_->tabAddRequested();
        return;
      }
      int hit = tabAt(e.x, e.y);
      if (hit < 0)
        return;

      if (e.mods.isPopupMenu()) {
        PopupItems options;
        options.addItem(1, "Remove parallel " + std::to_string(hit + 1));
        showPopupSelector(this, e.getPosition(), options, [this, hit](int selection) {
          if (selection == 1 && listener_)
            listener_->tabRemoveRequested(hit);
        });
        return;
      }
      listener_->tabSelected(hit);
    }

    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override {
      float amount = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : -wheel.deltaY;
      setScroll(scroll_ + static_cast<int>(amount * tabPitch() * 2.0f));
    }

  private:
    int tabWidth() const { return std::max(12, static_cast<int>(getHeight() * 1.4f)); }
    int gap() const { return std::max(2, static_cast<int>(size_ratio_ * 3.0f)); }
    int tabPitch() const { return tabWidth() + gap(); }
    int plusWidth() const { return getHeight(); }
    int stripWidth() const { return std::max(1, getWidth() - plusWidth() - gap()); }
    int contentWidth() const { return count_ * tabPitch(); }
    int maxScroll() const { return std::max(0, contentWidth() - gap() - stripWidth()); }
    Rectangle<int> plusBounds() const { return Rectangle<int>(getWidth() - plusWidth(), 0, plusWidth(), getHeight()); }
    Rectangle<int> tabBounds(int i) const { return Rectangle<int>(i * tabPitch() - scroll_, 0, tabWidth(), getHeight()); }

    int tabAt(int x, int y) const {
      if (x >= stripWidth() || y < 0 || y >= getHeight())
        return -1;
      for (int i = 0; i < count_; ++i) {
        if (tabBounds(i).contains(x, y))
          return i;
      }
      return -1;
    }

    void setScroll(int scroll) {
      int clamped = std::max(0, std::min(maxScroll(), scroll));
      if (clamped == scroll_)
        return;
      scroll_ = clamped;
      redraw();
    }

    void clampScroll() { scroll_ = std::max(0, std::min(maxScroll(), scroll_)); }

    void ensureVisible(int position) {
      if (position < 0 || position >= count_)
        return;
      int left = position * tabPitch();
      int right = left + tabWidth();
      if (left < scroll_)
        scroll_ = left;
      else if (right > scroll_ + stripWidth())
        scroll_ = right - stripWidth();
      clampScroll();
    }

    TabListener* listener_ = nullptr;
    std::unique_ptr<OpenGlImageComponent> background_;
    int count_ = 0, selected_ = 0, scroll_ = 0, drag_scroll_start_ = 0;
    bool dragged_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBranchTabs)
};

// Level, pan, mute and solo of one parallel.
class FxBranchStrip : public SynthSection {
  public:
    FxBranchStrip(int split, int branch) : SynthSection("psplit_" + std::to_string(split + 1) + "_strip_" + std::to_string(branch + 1)) {
      std::string id = "psplit_" + std::to_string(split + 1) + "_b" + std::to_string(branch + 1);
      level_ = std::make_unique<SynthSlider>(id + "_level");
      addSlider(level_.get());
      level_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      level_->snapToValue(true, 1.0);

      pan_ = std::make_unique<SynthSlider>(id + "_pan");
      addSlider(pan_.get());
      pan_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      pan_->setBipolar();
      pan_->snapToValue(true, 0.0);

      mute_ = std::make_unique<SynthButton>(id + "_mute");
      solo_ = std::make_unique<SynthButton>(id + "_solo");
      for (SynthButton* button : { mute_.get(), solo_.get() }) {
        addButton(button);
        button->setLookAndFeel(TextLookAndFeel::instance());
        button->setUiButton(true);
      }
      mute_->setText("MUTE");
      solo_->setText("SOLO");
      setSkinOverride(Skin::kAllEffects);
    }

    void paintBackground(Graphics& g) override {
      FXLOG("strip.paintBackground " + getName());
      paintBody(g);
      setLabelFont(g);
      drawLabelForComponent(g, "LEVEL", level_.get());
      drawLabelForComponent(g, "PAN", pan_.get());
      paintChildrenBackgrounds(g);
    }

    void resized() override {
      FXLOG("strip.resized " + getName() + " " + String(getWidth()) + "x" + String(getHeight()));
      int margin = std::max(3, static_cast<int>(getWidgetMargin()));
      int label_space = std::max(10, static_cast<int>(getHeight() * 0.28f));
      int knob = std::max(8, getHeight() - label_space - margin);
      int x = margin * 2;
      level_->setBounds(x, margin, knob, knob);
      x += knob + margin * 3;
      pan_->setBounds(x, margin, knob, knob);
      x += knob + margin * 4;
      int button_width = std::max(40, static_cast<int>(size_ratio_ * 64.0f));
      int button_height = std::max(14, static_cast<int>(knob * 0.55f));
      int button_y = margin + (knob - button_height) / 2;
      mute_->setBounds(x, button_y, button_width, button_height);
      solo_->setBounds(mute_->getRight() + margin, button_y, button_width, button_height);
      SynthSection::resized();
    }

  private:
    std::unique_ptr<SynthSlider> level_, pan_;
    std::unique_ptr<SynthButton> mute_, solo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBranchStrip)
};

// An FX Bin bar, or a Parallel Split (its header, the parallel selector and the controls of the shown parallel).
// The effects themselves are separate slots placed under it by the effects list.
class FxContainerPanel : public SynthSection, public Value::Listener, public FxBranchTabs::TabListener, private Timer {
  public:
    FxContainerPanel(bool is_split, int index) :
        SynthSection(std::string(is_split ? "psplit_" : "fx_bin_") + std::to_string(index + 1) + "_panel"),
        is_split_(is_split), index_(index) {
      std::string id = std::string(is_split ? "psplit_" : "fx_bin_") + std::to_string(index + 1);
      icon_ = is_split ? fx_container_ui::parallelShape() : fx_container_ui::folderShape();
      title_ = is_split ? "PARALLEL SPLIT" : "FX BIN";

      on_ = std::make_unique<SynthButton>(id + "_on");
      addButton(on_.get());
      on_->setPowerButton();
      on_->getToggleStateValue().addListener(this);

      mix_ = std::make_unique<SynthSlider>(id + "_mix");
      addSlider(mix_.get());
      mix_->setSliderStyle(Slider::LinearBar);
      mix_->setPopupPlacement(BubbleComponent::above);

      minimize_button_ = std::make_unique<OpenGlShapeButton>("minimize");
      addAndMakeVisible(minimize_button_.get());
      addOpenGlComponent(minimize_button_->getGlComponent());
      minimize_button_->addListener(this);
      minimize_button_->setShape(fx_container_ui::minusShape());

      close_button_ = std::make_unique<OpenGlShapeButton>("ungroup");
      addAndMakeVisible(close_button_.get());
      addOpenGlComponent(close_button_->getGlComponent());
      close_button_->addListener(this);
      close_button_->setShape(fx_container_ui::crossShape());

      if (is_split) {
        tabs_ = std::make_unique<FxBranchTabs>();
        tabs_->setTabListener(this);
        addSubSection(tabs_.get());
        for (int b = 0; b < vital::kMaxParallelBranches; ++b) {
          strips_[b] = std::make_unique<FxBranchStrip>(index, b);
          addSubSection(strips_[b].get(), false);
          strips_[b]->setVisible(false);
        }
      }
      setOpaque(false);
      setSkinOverride(Skin::kAllEffects);
    }

    ~FxContainerPanel() override {
      stopTimer();
      on_->getToggleStateValue().removeListener(this);
    }

    void addListener(fx_container_ui::Listener* listener) { listeners_.push_back(listener); }

    bool isSplit() const { return is_split_; }
    int index() const { return index_; }
    bool isOn() const { return on_->getToggleState(); }
    bool isMinimized() const { return minimized_; }
    const String& title() const { return title_; }

    void setTitle(const String& title) {
      String upper = title.toUpperCase();
      if (title_ == upper)
        return;
      title_ = upper;
      repaintBackground();
    }

    void setHeaderHeight(int height) {
      if (header_ == height)
        return;
      header_ = height;
      resized();
    }

    void setMinimized(bool minimized) {
      if (minimized_ == minimized)
        return;
      minimized_ = minimized;
      minimize_button_->setShape(minimized ? fx_container_ui::plusShape() : fx_container_ui::minusShape());
      refreshStrips();
      resized();
      repaintBackground();
    }

    // The parallels of a split: the branch slots in order and the position shown.
    void setBranches(const std::vector<int>& slots, int selected) {
      FXLOG("panel.setBranches " + String(index_) + " n=" + String((int) slots.size()) + " sel=" + String(selected));
      slots_ = slots;
      selected_ = selected;
      if (tabs_)
        tabs_->setState(static_cast<int>(slots.size()), selected);
      refreshStrips();
    }

    // Where the title text is, for renaming (in this panel's coordinates).
    Rectangle<int> titleHitBounds() const { return title_hit_; }

    void paintBackground(Graphics& g) override {
      FXLOG("panel.paintBackground " + getName());
      paintBody(g);
      Colour color = findColour(isOn() ? Skin::kWidgetPrimary1 : Skin::kWidgetPrimaryDisabled, true);
      g.setColour(color);
      g.fillPath(icon_, icon_.getTransformToScaleToFit(icon_bounds_, true));

      g.setColour(findColour(Skin::kHeadingText, true));
      Font font = Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 14.0f);
      float full_width = font.getStringWidthFloat(title_);
      if (title_bounds_.getWidth() > 0 && full_width > title_bounds_.getWidth())
        font = font.withPointHeight(std::max(size_ratio_ * 6.0f, size_ratio_ * 14.0f * title_bounds_.getWidth() / full_width));
      g.setFont(font);
      g.drawText(title_, title_bounds_, Justification::centredLeft, true);

      g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.7f));
      g.setFont(Fonts::instance()->proportional_regular().withPointHeight(size_ratio_ * 11.0f));
      g.drawText("MIX", mix_label_bounds_, Justification::centredRight, false);
      paintChildrenBackgrounds(g);
    }

    void resized() override {
      FXLOG("panel.resized " + getName() + " " + String(getWidth()) + "x" + String(getHeight()));
      if (header_ <= 0)
        header_ = getHeight();

      int title_width = getTitleWidth();
      int margin = std::max(2, static_cast<int>(getWidgetMargin()));
      int power = std::min(title_width, header_);
      on_->setBounds(static_cast<int>(getPowerButtonOffset()), (header_ - power) / 2, power, power);

      int button = std::max(8, static_cast<int>(title_width * 0.6f));
      int y = (header_ - button) / 2;
      close_button_->setBounds(getWidth() - button - margin, y, button, button);
      minimize_button_->setBounds(close_button_->getX() - button - margin, y, button, button);

      int mix_width = std::max(60, static_cast<int>(getWidth() * 0.18f));
      mix_->setBounds(minimize_button_->getX() - margin * 2 - mix_width, y, mix_width, button);
      int label_width = std::max(20, static_cast<int>(size_ratio_ * 30.0f));
      mix_label_bounds_ = Rectangle<int>(mix_->getX() - label_width - margin, 0, label_width, header_);

      float icon_size = header_ * 0.5f;
      icon_bounds_ = Rectangle<float>(on_->getRight() + margin * 1.5f, (header_ - icon_size) / 2.0f, icon_size, icon_size);
      int text_x = static_cast<int>(icon_bounds_.getRight()) + margin;
      title_bounds_ = Rectangle<int>(text_x, 0, std::max(0, mix_label_bounds_.getX() - margin - text_x), header_);
      Font font = Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 14.0f);
      int text_width = std::min(title_bounds_.getWidth(), static_cast<int>(font.getStringWidthFloat(title_)) + margin * 2);
      title_hit_ = title_bounds_.withWidth(text_width);

      if (is_split_ && tabs_) {
        bool show = !minimized_;
        tabs_->setVisible(show);
        if (show) {
          int padding = std::max(2, static_cast<int>(getPadding()));
          int tabs_height = std::max(14, static_cast<int>(header_ * 0.8f));
          int tabs_y = header_ + padding;
          tabs_->setBounds(margin, tabs_y, getWidth() - 2 * margin, tabs_height);
          int strip_y = tabs_y + tabs_height + padding;
          for (auto& strip : strips_)
            strip->setBounds(margin, strip_y, getWidth() - 2 * margin, std::max(1, getHeight() - strip_y - margin));
        }
      }
      SynthSection::resized();
      repaintBackground();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == minimize_button_.get())
        notify(fx_container_ui::kToggleMinimized);
      else if (clicked_button == close_button_.get())
        notify(fx_container_ui::kUngroup);
      else
        SynthSection::buttonClicked(clicked_button);
    }

    void valueChanged(Value& value) override {
      for (fx_container_ui::Listener* listener : listeners_)
        listener->containerPowerChanged();
    }

    // A single click on the title renames a bin (it waits to make sure a double click is not coming).
    void mouseUp(const MouseEvent& e) override {
      if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown() || is_split_)
        return;
      if (title_hit_.contains(e.getPosition()) && e.getNumberOfClicks() == 1)
        startTimer(MouseEvent::getDoubleClickTimeout() + 50);
    }

    void mouseDoubleClick(const MouseEvent& e) override {
      stopTimer();
      if (e.mods.isPopupMenu() || e.y > header_)
        return;
      notify(fx_container_ui::kToggleMinimized);
    }

    void mouseDown(const MouseEvent& e) override {
      if (!e.mods.isPopupMenu() || e.y > header_)
        return;

      stopTimer();
      PopupItems options;
      if (!is_split_)
        options.addItem(fx_container_ui::kRename, "Rename");
      options.addItem(fx_container_ui::kToggleMinimized, minimized_ ? "Expand" : "Collapse");
      options.addItem(fx_container_ui::kUngroup, "Ungroup");
      options.addItem(-1, "");
      options.addItem(fx_container_ui::kRemove, is_split_ ? "Remove split and its effects" : "Remove bin and its effects");
      showPopupSelector(this, e.getPosition(), options, [this](int selection) {
        if (selection > 0)
          notify(selection);
      });
    }

    // FxBranchTabs::TabListener
    void tabSelected(int position) override {
      for (fx_container_ui::Listener* listener : listeners_)
        listener->splitBranchSelected(index_, position);
    }
    void tabAddRequested() override {
      for (fx_container_ui::Listener* listener : listeners_)
        listener->splitBranchAdded(index_);
    }
    void tabRemoveRequested(int position) override {
      for (fx_container_ui::Listener* listener : listeners_)
        listener->splitBranchRemoved(index_, position);
    }

  private:
    void timerCallback() override {
      stopTimer();
      notify(fx_container_ui::kRename);
    }

    void notify(int action) {
      for (fx_container_ui::Listener* listener : listeners_)
        listener->containerAction(is_split_, index_, action);
    }

    void refreshStrips() {
      if (!is_split_)
        return;
      int shown = selected_ >= 0 && selected_ < static_cast<int>(slots_.size()) ? slots_[selected_] : -1;
      for (int b = 0; b < vital::kMaxParallelBranches; ++b)
        strips_[b]->setVisible(!minimized_ && b == shown);
    }

    bool is_split_;
    int index_;
    bool minimized_ = false;
    int header_ = 0;
    int selected_ = 0;
    std::vector<int> slots_;
    String title_;
    Path icon_;
    Rectangle<float> icon_bounds_;
    Rectangle<int> title_bounds_, title_hit_, mix_label_bounds_;
    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthSlider> mix_;
    std::unique_ptr<OpenGlShapeButton> minimize_button_, close_button_;
    std::unique_ptr<FxBranchTabs> tabs_;
    std::unique_ptr<FxBranchStrip> strips_[vital::kMaxParallelBranches];
    std::vector<fx_container_ui::Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxContainerPanel)
};
