cmake_minimum_required(VERSION 3.22)

# Must be set before project(): the oldest macOS the plug-in will load on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(ArtFreq VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ARTFREQ_BUILD_TESTS "Build the test runner" ON)
option(ARTFREQ_BUILD_TOOLS "Build the headless UI snapshot tool and the lows and carving labs" ON)
option(ARTFREQ_COPY_PLUGIN_AFTER_BUILD "Copy the built plug-in into the user's VST3 folder" OFF)
option(ARTFREQ_ENABLE_LTO "Enable link-time optimisation in Release builds" ON)

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
file(GLOB_RECURSE ARTFREQ_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB_RECURSE ARTFREQ_DSP_SOURCES  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp")
file(GLOB_RECURSE ARTFREQ_UI_SOURCES   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
file(GLOB         ARTFREQ_FONT_ASSETS  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")

set(ARTFREQ_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.cpp)

set(ARTFREQ_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    ARTFREQ_VERSION_STRING="${PROJECT_VERSION}")

juce_add_binary_data(ArtFreqAssets
    HEADER_NAME ArtFreqAssets.h
    NAMESPACE   ArtFreqAssets
    SOURCES     ${ARTFREQ_FONT_ASSETS})

add_library(ArtFreqBuildFlags INTERFACE)
target_link_libraries(ArtFreqBuildFlags INTERFACE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(ARTFREQ_ENABLE_LTO)
    target_link_libraries(ArtFreqBuildFlags INTERFACE juce::juce_recommended_lto_flags)
endif()

set(ARTFREQ_JUCE_MODULES
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra)

# ---------------------------------------------------------------------------
# The plug-ins: one code base, registered twice, because hosts decide from a VST3's category
# where it may go (Ableton Live, FL Studio and Cubase offer instruments only on MIDI /
# instrument tracks and effects only on audio tracks, buses and the master):
#
#   ArtFreq     an instrument: it makes the sound whose spectrogram is the drawing.
#   ArtFreq FX  an audio effect: it carves the drawing into the audio coming in (or plays it
#               on top). It also takes MIDI, for the hosts that send MIDI to effects.
#
# The same processor and editor (PluginProcessor.h: a Variant flag); each target only adds
# its own createPluginFilter (src/plugin/).
# ---------------------------------------------------------------------------
function(artfreq_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_ID;DESCRIPTION;IS_SYNTH;ENTRY" "VST3_CATEGORIES" ${ARGN})

    juce_add_plugin(${target}
        VERSION                     ${PROJECT_VERSION}
        COMPANY_NAME                "AnyMeans"
        COMPANY_COPYRIGHT           "AnyMeans"
        BUNDLE_ID                   "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE    Anym
        PLUGIN_CODE                 ${ARG_PLUGIN_CODE}
        FORMATS                     VST3
        PRODUCT_NAME                "${ARG_PRODUCT_NAME}"
        DESCRIPTION                 "${ARG_DESCRIPTION}"
        IS_SYNTH                    ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES             ${ARG_VST3_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD     ${ARTFREQ_COPY_PLUGIN_AFTER_BUILD})

    target_sources(${target} PRIVATE
        ${ARG_ENTRY}
        ${ARTFREQ_PLUGIN_SOURCES}
        ${ARTFREQ_CORE_SOURCES}
        ${ARTFREQ_DSP_SOURCES}
        ${ARTFREQ_UI_SOURCES})

    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PUBLIC ${ARTFREQ_COMMON_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(${target}
        PRIVATE
            ArtFreqAssets
            ${ARTFREQ_JUCE_MODULES}
        PUBLIC
            ArtFreqBuildFlags)
endfunction()

artfreq_add_plugin(ArtFreq
    PRODUCT_NAME    "ArtFreq"
    PLUGIN_CODE     FqAr
    BUNDLE_ID       "com.anymeans.artfreq"
    DESCRIPTION     "Draw text, images and brush strokes into the spectrogram"
    IS_SYNTH        TRUE
    VST3_CATEGORIES Instrument Generator
    ENTRY           ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/ArtFreqEntry.cpp)

artfreq_add_plugin(ArtFreqFX
    PRODUCT_NAME    "ArtFreq FX"
    PLUGIN_CODE     FqFx
    BUNDLE_ID       "com.anymeans.artfreqfx"
    DESCRIPTION     "Carve text, images and brush strokes into the spectrogram of a song"
    IS_SYNTH        FALSE
    VST3_CATEGORIES Fx Filter
    ENTRY           ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/ArtFreqFXEntry.cpp)

# Definitions the plug-in wrapper normally provides, for the console apps that compile the
# processor and editor without it (they make either variant themselves).
set(ARTFREQ_STANDALONE_DEFINITIONS
    JucePlugin_Name="ArtFreq"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=1
    JucePlugin_VersionString="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Tests: the drawing, the plan, the engine, the processor (no host).
# ---------------------------------------------------------------------------
if(ARTFREQ_BUILD_TESTS)
    file(GLOB ARTFREQ_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

    juce_add_console_app(ArtFreqTests PRODUCT_NAME "ArtFreqTests")
    target_sources(ArtFreqTests PRIVATE
        ${ARTFREQ_TEST_SOURCES}
        ${ARTFREQ_PLUGIN_SOURCES}
        ${ARTFREQ_CORE_SOURCES}
        ${ARTFREQ_DSP_SOURCES}
        ${ARTFREQ_UI_SOURCES})
    target_include_directories(ArtFreqTests PRIVATE src tests)
    target_compile_definitions(ArtFreqTests PRIVATE
        ${ARTFREQ_COMMON_DEFINITIONS}
        ${ARTFREQ_STANDALONE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1
        ARTFREQ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(ArtFreqTests
        PRIVATE
            ArtFreqAssets
            ${ARTFREQ_JUCE_MODULES}
        PUBLIC
            ArtFreqBuildFlags)

    enable_testing()
    add_test(NAME ArtFreqTests COMMAND ArtFreqTests)
endif()

# ---------------------------------------------------------------------------
# UI snapshot tool: renders the editor to PNG files without a host.
# ---------------------------------------------------------------------------
if(ARTFREQ_BUILD_TOOLS)
    juce_add_console_app(ArtFreqSnapshot PRODUCT_NAME "ArtFreqSnapshot")
    target_sources(ArtFreqSnapshot PRIVATE
        tools/Snapshot.cpp
        ${ARTFREQ_PLUGIN_SOURCES}
        ${ARTFREQ_CORE_SOURCES}
        ${ARTFREQ_DSP_SOURCES}
        ${ARTFREQ_UI_SOURCES})
    target_include_directories(ArtFreqSnapshot PRIVATE src)
    # The tool pumps the message loop by hand, which the plug-in itself never does.
    target_compile_definitions(ArtFreqSnapshot PRIVATE
        ${ARTFREQ_COMMON_DEFINITIONS}
        ${ARTFREQ_STANDALONE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ArtFreqSnapshot
        PRIVATE
            ArtFreqAssets
            ${ARTFREQ_JUCE_MODULES}
        PUBLIC
            ArtFreqBuildFlags)

    # The lows lab: renders test drawings through the real plan and engine, analyses the sound
    # like a spectrogram would and measures how the 30 Hz - 2 kHz part of it looks (PNGs and
    # metrics), so different ways of drawing the lows can be compared by numbers.
    juce_add_console_app(ArtFreqLab PRODUCT_NAME "ArtFreqLab")
    target_sources(ArtFreqLab PRIVATE
        tools/LowsLab.cpp
        ${ARTFREQ_PLUGIN_SOURCES}
        ${ARTFREQ_CORE_SOURCES}
        ${ARTFREQ_DSP_SOURCES}
        ${ARTFREQ_UI_SOURCES})
    target_include_directories(ArtFreqLab PRIVATE src)
    target_compile_definitions(ArtFreqLab PRIVATE
        ${ARTFREQ_COMMON_DEFINITIONS}
        ${ARTFREQ_STANDALONE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ArtFreqLab
        PRIVATE
            ArtFreqAssets
            ${ARTFREQ_JUCE_MODULES}
        PUBLIC
            ArtFreqBuildFlags)

    # The carving lab: plays songs through a spectral carving engine with ArtFreq drawings and
    # measures what an analyser shows (tools/CarveLab.cpp). Needs no processor or editor.
    juce_add_console_app(ArtFreqCarveLab PRODUCT_NAME "ArtFreqCarveLab")
    target_sources(ArtFreqCarveLab PRIVATE
        tools/CarveLab.cpp
        ${ARTFREQ_CORE_SOURCES}
        ${ARTFREQ_DSP_SOURCES})
    target_include_directories(ArtFreqCarveLab PRIVATE src)
    target_compile_definitions(ArtFreqCarveLab PRIVATE
        ${ARTFREQ_COMMON_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ArtFreqCarveLab
        PRIVATE
            ArtFreqAssets
            ${ARTFREQ_JUCE_MODULES}
        PUBLIC
            ArtFreqBuildFlags)

    # The emblem lab: renders the emblem (src/ui/Emblem.h) and the header lockup to PNG files.
    juce_add_console_app(ArtFreqEmblemLab PRODUCT_NAME "ArtFreqEmblemLab")
    target_sources(ArtFreqEmblemLab PRIVATE
        tools/EmblemLab.cpp
        src/ui/Emblem.cpp
        src/core/Fonts.cpp)
    target_include_directories(ArtFreqEmblemLab PRIVATE src)
    target_compile_definitions(ArtFreqEmblemLab PRIVATE
        ${ARTFREQ_COMMON_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ArtFreqEmblemLab
        PRIVATE
            ArtFreqAssets
            juce::juce_gui_basics
        PUBLIC
            ArtFreqBuildFlags)
endif()
