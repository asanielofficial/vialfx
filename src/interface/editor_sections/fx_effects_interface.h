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
#include "modulation_connection_processor.h"

#include "chorus_section.h"
#include "compressor_section.h"
#include "delay_section.h"
#include "distortion_section.h"
#include "equalizer_section.h"
#include "filter_section.h"
#include "flanger_section.h"
#include "fx_container_ui.h"
#include "fx_effect_chain.h"
#include "phaser_section.h"
#include "reverb_section.h"
#include "fx_morph_section.h"
#include "freq_shifter_section.h"
#include "pitch_shifter_section.h"
#include "tremolo_section.h"
#include "utility_section.h"
#include "vibrato_section.h"
#include "spectral_warp_editor.h"
#include "table_settings_editor.h"

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
                                              "FLANGER", "PHASER", "REVERB", "MORPH", "PITCH SHIFTER", "UTILITY",
                                              "FREQ SHIFTER", "VIBRATO", "TREMOLO" };
    return kTitles[type];
  }

  inline std::string typeMenuName(int type) {
    static const char* kNames[kNumTypes] = { "Chorus", "Compressor", "Delay", "Distortion", "EQ", "Filter",
                                             "Flanger", "Phaser", "Reverb", "Morph", "Pitch Shifter", "Utility",
                                             "Freq Shifter", "Vibrato", "Tremolo" };
    return kNames[type];
  }

  inline std::string instanceTitle(int type, int instance) {
    return typeTitle(type) + (instance > 0 ? " " + std::to_string(instance + 1) : "");
  }

  // Two overlapping circles: one sound turning into another.
  inline Path morphIcon() {
    Path path;
    path.setUsingNonZeroWinding(false);
    path.addEllipse(0.02f, 0.2f, 0.6f, 0.6f);
    path.addEllipse(0.38f, 0.2f, 0.6f, 0.6f);
    return path;
  }

  // Two arrows, one up and one down: the pitch moving both ways.
  inline Path pitchShifterIcon() {
    Path path;
    path.addTriangle(0.3f, 0.05f, 0.05f, 0.45f, 0.55f, 0.45f);
    path.addTriangle(0.7f, 0.95f, 0.45f, 0.55f, 0.95f, 0.55f);
    return path;
  }

  // Three faders.
  inline Path utilityIcon() {
    Path path;
    path.addRoundedRectangle(0.14f, 0.05f, 0.12f, 0.9f, 0.05f);
    path.addRoundedRectangle(0.44f, 0.05f, 0.12f, 0.9f, 0.05f);
    path.addRoundedRectangle(0.74f, 0.05f, 0.12f, 0.9f, 0.05f);
    path.setUsingNonZeroWinding(false);
    path.addEllipse(0.07f, 0.2f, 0.26f, 0.2f);
    path.addEllipse(0.37f, 0.55f, 0.26f, 0.2f);
    path.addEllipse(0.67f, 0.3f, 0.26f, 0.2f);
    return path;
  }

  // A line of bars shifted to the right: every frequency moved by the same amount.
  inline Path freqShifterIcon() {
    Path path;
    for (int i = 0; i < 4; ++i) {
      path.addRectangle(0.05f + 0.1f * i, 0.55f - 0.12f * i, 0.07f, 0.4f + 0.12f * i);
      path.addRectangle(0.5f + 0.1f * i, 0.55f - 0.12f * i, 0.07f, 0.4f + 0.12f * i);
    }
    return path;
  }

  // A wave drawn as a thick line.
  inline Path vibratoIcon() {
    Path wave;
    wave.startNewSubPath(0.0f, 0.5f);
    for (int i = 1; i <= 24; ++i) {
      float x = i / 24.0f;
      wave.lineTo(x, 0.5f - 0.35f * std::sin(x * 4.0f * MathConstants<float>::pi));
    }
    Path path;
    PathStrokeType(0.12f).createStrokedPath(path, wave);
    return path;
  }

  // A wave whose height rises and falls: the volume swelling.
  inline Path tremoloIcon() {
    Path path;
    path.startNewSubPath(0.0f, 0.5f);
    for (int i = 0; i <= 24; ++i) {
      float x = i / 24.0f;
      path.lineTo(x, 0.5f - (0.15f + 0.3f * std::sin(x * MathConstants<float>::pi)) * std::sin(x * 8.0f * MathConstants<float>::pi));
    }
    Path stroked;
    PathStrokeType(0.1f).createStrokedPath(stroked, path);
    return stroked;
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
      case vital::constants::kMorph: return morphIcon();
      case vital::constants::kPitchShifter: return pitchShifterIcon();
      case vital::constants::kUtility: return utilityIcon();
      case vital::constants::kFreqShifter: return freqShifterIcon();
      case vital::constants::kVibrato: return vibratoIcon();
      case vital::constants::kTremolo: return tremoloIcon();
      default: return Path();
    }
  }

  inline int typeOf(int effect_id) { return effect_id / kMaxInstances; }
  inline int instanceOf(int effect_id) { return effect_id % kMaxInstances; }
  inline std::string idOf(int effect_id) { return vital::FxEffectChain::instanceId(typeOf(effect_id), instanceOf(effect_id)); }

  // Chain entries hold 1 + effect id, plus a flag for minimized effects; containers are marker entries (see
  // vital::chain_token). entryId is -1 for markers.
  constexpr int kMinimizedFlag = vital::FxEffectChain::kMinimizedFlag;
  inline bool isEffectEntry(int entry) { return entry > 0 && entry < vital::chain_token::kBinBegin; }
  inline int entryId(int entry) { return isEffectEntry(entry) ? ((entry - 1) & (kMinimizedFlag - 1)) : -1; }
  inline bool entryMinimized(int entry) { return (entry & kMinimizedFlag) != 0; }
  inline int makeEntry(int effect_id, bool minimized) { return 1 + effect_id + (minimized ? kMinimizedFlag : 0); }
  // Identifies an entry whatever its minimized flag.
  inline int entryKey(int entry) { return entry & ~kMinimizedFlag; }

  enum RowKind { kRowEffect = 0, kRowBin, kRowSplit, kRowBranch };

  // One visible row of the chain list.
  struct RowItem {
    int kind = kRowEffect;
    int key = 0;
    int depth = 0;
    bool on = false;
    bool minimized = false;
    bool active = false;
    std::string title;
    Path icon;
  };

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
        // The effect grew or shrank (the Morph effect docks its editors below itself).
        virtual void effectLayoutChanged(int effect_id) = 0;
    };

    FxEffectSlot(int effect_id, const vital::output_map& mono_modulations,
                 const vital::output_map& poly_modulations, vital::SoundEngine* engine) :
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
        case vital::constants::kMorph: {
          auto morph = std::make_unique<FxMorphSection>(title, id, instance);
          morph_ = morph.get();
          section_ = std::move(morph);
          break;
        }
        case vital::constants::kPitchShifter:
          section_ = std::make_unique<PitchShifterSection>(title, id);
          break;
        case vital::constants::kUtility:
          section_ = std::make_unique<UtilitySection>(title, id);
          break;
        case vital::constants::kFreqShifter:
          section_ = std::make_unique<FreqShifterSection>(title, id);
          break;
        case vital::constants::kVibrato:
          section_ = std::make_unique<VibratoSection>(title, id);
          break;
        case vital::constants::kTremolo:
          section_ = std::make_unique<TremoloSection>(title, id);
          break;
        default:
          section_ = std::make_unique<ReverbSection>(title, id, mono_modulations);
          break;
      }
      section_->setName(title);
      addSubSection(section_.get());
      if (morph_ != nullptr)
        setupMorphAttachments(id, instance, title, mono_modulations, poly_modulations, engine);

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

    // The Morph effect's editors (curve editor, table menus) dock below the effect, inside this slot, and the slot
    // grows to hold them. The effect list gives the slot module_height for the effect itself and asks how much
    // more it needs.
    void setModuleHeight(int height) {
      if (module_height_ == height)
        return;
      module_height_ = height;
      if (morph_ != nullptr)
        resized();
    }

    bool hasAttachments() const { return morph_ != nullptr && !minimized_ && anyAttachmentOpen(); }

    int attachmentsHeight(int knob_section_height) {
      if (!hasAttachments())
        return 0;
      return getPadding() + static_cast<int>(attachmentHeightInKnobSections() * knob_section_height);
    }

    void setTableImportCallback(
        std::function<bool(int, String, InputStream*, WavetableCreator::AudioFileLoadStyle)> callback) {
      table_import_callback_ = callback;
    }
    void setTableEditorCallback(std::function<void(int)> callback) {
      if (morph_ != nullptr)
        morph_->setTableEditorCallback(callback);
    }

    // A preset was loaded: the modes under the editors may have changed, so they close.
    void setAllValues(vital::control_map& controls) override {
      closeAttachments(false);
      SynthSection::setAllValues(controls);
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
      if (minimized)
        closeAttachments(false);
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
        if (morph_ != nullptr) {
          int module_height = anyAttachmentOpen() ? std::min(module_height_, getHeight()) : getHeight();
          section_->setBounds(0, 0, getWidth(), module_height);
          layoutAttachments(module_height);
        }
        else
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

      for (SynthSection* attachment : attachments()) {
        if (attachment != nullptr && (hit == attachment || attachment->isParentOf(hit)))
          return false;
      }

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

    // The docked editors are about this many knob sections tall (a normal effect is two).
    static float attachmentHeightInKnobSections() { return 3.3f; }

    std::vector<SynthSection*> attachments() const {
      return { curve_editor_.get(), spectral_table_menu_.get(), distort_table_menu_.get() };
    }

    bool anyAttachmentOpen() const {
      for (SynthSection* attachment : attachments()) {
        if (attachment != nullptr && attachment->isVisible())
          return true;
      }
      return false;
    }

    void setupMorphAttachments(const std::string& id, int instance, const String& title,
                               const vital::output_map& mono_modulations,
                               const vital::output_map& poly_modulations, vital::SoundEngine* engine) {
      if (engine == nullptr)
        return;

      curve_editor_ = std::make_unique<SpectralWarpEditor>(engine, vital::SoundEngine::morphLane(instance), id);
      int spectral_slot = vital::FxTables::morphSlot(instance, true);
      int distort_slot = vital::FxTables::morphSlot(instance, false);
      std::string subtitle = title.toUpperCase().toStdString();
      spectral_table_menu_ = std::make_unique<TableSettingsEditor>(TableSettingsEditor::kSpectral, id, spectral_slot,
                                                                   mono_modulations, poly_modulations, subtitle);
      distort_table_menu_ = std::make_unique<TableSettingsEditor>(TableSettingsEditor::kDistort, id, distort_slot,
                                                                  mono_modulations, poly_modulations, subtitle);
      spectral_table_menu_->setFrameSlider(morph_->getSpectralSlider());
      distort_table_menu_->setFrameSlider(morph_->getDistortionSlider());

      for (SynthSection* attachment : attachments()) {
        addSubSection(attachment);
        attachment->setVisible(false);
      }
      curve_editor_->setVisibilityCallback([this](bool) { attachmentVisibilityChanged(); });
      spectral_table_menu_->setVisibilityCallback([this](bool) { attachmentVisibilityChanged(); });
      distort_table_menu_->setVisibilityCallback([this](bool) { attachmentVisibilityChanged(); });
      spectral_table_menu_->setImportCallback(
          [this, spectral_slot](String name, InputStream* stream, WavetableCreator::AudioFileLoadStyle style) {
            return table_import_callback_ ? table_import_callback_(spectral_slot, name, stream, style) : false;
          });
      distort_table_menu_->setImportCallback(
          [this, distort_slot](String name, InputStream* stream, WavetableCreator::AudioFileLoadStyle style) {
            return table_import_callback_ ? table_import_callback_(distort_slot, name, stream, style) : false;
          });

      morph_->setAttachmentCallback([this](int kind) { toggleAttachment(kind); });
      morph_->setModesChangedCallback([this]() { refreshAttachments(); });
    }

    // The spectral side (curve editor or Spectral Table menu) takes the left, the Distort Table menu the right; one
    // alone takes the full width.
    void layoutAttachments(int module_height) {
      SynthSection* left = nullptr;
      if (curve_editor_ && curve_editor_->isVisible())
        left = curve_editor_.get();
      else if (spectral_table_menu_ && spectral_table_menu_->isVisible())
        left = spectral_table_menu_.get();
      SynthSection* right = distort_table_menu_ && distort_table_menu_->isVisible() ? distort_table_menu_.get() : nullptr;

      int padding = getPadding();
      int y = module_height + padding;
      int height = std::max(1, getHeight() - y);
      if (left && right) {
        int half = (getWidth() - padding) / 2;
        left->setBounds(0, y, half, height);
        right->setBounds(half + padding, y, getWidth() - half - padding, height);
      }
      else if (left)
        left->setBounds(0, y, getWidth(), height);
      else if (right)
        right->setBounds(0, y, getWidth(), height);
    }

    void attachmentVisibilityChanged() {
      if (suppress_layout_)
        return;
      // The slot keeps its height when a second editor opens, so the list would not re-lay this slot out: do it here.
      resized();
      for (Listener* listener : listeners_)
        listener->effectLayoutChanged(effect_id_);
    }

    // Closes every editor (when notify is true the list re-lays itself out).
    void closeAttachments(bool notify) {
      bool was_open = anyAttachmentOpen();
      suppress_layout_ = true;
      for (SynthSection* attachment : attachments()) {
        if (attachment != nullptr && attachment->isVisible())
          attachment->setVisible(false);
      }
      suppress_layout_ = false;
      if (was_open && notify)
        attachmentVisibilityChanged();
    }

    void toggleAttachment(int kind) {
      if (curve_editor_ == nullptr)
        return;

      SynthSection* target = distort_table_menu_.get();
      if (kind == FxMorphSection::kCurveEditor)
        target = curve_editor_.get();
      else if (kind == FxMorphSection::kSpectralTableMenu)
        target = spectral_table_menu_.get();

      if (target->isVisible()) {
        target->setVisible(false);   // tells the list through its visibility callback
        return;
      }

      suppress_layout_ = true;
      if (kind == FxMorphSection::kCurveEditor) {
        // The curve editor and the Spectral Table menu are both for the spectral side: one at a time.
        if (spectral_table_menu_->isVisible())
          spectral_table_menu_->setVisible(false);
        curve_editor_->showCurve(0, morph_->curveIndex());
      }
      else {
        if (kind == FxMorphSection::kSpectralTableMenu && curve_editor_->isVisible())
          curve_editor_->setVisible(false);
        target->setVisible(true);
      }
      suppress_layout_ = false;
      attachmentVisibilityChanged();
    }

    // The modes changed: an editor whose mode is gone closes; the curve editor follows a switch between the three
    // drawable modes.
    void refreshAttachments() {
      if (curve_editor_ == nullptr)
        return;

      bool changed = false;
      suppress_layout_ = true;
      if (curve_editor_->isVisible()) {
        if (morph_->isDrawableMode())
          curve_editor_->showCurve(0, morph_->curveIndex());
        else {
          curve_editor_->setVisible(false);
          changed = true;
        }
      }
      if (spectral_table_menu_->isVisible() && !morph_->isSpectralTableMode()) {
        spectral_table_menu_->setVisible(false);
        changed = true;
      }
      if (distort_table_menu_->isVisible() && !morph_->isWarpTableMode()) {
        distort_table_menu_->setVisible(false);
        changed = true;
      }
      suppress_layout_ = false;
      if (changed)
        attachmentVisibilityChanged();
    }

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
    FxMorphSection* morph_ = nullptr;   // section_, when this is a Morph effect
    std::unique_ptr<SpectralWarpEditor> curve_editor_;
    std::unique_ptr<TableSettingsEditor> spectral_table_menu_;
    std::unique_ptr<TableSettingsEditor> distort_table_menu_;
    std::function<bool(int, String, InputStream*, WavetableCreator::AudioFileLoadStyle)> table_import_callback_;
    int module_height_ = 0;
    bool suppress_layout_ = false;
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::unique_ptr<OpenGlToggleButton> power_button_;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEffectSlot)
};

