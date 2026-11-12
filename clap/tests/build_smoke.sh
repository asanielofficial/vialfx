#!/bin/bash
# Local Linux build of the host smoke test (no CLAP/Visage needed): clap/tests/build_smoke.sh
set -e
cd "$(dirname "$0")/../.."
B=${BUILD:-clap/tests/build}; mkdir -p $B
INC="-Iheadless/JuceLibraryCode -Ithird_party/JUCE/modules -Ithird_party -Isrc/common -Isrc/common/wavetable -Isrc/interface/look_and_feel -Isrc/synthesis/effects_engine -Isrc/synthesis/effects -Isrc/synthesis/filters -Isrc/synthesis/framework -Isrc/synthesis/lookups -Isrc/synthesis/modulators -Isrc/synthesis/modules -Isrc/synthesis/producers -Isrc/synthesis/utilities -Iclap/src"
F="-std=c++17 -O2 -ffast-math -msse2 -fPIC -pthread -w -DLINUX=1 -DNDEBUG=1 -DVITAL_FX=1 -DNO_AUTH=1 -DHEADLESS=1 -DJUCE_USE_XRANDR=0 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 $(pkg-config --cflags freetype2 2>/dev/null)"
obj() { [ $B/$1.o -nt $2 ] || g++ $F $INC $3 -c $2 -o $B/$1.o & }
obj synthesis src/unity_build/synthesis.cpp
for f in synth_parameters line_generator tuning synth_types synth_base load_save midi_manager synth_gui_interface; do obj $f src/common/$f.cpp "-include operators.h"; done
for f in src/common/wavetable/*.cpp src/common/startup.cpp; do obj $(basename $f .cpp) $f "-include operators.h -include line_generator.h"; done
obj host clap/src/vialfx_host.cpp
obj smoke clap/tests/host_smoke.cpp
for m in core audio_basics data_structures events dsp audio_formats; do obj juce_$m headless/JuceLibraryCode/include_juce_$m.cpp; done
wait
g++ -o $B/host_smoke $B/*.o -pthread -ldl -lrt $(pkg-config --libs freetype2 2>/dev/null)
$B/host_smoke
