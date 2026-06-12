# Plugin target
juce_add_plugin(VialPlugin
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Vial
    PLUGIN_CODE Vial
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Vial"
)

# Add source files
target_sources(VialPlugin PRIVATE
    common/synth_base.cpp
    common/synth_parameters.cpp
    plugin/synth_plugin.cpp
    plugin/synth_editor.cpp
    # Add other source files...
)

# Include directories
target_include_directories(VialPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link JUCE modules
target_link_libraries(VialPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
)