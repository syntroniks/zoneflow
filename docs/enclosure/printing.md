# Printing

!!! warning "Nobody has printed these parts"
    The settings below are what the parts were designed for. The times and
    weights are calculated from the model's volume, not from a slicer. Treat
    the first print as a test and expect to adjust the clearances in
    `enclosure/params.scad`.

The files are in `enclosure/output/`, as STL and 3MF, already in print
orientation. Put each one on the bed as it is.

## Parts

| File | Quantity | Size on the bed, mm | Volume, cm³ | Filament | Face on the bed |
|---|---|---|---|---|---|
| `zoneflow-base` | 1 | 120.2 x 172.0 x 32.2 | 71.5 | Body colour | The back |
| `zoneflow-fascia` | 1 | 120.2 x 131.7 x 27.5 | 41.5 | Front colour | The front face |
| `zoneflow-cover` | 1 | 120.2 x 69.9 x 13.9 | 19.5 | Front colour | The front face |
| `zoneflow-bridge` | 1 | 77.5 x 42.0 x 8.9 | 3.4 | Any | The deck, legs up |
| `zoneflow-knob` | 1 | 34.0 x 34.0 x 12.0 | 5.3 | Body colour | The top |
| `zoneflow-button-back` | 1 | 20.0 x 12.0 x 5.1 | 0.7 | Body colour | The flange |
| `zoneflow-button-home` | 1 | 20.0 x 12.0 x 5.1 | 0.7 | Body colour | The flange |
| `zoneflow-lightpipe` | 1 | 92.4 x 11.0 x 2.4 | 1.0 | Clear | Its side |

The `bridge` file is the display carrier. All parts fit a 220 x 220 mm bed.

## Material

