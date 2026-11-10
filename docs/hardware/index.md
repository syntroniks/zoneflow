# Hardware

!!! warning "Design only"
    This hardware has not been built. Nothing on these pages has been
    bench-tested. The decisions below come from the
    [system specification](../design/system-spec.md). The circuit behind them,
    with part numbers and calculations, is in the
    [hardware design](../design/hardware-design.md). Tracked in [issue #4](https://github.com/syntroniks/zoneflow/issues/4) (schematic and parts) and
    [issue #5](https://github.com/syntroniks/zoneflow/issues/5) (layout and fabrication outputs).

## Baseline decisions

| Block | Decision |
|---|---|
| MCU module | ESP32-S3-WROOM-1-N16R8, PCB antenna, 16 MB flash, 8 MB PSRAM |
| Output stage | One triac per output, fired through an opto-triac. One 24 VAC leg is circuit ground and all triacs return through one current shunt |
| Logic supply | Half-wave rectifier, bulk capacitor, 100 V buck converter to a 5 V bus, linear regulator to 3.3 V |
| USB | USB-C, native ESP32-S3 USB for flashing and logs |
| Protection | Fuse and surge clamp on the AC input. Snubber, TVS and PTC on every output. TVS on every sensor terminal. A relay in the valve common |
| Current sense | One low-side shunt measures total valve current. A hardware comparator cuts drive on a short |
| AC presence | A signal that tells firmware 24 VAC is present |
| RTC | I2C RTC with coin-cell backup |
| Display | 2.0 to 2.4 inch colour IPS TFT, 320x240, SPI, no touch |
| Controls | Rotary encoder with push switch, plus Back and Manual/Home buttons |
| Expansion | One I2C connector (Qwiic/STEMMA QT pinout) |
| PCB | 2 or 4 layers, JLCPCB standard capabilities |

The logic supply input range follows from the supply requirement: the board
must survive 32 VAC RMS continuous, which is about 45 V peak (32 x 1.414).

## Electrical requirements

| Item | Requirement |
|---|---|
| Supply | 24 VAC nominal, Class 2 transformer, 50/60 Hz. Survive 32 VAC RMS continuous |
| Zones | 8 switched 24 VAC outputs |
| Master / pump | 1 additional switched 24 VAC output |
| Load per output | 0.5 A continuous, 1.5 A inrush for 100 ms |
| Total | Up to 2 zones plus master on at once (1.5 A total continuous) |
| Sensors | Rain sensor (dry contact, NO or NC). Flow meter (pulse, up to 200 Hz). One spare input |

## Terminals {#terminals}

Pluggable 3.81 mm screw terminal blocks, left to right: 24VAC x2, then COM x2,
MV and Z1 to Z4, then Z5 to Z8, then the sensor block 5V, FLOW, GND, RAIN,
GND, AUX, GND. The order and the reasons are in
[hardware design section 16](../design/hardware-design.md#16-connectors-and-test-points).

## Safety rules built into the design

1. Outputs are off by default. With the MCU in reset, unpowered, crashed or being flashed, every valve output is off. This is enforced in hardware, not only in firmware.
2. A valve output never shares an ESP32 strapping pin or a pin that glitches at boot.
3. A hardware output-enable gates all output drivers. Firmware must keep toggling a keep-alive signal or drive is removed.
4. Firmware enforces a per-zone maximum run time.
5. Only Class 2 circuits are on the board. No mains.

These are design rules. None has been verified on hardware.

## Pin map

The pin map is `hardware/pinmap.yaml`. Firmware reads pin numbers from one
board header only. The net on every component pin is in
`hardware/netlist.yaml`.

## Pages

- [Schematic](schematic.md)
- [Bill of materials](bom.md)
- [Ordering from a fab](ordering.md)
- [Design notes](design-notes.md)
