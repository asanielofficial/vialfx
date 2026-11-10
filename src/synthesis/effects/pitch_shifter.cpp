// VialFX - Pitch Shifter effect. Licensed under GPL-3.0-or-later.
#include "pitch_shifter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vital {

  namespace {
    constexpr float kPsPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    int nextPow2(int v) {
      int p = 1;
      while (p < v)
        p <<= 1;
      return p;
    }

    float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // In-place radix-2 FFT. Forward is e^-i, inverse is e^+i, neither is scaled.
    class Fft {
      public:
        void init(int n) {
          n_ = n;
          rev_.assign(n, 0);
          cos_.assign(n / 2, 0.0f);
          sin_.assign(n / 2, 0.0f);
          int bits = 0;
          while ((1 << bits) < n)
            ++bits;
          for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
              if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
            rev_[i] = r;
          }
          for (int i = 0; i < n / 2; ++i) {
            cos_[i] = std::cos(kTwoPi * i / n);
            sin_[i] = std::sin(kTwoPi * i / n);
          }
        }

        void run(float* re, float* im, bool inverse) const {
          for (int i = 0; i < n_; ++i) {
            int j = rev_[i];
            if (j > i) {
              std::swap(re[i], re[j]);
              std::swap(im[i], im[j]);
            }
          }
          for (int len = 2; len <= n_; len <<= 1) {
            int half = len / 2, step = n_ / len;
            for (int i = 0; i < n_; i += len) {
              for (int j = 0; j < half; ++j) {
                float wr = cos_[j * step];
                float wi = inverse ? sin_[j * step] : -sin_[j * step];
                int a = i + j, b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
              }
            }
          }
        }

        int size() const { return n_; }

      private:
        int n_ = 0;
        std::vector<int> rev_;
        std::vector<float> cos_, sin_;
    };

    // Circular buffer with fractional (cubic) reads. Delay 0 is the newest sample; keep delays >= 2.
    struct Ring {
      std::vector<float> d;
      int mask = 0;
      int w = 0;

      void init(int min_size) {
        int n = nextPow2(std::max(min_size, 16));
        d.assign(n, 0.0f);
        mask = n - 1;
        w = 0;
      }
      void clear() { std::fill(d.begin(), d.end(), 0.0f); w = 0; }
      void push(float x) { d[w] = x; w = (w + 1) & mask; }

      float read(float delay) const {
        float pos = static_cast<float>(w - 1) - delay;
        float fl = std::floor(pos);
        float f = pos - fl;
        int i0 = static_cast<int>(fl);
        float y0 = d[(i0 - 1) & mask], y1 = d[i0 & mask], y2 = d[(i0 + 1) & mask], y3 = d[(i0 + 2) & mask];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
      }
    };

    // ---------------------------------------------------------------------------------------------
    // Time-domain grain engines (delay-line pitch shifting).
    struct Grain {
      static constexpr int kMaxTaps = 4;
      Ring ring;
      float u[kMaxTaps] = { 0.0f, 0.25f, 0.5f, 0.75f };
      int taps = 2;

      void init(int size) { ring.init(size); setTaps(2); }
      void setTaps(int n) {
        taps = n;
        for (int k = 0; k < kMaxTaps; ++k)
          u[k] = static_cast<float>(k) / n;
      }
      void clear() { ring.clear(); setTaps(taps); }

      // ratio > 1 raises pitch. Each tap's delay slides by (ratio - 1) samples per sample.
      float processGranular(float x, float ratio, float grain, int num_taps) {
        if (num_taps != taps)
          setTaps(num_taps);
        ring.push(x);
        float inc = (ratio - 1.0f) / grain;
        float sum = 0.0f;
        for (int k = 0; k < taps; ++k) {
          float uk = u[k];
          float win = 0.5f - 0.5f * std::cos(kTwoPi * uk);
          sum += win * ring.read(uk * grain + 2.0f);
          uk -= inc;
          uk -= std::floor(uk);
          u[k] = uk;
        }
        return sum * (2.0f / taps);
      }

      // Sampler-style: one read head, short crossfade where it jumps back or forward by one grain.
      float processRetro(float x, float ratio, float grain) {
        ring.push(x);
        float u0 = u[0];
        float fade = clampf(clampf(0.1f * grain, 32.0f, 2048.0f) / grain, 0.02f, 0.5f);
        float out = ring.read(u0 * grain + 2.0f);
        if (ratio > 1.0f && u0 < fade) {
          float w = u0 / fade;
          w = w * w * (3.0f - 2.0f * w);
          out = w * out + (1.0f - w) * ring.read((u0 + 1.0f) * grain + 2.0f);
        }
        else if (ratio < 1.0f && u0 > 1.0f - fade) {
          float w = (1.0f - u0) / fade;
          w = w * w * (3.0f - 2.0f * w);
          out = w * out + (1.0f - w) * ring.read((u0 - 1.0f) * grain + 2.0f);
        }
        u0 -= (ratio - 1.0f) / grain;
        u0 -= std::floor(u0);
        u[0] = u0;
        return out;
      }
    };

    // Second-order Butterworth section; two in series make a Linkwitz-Riley 4th order slope.
    struct Biquad {
      float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
      void setup(float freq, float sample_rate, bool high) {
        float w0 = kTwoPi * clampf(freq, 20.0f, sample_rate * 0.45f) / sample_rate;
        float c = std::cos(w0), alpha = std::sin(w0) / (2.0f * 0.70710678f);
        float a0 = 1.0f + alpha;
        if (high) {
          b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0;
        }
        else {
          b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0;
        }
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
      }
      float tick(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
      }
      void clear() { z1 = z2 = 0.0f; }
    };

    struct Lr4 {
      Biquad a, b;
      void setup(float freq, float sample_rate, bool high) { a.setup(freq, sample_rate, high); b.setup(freq, sample_rate, high); }
      float tick(float x) { return b.tick(a.tick(x)); }
      void clear() { a.clear(); b.clear(); }
    };

    // ---------------------------------------------------------------------------------------------
    // Phase vocoder pitch shifter.
    struct Spectral {
      int n = 0, hop = 0, half = 0, overlap = 4;
      Fft fft;
      std::vector<float> win, in, ola, re, im, prev_phase, syn_phase, mag, prev_mag, true_bin, ana_phase, tmp_phase;
      std::vector<int> peak;
      std::vector<char> out_peak;
      float scale = 1.0f, flux_avg = 0.0f;
      int in_pos = 0, out_pos = 0, count = 0;

      void init(int size) {
        n = size;
        half = n / 2;
        fft.init(n);
        win.assign(n, 0.0f);
        for (int i = 0; i < n; ++i)
          win[i] = 0.5f - 0.5f * std::cos(kTwoPi * i / n);
        in.assign(n, 0.0f);
        ola.assign(n, 0.0f);
        re.assign(n, 0.0f);
        im.assign(n, 0.0f);
        prev_phase.assign(half + 1, 0.0f);
        syn_phase.assign(half + 1, 0.0f);
        mag.assign(half + 1, 0.0f);
        prev_mag.assign(half + 1, 0.0f);
        true_bin.assign(half + 1, 0.0f);
        ana_phase.assign(half + 1, 0.0f);
        tmp_phase.assign(half + 1, 0.0f);
        peak.assign(half + 1, 0);
        out_peak.assign(half + 1, 0);
        configure(4);
      }

      void configure(int ov) {
        overlap = ov;
        hop = n / ov;
        float acc = 0.0f;
        for (int m = 0; m < ov; ++m)
          acc += win[m * hop] * win[m * hop];
        scale = 1.0f / (acc * n);
        clear();
      }

      void clear() {
        std::fill(in.begin(), in.end(), 0.0f);
        std::fill(ola.begin(), ola.end(), 0.0f);
        std::fill(prev_phase.begin(), prev_phase.end(), 0.0f);
        std::fill(syn_phase.begin(), syn_phase.end(), 0.0f);
        std::fill(prev_mag.begin(), prev_mag.end(), 0.0f);
        in_pos = out_pos = count = 0;
        flux_avg = 0.0f;
      }

      float process(float x, float ratio, bool transients) {
        in[in_pos] = x;
        in_pos = (in_pos + 1) & (n - 1);
        if (++count >= hop) {
          count = 0;
          frame(ratio, transients);
        }
        float out = ola[out_pos];
        ola[out_pos] = 0.0f;
        out_pos = (out_pos + 1) & (n - 1);
        return out;
      }

      void frame(float ratio, bool transients) {
        const int mask = n - 1;
        for (int i = 0; i < n; ++i) {
          re[i] = in[(in_pos + i) & mask] * win[i];
          im[i] = 0.0f;
        }
        fft.run(re.data(), im.data(), false);

        float flux_sum = 0.0f, mag_sum = 0.0f;
        const float expected = kTwoPi * hop / n;
        for (int k = 0; k <= half; ++k) {
          float m = std::sqrt(re[k] * re[k] + im[k] * im[k]);
          float ph = std::atan2(im[k], re[k]);
          float dp = ph - prev_phase[k] - expected * k;
          dp -= kTwoPi * std::round(dp / kTwoPi);
          true_bin[k] = k + dp / expected;
          mag[k] = m;
          ana_phase[k] = ph;
          prev_phase[k] = ph;
          flux_sum += std::max(0.0f, m - prev_mag[k]);
          mag_sum += m;
          prev_mag[k] = m;
        }
        float flux = flux_sum / (mag_sum + 1.0e-9f);
        bool transient = transients && flux > 0.35f && flux > 2.5f * flux_avg;
        flux_avg = 0.9f * flux_avg + 0.1f * flux;

        // Peak locking: every analysis bin follows the spectral peak whose lobe it belongs to, so a
        // sinusoid keeps its window-lobe phase structure after being moved to another place.
        for (int k = 0; k <= half; ++k) {
          int pk = k;
          for (int step = 0; step < 6; ++step) {
            if (pk > 0 && mag[pk - 1] > mag[pk]) --pk;
            else if (pk < half && mag[pk + 1] > mag[pk]) ++pk;
            else break;
          }
          peak[k] = pk;
          out_peak[k] = 0;
        }
        for (int p = 0; p <= half; ++p) {
          if (peak[p] != p)
            continue;
          int kp = static_cast<int>(std::lround(p * ratio));
          if (kp < 0 || kp > half)
            continue;
          if (transient)
            syn_phase[kp] = ana_phase[p];
          else
            syn_phase[kp] += expected * true_bin[p] * ratio;
          syn_phase[kp] = std::fmod(syn_phase[kp], kTwoPi);
          tmp_phase[kp] = syn_phase[kp];
          out_peak[kp] = 1;
        }
        for (int k = 0; k <= half; ++k) {
          int src = static_cast<int>(std::lround(k / ratio));
          float m = 0.0f, ph = 0.0f;
          if (src <= half) {
            int p = peak[src];
            int kp = static_cast<int>(std::lround(p * ratio));
            if (kp >= 0 && kp <= half && out_peak[kp]) {
              m = mag[src];
              ph = tmp_phase[kp] + (ana_phase[src] - ana_phase[p]);
            }
            if (!out_peak[k])
              syn_phase[k] = std::fmod(ph, kTwoPi);
          }
          re[k] = m * std::cos(ph);
          im[k] = m * std::sin(ph);
        }
        im[0] = 0.0f;
        im[half] = 0.0f;
        for (int k = 1; k < half; ++k) {
          re[n - k] = re[k];
          im[n - k] = -im[k];
        }
        fft.run(re.data(), im.data(), true);
        for (int i = 0; i < n; ++i)
          ola[(out_pos + i) & mask] += re[i] * win[i] * scale;
      }
    };

    // ---------------------------------------------------------------------------------------------
    // Formant stage. Compares the spectral envelope of the shifted signal with that of the (aligned)
    // original and multiplies the difference back in, optionally moving the original envelope first.
    struct Formant {
      int n = 1024, hop = 256, half = 512, lifter = 44;
      Fft fft;
      std::vector<float> win, yin, rin, ola, re, im, yre, yim, cre, cim, ly, lr, gain;
      float scale = 1.0f;
      int in_pos = 0, out_pos = 0, count = 0;

      void init(int size, int lifter_bins) {
        n = size;
        hop = n / 4;
        half = n / 2;
        lifter = std::max(6, lifter_bins);
        fft.init(n);
        win.assign(n, 0.0f);
        for (int i = 0; i < n; ++i)
          win[i] = 0.5f - 0.5f * std::cos(kTwoPi * i / n);
        yin.assign(n, 0.0f); rin.assign(n, 0.0f); ola.assign(n, 0.0f);
        re.assign(n, 0.0f); im.assign(n, 0.0f); cre.assign(n, 0.0f); cim.assign(n, 0.0f);
        yre.assign(half + 1, 0.0f); yim.assign(half + 1, 0.0f);
        ly.assign(half + 1, 0.0f); lr.assign(half + 1, 0.0f); gain.assign(half + 1, 1.0f);
        float acc = 0.0f;
        for (int m = 0; m < 4; ++m)
          acc += win[m * hop] * win[m * hop];
        scale = 1.0f / (acc * n);
        clear();
      }

      void clear() {
        std::fill(yin.begin(), yin.end(), 0.0f);
        std::fill(rin.begin(), rin.end(), 0.0f);
        std::fill(ola.begin(), ola.end(), 0.0f);
        in_pos = out_pos = count = 0;
      }

      float process(float y, float ref, float ratio) {
        yin[in_pos] = y;
        rin[in_pos] = ref;
        in_pos = (in_pos + 1) & (n - 1);
        if (++count >= hop) {
          count = 0;
          frame(ratio);
        }
        float out = ola[out_pos];
        ola[out_pos] = 0.0f;
        out_pos = (out_pos + 1) & (n - 1);
        return out;
      }

      void frame(float ratio) {
        const int mask = n - 1;
        for (int i = 0; i < n; ++i) {
          re[i] = yin[(in_pos + i) & mask] * win[i];
          im[i] = rin[(in_pos + i) & mask] * win[i];
        }
        fft.run(re.data(), im.data(), false);

        // Split the two real signals packed into one complex FFT.
        for (int k = 0; k <= half; ++k) {
          int j = (n - k) & mask;
          yre[k] = 0.5f * (re[k] + re[j]);
          yim[k] = 0.5f * (im[k] - im[j]);
          float rre = 0.5f * (im[k] + im[j]);
          float rim = -0.5f * (re[k] - re[j]);
          ly[k] = std::log(std::sqrt(yre[k] * yre[k] + yim[k] * yim[k]) + 1.0e-7f);
          lr[k] = std::log(std::sqrt(rre * rre + rim * rim) + 1.0e-7f);
        }

        // Cepstral smoothing of both log spectra (real in cre, imaginary in cim).
        for (int k = 0; k <= half; ++k) {
          cre[k] = ly[k];
          cim[k] = lr[k];
          if (k > 0 && k < half) {
            cre[n - k] = ly[k];
            cim[n - k] = lr[k];
          }
        }
        fft.run(cre.data(), cim.data(), true);
        const float taper_end = 1.5f * lifter;
        for (int i = 0; i < n; ++i) {
          int q = std::min(i, n - i);
          float w = q <= lifter ? 1.0f : (q >= taper_end ? 0.0f : 0.5f + 0.5f * std::cos(kPsPi * (q - lifter) / (taper_end - lifter)));
          cre[i] *= w / n;
          cim[i] *= w / n;
        }
        fft.run(cre.data(), cim.data(), false);
        for (int k = 0; k <= half; ++k) {
          ly[k] = cre[k];
          lr[k] = cim[k];
        }

        for (int k = 0; k <= half; ++k) {
          float pos = clampf(k / ratio, 0.0f, static_cast<float>(half));
          int i0 = std::min(half - 1, static_cast<int>(pos));
          float f = pos - i0;
          float target = lr[i0] * (1.0f - f) + lr[i0 + 1] * f;
          gain[k] = clampf(std::exp(target - ly[k]), 0.25f, 4.0f);
        }

        for (int k = 0; k <= half; ++k) {
          re[k] = yre[k] * gain[k];
          im[k] = yim[k] * gain[k];
        }
        im[0] = 0.0f;
        im[half] = 0.0f;
        for (int k = 1; k < half; ++k) {
          re[n - k] = re[k];
          im[n - k] = -im[k];
        }
        fft.run(re.data(), im.data(), true);
        for (int i = 0; i < n; ++i)
          ola[(out_pos + i) & mask] += re[i] * win[i] * scale;
      }
    };
  } // namespace

  // -----------------------------------------------------------------------------------------------
  class PitchShifter::Channel {
    public:
      static constexpr int kNumResolutions = 3;
      static constexpr int kResolutionSizes[kNumResolutions] = { 1024, 2048, 4096 };

      void allocate(int sample_rate) {
        sr = static_cast<float>(sample_rate);
        int grain_ring = nextPow2(static_cast<int>(sr * 1.1f) + 64);
        for (Grain& g : grain)
          g.init(grain_ring);
        for (int i = 0; i < kNumResolutions; ++i)
          spectral[i].init(kResolutionSizes[i]);
        formant.init(sample_rate >= 88200 ? 2048 : 1024, static_cast<int>(sr / 1000.0f));
        int line = nextPow2(static_cast<int>(sr * 0.5f) + 16384);
        ref.init(line);
        dry.init(line);
        float f1 = 200.0f, f2 = 2000.0f;
        lo1.setup(f1, sr, false); hi1.setup(f1, sr, true);
        lo2.setup(f2, sr, false); hi2.setup(f2, sr, true);
        hardReset();
        ref_delay = dry_delay = 0.0f;
      }

      void hardReset() {
        for (Grain& g : grain)
          g.clear();
        for (Spectral& s : spectral)
          s.clear();
        formant.clear();
        ref.clear();
        dry.clear();
        lo1.clear(); hi1.clear(); lo2.clear(); hi2.clear();
        last_mode = -1;
      }

      float process(float x, int mode, int taps, int res_index, float ratio, float neutral, float grain_samples,
                    float formant_ratio, bool formant_on, float mix) {
        const float max_grain = sr * 0.4f;
        float g = clampf(grain_samples, 32.0f, max_grain);

        if (mode != last_mode || (mode == PitchShifter::kSpectral && (res_index != last_res || taps != last_taps))) {
          if (mode == PitchShifter::kSpectral) {
            spectral[res_index].configure(taps >= 4 ? 8 : 4);
            last_res = res_index;
          }
          last_mode = mode;
          last_taps = taps;
        }

        float wet = 0.0f, engine_delay = 0.0f, ref_signal = 0.0f;
        bool use_formant = true;

        // Reference tap: the input, delayed by about as much as the engine delays its output.
        switch (mode) {
          case PitchShifter::kGranular:
            engine_delay = 0.5f * g;
            break;
          case PitchShifter::kMultibandGranular:
            engine_delay = 0.5f * g;
            break;
          case PitchShifter::kRetro:
            engine_delay = 0.5f * clampf(g * (formant_on ? formant_ratio : 1.0f), 32.0f, max_grain);
            use_formant = false;
            break;
          default:
            engine_delay = static_cast<float>(kResolutionSizes[res_index]);
            break;
        }
        ref_delay += clampf(engine_delay - ref_delay, -0.05f, 0.05f);
        ref.push(x);
        ref_signal = ref.read(std::max(2.0f, ref_delay));

        switch (mode) {
          case PitchShifter::kGranular: {
            float shifted = grain[0].processGranular(x, ratio, g, taps);
            wet = ref_signal + neutral * (shifted - ref_signal);
            break;
          }
          case PitchShifter::kMultibandGranular: {
            float low = lo1.tick(x);
            float rest = hi1.tick(x);
            float mid = lo2.tick(rest);
            float high = hi2.tick(rest);
            float shifted = grain[0].processGranular(low, ratio, clampf(g * 2.0f, 32.0f, max_grain), taps) +
                            grain[1].processGranular(mid, ratio, g, taps) +
                            grain[2].processGranular(high, ratio, clampf(g * 0.5f, 32.0f, max_grain), taps);
            wet = ref_signal + neutral * (shifted - ref_signal);
            break;
          }
          case PitchShifter::kRetro: {
            // Formant control sets the loop length: smaller formant = shorter grains.
            float len = clampf(g * (formant_on ? formant_ratio : 1.0f), 32.0f, max_grain);
            wet = grain[0].processRetro(x, ratio, len);
            break;
          }
          default: {
            wet = spectral[res_index].process(x, ratio, taps > 0);
            break;
          }
        }

        float total_delay = engine_delay;
        if (use_formant) {
          wet = formant.process(wet, ref_signal, formant_on ? formant_ratio : 1.0f);
          total_delay += static_cast<float>(formant.n);
        }

        dry.push(x);
        dry_delay += clampf(total_delay - dry_delay, -0.05f, 0.05f);
        float dry_signal = dry.read(std::max(2.0f, dry_delay));
        return dry_signal + mix * (wet - dry_signal);
      }

    private:
      float sr = 44100.0f;
      Grain grain[3];
      Spectral spectral[kNumResolutions];
      Formant formant;
      Ring ref, dry;
      Lr4 lo1, hi1, lo2, hi2;
      float ref_delay = 0.0f, dry_delay = 0.0f;
      int last_mode = -1, last_res = -1, last_taps = -1;
  };

  constexpr int PitchShifter::Channel::kResolutionSizes[PitchShifter::Channel::kNumResolutions];

  // -----------------------------------------------------------------------------------------------
  PitchShifter::PitchShifter() : Processor(kNumInputs, kNumOutputs), allocated_rate_(0), ready_(false),
                                 mix_(1.0f), neutral_(0.0f), ratio_(1.0f), formant_ratio_(1.0f) {
    allocate(44100);
  }

  PitchShifter::~PitchShifter() { }

  void PitchShifter::allocate(int sample_rate) {
    if (ready_ && allocated_rate_ == sample_rate)
      return;
    for (auto& channel : channels_) {
      channel = std::make_unique<Channel>();
      channel->allocate(sample_rate);
    }
    allocated_rate_ = sample_rate;
    ready_ = true;
  }

  void PitchShifter::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    allocate(sample_rate);
  }

  void PitchShifter::hardReset() {
    if (!ready_)
      return;
    for (auto& channel : channels_)
      channel->hardReset();
  }

  void PitchShifter::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void PitchShifter::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;
    if (!ready_) {
      for (int i = 0; i < num_samples; ++i)
        audio_out[i] = audio_in[i];
      return;
    }

    int mode = clampf(std::round(input(kMode)->at(0)[0]), 0.0f, 3.0f);
    int compensation = clampf(std::round(input(kCompensation)->at(0)[0]), 0.0f, 2.0f);
    float semitones = clampf(input(kPitch)->at(0)[0], -24.0f, 24.0f);
    float grain_ms = clampf(input(kGrain)->at(0)[0], 5.0f, 200.0f);
    float resolution = clampf(input(kResolution)->at(0)[0], 0.0f, 1.0f);
    bool formant_on = input(kFormantControl)->at(0)[0] >= 0.5f;
    float formant_semitones = clampf(input(kFormant)->at(0)[0], -24.0f, 24.0f);
    float target_mix = clampf(input(kDryWet)->at(0)[0], 0.0f, 1.0f);

    float target_ratio = std::pow(2.0f, semitones / 12.0f);
    float target_formant = std::pow(2.0f, formant_semitones / 12.0f);
    float target_neutral = clampf(std::fabs(semitones) / 0.1f, 0.0f, 1.0f);

    int taps = compensation == 0 ? 2 : (compensation == 1 ? 3 : 4);
    int res_index = resolution < 0.34f ? 0 : (resolution < 0.67f ? 1 : 2);
    // The spectral engine reuses "taps" to say how much overlap and transient handling to use:
    // off = 4x overlap without transient resets, low = 4x with resets, high = 8x with resets.
    int spectral_flag = compensation == 0 ? 0 : (compensation == 1 ? 1 : 4);
    float grain_samples = grain_ms * 0.001f * static_cast<float>(getSampleRate());

    const float smooth = 1.0f - std::exp(-1.0f / (0.02f * getSampleRate()));
    for (int i = 0; i < num_samples; ++i) {
      ratio_ += (target_ratio - ratio_) * smooth;
      formant_ratio_ += (target_formant - formant_ratio_) * smooth;
      neutral_ += (target_neutral - neutral_) * smooth;
      mix_ += (target_mix - mix_) * smooth;

      float in_l = audio_in[i][0], in_r = audio_in[i][1];
      int shared = mode == kSpectral ? spectral_flag : taps;
      float l = channels_[0]->process(in_l, mode, shared, res_index, ratio_, neutral_, grain_samples,
                                      formant_ratio_, formant_on, mix_);
      float r = channels_[1]->process(in_r, mode, shared, res_index, ratio_, neutral_, grain_samples,
                                      formant_ratio_, formant_on, mix_);
      audio_out[i] = poly_float(l, r, 0.0f, 0.0f);
    }
  }
} // namespace vital
