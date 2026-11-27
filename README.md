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
2. Start the app with your headphone volume low. On Windows the app picks an
   ASIO driver on first launch if one is installed. A warning line appears at
   the top only when there is no audio device or no guitar input to use.
3. Click **Audio settings** to change the device, sample rate or buffer size,
   and set **Guitar input** to the channel the guitar is plugged into. Which
   number that is depends on the interface. Pick the one that makes the input
   meter move when you play.
4. Play. The input meter should move, and the red light above it means the
   interface input is clipping, so turn its gain knob down.

5. The stack of bricks on the left is the signal chain, top to bottom: the
   pedals before the amp, the amp and cabinet, then the pedals after the amp.
   Click a brick to show that pedal in the middle as a stompbox, and click the
   light on a brick (or the pedal's footswitch) to switch it on or off. Drag a
   brick up or down to move it, including to the other side of the amp.
   The blocks just before and after the open pedal stand beside it, patched in
   with cables; click one to open it. Right-click a pedal, in the stack or
   beside the open one, to remove it. Click the amp brick to see the whole
   chain in the middle, patched together in order; click a pedal there to open
   it, or its footswitch to switch it on or off.
6. The amp and cabinet are always on the right. The head's display picks a
   voicing; the cabinet's display picks a cabinet or loads your own impulse
   response file (WAV or AIFF) for a more realistic speaker sound. With
   **Match voicing** on, picking a voicing also picks the cabinet that suits
   it; choosing a cabinet yourself turns it off. The lights on the head and
   cabinet switch each one on or off.

Settings are remembered between launches. If the interface is unplugged, the
app waits and reconnects when it returns.

## Amp captures (NAM)

The Amp block can play a Neural Amp Modeler capture instead of a built-in
voicing. A capture is a `.nam` file: a small neural network trained on a real
amp or pedal at one setting. Thousands are free to download, for example from
ToneHunt.

1. On the amp head, open the display and choose **Load capture file
   (.nam)...**.
2. The knobs change to Input (how hard the capture is driven), Bass, Mid,
   Treble and Output. Pick a built-in voicing from the same drop-down to go
   back; the capture stays in the list until you choose **Remove capture**.
3. Captures marked as including the speaker cabinet switch the cabinet off
   when loaded, so you do not get two cabinets. Switch it back on with the
   cabinet's light if you want.

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

The tray under the pedal shows the pedals one group at a time. Click a pedal
in the tray to add it right after the selected brick, or drag it onto the stack
and drop it where the orange line shows. The **+ Add pedal** brick opens the
same list as a menu. Up to 8 pedals fit on each side of the amp, and the same
pedal can be added more than once.

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

### Order matters

As on a real board, where a pedal sits changes how it sounds:

- **Vintage Fuzz, Octave Fuzz and Treble Booster** like to be first. Straight
  from the guitar (nothing switched on in front of them) they sound warm and
  round; behind another pedal that is on, they sound thinner and spikier.
- **Compressor** in front of a drive evens out what the drive hears; after it,
  it only levels a sound that is already distorted.
- **Noise Gate** in front of a drive silences hiss between notes; after a
  drive it has to fight the amplified hiss.
- **Wah and Envelope Filter** sound vocal in front of a drive and more
  extreme after it. An envelope filter after a drive reacts less to how hard
  you pick, because the drive has already evened out your playing.
- Time and EQ pedals (EQ, chorus, delay, reverb) mostly sound the same in any
  order, as on a real board, except delay and reverb in front of a drive,
  where the repeats get distorted too.

## Comfort

- **Undo and redo** (Ctrl+Z, Ctrl+Y) cover every knob, switch and pedal move.
- **A / B** in the header keeps two versions of your sound to flip between.
  B starts as a copy of A; right-click either to copy one onto the other.
- **Preset folders:** save a preset as `Folder/Name` to file it in a folder.
  Folders show as submenus in the preset list.
- **Find a preset:** Ctrl+F, or More > Find a preset.

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

## Jam tracks

Click **Jam** at the top for a backing band to play along with. Everything is
made in the app, so nothing is downloaded.

- **Style:** rock, hard rock, blues shuffle, jazz swing, funk, pop, country,
  reggae or ballad. The style decides the drum beat, the bass line and how
  the chords are played. Picking one also sets a typical tempo and
  progression for it (your own chord edits are kept).
- **Key and chords:** choose the key's home note and major or minor, then a
  common progression. Click any chord in the chart to swap it for another
  chord that fits the key (they are listed with their Roman numerals), any
  other chord, change how long it lasts, or add and remove chords. Changing
  the key moves the whole progression.
- **Play:** with **Count in** ticked the band starts after four clicks. The
  chart lights up the chord being played, and the line under it shows the
  chord's notes, the next chord, and scales that fit the key.
- **Mix:** drums, bass, rhythm guitar, piano and organ each have an on/off
  switch and a level, and **Band** sets the whole band against your guitar.
- The tempo is the metronome's, so the click, the band and synced delays
  always agree. The band is heard and goes into recorded takes, but not into
  the looper. It keeps playing in other panels; a dot on the Jam button
  shows it.

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

Two hidden tests help with tuning by ear. `GuitarSimulatorTests "[soundlab]"`
prints how much each pedal changes the level and how different each pedal
order sounds. With the `GS_LISTEN_DIR` environment variable set to a folder,
`GuitarSimulatorTests "[listen]"` writes WAV files of each order comparison
there.

## Tech

C++20 with [JUCE](https://juce.com) 9, built with CMake. On Windows the app
is compiled with ASIO support (using the headers bundled with JUCE) and falls
back to WASAPI for interfaces without an ASIO driver.

Notes on how the interface is organised, and how new pedals, categories,
voicings and screens should fit into it, are in [docs/GUI_NOTES.md](docs/GUI_NOTES.md).
