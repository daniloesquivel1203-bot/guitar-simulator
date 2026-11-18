# Guitar Simulator

A standalone virtual guitar amp and pedalboard. Plug a guitar into any audio
interface, and the app processes it live and sends it to your headphones.

Status: all six planned milestones are done. The full signal chain is in
place (noise gate, compressor, overdrive, distortion, amp, cabinet, EQ, chorus,
delay, reverb), with factory presets, your own presets saved as files, and a
built-in tuner with selectable strings and tunings. With everything switched
on, the processing uses about 2.5% of one processor core at 48 kHz.

Your presets are plain JSON files in `%APPDATA%\Guitar Simulator\Presets` on
Windows. The More button next to the preset list opens that folder.

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

5. The row of tiles is the signal chain, left to right. Click a tile to show
   its controls, and click the light on a tile to switch it on or off.
6. On the Cab tile, choose a cabinet or load your own impulse response file
   (WAV or AIFF) for a more realistic speaker sound.

Settings are remembered between launches. If the interface is unplugged, the
app waits and reconnects when it returns.

## Amp captures (NAM)

The Amp block can play a Neural Amp Modeler capture instead of a built-in
voicing. A capture is a `.nam` file: a small neural network trained on a real
amp or pedal at one setting. Thousands are free to download, for example from
ToneHunt.

1. Click the Amp tile, open the drop-down and choose **Load capture file
   (.nam)...**.
2. The knobs change to Input (how hard the capture is driven), Bass, Mid,
   Treble and Output. Pick a built-in voicing from the same drop-down to go
   back; the capture stays in the list until you choose **Remove capture**.
3. Captures marked as including the speaker cabinet switch the Cab block off
   when loaded, so you do not get two cabinets. Switch it back on if you want.

Captures that report their loudness are levelled to a common volume. A capture
made at a different sample rate from your audio device is converted on the
fly. Presets remember the capture file by its location, so keep your captures
in one folder; a preset whose file has moved falls back to the built-in amp.

A full-size capture costs much more processing than the built-in amp (roughly
10 to 30% of one core, depending on the computer). If you hear crackles, raise
the buffer size or use a "lite" or "feather" version of the capture.

The captures are run by [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
(MIT licence), which the build downloads automatically along with Eigen.

## Pedal library

Click a **+** tile on either side of the amp to add a pedal. The menu groups
them by kind. Up to 8 pedals fit on each side, and the same pedal can be added
more than once.

| Group | Pedals |
| --- | --- |
| Dynamics | Noise Gate, Compressor, Opto Compressor (slow, smooth leveling), Clean Boost, Auto Swell (fades each note in) |
| Drive | Treble Booster, Blues Drive (low gain, open), Overdrive (mid hump), Distortion, Metal Distortion (with its own 3-band EQ), Vintage Fuzz (with a Bias knob for sputter), Sustain Fuzz (scooped, singing), Octave Fuzz, Bit Crusher |
| Filter | EQ, Graphic EQ (7 bands), Wah (manual or automatic), Envelope Filter (auto-wah), Vowel Filter (talking) |
| Modulation | Chorus, Flanger, Phaser (4 to 12 stages), Vibe, Vibrato, Tremolo (classic, harmonic or pan), Rotary Speaker, Ring Modulator |
| Pitch | Octaver (one and two octaves down, one up), Pitch Shifter, Detune (stereo doubler) |
| Delay | Delay (digital, with ping-pong), Tape Echo, Analog Delay, Reverse Delay |
| Reverb | Room Reverb, Spring Reverb, Plate Reverb, Hall Reverb, Shimmer Reverb |

The names describe a style of sound, not a particular product. Presets store
each pedal by a fixed key, so presets made with fewer pedals keep loading.

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
