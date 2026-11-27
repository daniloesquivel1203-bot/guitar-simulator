# GUI notes

How the pedal-style interface fits together, and what to keep in mind when
adding pedals, blocks or screens. Keep this file up to date as the UI changes.

## Layout

```
+----------------------------------------------------------------------------+
| Title  Presets ...   Undo Redo  A|B  Practice  Tuner  Audio settings       |  header (one row)
| [warning line, only when there is no audio device or no guitar input]      |
+-----------+-------------------------------------------+--------------------+
| IN level  |                                           |                    |
|           |   the open pedal as a stompbox,           |   Amp head on      |
| brick     |   its neighbours either side;             |   cabinet          |
| stack     |   or the whole chain (amp selected);      |   (AmpRig)         |
| (signal   |   or the tuner / practice / wide editor   |                    |
| chain)    +-------------------------------------------+                    |
|           |   pedal tray: category tabs, mini pedals  |                    |
| OUT level |                                           |                    |
+-----------+-------------------------------------------+--------------------+
```

- `MainComponent` owns the layout. The stack column is 270 px wide, the amp is
  450 px, the tray 150 px high. The window is at least 1100 x 700 and opens at
  1280 x 800, so the middle floor can be as narrow as about 380 px. Test new
  views at that size.
- The Custom and Patch pedal editors, the tuner and the practice tools are
  "wide" panels: they also take the amp's space.

## What shows in the middle

| Selected in the stack | Middle shows                         | Class            |
|-----------------------|--------------------------------------|------------------|
| An ordinary pedal     | Big stompbox, neighbours either side | `EffectPanel`, `NeighbourPedal` |
| Custom / Patch pedal  | Its block editor (wide)              | `CustomPedalPanel`, `PatchPanel` |
| Amp & Cab             | The whole chain, patched in order    | `ChainOverview`  |
| Tuner / Practice      | That tool (wide)                     | `TunerPanel`, `PracticePanel` |

Everything in the middle derives from `TonePanel`. A new screen there should
too, and should leave its background transparent so the floor gradient shows.

## The hardware look (`PedalLook.h`, `MiniPedal.h`)

- **Colour comes from the category.** `pedal_look::categoryColour` has one
  colour per category; `pedalColour` nudges hue and brightness per pedal so
  pedals in a category differ but still belong together. A new category needs
  its own entry in `categoryColour`, or it falls back to grey.
- **Text colour** on any coloured face comes from `pedal_look::inkFor`, which
  picks dark or light for contrast. Don't hard-code white or black on a face.
- **Off pedals** are drawn with the face desaturated and darkened
  (`withMultipliedSaturation (0.45f).withMultipliedBrightness (0.6f)`), the
  same in the stack, the mini pedals and the chain view.
- **Small pedals** (neighbours, chain view) all draw through `mini_pedal::paint`.
  The tray has its own tiny tile but shares `mini_pedal::paintKnobs`.
- **Knobs**: a pedal shows one knob per control that is neither a switch nor
  a choice (`mini_pedal::countKnobs`). They are laid out the same everywhere:
  up to 3 in one row, 4 as 2 x 2, 5-6 as two rows of 3, then rows of 4. If a
  pedal gets more controls, the big stompbox grows and the small ones follow
  without changes.
- Switches and choices (Sync, Mode, ...) only appear on the big stompbox, in
  the row under the name.
- Lights: pedals use a red LED, as on real stompboxes; the stack's brick
  lights are green, the amp's red.
- Cables are `mini_pedal::paintCable` (a sagging cable with plugs). Use it for
  anything that shows signal flow between blocks.

## Interaction rules

- **Click** a block to open it. **Click its light or footswitch** to switch it
  on or off. This works the same in the stack, the chain view and the
  neighbours.
- **Drag** to move pedals (in the stack) and to add them (from the tray onto
  the stack). Moving is not offered in menus, so menus stay short.
- **Right-click** a pedal for its menu, which now only has Remove. Open menus
  next to what was clicked (`withTargetScreenArea` on that item's screen
  bounds), never on a whole panel, or the menu lands far from the pointer.
- Clicking a pedal in the tray adds it after the selected one.
- Undo/redo and A/B work from snapshots of the whole state, so a new control
  that is saved in presets gets undo for free.

## The header

The header is one row of controls. Device details (driver, sample rate,
buffer, latency, CPU, dropouts) are not shown; they live in Audio settings.
The warning line under it appears only when the player has to act: no audio
device, or no guitar input. Don't add status text that is shown all the time.

## Adding a pedal: checklist

1. Add the effect and register its `PedalType` (key, short name, category) in
   `Pedalboard.cpp`. The short name is what the bricks and mini pedals show,
   so keep it to about 8 characters.
2. Use an existing category if one fits, so it gets a colour and a tray tab.
3. Knob captions come from the parameter names. Keep them short (one word)
   so they fit under a 92 px knob.
4. Check the stompbox at the 1280 x 800 window: with 7 or more knobs (4
   columns), the neighbours hide to make room. That is intended.
5. A pedal that needs more than knobs, switches and choices gets its own
   panel, like `CustomPedalPanel`, and is marked wide in `selectBlock`.

## Checking a UI change without the audio interface

The app opens the audio interface when it starts, so it can't run alongside
another copy that is using it. To look at a view, build a small throwaway
console target that creates the component (for example `ChainOverview`,
`EffectPanel` or `PedalTray`), calls `createComponentSnapshot` and saves a PNG.
None of these views need the audio engine.
