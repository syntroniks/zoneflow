# Mounting

!!! warning "Written from the design; not yet verified on hardware"
    The enclosure exists as a model and has not been printed or hung on a
    wall. Dimensions come from `enclosure/params.scad` and the
    [enclosure page](../enclosure/index.md).

## Choose the place

- Indoors or in a garage, on a wall that stays dry. The enclosure is not weatherproof.
- Ambient -20 to +50 °C, no condensation. Not above a heater, not in full sun through a window.
- Within reach of a socket for a plug-in transformer, or of the transformer's installed position.
- Where the valve cable can reach the bottom of the enclosure, or come out of the wall behind it.
- At a height where you can read the display. The screen faces straight out, and the design assumes a viewer about half a metre away.
- Leave the vents clear.
- Leave room below the unit to take the wiring cover off and work on the plugs.

## Size

| Item | Value |
|---|---|
| Size on the wall | 120.2 x 177.5 mm |
| Depth | 33.7 mm at the fascia, 35.7 mm at the wiring cover, 47.7 mm at the knob |
| Wall fixing | Two keyholes 75 mm apart, and a third screw 123 mm below them, inside the wiring compartment |
| Screws | Up to 4.5 mm, heads up to 9 mm, with wall plugs to suit the wall |

## Drilling template

The enclosure build writes a 1:1 drilling template:

- [Drilling template, PDF](../enclosure/drill-template.pdf)
- [Drilling template, SVG](../enclosure/drill-template.svg)

![Drilling template preview](../enclosure/drill-template.svg){ width="240" }

## Steps

1. Print the PDF template at 100 %, with no "fit to page" scaling.
2. Measure the template's 100 mm line with a ruler. If it is not 100 mm, fix the printer settings and print again.
3. Tape the template to the wall where the unit will hang. Level it.
4. Mark and drill the three points. Fit wall plugs if the wall needs them.
5. Fit the two upper screws. Leave their heads 3 mm proud of the wall.
6. Decide where each cable comes in: the bottom entries, or one of the two 22 mm knock-outs in the back for a cable that comes out of the wall.
7. Cut the thin skin out of each bottom entry you will use, or knock out the back panel you need.
8. Hang the base on the two screws and slide it down 8 mm.
9. Fit the third screw through the slot inside the wiring compartment. It stops the unit lifting off.

The [assembly guide](../enclosure/assembly.md) puts the board, the display
and the fascia into the base first and hangs the unit in its step 5. The
steps above are that step 5 in more detail.

Next: [wiring](wiring.md).