// A row of the chain list (left panel): an effect, an FX Bin, a Parallel Split or one of its parallels.
class FxEffectRow : public SynthSection {
  public:
    FxEffectRow() : SynthSection("effect_row"), dragging_(false) {
      setInterceptsMouseClicks(false, false);
      setSkinOverride(Skin::kAllEffects);
      background_ = std::make_unique<OpenGlImageComponent>("background");
      addOpenGlComponent(background_.get());
      background_->setComponent(this);
      background_->paintEntireComponent(false);
    }

    void setItem(const fx_ui::RowItem& item) {
      if (item_.kind == item.kind && item_.key == item.key && item_.on == item.on &&
          item_.minimized == item.minimized && item_.active == item.active && item_.title == item.title &&
          !item_.title.empty())
        return;

      bool icon_changed = item_.key != item.key || item_.kind != item.kind || item_.title.empty();
      item_ = item;
      if (icon_changed)
        icon_ = item.icon;
      redraw();
    }

    void clear() { item_ = fx_ui::RowItem(); }
    int key() const { return item_.key; }
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
      bool branch = item_.kind == fx_ui::kRowBranch;
      bool container = item_.kind == fx_ui::kRowBin || item_.kind == fx_ui::kRowSplit;

      g.setColour(getParentComponent() ? getParentComponent()->findColour(Skin::kBody, true) : Colours::black);
      float round_amount = findValue(Skin::kBodyRounding);
      g.fillRoundedRectangle(0.0f, 0.0f, getWidth(), getHeight(), round_amount);

