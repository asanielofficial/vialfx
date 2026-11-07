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

#include "skin.h"
#include "default_look_and_feel.h"
#include "full_interface.h"
#include "synth_constants.h"
#include "synth_section.h"
#include "wavetable_3d.h"

namespace {
  const std::string kOverrideNames[Skin::kNumSectionOverrides] = {
    "All",
    "Logo",
    "Header",
    "Overlays",
    "Oscillator",
    "Sample",
    "Sub",
    "Filter",
    "Envelope",
    "Lfo",
    "RandomLfo",
    "Voice",
    "Macro",
    "Keyboard",
    "All Effects",
    "Chorus",
    "Compressor",
    "Delay",
    "Distortion",
    "Equalizer",
    "Effects Filter",
    "Flanger",
    "Phaser",
    "Reverb",
    "Modulation Drag Drop",
    "Modulation Matrix",
    "Preset Browser",
    "Popup Browser",
    "Advanced",
    "Wavetable Editor",
  };

  const std::string kValueNames[Skin::kNumSkinValueIds] = {
    "Body Rounding",
    "Label Height",
    "Label Background Height",
    "Label Rounding",
    "Label Offset",
    "Text Component Label Offset",
    "Rotary Option X Offset",
    "Rotary Option Y Offset",
    "Rotary Option Width",
    "Title Width",
    "Padding",
    "Large Padding",
    "Slider Width",
    "Text Component Height",
    "Text Component Offset",
    "Text Component Font Size",
    "Text Button Height",
    "Button Font Size",
    "Knob Arc Size",
    "Knob Arc Thickness",
    "Knob Body Size",
    "Knob Handle Length",
    "Knob Mod Amount Arc Size",
    "Knob Mod Amount Arc Thickness",
    "Knob Mod Meter Arc Size",
    "Knob Mod Meter Arc Thickness",
    "Knob Offset",
    "Knob Section Height",
    "Knob Shadow Width",
    "Knob Shadow Offset",
    "Modulation Button Width",
    "Modulation Font Size",
    "Widget Margin",
    "Widget Rounded Corner",
    "Widget Line Width",
    "Widget Line Boost",
    "Widget Fill Center",
    "Widget Fill Fade",
    "Widget Fill Boost",
    "Wavetable Horizontal Angle",
    "Wavetable Vertical Angle",
    "Wavetable Draw Width",
    "Wavetable Draw Height",
    "Wavetable Y Offset",
  };

  const std::string kColorNames[Skin::kNumColors] = {
    "Background",
    "Body",
    "Body Heading Background",
    "Heading Text",
    "Preset Text",
    "Body Text",
    "Border",
    "Label Background",
    "Label Connection",
    "Power Button On",
    "Power Button Off",

    "Overlay Screen",
    "Lighten Screen",
    "Shadow",
    "Popup Selector Background",
    "Popup Background",
    "Popup Border",

    "Text Component Background",
    "Text Component Text",

    "Rotary Arc",
    "Rotary Arc Disabled",
    "Rotary Arc Unselected",
    "Rotary Arc Unselected Disabled",
    "Rotary Hand",
    "Rotary Body",
    "Rotary Body Border",

    "Linear Slider",
    "Linear Slider Disabled",
    "Linear Slider Unselected",
    "Linear Slider Thumb",
    "Linear Slider Thumb Disabled",

    "Widget Center Line",
    "Widget Primary 1",
    "Widget Primary 2",
    "Widget Primary Disabled",
    "Widget Secondary 1",
    "Widget Secondary 2",
    "Widget Secondary Disabled",
    "Widget Accent 1",
    "Widget Accent 2",
    "Widget Background",

    "Modulation Meter",
    "Modulation Meter Left",
    "Modulation Meter Right",
    "Modulation Meter Control",
    "Modulation Button Selected",
    "Modulation Button Dragging",
    "Modulation Button Unselected",

    "Icon Selector Icon",

    "Icon Button Off",
    "Icon Button Off Hover",
    "Icon Button Off Pressed",
    "Icon Button On",
    "Icon Button On Hover",
    "Icon Button On Pressed",

    "UI Button",
    "UI Button Text",
    "UI Button Hover",
    "UI Button Press",
    "UI Action Button",
    "UI Action Button Hover",
    "UI Action Button Press",

    "Text Editor Background",
    "Text Editor Border",
    "Text Editor Caret",
    "Text Editor Selection"
  };
} // namespace

bool Skin::shouldScaleValue(ValueId value_id) {
  return value_id != kWidgetFillFade && value_id != kWidgetFillBoost &&
         value_id != kWidgetLineBoost && value_id != kKnobHandleLength &&
         value_id != kWidgetFillCenter && value_id != kFrequencyDisplay &&
         value_id != kWavetableHorizontalAngle && value_id != kWavetableVerticalAngle;
}

Skin::Skin() {
  File default_skin = LoadSave::getDefaultSkin();
  if (default_skin.exists()) {
    if (!loadFromFile(default_skin))
      loadDefaultSkin();
  }
  else
    loadDefaultSkin();

  copyValuesToLookAndFeel(DefaultLookAndFeel::instance());
}

void Skin::collectGlows(std::vector<GlowEntry>& glows) const {
  for (int i = 0; i < kNumColors; ++i) {
    ColorId id = static_cast<ColorId>(kInitialColor + i);
    if (glow_[i] > 0.0f)
      glows.push_back({ colors_[i], glow_[i], glow_size_[i] });

    for (int section = 1; section < kNumSectionOverrides; ++section) {
      auto it = color_overrides_[section].find(id);
      if (it == color_overrides_[section].end())
        continue;

      float glow = getGlow(section, id);
      if (glow > 0.0f)
        glows.push_back({ it->second, glow, getGlowSize(section, id) });
    }
  }
}

String Skin::Gradient::toString() const {
  StringArray parts;
  for (const Colour& stop : stops)
    parts.add(stop.toString());
  return parts.joinIntoString(",") + ";" + String(angle);
}

Skin::Gradient Skin::Gradient::fromString(const String& text) {
  Gradient gradient;
  StringArray halves = StringArray::fromTokens(text, ";", "");
  if (halves.size() < 1)
    return gradient;

  StringArray stops = StringArray::fromTokens(halves[0], ",", "");
  for (int i = 0; i < stops.size() && i < 2; ++i)
    gradient.stops.push_back(Colour::fromString(stops[i]));
  if (halves.size() > 1)
    gradient.angle = jlimit(0.0f, 360.0f, halves[1].getFloatValue());
  return gradient;
}

void Skin::addOverrideGradient(int section, ColorId color_id, const Gradient& gradient) {
  if (section == kNone)
    setGradient(color_id, gradient);
  else
    gradient_overrides_[section][color_id] = gradient;
}

Skin::Gradient Skin::getGradient(int section, ColorId color_id) const {
  if (section != kNone) {
    auto it = gradient_overrides_[section].find(color_id);
    if (it != gradient_overrides_[section].end())
      return it->second;
  }
  return getGradient(color_id);
}

void Skin::collectGradients(std::vector<GradientEntry>& gradients) const {
  for (int i = 0; i < kNumColors; ++i) {
    ColorId id = static_cast<ColorId>(kInitialColor + i);
    if (gradients_[i].active())
      gradients.push_back({ colors_[i], gradients_[i], kNone });

    for (int section = 1; section < kNumSectionOverrides; ++section) {
      auto color = color_overrides_[section].find(id);
      if (color == color_overrides_[section].end())
        continue;

      Gradient gradient = getGradient(section, id);
      if (gradient.active())
        gradients.push_back({ color->second, gradient, section });
    }
  }
}

