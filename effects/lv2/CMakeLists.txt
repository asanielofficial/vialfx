# VialFX LV2 build for macOS and Windows (Linux uses effects/builds/linux_vst/lv2.mk).
#
# Links the plugin's already built "Shared Code" static library with the LV2 wrapper bundled with the JUCE copy in
# third_party. The CI build workflow then runs lv2_ttl_generator on the result to write the .ttl files. Options:
#
#   -DVIALFX_SHARED_LIB=<VialFX.lib / libVialFX.a built by the normal build>
#   -DVIALFX_VERSION=<version>
cmake_minimum_required(VERSION 3.21)

if(APPLE)
  # (The plugin library itself targets 10.13; the wrapper uses C++17 library features from 10.15.)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures")
endif()

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
  # The plugin library is built with the dynamic runtime (/MD).
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "Runtime")
endif()

if(APPLE)
  project(VialFXLv2 VERSION 1.6 LANGUAGES C CXX OBJC OBJCXX)
else()
  project(VialFXLv2 VERSION 1.6 LANGUAGES C CXX)
endif()

set(VIALFX_SHARED_LIB "" CACHE FILEPATH "Built VialFX shared code library")
set(VIALFX_VERSION "1.6" CACHE STRING "Version")
foreach(required VIALFX_SHARED_LIB)
  if("${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()

get_filename_component(VIALFX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(lv2_target VialFX_lv2)
add_library(${lv2_target} MODULE
  "${VIALFX_ROOT}/third_party/JUCE/modules/juce_audio_plugin_client/LV2/juce_LV2_Wrapper.cpp")
target_link_libraries(${lv2_target} PRIVATE "${VIALFX_SHARED_LIB}")
set_target_properties(${lv2_target} PROPERTIES PREFIX "" OUTPUT_NAME VialFX)
if(APPLE)
  set_target_properties(${lv2_target} PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
  set_target_properties(${lv2_target} PROPERTIES SUFFIX ".dll")
else()
  set_target_properties(${lv2_target} PROPERTIES SUFFIX ".so")
endif()

target_include_directories(${lv2_target} PRIVATE
  "${VIALFX_ROOT}/third_party/JUCE/modules"
  "${VIALFX_ROOT}/effects/JuceLibraryCode"
)

# The plugin metadata (names, channels, LV2 settings) comes from the header, force included in every file.
if(MSVC)
  target_compile_options(${lv2_target} PRIVATE "/FI${CMAKE_CURRENT_LIST_DIR}/JucePluginDefinesLv2.h")
else()
  target_compile_options(${lv2_target} PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/JucePluginDefinesLv2.h")
endif()

target_compile_definitions(${lv2_target} PRIVATE
  JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  VITAL_FX=1
  JUCE_OPENGL3=1
  NDEBUG=1
)

if(APPLE)
  find_library(FW_COCOA Cocoa)
  find_library(FW_IOKIT IOKit)
  find_library(FW_ACCELERATE Accelerate)
  find_library(FW_COREAUDIO CoreAudio)
  find_library(FW_COREMIDI CoreMIDI)
  find_library(FW_AUDIOTOOLBOX AudioToolbox)
  find_library(FW_QUARTZCORE QuartzCore)
  find_library(FW_CARBON Carbon)
  find_library(FW_METAL Metal)
  find_library(FW_METALKIT MetalKit)
  find_library(FW_OPENGL OpenGL)
  find_library(FW_SECURITY Security)
  find_library(FW_WEBKIT WebKit)
  find_library(FW_COREAUDIOKIT CoreAudioKit)
  target_link_libraries(${lv2_target} PRIVATE
    ${FW_COCOA} ${FW_IOKIT} ${FW_ACCELERATE} ${FW_COREAUDIO} ${FW_COREMIDI} ${FW_AUDIOTOOLBOX} ${FW_QUARTZCORE}
    ${FW_CARBON} ${FW_METAL} ${FW_METALKIT} ${FW_OPENGL} ${FW_SECURITY} ${FW_WEBKIT} ${FW_COREAUDIOKIT})
  target_link_libraries(${lv2_target} PRIVATE "-framework DiscRecording" "-framework AVFoundation"
                                               "-framework CoreMedia")
  target_compile_definitions(${lv2_target} PRIVATE REQUIRE_AUTH=1 NO_AUTH=1)
  # The plugin library is built with link time optimisation.
  target_link_options(${lv2_target} PRIVATE -flto)
elseif(WIN32)
  target_compile_definitions(${lv2_target} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32 _WINDOWS JUCE_USE_CURL=0
    WINVER=0x0601 _WIN32_WINNT=0x0601 _ENABLE_EXTENDED_ALIGNED_STORAGE REQUIRE_AUTH=1 NO_AUTH=1)
  target_compile_options(${lv2_target} PRIVATE /bigobj)
  # The plugin library is built with whole program optimisation.
  target_link_options(${lv2_target} PRIVATE /LTCG)
  target_link_libraries(${lv2_target} PRIVATE
    kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid wininet version ws2_32 opengl32
    imm32 winmm rpcrt4 shlwapi dxgi d2d1 dwrite)
else()
  target_compile_definitions(${lv2_target} PRIVATE LINUX=1 _GLIBCXX_USE_CXX11_ABI=0 JUCE_USE_XRANDR=0)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VIALFX_DEPS REQUIRED IMPORTED_TARGET alsa freetype2 libcurl x11 xext xinerama xrandr xcursor)
  target_link_libraries(${lv2_target} PRIVATE PkgConfig::VIALFX_DEPS GL rt dl pthread)
  # The plugin library is built with -flto.
  target_link_options(${lv2_target} PRIVATE -flto -Wl,--no-undefined)
endif()
