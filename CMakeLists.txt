cmake_minimum_required(VERSION 3.22)

project(KevyDoesDevy VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime on Windows, so the plugin needs nothing else installed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: a local copy (-DKDD_JUCE_PATH=...) or JUCE 8 downloaded automatically
set(KDD_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(KDD_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(KDD_BUILD_TESTS "Build the offline render test" ON)

if(KDD_BUILD_PLUGIN)
    if(KDD_JUCE_PATH)
        add_subdirectory(${KDD_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(KevyDoesDevy
        COMPANY_NAME "Kevy"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.kevydoesdevy"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Kddv
        FORMATS VST3 Standalone
        PRODUCT_NAME "Kevy does Devy"
        DESCRIPTION "Dual delay, reverb and looper, tuned for bass"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Delay Reverb)

    juce_add_binary_data(KddData SOURCES
        Resources/TitanOne-Regular.ttf
        Resources/ArchivoBlack-Regular.ttf
        Resources/PermanentMarker-Regular.ttf
        Resources/cranium.png
        Resources/jaw.png
        Resources/ballL.png
        Resources/ballR.png
        Resources/irisL.png
        Resources/irisR.png)

    target_sources(KevyDoesDevy PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp)

    target_include_directories(KevyDoesDevy PRIVATE Source Source/engine)

    target_compile_definitions(KevyDoesDevy PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(KevyDoesDevy
        PRIVATE
            KddData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# Offline engine test (no JUCE needed): renders a bass line through every effect
if(KDD_BUILD_TESTS)
    add_executable(kdd_render_test Tests/RenderTest.cpp)
    target_include_directories(kdd_render_test PRIVATE Source/engine)
endif()
