cmake_minimum_required(VERSION 3.22)
project(Vial VERSION 1.0.0)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE from root directory
add_subdirectory(JUCE)

# Create plugin target
juce_add_plugin(VialPlugin
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "buchenberg"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Vial"
    PLUGIN_MANUFACTURER_CODE Vial
    PLUGIN_CODE Vial
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
)

# Generate JUCE header
juce_generate_juce_header(VialPlugin)

# Configure plugin target
target_compile_definitions(VialPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

# Add all source files
file(GLOB_RECURSE SOURCE_FILES
    "src/common/*.cpp"
    "src/common/*.h"
    "src/headless/*.cpp"
    "src/headless/*.h"
    "src/interface/*.cpp"
    "src/interface/*.h"
    "src/plugin/*.cpp"
    "src/plugin/*.h"
    "src/standalone/*.cpp"
    "src/standalone/*.h"
    "src/synthesis/*.cpp"
    "src/synthesis/*.h"
)

# Embedded binary resources
juce_add_binary_data(VialBinaryData
    SOURCES
        fonts/DroidSansMono.ttf
        fonts/Lato-Light.ttf
        fonts/Lato-Regular.ttf
        fonts/Montserrat-Light.otf
        fonts/Montserrat-Regular.ttf
        icons/chorus.svg
        icons/compressor.svg
        icons/delay.svg
        icons/distortion.svg
        icons/drag_drop_arrows.svg
        icons/effects_filter.svg
        icons/equalizer.svg
        icons/flanger.svg
        icons/folder.svg
        icons/phaser.svg
        icons/reverb.svg
        icons/vial_ring.svg
        icons/vial_v.svg
        icons/vial_word.svg
        icons/vial_word_ring.svg
        default.vialskin
        tunings/5\ Limit.scl
        tunings/7\ Limit.scl
        tunings/Pythagorean.scl
)

target_sources(VialPlugin PRIVATE ${SOURCE_FILES})

# Set up include directories
target_include_directories(VialPlugin
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/common/wavetable
        ${CMAKE_SOURCE_DIR}/src/headless
        ${CMAKE_SOURCE_DIR}/src/interface
        ${CMAKE_SOURCE_DIR}/src/interface/editor_components
        ${CMAKE_SOURCE_DIR}/src/interface/editor_sections
        ${CMAKE_SOURCE_DIR}/src/interface/look_and_feel
        ${CMAKE_SOURCE_DIR}/src/interface/wavetable
        ${CMAKE_SOURCE_DIR}/src/interface/wavetable/editors
        ${CMAKE_SOURCE_DIR}/src/interface/wavetable/overlays
        ${CMAKE_SOURCE_DIR}/src/plugin
        ${CMAKE_SOURCE_DIR}/src/standalone
        ${CMAKE_SOURCE_DIR}/src/synthesis
        ${CMAKE_SOURCE_DIR}/src/synthesis/effects
        ${CMAKE_SOURCE_DIR}/src/synthesis/effects_engine
        ${CMAKE_SOURCE_DIR}/src/synthesis/filters
        ${CMAKE_SOURCE_DIR}/src/synthesis/framework
        ${CMAKE_SOURCE_DIR}/src/synthesis/lookups
        ${CMAKE_SOURCE_DIR}/src/synthesis/modulators
        ${CMAKE_SOURCE_DIR}/src/synthesis/modules
        ${CMAKE_SOURCE_DIR}/src/synthesis/producers
        ${CMAKE_SOURCE_DIR}/src/synthesis/synth_engine
        ${CMAKE_SOURCE_DIR}/src/synthesis/utilities
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_BINARY_DIR}/VialPlugin_artefacts/JuceLibraryCode
)

# Link JUCE modules
target_link_libraries(VialPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        VialBinaryData
)

# Add test target
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()