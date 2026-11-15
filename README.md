# Guitar Simulator

A standalone virtual guitar amp and pedalboard. Plug a guitar into any audio
interface, and the app processes it live and sends it to your headphones.

Status: milestone 3 of 6, amp and cabinet. The app takes the guitar from one
input channel and runs it through an amp with three voicings (clean, crunch
and lead), a speaker cabinet and a safety limiter, and plays it in both ears.
Pedals arrive in the next milestone.

## First run

1. Turn off Direct Monitor on your interface, or you will hear the dry guitar
   on top of the app's output.
2. Start the app with your headphone volume low. The status line at the top
   shows the device in use. On Windows the app picks an ASIO driver on first
   launch if one is installed.
3. Click **Audio settings** to change the device, sample rate or buffer size,
   and set **Guitar input** to the channel the guitar is plugged into. Which
   number that is depends on the interface. Pick the one that makes the input
   meter move when you play.
4. Play. The input meter should move, and the red light above it means the
   interface input is clipping, so turn its gain knob down.

5. Pick an amp voicing and turn the knobs. Choose a cabinet, or load your own
   impulse response file (WAV or AIFF) for a more realistic speaker sound.

Settings are remembered between launches. If the interface is unplugged, the
app waits and reconnects when it returns.

## Build on Windows

### One-time setup

Install these three tools. Open PowerShell and run:

```powershell
winget install Git.Git
winget install Kitware.CMake
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"
```

The last one is the C++ compiler and is a large download (several GB). If you
already have Visual Studio 2019 or newer with the "Desktop development with
C++" workload, skip it. Close and reopen PowerShell afterwards.

### Build and run

From the folder containing this README:

```powershell
cmake -B build
cmake --build build --config Release
& ".\build\GuitarSimulator_artefacts\Release\Guitar Simulator.exe"
```

The first `cmake -B build` downloads the JUCE framework, so it needs an
internet connection and takes a few minutes. The first build takes several
minutes. Later builds only recompile what changed.

After pulling new changes, only the last two commands are needed.

## Build on Linux

Install the JUCE dependencies (Debian or Ubuntu names):

```sh
sudo apt install build-essential cmake ninja-build libasound2-dev libx11-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev \
    libxcomposite-dev libxrender-dev libfreetype-dev libfontconfig1-dev \
    libgl1-mesa-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
"./build/GuitarSimulator_artefacts/Release/Guitar Simulator"
```

## Tests

The audio path has automated tests that run without an audio device:

```sh
cmake -B build -DGUITAR_SIMULATOR_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Tech

C++20 with [JUCE](https://juce.com) 9, built with CMake. On Windows the app
is compiled with ASIO support (using the headers bundled with JUCE) and falls
back to WASAPI for interfaces without an ASIO driver.
