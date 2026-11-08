# VialFX CLAP build.
#
# JUCE 6.0.5 has no CLAP support, so the plugin is wrapped with clap-juce-extensions (MIT). This file links the
# plugin's already built "Shared Code" static library (all of VialFX plus the JUCE modules) with the wrapper, which
# adds the CLAP entry points. It is driven by the CI build workflow; the options are:
#
#   -DPATH_TO_CLAP_EXTENSIONS=<clap-juce-extensions checkout with its clap-libs submodules>
#   -DVIALFX_SHARED_LIB=<VialFX.a / VialFX.lib / libVialFX.a built by the normal build>
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
  project(VialFXClap VERSION 1.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
  project(VialFXClap VERSION 1.1.0 LANGUAGES C CXX)
endif()

set(PATH_TO_CLAP_EXTENSIONS "" CACHE PATH "clap-juce-extensions checkout")
set(VIALFX_SHARED_LIB "" CACHE FILEPATH "Built VialFX shared code library")
set(VIALFX_VERSION "1.1" CACHE STRING "Version")
foreach(required PATH_TO_CLAP_EXTENSIONS VIALFX_SHARED_LIB)
  if("${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()

get_filename_component(VIALFX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_subdirectory(${PATH_TO_CLAP_EXTENSIONS} clap_ext EXCLUDE_FROM_ALL)

clap_juce_extensions_plugin_jucer(
  TARGET VialFX
  TARGET_PATH "${VIALFX_SHARED_LIB}"
  PLUGIN_BINARY_NAME VialFX
  PLUGIN_VERSION "${VIALFX_VERSION}"
  CLAP_ID "io.github.asanielofficial.vialfx"
  CLAP_FEATURES audio-effect stereo
  CLAP_MANUAL_URL "https://github.com/asanielofficial/vialfx"
  CLAP_SUPPORT_URL "https://github.com/asanielofficial/vialfx/issues"
)

set(clap_target VialFX_CLAP)

target_include_directories(${clap_target} PRIVATE
  "${VIALFX_ROOT}/third_party/JUCE/modules"
  "${VIALFX_ROOT}/effects/JuceLibraryCode"
)

# What the wrapper reads from the plugin's metadata (the same values as effects/JuceLibraryCode/JucePluginDefines.h).
target_compile_definitions(${clap_target} PRIVATE
  JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  JucePlugin_Name="VialFX"
  JucePlugin_Desc=""
  JucePlugin_Manufacturer="VialFX"
  JucePlugin_ManufacturerWebsite="https://github.com/asanielofficial/vialfx"
  JucePlugin_VersionString="${VIALFX_VERSION}"
  JucePlugin_ManufacturerCode=0x4f70656e
  JucePlugin_PluginCode=0x566c4658
  JucePlugin_VSTUniqueID=0x566c4658
  JucePlugin_IsSynth=0
  JucePlugin_WantsMidiInput=1
  JucePlugin_ProducesMidiOutput=0
  JucePlugin_EditorRequiresKeyboardFocus=1
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
  target_link_libraries(${clap_target} PRIVATE
    ${FW_COCOA} ${FW_IOKIT} ${FW_ACCELERATE} ${FW_COREAUDIO} ${FW_COREMIDI} ${FW_AUDIOTOOLBOX} ${FW_QUARTZCORE}
    ${FW_CARBON} ${FW_METAL} ${FW_METALKIT} ${FW_OPENGL} ${FW_SECURITY} ${FW_WEBKIT} ${FW_COREAUDIOKIT})
  target_link_libraries(${clap_target} PRIVATE "-framework DiscRecording" "-framework AVFoundation"
                                               "-framework CoreMedia")
  target_compile_definitions(${clap_target} PRIVATE REQUIRE_AUTH=1 NO_AUTH=1)
  # The plugin library is built with link time optimisation.
  target_link_options(${clap_target} PRIVATE -flto)
elseif(WIN32)
  target_compile_definitions(${clap_target} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32 _WINDOWS JUCE_USE_CURL=0
    WINVER=0x0601 _WIN32_WINNT=0x0601 _ENABLE_EXTENDED_ALIGNED_STORAGE REQUIRE_AUTH=1 NO_AUTH=1)
  target_compile_options(${clap_target} PRIVATE /bigobj)
  # The plugin library is built with whole program optimisation.
  target_link_options(${clap_target} PRIVATE /LTCG)
  target_link_libraries(${clap_target} PRIVATE
    kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid wininet version ws2_32 opengl32
    imm32 winmm rpcrt4 shlwapi dxgi d2d1 dwrite)
else()
  target_compile_definitions(${clap_target} PRIVATE LINUX=1 _GLIBCXX_USE_CXX11_ABI=0 JUCE_USE_XRANDR=0)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VIALFX_DEPS REQUIRED IMPORTED_TARGET alsa freetype2 libcurl x11 xext xinerama xrandr xcursor)
  target_link_libraries(${clap_target} PRIVATE PkgConfig::VIALFX_DEPS GL rt dl pthread)
  # The plugin library is built with -flto.
  target_link_options(${clap_target} PRIVATE -flto -Wl,--no-undefined)
endif()
