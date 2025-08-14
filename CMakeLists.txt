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
)

# Add all source files
file(GLOB_RECURSE SOURCE_FILES
    "src/common/*.cpp"
    "src/common/*.h"
    "src/plugin/*.cpp"
    "src/plugin/*.h"
    "src/synthesis/*.cpp"
    "src/synthesis/*.h"
)

target_sources(VialPlugin PRIVATE ${SOURCE_FILES})

# Set up include directories
target_include_directories(VialPlugin
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/interface
        ${CMAKE_SOURCE_DIR}/src/plugin
        ${CMAKE_SOURCE_DIR}/src/synthesis
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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Add test target
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()