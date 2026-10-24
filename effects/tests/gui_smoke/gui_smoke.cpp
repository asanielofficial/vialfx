// VialFX GUI smoke test (Linux, run under xvfb).
//
// Loads the built VST3 the way a host does, opens its editor, runs a fake audio thread, and then drives the
// interface with real mouse events (click and drag on components found by name). If the plugin crashes, the
// last "[smoke]" line says which step did it, and running under gdb gives the backtrace.
//
//   gui_smoke path/to/VialFX.vst3

#include "AppConfig.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace juce;

namespace {
  constexpr double kSampleRate = 44100.0;
  constexpr int kBlockSize = 512;

  void say(const String& text) {
    std::fprintf(stderr, "[smoke] %s\n", text.toRawUTF8());
    std::fflush(stderr);
  }

  void pump(int milliseconds) {
    MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
  }

  void collect(Component* root, const String& name, Array<Component*>& found) {
    if (root->getName() == name)
      found.add(root);
    for (Component* child : root->getChildren())
      collect(child, name, found);
  }

  // The component with this name that is actually on screen (names can repeat, e.g. hover copies).
  Component* find(Component* root, const String& name) {
    Array<Component*> found;
    collect(root, name, found);
    for (Component* c : found) {
      if (c->isShowing() && c->getWidth() > 0 && c->getHeight() > 0)
        return c;
    }
    return found.isEmpty() ? nullptr : found.getFirst();
  }

  void mouseEvent(Component* c, Point<float> local, bool pressed) {
    ComponentPeer* peer = c->getPeer();
    if (peer == nullptr)
      return;
    Point<float> in_peer = peer->globalToLocal(c->localPointToGlobal(local));
    ModifierKeys mods = pressed ? ModifierKeys(ModifierKeys::leftButtonModifier) : ModifierKeys();
    peer->handleMouseEvent(MouseInputSource::InputSourceType::mouse, in_peer, mods, 0.0f, 0.0f,
                           Time::currentTimeMillis());
  }

  bool click(Component* c) {
    if (c == nullptr || c->getPeer() == nullptr)
      return false;
    Point<float> centre = c->getLocalBounds().getCentre().toFloat();
    mouseEvent(c, centre, false);
    pump(30);
    mouseEvent(c, centre, true);
    pump(60);
    mouseEvent(c, centre, false);
    pump(60);
    return true;
  }

  bool drag(Component* c, float dx, float dy) {
    if (c == nullptr || c->getPeer() == nullptr)
      return false;
    Point<float> start = c->getLocalBounds().getCentre().toFloat();
    mouseEvent(c, start, false);
    pump(30);
    mouseEvent(c, start, true);
    pump(40);
    for (int i = 1; i <= 12; ++i) {
      mouseEvent(c, start + Point<float>(dx * i / 12.0f, dy * i / 12.0f), true);
      pump(20);
    }
    mouseEvent(c, start + Point<float>(dx, dy), false);
    pump(60);
    return true;
  }

  // Stands in for the host's audio callback.
  class AudioRunner : public Thread {
    public:
      AudioRunner(AudioProcessor* processor) : Thread("audio"), processor_(processor) { }

      void run() override {
        int channels = std::max(processor_->getTotalNumInputChannels(), processor_->getTotalNumOutputChannels());
        AudioBuffer<float> buffer(std::max(channels, 2), kBlockSize);
        MidiBuffer midi;
        double phase = 0.0;
        while (!threadShouldExit()) {
          for (int i = 0; i < kBlockSize; ++i) {
            float s = 0.25f * std::sin(phase);
            phase += 2.0 * MathConstants<double>::pi * 220.0 / kSampleRate;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
              buffer.setSample(ch, i, s);
          }
          processor_->processBlock(buffer, midi);
          wait(10);
        }
      }

    private:
      AudioProcessor* processor_;
  };

  struct Editor {
    std::unique_ptr<AudioProcessorEditor> editor;
    std::unique_ptr<DocumentWindow> window;
  };

  struct Window : public DocumentWindow {
    Window() : DocumentWindow("VialFX smoke", Colours::black, DocumentWindow::allButtons) { }
    void closeButtonPressed() override { }
  };

  void openEditor(AudioProcessor& processor, Editor& e) {
    e.editor.reset(processor.createEditorIfNeeded());
    if (e.editor == nullptr) {
      say("FAIL: plugin has no editor");
      std::exit(2);
    }
    e.window = std::make_unique<Window>();
    e.window->setUsingNativeTitleBar(false);
    e.window->setContentNonOwned(e.editor.get(), true);
    e.window->setVisible(true);
    e.window->centreWithSize(e.editor->getWidth(), e.editor->getHeight());
  }

  void closeEditor(AudioProcessor& processor, Editor& e) {
    if (e.window)
      e.window->clearContentComponent();
    e.window.reset();
    if (e.editor)
      processor.editorBeingDeleted(e.editor.get());
    e.editor.reset();
  }

