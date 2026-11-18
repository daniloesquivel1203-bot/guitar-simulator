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

## Practice tools

Click **Practice** at the top to open the metronome, looper and recorder. They
keep running when you switch to another panel, and a dot on the Practice
button shows that something is still on.

- **Metronome:** Start, set the tempo (or tap it with **Tap** four times or
  more), and choose the beats per bar. The first beat of the bar is a higher
  click unless you untick the accent. The click is only heard, never looped
  or recorded.
- **Looper:** works like a looper pedal with one main button. Press
  **Record**, play a phrase, then press **Play** at the moment it should
  start repeating. Press **Overdub** to add a layer on top and **Play** to
  keep it. **Undo** takes back the last layer, **Stop** pauses, **Clear**
  throws the loop away. Loops can be up to 90 seconds and record the sound
  you hear, pedals and amp included.
- **Record to WAV:** each take saves two files in
  `Documents\Guitar Simulator\Recordings`: the sound you hear (with the loop,
  without the click), and a `(dry)` file with the plain guitar exactly as it
  came in, so it can be played back through a different sound later.

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