void Skin::addOverrideGlow(int section, ColorId color_id, float glow, float size) {
  if (section == kNone) {
    setGlow(color_id, glow);
    setGlowSize(color_id, size);
  }
  else
    glow_overrides_[section][color_id] = { jlimit(0.0f, 1.0f, glow), jlimit(0.0f, 1.0f, size) };
}

float Skin::getGlow(int section, ColorId color_id) const {
  if (section != kNone && glow_overrides_[section].count(color_id))
    return glow_overrides_[section].at(color_id).first;
  return getGlow(color_id);
}

float Skin::getGlowSize(int section, ColorId color_id) const {
  if (section != kNone && glow_overrides_[section].count(color_id))
    return glow_overrides_[section].at(color_id).second;
  return getGlowSize(color_id);
}

void Skin::clearSkin() {
  for (int i = 0; i < kNumSectionOverrides; ++i)
    color_overrides_[i].clear();
  for (int i = 0; i < kNumSectionOverrides; ++i)
    value_overrides_[i].clear();
  for (int i = 0; i < kNumSectionOverrides; ++i)
    glow_overrides_[i].clear();
  for (int i = 0; i < kNumSectionOverrides; ++i)
    gradient_overrides_[i].clear();
}

void Skin::loadDefaultSkin() {
  MemoryInputStream skin((const void*)BinaryData::default_vitalskin, BinaryData::default_vitalskinSize, false);
  std::string skin_string = skin.readEntireStreamAsString().toStdString();

  try {
    json data = json::parse(skin_string, nullptr, false);
    applying_default_ = true;
    jsonToState(data);
    applying_default_ = false;
  }
  catch (const json::exception& e) {
    applying_default_ = false;
  }
}

void Skin::setComponentColors(Component* component) const {
  for (int i = 0; i < Skin::kNumColors; ++i) {
    int color_id = i + Skin::kInitialColor;
    Colour color = getColor(static_cast<Skin::ColorId>(color_id));
    component->setColour(color_id, color);
    component->getProperties().set(glowPropertyName(static_cast<Skin::ColorId>(color_id)), getGlow(static_cast<Skin::ColorId>(color_id)));
  }
}

void Skin::setComponentColors(Component* component, SectionOverride section_override, bool top_level) const {
  if (top_level) {
    setComponentColors(component);
    return;
  }

  for (int i = kInitialColor; i < kFinalColor; ++i)
    component->removeColour(i);

  for (const auto& color : color_overrides_[section_override])
    component->setColour(color.first, color.second);
}

void Skin::setComponentValues(SynthSection* component) const {
  std::map<ValueId, float> values;
  for (int i = 0; i < kNumSkinValueIds; ++i)
    values[(ValueId)i] = values_[i];

  component->setSkinValues(values);
}

void Skin::setComponentValues(SynthSection* component, SectionOverride section_override, bool top_level) const {
  if (top_level) {
    setComponentValues(component);
    return;
  }
  component->setSkinValues(value_overrides_[section_override]);
}

bool Skin::overridesColor(int section, ColorId color_id) const {
  if (section == kNone)
    return true;

  return color_overrides_[section].count(color_id) > 0;
}

bool Skin::overridesValue(int section, ValueId value_id) const {
  if (section == kNone)
    return true;

  return value_overrides_[section].count(value_id) > 0;
}

void Skin::copyValuesToLookAndFeel(LookAndFeel* look_and_feel) const {
  look_and_feel->setColour(PopupMenu::backgroundColourId, getColor(Skin::kPopupBackground));
  look_and_feel->setColour(PopupMenu::textColourId, getColor(Skin::kBodyText));
  look_and_feel->setColour(TooltipWindow::textColourId, getColor(Skin::kBodyText));

  look_and_feel->setColour(BubbleComponent::backgroundColourId, getColor(Skin::kPopupBackground));
  look_and_feel->setColour(BubbleComponent::outlineColourId, getColor(Skin::kPopupBorder));

  for (int i = kInitialColor; i < kFinalColor; ++i)
    look_and_feel->setColour(i, getColor(static_cast<Skin::ColorId>(i)));
}

Colour Skin::getColor(int section, ColorId color_id) const {
  if (section == kNone)
    return getColor(color_id);

  if (color_overrides_[section].count(color_id))
    return color_overrides_[section].at(color_id);

  return Colours::black;
}

float Skin::getValue(int section, ValueId value_id) const {
  if (value_overrides_[section].count(value_id))
    return value_overrides_[section].at(value_id);

  return getValue(value_id);
}

void Skin::addOverrideColor(int section, ColorId color_id, Colour color) {
  if (section == kNone)
    setColor(color_id, color);
  else
    color_overrides_[section][color_id] = color;
}

void Skin::removeOverrideColor(int section, ColorId color_id) {
  if (section != kNone) {
    color_overrides_[section].erase(color_id);
    glow_overrides_[section].erase(color_id);
    gradient_overrides_[section].erase(color_id);
  }
}

void Skin::addOverrideValue(int section, ValueId value_id, float value) {
  if (section == kNone)
    setValue(value_id, value);
  else
    value_overrides_[section][value_id] = value;
}

void Skin::removeOverrideValue(int section, ValueId color_id) {
  if (section != kNone)
    value_overrides_[section].erase(color_id);
}

json Skin::stateToJson() {
  json data;
  for (int i = 0; i < kNumColors; ++i)
    data[kColorNames[i]] = colors_[i].toString().toStdString();
  for (int i = 0; i < kNumColors; ++i) {
    if (glow_[i] > 0.0f) {
      data[kColorNames[i] + " Glow"] = glow_[i];
      data[kColorNames[i] + " Glow Size"] = glow_size_[i];
    }
    if (gradients_[i].active())
      data[kColorNames[i] + " Gradient"] = gradients_[i].toString().toStdString();
  }

  for (int i = 0; i < kNumSkinValueIds; ++i)
    data[kValueNames[i]] = values_[i];

  json overrides;
  for (int override_index = 0; override_index < kNumSectionOverrides; ++override_index) {
    json override_section;
    for (const auto& color : color_overrides_[override_index]) {
      int index = color.first - Skin::kInitialColor;
      override_section[kColorNames[index]] = color.second.toString().toStdString();
    }

    for (const auto& glow : glow_overrides_[override_index]) {
      int index = glow.first - Skin::kInitialColor;
      override_section[kColorNames[index] + " Glow"] = glow.second.first;
      override_section[kColorNames[index] + " Glow Size"] = glow.second.second;
    }

    for (const auto& gradient : gradient_overrides_[override_index]) {
      int index = gradient.first - Skin::kInitialColor;
      override_section[kColorNames[index] + " Gradient"] = gradient.second.toString().toStdString();
    }

    for (const auto& value : value_overrides_[override_index]) {
      int index = value.first;
      override_section[kValueNames[index]] = value.second;
    }

    overrides[kOverrideNames[override_index]] = override_section;
  }

  data["overrides"] = overrides;
  data["synth_version"] = ProjectInfo::versionNumber;

  return data;
}

String Skin::stateToString() {
  return stateToJson().dump();
}

void Skin::saveToFile(File destination) {
  destination.replaceWithText(stateToString());
}

json Skin::updateJson(json data) {
  int version = 0;
  if (data.count("synth_version"))
    version = data["synth_version"];

  if (version < 0x608) {
    data["Knob Arc Size"] = data["Knob Size"];
    data["Knob Arc Thickness"] = data["Knob Thickness"];
    data["Knob Handle Length"] = data["Knob Handle Radial Amount"];
    data["Knob Mod Amount Arc Size"] = data["Knob Mod Amount Size"];
    data["Knob Mod Amount Arc Thickness"] = data["Knob Mod Amount Thickness"];
    data["Knob Mod Meter Arc Size"] = data["Knob Mod Meter Size"];
    data["Knob Mod Meter Arc Thickness"] = data["Knob Mod Meter Thickness"];
  }

  if (data.count("Widget Fill Boost") == 0)
    data["Widget Fill Boost"] = 1.6f;
  if (data.count("Widget Line Boost") == 0)
    data["Widget Line Boost"] = 1.0f;

  if (version < 0x609)
    data["Modulation Meter"] = Colours::white.toString().toStdString();

  return data;
}