- **PETG** for the base, fascia and cover. The inside reaches 60 to 70 °C in a hot garage at full load, by the estimate on the [enclosure page](index.md#heat-and-ventilation). PLA softens at about 60 °C, so use it only where the room stays cool.
- **Clear PETG** for the light-pipe comb. Natural or "transparent". Clear PLA also works for this part.
- **Two colours** look best: a light body for the base, knob and buttons, and a dark front for the fascia and cover. The renders use those. One colour throughout is fine.

## Settings

| Setting | Value | Why |
|---|---|---|
| Nozzle | 0.4 mm | Walls are multiples of 0.4 |
| Layer height | 0.2 mm | The 0.4 mm engraving and the 0.4 mm knock-out skins are two layers |
| Perimeters | 5 on the base, 3 elsewhere | The 2.0 mm side walls print solid |
| Top and bottom layers | 5 | 1.0 mm, so the 2.0 mm front skin is solid |
| Infill | 20 % gyroid | Only the bosses and pillars have any |
| Supports | None | |
| Brim | None. Add a 5 mm brim to the base if its corners lift. | |
| Bed surface | Smooth or satin sheet for the fascia and cover | The bed gives the front its finish |
| Light-pipe comb | 100 % infill, 0.1 mm layers, slow | Fewer voids, more light |
| Button caps | Ironing on the top layer | Their visible face is the last layer |

## Time and filament

Calculated, not sliced. Mass is the model volume times 1.27 g/cm³, which
assumes solid plastic and so reads high for the base. Time is the volume
divided by an average flow. Use the 5 mm³/s column for an older bed-slinger
and the 10 mm³/s column for a current fast printer.

| Part | Mass, g | Hours at 5 mm³/s | Hours at 10 mm³/s |
|---|---|---|---|
| Base | 91 | 4.0 | 2.0 |
| Fascia | 53 | 2.3 | 1.2 |
| Wiring cover | 25 | 1.1 | 0.5 |
| Display carrier | 4 | 0.2 | 0.1 |
| Knob | 7 | 0.3 | 0.1 |
| Button caps, both | 2 | 0.1 | 0.1 |
| Light-pipe comb | 1 | 0.3, printed slow | 0.3 |
| **Total** | **182** | **8.3** | **4.3** |

The base is the long one, at four hours on a slow printer. Its back wall is
1.6 mm thick to keep it there.

## Features to watch on the first print

None of these needs support. Each is a place where a printer that is out of
tune will show it.

- **Keyhole pockets in the base.** Their roofs bridge 12 mm.
- **Slits.** All are 1.4 mm wide and closed at one end by a 2 mm bridge: 24 slanted ones in each side wall, 22 in the top wall, 28 in the partition, 8 in the bottom wall.
- **The roof on the fascia.** With the fascia face down, the roof is a wall 27.5 mm tall, 2 mm thick and 104 mm long, tied to the rest by two bosses. If it wobbles or strings, slow the outer wall down.
- **Fuse well.** Four thin walls 10 mm tall standing on the fascia's inside.
- **Cable-tie tunnels.** 4 mm bridges.
- **Knock-out skins.** 0.5 mm in the bottom entries, 0.4 mm on the back. If they print with holes, raise `entry_web` or `rear_ko_skin`.
- **Fascia hooks and cover toes.** Their catches are 45 degree overhangs.
- **The lip of the fascia and cover.** It widens into the wall at 45 degrees.
- **Engraving.** It is on the bed, 0.4 mm deep: numbers, name and fuse rating on the fascia, a ring on the knob. A smooth sheet prints it best.
- **The knob's scallops** fade out above its foot over 2 mm, an overhang of under 30 degrees.
- **Screw holes that lie horizontal:** two through the top wall of the base and two through the fascia's roof into its pillars.

## Clearances

All are parameters in `enclosure/params.scad`. Change one, run `make`, and
the check tells you if something now collides.

| Parameter | Value, mm | Where |
|---|---|---|
| `fit` | 0.25 per side | Between two printed parts that slide together: lips in the base, buttons in the fascia |
| `comp_clear` | 0.4 per side | Around bought parts: connector openings in the partition |
| `display_fit` | 0.4 per side | Ribs around the display panel |
| `display_gap` | 0.5 | Between the glass and the bezel lip |
| `pcb_gap` | 1.1 | Board edge to side wall |
| `part_gap` | 0.4 | Board edge to partition |
| `hole_comp` | 0.2 | Reserved for small vertical holes that print tight |
| `knob_bore_fit` | 0.1 per side | Knob on the 6 mm D shaft. Push fit. |
| `lp_fit` | 0.1 per side | Light-pipe rail in its channel. Press fit. |
| `btn_flange_gap` | 0.6 | So that a cap never pre-loads its switch |

If the knob is loose, lower `knob_bore_fit`. If the fascia will not go into
the base, raise `fit`.

## Screws and inserts

The model has one parameter, `fastener`, with two values.

- **`"insert"`**, the default: seven M3 heat-set inserts, 5.7 mm long, in 4.0 mm holes. Press them in with a soldering iron at about 230 °C until flush.
- **`"selftap"`**: plain 2.6 mm pilot holes. The same M3 machine screws cut their own thread. Fine for a unit that is opened a few times. Use inserts if you expect to open it often.

| Where | Inserts | Screw |
|---|---|---|
| Board to base, lower corners | 2 | 2 x M3x6 pan head |
| Display carrier and board to base | 2 | 2 x M3x16 countersunk |
| Fascia, through the roof and the top wall | 2 | 2 x M3x12 countersunk |
| Wiring cover | 1 | 1 x M3x8 countersunk |

The 4.0 mm hole size is the common figure for this insert. I took it from a
supplier's listing as summarised by a search, not from a datasheet that I
opened. Check it against the inserts you buy. It is `insert_d`.

## Finishing

- **Fill the engraving.** Wipe white acrylic paint or a paint marker over the numbers, the name and the two button symbols, then wipe the surface clean. Without it they are dark on dark.
- **Cable entries.** Leave the skins in. Cut out only the ones you use, at installation.
- **Label.** A 28 x 89 mm or 29 x 90 mm address label fits the recess on the wiring cover. Write the zone names on it.

## Building the files yourself

```sh
cd enclosure
make openscad   # once: fetches the OpenSCAD snapshot the files were built with
make
```

To build the fascia and carrier for the 2.4 inch panel option, set
`panel = "2.4"` in `params.scad` first. The board is not laid out for that
panel; see the [mechanical design](../design/mechanical.md#73-a-24-inch-panel-later).

`make` needs OpenSCAD with the Manifold backend, which means a 2024 or later
build. The 2021.01 release does not have it. It also needs `uv` and Python 3.
