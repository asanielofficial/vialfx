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

  inline bool isAutomationResult(int result) {
    return result >= kClear && result < kFirstSlot + vital::kNumAutomationSlots;
  }

  inline PopupItems build(SynthBase* synth, const std::string& parameter) {
    PopupItems menu("Host Automation Slot");
    int current = synth->findAutomationSlot(parameter);
    for (int group = 0; group < vital::kNumAutomationSlots; group += kGroupSize) {
      PopupItems sub("Slots " + std::to_string(group + 1) + " - " + std::to_string(group + kGroupSize));
      for (int slot = group; slot < group + kGroupSize; ++slot) {
        std::string label = "Automation " + std::to_string(slot + 1);
        const std::string& taken = synth->getAutomationSlot(slot);
        if (!taken.empty() && slot != current && vital::Parameters::isParameter(taken))
          label += " (" + vital::Parameters::getDetails(taken).display_name + ")";
        sub.addItem(kFirstSlot + slot, label, slot == current);
      }
      menu.addItem(sub);
    }
    if (current >= 0)
      menu.addItem(kClear, "Remove from Automation " + std::to_string(current + 1));
    return menu;
  }

  inline bool handle(SynthBase* synth, const std::string& parameter, int result) {
    if (result == kClear) {
      synth->clearAutomationSlot(parameter);
      return true;
    }
    if (result >= kFirstSlot && result < kFirstSlot + vital::kNumAutomationSlots) {
      synth->setAutomationSlot(result - kFirstSlot, parameter);
      return true;
    }
    return false;
  }
}
