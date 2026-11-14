cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64) on CI.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(Outrigger VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(outrigger_engine STATIC
    Source/engine/Sync.cpp
    Source/engine/Router.cpp
    Source/engine/Clock.cpp
    Source/engine/Analysis.cpp)
target_include_directories(outrigger_engine PUBLIC Source/engine)
set_target_properties(outrigger_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(OUTRIGGER_BUILD_TESTS "Build engine tests" ON)
if(OUTRIGGER_BUILD_TESTS)
    add_executable(outrigger_tests tests/EngineTests.cpp)
    target_link_libraries(outrigger_tests PRIVATE outrigger_engine)
    enable_testing()
    add_test(NAME engine COMMAND outrigger_tests)
endif()

option(OUTRIGGER_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(NOT OUTRIGGER_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Outrigger
    COMPANY_NAME "OutriggerAudio"
    PLUGIN_MANUFACTURER_CODE Otrg
    PLUGIN_CODE Otr1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Outrigger"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument External)

target_sources(Outrigger PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/MidiOutHub.cpp
    Source/plugin/Theme.cpp)

target_compile_definitions(Outrigger PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Outrigger
    PRIVATE
        outrigger_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- UI snapshot tool (renders the editor to PNG; used for docs, not shipped) ----
option(OUTRIGGER_BUILD_SNAPSHOT "Build the UI snapshot tool" OFF)
if(OUTRIGGER_BUILD_SNAPSHOT)
    juce_add_console_app(OutriggerSnapshot PRODUCT_NAME "OutriggerSnapshot")
    target_sources(OutriggerSnapshot PRIVATE
        tools/Snapshot.cpp
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp
        Source/plugin/MidiOutHub.cpp
        Source/plugin/Theme.cpp)
    target_compile_definitions(OutriggerSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(OutriggerSnapshot PRIVATE outrigger_engine juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
