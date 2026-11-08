cmake_minimum_required(VERSION 3.22)

project(OpenFilter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF : ne compile que le DSP et ses tests (rapide, sans JUCE, utilisable dans WSL / Linux).
option(OPENFILTER_PLUGIN "Compiler le plugin (JUCE, WebView2)" ON)

# --- Open Core : code commun aux plugins Open (https://github.com/Erwan33800/open-core) ---
# Version épinglée dans OPEN_CORE_VERSION. Pour travailler sur le noyau en même temps : variable d'environnement
# OPEN_CORE_DIR, ou dossier open-core voisin de celui du plugin (utilisé tel quel, sans tenir compte de la version).
include(FetchContent)
if(DEFINED ENV{OPEN_CORE_DIR})
    file(TO_CMAKE_PATH "$ENV{OPEN_CORE_DIR}" OPEN_CORE_SOURCE_DIR)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../open-core/cmake/OpenPlugin.cmake")
    get_filename_component(OPEN_CORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../open-core" ABSOLUTE)
else()
    file(STRINGS OPEN_CORE_VERSION OPEN_CORE_TAG LIMIT_COUNT 1)
    FetchContent_Declare(
        open-core
        GIT_REPOSITORY https://github.com/Erwan33800/open-core.git
        GIT_TAG        ${OPEN_CORE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(open-core)
    set(OPEN_CORE_SOURCE_DIR ${open-core_SOURCE_DIR})
endif()
include(${OPEN_CORE_SOURCE_DIR}/cmake/OpenPlugin.cmake)

# --- Tests : Catch2 v3 ---
open_use_catch2()

# --- DSP : C++ pur, sans JUCE ---
add_library(OpenFilterDsp STATIC
    Source/dsp/FilterDesign.cpp
    Source/dsp/EqEngine.cpp
    Source/params/ParamSpec.cpp)
target_include_directories(OpenFilterDsp PUBLIC Source)
target_compile_features(OpenFilterDsp PUBLIC cxx_std_20)
if(NOT MSVC)
    target_compile_options(OpenFilterDsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(OpenFilterTests
    tests/dsp/test_skeleton.cpp
    tests/dsp/test_filter_design.cpp
    tests/dsp/test_params.cpp
    tests/dsp/test_eq_engine.cpp)
target_link_libraries(OpenFilterTests PRIVATE OpenFilterDsp Catch2::Catch2WithMain)
catch_discover_tests(OpenFilterTests)

# Outil d'écoute hors ligne : applique l'égaliseur à un fichier WAV avec le même code que le plugin.
add_executable(of_render tools/render/main.cpp)
target_link_libraries(of_render PRIVATE OpenFilterDsp)

if(NOT OPENFILTER_PLUGIN)
    return()
endif()

# --- Plugin : JUCE, éditeur WebView2 et pont des paramètres viennent d'open-core ---
# Effet audio (pas un instrument) : entrée et sortie mono ou stéréo, plus un bus sidechain stéréo optionnel.
open_add_plugin(
    TARGET        OpenFilter
    PRODUCT_NAME  "Open Filter"
    SLUG          openfilter
    PLUGIN_CODE   OpnF
    KIND          EFFECT
    CATEGORIES    "Fx EQ"
    SOURCES
        Source/params/ParameterLayout.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    LINK
        OpenFilterDsp)
