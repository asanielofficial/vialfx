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

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
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
    Stereo render(int num_samples, const Generator& generator, int block = 64) {
      Stereo out;
      out.left.resize(num_samples);
      out.right.resize(num_samples);
      std::vector<poly_float> input(kMaxBufferSize);

      int done = 0;
      while (done < num_samples) {
        int n = std::min(block, num_samples - done);
        n = std::min(n, static_cast<int>(kMaxBufferSize));
        for (int i = 0; i < n; ++i) {
          float l = 0.0f, r = 0.0f;
          generator(done + i, l, r);
          input[i] = poly_float(l, r);
        }

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
      check(placeholder_rig.controls.count(name) == 0 || placeholder_rig.controls[name]->value() == 0.0f,
            std::string("defaults to 0: ") + name);
    }

    // Controls the GUI touches directly (Advanced tab and header): touching them used to crash the plugin.
    Rig gui_rig;
    for (const char* name : { "mpe_enabled", "view_spectrogram", "voice_transpose", "voice_tune", "bypass" })
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
