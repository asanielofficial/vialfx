enable_testing()

juce_add_console_app(VialTests
    PRODUCT_NAME "VialTests"
)

# Add all test source files
file(GLOB_RECURSE TEST_FILES 
    "*.cpp"
    "*.h"
)

target_sources(VialTests PRIVATE ${TEST_FILES})

target_link_libraries(VialTests
    PRIVATE
        VialPlugin
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME VialTests COMMAND VialTests)