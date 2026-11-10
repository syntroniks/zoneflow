# Enclosure

!!! warning "Designed, not printed"
    The enclosure exists as a parametric model. Nobody has printed it and no
    board exists to put in it. What has been checked is that the modelled
    parts do not overlap each other or the placeholder board. See
    [what was checked](#what-was-checked). Tracked in
    [issue #9](https://github.com/syntroniks/zoneflow/issues/9).

![Front view](../assets/enclosure/render-front.png){ width="420" }
![Three-quarter view](../assets/enclosure/render-three-quarter.png){ width="420" }

The enclosure is for indoor or garage walls. It is not weatherproof. See
[safety](../safety.md).

- [Printing](printing.md): material, settings, orientation, time and filament for each part
- [Assembly](assembly.md): from printed parts and a board to a unit on the wall
- [Mechanical design](../design/mechanical.md): the contract with the PCB, for the layout engineer

## At a glance

| Item | Value |
|---|---|
| Size on the wall | 120.2 x 162.0 mm |
| Depth | 33.7 mm at the fascia, 36.2 mm at the wiring cover, 47.7 mm at the knob |
| Printed parts | 8: base, fascia, wiring cover, display carrier, knob, two button caps, light-pipe comb |
| Largest part | Base, 120.2 x 162.0 x 32.7 mm in print orientation |
| Supports | None |
| Screws | 7 M3 screws and 7 M3 heat-set inserts, or self-tapping bosses |
| Wall fixing | Two keyholes 75 mm apart and a third screw 113 mm below |

## The front

Top to bottom, the fascia has three rows and the wiring cover is below them.

- **Display and knob.** The 2.0 inch screen is on the left behind a chamfered window. The 30 mm knob is on the right at the same height. The margins left, right and above are 17 to 18 mm.
- **Two buttons under the screen.** Back is under the screen's left edge and Home / Manual under its right edge, so the bottom row of the screen can name them. The caps carry a chevron and a ring.
- **Ten lights.** Status on the left, then M for the master valve and 1 to 8, on a 9.5 mm pitch. The numbers are engraved.
- **Label.** A recess 90.5 x 29.5 mm under the lights takes a standard address label, 28 x 89 mm or 29 x 90 mm, with the zone names.

The screen is perpendicular to the wall. The panel is IPS with no preferred
viewing direction, so at chest to eye height it reads the same from above and
below. I did not tilt it. A tilted face would need the board tilted with it or
a longer display cable than the panel has.

### Display window: an open aperture

The panel sits 1.5 mm behind the front surface, behind a printed bezel that
covers its metal frame and inactive border. There is no clear window in front
of it.

A printed part cannot be optically clear, so a window would mean a piece of
1 mm acrylic that every builder has to cut to size. That is one more part to
source, and a second surface that reflects the room. The cost of leaving it
out is that the panel's polariser is exposed to fingers and to a careless
screwdriver. For an indoor controller that is touched a few times a year, I
accept that.

### Light pipes

The comb is one flat part printed in clear PETG. Ten square pipes, 2.4 mm
across, run from 0.7 mm above each LED to the front surface. Printed on its
side, the layers run along each pipe, which carries light better than layers
across it. The pipes share one rail, so a little light from a lit LED will
reach its neighbours. Whether that is visible, and whether 1.6 mA per LED is
bright enough, are bench questions.

## Two compartments

![Wiring cover removed](../assets/enclosure/render-wiring-open.png){ width="560" }

A partition separates the electronics above from the wiring below. The
terminal headers, the USB socket and the Qwiic socket pass through it with
0.4 mm of clearance. With the wiring cover off, an installer sees the four
plugs and nothing else of the board.

- **Wiring cover.** Two toes at its top edge hook into the fascia and one M3 screw holds the bottom. It stands 2.5 mm proud of the fascia because the screw plugs are taller than the space under the fascia. Its top edge laps 1.5 mm over the fascia, which hides the seam and holds the fascia's lower edge down.
- **Cable entries from below.** Four notches in the bottom wall: two 12.5 mm wide for 18 AWG multi-conductor sprinkler cable and two 7 mm wide for the transformer lead and a sensor cable. Each is closed by a 0.5 mm skin until you cut it out. The notches are open to the front, so a cable is laid in, not threaded through.
- **Cable entry from behind.** Two 22 mm knock-outs in the back wall.
- **Strain relief.** Behind each bottom entry is a cradle with a tunnel for a 2.5 mm cable tie. The tie goes round the cable jacket and through the tunnel.
- **Room for the wires.** 30 mm from the cradles to the plugs and 32 mm of depth. 18 AWG conductors need about 8 mm of bend radius.

### USB is behind the wiring cover

The USB-C socket is at the left end of the terminal row and points down into
the wiring compartment. To use it, take the wiring cover off.

- **It is a service port.** Firmware updates normally arrive over the network. USB is for the first flash and for logs.
- **It sits next to the 24 VAC plug.** The hardware design explains why connecting an earthed computer while 24 VAC is connected can short the transformer in some installations. Its advice is to pull the 24 VAC plug first. With USB behind the cover, that plug is the thing next to your hand.
- **No hole in the outside.** Nothing for dust, insects or a child's finger.

The electronics compartment stays closed for all of this.

## Fixing to the wall

Two keyholes take screws up to 4.5 mm with heads up to 9 mm. The unit hangs on
them and slides down 8 mm. A third screw goes through a slot inside the wiring
compartment and stops it lifting off. The build produces a 1:1 drilling
template, `enclosure/output/drill-template.pdf`.

Each keyhole opens into a closed pocket inside the base. A screw head cannot
reach the board and the keyhole does not open into the electronics.

## Heat and ventilation

About 2 to 3.5 W turns into heat inside, depending on what is running. The
triacs, shunt and regulator are in the lower half of the board. The module is
at the top.

There are 20 slits in each side wall, 1.4 mm wide and 16 mm long. Each one
slopes down towards the outside at 45 degrees.

- **Drips.** There is no opening in the top or the front. Water that runs down a side wall would have to climb 2 mm up a slit to get in.
- **Insects.** 1.4 mm stops wasps, flies and most spiders. It does not stop small ants. A narrower slit prints badly.
- **Air path.** Air enters the lower slits behind the board, rises past both faces of the board through the 1.1 mm gaps at its edges and the 10 mm gap above it, and leaves by the upper slits. The board is 18 mm off the back wall, so the rear space is one open channel.

### How warm it gets: an estimate

Nothing has been measured. This is a calculation with textbook coefficients,
and a reviewer can redo it.

- **Outside surface** of the electronics compartment that sheds heat: front 135 cm², sides and top 116 cm², half credit for the 135 cm² back because it faces the wall. About 0.032 m².
- **Outside film**: 9 W/m²K for natural convection plus radiation. That gives 3.5 K/W from the wall surface to the room.
- **Inside film**: I assume half the heat leaves the parts by convection into the air and half by radiation straight to the walls. With 4 W/m²K over 0.0385 m², the air sits 6.5 K above the walls per watt of convected heat.
- **Sealed box**: 3.5 x 0.9 + 6.5 x 0.5 gives about 6.4 K of air temperature rise per watt.
- **Slits**: 448 mm² per side. Taking the lower half as inlet and the upper half as outlet, 45 mm between their centres, a discharge coefficient of 0.5 and air 15 K above ambient, the flow is 33 cm³/s and it carries away 0.5 W.

| Heat inside | Air temperature rise, estimated |
|---|---|
| 1 W, idle with the backlight dimmed | 6 K |
| 2 W | 11 K |
| 3 W, three valves on | 16 K |
| 3.5 W, three valves on and backlight full | 18 K |

The circuit design assumes 10 K. By this estimate that holds up to about
1.9 W continuous and not at full watering load. The walls do most of the
cooling and the slits add about a sixth. The box's thermal time constant is
around half an hour, so a ten minute watering run does not reach these
figures and a two hour one does. The consequences for the triacs and the
module are in the [mechanical design](../design/mechanical.md#10-problems-found-with-chosen-parts).

If the bench shows it too warm, the changes are in this order: slits in the
top wall under a printed hood, then a taller electronics compartment.

## What was checked

Run `make` in `enclosure/`. It rebuilds the parameters from
`hardware/mechanical.yaml`, exports every part, renders the views and runs
the interference check.

- **Interference.** 45 pairs of solids are intersected in their assembled positions: the placeholder board, the display with its folded cable, the mated plugs and the promised component height zones, each against every printed part, and the printed parts against each other. Every pair overlaps by less than 0.5 mm³, except the knob and the encoder shaft, where the 35.5 mm³ is the flat that the placeholder shaft does not have. The report is `enclosure/output/check-report.md`.
- **Bed size.** Every part fits 220 x 220 mm in its print orientation.
- **Contract consistency.** The generator rejects a contract whose numbers contradict each other.

Not checked, because it needs a printer and a board:

- **Fit of any printed part** to another or to a real component.
- **Snap and press fits**: the fascia hooks, the cover toes, the knob on the shaft, the light-pipe rail.
- **Whether the display cable reaches** its connector.
- **Print quality** of the 45 degree features and the 1.4 mm slits.
- **Temperature, RF range and LED brightness.**

## What still looks weak

- **The lower third of the fascia is busy.** Lights, numbers and label sit close together. It is ordered, but it is not calm.
- **The engraved numbers are dark on dark** unless you fill them with paint, which the printing page asks for. The renders show them filled.
- **The step between fascia and wiring cover** is there because the plugs are tall, not because the design wanted it.
- **The knob is a plain fluted cylinder,** 12 mm tall on a shaft that stands 12 mm out of the fascia.
- **Two screw heads show on the top face** and one on the wiring cover.
- **The side slits are visible.** They are regular, but they are still slits.
