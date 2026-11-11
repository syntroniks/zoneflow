# Design notes

!!! warning "Calculations and simulations; not yet verified on hardware"
    Nothing here has been measured. Each calculation states its inputs so a
    reviewer can redo it.

The specification asks for a design-notes document with the calculations
for the power budget, the thermal check of the output devices, the
regulator input range and the protection parts. They are in the
[hardware design](../design/hardware-design.md). This page points to each.

| Calculation | Result in short | Section |
|---|---|---|
| Power budget | 3.3 V rail 162 mA typical, 498 mA peak; 5.3 V buck output 284 mA typical, 775 mA peak against a 1 A rating; 1.76 W typical and 4.8 W peak from the rectified supply, at an assumed 85 % efficiency | [4.2](../design/hardware-design.md#42-rail-budget) |
| Regulator input range | Rectified supply 26 to 31 V at 24 VAC, 44 V at 32 VAC with no load; the surge clamp holds it at 77.4 V; the buck regulator is rated 100 V | [3.1](../design/hardware-design.md#31-supply-rails-and-ground), [4.4](../design/hardware-design.md#44-surge-clamp-d1-and-rectifier-d2), [4.6](../design/hardware-design.md#46-buck-converter-u1) |
| Bulk capacitor | Ripple and life at peak load and low line | [4.5](../design/hardware-design.md#45-bulk-capacitor-c1) |
| Fuses | Steady load 51 % of rating; valve inrush 3 % and a zone short 22 % of the melting I²t; why time-lag | [4.3](../design/hardware-design.md#43-fuses-f1-and-f4) |
| USB and earthing | Every fault loop through a USB cable and which fuse is in it | [5.3](../design/hardware-design.md#53-the-ground-question) |
| Triac drive | Opto-triac LED current and gate current margins | [6.4](../design/hardware-design.md#64-drive-margins) |
| Snubber and commutation | dV/dt at turn-off with a solenoid load | [6.5](../design/hardware-design.md#65-dvdt-commutation-and-the-snubber) |
| Short circuit | Fault current for 20, 40 and 75 VA transformers; whether the triac, the PTC and the relay survive | [6.6](../design/hardware-design.md#66-surge-short-circuit-and-the-fault-limiter) |
| Thermal check of the output devices | Triac dissipation and junction temperature at 70 °C inside the enclosure | [6.7](../design/hardware-design.md#67-dissipation-and-junction-temperature) |
| Keep-alive detectors | Thresholds, timing from simulation, single-fault table | [7.3](../design/hardware-design.md#73-the-relay-keep-alive-detector), [8.3 to 8.5](../design/hardware-design.md#83-thresholds) |
| Over-current trip | Trip level and timing | [9](../design/hardware-design.md#9-hardware-over-current-path) |
| Current measurement | Resolution and error budget, about 4 % of reading | [10.5](../design/hardware-design.md#105-resolution-and-error-budget) |
| Sensor inputs | Filter timing, 24 VAC miswire, surge, sensor supply limits | [13](../design/hardware-design.md#13-sensor-inputs-and-sensor-supply) |
| Clock backup | About ten years on a CR2032, set by the cell's shelf life | [14](../design/hardware-design.md#14-real-time-clock-and-backup) |
| Backlight | Current from 24 VAC and from USB | [15.2](../design/hardware-design.md#152-backlight) |
| Enclosure temperature | 9 K rise at 2 W and 14 K at 3.5 W with vents, by estimate | [Mechanical design 11](../design/mechanical.md#11-for-the-circuit-designer), [enclosure](../enclosure/index.md#how-warm-it-gets-an-estimate) |

The simulation netlists are in
[`hardware/sim/`](https://github.com/syntroniks/zoneflow/tree/main/hardware/sim),
with a README that says what each run showed.

## What the review changed

[Hardware review A1](../design/hardware-review-a1.md) found one blocking, seven
major and sixteen minor issues in the first draft. Section 24 of the
hardware design records each decision. The main changes: a second keep-alive
detector for the relay, a fuse in each transformer wire, an 8 A triac with a
40 A PTC per output, and read-backs of both detectors.

!!! note "To be completed"
    Notes on the laid-out board (copper widths for the valve current paths,
    clearances, the spark gaps the specification asks for at the field
    terminals) come with the layout,
    [issue #5](https://github.com/syntroniks/zoneflow/issues/5).
