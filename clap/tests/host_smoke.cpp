// Headless smoke test of the CLAP host layer: audio through the engine, parameters, state round trip.
#include "vialfx_host.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;
  vialfx::VialfxHost host;
  CHECK(host.params().size() > 100);
  std::printf("%zu parameters\n", host.params().size());

  const int frames = 512;
  host.prepare(44100.0, frames);

  std::vector<float> in_l(frames), in_r(frames), out_l(frames), out_r(frames);
  for (int i = 0; i < frames; ++i) {
    in_l[i] = 0.5f * std::sin(i * 0.05f);
    in_r[i] = in_l[i];
  }
  vialfx::AudioBlock block;
  block.input[0] = in_l.data(); block.input[1] = in_r.data();
  block.output[0] = out_l.data(); block.output[1] = out_r.data();
  block.num_samples = frames;
  juce::MidiBuffer midi;
  vialfx::Transport transport;

  float energy = 0.0f;
  for (int b = 0; b < 40; ++b) {
    host.process(block, midi, transport);
    for (int i = 0; i < frames; ++i) {
      CHECK(std::isfinite(out_l[i]) && std::isfinite(out_r[i]));
      energy += out_l[i] * out_l[i];
    }
  }
  std::printf("output energy %f\n", energy);
  CHECK(energy > 0.001f);

  // Parameter round trip through names, ids and text.
  const vialfx::ParamInfo* p = host.findParam(host.params()[10].id);
  CHECK(p != nullptr);
  CHECK(host.findParam(p->name) == p);
  float v = 0.0f;
  std::string text = host.valueToText(*p, p->default_value);
  CHECK(!text.empty());
  host.textToValue(*p, text, &v);

  // State round trip.
  host.setParamFromHost(*p, p->max);
  host.process(block, midi, transport);
  std::string state = host.saveState();
  CHECK(state.size() > 100);
  vialfx::VialfxHost other;
  other.prepare(44100.0, frames);
  CHECK(other.loadState(state.data(), state.size()));
  other.process(block, midi, transport);
  const vialfx::ParamInfo* q = other.findParam(p->id);
  std::printf("%s: %f -> %f\n", p->name.c_str(), host.getParamValue(*p), other.getParamValue(*q));
  CHECK(std::fabs(host.getParamValue(*p) - other.getParamValue(*q)) < 1e-3f);

  std::printf(failures ? "FAILED\n" : "OK\n");
  return failures ? 1 : 0;
}
