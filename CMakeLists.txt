cmake_minimum_required(VERSION 3.22)

project(OpenFilter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF : ne compile que le DSP et ses tests (rapide, sans JUCE, utilisable dans WSL / Linux).
option(OPENFILTER_PLUGIN "Compiler le plugin (JUCE, WebView2)" ON)

include(FetchContent)

# --- Tests : Catch2 v3, tag épinglé ---
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# --- DSP : C++ pur, sans JUCE ---
add_library(OpenFilterDsp INTERFACE)
target_include_directories(OpenFilterDsp INTERFACE Source)
target_compile_features(OpenFilterDsp INTERFACE cxx_std_20)

enable_testing()
include(Catch)
add_executable(OpenFilterTests
    tests/dsp/test_skeleton.cpp)
target_link_libraries(OpenFilterTests PRIVATE OpenFilterDsp Catch2::Catch2WithMain)
catch_discover_tests(OpenFilterTests)

if(NOT OPENFILTER_PLUGIN)
    return()
endif()

# --- Plugin : JUCE 8.0.4, tag épinglé ---
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

set(OPENFILTER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND OPENFILTER_FORMATS AU)
endif()

# Effet audio (pas un instrument) : entrée et sortie mono ou stéréo, plus un bus sidechain stéréo optionnel.
juce_add_plugin(OpenFilter
    COMPANY_NAME                 "Erwan"
    PRODUCT_NAME                 "Open Filter"
    BUNDLE_ID                    "fr.erwan.openfilter"
    PLUGIN_MANUFACTURER_CODE     Ewan
    PLUGIN_CODE                  OpnF
    FORMATS                      ${OPENFILTER_FORMATS}
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    NEEDS_WEBVIEW2               TRUE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Fx EQ)

juce_add_binary_data(OpenFilterUI SOURCES ui/openfilter_ui.html)

target_sources(OpenFilter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(OpenFilter PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OpenFilter
    PRIVATE
        OpenFilterUI
        OpenFilterDsp
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
