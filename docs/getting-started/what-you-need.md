# What you need

!!! warning "Written from the design; not yet verified on hardware"
    No ZoneFlow board has been built. Ratings on this page are design values
    from the [specification](../design/system-spec.md) and the
    [hardware design](../design/hardware-design.md), not measurements.

## The controller

- A ZoneFlow board, 8-zone or [4-zone](../hardware/four-zone.md) build, with its terminal plugs, the display panel and a CR2032 cell.
- The printed enclosure and its screws. See [printing](../enclosure/printing.md) and [assembly](../enclosure/assembly.md#what-you-need).

!!! note "To be completed"
    How you get a board is open: no board can be ordered yet. See
    [ordering boards](../hardware/ordering.md). Whether boards arrive with
    firmware on them will be decided with the first release,
    [issue #8](https://github.com/syntroniks/zoneflow/issues/8).

## Transformer { #transformer }

The board and every valve are powered from one transformer.

| Requirement | Value | Source |
|---|---|---|
| Type | Class 2, power-limited | Specification section 2, rule 5 |
| Output | 24 VAC nominal, 50 or 60 Hz | Specification section 1 |
| Output with no load | The board is designed to survive 32 VAC continuous | Specification section 1 |
| Size the design was checked for | 20 to 75 VA | Hardware design section 3.3 |
| Earthing | Output not connected to earth on either side | Hardware design section 5.3, and [safety](../safety.md#usb-and-earthing) |
| Shared use | Not shared with another controller or device, unless you have checked both use the same wire as ground | Hardware design section 5.3 |

To choose the size:

1. Look up the holding current of each valve in its data sheet.
2. Add up the valves that can be open at the same time, plus the master valve or the pump-start relay coil. The board allows at most two zones and the master at once. Programs open their zones one after another.
3. Multiply by 24 V. That is the valves' load in VA.
4. Add a margin for the board's own supply. The design calculates about 2 W typical and 5 W at peak ([hardware design section 4.2](../design/hardware-design.md#42-rail-budget)).
5. Choose a transformer rated above that total, within 20 to 75 VA.

Check the earthing before you install it:

1. Unplug the transformer.
2. Set the multimeter to continuity or the lowest resistance range.
3. Measure between each 24 VAC output terminal and the earth pin of the plug (or the earth terminal, for a hard-wired transformer).
4. Neither terminal should show continuity. If one does, read the [USB rules](../safety.md#usb-and-earthing) again: you must never connect a mains-powered computer by USB while that transformer is plugged in.

## Valves

- Standard 24 VAC solenoid valves. Not DC, not latching (pulse) valves.
- The design limits each output to **0.5 A continuous** and **1.5 A inrush for 100 ms**, and the total to **1.5 A continuous**.
- One valve per zone terminal is the normal case. Two valves on one terminal draw twice the current and can trip the current check (the default window per zone is 80 to 700 mA in both firmwares).
- The smallest load the design was checked for is a 50 mA pump-start relay coil.

## Master valve or pump-start relay (optional)

- A master valve: a 24 VAC valve on the main line, opened before any zone and closed after the last.
- Or a pump-start relay with a **24 VAC coil**. The relay's contacts switch the pump. Wiring the pump and the relay's mains side is outside this project and needs a qualified person.

## Sensors (optional)

| Sensor | Requirement |
|---|---|
| Rain sensor | A dry contact, normally closed or normally open. Most rain sensors open their contact when wet. |
| Flow meter | Pulse output up to 200 Hz: a dry contact (reed), an open collector, or a powered sensor that runs from 4.5 V or less and draws at most 100 mA. Its pulses per litre (K-factor) from its data sheet. |
| Spare input | Any dry contact. |

The 5V terminal gives about 4.8 V at 20 mA and 4.5 to 4.6 V at 50 mA. A
sensor that needs 5 V within 5 % at more than about 30 mA is not supported
([hardware design section 13.5](../design/hardware-design.md#135-sensor-supply-terminal)).

## Cable

| Run | Cable |
|---|---|
| Valves | Multi-conductor irrigation cable, rated for direct burial: one common wire plus one wire per zone. 0.82 mm² (18 AWG) is usual. |
| Transformer to controller | Two-wire, 0.82 mm² (18 AWG) or larger, within what the plug accepts. |
| Sensors | Two or three wires as the sensor maker says. |

The pluggable terminals take 0.33 to 1.3 mm² (22 to 16 AWG) wire, from the
plug maker's listing. The enclosure's two wide cable entries are sized for
18 AWG multi-conductor sprinkler cable, the two narrow ones for the
transformer lead and a sensor cable.

!!! note "To be completed"
    Cable length limits are not calculated. The design's short-circuit
    figures assume 30 m of 18 AWG pair. The voltage at a valve at the end of
    a long run will be measured on the prototype.

## Tools and small parts

- Multimeter with AC volts, resistance and continuity.
- Small flat screwdriver for the terminal screws.
- Wire stripper.
- Drill, wall plugs and three screws up to 4.5 mm with heads up to 9 mm.
- 2.5 mm cable ties, for the strain relief.
- Waterproof splice connectors for joins outdoors.
- A label 28 x 89 mm or 29 x 90 mm for the zone names on the wiring cover (optional).
- A phone with Bluetooth, or a computer with a Chromium browser and a USB-C cable, for setup.
