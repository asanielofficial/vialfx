/* The seam between the CLAP plugin (JUCE, C++17) and the Visage interface (C++20).
 * Nothing here may include JUCE or Visage headers.
 *
 * GPL-3.0-or-later, like the rest of VialFX.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vialfx {

  struct GuiParam {
    uint32_t id = 0;
    std::string name;          // engine name, "filter_fx_cutoff"
    std::string display_name;
    std::string module;
    float min = 0.0f, max = 1.0f, default_value = 0.0f;
    bool stepped = false;
  };

  // What the interface may ask of the plugin. Called on the interface (main) thread.
  class GuiHost {
    public:
      virtual ~GuiHost() = default;
      virtual const std::vector<GuiParam>& params() const = 0;
      virtual float value(uint32_t id) const = 0;
      virtual std::string valueText(uint32_t id, float value) const = 0;
      virtual void beginGesture(uint32_t id) = 0;
      virtual void setValue(uint32_t id, float value) = 0;
      virtual void endGesture(uint32_t id) = 0;
      virtual std::string presetName() const = 0;
      virtual std::string version() const = 0;
      // Recent peak levels (0..1+) of the input and output, for the meters.
      virtual void levels(float* input_l, float* input_r, float* output_l, float* output_r) const = 0;
  };

  // The window the plugin embeds in the host. Pixel sizes are native pixels.
  class GuiWindow {
    public:
      virtual ~GuiWindow() = default;
      virtual bool attach(void* native_parent) = 0;
      virtual void close() = 0;
      virtual uint32_t width() const = 0;
      virtual uint32_t height() const = 0;
      virtual void setSizeFromHost(uint32_t width, uint32_t height) = 0;
      virtual void adjustSize(uint32_t* width, uint32_t* height) = 0;
      virtual bool fixedAspectRatio() const = 0;
      virtual float aspectRatio() const = 0;
      // The window asks the host for a new size (the interface was resized from inside).
      std::function<void()> on_resized;
      virtual int posixFd() const { return -1; }
      virtual void processPosixFd() { }
  };

  std::unique_ptr<GuiWindow> createGuiWindow(GuiHost* host);
}
