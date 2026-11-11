# The 4-zone build

!!! warning "Design values; not yet verified on hardware"
    From [hardware design section 17](../design/hardware-design.md#17-the-4-zone-variant)
    and the KiCad variant `4-zone`. No board of either build exists.

The 4-zone build is the same PCB with parts left off. It has zones 1 to 4 and
the master valve output. Everything else is the same: the display, the
sensors, the fail-safe, the relay and the current measurement.

## What is not fitted

| Parts | Why |
|---|---|
| Channels 5 to 8: every part numbered 501 to 805, eleven per channel, 44 in all | The output circuits for zones 5 to 8 |
| J3 and its plug | The terminal block for zones 5 to 8 |
| R28 | The strap that tells firmware it is an 8-zone board |

Without R28, firmware reads the build as 4 zones at start-up and offers
zones 1 to 4 only. Both firmwares do this.

## Cost

From the [BOM estimate](../design/hardware-design.md#18-bill-of-materials-and-cost-estimate),
parts only, without PCB, assembly, extended-part fees, shipping, the coin
cell and the enclosure, at prices checked on 2026-11-10:

| Build | 10 boards | 100 boards |
|---|---|---|
| 8-zone | 40.77 USD | 31.84 USD |
| 4-zone | 34.48 USD | 27.04 USD |

## Firmware

- Standalone firmware: the same image runs on both builds.
- ESPHome: use `zoneflow-4zone.yaml` in place of `zoneflow.yaml`. The 8-zone file also works on a 4-zone board but shows four zones that never run.
- The web installer's ESPHome image is the 8-zone package; on a 4-zone board it offers zones 1 to 4.

## Assembly

The JLCPCB BOM for this build is
[`zoneflow-a-bom-jlcpcb-4-zone.csv`](zoneflow-a/zoneflow-a-bom-jlcpcb-4-zone.csv).
The enclosure is the same; the zone 5 to 8 lights and the space for J3 stay
empty.
