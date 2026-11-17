#pragma once

#include "JuceHeader.h"

// Writes a line to VialFX_debug.log in the temp folder. For tracking down where the interface stops while it opens.
namespace vial_debug {
  inline void log(const String& message) {
    File file = File::getSpecialLocation(File::tempDirectory).getChildFile("VialFX_debug.log");
    file.appendText(Time::getCurrentTime().toString(false, true, true, true) + "  " + message + "\n");
  }
}
