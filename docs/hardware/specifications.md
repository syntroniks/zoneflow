# Specifications

!!! warning "Design values; not yet verified on hardware"
    Every value on this page is a requirement from the
    [system specification](../design/system-spec.md) or a design value from
    the [hardware design](../design/hardware-design.md), mechanical
    contract or firmware. None is a measurement. The source column says
    where each comes from.

## Supply

| Item | Value | Source |
|---|---|---|
| Input | 24 VAC nominal, 50 or 60 Hz, from a Class 2 transformer | Spec 1 |
| Input range at load | 21.6 to 26.4 VAC | Design 3.3 |
| Highest input | 32 VAC RMS continuous, unloaded transformer at high line | Spec 1 |
| Transformer size checked | 20 to 75 VA | Design 3.3 |
| Board's own load from the rectified supply | About 1.8 W typical, 4.8 W peak, by calculation | Design 4.2 |
| Fuses | Two, one in each transformer wire: 3 A time-lag, 125 V, Littelfuse 154 holder with 454-series fuse; replacement 452 series, 0452003.MRL | Design 4.3 |
| Input surge clamp | 48 V bidirectional TVS, 1500 W | Design 4.4 |
| USB | USB-C, USB 2.0, device. Powers the logic, display and radio; not the valves or the sensor supply | Design 5 |

## Valve outputs

| Item | Value | Source |
|---|---|---|
| Zones | 8 (4 on the 4-zone build) | Spec 1 |
| Master valve or pump-start | 1 | Spec 1 |
| Switch | Triac, 8 A, 600 V, three-quadrant, fired by an opto-triac | Design 6 |
| Load per output | 0.5 A RMS continuous, 1.5 A RMS inrush for 100 ms | Spec 1 |
| Outputs on at once | Up to 2 zones and the master; 1.5 A total continuous | Spec 1 |
| Smallest load checked | 50 mA pump-start relay coil | Design 3.3 |
| Per-output protection | Resettable fuse (1.1 A hold, 40 A maximum), 48 V TVS, 47 ohm and 10 nF snubber | Design 6.1 |
| Valve common | Relay in the feed to the COM terminals; 48 V TVS on COM | Design 7 |
| Hardware over-current trip | 4.6 A peak nominal, 4.0 to 5.4 A with all tolerances; acts about 70 us after the current passes it | Design 9, 19.3 |
| Firmware current window per output | 80 to 700 mA by default, after 500 ms of inrush; short above 3000 mA RMS | Firmware defaults |
| Maximum run time per zone | 60 min by default, at most 4 h; rest time 30 min by default | Firmware defaults, spec 2 rule 4 |
| Keep-alive release | Gate drive gone 0.45 to 0.9 s after the last pulse; relay open within 0.95 s (simulation) | Design 8.4, 19.4 |

## Current measurement

| Item | Value | Source |
|---|---|---|
| Method | 0.1 ohm shunt in the shared triac return; two amplifier ranges; true RMS over 100 ms windows in firmware | Design 10, 19.2 |
| Error | About 4 % of reading plus a floor of a few milliamps, by calculation | Design 10.5 |
| 24 VAC detection | Square wave at mains frequency; loss reported after 25 ms with no edge | Design 11, 19.9 |

## Sensor inputs

| Item | Value | Source |
|---|---|---|
| Inputs | RAIN, FLOW, AUX, each with its own GND terminal | Design 13, 16.1 |
| Contact | Closed reads low; 10 kohm pull-up to 5 V, about 0.5 mA wetting current | Design 13.2 |
| Contact and cable resistance | Reads closed below 1.9 kohm | Design 13.2 |
| Flow pulse rate | Up to 200 Hz | Spec 1, design 13.2 |
| Protection | 48 V TVS on each terminal; survives 32 VAC applied to a signal terminal | Design 13.3 |
| Sensor supply (5V terminal) | Current-limited switch, 116 to 158 mA limit, PTC behind it; 100 mA usable; about 4.8 V at 20 mA, 4.5 to 4.6 V at 50 mA; off on USB power | Design 13.5 |

## Processor, display and controls

| Item | Value | Source |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N16R8: 16 MB flash, 8 MB PSRAM, PCB antenna | Spec 3.1 |
| Radio | WiFi 2.4 GHz, Bluetooth LE | Spec 1 |
| Display | 2.0 inch IPS TFT, 240 x 320, ST7789-class controller, SPI, no touch; Hansheng HS20HS072RX | Design 15.1 |
| Controls | Rotary encoder with push switch (30 detents), Back button, Manual/Home button | Design 15.4 |
| Lights | Status, master valve and one per zone (yellow-green); red over-current light on the board, not visible from outside | Design 15.5 |
| Clock | PCF8563 real-time clock, 32.768 kHz crystal, CR2032 cell; about ten years on the cell, set by the cell's shelf life | Design 14 |
| Expansion | Qwiic (JST SH 4-way, I2C, 3.3 V), 80 mA | Design 16.2 |
| Service header | 6 pads, 2.54 mm: 3V3, GND, TXD0, RXD0, EN, IO0 | Design 16.2 |

## Terminals

| Item | Value | Source |
|---|---|---|
| Type | Pluggable screw terminals, 3.81 mm pitch | Design 16.1 |
| Wire | 0.33 to 1.3 mm² (22 to 16 AWG), from the plug maker's listing | Design 16.1 |
| Blocks | J1 2-way (24V~), J2 7-way (COM, COM, MV, 1 to 4), J3 4-way (5 to 8), J4 7-way (5V, FLOW, GND, RAIN, GND, AUX, GND) | Design 16.1 |

## Mechanical and environment

| Item | Value | Source |
|---|---|---|
| Board | 114 x 110 mm, 1.6 mm, four layers, all surface-mount parts on the top side | Mechanical design 3 |
| Enclosure on the wall | 120.2 x 177.5 mm, 33.7 mm deep at the fascia, 47.7 mm at the knob | Enclosure |
| Mounting | Indoor or garage wall; two keyholes 75 mm apart and a third screw 123 mm below | Enclosure |
| Ambient temperature | -20 to +50 °C, non-condensing | Spec 1 |
| Air inside the enclosure | Up to 70 °C, the figure the parts are chosen for | Design 3.3 |
| Weatherproof | No | Spec 1 |

## Not specified yet

ZoneFlow has no safety, EMC or radio certification.

!!! note "To be completed"
    - Measured current draw, temperatures and radio range: after the first prototype (see [status](../status.md)).
    - Board weight and the final enclosure weight: after the first print, [issue #9](https://github.com/syntroniks/zoneflow/issues/9).
