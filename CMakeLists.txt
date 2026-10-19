cmake_minimum_required(VERSION 3.22)
project(StopDial VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(StopDial
    COMPANY_NAME "Skell Audio"
    COMPANY_WEBSITE "https://example.invalid/stopdial"
    BUNDLE_ID "audio.skell.stopdial"
    PLUGIN_MANUFACTURER_CODE SkAu
    PLUGIN_CODE Sd01
    PRODUCT_NAME "StopDial"
    DESCRIPTION "Tempo-synchronised tape stop and creative looping"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone)

juce_generate_juce_header(StopDial)

target_sources(StopDial PRIVATE
    Source/PluginProcessor.h Source/PluginProcessor.cpp
    Source/PluginEditor.h Source/PluginEditor.cpp
    Source/ui/Theme.h
    Source/ui/DialComponent.h Source/ui/DialComponent.cpp
    Source/ui/WaveformView.h Source/ui/WaveformView.cpp
    Source/ui/RoundIconButton.h Source/ui/RoundIconButton.cpp
    Source/ui/ModePills.h Source/ui/ModePills.cpp
    Source/ui/LookAndFeel.h Source/ui/LookAndFeel.cpp)

target_compile_features(StopDial PRIVATE cxx_std_17)
target_compile_definitions(StopDial PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StopDial PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

option(STOPDIAL_BUILD_TESTS "Build standalone DSP verification tests" OFF)
if(STOPDIAL_BUILD_TESTS)
    enable_testing()
    # The test harness is an in-plugin debug check, documented in tests/README.md.
endif()