      Colour outline = findColour(item_.on ? Skin::kPowerButtonOn : Skin::kPowerButtonOff, true);
      if (branch && item_.active)
        outline = findColour(Skin::kWidgetPrimary1, true);
      g.setColour(outline);
      float thickness = dragging_ || (branch && item_.active) ? 2.0f : 1.0f;
      g.drawRoundedRectangle(0.5f, 0.5f, getWidth() - 1.0f, getHeight() - 1.0f, round_amount, thickness);

      g.setFont(Fonts::instance()->proportional_regular().withPointHeight(size_ratio_ * 12.0f));
      float margin = getHeight() * 0.3f;
      float text_x = margin;
      if (selecting_ && !branch) {
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

      String text = String(item_.title);
      if (item_.kind == fx_ui::kRowEffect) {
        text = text.toLowerCase();
        text = text.substring(0, 1).toUpperCase() + text.substring(1);
      }
      bool bright = item_.on || branch;
      g.setColour(findColour(bright ? Skin::kBodyText : Skin::kHeadingText, true).withMultipliedAlpha(bright ? 1.0f : 0.6f));
      g.drawText(text, text_x, 0, getWidth() - text_x - margin - getHeight(), getHeight(),
                 Justification::centredLeft, true);

      if (!branch) {
        float icon_width = kIconSize * getHeight();
        Rectangle<float> icon_bounds(getWidth() - icon_width - margin, (getHeight() - icon_width) / 2.0f,
                                     icon_width, icon_width);
        g.fillPath(icon_, icon_.getTransformToScaleToFit(icon_bounds, true));
        if (container && item_.minimized) {
          // A collapsed container shows a small "+" before its icon.
          Path plus = fx_container_ui::plusShape();
          float size = icon_width * 0.7f;
          g.fillPath(plus, plus.getTransformToScaleToFit(Rectangle<float>(icon_bounds.getX() - size - margin * 0.4f,
                                                                           (getHeight() - size) / 2.0f, size, size), true));
        }
      }
    }

    void resized() override {
      SynthSection::resized();
      background_->redrawImage(true);
    }

  private:
    fx_ui::RowItem item_;
    bool dragging_;
    bool selecting_ = false, selected_ = false;
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
    // Where a dragged row lands relative to the row it was dropped on.
    enum DropMode { kBefore = 0, kAfter, kFirstChild, kEnd };

    class Listener {
      public:
        virtual ~Listener() { }
        // type is an effect type, or fx_ui::kNumTypes for a Parallel Split.
        virtual void addEffectRequested(int type) = 0;
        // outdent: with kAfter, how many containers to leave when the row was the last one in them.
        virtual void rowMoved(int key, int target_key, int mode, int outdent) = 0;
        virtual void rowClicked(int key) = 0;
        virtual void rowsDuplicateRequested(const std::vector<int>& keys) = 0;
        virtual void rowsRemoveRequested(const std::vector<int>& keys) = 0;
        virtual void rowMinimizeToggled(int key) = 0;
        virtual void rowsGroupRequested(const std::vector<int>& keys) = 0;
        virtual void rowRenameRequested(int key) = 0;
        virtual void rowUngroupRequested(int key) = 0;
    };

    FxEffectList() : SynthSection("effect_chain"), dragged_(-1), mouse_down_y_(0), dragged_start_y_(0), moved_(false) {
      select_button_ = std::make_unique<OpenGlShapeButton>("select_effects");
      addAndMakeVisible(select_button_.get());
      addOpenGlComponent(select_button_->getGlComponent());
      select_button_->addListener(this);
      select_button_->setClickingTogglesState(true);
      select_button_->useOnColors(true);
      select_button_->setShape(selectShape());

      for (OpenGlToggleButton* button : { &duplicate_button_, &delete_button_, &group_button_ }) {
        addChildComponent(button);
        addOpenGlComponent(button->getGlComponent());
        button->addListener(this);
        button->setLookAndFeel(TextLookAndFeel::instance());
        button->setUiButton(true);
      }
      duplicate_button_.setText("DUPLICATE");
      delete_button_.setText("DELETE");
      group_button_.setText("CREATE FX BIN");

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

      for (int i = 0; i < vital::kMaxFxChainEntries; ++i) {
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

    // The visible rows, in order.
    void setChain(const std::vector<fx_ui::RowItem>& items) {
      items_ = items;
      if (static_cast<int>(items_.size()) > vital::kMaxFxChainEntries)
        items_.resize(vital::kMaxFxChainEntries);
      selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [this](int key) {
        return rowOfKey(key) < 0;
      }), selected_.end());
      for (int i = 0; i < vital::kMaxFxChainEntries; ++i) {
        if (i < static_cast<int>(items_.size())) {
          rows_[i]->setItem(items_[i]);
          rows_[i]->setSelection(selecting_, isSelected(items_[i].key));
          rows_[i]->setVisible(true);
        }
        else {
          rows_[i]->clear();
          rows_[i]->setVisible(false);
        }
      }
      layoutRows();
    }

