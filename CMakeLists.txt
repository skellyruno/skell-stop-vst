cmake_minimum_required(VERSION 3.22)
project(StopDial VERSION 2.3.1 LANGUAGES C CXX)

# Dependency-free, fast DSP regression build for machines without the JUCE SDK.
option(STOPDIAL_MATH_ONLY "Build native DSP tests without fetching JUCE" OFF)
option(STOPDIAL_ENABLE_SANITIZERS "Enable ASan/UBSan on native DSP tests (Linux)" OFF)
option(STOPDIAL_PROFILE_DSP "Build optional no-JUCE Modern coefficient microbenchmark" OFF)
# Treat native math regression warnings as errors on supported compilers.
function(stopdial_strict_test_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Werror)
    endif()
endfunction()
if(STOPDIAL_MATH_ONLY)
    enable_testing()
    add_executable(StopDialDSPTests tests/stopdial_math_tests.cpp)
    target_compile_features(StopDialDSPTests PRIVATE cxx_std_17)
    stopdial_strict_test_warnings(StopDialDSPTests)
    if(STOPDIAL_ENABLE_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(StopDialDSPTests PRIVATE -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(StopDialDSPTests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME StopDialDSPTests COMMAND StopDialDSPTests)
    if(STOPDIAL_PROFILE_DSP)
        add_executable(StopDialModernBenchmark tests/bench_modern.cpp)
        target_compile_features(StopDialModernBenchmark PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(StopDialModernBenchmark PRIVATE /O2)
        else()
            target_compile_options(StopDialModernBenchmark PRIVATE -O3)
        endif()
    endif()
    return()
endif()

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
    COMPANY_WEBSITE "https://skellyr.uno"
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
    Source/dsp/StopDialMath.h
    Source/PluginEditor.h Source/PluginEditor.cpp
    Source/ui/Theme.h
    Source/ui/DialComponent.h Source/ui/DialComponent.cpp
    Source/ui/WaveformView.h Source/ui/WaveformView.cpp
    Source/ui/RoundIconButton.h Source/ui/RoundIconButton.cpp
    Source/ui/ModePills.h Source/ui/ModePills.cpp
    Source/ui/StatusStrip.h Source/ui/StatusStrip.cpp
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

option(STOPDIAL_BUILD_TESTS "Build dependency-free C++ DSP regression tests" ON)
if(STOPDIAL_BUILD_TESTS)
    enable_testing()
    add_executable(StopDialDSPTests tests/stopdial_math_tests.cpp)
    target_compile_features(StopDialDSPTests PRIVATE cxx_std_17)
    stopdial_strict_test_warnings(StopDialDSPTests)
    add_test(NAME StopDialDSPTests COMMAND StopDialDSPTests)

    # Full signal-chain/UI smoke tests, not a second implementation of the DSP.
    # StopDial is the JUCE shared-code static target; reuse exactly those
    # compiled plugin sources in the test executable.
    juce_add_console_app(StopDialHostTests PRODUCT_NAME "StopDial Host Tests")
    target_sources(StopDialHostTests PRIVATE tests/stopdial_host_tests.cpp)
    target_compile_features(StopDialHostTests PRIVATE cxx_std_17)
    target_compile_definitions(StopDialHostTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(StopDialHostTests PRIVATE StopDial
        juce::juce_audio_utils juce::juce_audio_processors
        juce::juce_gui_extra juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    juce_generate_juce_header(StopDialHostTests)
    add_test(NAME StopDialHostTests COMMAND StopDialHostTests)
endif()
