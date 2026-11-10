# Hardware

!!! warning "Design only"
    This hardware has not been built. Nothing on these pages has been
    bench-tested. The decisions below come from the
    [system specification](../design/system-spec.md) and may change when the
    schematic is drawn. Tracked in [issue #4](https://github.com/syntroniks/zoneflow/issues/4) (schematic and parts) and
    [issue #5](https://github.com/syntroniks/zoneflow/issues/5) (layout and fabrication outputs).

## Baseline decisions

| Block | Decision |
|---|---|
| MCU module | ESP32-S3-WROOM-1, PCB antenna, at least 8 MB flash |
| Output stage | One isolated AC switch per output. Baseline: opto-triac driver plus triac |
| Logic supply | Bridge rectifier, bulk capacitor, buck converter rated at least 60 V input, 3.3 V rail |
| USB | USB-C, native ESP32-S3 USB for flashing and logs |
| Protection | Fuse or PTC on AC input, surge clamp on AC input, clamp or snubber on every output, ESD/TVS on every field terminal |
| Current sense | One isolated measurement of total valve current |
| AC presence | A signal that tells firmware 24 VAC is present |
| RTC | I2C RTC with backup |
| Display | 128x64 monochrome OLED on I2C |
| Controls | Rotary encoder with push switch, plus one Back/Manual button |
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

Pluggable screw terminal blocks: 24VAC x2, COM x2, MV, Z1 to Z8, and sensor
inputs with ground. The final layout is not decided.

## Safety rules built into the design

1. Outputs are off by default. With the MCU in reset, unpowered, crashed or being flashed, every valve output is off. This is enforced in hardware, not only in firmware.
2. A valve output never shares an ESP32 strapping pin or a pin that glitches at boot.
3. A hardware output-enable gates all output drivers. Firmware must assert it deliberately.
4. Firmware enforces a per-zone maximum run time.
5. Only Class 2 circuits are on the board. No mains.

These are design rules. None has been verified on hardware.

## Pin map

The pin map will live in `hardware/pinmap.yaml` once the schematic exists.
Firmware reads pin numbers from one board header only.

## Pages

- [Schematic](schematic.md)
- [Bill of materials](bom.md)
- [Ordering from a fab](ordering.md)
- [Design notes](design-notes.md)