  int failures = 0;

  void expect(bool ok, const String& what) {
    if (!ok) {
      say("FAIL: " + what);
      ++failures;
    }
  }
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: gui_smoke path/to/Plugin.vst3\n");
    return 64;
  }

  ScopedJuceInitialiser_GUI juce_init;

  VST3PluginFormat format;
  OwnedArray<PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions, argv[1]);
  if (descriptions.isEmpty()) {
    say("FAIL: no plugin found in " + String(argv[1]));
    return 2;
  }

  AudioPluginFormatManager manager;
  manager.addFormat(new VST3PluginFormat());
  String error;
  std::unique_ptr<AudioPluginInstance> plugin = manager.createPluginInstance(*descriptions[0], kSampleRate,
                                                                              kBlockSize, error);
  if (plugin == nullptr) {
    say("FAIL: could not load plugin: " + error);
    return 2;
  }
  say("loaded " + plugin->getName() + ", " + String(plugin->getParameters().size()) + " parameters");

  plugin->prepareToPlay(kSampleRate, kBlockSize);
  AudioRunner audio(plugin.get());
  audio.startThread();

  Editor e;
  openEditor(*plugin, e);
  say("editor opened " + String(e.editor->getWidth()) + "x" + String(e.editor->getHeight()));
  pump(2500);
  say("step ok: first render");

  Component* root = e.editor.get();

  auto tab = [&](int index, const char* label) {
    Component* selector = find(root, "tab_selector");
    expect(selector != nullptr, "tab selector exists");
    if (auto* slider = dynamic_cast<Slider*>(selector))
      slider->setValue(index, sendNotificationSync);
    pump(1200);
    say(String("step ok: tab ") + label);
  };

  // Header visualizer: the click that toggles oscilloscope/spectrum.
  Component* spectrum = find(root, "view_spectrogram");
  expect(spectrum != nullptr, "view_spectrogram exists");
  say("step: click spectrum display");
  click(spectrum);
  pump(2500);
  say("step ok: spectrum shown");
  click(spectrum);
  pump(1500);
  say("step ok: spectrum hidden");

  // Advanced tab.
  tab(3, "ADVANCED");

  Component* mpe = find(root, "mpe_enabled");
  expect(mpe != nullptr, "mpe_enabled exists");
  say("step: click MPE enabled");
  click(mpe);
  pump(1500);
  say("step ok: MPE on");
  click(mpe);
  pump(800);
  say("step ok: MPE off");

  Component* transpose = find(root, "voice_transpose");
  expect(transpose != nullptr, "voice_transpose exists");
  say("step: drag transpose");
  drag(transpose, 0.0f, -40.0f);
  pump(1200);
  say("step ok: transpose dragged up");
  drag(transpose, 0.0f, 90.0f);
  pump(1200);
  say("step ok: transpose dragged down");
  if (auto* slider = dynamic_cast<Slider*>(transpose)) {
    slider->setValue(7.0, sendNotificationSync);
    pump(800);
    slider->setValue(0.0, sendNotificationSync);
    pump(800);
  }
  say("step ok: transpose set directly");

  for (const char* name : { "voice_tune", "voice_priority", "voice_override", "oversampling" }) {
    if (Component* c = find(root, name)) {
      say(String("step: drag ") + name);
      drag(c, 0.0f, -25.0f);
      pump(600);
      say(String("step ok: ") + name);
    }
  }

  // Other tabs, with a few controls on each.
  tab(2, "MATRIX");
  tab(1, "EFFECTS");
  tab(0, "VOICE");

  for (const char* name : { "filter_1_cutoff", "filter_2_cutoff", "filter_1_resonance", "filter_2_mix",
                            "input_spectral_morph_amount", "input_distortion_amount",
                            "sidechain_spectral_morph_amount", "sidechain_distortion_amount" }) {
    if (Component* c = find(root, name)) {
      say(String("step: drag ") + name);
      drag(c, 0.0f, -30.0f);
      pump(500);
      say(String("step ok: ") + name);
    }
  }

  // Every host parameter, set to random values while the editor is open.
  say("step: randomise host parameters");
  Random random(1234);
  for (int pass = 0; pass < 3; ++pass) {
    for (AudioProcessorParameter* parameter : plugin->getParameters())
      parameter->setValueNotifyingHost(random.nextFloat());
    pump(800);
  }
  say("step ok: parameters randomised");

  // Closing and reopening the editor.
  closeEditor(*plugin, e);
  pump(500);
  say("step ok: editor closed");
  openEditor(*plugin, e);
  pump(1500);
  say("step ok: editor reopened");
  closeEditor(*plugin, e);
  pump(500);

  audio.stopThread(2000);
  plugin->releaseResources();
  plugin.reset();
  say("step ok: plugin unloaded");

  say(failures == 0 ? "ALL OK" : "DONE WITH FAILURES");
  return failures == 0 ? 0 : 1;
}
