# GUI notes

How the pedal-style interface is put together, and what to do when adding
pedals, blocks or new screens so they fit in. The sound engine does not depend
on any of this; the UI only reads and writes `Parameter`s.

## Layout

```
┌────────────────────── header (presets, undo, A/B, Jam, Practice, Tuner, Audio settings) ─┐
│ IN level          │                                        │                            │
│ ┌ BEFORE AMP ─┐   │   open pedal as a stompbox,            │   Amp & Cab:               │
│ │ brick       │   │   neighbours faded at its sides        │   head (voicing / capture) │
│ │ brick       │   │   (or the whole chain, when Amp & Cab  │   on a cabinet             │
│ │ Amp & Cab   │   │    is picked)                          │   (cab, cuts, Match        │
│ ┌ AFTER AMP ──┐   │                                        │    voicing)                │
│ │ brick       │   ├────────────────────────────────────────┤                            │
│ │ + Add pedal │   │   pedal tray: category tabs + mini     │                            │
│ OUT level, Mute   │   pedals (click or drag to add)        │                            │
└───────────────────┴────────────────────────────────────────┴────────────────────────────┘
```

- Left column (`ChainStack`, 270 px): the chain top to bottom as Lego-style
  bricks. Up to 8 pedals per side; bricks shrink so a full board fits without
  scrolling.
- Middle: the open block. A normal pedal is an `EffectPanel` stompbox with its
  neighbours (`NeighbourPedal`). Picking Amp & Cab shows `ChainOverview`.
- Bottom of the middle: `PedalTray`.
- Right column (`AmpRig`, 450 px): always visible, except while a wide panel is
  open (Tuner, Practice, Jam, Custom pedal, Patch pedal), which takes the
  middle and right together and hides the tray for Tuner, Practice and Jam.
- The header has one row. The warning line under it appears only when
  something is wrong (no device, no guitar input). Device details live in
  Audio settings.
- Window: 1280 x 800 by default, 1100 x 700 minimum.

## Files

| File | What it draws |
| --- | --- |
| `ui/PedalLook.*` | Category colours, `pedalColour()`, and the painting helpers: enclosure, brick, LED, footswitch, tolex, grill. Knob looks `StompboxKnobs` and `AmpKnobs`. |
| `ui/ChainStack.h` | The brick stack: select, light toggles, drag to reorder, drops from the tray, right-click menu. |
| `ui/TonePanels.*` | `EffectPanel`, a pedal as a big stompbox, built from the effect's own `getControls()`. |
| `ui/MiniPedal.h` | Small stompboxes and the small amp, shared by the neighbours and the chain overview. |
| `ui/NeighbourPedal.h` | The faded pedals beside the open one. |
| `ui/ChainOverview.h` | The whole chain, patched with cables, shown when Amp & Cab is picked. |
| `ui/PedalTray.*` | Category tabs and mini pedals. Drags carry `pedal:<key>` or `mine:<name>`. |
| `ui/JamPanel.*` | Jam tracks: style, key and progression, the clickable chord chart, and the band's mix. |
| `ui/AmpRig.*` | Amp head and cabinet, including Match voicing (`getMatchingCabinet`). |

## Adding a pedal

Nothing in the UI needs changing for an ordinary pedal:

1. Add the effect and its entry in `getPedalTypes()` (`engine/Pedalboard.cpp`)
   as before. Keep keys and parameter ids stable and names generic, never
   brand or product names.
2. Its `category` decides its colour family, its tray tab and the small text on
   its brick. Use an existing category if one fits.
3. `getControls()` decides the stompbox: plain parameters become knobs,
   0-to-1 parameters become switches, parameters with named choices become
   drop-downs. Knob order is the order returned.
   - Knob grid: up to 3 in a row, 4 as 2 x 2, 5 or 6 as two rows of 3, more
     as rows of 4. More than about 8 knobs makes a tall pedal; consider a
     drop-down or switch instead.
   - Switches, drop-downs and Tap sit in one row under the name.
4. The mini pedals (tray, neighbours, overview) count the same controls, so
   their knob count follows automatically.
5. Give the parameter a short `name`: it is the caption under the knob, about
   10 characters at most.

## Adding a category

- Add its colour in `pedal_look::categoryColour()`. Pick one that is not close
  to an existing category, and keep it mid-brightness so `inkFor()` can choose
  dark or light text that reads well.
- The tray adds a tab by itself (categories in the order they first appear in
  `getPedalTypes()`, then "My pedals"). Tabs shrink to fit, so roughly 10
  categories is the comfortable limit at the minimum window width.

Current colours: Dynamics grey-blue, Drive olive, Filter mustard, Modulation
teal, Pitch rose, Delay purple, Reverb blue, Custom grey.

## Pedals with their own editor

Custom and Patch pedals use their own panels (`CustomPedalPanel`,
`PatchPanel`) instead of the stompbox, and open as wide panels. A new pedal
that needs a canvas or a long list should do the same: return its panel from
the `panelMakers` lambda in `MainComponent::rebuildChain`. Anything that is not
an `EffectPanel` is treated as wide.

## Amp and cabinet

- New voicings: append to `AmpModel::Voicing` and `getVoicingNames()` (presets
  store the index), and add a matching cabinet to the table in
  `AmpRig::getMatchingCabinet()`. That table must have one entry per voicing.
- New built-in cabinets: append in `Cabinet.cpp`. Start the name with its
  layout, such as "2x12 ...", because the grill draws that many speakers (1 to
  4) from the first characters of the name.
- Match voicing is an app setting (`cab.matchVoicing`), not part of presets.
  Choosing a cabinet yourself, or loading an IR, turns it off.

## Style rules

- Hardware look, generic names: no real brand names, logos or look-alikes of
  specific products.
- Colours come from `Theme` (app chrome) and `pedal_look` (hardware). Avoid
  one-off colour values in panels.
- Every control needs a tooltip that says what it does and how to use it
  (drag, double-click to reset, right-click for more).
- Lights: green on bricks, red on stompboxes and the amp, as on real gear.
  Pedals that are off keep their colour but are faded, so they stay
  recognisable.
- Before merging a UI change, build it and look at it: on Linux run the app
  under Xvfb and take screenshots, and check the minimum window size too.
