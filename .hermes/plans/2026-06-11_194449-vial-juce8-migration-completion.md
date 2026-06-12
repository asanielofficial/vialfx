# Vial JUCE 8 Migration — Completion Plan

> **For Hermes:** Execute phases sequentially. Each phase ends with a build. Do NOT proceed to next phase without a clean build from the current phase. Fix cascade errors as they appear — if a file compiles clean after upstream root causes are fixed, mark it done.

**Goal:** Finish the JUCE 6.0.5 → 8.0.8 migration of the Vial synthesizer so it compiles clean with zero errors.

**Architecture:** The original Vital codebase used Projucer's unity build (all `.cpp` in one translation unit). The new CMake build compiles files separately. The bulk of errors are "missing includes" cascades — two header files (`synth_constants.h`, `line_generator.h`) that use framework types (`mono_float`, `poly_float`, `force_inline`, `kPi`, etc.) but never included the headers that define them because they relied on the unity build's shared context.

**Current state:** 797 warnings, 10 errors (cmake configure clean; build fails on `synth_parameters.cpp`). 137 error lines across 7 files.

**Tech Stack:** C++17, CMake 3.22+, JUCE 8.0.12 (submodule at `JUCE/`), AppleClang 21.0.0

---

## Phase 1: Root Cause — Add `common.h` to two type-starved headers

These two files cause 88 of 137 error lines. They use `force_inline`, `vial::mono_float`, `poly_float`, `poly_mask`, `kPi`, `kSqrt2`, `cr::Value` without including the framework. Fix them first — many downstream errors may be cascade.

### Task 1.1: Add `#include "common.h"` to `synth_constants.h`

**Objective:** Make `vial::mono_float`, `poly_float`, `poly_mask`, `kSqrt2`, `cr::Value`, `kPi` visible inside `src/common/synth_constants.h`

**File:** Modify `src/common/synth_constants.h` — add include after line 17 (`#pragma once`)

**Step 1: Edit the file**

```cpp
#pragma once

#include "common.h"
#include <string>
```

(Replace the existing `#include <string>` on line 19 with the two-line include block above `#include <string>` — or just add `#include "common.h"` before line 19.)

### Task 1.2: Add `#include "common.h"` to `line_generator.h`

**Objective:** Make `force_inline`, `vial::kPi`, `vial::mono_float`, `VITAL_ASSERT`, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` visible inside `src/common/line_generator.h`

**File:** Modify `src/common/line_generator.h` — add include after line 17 (`#pragma once`)

**Step 1: Edit the file**

```cpp
#pragma once

#include "common.h"
#include <JuceHeader.h>
#include "json/json.h"
```

(Add `#include "common.h"` before the existing `<JuceHeader.h>` on line 19.)

### Task 1.3: Rebuild and diagnose

**Step 1: Clean rebuild**

```bash
cd ~/Code/vial
rm -rf build && mkdir build && cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
make -j8 2>&1 | tee /tmp/vial_build_phase1.log
```

**Step 2: Count errors**

```bash
grep -c "error:" /tmp/vial_build_phase1.log
```

**Expected:** Error count drops significantly (from 137 to maybe 20-40). Most `synth_constants.h` and `line_generator.h` errors should be gone. Remaining errors likely in OpenGL components and `synth_slider.h`.

**Verification:** Error count < 50 means we're on the right track. If error count stays > 100, something else is wrong — stop and diagnose.

---

## Phase 2: Root Cause — Circular Include via `common.h` → `synth_slider.h`

**After deep investigation, all 48 remaining errors trace to ONE root cause.**

### The Problem

The migration added `synth_slider.h` to `common.h` (line 21). This was intended as a convenience — files including `common.h` would automatically get `SynthSlider`, `SynthSection`, etc. without separate includes. But it created **three fatal circular include chains**:

#### Chain A: `midi_manager.cpp` crash
```
midi_manager.cpp → midi_manager.h → common.h → synth_slider.h
  → synth_section.h → synth_button.h → synth_gui_interface.h
  → synth_base.h → midi_manager.h  ← #pragma once SKIPS (paused at line 20)
```
Result: `MidiManager` class never defined → `synth_base.h:43` undeclared

#### Chain B: `synth_parameters.h` paused-body crash
```
synth_parameters.h → common.h (synth_parameters.h PAUSED at line 19)
  → common.h → synth_slider.h → ... → synth_types.h → synth_parameters.h
  ← #pragma once SKIPS (body never processed → `ValueDetails` doesn't exist yet)
```
Result: `vial::ValueDetails` incomplete type in `synth_slider.h`

#### Chain C: `open_gl_component.h` crash
```
synth_parameters.h → common.h → synth_slider.h
  → open_gl_image_component.h → open_gl_component.h
  → common.h ← GUARDED (mid-processing)
  → synth_module.h → synth_types.h ← GUARDED (already seen via synth_slider.h)
  → synth_parameters.h ← GUARDED (paused, body not processed)
```
Result: `OpenGlComponent` fails to compile → 17 cascade errors in `open_gl_image_component.h` (override mismatches, `redoImage`/`image_component_`/`addListener` undeclared — all from base class `OpenGlComponent` being broken)

#### Cascade summary

