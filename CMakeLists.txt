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

# NeuralAmpModelerCore runs amp captures (.nam files). It is MIT licensed.
# Only its source files are used, so SOURCE_SUBDIR points at a folder without a
# CMakeLists.txt, which stops its own test tools from being built.
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        d3189ed482ded880c128bf312941266c4fa5ceec
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  NAM)

# Eigen, the maths library NeuralAmpModelerCore uses, at the version it pins.
FetchContent_Declare(Eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/bc3b39870ecb690a623a3f49149a358b95c5781d/eigen-bc3b39870ecb690a623a3f49149a358b95c5781d.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  none)

FetchContent_MakeAvailable(NeuralAmpModelerCore Eigen)

file(GLOB NAM_SOURCES
    "${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/*/*.cpp")

# An object library, not a static one: each network type registers itself from
# a static initialiser, which a linker would drop from a static library.
add_library(nam_core OBJECT ${NAM_SOURCES})
# SYSTEM keeps the app's strict warnings from applying to these headers.
target_include_directories(nam_core SYSTEM
    PUBLIC
        "${neuralampmodelercore_SOURCE_DIR}"
        "${eigen_SOURCE_DIR}"
        "${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann")
# Process in 32-bit floats, like the rest of the app.
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_compile_features(nam_core PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(nam_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The network runs on the audio thread, so outside Visual Studio it is always
# built with optimisation, even when no build type is given.
if(MSVC)
    target_compile_options(nam_core PRIVATE /W0 /bigobj)
else()
    target_compile_options(nam_core PRIVATE -O3 -w)
endif()

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
        src/engine/AmpCapture.cpp
        src/engine/AmpModel.cpp
        src/engine/AudioEngine.cpp
        src/engine/Cabinet.cpp
        src/engine/CustomPedalLibrary.cpp
        src/engine/Pedalboard.cpp
        src/engine/Practice.cpp
        src/engine/Presets.cpp
        src/engine/SignalChain.cpp
        src/engine/Tuner.cpp
        src/engine/effects/BlockDsp.cpp
        src/engine/effects/CustomPedal.cpp
        src/engine/effects/PatchPedal.cpp
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
        src/ui/CustomPedalPanel.cpp
        src/ui/LooperLayersView.cpp
        src/ui/PracticePanel.cpp
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
        nam_core
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
            src/engine/AmpCapture.cpp
            src/engine/AmpModel.cpp
            src/engine/Cabinet.cpp
            src/engine/CustomPedalLibrary.cpp
            src/engine/Pedalboard.cpp
            src/engine/Practice.cpp
            src/engine/Presets.cpp
            src/engine/SignalChain.cpp
            src/engine/Tuner.cpp
            src/engine/effects/BlockDsp.cpp
            src/engine/effects/CustomPedal.cpp
            src/engine/effects/PatchPedal.cpp
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
            tests/AmpCaptureTests.cpp
            tests/AmpModelTests.cpp
            tests/ComfortTests.cpp
            tests/CabinetTests.cpp
            tests/CustomPedalTests.cpp
            tests/PatchPedalTests.cpp
            tests/EffectTests.cpp
            tests/InputRoutingTests.cpp
            tests/PedalLibraryTests.cpp
            tests/PedalboardTests.cpp
            tests/PerformanceTests.cpp
            tests/PracticeTests.cpp
            tests/PresetTests.cpp
            tests/TunerTests.cpp
            tests/SafetyLimiterTests.cpp
            tests/SignalChainTests.cpp
            tests/SoundLabTests.cpp
            tests/TempoSyncTests.cpp
            tests/TestSignals.h)

    target_include_directories(GuitarSimulatorTests PRIVATE src)

    target_compile_definitions(GuitarSimulatorTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            # Example captures that come with NeuralAmpModelerCore, used by the capture tests.
            NAM_EXAMPLE_MODELS_DIR="${neuralampmodelercore_SOURCE_DIR}/example_models")

    target_link_libraries(GuitarSimulatorTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_dsp
            Catch2::Catch2WithMain
            nam_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME GuitarSimulatorTests COMMAND GuitarSimulatorTests)
endif()
