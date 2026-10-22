# VialFX headless engine test (Linux).
#
#   make -C effects/tests            builds and runs the test
#   make -C effects/tests JUCE_HEADER_DIR=/some/dir   use a different JuceHeader.h (e.g. one without OpenGL)
#
# This links only the synthesis code, the parameter table and four JUCE modules, so it needs no GUI,
# OpenGL or audio-device libraries.

ROOT        := ../..
JUCE_HEADER_DIR ?= ../JuceLibraryCode
BUILD       := build

CXX         ?= g++
OPT         ?= -O2
CXXFLAGS    := -std=c++17 -MMD -MP $(OPT) -ffast-math -msse2 -fPIC -pthread -w \
               -DLINUX=1 -DNDEBUG=1 -DVITAL_FX=1 -DNO_AUTH=1 -DHEADLESS=1 -DJUCE_USE_XRANDR=0 \
               -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 \
               $(shell pkg-config --cflags freetype2 2>/dev/null)

INCLUDES    := -I$(JUCE_HEADER_DIR) -I../JuceLibraryCode -I$(ROOT)/third_party/JUCE/modules -I$(ROOT)/third_party \
               -I$(ROOT)/src/common -I$(ROOT)/src/common/wavetable -I$(ROOT)/src/interface/look_and_feel \
               -I$(ROOT)/src/synthesis/effects_engine -I$(ROOT)/src/synthesis/effects \
               -I$(ROOT)/src/synthesis/filters -I$(ROOT)/src/synthesis/framework \
               -I$(ROOT)/src/synthesis/lookups -I$(ROOT)/src/synthesis/modulators \
               -I$(ROOT)/src/synthesis/modules -I$(ROOT)/src/synthesis/producers \
               -I$(ROOT)/src/synthesis/utilities

JUCE_MODULES := core audio_basics data_structures events graphics dsp
JUCE_OBJECTS := $(foreach m,$(JUCE_MODULES),$(BUILD)/juce_$(m).o)

COMMON_OBJECTS := $(BUILD)/line_generator.o $(BUILD)/tuning.o $(BUILD)/synth_types.o

OBJECTS     := $(BUILD)/synthesis.o $(BUILD)/synth_parameters.o $(COMMON_OBJECTS) $(BUILD)/engine_test.o $(JUCE_OBJECTS)

.PHONY: all run clean
all: run

run: $(BUILD)/engine_test
	$(BUILD)/engine_test

$(BUILD)/engine_test: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) -pthread -ldl -lrt $(shell pkg-config --libs freetype2 2>/dev/null)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/synthesis.o: $(ROOT)/src/unity_build/synthesis.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/synth_parameters.o: $(ROOT)/src/common/synth_parameters.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -include operators.h -c $< -o $@

$(BUILD)/line_generator.o: $(ROOT)/src/common/line_generator.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -include operators.h -c $< -o $@

$(BUILD)/tuning.o: $(ROOT)/src/common/tuning.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -include operators.h -c $< -o $@

$(BUILD)/synth_types.o: $(ROOT)/src/common/synth_types.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -include operators.h -c $< -o $@

$(BUILD)/engine_test.o: engine_test.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/juce_%.o: ../JuceLibraryCode/include_juce_%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD)