void Skin::jsonToState(json data) {
  if (!applying_default_) {
    // Start from the built-in default so nothing of the previous skin survives where the new one is silent.
    MemoryInputStream builtin((const void*)BinaryData::default_vitalskin, BinaryData::default_vitalskinSize, false);
    json base = json::parse(builtin.readEntireStreamAsString().toStdString(), nullptr, false);
    if (!base.is_discarded()) {
      applying_default_ = true;
      jsonToState(base);
      applying_default_ = false;
    }
  }

  clearSkin();
  data = updateJson(data);

  if (data.count("overrides")) {
    json overrides = data["overrides"];
    for (int override_index = 0; override_index < kNumSectionOverrides; ++override_index) {
      std::string name = kOverrideNames[override_index];
      color_overrides_[override_index].clear();
      value_overrides_[override_index].clear();

      if (overrides.count(name) == 0)
        continue;

      json override_section = overrides[name];
      for (int i = 0; i < kNumColors; ++i) {
        if (override_section.count(kColorNames[i])) {
          ColorId color_id = static_cast<Skin::ColorId>(i + Skin::kInitialColor);

          std::string color_string = override_section[kColorNames[i]];
          color_overrides_[override_index][color_id] = Colour::fromString(color_string);
        }
        if (override_section.count(kColorNames[i] + " Glow")) {
          ColorId color_id = static_cast<Skin::ColorId>(i + Skin::kInitialColor);
          float glow = override_section[kColorNames[i] + " Glow"];
          float size = override_section.count(kColorNames[i] + " Glow Size") ? (float)override_section[kColorNames[i] + " Glow Size"] : 0.5f;
          glow_overrides_[override_index][color_id] = { jlimit(0.0f, 1.0f, glow), jlimit(0.0f, 1.0f, size) };
        }
        if (override_section.count(kColorNames[i] + " Gradient")) {
          ColorId color_id = static_cast<Skin::ColorId>(i + Skin::kInitialColor);
          std::string text = override_section[kColorNames[i] + " Gradient"];
          Gradient gradient = Gradient::fromString(String(text));
          if (gradient.active())
            gradient_overrides_[override_index][color_id] = gradient;
        }
      }

      for (int i = 0; i < kNumSkinValueIds; ++i) {
        if (override_section.count(kValueNames[i])) {
          Skin::ValueId value_id = static_cast<Skin::ValueId>(i);
          float value = override_section[kValueNames[i]];
          value_overrides_[override_index][value_id] = value;
        }
      }
    }
  }

  for (int i = 0; i < kNumColors; ++i) {
    if (data.count(kColorNames[i])) {
      std::string color_string = data[kColorNames[i]];
      colors_[i] = Colour::fromString(color_string);
    }
    glow_[i] = data.count(kColorNames[i] + " Glow") ? jlimit(0.0f, 1.0f, (float)data[kColorNames[i] + " Glow"]) : 0.0f;
    gradients_[i] = Gradient();
    if (data.count(kColorNames[i] + " Gradient")) {
      std::string text = data[kColorNames[i] + " Gradient"];
      gradients_[i] = Gradient::fromString(String(text));
    }
    glow_size_[i] = data.count(kColorNames[i] + " Glow Size") ? jlimit(0.0f, 1.0f, (float)data[kColorNames[i] + " Glow Size"]) : 0.5f;
  }

  for (int i = 0; i < kNumSkinValueIds; ++i) {
    if (data.count(kValueNames[i]))
      values_[i] = data[kValueNames[i]];
    else
      values_[i] = 0.0f;
  }
}

bool Skin::stringToState(String skin_string) {
  try {
    json data = json::parse(skin_string.toStdString(), nullptr, false);
    jsonToState(data);
  }
  catch (const json::exception& e) {
    return false;
  }
  return true;
}

bool Skin::loadFromFile(File source) {
  return stringToState(source.loadFileAsString());
}

namespace {
  struct BinInfo {
    const char* name;
    std::vector<Skin::SectionOverride> sections;
    int parent = -1;
  };

  struct EffectModuleBin {
    const char* name;
    Skin::SectionOverride section;
  };

  // Every effect module that has its own skin sub-bin under "Effects". To add a new effect module:
  //   1. give its section a SectionOverride (skin.h enum + kOverrideNames) and call setSkinOverride() with it,
  //   2. add one line here.
  // The "Effects" bin edits all modules together and each module also gets its own sub-bin, with no other change.
  const std::vector<EffectModuleBin>& effectModuleBins() {
    static const std::vector<EffectModuleBin> modules = {
      { "Chorus", Skin::kChorus },
      { "Compressor", Skin::kCompressor },
      { "Delay", Skin::kDelay },
      { "Distortion", Skin::kDistortion },
      { "Equalizer", Skin::kEqualizer },
      { "Filter", Skin::kFxFilter },
      { "Flanger", Skin::kFlanger },
      { "Phaser", Skin::kPhaser },
      { "Reverb", Skin::kReverb },
    };
    return modules;
  }

  const std::vector<BinInfo>& binTable() {
    static const std::vector<BinInfo> bins = []() {
      std::vector<BinInfo> list = {
        { "Global", { Skin::kNone } },
        { "Header", { Skin::kLogo, Skin::kHeader } },
        { "Input / Oscillators", { Skin::kOscillator, Skin::kSample, Skin::kSub } },
        { "Filters", { Skin::kFilter, Skin::kFxFilter } },
        { "Envelopes", { Skin::kEnvelope } },
        { "LFO / Tables", { Skin::kLfo, Skin::kRandomLfo } },
        { "Voice / MIDI", { Skin::kVoice, Skin::kMacro, Skin::kKeyboard } },
      };

      BinInfo effects = { "Effects", { Skin::kAllEffects } };
      for (const EffectModuleBin& module : effectModuleBins())
        effects.sections.push_back(module.section);
      int effects_index = static_cast<int>(list.size());
      list.push_back(effects);
      for (const EffectModuleBin& module : effectModuleBins())
        list.push_back({ module.name, { module.section }, effects_index });

      list.push_back({ "Matrix / Mod", { Skin::kModulationDragDrop, Skin::kModulationMatrix } });
      list.push_back({ "Browser", { Skin::kPresetBrowser, Skin::kPopupBrowser } });
      list.push_back({ "Overlays / Advanced", { Skin::kOverlay, Skin::kAdvanced, Skin::kWavetableEditor } });
      return list;
    }();
    return bins;
  }

  // Finds the nearest skin-tagged section around a component and the bin it belongs to.
  SynthSection* findBinSection(Component* component, Component* root, int& bin) {
    bin = -1;
    if (root == nullptr || component == nullptr || !(component == root || root->isParentOf(component)))
      return nullptr;

    for (Component* c = component; c != nullptr; c = c->getParentComponent()) {
      SynthSection* section = dynamic_cast<SynthSection*>(c);
      if (section != nullptr && section->getSkinOverride() != Skin::kNone) {
        bin = SkinBins::binOf(section->getSkinOverride());
        if (bin > 0)
          return section;
      }
    }
    return nullptr;
  }

