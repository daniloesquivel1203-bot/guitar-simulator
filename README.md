# Guitar Simulator

A standalone virtual guitar amp and pedalboard. Plug a guitar into any audio
interface, and the app processes it live and sends it to your headphones.

Status: milestone 1 of 6, the project skeleton. The app opens an empty window.
Audio arrives in milestone 2.

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

## Tech

C++20 with [JUCE](https://juce.com) 9, built with CMake. On Windows the app
is compiled with ASIO support (using the headers bundled with JUCE) and falls
back to WASAPI for interfaces without an ASIO driver.
