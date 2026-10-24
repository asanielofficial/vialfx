# GUI smoke test: hosts the built VialFX.vst3 and drives its editor (run under xvfb).
#
#   make -C effects/tests/gui_smoke                    builds the test app
#   xvfb-run -a build/gui_smoke ../../builds/linux_vst/build/VialFX.vst3

LIB      := ../../JuceLibraryCode
JUCE     := ../../../third_party/JUCE/modules
BUILD    := build
CXX      ?= g++

CXXFLAGS := -std=c++17 -O0 -g -MMD -MP -pthread -w -DLINUX=1 -DNDEBUG=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 \
            -DJUCE_USE_XRANDR=0 -DJUCE_PLUGINHOST_VST3=1 -DJUCE_STANDALONE_APPLICATION=1 \
            -DJUCE_DSP_USE_SHARED_FFTW=0 $(shell pkg-config --cflags freetype2 2>/dev/null)
INCLUDES := -I$(LIB) -I$(JUCE) -I$(JUCE)/juce_audio_processors/format_types/VST3_SDK

MODULES  := core events data_structures graphics gui_basics audio_basics audio_processors
OBJECTS  := $(foreach m,$(MODULES),$(BUILD)/juce_$(m).o) $(BUILD)/gui_smoke.o

all: $(BUILD)/gui_smoke

$(BUILD)/gui_smoke: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) -pthread -ldl -lrt -lX11 -lXext -lXinerama -lXcursor $(shell pkg-config --libs freetype2)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/juce_%.o: $(LIB)/include_juce_%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/gui_smoke.o: gui_smoke.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD)