  void collectBinSections(Component* component, const std::vector<Skin::SectionOverride>& sections,
                          FullInterface* root, std::vector<Rectangle<int>>& rects) {
    for (int i = 0; i < component->getNumChildComponents(); ++i) {
      Component* child = component->getChildComponent(i);
      if (!child->isVisible())
        continue;

      SynthSection* section = dynamic_cast<SynthSection*>(child);
      if (section != nullptr && child->getWidth() > 0 && child->getHeight() > 0 &&
          std::find(sections.begin(), sections.end(), section->getSkinOverride()) != sections.end() &&
          section->getSkinOverride() != Skin::kNone) {
        rects.push_back(root->getLocalArea(child, child->getLocalBounds()));
        continue;
      }
      collectBinSections(child, sections, root, rects);
    }
  }

  const Colour kEditorBackground(0xff1c1b22);
  const Colour kEditorPanel(0xff282733);
  const Colour kEditorAccent(0xffa77bff);
  const Colour kEditorText(0xffe6e3f0);
  const Colour kEditorDim(0xff8d89a0);
} // namespace

int SkinBins::numBins() { return static_cast<int>(binTable().size()); }
String SkinBins::name(int bin) { return binTable()[bin].name; }
const std::vector<Skin::SectionOverride>& SkinBins::sections(int bin) { return binTable()[bin].sections; }

int SkinBins::parent(int bin) { return binTable()[bin].parent; }

// The most specific bin wins (an effect module's own sub-bin over the "Effects" bin that also covers it).
int SkinBins::binOf(Skin::SectionOverride section) {
  const auto& bins = binTable();
  int best = -1;
  for (int i = 1; i < static_cast<int>(bins.size()); ++i) {
    if (std::find(bins[i].sections.begin(), bins[i].sections.end(), section) == bins[i].sections.end())
      continue;
    if (best < 0 || bins[i].sections.size() < bins[best].sections.size())
      best = i;
  }
  return best;
}

// One colour or value in a bin: swatch + name + remove button (or a value slider).
class SkinEditorRow : public Component {
  public:
    static constexpr int kHeight = 34;

    SkinEditorRow(bool is_value, int id, String name, bool removable) :
        is_value_(is_value), id_(id), name_(std::move(name)), selected_(false) {
      remove_.setButtonText(CharPointer_UTF8("\xc3\x97"));
      remove_.setColour(TextButton::buttonColourId, Colour(0xff3a3948));
      remove_.setColour(TextButton::textColourOffId, kEditorText);
      remove_.setTooltip("Remove this override from the bin");
      addChildComponent(remove_);
      remove_.setVisible(removable);
      remove_.onClick = [this]() { if (on_remove) on_remove(this); };

      if (is_value_) {
        // The stock inc/dec buttons do nothing when the slider has no step, so the buttons are our own.
        slider_.setRange(-10000.0, 10000.0, 0.01);
        slider_.setSliderStyle(Slider::LinearBar);
        slider_.setVelocityBasedMode(true);
        slider_.setNumDecimalPlacesToDisplay(2);
        slider_.setScrollWheelEnabled(false);
        slider_.setTextBoxIsEditable(true);
        slider_.setColour(Slider::trackColourId, kEditorAccent.withAlpha(0.25f));
        slider_.setColour(Slider::textBoxTextColourId, kEditorText);
        slider_.onValueChange = [this]() { if (on_value) on_value(this, static_cast<float>(slider_.getValue())); };
        addAndMakeVisible(slider_);
        for (TextButton* step : { &minus_, &plus_ }) {
          step->setColour(TextButton::buttonColourId, Colour(0xff3a3948));
          step->setColour(TextButton::textColourOffId, kEditorText);
          step->setTooltip("Step by 0.1 (hold Shift for 0.01, Ctrl/Cmd for 1)");
          addAndMakeVisible(step);
        }
        minus_.setButtonText("-");
        plus_.setButtonText("+");
        minus_.onClick = [this]() { stepValue(-1.0); };
        plus_.onClick = [this]() { stepValue(1.0); };
      }
    }

    bool isValue() const { return is_value_; }
    int id() const { return id_; }
    Colour color() const { return color_; }
    void setColor(Colour color) { color_ = color; repaint(); }
    void setGradientStops(const std::vector<Colour>& stops) { gradient_stops_ = stops; repaint(); }
    void setValue(float value) { slider_.setValue(value, dontSendNotification); }
    void stepValue(double direction) {
      ModifierKeys mods = ModifierKeys::currentModifiers;
      double step = mods.isShiftDown() ? 0.01 : (mods.isCommandDown() || mods.isCtrlDown() ? 1.0 : 0.1);
      slider_.setValue(slider_.getValue() + direction * step, sendNotificationSync);
    }
    void setSelected(bool selected) { selected_ = selected; repaint(); }

    void resized() override {
      remove_.setBounds(getWidth() - 30, 5, 24, 24);
      if (is_value_) {
        minus_.setBounds(getWidth() - 36 - 170, 6, 24, 22);
        slider_.setBounds(getWidth() - 36 - 142, 6, 110, 22);
        plus_.setBounds(getWidth() - 36 - 28, 6, 24, 22);
      }
    }

    Rectangle<int> swatchBounds() const { return Rectangle<int>(8, 5, 92, 24); }

    void paint(Graphics& g) override {
      g.setColour(selected_ ? kEditorPanel.brighter(0.15f) : kEditorPanel);
      g.fillRoundedRectangle(getLocalBounds().reduced(2, 1).toFloat(), 5.0f);
      if (selected_) {
        g.setColour(kEditorAccent);
        g.drawRoundedRectangle(getLocalBounds().reduced(2, 1).toFloat(), 5.0f, 1.5f);
      }

      Rectangle<int> swatch = swatchBounds();
      if (!is_value_) {
        g.saveState();
        g.reduceClipRegion(swatch);
        g.fillCheckerBoard(swatch.toFloat(), 8.0f, 8.0f, Colour(0xff666666), Colour(0xffaaaaaa));
        g.restoreState();
        if (gradient_stops_.empty())
          g.setColour(color_);
        else {
          ColourGradient preview(color_, swatch.getX(), 0.0f, gradient_stops_.back(), swatch.getRight(), 0.0f, false);
          if (gradient_stops_.size() > 1)
            preview.addColour(0.5, gradient_stops_[0]);
          g.setGradientFill(preview);
        }
        g.fillRoundedRectangle(swatch.toFloat(), 4.0f);
        g.setColour(color_.withAlpha(1.0f).contrasting(0.9f));
        g.setFont(12.0f);
        g.drawText(color_.toDisplayString(true), swatch, Justification::centred, false);
      }
      else {
        g.setColour(kEditorDim);
        g.setFont(12.0f);
        g.drawText("VALUE", swatch, Justification::centred, false);
      }

      g.setColour(kEditorText);
      g.setFont(14.0f);
      int name_width = getWidth() - swatch.getRight() - 16 - (is_value_ ? 210 : 40);
      g.drawText(name_, swatch.getRight() + 10, 0, std::max(name_width, 40), getHeight(),
                 Justification::centredLeft, true);
    }

    void mouseDown(const MouseEvent& e) override {
      if (on_select)
        on_select(this);
    }

    std::function<void(SkinEditorRow*)> on_select;
    std::function<void(SkinEditorRow*)> on_remove;
    std::function<void(SkinEditorRow*, float)> on_value;

  private:
    bool is_value_;
    int id_;
    String name_;
    bool selected_;
    Colour color_;
    std::vector<Colour> gradient_stops_;
    TextButton remove_, minus_, plus_;
    Slider slider_;
};

