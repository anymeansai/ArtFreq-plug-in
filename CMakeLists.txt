cmake_minimum_required(VERSION 3.22)

# Must be set before project(): the oldest macOS the plug-in will load on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(FreqArt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FREQART_BUILD_TESTS "Build the test runner" ON)
option(FREQART_BUILD_TOOLS "Build the headless UI snapshot tool" ON)
option(FREQART_COPY_PLUGIN_AFTER_BUILD "Copy the built plug-in into the user's VST3 folder" OFF)
option(FREQART_ENABLE_LTO "Enable link-time optimisation in Release builds" ON)

# ---------------------------------------------------------------------------
# JUCE (pinned). Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to
# skip the download.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# core: the drawing (document, text, images, compositing, the frequency scale) and the
#       plan that turns it into sound. No audio-thread code, no UI widgets.
# dsp:  everything the audio thread runs, plus the offline WAV export and the live analyser.
# ui:   the editor.
file(GLOB_RECURSE FREQART_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB_RECURSE FREQART_DSP_SOURCES  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp")
file(GLOB_RECURSE FREQART_UI_SOURCES   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
file(GLOB         FREQART_FONT_ASSETS  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")

set(FREQART_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.cpp)

set(FREQART_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    FREQART_VERSION_STRING="${PROJECT_VERSION}")

juce_add_binary_data(FreqArtAssets
    HEADER_NAME FreqArtAssets.h
    NAMESPACE   FreqArtAssets
    SOURCES     ${FREQART_FONT_ASSETS})

add_library(FreqArtBuildFlags INTERFACE)
target_link_libraries(FreqArtBuildFlags INTERFACE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(FREQART_ENABLE_LTO)
    target_link_libraries(FreqArtBuildFlags INTERFACE juce::juce_recommended_lto_flags)
endif()

set(FREQART_JUCE_MODULES
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra)

# ---------------------------------------------------------------------------
# The plug-in: an instrument. It makes the sound whose spectrogram is the drawing.
# ---------------------------------------------------------------------------
juce_add_plugin(FreqArt
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "AnyMeans"
    COMPANY_COPYRIGHT           "AnyMeans"
    BUNDLE_ID                   "com.anymeans.freqart"
    PLUGIN_MANUFACTURER_CODE    Anym
    PLUGIN_CODE                 FqAr
    FORMATS                     VST3
    PRODUCT_NAME                "FreqArt"
    DESCRIPTION                 "Draw text, images and brush strokes into the spectrogram"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Generator
    COPY_PLUGIN_AFTER_BUILD     ${FREQART_COPY_PLUGIN_AFTER_BUILD})

target_sources(FreqArt PRIVATE
    ${FREQART_PLUGIN_SOURCES}
    ${FREQART_CORE_SOURCES}
    ${FREQART_DSP_SOURCES}
    ${FREQART_UI_SOURCES})

target_include_directories(FreqArt PRIVATE src)
target_compile_definitions(FreqArt PUBLIC ${FREQART_COMMON_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(FreqArt
    PRIVATE
        FreqArtAssets
        ${FREQART_JUCE_MODULES}
    PUBLIC
        FreqArtBuildFlags)

# Definitions the plug-in wrapper normally provides, for the console apps that compile the
# processor and editor without it.
set(FREQART_STANDALONE_DEFINITIONS
    FREQART_NO_PLUGIN_CLIENT=1
    JucePlugin_Name="FreqArt"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=1
    JucePlugin_VersionString="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Tests: the drawing, the plan, the engine, the processor (no host).
# ---------------------------------------------------------------------------
if(FREQART_BUILD_TESTS)
    file(GLOB FREQART_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

    juce_add_console_app(FreqArtTests PRODUCT_NAME "FreqArtTests")
    target_sources(FreqArtTests PRIVATE
        ${FREQART_TEST_SOURCES}
        ${FREQART_PLUGIN_SOURCES}
        ${FREQART_CORE_SOURCES}
        ${FREQART_DSP_SOURCES}
        ${FREQART_UI_SOURCES})
    target_include_directories(FreqArtTests PRIVATE src tests)
    target_compile_definitions(FreqArtTests PRIVATE
        ${FREQART_COMMON_DEFINITIONS}
        ${FREQART_STANDALONE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1
        FREQART_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(FreqArtTests
        PRIVATE
            FreqArtAssets
            ${FREQART_JUCE_MODULES}
        PUBLIC
            FreqArtBuildFlags)

    enable_testing()
    add_test(NAME FreqArtTests COMMAND FreqArtTests)
endif()

# ---------------------------------------------------------------------------
# UI snapshot tool: renders the editor to PNG files without a host.
# ---------------------------------------------------------------------------
if(FREQART_BUILD_TOOLS)
    juce_add_console_app(FreqArtSnapshot PRODUCT_NAME "FreqArtSnapshot")
    target_sources(FreqArtSnapshot PRIVATE
        tools/Snapshot.cpp
        ${FREQART_PLUGIN_SOURCES}
        ${FREQART_CORE_SOURCES}
        ${FREQART_DSP_SOURCES}
        ${FREQART_UI_SOURCES})
    target_include_directories(FreqArtSnapshot PRIVATE src)
    # The tool pumps the message loop by hand, which the plug-in itself never does.
    target_compile_definitions(FreqArtSnapshot PRIVATE
        ${FREQART_COMMON_DEFINITIONS}
        ${FREQART_STANDALONE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(FreqArtSnapshot
        PRIVATE
            FreqArtAssets
            ${FREQART_JUCE_MODULES}
        PUBLIC
            FreqArtBuildFlags)
endif()
