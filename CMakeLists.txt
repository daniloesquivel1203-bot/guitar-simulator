cmake_minimum_required(VERSION 3.22)

project(GuitarSimulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is downloaded at configure time. Nothing needs to be installed by hand.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(GuitarSimulator
    PRODUCT_NAME "Guitar Simulator"
    COMPANY_NAME "Guitar Simulator"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(GuitarSimulator
    PRIVATE
        src/Main.cpp
        src/MainComponent.cpp)

target_compile_definitions(GuitarSimulator
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # ASIO gives the lowest latency on Windows. JUCE bundles the ASIO headers,
        # so no separate SDK download is needed. Ignored on other platforms.
        JUCE_ASIO=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GuitarSimulator,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GuitarSimulator,JUCE_VERSION>")

target_link_libraries(GuitarSimulator
    PRIVATE
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
