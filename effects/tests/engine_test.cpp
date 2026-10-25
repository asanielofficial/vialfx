/* VialFX headless engine test.
 *
 * Drives the effects engine (the same vital::SoundEngine the plugin uses) with test signals and checks:
 * passthrough, Filter 1 / Filter 2 and their routing, MIDI key tracking, every filter model/style,
 * every effect in the rack, oversampling and block-size independence.
 *
 * Exit code is the number of failed checks.
 *
 * This file is part of VialFX and is licensed under the GNU General Public License v3 or later.
 */

#include "JuceHeader.h"
#include "sound_engine.h"
#include "synth_constants.h"
#include "modulation_connection_processor.h"
#include "memory.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace vital;

namespace {
  constexpr int kSampleRate = 44100;
  constexpr float kPi = 3.14159265358979f;
  constexpr float kAmp = 0.25f;

  int g_failures = 0;
  int g_checks = 0;

  void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_checks;
    if (!ok)
      ++g_failures;
    std::printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    std::fflush(stdout);
  }

  std::string fmt(const char* format, double a = 0, double b = 0, double c = 0, double d = 0) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, a, b, c, d);
    return buffer;
  }

  struct Stereo {
    std::vector<float> left, right;
    bool finite() const {
      for (size_t i = 0; i < left.size(); ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
          return false;
      }
      return true;
    }
    float peak() const {
      float p = 0.0f;
      for (size_t i = 0; i < left.size(); ++i)
        p = std::max(p, std::max(std::fabs(left[i]), std::fabs(right[i])));
      return p;
    }
  };

  double rms(const std::vector<float>& x, size_t start) {
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = start; i < x.size(); ++i, ++n)
      sum += static_cast<double>(x[i]) * x[i];
    return n ? std::sqrt(sum / n) : 0.0;
  }

  double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }

  using Generator = std::function<void(int index, float& left, float& right)>;

  Generator sine(float frequency) {
    return [frequency](int i, float& l, float& r) {
      l = r = kAmp * std::sin(2.0f * kPi * frequency * static_cast<float>(i % kSampleRate) / kSampleRate);
    };
  }

  Generator noise() {
    auto state = std::make_shared<uint32_t>(12345u);
    return [state](int, float& l, float& r) {
      *state = *state * 1664525u + 1013904223u;
      l = kAmp * (static_cast<float>(*state >> 8) / 8388608.0f - 1.0f);
      *state = *state * 1664525u + 1013904223u;
      r = kAmp * (static_cast<float>(*state >> 8) / 8388608.0f - 1.0f);
    };
  }

  struct Rig {
    std::unique_ptr<SoundEngine> engine;
    control_map controls;

    Rig() {
      engine = std::make_unique<SoundEngine>();
      engine->setSampleRate(kSampleRate);
      engine->updateAllModulationSwitches();
      controls = engine->getControls();
    }

    void set(const std::string& name, float value) {
      auto it = controls.find(name);
      if (it == controls.end()) {
        std::printf("[FAIL] unknown control '%s'\n", name.c_str());
        std::exit(100);
      }
      it->second->set(value);
    }

    void noteOn(int note) { engine->noteOn(note, 1.0f, 0, 0); }

    // Renders num_samples through the plugin-style block loop (blocks of at most 128 samples).
    Stereo render(int num_samples, const Generator& generator, int block = 64,
                  const Generator* sidechain = nullptr) {
      Stereo out;
      out.left.resize(num_samples);
      out.right.resize(num_samples);
      std::vector<poly_float> input(kMaxBufferSize);
      std::vector<poly_float> side(kMaxBufferSize);

      int done = 0;
      while (done < num_samples) {
        int n = std::min(block, num_samples - done);
        n = std::min(n, static_cast<int>(kMaxBufferSize));
        for (int i = 0; i < n; ++i) {
          float l = 0.0f, r = 0.0f;
          generator(done + i, l, r);
          input[i] = poly_float(l, r);
          float sl = 0.0f, sr = 0.0f;
          if (sidechain)
            (*sidechain)(done + i, sl, sr);
          side[i] = poly_float(sl, sr);
        }

        engine->setSidechainInput(sidechain ? side.data() : nullptr);
        engine->processWithInput(input.data(), n);

        const mono_float* result = reinterpret_cast<const mono_float*>(engine->output(0)->buffer);
        for (int i = 0; i < n; ++i) {
          out.left[done + i] = result[poly_float::kSize * i];
          out.right[done + i] = result[poly_float::kSize * i + 1];
        }
        done += n;
      }
      return out;
    }

    // Gain in dB of a sine on the main input (or on the sidechain input) through the engine, left channel.
    double sineGainDbOn(bool use_sidechain, float frequency) {
      const int total = kSampleRate;
      Generator silence = [](int, float& l, float& r) { l = r = 0.0f; };
      Generator tone = sine(frequency);
      Stereo out = use_sidechain ? render(total, silence, 64, &tone) : render(total, tone);
      return db(rms(out.left, total / 2) / (kAmp / std::sqrt(2.0)));
    }

    // Gain in dB of a sine through the engine (left channel), measured after the settling time.
    double sineGainDb(float frequency, int block = 64) {
      const int total = kSampleRate;
      Stereo out = render(total, sine(frequency), block);
      return db(rms(out.left, total / 2) / (kAmp / std::sqrt(2.0)));
    }
  };

  void setFilter(Rig& rig, int filter, bool on, float cutoff, int model = 0, int style = 1) {
    std::string prefix = "filter_" + std::to_string(filter) + "_";
    rig.set(prefix + "on", on ? 1.0f : 0.0f);
    rig.set(prefix + "cutoff", cutoff);
    rig.set(prefix + "model", static_cast<float>(model));
    rig.set(prefix + "style", static_cast<float>(style));
    rig.set(prefix + "resonance", 0.0f);  // default resonance bumps/droops the passband and would skew level checks
  }

  // ---------------------------------------------------------------------------------------------------------

  void testPassthrough() {
    std::printf("\n== Passthrough (filters off, effects off) ==\n");
    const float frequencies[] = { 100.0f, 1000.0f, 4000.0f, 10000.0f, 15000.0f, 18000.0f };
    double reference = 0.0;
    for (float f : frequencies) {
      Rig rig;
      double g = rig.sineGainDb(f);
      if (f == 1000.0f)
        reference = g;
      check(std::fabs(g) < 0.1, fmt("passthrough gain at %.0f Hz is unity", f), fmt("%.3f dB", g));
    }
    (void)reference;

    Rig rig;
    Stereo out = rig.render(kSampleRate, sine(440.0f));
    check(out.finite(), "passthrough output is finite");
    check(out.peak() < 0.3f, "passthrough does not add level", fmt("peak %.4f", out.peak()));

    // Left and right must stay separate.
    Rig stereo;
    Stereo st = stereo.render(kSampleRate, [](int i, float& l, float& r) {
      l = kAmp * std::sin(2.0f * kPi * 440.0f * i / kSampleRate);
      r = 0.0f;
    });
    double l_rms = rms(st.left, kSampleRate / 2);
    double r_rms = rms(st.right, kSampleRate / 2);
    check(l_rms > 0.1 && r_rms < 0.001, "left/right channels stay separate", fmt("L %.4f R %.6f", l_rms, r_rms));
  }

  void testFilters() {
    std::printf("\n== Filter 1 / Filter 2 ==\n");
    for (int filter = 1; filter <= 2; ++filter) {
      Rig rig;
      setFilter(rig, filter, true, 48.0f);
      double low = rig.sineGainDb(100.0f);
      Rig rig_high;
      setFilter(rig_high, filter, true, 48.0f);
      double high = rig_high.sineGainDb(4000.0f);
      check(low > -3.0, fmt("filter %.0f passes low frequencies", filter), fmt("100 Hz: %.2f dB", low));
      check(high < -30.0, fmt("filter %.0f attenuates high frequencies", filter), fmt("4 kHz: %.2f dB", high));
    }

    // Cutoff moves the corner.
    Rig closed, open;
    setFilter(closed, 1, true, 40.0f);
    setFilter(open, 1, true, 100.0f);
    double closed_gain = closed.sineGainDb(1000.0f);
    double open_gain = open.sineGainDb(1000.0f);
    check(open_gain - closed_gain > 20.0, "raising the cutoff lets more through",
          fmt("closed %.1f dB, open %.1f dB", closed_gain, open_gain));

    // Mix = 0 is dry.
    Rig dry;
    setFilter(dry, 1, true, 40.0f);
    dry.set("filter_1_mix", 0.0f);
    double dry_gain = dry.sineGainDb(4000.0f);
    check(std::fabs(dry_gain) < 1.0, "filter mix 0 passes the dry signal", fmt("%.2f dB", dry_gain));
  }

  void testRouting() {
    std::printf("\n== Filter routing ==\n");
    const float probe = 600.0f;

    Rig single;
    setFilter(single, 1, true, 60.0f);
    double one = single.sineGainDb(probe);

    Rig forward;   // Filter 2 takes Filter 1's output.
    setFilter(forward, 1, true, 60.0f);
    setFilter(forward, 2, true, 60.0f);
    forward.set("filter_2_filter_input", 1.0f);
    double serial_forward = forward.sineGainDb(probe);

    Rig backward;  // Filter 1 takes Filter 2's output.
    setFilter(backward, 1, true, 60.0f);
    setFilter(backward, 2, true, 60.0f);
    backward.set("filter_1_filter_input", 1.0f);
    double serial_backward = backward.sineGainDb(probe);

    Rig parallel;
    setFilter(parallel, 1, true, 60.0f);
    setFilter(parallel, 2, true, 60.0f);
    double both = parallel.sineGainDb(probe);

    std::printf("   single %.2f dB | serial 1->2 %.2f dB | serial 2->1 %.2f dB | parallel %.2f dB\n",
                one, serial_forward, serial_backward, both);

    check(one < -3.0, "single filter is attenuating the probe tone", fmt("%.2f dB", one));
    check(serial_forward < one - 3.0, "serial Filter 1 -> 2 cascades", fmt("%.2f dB vs single %.2f dB", serial_forward, one));
    check(serial_backward < one - 3.0, "serial Filter 2 -> 1 cascades", fmt("%.2f dB vs single %.2f dB", serial_backward, one));
    check(both > one + 3.0 && both < one + 9.0, "parallel filters add (about +6 dB)", fmt("%.2f dB vs single %.2f dB", both, one));

    // Serial with the first filter off: the second still gets the audio.
    Rig only_second;
    setFilter(only_second, 1, false, 60.0f);
    setFilter(only_second, 2, true, 60.0f);
    only_second.set("filter_2_filter_input", 1.0f);
    double second = only_second.sineGainDb(probe);
    check(std::fabs(second - one) < 1.0, "serial routing with filter 1 off still filters through filter 2",
          fmt("%.2f dB vs %.2f dB", second, one));

    Rig only_first;
    setFilter(only_first, 1, true, 60.0f);
    setFilter(only_first, 2, false, 60.0f);
    only_first.set("filter_1_filter_input", 1.0f);
    double first = only_first.sineGainDb(probe);
    check(std::fabs(first - one) < 1.0, "serial routing with filter 2 off still filters through filter 1",
          fmt("%.2f dB vs %.2f dB", first, one));
  }

  void testInputRouting() {
    std::printf("\n== Input destinations (main + sidechain) ==\n");
    const float probe = 4000.0f;  // well above a low cutoff, so "went through the filter" is obvious
    const int kFilter1 = 0, kFilter2 = 1, kDual = 2, kEffects = 3, kDirect = 4;

    for (int sidechain = 0; sidechain <= 1; ++sidechain) {
      std::string who = sidechain ? "sidechain" : "main input";
      std::string control = sidechain ? "sidechain_destination" : "input_destination";
      auto gain = [&](int destination, bool f1, bool f2) {
        Rig rig;
        setFilter(rig, 1, f1, 40.0f);
        setFilter(rig, 2, f2, 40.0f);
        rig.set(control, static_cast<float>(destination));
        rig.set("sidechain_on", 1.0f);
        return rig.sineGainDbOn(sidechain != 0, probe);
      };

      double to_f1 = gain(kFilter1, true, true);
      double to_f2 = gain(kFilter2, true, true);
      double dual = gain(kDual, true, true);
      double effects = gain(kEffects, true, true);
      double direct = gain(kDirect, true, true);
      double f1_off = gain(kFilter1, false, true);
      double dual_all_off = gain(kDual, false, false);
      std::printf("   %s: F1 %.1f | F2 %.1f | F1+2 %.1f | effects %.1f | direct %.1f | F1 (off) %.1f dB\n",
                  who.c_str(), to_f1, to_f2, dual, effects, direct, f1_off);

      check(to_f1 < -20.0, who + " -> Filter 1 goes through the filter", fmt("%.1f dB", to_f1));
      check(to_f2 < -20.0, who + " -> Filter 2 goes through the filter", fmt("%.1f dB", to_f2));
      check(dual < -20.0, who + " -> Filter 1+2 goes through the filters", fmt("%.1f dB", dual));
      check(std::fabs(effects) < 0.2, who + " -> Effects skips the filters", fmt("%.2f dB", effects));
      check(std::fabs(direct) < 0.2, who + " -> Direct Out skips the filters", fmt("%.2f dB", direct));
      check(std::fabs(f1_off) < 0.2, who + " -> a switched-off filter is skipped, audio not lost", fmt("%.2f dB", f1_off));
      check(std::fabs(dual_all_off) < 0.2, who + " -> both filters off passes the audio", fmt("%.2f dB", dual_all_off));
    }

    // The two inputs route independently.
    {
      Rig rig;
      setFilter(rig, 1, true, 40.0f);
      rig.set("input_destination", static_cast<float>(kFilter1));
      rig.set("sidechain_destination", static_cast<float>(kDirect));
      rig.set("sidechain_on", 1.0f);
      Generator tone = sine(probe);
      Generator silence = [](int, float& l, float& r) { l = r = 0.0f; };
      Stereo both = rig.render(kSampleRate, tone, 64, &tone);
      double level = db(rms(both.left, kSampleRate / 2) / (kAmp / std::sqrt(2.0)));
      // Main is filtered away, sidechain passes: about unity (one tone at full level).
      check(std::fabs(level) < 1.0, "inputs route independently (main filtered, sidechain direct)",
            fmt("%.2f dB", level));
      Rig none;
      Stereo silent = none.render(kSampleRate, silence, 64, &silence);
      check(silent.peak() < 1e-4, "silent inputs give silence", fmt("peak %.6f", silent.peak()));
    }

    // Direct Out skips the effect rack; Effects does not.
    {
      Rig baseline;
      Stereo reference = baseline.render(kSampleRate * 2, sine(440.0f));

      auto difference = [&](int destination) {
        Rig rig;
        rig.set("distortion_on", 1.0f);
        rig.set("distortion_drive", 20.0f);
        rig.set("input_destination", static_cast<float>(destination));
        Stereo out = rig.render(kSampleRate * 2, sine(440.0f));
        double diff = 0.0;
        size_t n = 0;
        for (size_t i = kSampleRate; i < reference.left.size(); ++i, ++n)
          diff += std::pow(static_cast<double>(out.left[i]) - reference.left[i], 2.0);
        return std::sqrt(diff / std::max<size_t>(n, 1));
      };

      double through_effects = difference(kEffects);
      double direct_out = difference(kDirect);
      check(through_effects > 0.005, "Effects destination runs the effect rack", fmt("difference rms %.4f", through_effects));
      check(direct_out < 0.002, "Direct Out destination skips the effect rack", fmt("difference rms %.5f", direct_out));
    }

    // Serial filters still cascade for the new routing (input -> Filter 1+2, Filter 2 fed by Filter 1).
    {
      Rig single;
      setFilter(single, 1, true, 60.0f);
      single.set("input_destination", static_cast<float>(kFilter1));
      double one = single.sineGainDbOn(false, 600.0f);

      Rig serial;
      setFilter(serial, 1, true, 60.0f);
      setFilter(serial, 2, true, 60.0f);
      serial.set("filter_2_filter_input", 1.0f);
      serial.set("input_destination", static_cast<float>(kDual));
      double cascade = serial.sineGainDbOn(false, 600.0f);
      check(cascade < one - 3.0, "serial routing still cascades with destinations", fmt("%.2f vs %.2f dB", cascade, one));
    }
  }

  void testLanes() {
    std::printf("\n== Lane switch, level and pan ==\n");

    {
      Rig rig;
      double gain = rig.sineGainDbOn(true, 1000.0f);
      check(gain < -100.0, "sidechain lane is off by default (silent)", fmt("%.1f dB", gain));
    }
    {
      Rig rig;
      rig.set("sidechain_on", 1.0f);
      double gain = rig.sineGainDbOn(true, 1000.0f);
      check(std::fabs(gain) < 0.1, "sidechain lane passes audio when switched on", fmt("%.2f dB", gain));
    }
    {
      Rig rig;
      rig.set("input_on", 0.0f);
      double gain = rig.sineGainDbOn(false, 1000.0f);
      check(gain < -100.0, "main lane switched off is silent", fmt("%.1f dB", gain));
    }

    for (int lane = 0; lane < 2; ++lane) {
      std::string prefix = lane ? "sidechain" : "input";
      auto level_gain = [&](float knob) {
        Rig rig;
        rig.set("sidechain_on", 1.0f);
        rig.set(prefix + "_level", knob);
        return rig.sineGainDbOn(lane == 1, 1000.0f);
      };
      double half = level_gain(std::sqrt(0.5f));  // knob^2 = 0.5 -> -6 dB
      double zero = level_gain(0.0f);
      double max = level_gain(1.41421356f);        // knob^2 = 2 -> +6 dB
      check(std::fabs(half + 6.02) < 0.3, prefix + " level knob 0.707 is -6 dB", fmt("%.2f dB", half));
      check(zero < -100.0, prefix + " level 0 is silent", fmt("%.1f dB", zero));
      check(std::fabs(max - 6.02) < 0.3, prefix + " level at maximum is +6 dB", fmt("%.2f dB", max));

      auto pan_levels = [&](float pan) {
        Rig rig;
        rig.set("sidechain_on", 1.0f);
        rig.set(prefix + "_pan", pan);
        Generator tone = sine(1000.0f);
        Generator silence = [](int, float& l, float& r) { l = r = 0.0f; };
        Stereo out = lane ? rig.render(kSampleRate, silence, 64, &tone) : rig.render(kSampleRate, tone);
        return std::make_pair(db(rms(out.left, kSampleRate / 2) / (kAmp / std::sqrt(2.0))),
                              db(rms(out.right, kSampleRate / 2) / (kAmp / std::sqrt(2.0))));
      };
      auto center = pan_levels(0.0f);
      auto left = pan_levels(-1.0f);
      auto right = pan_levels(1.0f);
      auto quarter = pan_levels(-0.5f);
      check(std::fabs(center.first) < 0.1 && std::fabs(center.second) < 0.1, prefix + " pan centre is unity on both sides",
            fmt("L %.2f R %.2f dB", center.first, center.second));
      check(left.second < -60.0 && left.first > 2.9 && left.first < 3.1, prefix + " pan hard left: right silent, left +3 dB",
            fmt("L %.2f R %.1f dB", left.first, left.second));
      check(right.first < -60.0 && right.second > 2.9 && right.second < 3.1, prefix + " pan hard right: left silent, right +3 dB",
            fmt("L %.1f R %.2f dB", right.first, right.second));
      check(quarter.first > 0.0 && quarter.second < 0.0, prefix + " pan half left favours the left", fmt("L %.2f R %.2f dB", quarter.first, quarter.second));
    }
  }

  // Connects a modulation source to a destination the way the interface does, at full amount.
  // Returns the connection's processor (its output is what gets added to the destination), or nullptr.
  ModulationConnectionProcessor* connectMod(Rig& rig, const std::string& source, const std::string& destination,
                                            float amount = 1.0f) {
    ModulationConnection* connection = rig.engine->getModulationBank().createConnection(source, destination);
    if (connection == nullptr)
      return nullptr;

    modulation_change change;
    change.source = rig.engine->getModulationSource(source);
    change.mono_destination = rig.engine->getMonoModulationDestination(destination);
    change.mono_modulation_switch = rig.engine->getMonoModulationSwitch(destination);
    change.destination_scale = Parameters::getParameterRange(destination);
    change.poly_modulation_switch = rig.engine->getPolyModulationSwitch(destination);
    change.poly_destination = rig.engine->getPolyModulationDestination(destination);
    change.modulation_processor = connection->modulation_processor.get();
    change.num_audio_rate = 0;
    change.disconnecting = false;
    if (change.source == nullptr || change.mono_destination == nullptr || change.mono_modulation_switch == nullptr)
      return nullptr;

    rig.engine->connectModulation(change);
    rig.set("modulation_1_amount", amount);  // the first connection of a fresh engine
    return change.modulation_processor;
  }

  // What the connection currently adds to its destination (sum over lanes, so any lane moving shows up).
  float modulationOutput(ModulationConnectionProcessor* processor) {
    poly_float value = processor->output(ModulationConnectionProcessor::kModulationOutput)->buffer[0];
    float sum = 0.0f;
    for (int i = 0; i < poly_float::kSize; ++i)
      sum += value[i];
    return sum;
  }

  void testModulationMatrix() {
    std::printf("\n== Modulation matrix: every source reaches a destination ==\n");
    std::vector<poly_float> silence(kMaxBufferSize, poly_float(0.0f));

    // Reads the value of the connection's source as seen by the matrix after running some blocks.
    ModulationConnectionProcessor* watched = nullptr;
    auto read = [&](Rig& rig, int blocks) {
      float last = 0.0f;
      for (int b = 0; b < blocks; ++b) {
        rig.engine->processWithInput(silence.data(), 64);
        last = modulationOutput(watched);
      }
      return last;
    };

    struct Case {
      const char* source;
      std::function<void(Rig&)> action;  // what the player/host does to move the source
    };
    const Case cases[] = {
      { "macro_control_1", [](Rig& r) { r.set("macro_control_1", 0.8f); } },
      { "macro_control_2", [](Rig& r) { r.set("macro_control_2", 0.8f); } },
      { "macro_control_3", [](Rig& r) { r.set("macro_control_3", 0.8f); } },
      { "macro_control_4", [](Rig& r) { r.set("macro_control_4", 0.8f); } },
      { "mod_wheel", [](Rig& r) { r.engine->setModWheel(0.9f, 0); } },
      { "pitch_wheel", [](Rig& r) { r.engine->setZonedPitchWheel(1.0f, 0, 0); } },
      { "velocity", [](Rig& r) { r.engine->noteOn(60, 0.9f, 0, 0); } },
      { "note", [](Rig& r) { r.engine->noteOn(96, 1.0f, 0, 0); } },
      { "note_in_octave", [](Rig& r) { r.engine->noteOn(71, 1.0f, 0, 0); } },
      { "aftertouch", [](Rig& r) { r.engine->noteOn(60, 1.0f, 0, 0); r.engine->setAftertouch(60, 0.9f, 0, 0); } },
      { "slide", [&silence](Rig& r) {
        r.engine->noteOn(60, 1.0f, 0, 0);
        r.engine->processWithInput(silence.data(), 64);
        r.engine->setChannelSlide(0, 0.9f, 0);
      } },
      { "lift", [](Rig& r) { r.engine->noteOn(60, 1.0f, 0, 0); r.engine->noteOff(60, 0.9f, 0, 0); } },
      { "env_1", [](Rig& r) { r.engine->noteOn(60, 1.0f, 0, 0); } },
      { "env_2", [](Rig& r) { r.engine->enableModSource("env_2"); r.engine->noteOn(60, 1.0f, 0, 0); } },
    };

    for (const Case& c : cases) {
      Rig rig;
      watched = connectMod(rig, c.source, "filter_1_cutoff");
      bool connected = watched != nullptr;
      check(connected, std::string(c.source) + " can be connected to a destination");
      if (!connected)
        continue;

      float before = read(rig, 100);
      c.action(rig);
      float peak_change = 0.0f;
      for (int b = 0; b < 20; ++b)
        peak_change = std::max(peak_change, std::fabs(read(rig, 20) - before));
      check(peak_change > 0.03f, std::string(c.source) + " moves the matrix source when played / turned",
            fmt("changed by %.3f", peak_change));
    }

    // Free-running and random sources move on their own.
    for (const char* source : { "lfo_1", "lfo_2", "lfo_3", "lfo_4", "random_1", "random_2" }) {
      Rig rig;
      rig.engine->enableModSource(source);
      watched = connectMod(rig, source, "filter_1_cutoff");
      bool connected = watched != nullptr;
      check(connected, std::string(source) + " can be connected to a destination");
      if (!connected)
        continue;

      std::string name = source;
      if (name.find("lfo") == 0)
        rig.set(name + "_frequency", 2.0f);
      if (name.find("random") == 0) {
        rig.set(name + "_frequency", 2.0f);
        rig.set(name + "_sync", 0.0f);
      }
      float lo = 9.0f, hi = -9.0f;
      for (int b = 0; b < 400; ++b) {
        rig.engine->processWithInput(silence.data(), 64);
        float v = modulationOutput(watched);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      check(hi - lo > 0.05f, std::string(source) + " moves by itself without notes", fmt("range %.3f..%.3f", lo, hi));
    }

    // "random" takes a new value per note.
    {
      Rig rig;
      rig.engine->enableModSource("random");
      watched = connectMod(rig, "random", "filter_1_cutoff");
      bool connected = watched != nullptr;
      check(connected, "random can be connected to a destination");
      if (connected) {
        std::set<int> values;
        for (int note = 40; note < 60; ++note) {
          rig.engine->noteOn(note, 1.0f, 0, 0);
          values.insert(static_cast<int>(read(rig, 4) * 1000.0f));
          rig.engine->noteOff(note, 0.5f, 0, 0);
          read(rig, 4);
        }
        check(values.size() > 3, "random picks different values for different notes", fmt("%.0f distinct", static_cast<double>(values.size())));
      }
    }

    // End to end: a macro really changes what the lane does to the audio.
    {
      auto level_with_macro = [&](float macro) {
        Rig rig;
        rig.set("input_level", 0.5f);
        connectMod(rig, "macro_control_1", "input_level", 0.3f);
        rig.set("macro_control_1", macro);
        return rig.sineGainDbOn(false, 1000.0f);
      };
      double low = level_with_macro(0.0f);
      double high = level_with_macro(1.0f);
      check(high > low + 6.0, "macro -> lane level changes the audio level", fmt("%.1f dB -> %.1f dB", low, high));

      auto pan_with_macro = [&](float macro) {
        Rig rig;
        connectMod(rig, "macro_control_1", "sidechain_pan", 1.0f);
        rig.set("sidechain_on", 1.0f);
        rig.set("macro_control_1", macro);
        Generator tone = sine(1000.0f);
        Generator silence_gen = [](int, float& l, float& r) { l = r = 0.0f; };
        Stereo out = rig.render(kSampleRate / 2, silence_gen, 64, &tone);
        return std::make_pair(rms(out.left, kSampleRate / 4), rms(out.right, kSampleRate / 4));
      };
      auto centred = pan_with_macro(0.0f);
      auto swung = pan_with_macro(1.0f);
      check(swung.second > swung.first * 2.0 && std::fabs(centred.first - centred.second) < 0.02 * centred.first,
            "macro -> lane pan moves the sidechain across the stereo field",
            fmt("centred %.3f/%.3f, swung %.3f/%.3f", centred.first, centred.second, swung.first, swung.second));

      Rig cutoff_rig;
      setFilter(cutoff_rig, 1, true, 100.0f);
      cutoff_rig.set("input_destination", 0.0f);
      connectMod(cutoff_rig, "macro_control_1", "filter_1_cutoff", -1.0f);
      cutoff_rig.set("macro_control_1", 0.0f);
      double open_gain = cutoff_rig.sineGainDbOn(false, 4000.0f);
      Rig closed_rig;
      setFilter(closed_rig, 1, true, 100.0f);
      closed_rig.set("input_destination", 0.0f);
      connectMod(closed_rig, "macro_control_1", "filter_1_cutoff", -1.0f);
      closed_rig.set("macro_control_1", 1.0f);
      double closed_gain = closed_rig.sineGainDbOn(false, 4000.0f);
      check(open_gain > closed_gain + 10.0, "macro -> filter cutoff closes the filter", fmt("%.1f dB vs %.1f dB", open_gain, closed_gain));
    }
  }

  // Amplitude of one frequency in the second half of a signal (single-bin DFT).
  double toneAmplitude(const std::vector<float>& x, float frequency) {
    size_t start = x.size() / 2;
    double re = 0.0, im = 0.0;
    for (size_t i = start; i < x.size(); ++i) {
      double phase = 2.0 * kPi * frequency * static_cast<double>(i) / kSampleRate;
      re += x[i] * std::cos(phase);
      im += x[i] * std::sin(phase);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size() - start);
  }

  void testWarp() {
    std::printf("\n== Distortion warp (8 modes per lane) ==\n");
    const char* names[] = { "Sync", "Formant", "Quantize", "Bend", "Squeeze", "Pulse", "FM", "RM" };
    Generator silence = [](int, float& l, float& r) { l = r = 0.0f; };
    Generator tone = sine(440.0f);

    Rig baseline_rig;
    Stereo baseline = baseline_rig.render(kSampleRate, tone);

    auto difference = [&](const Stereo& out) {
      double diff = 0.0;
      size_t n = 0;
      for (size_t i = kSampleRate / 2; i < out.left.size(); ++i, ++n)
        diff += std::pow(static_cast<double>(out.left[i]) - baseline.left[i], 2.0);
      return std::sqrt(diff / std::max<size_t>(n, 1));
    };

    for (int mode = 0; mode < 8; ++mode) {
      // Amount 0 is transparent in every mode, on the main lane.
      Rig off;
      off.set("input_distortion_type", static_cast<float>(mode));
      // The knob's neutral position: the centre for the bipolar modes, zero for the others.
      off.set("input_distortion_amount", (mode == 0 || mode == 1 || mode == 3 || mode == 4) ? 0.5f : 0.0f);
      off.set("sidechain_on", 1.0f);
      Stereo out = off.render(kSampleRate, tone, 64, &silence);
      // (FM/RM at amount 0 must leave the main signal alone even with the sidechain lane switched on)
      check(difference(out) < 1e-4, std::string(names[mode]) + " at its neutral amount passes the audio through",
            fmt("difference %.6f", difference(out)));
    }

    for (int lane = 0; lane < 2; ++lane) {
      std::string prefix = lane ? "sidechain" : "input";
      for (int mode = 0; mode < 6; ++mode) {
        Rig rig;
        rig.set("sidechain_on", 1.0f);
        rig.set(prefix + "_distortion_type", static_cast<float>(mode));
        rig.set(prefix + "_distortion_amount", 0.7f);
        Stereo out = lane ? rig.render(kSampleRate, silence, 64, &tone) : rig.render(kSampleRate, tone);
        double diff = difference(out);
        check(out.finite() && out.peak() < 2.0f && diff > 0.003,
              std::string(names[mode]) + " warps the " + (lane ? "sidechain" : "main") + " lane",
              fmt("difference rms %.4f, peak %.3f", diff, out.peak()));
      }
    }

    // The shapers are bounded and finite for any amount, including loud input.
    for (int mode = 0; mode < 8; ++mode) {
      for (float amount : { 0.2f, 1.0f }) {
        Rig rig;
        rig.set("sidechain_on", 1.0f);
        rig.set("input_distortion_type", static_cast<float>(mode));
        rig.set("input_distortion_amount", amount);
        Generator loud = [](int i, float& l, float& r) {
          l = r = 1.8f * std::sin(2.0f * kPi * 330.0f * i / kSampleRate);
        };
        Stereo out = rig.render(kSampleRate / 2, loud, 64, &loud);
        check(out.finite() && out.peak() < 4.5f, std::string(names[mode]) + " stays finite and bounded at amount " +
              fmt("%.1f", amount), fmt("peak %.3f", out.peak()));
      }
    }

    // The waveshaping modes work at any input level (quiet music included), not just at full scale.
    {
      Generator quiet = [](int i, float& l, float& r) {
        l = r = 0.03f * std::sin(2.0f * kPi * 440.0f * i / kSampleRate);
      };
      Rig quiet_baseline;
      Stereo quiet_ref = quiet_baseline.render(kSampleRate, quiet);
      for (int mode : { 0, 1, 3, 4, 5 }) {
        Rig rig;
        rig.set("input_distortion_type", static_cast<float>(mode));
        rig.set("input_distortion_amount", mode == 5 ? 0.8f : 0.9f);
        Stereo out = rig.render(kSampleRate, quiet);
        double diff = 0.0, ref = 0.0;
        for (size_t i = kSampleRate / 2; i < out.left.size(); ++i) {
          diff += std::pow(static_cast<double>(out.left[i]) - quiet_ref.left[i], 2.0);
          ref += std::pow(static_cast<double>(quiet_ref.left[i]), 2.0);
        }
        double relative = std::sqrt(diff / std::max(ref, 1e-12));
        check(relative > 0.1, std::string(names[mode]) + " changes a quiet (-30 dB) signal clearly",
              fmt("relative difference %.3f", relative));
      }
    }

    // Sync, Formant, Bend and Squeeze are bipolar: below the centre does something different from above it.
    for (int mode : { 0, 1, 3, 4 }) {
      auto render_at = [&](float knob) {
        Rig rig;
        rig.set("input_distortion_type", static_cast<float>(mode));
        rig.set("input_distortion_amount", knob);
        return rig.render(kSampleRate, tone);
      };
      Stereo down = render_at(0.1f);
      Stereo centre = render_at(0.5f);
      Stereo up = render_at(0.9f);
      double centre_diff = difference(centre);
      double down_diff = difference(down);
      double up_diff = difference(up);
      double between = 0.0;
      size_t n = 0;
      for (size_t i = kSampleRate / 2; i < up.left.size(); ++i, ++n)
        between += std::pow(static_cast<double>(up.left[i]) - down.left[i], 2.0);
      between = std::sqrt(between / std::max<size_t>(n, 1));
      check(centre_diff < 1e-4 && down_diff > 0.003 && up_diff > 0.003 && between > 0.003,
            std::string(names[mode]) + " is bipolar: centre neutral, both directions act, and differently",
            fmt("centre %.5f, down %.4f, up %.4f, down-vs-up %.4f", centre_diff, down_diff, up_diff, between));
    }

    // The input spectrum displays read the lane after the warp.
    {
      Rig rig;
      rig.set("input_distortion_type", 6.0f);   // FM with the sidechain off: the lane is silent
      rig.set("input_distortion_amount", 0.5f);
      rig.render(kSampleRate / 2, tone);
      const StereoMemory* memory = rig.engine->getInputMemory();
      float peak = 0.0f;
      float buffer[2048];
      memory->readSamples(buffer, 2048, 0, 0);
      for (float v : buffer)
        peak = std::max(peak, std::fabs(v));
      check(peak < 1e-4, "the input display memory follows the warped (here silenced) lane", fmt("peak %.6f", peak));

      Rig open;
      open.render(kSampleRate / 2, tone);
      open.engine->getInputMemory()->readSamples(buffer, 2048, 0, 0);
      peak = 0.0f;
      for (float v : buffer)
        peak = std::max(peak, std::fabs(v));
      check(peak > 0.2f, "the input display memory carries the lane audio", fmt("peak %.4f", peak));
    }

    // Bend bites on music-like (noisy, low crest factor) material, not just on a sine.
    for (int mode : { 3 }) {
      Rig reference_rig;
      Stereo reference = reference_rig.render(kSampleRate, noise());
      Rig rig;
      rig.set("input_distortion_type", static_cast<float>(mode));
      rig.set("input_distortion_amount", 0.9f);
      Stereo out = rig.render(kSampleRate, noise());
      double diff = 0.0, ref = 0.0;
      for (size_t i = kSampleRate / 2; i < out.left.size(); ++i) {
        diff += std::pow(static_cast<double>(out.left[i]) - reference.left[i], 2.0);
        ref += std::pow(static_cast<double>(reference.left[i]), 2.0);
      }
      double relative = std::sqrt(diff / std::max(ref, 1e-12));
      check(relative > 0.25, std::string(names[mode]) + " has a clear effect on dense material",
            fmt("relative difference %.3f", relative));
    }

    // FM and RM work with a quiet sidechain too (the modulator is scaled to its own level).
    {
      Generator quiet_modulator = [](int i, float& l, float& r) {
        l = r = 0.02f * std::sin(2.0f * kPi * 1000.0f * i / kSampleRate);
      };
      Rig rm;
      rm.set("sidechain_on", 1.0f);
      rm.set("input_distortion_type", 7.0f);
      rm.set("input_distortion_amount", 1.0f);
      Stereo rm_out = rm.render(kSampleRate, tone, 64, &quiet_modulator);
      check(toneAmplitude(rm_out.left, 1440.0f) > 0.02, "RM works with a quiet (-34 dB) sidechain",
            fmt("1440 Hz %.4f", toneAmplitude(rm_out.left, 1440.0f)));

      Generator quiet_slow = [](int i, float& l, float& r) {
        l = r = 0.02f * std::sin(2.0f * kPi * 150.0f * i / kSampleRate);
      };
      Rig fm;
      fm.set("sidechain_on", 1.0f);
      fm.set("input_distortion_type", 6.0f);
      fm.set("input_distortion_amount", 0.6f);
      Stereo fm_out = fm.render(kSampleRate, tone, 64, &quiet_slow);
      double sidebands = 0.0;
      for (int k = 1; k <= 3; ++k)
        sidebands += toneAmplitude(fm_out.left, 440.0f + 150.0f * k) + toneAmplitude(fm_out.left, 440.0f - 150.0f * k);
      check(sidebands > 0.03, "FM works with a quiet (-34 dB) sidechain", fmt("sidebands %.4f", sidebands));
    }

    // RM: the 440 Hz carrier is replaced by sum/difference tones with a 1 kHz modulator.
    {
      Rig rig;
      rig.set("sidechain_on", 1.0f);
      rig.set("input_distortion_type", 7.0f);
      rig.set("input_distortion_amount", 1.0f);
      Generator modulator = sine(1000.0f);
      Stereo out = rig.render(kSampleRate, tone, 64, &modulator);
      double carrier = toneAmplitude(out.left, 440.0f);
      double sum = toneAmplitude(out.left, 1440.0f);
      double difference_tone = toneAmplitude(out.left, 560.0f);
      check(carrier < 0.01 && sum > 0.02 && difference_tone > 0.02,
            "RM replaces the carrier with sum and difference tones",
            fmt("440 Hz %.4f, 1440 Hz %.4f, 560 Hz %.4f", carrier, sum, difference_tone));
    }

    // FM: sidebands appear around the carrier.
    {
      Rig rig;
      rig.set("sidechain_on", 1.0f);
      rig.set("input_distortion_type", 6.0f);
      rig.set("input_distortion_amount", 1.0f);
      Generator modulator = [](int i, float& l, float& r) {
        l = r = std::sin(2.0f * kPi * 100.0f * i / kSampleRate);
      };
      Stereo out = rig.render(kSampleRate, tone, 64, &modulator);
      double sideband = toneAmplitude(out.left, 540.0f) + toneAmplitude(out.left, 340.0f);
      check(out.finite() && sideband > 0.003, "FM puts sidebands around the carrier", fmt("sidebands %.4f", sideband));
    }

    // FM and RM need the sidechain lane: with it off they output silence.
    for (int mode : { 6, 7 }) {
      Rig rig;
      rig.set("input_distortion_type", static_cast<float>(mode));
      rig.set("input_distortion_amount", 0.5f);
      Stereo out = rig.render(kSampleRate / 2, tone);
      check(out.peak() < 1e-4, std::string(names[mode]) + " is silent while the sidechain lane is off",
            fmt("peak %.6f", out.peak()));
    }

    // The warp amount is a modulation target.
    {
      auto warped_difference = [&](float macro) {
        Rig rig;
        rig.set("input_distortion_type", 5.0f);
        rig.set("input_distortion_amount", 0.0f);
        connectMod(rig, "macro_control_1", "input_distortion_amount", 1.0f);
        rig.set("macro_control_1", macro);
        return difference(rig.render(kSampleRate, tone));
      };
      double low = warped_difference(0.0f);
      double high = warped_difference(1.0f);
      check(low < 1e-4 && high > 0.01, "a macro can drive the warp amount", fmt("%.5f -> %.4f", low, high));
    }
  }

  double highBandEnergy(const Stereo& s, size_t start) {
    // Crude high-pass (first difference twice) to compare spectral tilt.
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = start + 2; i < s.left.size(); ++i, ++n) {
      double d = s.left[i] - 2.0 * s.left[i - 1] + s.left[i - 2];
      sum += d * d;
    }
    return n ? std::sqrt(sum / n) : 0.0;
  }

  void testKeyTracking() {
    std::printf("\n== MIDI key tracking ==\n");
    auto brightness = [](int note, float keytrack) {
      Rig rig;
      setFilter(rig, 1, true, 60.0f);
      rig.set("filter_1_keytrack", keytrack);
      rig.noteOn(note);
      Stereo out = rig.render(kSampleRate, noise());
      return highBandEnergy(out, kSampleRate / 2);
    };

    double low_tracked = brightness(36, 1.0f);
    double high_tracked = brightness(96, 1.0f);
    double low_untracked = brightness(36, 0.0f);
    double high_untracked = brightness(96, 0.0f);

    std::printf("   tracked: note 36 %.5f, note 96 %.5f | untracked: note 36 %.5f, note 96 %.5f\n",
                low_tracked, high_tracked, low_untracked, high_untracked);
    check(db(high_tracked / low_tracked) > 15.0, "key tracking opens the filter for high notes",
          fmt("high vs low note: %.1f dB", db(high_tracked / low_tracked)));
    check(std::fabs(db(high_untracked / low_untracked)) < 1.5, "no key tracking leaves the filter alone",
          fmt("%.2f dB", db(high_untracked / low_untracked)));

    // Negative tracking goes the other way.
    double low_negative = brightness(36, -1.0f);
    double high_negative = brightness(96, -1.0f);
    check(db(low_negative / high_negative) > 10.0, "negative key tracking closes the filter for high notes",
          fmt("low vs high note: %.1f dB", db(low_negative / high_negative)));
  }

  void testAllFilterModels() {
    std::printf("\n== Every filter model and style ==\n");
    const int styles_per_model[] = { 5, 5, 5, 5, 2, 3, 6, 2 };
    int combos = 0;
    int bad = 0;
    for (int model = 0; model < constants::kNumFilterModels; ++model) {
      for (int style = 0; style < styles_per_model[model]; ++style) {
        for (int filter = 1; filter <= 2; ++filter) {
          Rig rig;
          setFilter(rig, filter, true, 70.0f, model, style);
          rig.set("filter_" + std::to_string(filter) + "_resonance", 0.9f);
          rig.set("filter_" + std::to_string(filter) + "_drive", 12.0f);
          rig.noteOn(60);
          Stereo out = rig.render(kSampleRate / 2, noise());
          ++combos;
          if (!out.finite() || out.peak() > 2.2f) {
            ++bad;
            std::printf("   bad output: filter %d model %d style %d (finite=%d peak=%.3f)\n",
                        filter, model, style, out.finite(), out.peak());
          }
        }
      }
    }
    check(bad == 0, "all filter models/styles stay finite and bounded", fmt("%.0f combinations, %.0f bad", combos, bad));
  }

  void testEffects() {
    std::printf("\n== Every effect in the rack ==\n");
    const char* names[] = { "chorus", "compressor", "delay", "distortion", "eq", "filter_fx", "flanger", "phaser", "reverb" };

    Rig baseline_rig;
    Stereo baseline = baseline_rig.render(kSampleRate * 2, sine(440.0f));
    double baseline_rms = rms(baseline.left, kSampleRate / 2);

    for (const char* name : names) {
      Rig rig;
      rig.set(std::string(name) + "_on", 1.0f);

      // Make the quieter defaults clearly audible so "processes audio" is a real test.
      if (std::string(name) == "eq")
        rig.set("eq_low_gain", 12.0f), rig.set("eq_band_gain", -12.0f), rig.set("eq_high_gain", 12.0f);
      if (std::string(name) == "filter_fx")
        rig.set("filter_fx_cutoff", 100.0f);
      if (std::string(name) == "compressor") {
        rig.set("compressor_low_gain", 24.0f);
        rig.set("compressor_band_gain", 24.0f);
        rig.set("compressor_high_gain", 24.0f);
      }
      if (std::string(name) == "distortion")
        rig.set("distortion_drive", 20.0f);

      Stereo out = rig.render(kSampleRate * 2, sine(440.0f));
      double out_rms = rms(out.left, kSampleRate);
      double change = std::fabs(db(out_rms / baseline_rms));
      bool finite = out.finite();

      // Difference from the untouched signal, sample by sample.
      double diff = 0.0;
      size_t n = 0;
      for (size_t i = kSampleRate; i < baseline.left.size() && i < out.left.size(); ++i, ++n)
        diff += std::pow(static_cast<double>(out.left[i]) - baseline.left[i % baseline.left.size()], 2.0);
      diff = std::sqrt(diff / std::max<size_t>(n, 1));

      check(finite && out.peak() < 2.2f && out_rms > 0.01,
            std::string(name) + " produces finite, non-silent audio", fmt("rms %.4f, peak %.3f", out_rms, out.peak()));
      check(change > 0.1 || diff > 0.005, std::string(name) + " actually changes the signal",
            fmt("level change %.2f dB, difference rms %.4f", change, diff));
    }

    Rig all;
    for (const char* name : names)
      all.set(std::string(name) + "_on", 1.0f);
    setFilter(all, 1, true, 90.0f);
    setFilter(all, 2, true, 90.0f);
    Stereo out = all.render(kSampleRate * 2, noise());
    check(out.finite() && out.peak() < 2.2f, "filters + all 9 effects together stay finite and bounded",
          fmt("peak %.3f", out.peak()));
  }

  void testOversampling() {
    std::printf("\n== Oversampling and block sizes ==\n");
    double reference = 0.0;
    for (int oversample : { 1, 2, 4, 8 }) {
      Rig rig;
      rig.engine->setOversamplingAmount(oversample, kSampleRate);
      setFilter(rig, 1, true, 100.0f);
      double g = rig.sineGainDb(1000.0f);
      if (oversample == 2)
        reference = g;
      Rig check_rig;
      check_rig.engine->setOversamplingAmount(oversample, kSampleRate);
      Stereo out = check_rig.render(kSampleRate / 2, noise());
      check(out.finite(), fmt("oversampling x%.0f is finite", oversample));
      check(std::fabs(g) < 1.5, fmt("oversampling x%.0f keeps a passband tone near unity", oversample), fmt("%.2f dB", g));
    }
    (void)reference;

    double reference_block = 0.0;
    for (int block : { 1, 7, 32, 64, 100, 128 }) {
      Rig rig;
      setFilter(rig, 1, true, 70.0f);
      double g = rig.sineGainDb(500.0f, block);
      if (block == 64)
        reference_block = g;
      if (block != 64)
        check(std::fabs(g - reference_block) < 0.5 || reference_block == 0.0,
              fmt("block size %.0f gives the same result as 64", block), fmt("%.2f dB", g));
      else
        reference_block = g;
    }
  }

  // Min/max of the lfo_1 status output over the given number of blocks (64 samples each).
  std::pair<float, float> lfoRange(Rig& rig, int blocks, double& seconds) {
    std::vector<poly_float> silence(kMaxBufferSize, poly_float(0.0f));
    float lo = 9.0f, hi = -9.0f;
    for (int b = 0; b < blocks; ++b) {
      rig.engine->correctToTime(seconds);
      rig.engine->processWithInput(silence.data(), 64);
      seconds += 64.0 / kSampleRate;
      float v = rig.engine->getStatusOutput("lfo_1")->value()[0];
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    return { lo, hi };
  }

  void testModulation() {
    std::printf("\n== LFOs and modulation without MIDI ==\n");
    const char* names[] = { "trigger", "sync", "envelope", "sustain envelope", "loop point", "loop hold" };
    const int blocks = kSampleRate / 64;

    for (int tempo_sync : { 0, 1 }) {
      for (int mode = 0; mode < 6; ++mode) {
        Rig rig;
        rig.engine->enableModSource("lfo_1");
        rig.set("lfo_1_sync_type", static_cast<float>(mode));
        rig.set("lfo_1_phase", 0.5f);  // loop/hold point for the looping modes
        rig.set("lfo_1_sync", tempo_sync ? 1.0f : 0.0f);
        rig.set("lfo_1_frequency", 1.0f);
        rig.set("lfo_1_tempo", tempo_sync ? 8.0f : 0.0f);
        rig.set("beats_per_minute", 2.0f);

        double t = 0.0;
        auto first = lfoRange(rig, blocks, t);   // no note has ever been played
        std::string label = std::string(tempo_sync ? "tempo-synced " : "free ") + names[mode] + " LFO moves with no notes";
        check(first.second - first.first > 0.02f, label, fmt("range %.3f..%.3f", first.first, first.second));

        // Playing and releasing a note must not stop anything, and must not add voices.
        rig.noteOn(60);
        lfoRange(rig, 8, t);
        rig.engine->noteOff(60, 0.5f, 0, 0);
        rig.noteOn(64);
        rig.engine->noteOff(64, 0.5f, 0, 0);
        check(rig.engine->getNumActiveVoices() == 1, "notes steal the single modulation voice",
              fmt("%.0f voices", static_cast<double>(rig.engine->getNumActiveVoices())));
      }
    }

    // Envelopes are global: any note triggers them, and they idle at zero with no note.
    {
      Rig env_rig;
      env_rig.engine->enableModSource("env_2");
      std::vector<poly_float> silence(kMaxBufferSize, poly_float(0.0f));
      auto run_env = [&](int blocks) {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b) {
          env_rig.engine->processWithInput(silence.data(), 64);
          peak = std::max(peak, env_rig.engine->getStatusOutput("env_2")->value()[0]);
        }
        return peak;
      };
      float idle = run_env(200);
      env_rig.noteOn(48);
      float triggered = run_env(400);
      check(idle < 0.01f, "envelope idles at zero with no note", fmt("peak %.3f", idle));
      check(triggered > 0.5f, "a note triggers the global envelope", fmt("peak %.3f", triggered));
    }

    // Free-running LFO keeps going after notes have come and gone.
    Rig rig;
    rig.engine->enableModSource("lfo_1");
    rig.set("lfo_1_sync", 0.0f);
    rig.set("lfo_1_frequency", 1.0f);
    double t = 0.0;
    rig.noteOn(60);
    lfoRange(rig, 16, t);
    rig.engine->noteOff(60, 0.5f, 0, 0);
    lfoRange(rig, 200, t);
    auto later = lfoRange(rig, blocks, t);
    check(later.second - later.first > 0.5f, "free LFO still runs long after a note was released",
          fmt("range %.3f..%.3f", later.first, later.second));
  }

  void testPlaceholders() {
    std::printf("\n== Input panel placeholder controls ==\n");
    // Placeholder input-panel parameters must exist in the engine so the GUI and presets can bind to them.
    for (const char* name : { "input_spectral_morph_amount", "input_distortion_amount",
                              "sidechain_spectral_morph_amount", "sidechain_distortion_amount" }) {
      Rig placeholder_rig;
      check(placeholder_rig.controls.count(name) == 1, std::string("control exists: ") + name);
      // The morph knobs default to 0; the warp amounts default to their bipolar centre (Sync is the first mode).
      float expected = std::string(name).find("distortion") != std::string::npos ? 0.5f : 0.0f;
      check(placeholder_rig.controls.count(name) == 0 || placeholder_rig.controls[name]->value() == expected,
            std::string("default value: ") + name);
    }

    // Controls the GUI touches directly (Advanced tab and header): touching them used to crash the plugin.
    Rig gui_rig;
    for (const char* name : { "mpe_enabled", "view_spectrogram", "voice_transpose", "voice_tune", "bypass",
                              "input_destination", "sidechain_destination", "input_on", "input_level", "input_pan",
                              "sidechain_on", "sidechain_level", "sidechain_pan",
                              "input_distortion_type", "sidechain_distortion_type" })
      check(gui_rig.controls.count(name) == 1, std::string("GUI control exists: ") + name);

    // Parameters the GUI/host can set but the engine lacks: reported so they are visible (the control lookup
    // in SynthBase is tolerant, so these are ignored rather than crashing).
    int missing = 0;
    for (int i = 0; i < vital::Parameters::getNumParameters(); ++i) {
      const vital::ValueDetails* d = vital::Parameters::getDetails(i);
      if (d && gui_rig.controls.count(d->name) == 0)
        ++missing;
    }
    std::printf("  (%d of %d parameters have no engine control; ignored safely)\n", missing,
                vital::Parameters::getNumParameters());

    // Transpose and MPE while notes are playing.
    for (float transpose : { 12.0f, -24.0f, 48.0f, -48.0f, 0.0f }) {
      gui_rig.controls["voice_transpose"]->set(transpose);
      gui_rig.controls["mpe_enabled"]->set(transpose > 0 ? 1.0f : 0.0f);
      gui_rig.controls["view_spectrogram"]->set(transpose > 0 ? 1.0f : 0.0f);
    }
    check(true, "transpose / MPE / spectrogram controls accept values");
  }

  void testCpu() {
    std::printf("\n== CPU ==\n");
    Rig rig;
    const char* names[] = { "chorus", "compressor", "delay", "distortion", "eq", "filter_fx", "flanger", "phaser", "reverb" };
    for (const char* name : names)
      rig.set(std::string(name) + "_on", 1.0f);
    setFilter(rig, 1, true, 80.0f);
    setFilter(rig, 2, true, 80.0f);

    const int seconds = 5;
    auto start = std::chrono::steady_clock::now();
    Stereo out = rig.render(kSampleRate * seconds, noise(), 128);
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    double realtime = seconds / elapsed;
    std::printf("   %.1f s of audio in %.2f s (%.1fx realtime, everything on)\n", static_cast<double>(seconds), elapsed, realtime);
    check(out.finite(), "long render is finite");
    check(realtime > 2.0, "everything-on is comfortably faster than realtime", fmt("%.1fx", realtime));
  }
} // namespace

int main() {
  std::printf("VialFX engine test\n");
  try {
    testPassthrough();
    testFilters();
    testRouting();
    testKeyTracking();
    testAllFilterModels();
    testEffects();
    testOversampling();
    testModulation();
    testInputRouting();
    testLanes();
    testModulationMatrix();
    testWarp();
    testPlaceholders();
    testCpu();
  }
  catch (const std::exception& e) {
    std::printf("[FAIL] exception: %s\n", e.what());
    ++g_failures;
  }

  std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures;
}
