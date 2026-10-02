cmake_minimum_required(VERSION 3.22)

project(POLOOYX VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no VC++ redist needed
endif()

option(POLOOYX_BUILD_TESTS "Build the offline DSP test harness" ON)

# JUCE: use a local checkout if present, otherwise download JUCE 8.0.4 at configure time
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(POLOOYX
    COMPANY_NAME "Polooyx"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.polooyx.vocalengine"
    PLUGIN_MANUFACTURER_CODE Plxy
    PLUGIN_CODE Plx1
    PRODUCT_NAME "POLOOYX"
    DESCRIPTION "POLOOYX Vocal Engine"
    VERSION 1.1.0
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Pitch Shift
    VST3_AUTO_MANIFEST FALSE)

set(POLOOYX_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(POLOOYX PRIVATE ${POLOOYX_SOURCES})
target_include_directories(POLOOYX PRIVATE Source)

set(POLOOYX_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_compile_definitions(POLOOYX PUBLIC ${POLOOYX_DEFS})

target_link_libraries(POLOOYX
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(POLOOYX_BUILD_TESTS)
    juce_add_console_app(PolooyxTests PRODUCT_NAME "PolooyxTests")
    target_sources(PolooyxTests PRIVATE tests/TestMain.cpp ${POLOOYX_SOURCES})
    target_include_directories(PolooyxTests PRIVATE Source)
    target_compile_definitions(PolooyxTests PRIVATE ${POLOOYX_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PolooyxTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
endif()
