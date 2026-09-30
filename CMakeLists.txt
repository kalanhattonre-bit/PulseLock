cmake_minimum_required(VERSION 3.22)

project(PulseLock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSELOCK_BUILD_PLUGIN "Build the PulseLock VST3 plugin" ON)
option(PULSELOCK_BUILD_TESTS  "Build the offline PulseLock test runner" ON)

# Static MSVC runtime: the plugin then needs no Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE is pinned to one JUCE 8 release so every build uses identical framework code.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# Sources shared by the plugin and the test runner, so the tests exercise the shipped code.
set(PULSELOCK_SOURCES
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/pattern/Pattern.cpp
    Source/dsp/PulseEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(PULSELOCK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(PULSELOCK_BUILD_PLUGIN)
    juce_add_plugin(PulseLock
        COMPANY_NAME                "Kalan Hatton"
        COMPANY_WEBSITE             "https://github.com/kalanhattonre-bit/PulseLock"
        BUNDLE_ID                   com.kalanhatton.pulselock
        PLUGIN_MANUFACTURER_CODE    Khtn
        PLUGIN_CODE                 Plck
        FORMATS                     VST3
        PRODUCT_NAME                "PulseLock"
        VERSION                     ${PROJECT_VERSION}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx)

    target_sources(PulseLock PRIVATE ${PULSELOCK_SOURCES})
    target_include_directories(PulseLock PRIVATE Source)
    target_compile_definitions(PulseLock PUBLIC ${PULSELOCK_DEFINITIONS})
    target_link_libraries(PulseLock
        PRIVATE
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

if(PULSELOCK_BUILD_TESTS)
    juce_add_console_app(PulseLockTests PRODUCT_NAME "PulseLockTests")

    target_sources(PulseLockTests PRIVATE Tests/TestMain.cpp ${PULSELOCK_SOURCES})
    target_include_directories(PulseLockTests PRIVATE Source Tests)
    target_compile_definitions(PulseLockTests PRIVATE ${PULSELOCK_DEFINITIONS} PULSELOCK_TESTS=1)
    target_link_libraries(PulseLockTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
