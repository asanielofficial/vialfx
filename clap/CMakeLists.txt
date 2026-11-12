# VialFX on Visage: CLAP plugin (built with clap-helpers) with the interface in Visage, exported to VST3 by clap-wrapper.
#
#   cmake -S clap -B clap/build -A x64          (Windows; needs Visual Studio 2022)
#   cmake --build clap/build --config Release
#
# This sits beside the Projucer builds in effects/builds, which keep working. Visage, CLAP, clap-helpers and
# clap-wrapper are downloaded at configure time (pinned commits) and are not part of this repository.
cmake_minimum_required(VERSION 3.21)
project(VialFXVisage VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif ()

get_filename_component(VIALFX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9)
FetchContent_Declare(clap_helpers
  GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
  GIT_TAG a380c978f429103e08afc1d17e9efbf3e3c96ee2)
FetchContent_Declare(visage
  GIT_REPOSITORY https://github.com/VitalAudio/visage.git
  GIT_TAG ba0679e3b2a602935f18d319ca17e76545daa6cf)
FetchContent_Declare(clap_wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c)

FetchContent_MakeAvailable(clap clap_helpers)

set(VISAGE_APPLICATION_NAME "VialFX")
set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(visage)

# clap-wrapper downloads the (current) VST3 SDK itself; the CLAP SDK is the one fetched above.
set(CLAP_SDK_ROOT "${clap_SOURCE_DIR}" CACHE STRING "" FORCE)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_WINDOWS_SINGLE_FILE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clap_wrapper)

# ---------------------------------------------------------------------------------------------------------------
# Engine: the synthesis code, the parameter table, SynthBase and the headless JUCE modules it uses (C++17).
# ---------------------------------------------------------------------------------------------------------------
set(JUCE_MODULES core audio_basics data_structures events dsp audio_formats)
set(JUCE_SOURCES)
foreach (m IN LISTS JUCE_MODULES)
  list(APPEND JUCE_SOURCES "${VIALFX_ROOT}/headless/JuceLibraryCode/include_juce_${m}.cpp")
endforeach ()

