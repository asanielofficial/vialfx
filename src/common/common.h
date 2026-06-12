#pragma once

// JUCE headers (must come before framework for JuceHeader.h)
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

// Vital/vial synthesis framework — provides mono_float, poly_float, poly_int,
// force_inline, VITAL_ASSERT, kPi, kSqrt2, and the rest of the audio constants.
#include "synthesis/framework/common.h"
#include "synthesis/framework/value.h"
#include "synthesis/framework/operators.h"
#include "synthesis/framework/processor.h"

// JUCE 8 moved OpenGL into juce::gl namespace; Vital code uses bare identifiers.
using namespace juce::gl;

// Stereo memory used across synthesis engine
#include "memory.h"

// Common type aliases
namespace vial {
    using String = juce::String;
    using var = juce::var;
    using Range = juce::Range<float>;
}