    void paintBackground(Graphics& g) override {
      paintBody(g);
      paintChildrenBackgrounds(g);

      if (items_.empty()) {
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
        group_button_.setBounds(margin, top, getWidth() - 2 * margin, button_height);
        top = group_button_.getBottom() + margin;
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
      if (clicked_button == &duplicate_button_ || clicked_button == &delete_button_ || clicked_button == &group_button_) {
        duplicate_button_.setToggleState(false, dontSendNotification);
        delete_button_.setToggleState(false, dontSendNotification);
        group_button_.setToggleState(false, dontSendNotification);
        std::vector<int> keys = selectedKeys();
        if (keys.empty())
          return;

        if (clicked_button == &group_button_) {
          if (keys.size() < 2)
            return;
          selected_.clear();
          for (Listener* listener : listeners_)
            listener->rowsGroupRequested(keys);
          return;
        }

        bool duplicate = clicked_button == &duplicate_button_;
        selected_.clear();
        for (Listener* listener : listeners_) {
          if (duplicate)
            listener->rowsDuplicateRequested(keys);
          else
            listener->rowsRemoveRequested(keys);
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
      options.addItem(-1, "");
      options.addItem(fx_ui::kNumTypes + 1, "Parallel Split");
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

    static bool selectable(const fx_ui::RowItem& item) { return item.kind != fx_ui::kRowBranch; }

    bool isSelected(int key) const { return std::find(selected_.begin(), selected_.end(), key) != selected_.end(); }

    int rowOfKey(int key) const {
      for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (items_[i].key == key)
          return i;
      }
      return -1;
    }

    std::vector<int> selectedKeys() const {
      std::vector<int> keys;
      for (const fx_ui::RowItem& item : items_) {
        if (selectable(item) && isSelected(item.key))
          keys.push_back(item.key);
      }
      return keys;
    }

    void setSelecting(bool selecting) {
      selecting_ = selecting;
      selected_.clear();
      duplicate_button_.setVisible(selecting);
      delete_button_.setVisible(selecting);
      group_button_.setVisible(selecting);
      for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        rows_[i]->setSelection(selecting_, false);
      resized();
    }

    float rowHeight() const { return std::max(18.0f, size_ratio_ * 40.0f); }
    float rowPitch() const { return rowHeight() + std::max(2.0f, size_ratio_ * 5.0f); }
    int indentWidth() const { return std::max(6, static_cast<int>(size_ratio_ * 14.0f)); }
    int rowX(int i) const { return std::min(items_[i].depth * indentWidth(), std::max(0, viewport_.getWidth() / 2)); }

    void layoutRows() {
      int pitch = static_cast<int>(rowPitch());
      int height = static_cast<int>(rowHeight());
      int width = viewport_.getWidth();
      // One free row below the last, so a row can be dropped below everything.
      int total = (static_cast<int>(items_.size()) + 1) * pitch;
      container_->setBounds(0, 0, width, std::max(total, viewport_.getHeight()));
      for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        int x = rowX(i);
        if (i != dragged_)
          rows_[i]->setBounds(x, i * pitch, std::max(1, width - x), height);
        else
          rows_[i]->setSize(std::max(1, width - x), height);
      }
      updateScrollBar();
      repaintBackground();
    }

    void updateScrollBar() {
      scroll_bar_->setRangeLimits(0.0, container_->getHeight());
      scroll_bar_->setCurrentRange(viewport_.getViewPositionY(), viewport_.getHeight(), dontSendNotification);
    }

    int rowAt(int y) const {
      if (y < 0)
        return -1;
      int index = static_cast<int>(y / rowPitch());
      return index >= 0 && index < static_cast<int>(items_.size()) ? index : -1;
    }

    int clampedRowAt(int y) const {
      int last = static_cast<int>(items_.size()) - 1;
      return std::max(0, std::min(last, static_cast<int>(y / rowPitch())));
    }

    void rowMouseDown(const MouseEvent& e) {
      moved_ = false;
      int hit = rowAt(e.y);
      if (e.mods.isPopupMenu()) {
        // Right click: the same menu as right clicking the effect itself.
        dragged_ = -1;
        select_anchor_ = -1;
        if (hit >= 0)
          showRowMenu(hit, getLocalPoint(container_.get(), e.getPosition()));
        return;
      }
      if (selecting_ || hit < 0) {
        // Selecting: a click toggles the row and dragging over more rows paints the same choice onto them. Pressing
        // the empty space below the rows starts selecting too, so a drag from there sweeps up a group.
        dragged_ = -1;
        if (items_.empty())
          return;
        if (!selecting_) {
          setSelecting(true);
          select_button_->setToggleState(true, dontSendNotification);
        }
        select_base_ = selected_;
        select_anchor_ = hit >= 0 ? hit : clampedRowAt(e.y);
        select_paint_ = hit >= 0 ? !isSelected(items_[hit].key) : true;
        paintSelection(select_anchor_);
        return;
      }
      dragged_ = hit;

      mouse_down_y_ = e.y;
      dragged_start_y_ = rows_[dragged_]->getY();
      rows_[dragged_]->setDragging(true);
      rows_[dragged_]->toFront(false);
    }

    void showRowMenu(int index, Point<int> position) {
      if (index < 0 || index >= static_cast<int>(items_.size()))
        return;

      enum { kDuplicate = 1, kMinimize, kRemove, kGroup, kRename, kUngroup };
      const fx_ui::RowItem item = items_[index];
      std::vector<int> keys = selectedKeys();
      PopupItems options;
      if (selecting_ && keys.size() >= 2) {
        options.addItem(kGroup, "Create FX Bin");
        options.addItem(-1, "");
      }
      switch (item.kind) {
        case fx_ui::kRowEffect:
          options.addItem(kDuplicate, "Duplicate");
          options.addItem(kMinimize, item.minimized ? "Expand" : "Minimize");
          options.addItem(-1, "");
          options.addItem(kRemove, "Remove");
          break;
        case fx_ui::kRowBin:
          options.addItem(kRename, "Rename");
          options.addItem(kMinimize, item.minimized ? "Expand" : "Collapse");
          options.addItem(kUngroup, "Ungroup");
          options.addItem(-1, "");
          options.addItem(kRemove, "Remove bin and its effects");
          break;
        case fx_ui::kRowSplit:
          options.addItem(kMinimize, item.minimized ? "Expand" : "Collapse");
          options.addItem(kUngroup, "Ungroup");
          options.addItem(-1, "");
          options.addItem(kRemove, "Remove split and its effects");
          break;
        default:
          options.addItem(kRemove, "Remove parallel");
          break;
      }

      int key = item.key;
      showPopupSelector(this, position, options, [this, key, keys](int selection) {
        for (Listener* listener : listeners_) {
          if (selection == kDuplicate)
            listener->rowsDuplicateRequested({ key });
          else if (selection == kMinimize)
            listener->rowMinimizeToggled(key);
          else if (selection == kRemove)
            listener->rowsRemoveRequested({ key });
          else if (selection == kGroup)
            listener->rowsGroupRequested(keys);
          else if (selection == kRename)
            listener->rowRenameRequested(key);
          else if (selection == kUngroup)
            listener->rowUngroupRequested(key);
        }
      });
    }

    // The rows from the anchor to `current` take the painted state; the rest keep what they had before the drag.
    void paintSelection(int current) {
      int low = std::min(select_anchor_, current);
      int high = std::max(select_anchor_, current);
      selected_ = select_base_;
      for (int i = low; i <= high; ++i) {
        if (!selectable(items_[i]))
          continue;
        int key = items_[i].key;
        auto found = std::find(selected_.begin(), selected_.end(), key);
        if (select_paint_ && found == selected_.end())
          selected_.push_back(key);
        else if (!select_paint_ && found != selected_.end())
          selected_.erase(found);
      }
      for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        rows_[i]->setSelection(true, isSelected(items_[i].key));
    }

    void rowMouseDrag(const MouseEvent& e) {
      if (select_anchor_ >= 0 && !items_.empty()) {
        paintSelection(clampedRowAt(e.y));
        return;
      }
      if (dragged_ < 0)
        return;

      int pitch = static_cast<int>(rowPitch());
      int max_y = std::max(0, static_cast<int>(items_.size()) * pitch);
      int y = std::max(0, std::min(max_y, dragged_start_y_ + e.y - mouse_down_y_));
      if (std::abs(e.y - mouse_down_y_) > 3)
        moved_ = true;
      rows_[dragged_]->setTopLeftPosition(rowX(dragged_), y);
    }

    void rowMouseUp(const MouseEvent& e) {
      if (select_anchor_ >= 0) {
        select_anchor_ = -1;
        return;
      }
      if (selecting_ || dragged_ < 0)
        return;

      int source = dragged_;
      int key = items_[source].key;
      int pitch = static_cast<int>(rowPitch());
      int center = rows_[source]->getY() + pitch / 2;
      rows_[source]->setDragging(false);
      dragged_ = -1;
      layoutRows();

      if (!moved_) {
        for (Listener* listener : listeners_)
          listener->rowClicked(key);
        return;
      }
      // A branch row only selects the parallel; it cannot be moved.
      if (items_[source].kind == fx_ui::kRowBranch)
        return;

      int target = rowAt(center);
      int mode = kEnd;
      int target_key = 0;
      int outdent = 0;
      if (target >= 0) {
        const fx_ui::RowItem& item = items_[target];
        // Dragging to the left of the row it lands on leaves the containers it is the last row of.
        int diff = rowX(target) - e.x;
        if (diff > indentWidth() / 3)
          outdent = (diff + indentWidth() - 1) / indentWidth();
        if (item.key == key && outdent == 0)
          return;

        target_key = item.key;
        float fraction = (center - target * rowPitch()) / rowPitch();
        bool open_container = (item.kind == fx_ui::kRowBin || item.kind == fx_ui::kRowSplit) && !item.minimized;
        if (open_container || item.kind == fx_ui::kRowBranch)
          mode = fraction < 0.25f && item.kind != fx_ui::kRowBranch ? kBefore : kFirstChild;
        else
          mode = target > source ? kAfter : kBefore;
        if (item.key == key || outdent > 0) {
          // Only a move out of the container makes sense here.
          if (open_container || item.kind == fx_ui::kRowBranch)
            outdent = 0;
          else
            mode = kAfter;
        }
        if (mode != kAfter)
          outdent = 0;
      }
      for (Listener* listener : listeners_)
        listener->rowMoved(key, target_key, mode, outdent);
    }

    std::vector<Listener*> listeners_;
    std::unique_ptr<OpenGlShapeButton> select_button_;
    OpenGlToggleButton duplicate_button_{"duplicate_selected"}, delete_button_{"delete_selected"};
    OpenGlToggleButton group_button_{"group_selected"};
    std::unique_ptr<OpenGlToggleButton> add_button_;
    ListViewport viewport_;
    std::unique_ptr<FxRowContainer> container_;
    std::unique_ptr<FxEffectRow> rows_[vital::kMaxFxChainEntries];
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;
    std::vector<fx_ui::RowItem> items_;
    CriticalSection open_gl_critical_section_;
    int dragged_;
    int mouse_down_y_;
    int dragged_start_y_;
    bool moved_;
    bool selecting_ = false;
    int select_anchor_ = -1;       // the row a selecting drag started on
    bool select_paint_ = true;     // what the drag paints onto the rows it crosses: selected or not
    std::vector<int> select_base_; // the selection before the drag
    std::vector<int> selected_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEffectList)
};

class EffectsContainer : public SynthSection {
  public:
    struct Hint {
      Rectangle<int> bounds;
      String text;
    };

    EffectsContainer(String name) : SynthSection(name) { }

    void paintBackground(Graphics& g) override {
      g.fillAll(findColour(Skin::kBackground, true));
      paintChildrenShadows(g);
      paintChildrenBackgrounds(g);

      // A guide line down the inside of every open FX Bin and Parallel Split.
      g.setColour(findColour(Skin::kWidgetPrimary1, true).withAlpha(0.5f));
      for (const Rectangle<int>& guide : guides_)
        g.fillRoundedRectangle(guide.toFloat(), 1.0f);

      g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.45f));
      g.setFont(Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 12.0f));
      for (const Hint& hint : hints_)
        g.drawText(hint.text, hint.bounds, Justification::centred, true);
    }

    void setDecorations(const std::vector<Rectangle<int>>& guides, const std::vector<Hint>& hints) {
      guides_ = guides;
      hints_ = hints;
    }

    void addEditor(OpenGlTextEditor* editor) {
      addChildComponent(editor);
      addOpenGlComponent(editor->getImageComponent());
    }

  private:
    std::vector<Rectangle<int>> guides_;
    std::vector<Hint> hints_;
};