class SkinEditor : public Component, public Button::Listener, public ChangeListener,
                   public TextEditor::Listener, public Timer {
  public:
    SkinEditor(Skin* skin, FullInterface* full_interface) :
        skin_(skin), full_interface_(full_interface), bin_(0), selected_row_(-1),
        hover_editor_bin_(-1), last_hover_bin_(-2),
        selector_(ColourSelector::showAlphaChannel | ColourSelector::showColourAtTop |
                  ColourSelector::showSliders | ColourSelector::showColourspace, 4, 0) {
      setOpaque(true);

      for (int i = 0; i < SkinBins::numBins(); ++i) {
        auto button = std::make_unique<TextButton>(SkinBins::name(i));
        button->setClickingTogglesState(false);
        button->setColour(TextButton::buttonColourId, kEditorPanel);
        button->setColour(TextButton::buttonOnColourId, kEditorAccent);
        button->setColour(TextButton::textColourOffId, kEditorText);
        button->setColour(TextButton::textColourOnId, Colour(0xff1a1426));
        button->addListener(this);
        button->addMouseListener(this, false);
        addAndMakeVisible(button.get());
        bin_buttons_.push_back(std::move(button));
      }

      setupButton(default_button_, "Default");
      default_button_.setTooltip("Reset every colour, value, glow and gradient to the built-in default skin");
      setupButton(undo_button_, "Undo");
      setupButton(redo_button_, "Redo");
      setupButton(import_button_, "Import");
      setupButton(save_button_, "Save");
      setupButton(add_button_, "+ Add override");

      addAndMakeVisible(rows_viewport_);
      rows_viewport_.setViewedComponent(&rows_container_, false);
      rows_viewport_.setScrollBarsShown(true, false);

      hex_label_.setText("HEX (AARRGGBB)", dontSendNotification);
      hex_label_.setColour(Label::textColourId, kEditorDim);
      hex_label_.setFont(12.0f);
      addAndMakeVisible(hex_label_);
      hex_.setInputRestrictions(8, "0123456789abcdefABCDEF");
      hex_.setColour(TextEditor::backgroundColourId, kEditorPanel);
      hex_.setColour(TextEditor::textColourId, kEditorText);
      hex_.setColour(TextEditor::outlineColourId, Colour(0xff3a3948));
      hex_.addListener(this);
      addAndMakeVisible(hex_);

      setupKnob(glow_slider_, glow_label_, "GLOW STRENGTH");
      setupKnob(size_slider_, size_label_, "GLOW SIZE");
      glow_slider_.onValueChange = [this]() { applyGlow(static_cast<float>(glow_slider_.getValue()), true); };
      size_slider_.onValueChange = [this]() { applyGlow(static_cast<float>(size_slider_.getValue()), false); };

      grad_toggle_.setButtonText("GRADIENT");
      grad_toggle_.setClickingTogglesState(true);
      grad_toggle_.setColour(TextButton::buttonColourId, kEditorPanel);
      grad_toggle_.setColour(TextButton::buttonOnColourId, kEditorAccent);
      grad_toggle_.setColour(TextButton::textColourOffId, kEditorText);
      grad_toggle_.setColour(TextButton::textColourOnId, Colour(0xff1a1426));
      grad_toggle_.addListener(this);
      addAndMakeVisible(grad_toggle_);
      const char* target_names[3] = { "Start", "Stop 2", "Stop 3" };
      for (int i = 0; i < 3; ++i) {
        target_buttons_[i].setButtonText(target_names[i]);
        target_buttons_[i].setColour(TextButton::textColourOffId, kEditorText);
        target_buttons_[i].setColour(TextButton::textColourOnId, kEditorText);
        target_buttons_[i].addListener(this);
        addAndMakeVisible(target_buttons_[i]);
      }
      setupKnob(angle_slider_, angle_label_, "ANGLE");
      angle_slider_.setRange(0.0, 360.0, 1.0);
      angle_slider_.setDoubleClickReturnValue(true, 90.0);
      angle_slider_.onValueChange = [this]() { editGradient([this](Skin::Gradient& g) { g.angle = (float)angle_slider_.getValue(); }); };

      selector_.setColour(ColourSelector::backgroundColourId, kEditorBackground);
      selector_.setColour(ColourSelector::labelTextColourId, kEditorText);
      selector_.addChangeListener(this);
      addAndMakeVisible(selector_);

      hint_.setText("Hover the plugin to outline a section and its bin. Alt-click a section to edit its bin.",
                    dontSendNotification);
      hint_.setFont(12.0f);
      hint_.setColour(Label::textColourId, kEditorDim);
      hint_.setJustificationType(Justification::topLeft);
      addAndMakeVisible(hint_);

      Desktop::getInstance().addGlobalMouseListener(this);
      selectBin(0);
      startTimerHz(25);
    }

    ~SkinEditor() override {
      stopTimer();
      Desktop::getInstance().removeGlobalMouseListener(this);
      if (full_interface_ != nullptr)
        full_interface_->clearBinHighlight();
    }

    void paint(Graphics& g) override {
      g.fillAll(kEditorBackground);
      g.setColour(kEditorText);
      g.setFont(Font(18.0f, Font::bold));
      g.drawText("Skin Editor", 14, 8, 200, 28, Justification::centredLeft);
      g.setColour(kEditorDim);
      g.setFont(12.0f);
      g.drawText(SkinBins::name(bin_) + " bin", 14, 34, 200, 16, Justification::centredLeft);
    }

    void resized() override {
      const int top = 56;
      Rectangle<int> bounds = getLocalBounds();

      int x = bounds.getRight() - 12;
      for (TextButton* b : { &save_button_, &import_button_, &redo_button_, &undo_button_, &default_button_ }) {
        b->setBounds(x - 76, 14, 76, 28);
        x -= 84;
      }

      const int list_width = 180;
      int y = top;
      for (int i = 0; i < static_cast<int>(bin_buttons_.size()); ++i) {
        bool visible = binListed(i);
        bin_buttons_[i]->setVisible(visible);
        if (!visible)
          continue;

        bool sub = SkinBins::parent(i) >= 0;
        int indent = sub ? 16 : 0;
        int height = sub ? 24 : 30;
        bin_buttons_[i]->setBounds(12 + indent, y, list_width - 12 - indent, height);
        y += height + (sub ? 3 : 4);
      }
      hint_.setBounds(12, y + 10, list_width - 16, 90);
      hint_.setVisible(y + 100 < bounds.getHeight());

      int right_x = list_width + 12;
      int right_width = bounds.getWidth() - right_x - 12;
      const int picker_height = 330;
      add_button_.setBounds(right_x, top, 150, 28);
      rows_viewport_.setBounds(right_x, top + 36, right_width,
                               bounds.getHeight() - top - 36 - picker_height - 20);
      rows_container_.setSize(rows_viewport_.getMaximumVisibleWidth(), rows_container_.getHeight());
      layoutRows();

      int picker_y = bounds.getBottom() - picker_height - 8;
      hex_label_.setBounds(right_x, picker_y, 120, 24);
      hex_.setBounds(right_x + 124, picker_y, 110, 24);
      glow_slider_.setBounds(right_x + 260, picker_y - 4, 56, 48);
      glow_label_.setBounds(right_x + 248, picker_y + 42, 80, 14);
      size_slider_.setBounds(right_x + 340, picker_y - 4, 56, 48);
      size_label_.setBounds(right_x + 336, picker_y + 42, 80, 14);
      grad_toggle_.setBounds(right_x + 420, picker_y, 150, 24);
      for (int i = 0; i < 3; ++i)
        target_buttons_[i].setBounds(right_x + 420 + i * 52, picker_y + 28, 50, 22);
      angle_slider_.setBounds(right_x + 590, picker_y - 4, 56, 48);
      angle_label_.setBounds(right_x + 586, picker_y + 42, 64, 14);
      selector_.setBounds(right_x, picker_y + 62, right_width, picker_height - 62);
    }

    void buttonClicked(Button* button) override {
      for (int i = 0; i < static_cast<int>(bin_buttons_.size()); ++i) {
        if (button == bin_buttons_[i].get()) {
          selectBin(i);
          return;
        }
      }

      if (button == &grad_toggle_) {
        toggleGradient(grad_toggle_.getToggleState());
        return;
      }
      for (int i = 0; i < 3; ++i) {
        if (button == &target_buttons_[i]) {
          selectTarget(i);
          return;
        }
      }

      if (button == &default_button_)
        resetToDefault();
      else if (button == &undo_button_)
        undo();
      else if (button == &redo_button_)
        redo();
      else if (button == &save_button_)
        save();
      else if (button == &import_button_)
        import();
      else if (button == &add_button_)
        showAddMenu();
    }

    void mouseEnter(const MouseEvent& e) override {
      for (int i = 0; i < static_cast<int>(bin_buttons_.size()); ++i) {
        if (e.eventComponent == bin_buttons_[i].get())
          hover_editor_bin_ = i;
      }
    }

    void mouseExit(const MouseEvent& e) override {
      for (int i = 0; i < static_cast<int>(bin_buttons_.size()); ++i) {
        if (e.eventComponent == bin_buttons_[i].get() && hover_editor_bin_ == i)
          hover_editor_bin_ = -1;
      }
    }

    void mouseDown(const MouseEvent& e) override {
      if (!e.mods.isAltDown() || full_interface_ == nullptr)
        return;

      int bin = -1;
      if (findBinSection(e.eventComponent, full_interface_, bin) != nullptr && bin > 0)
        selectBin(bin);
    }

    void timerCallback() override {
      if (full_interface_ == nullptr)
        return;

      flushReload();

      std::vector<Rectangle<int>> rects;
      int bin = -1;
      if (hover_editor_bin_ > 0) {
        bin = hover_editor_bin_;
        collectBinSections(full_interface_, SkinBins::sections(bin), full_interface_, rects);
      }
      else if (hover_editor_bin_ < 0) {
        Component* under = Desktop::getInstance().findComponentAt(Desktop::getMousePosition());
        SynthSection* section = findBinSection(under, full_interface_, bin);
        if (section != nullptr)
          rects.push_back(full_interface_->getLocalArea(section, section->getLocalBounds()));
      }

      if (rects.empty())
        full_interface_->clearBinHighlight();
      else
        full_interface_->setBinHighlight(rects, SkinBins::name(bin));
    }

    void changeListenerCallback(ChangeBroadcaster*) override {
      SkinEditorRow* row = selectedRow();
      if (row == nullptr || row->isValue())
        return;

      Colour color = selector_.getCurrentColour();
      hex_.setText(color.toDisplayString(true), dontSendNotification);
      if (edit_target_ > 0) {
        editGradient([&](Skin::Gradient& g) {
          size_t index = static_cast<size_t>(edit_target_ - 1);
          if (index < g.stops.size())
            g.stops[index] = color;
        });
        return;
      }
      applyColor(row, color);
    }

    void textEditorReturnKeyPressed(TextEditor&) override { applyHex(); }
    void textEditorFocusLost(TextEditor&) override { applyHex(); }

  private:
    void setupButton(TextButton& button, const String& text) {
      button.setButtonText(text);
      button.setColour(TextButton::buttonColourId, kEditorPanel);
      button.setColour(TextButton::textColourOffId, kEditorText);
      button.addListener(this);
      addAndMakeVisible(button);
    }

    void applyHex() {
      SkinEditorRow* row = selectedRow();
      String text = hex_.getText();
      if (row == nullptr || row->isValue() || text.length() != 8)
        return;

      Colour color = Colour::fromString(text);
      selector_.setCurrentColour(color, dontSendNotification);
      if (edit_target_ > 0) {
        editGradient([&](Skin::Gradient& g) {
          size_t index = static_cast<size_t>(edit_target_ - 1);
          if (index < g.stops.size())
            g.stops[index] = color;
        });
        return;
      }
      applyColor(row, color);
    }

    void setupKnob(Slider& slider, Label& label, const String& text) {
      slider.setRange(0.0, 1.0, 0.01);
      slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      slider.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
      slider.setDoubleClickReturnValue(true, 0.5);
      slider.setColour(Slider::rotarySliderFillColourId, kEditorAccent);
      slider.setColour(Slider::rotarySliderOutlineColourId, Colour(0xff3a3948));
      slider.setColour(Slider::thumbColourId, kEditorText);
      slider.setPopupDisplayEnabled(true, true, this);
      addAndMakeVisible(slider);
      label.setText(text, dontSendNotification);
      label.setColour(Label::textColourId, kEditorDim);
      label.setFont(10.0f);
      label.setJustificationType(Justification::centred);
      addAndMakeVisible(label);
    }

    void applyGlow(float value, bool strength) {
      SkinEditorRow* row = selectedRow();
      if (row == nullptr || row->isValue())
        return;

      pushUndo(String(strength ? "g:" : "gs:") + String(row->id()));
      Skin::ColorId id = static_cast<Skin::ColorId>(row->id());
      for (Skin::SectionOverride section : SkinBins::sections(bin_)) {
        float glow = strength ? value : skin_->getGlow(section, id);
        float size = strength ? skin_->getGlowSize(section, id) : value;
        skin_->addOverrideGlow(section, id, glow, size);
      }
      refreshGlow();
      return;
      reloadInterface();
    }

    SkinEditorRow* selectedRow() {
      if (selected_row_ < 0 || selected_row_ >= static_cast<int>(rows_.size()))
        return nullptr;
      return rows_[selected_row_].get();
    }

    // Skin access for the current bin (every section of the bin is kept in step).
    bool binHas(bool is_value, int id) const {
      const auto& sections = SkinBins::sections(bin_);
      for (Skin::SectionOverride section : sections) {
        if (is_value ? skin_->overridesValue(section, static_cast<Skin::ValueId>(id))
                     : skin_->overridesColor(section, static_cast<Skin::ColorId>(id)))
          return true;
      }
      return false;
    }

    Colour binColor(int id) const {
      Skin::ColorId color_id = static_cast<Skin::ColorId>(id);
      for (Skin::SectionOverride section : SkinBins::sections(bin_)) {
        if (skin_->overridesColor(section, color_id))
          return skin_->getColor(section, color_id);
      }
      return skin_->getColor(color_id);
    }

    float binValue(int id) const {
      Skin::ValueId value_id = static_cast<Skin::ValueId>(id);
      for (Skin::SectionOverride section : SkinBins::sections(bin_)) {
        if (skin_->overridesValue(section, value_id))
          return skin_->getValue(section, value_id);
      }
      return skin_->getValue(value_id);
    }

    void pushUndo(const String& key) {
      double now = Time::getMillisecondCounterHiRes();
      if (key == coalesce_key_ && now - coalesce_time_ < 900.0) {
        coalesce_time_ = now;
        return;
      }
      coalesce_key_ = key;
      coalesce_time_ = now;
      undo_stack_.push_back(skin_->stateToString());
      if (undo_stack_.size() > 200)
        undo_stack_.erase(undo_stack_.begin());
      redo_stack_.clear();
      updateUndoButtons();
    }

    void updateUndoButtons() {
      undo_button_.setEnabled(!undo_stack_.empty());
      redo_button_.setEnabled(!redo_stack_.empty());
    }

    // Colour/value edits re-layout the whole interface, so they are coalesced and applied at most every
    // ~60 ms; dragging a knob stays smooth and the plugin window doesn't flicker. Glow needs no layout.
    void reloadInterface() { reload_pending_ = true; }

    void refreshGlow() {
      if (full_interface_ != nullptr)
        full_interface_->updateGlow(*skin_);
    }

    void flushReload() {
      double now = Time::getMillisecondCounterHiRes();
      if (!reload_pending_ || full_interface_ == nullptr || now - last_reload_ < 60.0)
        return;

      reload_pending_ = false;
      last_reload_ = now;
      full_interface_->reloadSkin(*skin_);
    }

    void applyColor(SkinEditorRow* row, Colour color) {
      pushUndo("c" + String(bin_) + ":" + String(row->id()));
      for (Skin::SectionOverride section : SkinBins::sections(bin_))
        skin_->addOverrideColor(section, static_cast<Skin::ColorId>(row->id()), color);
      row->setColor(color);
      reloadInterface();
    }

    void applyValue(SkinEditorRow* row, float value) {
      pushUndo("v" + String(bin_) + ":" + String(row->id()));
      for (Skin::SectionOverride section : SkinBins::sections(bin_))
        skin_->addOverrideValue(section, static_cast<Skin::ValueId>(row->id()), value);
      reloadInterface();
    }

    void removeRow(SkinEditorRow* row) {
      pushUndo("r");
      coalesce_key_ = String();
      for (Skin::SectionOverride section : SkinBins::sections(bin_)) {
        if (row->isValue())
          skin_->removeOverrideValue(section, static_cast<Skin::ValueId>(row->id()));
        else
          skin_->removeOverrideColor(section, static_cast<Skin::ColorId>(row->id()));
      }
      reloadInterface();
      rebuildRows();
    }

    Skin::Gradient currentGradient() {
      SkinEditorRow* row = selectedRow();
      if (row == nullptr || row->isValue())
        return Skin::Gradient();
      return skin_->getGradient(SkinBins::sections(bin_)[0], static_cast<Skin::ColorId>(row->id()));
    }

    // Gradients need no layout: the screen-space pass picks them up straight away.
    void editGradient(const std::function<void(Skin::Gradient&)>& change) {
      SkinEditorRow* row = selectedRow();
      if (row == nullptr || row->isValue() || updating_gradient_controls_)
        return;

      pushUndo("gr:" + String(bin_) + ":" + String(row->id()));
      Skin::Gradient gradient = currentGradient();
      change(gradient);
      Skin::ColorId id = static_cast<Skin::ColorId>(row->id());
      for (Skin::SectionOverride section : SkinBins::sections(bin_))
        skin_->addOverrideGradient(section, id, gradient);
      row->setGradientStops(gradient.stops);
      refreshGlow();
      updateTargetButtons();
    }

    void toggleGradient(bool on) {
      SkinEditorRow* row = selectedRow();
      if (row == nullptr || row->isValue())
        return;

      coalesce_key_ = String();
      editGradient([&](Skin::Gradient& g) {
        if (!on)
          g.stops.clear();
        else if (g.stops.empty()) {
          g.stops.push_back(row->color().interpolatedWith(Colours::white, 0.5f));
          g.angle = 90.0f;
        }
      });
      edit_target_ = 0;
      updateGradientControls();
    }

    void selectTarget(int target) {
      Skin::Gradient gradient = currentGradient();
      if (!gradient.active() || target > static_cast<int>(gradient.stops.size()) + 0) {
        // "Stop 3" is created on first use.
        if (gradient.active() && target == 2) {
          coalesce_key_ = String();
          editGradient([](Skin::Gradient& g) { if (g.stops.size() < 2) g.stops.push_back(g.stops.back().brighter(0.4f)); });
        }
        else
          return;
      }
      edit_target_ = target;
      updateGradientControls();
    }

    void updateTargetButtons() {
      Skin::Gradient gradient = currentGradient();
      SkinEditorRow* row = selectedRow();
      for (int i = 0; i < 3; ++i) {
        bool present = i == 0 || (gradient.active() && i - 1 < static_cast<int>(gradient.stops.size()));
        Colour color = i == 0 ? (row ? row->color() : Colours::grey)
                              : (present ? gradient.stops[i - 1] : kEditorPanel);
        target_buttons_[i].setColour(TextButton::buttonColourId, color);
        target_buttons_[i].setColour(TextButton::buttonOnColourId, color);
        target_buttons_[i].setColour(TextButton::textColourOffId, color.withAlpha(1.0f).contrasting(0.9f));
        target_buttons_[i].setColour(TextButton::textColourOnId, color.withAlpha(1.0f).contrasting(0.9f));
        String label = i == 2 && !present ? String("+ Stop 3") : (i == 0 ? String("Start") : "Stop " + String(i + 1));
        target_buttons_[i].setButtonText(i == edit_target_ ? String(CharPointer_UTF8("\xe2\x80\xa2 ")) + label : label);
        target_buttons_[i].setToggleState(i == edit_target_, dontSendNotification);
        target_buttons_[i].repaint();
      }
    }

    void updateGradientControls() {
      SkinEditorRow* row = selectedRow();
      bool is_color = row != nullptr && !row->isValue();
      Skin::Gradient gradient = currentGradient();
      updating_gradient_controls_ = true;
      grad_toggle_.setVisible(is_color);
      grad_toggle_.setToggleState(gradient.active(), dontSendNotification);
      for (int i = 0; i < 3; ++i)
        target_buttons_[i].setVisible(is_color && gradient.active());
      angle_slider_.setVisible(is_color && gradient.active());
      angle_label_.setVisible(is_color && gradient.active());
      if (gradient.active())
        angle_slider_.setValue(gradient.angle, dontSendNotification);
      if (!gradient.active() || edit_target_ > static_cast<int>(gradient.stops.size()))
        edit_target_ = 0;
      updateTargetButtons();
      if (is_color) {
        Colour color = edit_target_ == 0 ? row->color() : gradient.stops[edit_target_ - 1];
        selector_.setCurrentColour(color, dontSendNotification);
        hex_.setText(color.toDisplayString(true), dontSendNotification);
      }
      updating_gradient_controls_ = false;
    }

    void selectRow(SkinEditorRow* row) {
      selected_row_ = -1;
      for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        bool selected = rows_[i].get() == row;
        rows_[i]->setSelected(selected);
        if (selected)
          selected_row_ = i;
      }

      bool is_color = row != nullptr && !row->isValue();
      bool show_glow = is_color;
      for (Component* c : std::initializer_list<Component*>{ &glow_slider_, &glow_label_, &size_slider_, &size_label_ })
        c->setVisible(show_glow);
      if (show_glow) {
        Skin::SectionOverride section = SkinBins::sections(bin_)[0];
        glow_slider_.setValue(skin_->getGlow(section, static_cast<Skin::ColorId>(row->id())), dontSendNotification);
        size_slider_.setValue(skin_->getGlowSize(section, static_cast<Skin::ColorId>(row->id())), dontSendNotification);
      }
      selector_.setVisible(is_color);
      hex_.setVisible(is_color);
      hex_label_.setVisible(is_color);
      if (is_color) {
        selector_.setCurrentColour(row->color(), dontSendNotification);
        hex_.setText(row->color().toDisplayString(true), dontSendNotification);
      }
      edit_target_ = 0;
      updateGradientControls();
    }

    // Sub-bins (the effect modules) are listed while their parent bin or one of its sub-bins is selected.
    bool binListed(int bin) const {
      int parent = SkinBins::parent(bin);
      return parent < 0 || bin_ == parent || SkinBins::parent(bin_) == parent;
    }

    void selectBin(int bin) {
      bin_ = bin;
      resized();
      for (int i = 0; i < static_cast<int>(bin_buttons_.size()); ++i)
        bin_buttons_[i]->setToggleState(i == bin, dontSendNotification);
      add_button_.setVisible(bin > 0);
      selected_row_ = -1;
      rebuildRows();
      repaint();
    }

    void rebuildRows() {
      int keep_id = -1;
      bool keep_value = false;
      if (SkinEditorRow* row = selectedRow()) {
        keep_id = row->id();
        keep_value = row->isValue();
      }

      rows_.clear();
      rows_container_.removeAllChildren();

      bool global = bin_ == 0;
      for (int i = 0; i < Skin::kNumColors; ++i) {
        int id = Skin::kInitialColor + i;
        if (global || binHas(false, id))
          addRow(false, id, kColorNames[i], !global);
      }
      for (int i = 0; i < Skin::kNumSkinValueIds; ++i) {
        if (global || binHas(true, i))
          addRow(true, i, kValueNames[i], !global);
      }

      layoutRows();

      SkinEditorRow* reselect = nullptr;
      for (auto& row : rows_) {
        if (row->id() == keep_id && row->isValue() == keep_value)
          reselect = row.get();
      }
      if (reselect == nullptr && !rows_.empty())
        reselect = rows_[0].get();
      selectRow(reselect);
    }

    void addRow(bool is_value, int id, const std::string& name, bool removable) {
      auto row = std::make_unique<SkinEditorRow>(is_value, id, String(name), removable);
      if (is_value)
        row->setValue(binValue(id));
      else {
        row->setColor(binColor(id));
        row->setGradientStops(skin_->getGradient(SkinBins::sections(bin_)[0], static_cast<Skin::ColorId>(id)).stops);
      }

      row->on_select = [this](SkinEditorRow* r) { selectRow(r); };
      Component::SafePointer<SkinEditor> safe(this);
      row->on_remove = [safe](SkinEditorRow* r) {
        MessageManager::callAsync([safe, r]() {
          if (SkinEditor* editor = safe.getComponent())
            editor->removeRow(r);
        });
      };
      row->on_value = [this](SkinEditorRow* r, float value) { selectRow(r); applyValue(r, value); };
      rows_container_.addAndMakeVisible(row.get());
      rows_.push_back(std::move(row));
    }

    void layoutRows() {
      int width = rows_viewport_.getMaximumVisibleWidth();
      int y = 0;
      for (auto& row : rows_) {
        row->setBounds(0, y, width, SkinEditorRow::kHeight);
        y += SkinEditorRow::kHeight;
      }
      rows_container_.setSize(width, std::max(y, 1));
    }

    void showAddMenu() {
      PopupMenu menu, colors, values;
      for (int i = 0; i < Skin::kNumColors; ++i) {
        int id = Skin::kInitialColor + i;
        if (!binHas(false, id))
          colors.addItem(1000 + i, String(kColorNames[i]));
      }
      for (int i = 0; i < Skin::kNumSkinValueIds; ++i) {
        if (!binHas(true, i))
          values.addItem(5000 + i, String(kValueNames[i]));
      }
      menu.addSubMenu("Colour", colors, colors.getNumItems() > 0);
      menu.addSubMenu("Value", values, values.getNumItems() > 0);

      menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&add_button_),
                         [this, safe = Component::SafePointer<SkinEditor>(this)](int result) {
        if (safe == nullptr || result == 0)
          return;

        pushUndo("add");
        coalesce_key_ = String();
        int bin = bin_;
        if (result >= 5000) {
          Skin::ValueId id = static_cast<Skin::ValueId>(result - 5000);
          for (Skin::SectionOverride section : SkinBins::sections(bin))
            skin_->addOverrideValue(section, id, skin_->getValue(id));
          rebuildRows();
          selectById(true, id);
        }
        else {
          int id = Skin::kInitialColor + result - 1000;
          for (Skin::SectionOverride section : SkinBins::sections(bin))
            skin_->addOverrideColor(section, static_cast<Skin::ColorId>(id), skin_->getColor(static_cast<Skin::ColorId>(id)));
          rebuildRows();
          selectById(false, id);
        }
        reloadInterface();
      });
    }

    void selectById(bool is_value, int id) {
      for (auto& row : rows_) {
        if (row->id() == id && row->isValue() == is_value) {
          selectRow(row.get());
          rows_viewport_.setViewPosition(0, std::max(0, row->getY() - 40));
        }
      }
    }

    void restoreState(const String& state) {
      skin_->stringToState(state);
      reloadInterface();
      rebuildRows();
      updateUndoButtons();
    }

    void resetToDefault() {
      pushUndo("default");
      coalesce_key_ = String();
      File saved = LoadSave::getDefaultSkin();
      if (saved.exists() && saved.hasWriteAccess())
        saved.deleteFile();

      skin_->loadDefaultSkin();
      reloadInterface();
      refreshGlow();
      rebuildRows();
    }

    void undo() {
      if (undo_stack_.empty())
        return;
      redo_stack_.push_back(skin_->stateToString());
      String state = undo_stack_.back();
      undo_stack_.pop_back();
      coalesce_key_ = String();
      restoreState(state);
    }

    void redo() {
      if (redo_stack_.empty())
        return;
      undo_stack_.push_back(skin_->stateToString());
      String state = redo_stack_.back();
      redo_stack_.pop_back();
      coalesce_key_ = String();
      restoreState(state);
    }

    void save() {
      FileChooser save_box("Save Skin", File(), String("*.") + vital::kSkinExtension);
      if (save_box.browseForFileToSave(true))
        skin_->saveToFile(save_box.getResult().withFileExtension(vital::kSkinExtension));
    }

    void import() {
      FileChooser open_box("Import Skin", File(), String(vital::kSkinExtensionsList));
      if (!open_box.browseForFileToOpen())
        return;

      String before = skin_->stateToString();
      if (!skin_->loadFromFile(open_box.getResult())) {
        skin_->stringToState(before);
        AlertWindow::showNativeDialogBox("Error importing skin", "Skin file is corrupted and won't load.", false);
        return;
      }

      undo_stack_.push_back(before);
      redo_stack_.clear();
      coalesce_key_ = String();
      reloadInterface();
      rebuildRows();
      updateUndoButtons();
    }

    Skin* skin_;
    Component::SafePointer<FullInterface> full_interface_;
    int bin_;
    int selected_row_;
    int hover_editor_bin_;
    int last_hover_bin_;

    std::vector<std::unique_ptr<TextButton>> bin_buttons_;
    TextButton default_button_, undo_button_, redo_button_, import_button_, save_button_, add_button_;
    Viewport rows_viewport_;
    Component rows_container_;
    std::vector<std::unique_ptr<SkinEditorRow>> rows_;
    Label hex_label_, hint_, glow_label_, size_label_, angle_label_;
    TextButton grad_toggle_, target_buttons_[3];
    Slider angle_slider_;
    int edit_target_ = 0;
    bool updating_gradient_controls_ = false;
    Slider glow_slider_, size_slider_;
    TextEditor hex_;
    ColourSelector selector_;

    std::vector<String> undo_stack_, redo_stack_;
    String coalesce_key_;
    double coalesce_time_ = 0.0;
    bool reload_pending_ = false;
    double last_reload_ = 0.0;
};

SkinDesigner::SkinDesigner(Skin* skin, FullInterface* full_interface) :
    DocumentWindow("VialFX Skin Editor", kEditorBackground, DocumentWindow::closeButton) {
  setAlwaysOnTop(true);
  setResizeLimits(860, 700, 3000, 3000);
  container_ = std::make_unique<SkinEditor>(skin, full_interface);
  container_->setSize(860, 840);
  setContentNonOwned(container_.get(), false);
}

SkinDesigner::~SkinDesigner() = default;
