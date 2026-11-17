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

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
  #include <sys/mman.h>
#endif

#include "poly_utils.h"

namespace vital {
  // Arrays that start out as zeros the operating system hands over lazily: until something writes to a page it costs
  // address space, not memory. For the big buffers of effects that may never be used. Big blocks come straight from
  // the system (a malloc that reuses freed memory would zero, and so touch, every page), small ones from calloc.
  namespace lazy_memory {
    constexpr size_t kHeader = 64;
    constexpr size_t kSystemThreshold = 128 * 1024;

    inline void* allocate(size_t bytes) {
      size_t total = bytes + kHeader;
      void* base = nullptr;
      bool system = total >= kSystemThreshold;
#if defined(_WIN32)
      if (system)
        base = VirtualAlloc(nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__unix__) || defined(__APPLE__)
      if (system) {
        base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
          base = nullptr;
      }
#else
      system = false;
#endif
      if (base == nullptr) {
        system = false;
        base = std::calloc(1, total);
        if (base == nullptr)
          return nullptr;
      }

      size_t* header = static_cast<size_t*>(base);
      header[0] = total;
      header[1] = system ? 1 : 0;
      return static_cast<char*>(base) + kHeader;
    }

    inline void release(void* memory) {
      if (memory == nullptr)
        return;

      void* base = static_cast<char*>(memory) - kHeader;
      size_t* header = static_cast<size_t*>(base);
      size_t total = header[0];
      bool system = header[1] != 0;
#if defined(_WIN32)
      if (system) {
        VirtualFree(base, 0, MEM_RELEASE);
        return;
      }
#elif defined(__unix__) || defined(__APPLE__)
      if (system) {
        munmap(base, total);
        return;
      }
#endif
      (void)total;
      std::free(base);
    }
  }

  struct LazyFree {
    void operator()(void* memory) const { lazy_memory::release(memory); }
  };
  template<class T>
  using LazyArray = std::unique_ptr<T[], LazyFree>;
  template<class T>
  LazyArray<T> makeLazyArray(size_t size) {
    return LazyArray<T>(static_cast<T*>(lazy_memory::allocate(size * sizeof(T))));
  }

  template<size_t kChannels>
  class MemoryTemplate {
    public:
      static constexpr mono_float kMinPeriod = 2.0f;
      static constexpr int kExtraInterpolationValues = 3;

      MemoryTemplate(int size) : offset_(0) {
        size_ = utils::nextPowerOfTwo(size);
        bitmask_ = size_ - 1;
        allocate();
      }

      MemoryTemplate(const MemoryTemplate& other) {
        size_ = other.size_;
        allocate();

        size_ = other.size_;
        bitmask_ = other.bitmask_;
        offset_ = other.offset_;
      }

      virtual ~MemoryTemplate() { }

      void push(poly_float sample) {
        dirty_ = true;
        offset_ = (offset_ + 1) & bitmask_;
        for (int i = 0; i < kChannels; ++i) {
          mono_float val = sample[i];
          buffers_[i][offset_] = val;
          buffers_[i][offset_ + size_] = val;
        }

        VITAL_ASSERT(utils::isFinite(sample));
      }

      void clearMemory(int num, poly_mask clear_mask) {
        int start = (offset_ - (num + kExtraInterpolationValues)) & bitmask_;
        int end = (offset_ + kExtraInterpolationValues) & bitmask_;

        for (int p = 0; p < kChannels; ++p) {
          if (clear_mask[p]) {
            mono_float* buffer = buffers_[p];
            for (int i = start; i != end; i = (i + 1) & bitmask_)
              buffer[i] = 0.0f;
            buffer[end] = 0.0f;

            for (int i = 0; i < kExtraInterpolationValues; ++i)
              buffer[size_ + i] = 0.0f;
          }
        }
      }

      void clearAll() {
        // Memory that was never written is still the zeros it was allocated with (and its pages were never touched).
        if (!dirty_)
          return;

        dirty_ = false;
        for (int c = 0; c < kChannels; ++c)
          memset(buffers_[c], 0, 2 * size_ * sizeof(mono_float));
      }

      void readSamples(mono_float* output, int num_samples, int offset, int channel) const {
        mono_float* buffer = buffers_[channel];
        int bitmask = bitmask_;
        int start_index = (offset_ - num_samples - offset) & bitmask;
        for (int i = 0; i < num_samples; ++i)
          output[i] = buffer[(i + start_index) & bitmask];
      }

      unsigned int getOffset() const { return offset_; }

      void setOffset(int offset) { offset_ = offset; }

      int getSize() const {
        return size_;
      }

      int getMaxPeriod() const {
        return size_ - kExtraInterpolationValues;
      }

    protected:
      // Zeroed by the operating system, which hands out untouched pages lazily: an effect that is never used costs
      // address space, not memory.
      void allocate() {
        for (int i = 0; i < kChannels; ++i) {
          memories_[i] = makeLazyArray<mono_float>(2 * static_cast<size_t>(size_));
          buffers_[i] = memories_[i].get();
        }
      }

      LazyArray<mono_float> memories_[poly_float::kSize];
      bool dirty_ = false;
      mono_float* buffers_[poly_float::kSize];
      unsigned int size_;
      unsigned int bitmask_;
      unsigned int offset_;
  };

  class Memory : public MemoryTemplate<poly_float::kSize> {
    public:
      Memory(int size) : MemoryTemplate(size) { }
      Memory(Memory& other) : MemoryTemplate(other) { }

      force_inline poly_float get(poly_float past) const {
        VITAL_ASSERT(poly_float::lessThan(past, kMinPeriod).sum() == 0);
        VITAL_ASSERT(poly_float::greaterThan(past, getMaxPeriod()).sum() == 0);
        poly_int past_index = utils::toInt(past);
        poly_float t = utils::toFloat(past_index) - past + 1.0f;
        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);

        poly_int indices = (poly_int(offset_) - past_index - 2) & poly_int(bitmask_);
        matrix value_matrix = utils::getValueMatrix(buffers_, indices);
        value_matrix.transpose();
        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }
  };

  class StereoMemory : public MemoryTemplate<2> {
    public:
      StereoMemory(int size) : MemoryTemplate(size) { }
      StereoMemory(StereoMemory& other) : MemoryTemplate(other) { }

      force_inline poly_float get(poly_float past) const {
        VITAL_ASSERT(poly_float::lessThan(past, 2.0f).anyMask() == 0);
        VITAL_ASSERT(poly_float::greaterThan(past, getMaxPeriod()).anyMask() == 0);
        poly_int past_index = utils::toInt(past);
        poly_float t = utils::toFloat(past_index) - past + 1.0f;
        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);

        poly_int indices = (poly_int(offset_) - past_index - 2) & poly_int(bitmask_);
        matrix value_matrix(utils::toPolyFloatFromUnaligned(buffers_[0] + indices[0]),
                            utils::toPolyFloatFromUnaligned(buffers_[1] + indices[1]), 0.0f, 0.0f);
        value_matrix.transpose();
        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }
  };
} // namespace vital