class EffectsInterface : public SynthSection, public FxEffectList::Listener, public FxEffectSlot::Listener,
                         public fx_container_ui::Listener, public TextEditor::Listener,
                         public ScrollBar::Listener, EffectsViewport::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void effectsMoved() = 0;
    };

    EffectsInterface(const vital::output_map& mono_modulations, const vital::output_map& poly_modulations,
                     vital::SoundEngine* engine) : SynthSection("effects") {
      container_ = std::make_unique<EffectsContainer>("container");

      addAndMakeVisible(viewport_);
      viewport_.setViewedComponent(container_.get());
      viewport_.addListener(this);
      viewport_.setScrollBarsShown(false, false, true, false);

      for (int id = 0; id < fx_ui::kPoolSize; ++id) {
        slots_[id] = std::make_unique<FxEffectSlot>(id, mono_modulations, poly_modulations, engine);
        slots_[id]->addListener(this);
        container_->addSubSection(slots_[id].get());
        slots_[id]->setVisible(false);
      }

      for (int b = 0; b < vital::kMaxFxBins; ++b) {
        bin_panels_[b] = std::make_unique<FxContainerPanel>(false, b);
        bin_panels_[b]->addListener(this);
        container_->addSubSection(bin_panels_[b].get());
        bin_panels_[b]->setVisible(false);
      }
      for (int s = 0; s < vital::kMaxParallelSplits; ++s) {
        split_panels_[s] = std::make_unique<FxContainerPanel>(true, s);
        split_panels_[s]->addListener(this);
        container_->addSubSection(split_panels_[s].get());
        split_panels_[s]->setVisible(false);
        active_slot_[s] = -1;
      }

      rename_editor_ = std::make_unique<OpenGlTextEditor>("rename_fx_bin");
      rename_editor_->addListener(this);
      rename_editor_->setSelectAllWhenFocused(true);
      rename_editor_->setMultiLine(false, false);
      rename_editor_->setJustification(Justification::centredLeft);
      container_->addEditor(rename_editor_.get());
      rename_editor_->setVisible(false);

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

    // ---- chain editing (all of it goes through applyChain / commit) ----

    void addEffectRequested(int type) override {
      if (type == fx_ui::kNumTypes) {
        addSplit();
        return;
      }
      if (type < 0 || type >= fx_ui::kNumTypes || static_cast<int>(chain_.size()) >= vital::kMaxFxChainEntries)
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

    // A row was dropped somewhere else in the list.
    void rowMoved(int key, int target_key, int mode, int outdent) override {
      int s = findKey(key);
      if (s < 0)
        return;

      int e = endOf(s);
      std::vector<int> unit(chain_.begin() + s, chain_.begin() + e + 1);
      std::vector<int> rest;
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        if (i < s || i > e)
          rest.push_back(chain_[i]);
      }

      int pos = static_cast<int>(rest.size());
      if (mode != FxEffectList::kEnd) {
        int t = findTargetPos(target_key);
        if (t < 0)
          return;

        if (t >= s && t <= e) {
          // Dropped on itself: only leaving its container counts.
          if (target_key != key || outdent <= 0 || mode != FxEffectList::kAfter)
            return;
          pos = s;
          t = -1;
        }
        int tr = t > e ? t - (e - s + 1) : t;
        if (t < 0) {
        }
        else if (mode == FxEffectList::kBefore)
          pos = tr;
        else if (mode == FxEffectList::kFirstChild)
          pos = tr + 1;
        else {
          int kind = vital::chain_token::kind(rest[tr]);
          bool begins = kind == vital::chain_token::kBinBegin || kind == vital::chain_token::kSplitBegin;
          int block_end = begins ? vital::chain_token::findEnd(rest.data(), tr, static_cast<int>(rest.size())) : tr;
          pos = std::min(static_cast<int>(rest.size()), block_end + 1);
        }

        // Past the end markers of the containers it was the last row of.
        for (int levels = outdent; levels > 0 && pos < static_cast<int>(rest.size()); --levels) {
          int kind = vital::chain_token::kind(rest[pos]);
          if (kind != vital::chain_token::kBinEnd && kind != vital::chain_token::kSplitEnd)
            break;
          ++pos;
        }
      }
      rest.insert(rest.begin() + pos, unit.begin(), unit.end());
      if (!commit(rest))
        refreshList();
    }

    void rowClicked(int key) override {
      int kind = vital::chain_token::kind(key);
      if (kind == vital::chain_token::kBranch) {
        int split = vital::chain_token::index(key);
        active_slot_[split] = vital::chain_token::slot(key);
        {
          ScopedLock lock(open_gl_critical_section_);
          refreshList();
          setEffectPositions();
        }
        int pos = splitPos(split);
        if (pos >= 0)
          scrollToPanel(split_panels_[split].get());
        return;
      }
      if (fx_ui::isEffectEntry(key)) {
        scrollToEffect(fx_ui::entryId(key));
        return;
      }
      int index = vital::chain_token::index(key);
      scrollToPanel(kind == vital::chain_token::kBinBegin ? bin_panels_[index].get() : split_panels_[index].get());
    }

    void rowsDuplicateRequested(const std::vector<int>& keys) override {
      for (int key : keys) {
        if (fx_ui::isEffectEntry(key))
          effectDuplicateRequested(fx_ui::entryId(key));
      }
    }

    void rowsRemoveRequested(const std::vector<int>& keys) override {
      for (int key : keys) {
        if (vital::chain_token::kind(key) == vital::chain_token::kBranch)
          removeBranch(vital::chain_token::index(key), vital::chain_token::slot(key));
        else
          removeUnit(key);
      }
    }

    void rowMinimizeToggled(int key) override {
      int pos = findKey(key);
      if (pos < 0)
        return;
      chain_[pos] ^= vital::chain_token::kMinimizedFlag;
      applyChain();
    }

    void rowsGroupRequested(const std::vector<int>& keys) override { createBin(keys); }

    void rowRenameRequested(int key) override {
      if (vital::chain_token::kind(key) == vital::chain_token::kBinBegin)
        startRename(vital::chain_token::index(key));
    }

    void rowUngroupRequested(int key) override { ungroup(key); }

    // fx_container_ui::Listener
    void containerAction(bool is_split, int index, int action) override {
      int pos = containerPos(is_split, index);
      if (pos < 0)
        return;

      int key = fx_ui::entryKey(chain_[pos]);
      if (action == fx_container_ui::kRename)
        startRename(index);
      else if (action == fx_container_ui::kToggleMinimized)
        rowMinimizeToggled(key);
      else if (action == fx_container_ui::kUngroup)
        ungroup(key);
      else if (action == fx_container_ui::kRemove)
        removeUnit(key);
    }

    void containerPowerChanged() override {
      ScopedLock lock(open_gl_critical_section_);
      refreshList();
    }

    void splitBranchSelected(int split, int position) override {
      int pos = splitPos(split);
      if (pos < 0)
        return;

      std::vector<int> slots = branchSlots(pos);
      if (position < 0 || position >= static_cast<int>(slots.size()))
        return;

      active_slot_[split] = slots[position];
      ScopedLock lock(open_gl_critical_section_);
      refreshList();
      setEffectPositions();
    }

    void splitBranchAdded(int split) override {
      int pos = splitPos(split);
      if (pos < 0)
        return;

      std::vector<int> slots = branchSlots(pos);
      if (static_cast<int>(slots.size()) >= vital::kMaxParallelBranches)
        return;

      int slot = 0;
      while (std::find(slots.begin(), slots.end(), slot) != slots.end())
        ++slot;

      resetControlsWithPrefix("psplit_" + std::to_string(split + 1) + "_b" + std::to_string(slot + 1) + "_");
      std::vector<int> candidate = chain_;
      candidate.insert(candidate.begin() + endOf(pos), vital::chain_token::branch(split, slot));
      active_slot_[split] = slot;
      commit(candidate);
    }

    void splitBranchRemoved(int split, int position) override {
      int pos = splitPos(split);
      if (pos < 0)
        return;

      std::vector<int> slots = branchSlots(pos);
      if (position >= 0 && position < static_cast<int>(slots.size()))
        removeBranch(split, slots[position]);
    }

    // TextEditor::Listener (the bin name)
    void textEditorReturnKeyPressed(TextEditor& editor) override { finishRename(); }
    void textEditorFocusLost(TextEditor& editor) override { finishRename(); }
    void textEditorEscapeKeyPressed(TextEditor& editor) override {
      renaming_bin_ = -1;
      rename_editor_->setVisible(false);
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
      if (copy < 0 || static_cast<int>(chain_.size()) >= vital::kMaxFxChainEntries)
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

    // A Morph effect opened or closed an editor: the slot grows or shrinks and every effect below it moves, so
    // nothing can overlap.
    void effectLayoutChanged(int effect_id) override {
      ScopedLock lock(open_gl_critical_section_);
      setEffectPositions();
      scrollToEffect(effect_id);
    }

    void setTableImportCallback(
        std::function<bool(int, String, InputStream*, WavetableCreator::AudioFileLoadStyle)> callback) {
      for (auto& slot : slots_)
        slot->setTableImportCallback(callback);
    }

    void setTableEditorCallback(std::function<void(int)> callback) {
      for (auto& slot : slots_)
        slot->setTableEditorCallback(callback);
    }

    void setFocus() { grabKeyboardFocus(); }

    // Tremolo and Vibrato have unit selectors under the knobs.
    static constexpr int kUnitSelectorExtraHeightValue = 44;
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
      // The Morph effect has one row of controls, so it is shorter.
      int morph_height = std::min(effect_height, static_cast<int>(FxMorphSection::heightInKnobSections() * knob_section_height));
      int header = static_cast<int>(getTitleWidth() * 1.35f);
      int indent = std::max(8, static_cast<int>(size_ratio_ * 14.0f));
      int inner_padding = std::max(2, padding);
      int tabs_height = std::max(14, static_cast<int>(header * 0.8f));
      int split_height = header + inner_padding + tabs_height + inner_padding + knob_section_height +
                         std::max(2, static_cast<int>(getWidgetMargin()));
      int y = 0;

      Point<int> position = viewport_.getViewPosition();

      struct Frame {
        bool is_split = false;
        int index = 0;
        bool minimized = false;
        bool placed = false;
        int cur_slot = 0;
        int active_slot = 0;
        int content_count = 0;
        int guide_top = 0;
      };
      std::vector<Frame> stack;
      std::vector<Rectangle<int>> guides;
      std::vector<EffectsContainer::Hint> hints;
      std::vector<bool> shown(fx_ui::kPoolSize, false);
      bool bin_shown[vital::kMaxFxBins] = {};
      bool split_shown[vital::kMaxParallelSplits] = {};

      auto frameHides = [](const Frame& f) {
        return f.minimized || (f.is_split && f.cur_slot != f.active_slot);
      };
      auto hidden = [&]() {
        for (const Frame& f : stack) {
          if (frameHides(f))
            return true;
        }
        return false;
      };
      auto xFor = [&](int depth) { return shadow_width + depth * indent; };
      auto widthFor = [&](int depth) { return std::max(10, effect_width - depth * indent); };
      auto closeContent = [&](Frame& f) {
        if (f.content_count > 0)
          return;
        int depth = static_cast<int>(stack.size());
        EffectsContainer::Hint hint;
        hint.bounds = Rectangle<int>(xFor(depth), y, widthFor(depth), header);
        hint.text = "Drag effects here from the list";
        hints.push_back(hint);
        y += header + padding;
      };

      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        int token = chain_[i];
        if (fx_ui::isEffectEntry(token)) {
          int id = fx_ui::entryId(token);
          if (id < 0 || id >= fx_ui::kPoolSize || hidden())
            continue;

          int depth = static_cast<int>(stack.size());
          FxEffectSlot* slot = slots_[id].get();
          bool minimized = fx_ui::entryMinimized(token);
          slot->setVisible(true);
          slot->setMinimized(minimized);
          int type = fx_ui::typeOf(id);
          int height = minimized ? slot->minimizedHeight()
                                 : (type == vital::constants::kMorph ? morph_height
                                    : (type == vital::constants::kTremolo || type == vital::constants::kVibrato)
                                        ? effect_height + static_cast<int>(kUnitSelectorExtraHeightValue * getSizeRatio())
                                        : effect_height);
          slot->setModuleHeight(height);
          height += slot->attachmentsHeight(knob_section_height);
          slot->setBounds(xFor(depth), y, widthFor(depth), height);
          y += height + padding;
          shown[id] = true;
          if (!stack.empty())
            stack.back().content_count++;
          continue;
        }

        int kind = vital::chain_token::kind(token);
        int index = vital::chain_token::index(token);
        if (kind == vital::chain_token::kBinBegin) {
          Frame frame;
          frame.index = index;
          frame.minimized = vital::chain_token::minimized(token);
          if (!hidden() && index < vital::kMaxFxBins) {
            int depth = static_cast<int>(stack.size());
            FxContainerPanel* panel = bin_panels_[index].get();
            panel->setVisible(true);
            panel->setHeaderHeight(header);
            panel->setMinimized(frame.minimized);
            panel->setTitle(binName(index));
            panel->setBounds(xFor(depth), y, widthFor(depth), header);
            bin_shown[index] = true;
            y += header + padding;
            frame.placed = true;
            frame.guide_top = y;
            if (!stack.empty())
              stack.back().content_count++;
          }
          stack.push_back(frame);
        }
        else if (kind == vital::chain_token::kSplitBegin) {
          Frame frame;
          frame.is_split = true;
          frame.index = index;
          frame.minimized = vital::chain_token::minimized(token);
          frame.cur_slot = vital::chain_token::slot(token);
          std::vector<int> slots = branchSlots(i);
          frame.active_slot = resolveActive(index, slots);
          if (!hidden() && index < vital::kMaxParallelSplits) {
            int depth = static_cast<int>(stack.size());
            FxContainerPanel* panel = split_panels_[index].get();
            int height = frame.minimized ? header : split_height;
            panel->setVisible(true);
            panel->setHeaderHeight(header);
            panel->setMinimized(frame.minimized);
            panel->setBounds(xFor(depth), y, widthFor(depth), height);
            int selected = static_cast<int>(std::find(slots.begin(), slots.end(), frame.active_slot) - slots.begin());
            panel->setBranches(slots, selected);
            split_shown[index] = true;
            y += height + padding;
            frame.placed = true;
            frame.guide_top = y;
            if (!stack.empty())
              stack.back().content_count++;
          }
          stack.push_back(frame);
        }
        else if (kind == vital::chain_token::kBranch) {
          if (!stack.empty() && stack.back().is_split) {
            Frame& frame = stack.back();
            bool was_visible = frame.placed && !frame.minimized && frame.cur_slot == frame.active_slot;
            if (was_visible)
              closeContent(frame);
            frame.cur_slot = vital::chain_token::slot(token);
            frame.content_count = 0;
          }
        }
        else if (kind == vital::chain_token::kBinEnd || kind == vital::chain_token::kSplitEnd) {
          if (stack.empty())
            continue;

          Frame frame = stack.back();
          bool content_visible = frame.placed && !frame.minimized && (!frame.is_split || frame.cur_slot == frame.active_slot);
          if (content_visible) {
            stack.back().content_count = frame.content_count;
            closeContent(stack.back());
            int depth = static_cast<int>(stack.size()) - 1;
            int guide_height = y - padding - frame.guide_top;
            if (guide_height > 0)
              guides.push_back(Rectangle<int>(xFor(depth) + indent / 2 - 1, frame.guide_top, 2, guide_height));
          }
          stack.pop_back();
        }
      }

      for (int id = 0; id < fx_ui::kPoolSize; ++id) {
        if (!shown[id])
          slots_[id]->setVisible(false);
      }
      for (int b = 0; b < vital::kMaxFxBins; ++b) {
        if (!bin_shown[b])
          bin_panels_[b]->setVisible(false);
      }
      for (int s = 0; s < vital::kMaxParallelSplits; ++s) {
        if (!split_shown[s])
          split_panels_[s]->setVisible(false);
      }

      container_->setDecorations(guides, hints);
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
    // ---- the chain as tokens ----

    int findKey(int key) const {
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        if (fx_ui::entryKey(chain_[i]) == key)
          return i;
      }
      return -1;
    }

    // The first parallel of a split has no marker of its own: its row targets the split's begin marker.
    int findTargetPos(int key) const {
      int pos = findKey(key);
      if (pos >= 0 || vital::chain_token::kind(key) != vital::chain_token::kBranch)
        return pos;
      return splitPos(vital::chain_token::index(key));
    }

    int containerPos(bool is_split, int index) const {
      int wanted = is_split ? vital::chain_token::kSplitBegin : vital::chain_token::kBinBegin;
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        if (vital::chain_token::kind(chain_[i]) == wanted && vital::chain_token::index(chain_[i]) == index)
          return i;
      }
      return -1;
    }

    int splitPos(int split) const { return containerPos(true, split); }

    // The last entry of the unit that starts at pos: itself, or the end marker of a container.
    int endOf(int pos) const {
      int kind = vital::chain_token::kind(chain_[pos]);
      if (kind == vital::chain_token::kBinBegin || kind == vital::chain_token::kSplitBegin)
        return std::min(static_cast<int>(chain_.size()) - 1,
                        vital::chain_token::findEnd(chain_.data(), pos, static_cast<int>(chain_.size())));
      return pos;
    }

    // The branch slots of the split that begins at pos, in order.
    std::vector<int> branchSlots(int pos) const {
      std::vector<int> slots;
      int split = vital::chain_token::index(chain_[pos]);
      slots.push_back(vital::chain_token::slot(chain_[pos]));
      int end = endOf(pos);
      int nesting = 0;
      for (int i = pos + 1; i < end; ++i) {
        int kind = vital::chain_token::kind(chain_[i]);
        if (kind == vital::chain_token::kBinBegin || kind == vital::chain_token::kSplitBegin)
          ++nesting;
        else if (kind == vital::chain_token::kBinEnd || kind == vital::chain_token::kSplitEnd)
          --nesting;
        else if (kind == vital::chain_token::kBranch && nesting == 0 && vital::chain_token::index(chain_[i]) == split)
          slots.push_back(vital::chain_token::slot(chain_[i]));
      }
      return slots;
    }

    // The branch shown for a split: the remembered one if it still exists, else the first.
    int resolveActive(int split, const std::vector<int>& slots) {
      if (split < 0 || split >= vital::kMaxParallelSplits || slots.empty())
        return 0;
      if (std::find(slots.begin(), slots.end(), active_slot_[split]) == slots.end())
        active_slot_[split] = slots[0];
      return active_slot_[split];
    }

    int firstFreeContainer(bool is_split) const {
      int count = is_split ? vital::kMaxParallelSplits : vital::kMaxFxBins;
      for (int i = 0; i < count; ++i) {
        if (containerPos(is_split, i) < 0)
          return i;
      }
      return -1;
    }

    // Takes a new chain only if every entry survives the engine's well-formedness check.
    bool commit(const std::vector<int>& candidate) {
      if (static_cast<int>(candidate.size()) > vital::kMaxFxChainEntries)
        return false;

      int cleaned[vital::kMaxFxChainEntries + 8];
      int count = vital::chain_token::sanitize(candidate.data(), static_cast<int>(candidate.size()), cleaned,
                                               fx_ui::kPoolSize);
      if (count != static_cast<int>(candidate.size()))
        return false;

      chain_ = candidate;
      applyChain();
      return true;
    }

    String binName(int index) {
      SynthGuiInterface* parent = gui();
      String name = parent ? parent->getSynth()->getFxBinName(index) : String();
      if (name.isNotEmpty())
        return name;
      return index == 0 ? String("FX Bin") : "FX Bin " + String(index + 1);
    }

    void resetControlsWithPrefix(const std::string& prefix) {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      std::vector<std::string> names;
      for (auto& control : parent->getSynth()->getControls()) {
        if (control.first.compare(0, prefix.size(), prefix) == 0)
          names.push_back(control.first);
      }
      for (const std::string& name : names) {
        for (vital::ModulationConnection* connection : parent->getSynth()->getDestinationConnections(name))
          parent->disconnectModulation(connection);
        setControl(name, vital::Parameters::getDetails(name).default_value);
      }
    }

    void resetContainer(bool is_split, int index) {
      resetControlsWithPrefix(std::string(is_split ? "psplit_" : "fx_bin_") + std::to_string(index + 1) + "_");
      if (!is_split) {
        SynthGuiInterface* parent = gui();
        if (parent)
          parent->getSynth()->setFxBinName(index, String());
      }
      else if (index >= 0 && index < vital::kMaxParallelSplits)
        active_slot_[index] = -1;
    }

    // Everything in the entries [from, to] goes back to the pool.
    void resetEntries(int from, int to) {
      for (int i = from; i <= to && i < static_cast<int>(chain_.size()); ++i) {
        int token = chain_[i];
        int kind = vital::chain_token::kind(token);
        if (fx_ui::isEffectEntry(token))
          resetEffect(fx_ui::entryId(token));
        else if (kind == vital::chain_token::kBinBegin)
          resetContainer(false, vital::chain_token::index(token));
        else if (kind == vital::chain_token::kSplitBegin)
          resetContainer(true, vital::chain_token::index(token));
      }
    }

    void removeUnit(int key) {
      int s = findKey(key);
      if (s < 0)
        return;

      int e = endOf(s);
      resetEntries(s, e);
      chain_.erase(chain_.begin() + s, chain_.begin() + e + 1);
      applyChain();
    }

    // The container's markers go; what was inside stays, in order.
    void ungroup(int key) {
      int s = findKey(key);
      if (s < 0)
        return;

      int e = endOf(s);
      int kind = vital::chain_token::kind(chain_[s]);
      int index = vital::chain_token::index(chain_[s]);
      bool is_split = kind == vital::chain_token::kSplitBegin;
      std::vector<int> candidate;
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        if (i == s || i == e)
          continue;
        if (is_split && vital::chain_token::kind(chain_[i]) == vital::chain_token::kBranch &&
            vital::chain_token::index(chain_[i]) == index && i > s && i < e)
          continue;
        candidate.push_back(chain_[i]);
      }
      if (commit(candidate))
        resetContainer(is_split, index);
    }

    void removeBranch(int split, int slot) {
      int pos = splitPos(split);
      if (pos < 0)
        return;

      std::vector<int> slots = branchSlots(pos);
      if (slots.size() <= 1)
        return;  // the last parallel stays; remove the split instead

      int end = endOf(pos);
      // The branch starts at its marker (or at the split's begin marker) and runs to the next branch's marker.
      int first = -1, next = end;
      int nesting = 0;
      bool first_branch = vital::chain_token::slot(chain_[pos]) == slot;
      if (first_branch)
        first = pos;
      for (int i = pos + 1; i < end; ++i) {
        int kind = vital::chain_token::kind(chain_[i]);
        if (kind == vital::chain_token::kBinBegin || kind == vital::chain_token::kSplitBegin)
          ++nesting;
        else if (kind == vital::chain_token::kBinEnd || kind == vital::chain_token::kSplitEnd)
          --nesting;
        else if (kind == vital::chain_token::kBranch && nesting == 0 && vital::chain_token::index(chain_[i]) == split) {
          if (first >= 0) {
            next = i;
            break;
          }
          if (vital::chain_token::slot(chain_[i]) == slot)
            first = i;
        }
      }
      if (first < 0)
        return;

      resetEntries(first, next - 1);
      std::vector<int> candidate = chain_;
      if (first_branch) {
        // The next parallel becomes the first: its marker turns into the split's begin marker.
        bool minimized = vital::chain_token::minimized(chain_[pos]);
        int next_slot = vital::chain_token::slot(chain_[next]);
        candidate.erase(candidate.begin() + first, candidate.begin() + next + 1);
        candidate.insert(candidate.begin() + first, vital::chain_token::splitBegin(split, next_slot, minimized));
      }
      else
        candidate.erase(candidate.begin() + first, candidate.begin() + next);

      resetControlsWithPrefix("psplit_" + std::to_string(split + 1) + "_b" + std::to_string(slot + 1) + "_");
      active_slot_[split] = -1;
      commit(candidate);
    }

    void addSplit() {
      int split = firstFreeContainer(true);
      if (split < 0 || static_cast<int>(chain_.size()) + 3 > vital::kMaxFxChainEntries)
        return;

      resetContainer(true, split);
      std::vector<int> candidate = chain_;
      candidate.push_back(vital::chain_token::splitBegin(split, 0));
      candidate.push_back(vital::chain_token::branch(split, 1));
      candidate.push_back(vital::chain_token::splitEnd(split));
      active_slot_[split] = 0;
      bool committed = commit(candidate);
      if (committed)
        scrollToPanel(split_panels_[split].get());
    }

    // The selected units are moved into a new bin at the place of the first of them, keeping their order.
    void createBin(const std::vector<int>& keys) {
      std::vector<int> positions;
      for (int key : keys) {
        int pos = findKey(key);
        if (pos >= 0)
          positions.push_back(pos);
      }
      std::sort(positions.begin(), positions.end());

      std::vector<std::pair<int, int>> ranges;
      for (int pos : positions) {
        if (!ranges.empty() && pos <= ranges.back().second)
          continue;  // inside a unit that is already selected
        ranges.push_back({ pos, endOf(pos) });
      }
      int bin = firstFreeContainer(false);
      if (ranges.empty() || bin < 0)
        return;

      std::vector<int> unit, rest;
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        bool inside = false;
        for (const auto& range : ranges) {
          if (i >= range.first && i <= range.second) {
            inside = true;
            break;
          }
        }
        (inside ? unit : rest).push_back(chain_[i]);
      }
      std::vector<int> bin_tokens;
      bin_tokens.push_back(vital::chain_token::binBegin(bin));
      bin_tokens.insert(bin_tokens.end(), unit.begin(), unit.end());
      bin_tokens.push_back(vital::chain_token::binEnd(bin));
      rest.insert(rest.begin() + ranges[0].first, bin_tokens.begin(), bin_tokens.end());

      resetContainer(false, bin);
      if (commit(rest))
        scrollToPanel(bin_panels_[bin].get());
    }

    // ---- renaming a bin ----

    void startRename(int bin) {
      if (bin < 0 || bin >= vital::kMaxFxBins || !bin_panels_[bin]->isVisible())
        return;

      FxContainerPanel* panel = bin_panels_[bin].get();
      scrollToPanel(panel);
      Rectangle<int> hit = panel->titleHitBounds();
      int width = std::max(hit.getWidth(), static_cast<int>(size_ratio_ * 160.0f));
      renaming_bin_ = bin;
      rename_editor_->setBounds(panel->getX() + hit.getX(), panel->getY() + hit.getY(), width, hit.getHeight());
      rename_editor_->setColour(CaretComponent::caretColourId, findColour(Skin::kTextEditorCaret, true));
      rename_editor_->setColour(TextEditor::textColourId, findColour(Skin::kBodyText, true));
      rename_editor_->setColour(TextEditor::highlightedTextColourId, findColour(Skin::kBodyText, true));
      rename_editor_->setColour(TextEditor::highlightColourId, findColour(Skin::kTextEditorSelection, true));
      rename_editor_->setText(binName(bin), dontSendNotification);
      rename_editor_->setVisible(true);
      rename_editor_->toFront(false);
      rename_editor_->grabKeyboardFocus();
      rename_editor_->selectAll();
    }

    void finishRename() {
      if (renaming_bin_ < 0)
        return;

      int bin = renaming_bin_;
      renaming_bin_ = -1;
      String text = rename_editor_->getText().trim();
      rename_editor_->setVisible(false);
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      String fallback = bin == 0 ? String("FX Bin") : "FX Bin " + String(bin + 1);
      parent->getSynth()->setFxBinName(bin, text == fallback ? String() : text);
      bin_panels_[bin]->setTitle(binName(bin));
      ScopedLock lock(open_gl_critical_section_);
      refreshList();
    }

    void scrollToPanel(FxContainerPanel* panel) {
      if (panel == nullptr || !panel->isVisible())
        return;

      int max_y = std::max(0, container_->getHeight() - viewport_.getHeight());
      viewport_.setViewPosition(0, std::max(0, std::min(max_y, panel->getY())));
    }

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
      copyModulations(from, to, controls);
      syncActive(to);
    }

    // The copy gets the same modulation mappings as the original: every source that drives one of its knobs drives
    // the same knob of the copy, with the same amount, polarity, stereo, bypass and curve.
    void copyModulations(int from, int to, const vital::control_map& controls) {
      int type = fx_ui::typeOf(from), instance = fx_ui::instanceOf(from);
      std::string from_prefix = fx_ui::idOf(from) + "_", to_prefix = fx_ui::idOf(to) + "_";
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      SynthBase* synth = parent->getSynth();
      struct Mapping { std::string source, from, to; int index; };
      std::vector<Mapping> mappings;
      for (vital::ModulationConnection* connection : synth->getModulationConnections()) {
        const std::string& destination = connection->destination_name;
        if (destination.empty() || !fx_ui::belongsTo(destination, type, instance) ||
            destination.compare(0, from_prefix.size(), from_prefix) != 0)
          continue;

        std::string target = to_prefix + destination.substr(from_prefix.size());
        if (controls.count(target) == 0)
          continue;
        mappings.push_back({ connection->source_name, destination, target,
                             connection->modulation_processor->index() });
      }

      // Whatever the copy was modulated by before is replaced.
      for (vital::ModulationConnection* connection : synth->getModulationConnections()) {
        if (!connection->destination_name.empty() && connection->destination_name.compare(0, to_prefix.size(), to_prefix) == 0 &&
            fx_ui::belongsTo(connection->destination_name, fx_ui::typeOf(to), fx_ui::instanceOf(to)))
          parent->disconnectModulation(connection->source_name, connection->destination_name);
      }

      for (const Mapping& mapping : mappings) {
        parent->connectModulation(mapping.source, mapping.to);
        int new_index = synth->getConnectionIndex(mapping.source, mapping.to);
        if (new_index < 0)
          continue;

        std::string old_number = std::to_string(mapping.index + 1);
        std::string new_number = std::to_string(new_index + 1);
        auto value_of = [&](const std::string& suffix) {
          auto found = controls.find("modulation_" + old_number + "_" + suffix);
          return found == controls.end() ? 0.0f : static_cast<float>(found->second->value());
        };
        parent->setModulationValues(mapping.source, mapping.to, value_of("amount"), value_of("bipolar") != 0.0f,
                                    value_of("stereo") != 0.0f, value_of("bypass") != 0.0f);
        setControl("modulation_" + new_number + "_power", value_of("power"));

        vital::ModulationConnection* from_connection = synth->getModulationBank().atIndex(mapping.index);
        vital::ModulationConnection* to_connection = synth->getModulationBank().atIndex(new_index);
        to_connection->modulation_processor->lineMapGenerator()->jsonToState(
            from_connection->modulation_processor->lineMapGenerator()->stateToJson());
      }
      parent->notifyModulationsChanged();
    }

    void loadChainFromSynth() {
      SynthGuiInterface* parent = gui();
      if (parent == nullptr)
        return;

      chain_loaded_ = true;
      int entries[vital::FxEffectChain::kMaxChain];
      int count = parent->getSynth()->getEffectChain(entries);
      int cleaned[vital::FxEffectChain::kMaxChain + 8];
      count = vital::chain_token::sanitize(entries, count, cleaned, fx_ui::kPoolSize);
      chain_.assign(cleaned, cleaned + count);
      for (int& slot : active_slot_)
        slot = -1;

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

    // The rows of the list: everything except what is inside a collapsed container.
    void refreshList() {
      namespace ct = vital::chain_token;
      std::vector<fx_ui::RowItem> items;
      int depth = 0;
      int skip = 0;
      for (int i = 0; i < static_cast<int>(chain_.size()); ++i) {
        int token = chain_[i];
        int kind = ct::kind(token);
        bool is_begin = kind == ct::kBinBegin || kind == ct::kSplitBegin;
        bool is_end = kind == ct::kBinEnd || kind == ct::kSplitEnd;
        if (skip > 0) {
          if (is_begin)
            ++skip;
          else if (is_end)
            --skip;
          continue;
        }

        fx_ui::RowItem item;
        item.key = fx_ui::entryKey(token);
        item.depth = depth;
        item.minimized = fx_ui::entryMinimized(token);
        if (fx_ui::isEffectEntry(token)) {
          int id = fx_ui::entryId(token);
          if (id < 0 || id >= fx_ui::kPoolSize)
            continue;
          item.kind = fx_ui::kRowEffect;
          item.on = slots_[id]->isOn();
          item.title = fx_ui::instanceTitle(fx_ui::typeOf(id), fx_ui::instanceOf(id));
          item.icon = fx_ui::typeIcon(fx_ui::typeOf(id));
          items.push_back(item);
        }
        else if (kind == ct::kBinBegin) {
          int index = ct::index(token);
          item.kind = fx_ui::kRowBin;
          item.on = index < vital::kMaxFxBins && bin_panels_[index]->isOn();
          item.title = binName(index).toStdString();
          item.icon = fx_container_ui::folderShape();
          items.push_back(item);
          if (item.minimized)
            skip = 1;
          else
            ++depth;
        }
        else if (kind == ct::kSplitBegin) {
          int index = ct::index(token);
          bool on = index < vital::kMaxParallelSplits && split_panels_[index]->isOn();
          item.kind = fx_ui::kRowSplit;
          item.on = on;
          item.title = "Parallel Split";
          item.icon = fx_container_ui::parallelShape();
          items.push_back(item);
          if (item.minimized) {
            skip = 1;
            continue;
          }

          std::vector<int> slots = branchSlots(i);
          int active = resolveActive(index, slots);
          fx_ui::RowItem branch;
          branch.kind = fx_ui::kRowBranch;
          branch.key = ct::branch(index, ct::slot(token));
          branch.depth = depth + 1;
          branch.on = on;
          branch.active = ct::slot(token) == active;
          branch.title = "Parallel 1";
          items.push_back(branch);
          depth += 2;
        }
        else if (kind == ct::kBranch) {
          int index = ct::index(token);
          int split = splitPos(index);
          if (split < 0)
            continue;

          std::vector<int> slots = branchSlots(split);
          int position = static_cast<int>(std::find(slots.begin(), slots.end(), ct::slot(token)) - slots.begin());
          fx_ui::RowItem branch;
          branch.kind = fx_ui::kRowBranch;
          branch.key = fx_ui::entryKey(token);
          branch.depth = std::max(0, depth - 1);
          branch.on = index < vital::kMaxParallelSplits && split_panels_[index]->isOn();
          branch.active = ct::slot(token) == resolveActive(index, slots);
          branch.title = "Parallel " + std::to_string(position + 1);
          items.push_back(branch);
        }
        else if (is_end) {
          depth = std::max(0, depth - (kind == ct::kBinEnd ? 1 : 2));
        }
      }
      chain_list_->setChain(items);
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
    std::unique_ptr<FxContainerPanel> bin_panels_[vital::kMaxFxBins];
    std::unique_ptr<FxContainerPanel> split_panels_[vital::kMaxParallelSplits];
    std::unique_ptr<OpenGlTextEditor> rename_editor_;
    int renaming_bin_ = -1;
    int active_slot_[vital::kMaxParallelSplits];
    std::unique_ptr<FxEffectList> chain_list_;
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;
    std::vector<int> chain_;
    bool chain_loaded_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsInterface)
};