| File | Errors | Root cause |
|------|--------|-----------|
| `open_gl_image_component.h` | 17 | `OpenGlComponent` base class fails (Chain C) → all overrides, members cascade-fail |
| `synth_slider.h` | 11 | `ValueDetails` incomplete — `synth_parameters.h` body paused (Chain B) |
| `synth_gui_interface.h` | 7 | `synth_base.h` fails (Chain A/B) → `SynthBase`, `WavetableCreator` undeclared |
| `synth_base.h` | 7 | `MidiManager` undeclared (Chain A) |
| `synth_button.h` | 1 | `SynthGuiInterface` never defined (Chain A cascade) |
| `open_gl_component.h` | 2 | `StatusOutput` not found (Chain C — `synth_module.h` blocked) |

### The Fix

**Remove `synth_slider.h` from `common.h`.** This is a single-line deletion.

`synth_slider.h` was only added during this migration — the original codebase never had it in `common.h`. All files that need `SynthSlider`/`SynthSection` already include them directly.

**File:** `src/common/common.h` — delete line 21: `#include "synth_slider.h"`

This breaks all three circular chains simultaneously. The `memory.h` include (line 24) is harmless — `memory.h` includes `common.h` but only needs framework types (already processed by `common.h` lines 11-14 before `memory.h` is reached).

### Task 2.1: Remove `synth_slider.h` from `common.h`

**File:** `src/common/common.h`

```cpp
// Delete this line:
#include "synth_slider.h"
```

### Task 2.2: Rebuild and verify

```bash
cd ~/Code/vial
rm -rf build && mkdir build && cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
make -j8 2>&1 | tee /tmp/vial_build_phase2.log
grep -c "error:" /tmp/vial_build_phase2.log
```

**Expected:** 0 errors (or near-zero). All 48 remaining errors should clear. If any remain, they'll be genuine JUCE 8 API mismatches in specific files (not circular include cascades).

---

## Phase 3: Warnings Triage

797 warnings. Most are sign-conversion warnings from the framework. Two actionable categories:

### Task 4.1: Suppress framework warnings (low priority)

Add to CMakeLists.txt:
```cmake
target_compile_options(VialPlugin PRIVATE
    -Wno-sign-conversion
    -Wno-unused-private-field
)
```

This eliminates ~780 of 797 warnings.

### Task 4.2: Fix project-specific warnings (optional)

Remaining warnings are in project code — address after clean build.

---

## Phase 4: Linker Phase

After clean compilation, the linker may surface new issues:
- Missing function definitions (unity build hid them)
- Duplicate symbols (templates shared across .cpp files)

### Task 5.1: Handle linker errors

Common patterns from the skill:
- Template function in anonymous namespace shared between `.cpp` files → duplicate in each `.cpp`
- Missing method definitions → add stubs or find the original definitions

---

## Phase 5: Runtime Verification

### Task 6.1: Build standalone and test launch

```bash
cd ~/Code/vial/build
make -j8
open VialPlugin_artefacts/Debug/Standalone/Vial.app
```

### Task 6.2: Test AU/VST3 plugin loading

```bash
# Build AudioPluginHost from JUCE
cd ~/Code/Juce
cmake . -B cmake-build -DJUCE_BUILD_EXTRAS=ON
cmake --build cmake-build --target AudioPluginHost
# Load Vial plugin
```

---

## Execution Strategy

**Phased approach — do NOT jump ahead:**

1. ✅ **Phase 1** — Fix two root cause files, rebuild, count errors. Stop if > 50 errors remain.
2. **Phase 2** — Fix OpenGL API breakage (likely cascade-cleared by Phase 1)
3. **Phase 3** — Fix remaining include/incomplete type issues (likely cascade-cleared)
4. **Phase 4** — Warnings cleanup
5. **Phase 5** — Linker fixes
6. **Phase 6** — Runtime verification

**Rule:** After each phase, commit with message like `fix: <phase description>`.

---

## Key Files Reference

| File | Purpose | Phase |
|------|---------|-------|
| `src/common/synth_constants.h` | Synth constants using vial types | Phase 1 |
| `src/common/line_generator.h` | Line generator using force_inline, vial types | Phase 1 |
| `src/common/common.h` | Central include hub (already includes framework + synth_slider) | Reference |
| `src/interface/editor_components/open_gl_component.h` | OpenGL base component | Phase 3 |
| `src/interface/editor_components/open_gl_image_component.h` | OpenGL image rendering | Phase 2 |
| `src/interface/editor_components/synth_slider.h` | Slider UI component | Phase 3 |
| `src/common/synth_base.h` | Synth base class | Phase 3 |
| `CMakeLists.txt` | Build configuration | Phase 4 |

---

## Risks & Open Questions

1. **Cascade assumption:** Many errors may clear after Phase 1. If they don't, the plan has contingency fixes for each category.
2. **Circular includes:** `synth_slider.h` ↔ `common.h` ↔ `synth_parameters.h` may create a loop. `#pragma once` handles re-entry but can cause incomplete types if a type is used before its definition is seen.
3. **Linker phase:** Unknown territory — the skill mentions potential issues with template duplication and missing definitions.
4. **Runtime correctness:** Compilation success ≠ plugin works. Test with AudioPluginHost.
5. **`common.h` pull of `synth_slider.h`:** This means including `common.h` pulls a huge dependency tree (all of OpenGL UI). Not ideal but matches the original unity build behavior.
