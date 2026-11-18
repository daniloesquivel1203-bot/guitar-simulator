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
    BUNDLE_ID "com.guitarsimulator.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(GuitarSimulator
    PRIVATE
        src/Main.cpp
        src/MainComponent.cpp
        src/engine/AmpModel.cpp
        src/engine/AudioEngine.cpp
        src/engine/Cabinet.cpp
        src/engine/Pedalboard.cpp
        src/engine/Presets.cpp
        src/engine/SignalChain.cpp
        src/engine/Tuner.cpp
        src/engine/effects/Drives.cpp
        src/engine/effects/Dynamics.cpp
        src/engine/effects/Echoes.cpp
        src/engine/effects/Filters.cpp
        src/engine/effects/Modulation.cpp
        src/engine/effects/MoreDrives.cpp
        src/engine/effects/MoreDynamics.cpp
        src/engine/effects/Pitch.cpp
        src/engine/effects/Reverbs.cpp
        src/engine/effects/TimeEffects.cpp
        src/ui/AudioSettingsPanel.cpp
        src/ui/PresetBar.cpp
        src/ui/Theme.cpp
        src/ui/TonePanels.cpp
        src/ui/TunerPanel.cpp)

target_include_directories(GuitarSimulator PRIVATE src)

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
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Automated tests for the audio path. They run without any audio device.
# Enable with: cmake -B build -DGUITAR_SIMULATOR_BUILD_TESTS=ON
option(GUITAR_SIMULATOR_BUILD_TESTS "Build the automated tests" OFF)

if(GUITAR_SIMULATOR_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(GuitarSimulatorTests PRODUCT_NAME "GuitarSimulatorTests")

    target_sources(GuitarSimulatorTests
        PRIVATE
            src/engine/AmpModel.cpp
            src/engine/Cabinet.cpp
            src/engine/Pedalboard.cpp
            src/engine/Presets.cpp
            src/engine/SignalChain.cpp
            src/engine/Tuner.cpp
            src/engine/effects/Drives.cpp
            src/engine/effects/Dynamics.cpp
            src/engine/effects/Echoes.cpp
            src/engine/effects/Filters.cpp
            src/engine/effects/Modulation.cpp
            src/engine/effects/MoreDrives.cpp
            src/engine/effects/MoreDynamics.cpp
            src/engine/effects/Pitch.cpp
            src/engine/effects/Reverbs.cpp
            src/engine/effects/TimeEffects.cpp
            tests/AmpModelTests.cpp
            tests/ComfortTests.cpp
            tests/CabinetTests.cpp
            tests/EffectTests.cpp
            tests/InputRoutingTests.cpp
            tests/PedalLibraryTests.cpp
            tests/PedalboardTests.cpp
            tests/PerformanceTests.cpp
            tests/PresetTests.cpp
            tests/TunerTests.cpp
            tests/SafetyLimiterTests.cpp
            tests/SignalChainTests.cpp
            tests/SoundLabTests.cpp
            tests/TestSignals.h)

    target_include_directories(GuitarSimulatorTests PRIVATE src)

    target_compile_definitions(GuitarSimulatorTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)

    target_link_libraries(GuitarSimulatorTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_dsp
            Catch2::Catch2WithMain
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME GuitarSimulatorTests COMMAND GuitarSimulatorTests)
endif()
