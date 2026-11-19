#pragma once

#include "JuceHeader.h"

// Writes a line to VialFX_debug.log in the temp folder, only when the VIALFX_DEBUG_LOG (or VIALFX_SELFTEST)
// environment variable is set. For tracking down where the interface stops while it opens.
namespace vial_debug {
  inline bool enabled() {
    static const bool on = SystemStats::getEnvironmentVariable("VIALFX_DEBUG_LOG", "").isNotEmpty() ||
                           SystemStats::getEnvironmentVariable("VIALFX_SELFTEST", "").isNotEmpty();
    return on;
  }

  inline void log(const String& message) {
    if (!enabled())
      return;

    File file = File::getSpecialLocation(File::tempDirectory).getChildFile("VialFX_debug.log");
    file.appendText(Time::getCurrentTime().toString(false, true, true, true) + "  " + message + "\n");
  }
}