file(GLOB WAVETABLE_SOURCES "${VIALFX_ROOT}/src/common/wavetable/*.cpp")
set(ENGINE_SOURCES
  "${VIALFX_ROOT}/src/unity_build/synthesis.cpp"
  "${VIALFX_ROOT}/src/common/synth_parameters.cpp"
  "${VIALFX_ROOT}/src/common/line_generator.cpp"
  "${VIALFX_ROOT}/src/common/tuning.cpp"
  "${VIALFX_ROOT}/src/common/synth_types.cpp"
  "${VIALFX_ROOT}/src/common/synth_base.cpp"
  "${VIALFX_ROOT}/src/common/load_save.cpp"
  "${VIALFX_ROOT}/src/common/midi_manager.cpp"
  "${VIALFX_ROOT}/src/common/synth_gui_interface.cpp"
  "${VIALFX_ROOT}/src/common/startup.cpp"
  ${WAVETABLE_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/src/vialfx_host.cpp")

add_library(vialfx_engine STATIC ${ENGINE_SOURCES} ${JUCE_SOURCES})
set_target_properties(vialfx_engine PROPERTIES CXX_STANDARD 17)
target_include_directories(vialfx_engine PUBLIC
  "${VIALFX_ROOT}/headless/JuceLibraryCode"
  "${VIALFX_ROOT}/third_party/JUCE/modules"
  "${VIALFX_ROOT}/third_party"
  "${VIALFX_ROOT}/src/common"
  "${VIALFX_ROOT}/src/common/wavetable"
  "${VIALFX_ROOT}/src/interface/look_and_feel"
  "${VIALFX_ROOT}/src/synthesis/effects_engine"
  "${VIALFX_ROOT}/src/synthesis/effects"
  "${VIALFX_ROOT}/src/synthesis/filters"
  "${VIALFX_ROOT}/src/synthesis/framework"
  "${VIALFX_ROOT}/src/synthesis/lookups"
  "${VIALFX_ROOT}/src/synthesis/modulators"
  "${VIALFX_ROOT}/src/synthesis/modules"
  "${VIALFX_ROOT}/src/synthesis/producers"
  "${VIALFX_ROOT}/src/synthesis/utilities"
  "${CMAKE_CURRENT_LIST_DIR}/src")
target_compile_definitions(vialfx_engine PUBLIC
  VITAL_FX=1 NO_AUTH=1 HEADLESS=1 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
  JUCE_USE_XRANDR=0 JUCE_APP_VERSION=1.4.0 JUCE_APP_VERSION_HEX=0x10400 __SSE2__=1)
if (WIN32)
  target_compile_definitions(vialfx_engine PUBLIC
    _CRT_SECURE_NO_WARNINGS WIN32 _WINDOWS WINVER=0x0601 _WIN32_WINNT=0x0601 _ENABLE_EXTENDED_ALIGNED_STORAGE NOMINMAX)
  target_compile_options(vialfx_engine PUBLIC /bigobj /fp:fast /EHsc /wd4244 /wd4267 /wd4996 /wd4100 /wd4305)
  target_link_libraries(vialfx_engine PUBLIC ws2_32 winmm version imm32 shlwapi)
elseif (UNIX)
  target_compile_definitions(vialfx_engine PUBLIC LINUX=1)
  target_compile_options(vialfx_engine PUBLIC -ffast-math -msse2 -w)
  find_package(Threads REQUIRED)
  target_link_libraries(vialfx_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} rt)
endif ()

# The engine's own sources rely on operators.h and line_generator.h being seen first (the Projucer builds get this
# from unity builds); the JUCE modules must not get them.
if (MSVC)
  set(FORCED "/FIoperators.h" "/FIline_generator.h")
else ()
  set(FORCED "-include" "operators.h" "-include" "line_generator.h")
endif ()
foreach (src IN LISTS ENGINE_SOURCES)
  if (src MATCHES "synthesis\\.cpp$" OR src MATCHES "vialfx_host\\.cpp$")
    continue()
  endif ()
  set_property(SOURCE "${src}" APPEND PROPERTY COMPILE_OPTIONS ${FORCED})
endforeach ()

# ---------------------------------------------------------------------------------------------------------------
# Interface: Visage (C++20), isolated behind gui_bridge.h.
# ---------------------------------------------------------------------------------------------------------------
set(FONT_FILE "${visage_SOURCE_DIR}/examples/fonts/Lato-Regular.ttf")
add_embedded_resources(VialfxFontResources "vialfx_fonts.h" "resources::fonts" "${FONT_FILE}")

add_library(vialfx_gui STATIC src/gui_visage.cpp)
set_target_properties(vialfx_gui PROPERTIES CXX_STANDARD 20)
target_include_directories(vialfx_gui PRIVATE src)
target_link_libraries(vialfx_gui PUBLIC visage VialfxFontResources)

# ---------------------------------------------------------------------------------------------------------------
# The CLAP plugin (static implementation library; clap-wrapper adds the entry point and the VST3 export).
# ---------------------------------------------------------------------------------------------------------------
add_library(vialfx_clap_impl STATIC src/vialfx_clap.cpp src/vialfx_clap_entry.cpp)
set_target_properties(vialfx_clap_impl PROPERTIES CXX_STANDARD 17)
target_compile_definitions(vialfx_clap_impl PUBLIC VIALFX_VERSION="${PROJECT_VERSION}-visage-test")
target_link_libraries(vialfx_clap_impl PUBLIC vialfx_engine vialfx_gui clap clap-helpers clap-wrapper-extensions)
if (MSVC)
  target_compile_options(vialfx_clap_impl PRIVATE /EHsc)
endif ()

include("${clap_wrapper_SOURCE_DIR}/cmake/make_clapfirst.cmake")

make_clapfirst_plugins(
  TARGET_NAME vialfx_visage
  IMPL_TARGET vialfx_clap_impl
  OUTPUT_NAME "VialFX Visage"
  ENTRY_SOURCE src/vialfx_entry_export.cpp
  BUNDLE_IDENTIFIER "com.asanielofficial.vialfx.visage"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  WINDOWS_FOLDER_VST3 FALSE
  PLUGIN_FORMATS CLAP VST3
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/assets")
