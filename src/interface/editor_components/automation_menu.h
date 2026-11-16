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

#include "JuceHeader.h"
#include "synth_base.h"
#include "synth_parameters.h"
#include "synth_section.h"

// The right-click submenu that assigns a parameter to one of the host automation slots.
namespace automation_menu {
  constexpr int kClear = 100000;
  constexpr int kFirstSlot = 100001;
  constexpr int kGroupSize = 16;
  // The slot manager: choosing a mapped slot empties it.
  constexpr int kClearAll = kFirstSlot + vital::kNumAutomationSlots;
  constexpr int kFirstManaged = kClearAll + 1;

  inline bool isAutomationResult(int result) {
    return result >= kClear && result < kFirstManaged + vital::kNumAutomationSlots;
  }

  inline std::string slotLabel(SynthBase* synth, int slot) {
    std::string label = std::to_string(slot + 1) + ": ";
    const std::string& mapped = synth->getAutomationSlot(slot);
    if (mapped.empty())
      return label + "(empty)";
    if (vital::Parameters::isParameter(mapped))
      return label + vital::Parameters::getDetails(mapped).display_name + "  (click to clear)";
    return label + mapped + "  (click to clear)";
  }

  inline PopupItems buildManager(SynthBase* synth) {
    PopupItems manager("Manage Automation Slots");
    int used = 0;
    for (int slot = 0; slot < vital::kNumAutomationSlots; ++slot) {
      used += synth->getAutomationSlot(slot).empty() ? 0 : 1;
      manager.addItem(kFirstManaged + slot, slotLabel(synth, slot), !synth->getAutomationSlot(slot).empty());
    }
    if (used > 0)
      manager.addItem(kClearAll, "Clear all " + std::to_string(used) + " mapped slots");
    return manager;
  }

  // Two levels only (the popup lists are flat): the slots to assign to, and the manager.
  inline PopupItems build(SynthBase* synth, const std::string& parameter) {
    PopupItems menu("Host Automation Slot");
    int current = synth->findAutomationSlot(parameter);
    for (int slot = 0; slot < vital::kNumAutomationSlots; ++slot) {
      std::string label = "Automation " + std::to_string(slot + 1);
      const std::string& taken = synth->getAutomationSlot(slot);
      if (!taken.empty() && slot != current && vital::Parameters::isParameter(taken))
        label += " (" + vital::Parameters::getDetails(taken).display_name + ")";
      menu.addItem(kFirstSlot + slot, label, slot == current);
    }
    if (current >= 0)
      menu.addItem(kClear, "Remove from Automation " + std::to_string(current + 1));
    return menu;
  }

  inline void addTo(PopupItems& options, SynthBase* synth, const std::string& parameter) {
    options.addItem(build(synth, parameter));
    options.addItem(buildManager(synth));
  }

  inline bool handle(SynthBase* synth, const std::string& parameter, int result) {
    if (result == kClear) {
      synth->clearAutomationSlot(parameter);
      return true;
    }
    if (result == kClearAll) {
      for (int slot = 0; slot < vital::kNumAutomationSlots; ++slot)
        synth->setAutomationSlot(slot, "");
      return true;
    }
    if (result >= kFirstManaged && result < kFirstManaged + vital::kNumAutomationSlots) {
      synth->setAutomationSlot(result - kFirstManaged, "");
      return true;
    }
    if (result >= kFirstSlot && result < kFirstSlot + vital::kNumAutomationSlots) {
      synth->setAutomationSlot(result - kFirstSlot, parameter);
      return true;
    }
    return false;
  }
}
