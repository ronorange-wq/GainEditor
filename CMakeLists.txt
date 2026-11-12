cmake_minimum_required(VERSION 3.22)

# Build natively for Apple Silicon (M1/M2/M3). Change to "arm64;x86_64" for a Universal build.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(GainEditor VERSION 1.4.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: either a folder next to this project (./JUCE) or downloaded automatically.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# Audio Unit only on macOS. (On other systems a Standalone build is made, for testing only.)
if(APPLE)
    set(GE_FORMATS AU)
else()
    set(GE_FORMATS Standalone)
endif()

juce_add_plugin(GainEditor
    COMPANY_NAME "GainEditor"
    PRODUCT_NAME "Gain Editor"
    PLUGIN_MANUFACTURER_CODE Gned
    PLUGIN_CODE Gedt
    FORMATS ${GE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    BUNDLE_ID com.gaineditor.plugin)

juce_generate_juce_header(GainEditor)

target_sources(GainEditor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EditModel.cpp
    Source/Analysis.cpp
    Source/CaptureStore.cpp
    Source/WaveformView.cpp
    Source/LoudnessPanel.cpp)

target_compile_definitions(GainEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GainEditor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
