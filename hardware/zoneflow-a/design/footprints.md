# Footprints of ZoneFlow rev A: what was checked

No part has been bought and no board made. "Checked" on this page means one
thing: the pad positions, pad sizes, holes and pad numbers of the footprint
file were printed with KiCad's `pcbnew` module and compared, number by number,
with the drawing named in the same row. Nothing was compared with a physical
part. Checked on 2026-11-10 against the KiCad 10.0.8 libraries.

Frame used below: millimetres, x to the right, y down, seen from the component
side, the same as KiCad's footprint editor.

Datasheets are the copies LCSC serves for the ordering code in the BOM,
`https://www.lcsc.com/datasheet/<LCSC number>.pdf`, unless another source is
named.

## 1. Project-local footprints

Written by `design/footprints.py` into `lib/ZoneFlow.pretty`. A footprint is
local only where KiCad has none for the ordering code, or where the stock one
does not match the maker's drawing.

### 1.1 Terminal headers J1, J2, J3, J4: `KF2EDGR-3.81-2P`, `-4P`, `-7P`

Cixi Kefa KF2EDGR-3.81-2P (C441182), -4P (C441184), -7P (C441187).
Drawing: Kefa "KF2EDGR-3.81", revision A, dated 2021-03-13.

| Dimension | Drawing | Footprint |
|---|---|---|
| Pitch | 3.81 ± 0.03 | 3.81 |
| PCB hole | 1.40 +0.10 / -0 | 1.40 drill, 2.30 pad |
| Pin | 0.80 square | (fits the hole with 0.27 clearance on the diagonal) |
| Body length | poles x 3.81 + 0.80 | 8.42, 16.04, 27.47 |
| Body depth | 9.20 | 9.20 |
| Pin row to rear face | 1.30 | 1.30 |
| Height | 7.00 | not in the footprint; the mechanical contract carries it |

Pad 1 is square and is the origin. The mating face points to +y.

The courtyard is 0.25 mm around the body, not the 0.5 mm that is usual for a
connector: the mechanical contract puts J2 and J3 end to end with 0.6 mm
between the bodies.

Why not the stock Phoenix MC 1,5/x-G-3,81 footprint, which is the usual
substitute: its holes are 1.2 mm, 0.2 mm smaller than the Kefa drawing asks
for, and its body outline differs.

Not checked: the drawing does not say which end is pin 1. The footprint counts
from the left, seen from the mating face with the pins down. Only the
silkscreen legend depends on it.

3D model: the Phoenix MC header of the same pole count, as a stand-in.

### 1.2 Display connector J7: `FPC_XUNPU_FPC-05F-12PH20`

XUNPU FPC-05F-12PH20 (C2856799), 12-way, 0.5 mm, bottom contact, flip lock.
Drawing: XUNPU "FPC 0.5PH H2.0", part FPC-05F-NPH20, revision A, page 1 of
`2109221030_XUNPU-FPC-05F-12PH20_C2856799.pdf`.

| Dimension | Drawing | Footprint |
|---|---|---|
| Signal lands | 0.30 x 1.25 at 0.50 pitch | 0.30 x 1.25 at 0.50 |
| Mounting lands | 2.00 x 2.50 | 2.00 x 2.50 |
| Mounting land outer edge beyond the end land centre | 2.69 | 2.69 |
| Mounting land near edge to the land row | 0.70 | 0.70 |
| Body | (N - 1) x 0.5 + 4.90 = 10.40 long, 5.12 deep, 2.00 high | 10.40 x 5.12 |
| FPC | (N + 1) x 0.5 = 6.5 wide, 0.30 thick, 3.00 contact length at least | (not in the footprint) |

Orientation follows the mechanical contract: the FPC enters from +x, pad 1 is
at +y. The distributor listing says "Bottom Contact, Flip-Top".

Not in the drawing: the position of the body against the lands. The footprint
puts the rear edge of the body 0.305 mm in front of the land centres. That
figure is from the footprint in LCSC's own library for C2856799, which was used
for this one number only, and it agrees with the drawing as far as a drawing
can be scaled. **The reviewer should check this against a part**, because the
mechanical contract fixes the mouth of the connector (x = 63.0) and not the
lands.

Not checked here: which end of the connector the panel's FPC calls pin 1. The
design document, section 16.4, argues it from the panel drawing. The mounting
lands have no net.

No 3D model.

### 1.3 Coin cell holder BT1: `BatteryHolder_QJ_CR2032-BS-6-1`

Q&J CR2032-BS-6-1 (C70377). Drawing: Q&J "BS-6", dated 2011-02-14 (the copy
JLCPCB serves for C70377).

| Dimension | Drawing | Footprint |
|---|---|---|
| Lands, over the outer edges | 32 +0.2 / -0 | 32.0 |
| Lands, between the inner edges | 26 +0 / -0.2 | 26.0 |
| Land width | 4.2 | 4.2 |
| Housing | 22.1 x 16, 5.5 high | 22.1 x 16.0 |
| Over the terminals | 28.5 and 31.3 | drawn at 31.3 |

Pad 1 is the positive contact. The drawing marks the right-hand land "+" in
its top view; the footprint has pad 1 at +x.

No 3D model.

### 1.4 Encoder SW3: `RotaryEncoder_Alps_EC11J15`

ALPS ALPINE EC11J1525402 (C209762). Drawing: ALPS product sheet "EC11J
Series", 2018-03-30, land pattern figure for the reflow type.

| Dimension | Drawing | Footprint |
|---|---|---|
| Terminal lands | five, 1.3 x 2.4 | 1.3 x 2.4 |
| A, C, B | 2.5 pitch, outer edges 8.5 from the shaft axis | x = -2.5, 0, +2.5; y = 7.3 |
| Switch terminals | 5.0 apart, outer edges 8.2 from the axis | x = -2.5, +2.5; y = -7.0 |
| Corner frame lands | 2.5 x 3.2; 7.5 between and 12.5 over; 12.6 between and 19 over the rows | 2.5 x 3.2 at (±5.0, ±7.9) |
| Side frame lands | 3.75 x 4; 9 between, 16.5 over | 3.75 x 4.0 at (±6.375, 0) |
| Centre hole | 2.1 | 2.1, unplated |
| Locating hole, EC11J15 type | 1.6 ± 0.05 at 4.5 from the axis | 1.6, unplated, at (0, 4.5) |

Pads `S1` and `S2` are the switch terminals, which the drawing letters D and E.

Could not be read: the drawing is a 512-pixel image. It marks four areas
beside the side lands "do not solder and wiring". Their extent along x is not
legible. The footprint has four keep-out areas on the top copper, x from 3.75
to 6.25 and y from 2.75 to 5.0 on each side, which is an estimate. **The
reviewer should check them against a better copy of the drawing.**

The six frame lands have no pad number and no net. Grounding the frame is the
usual practice; the design netlist does not say, so it is left for the review.

The courtyard is 0.1 mm beyond the side lands, not 0.25 mm. The mechanical
contract puts the encoder at x = 85.0 and the module at x = 103.0. The
right-hand side land then ends at x = 93.25 and the module's pad row starts
at x = 93.5: **0.25 mm of bare board between a frame land and fourteen module
pads.** It passes DRC. It is close for reflow soldering, and it is a position
the contract fixes; see the layout document.

3D model: KiCad's EC11E (the through-hole relative with a 20 mm shaft), as a
stand-in. Body height and shaft are close; the terminals are not.

### 1.5 Buttons SW4, SW5: `SW_Push_XKB_TS-1102S`

XKB TS-1102S-C-M-B (C381050), 6 x 6 mm, 10 mm high. Drawing: XKB
"TS-1102S-C-M-B", revision A1, dated 2025-06-03.

| Dimension | Drawing | Footprint |
|---|---|---|
| Lands | 2.60 x 1.40 | 2.60 x 1.40 |
| Along the terminals | 12.00 over, 6.80 between | 12.00, 6.80 |
| Across the rows | 5.90 over, 3.10 between | 5.90, 3.10 |
| Body | 6.00 x 6.00, 3.30 high; actuator 3.50; total height, code M, 10.00 | 6.00 x 6.00 |

Terminals 1 and 2 are joined inside the switch, 3 and 4 likewise, and the
contact is between the two pairs. Pad 1 is terminals 1 and 2 (one row), pad 2
is terminals 3 and 4.

No 3D model.

### 1.6 Bulk capacitor C1: `CP_Radial_D12.5mm_L20mm_P5.00mm_LyingDown`

Rubycon 63ZLJ470M12.5X20 (C2940725). Rubycon ZLJ series sheet: case 12.5 x 20,
13.0 diameter and 22.0 long at most, lead pitch 5.0, lead diameter 0.6.

| Dimension | Source | Footprint |
|---|---|---|
| Lead pitch | 5.0, series sheet | 5.0 |
| Hole | 1.0, mechanical contract | 1.0 drill, 2.0 pad |
| Can | 13.0 x 22.0 at most | 13.0 x 22.0, starting 4.25 from the holes |

Pad 1 is positive and square. The can lies along +x from the leads. The 4.25 mm
between the holes and the can is the lead bend allowance the mechanical
contract uses; Rubycon's sheet gives no forming dimensions for this part, so
the leads are bent by hand. The part goes on the bottom side.

The mechanical contract was written for the Rubycon PX part of the same case
size, 21.5 mm long at most. The ZLJ part is 0.5 mm longer at most and still
fits the contract's zone `B_C1`: the can ends at x = 4.25 + 22.0 from the
holes, the zone allows 26.5.

No 3D model for a part lying down.

### 1.7 Mounting holes H1 to H4: `MountingHole_3.4mm_M3_Keepout9mm`, `...Keepout7mm`

From `hardware/mechanical.yaml`, "holes": 3.4 mm unplated. The courtyard on
each side is the contract's component keep-out: 9.0 mm on the top at holes A
and B, 7.0 mm at C and D, 10.0 mm on the bottom.

### 1.8 Module U4: `ESP32-S3-WROOM-1`

Espressif ESP32-S3-WROOM-1-N16R8 (C2913202). Module datasheet, figure
"Recommended PCB land pattern" (page 30 of the copy LCSC serves).

A copy of KiCad's `RF_Module:ESP32-S3-WROOM-1`, pads unchanged, after this
comparison:

| Dimension | Datasheet | KiCad footprint |
|---|---|---|
| Module | 18 x 25.5 | 18.0 x 25.5 |
| Lands | 1.5 x 0.9 at 1.27 pitch | 1.5 x 0.9 at 1.27 |
| Between the two side rows | 17.5 over the land centres | ±8.75 |
| First land row below the antenna end | 7.49 | 7.49 |
| Bottom row, first land from the side | 2.015 | 2.015 |
| Ground pad | nine 0.9 squares in 3.7 x 3.7, centred 7.5 from the left edge and 10.29 from the bottom edge | the same |
| Antenna area | 6 mm at the end of the module | keep-out 15 mm around the antenna end |

Two changes. The stock keep-out covers the outer copper layers; the copy puts
it on every copper layer, because this is a four-layer board and the inner
planes must stay out from under the antenna too. And the courtyard beside the
pad rows is 0.1 mm beyond the pads, not 0.25 mm, for the reason given under
the encoder (1.4).

3D model: KiCad's own.

### 1.9 Opto-triacs U101 to U901: `SMDIP-6_onsemi_646BY`

onsemi MOC3023SR2M (C40701). Drawing: onsemi case 646BY, "PDIP6 8.51x6.35,
2.54P", issue A, 15 July 2019.

A copy of KiCad's `Package_DIP:SMDIP-6_W9.53mm` with a larger courtyard. The
lands were compared in section 2 and are unchanged. The stock courtyard is
8.17 mm along the body. The drawing gives the body length, dimension D, as
8.13 to 8.89 mm, so two parts placed courtyard to courtyard could touch. The
courtyard here is 8.89 + 2 x 0.25 = 9.39 mm long. Found when the nine
opto-triacs were put in a row on the 9.5 mm pitch of the LEDs.

| Dimension | Drawing | Footprint |
|---|---|---|
| Body length D | 8.13 to 8.89 | courtyard 9.40 |
| Body width E1 | 6.10 to 6.60 | (stock outline) |
| Over the leads E | 8.43 to 9.90 | lands to 11.53, courtyard 12.08 |
| Height A | 4.80 at most | in `heights.toml` |

3D model: KiCad's own.

## 2. Stock KiCad footprints compared with the maker's drawing

Parts that carry mains-side current, heat or a connector position.

| Part | Footprint | Drawing | Result |
|---|---|---|---|
| F1, F4 fuse holder, Littelfuse 0154003.DRT (C206913) | `Fuse:Fuseholder_Littelfuse_Nano2_154x` | Littelfuse 154 series, recommended pad layout | Equal: 10.62 over the lands, lands 4.24 x 3.81, gap 2.13 |
| F5 fuse, Littelfuse 0451.500MRL (C142836) | `Fuse:Fuse_Littelfuse-NANO2-451_453` | Littelfuse 451/453 series sheet, "Recommended pad layout", read as an image | Equal: lands 1.96 x 3.15, 2.95 between them, 6.86 over them (the footprint's pad centres give 6.87). The part is 6.10 x 2.69 |
| Q101 to Q901 triac, ST T810-600B (C155588) | `Package_TO_SOT_SMD:TO-252-2` | ST DocID7472 rev 12, figure 14, DPAK footprint | Lead pitch 4.572 against 4.56. ST draws larger lands: tab 6.7 x 6.7 against 6.4 x 5.8, leads 3.0 x 1.6 against 2.2 x 1.2. Kept the stock lands; the layout adds copper around the tab for heat. Pad 1 is A1, pad 2 (tab) is A2, pad 3 is the gate: with the leads pointing down, pad 1 is on the left, as in ST's top view |
| U101 to U901 opto-triac, onsemi MOC3023SR2M (C40701) | lands of `Package_DIP:SMDIP-6_W9.53mm`, used through the local copy of 1.9 | onsemi case 646BY, issue A, land pattern | Pitch 2.54, equal. Inner edges 7.53 apart against 7.49. KiCad's lands are 0.5 longer outwards (2.0 x 1.78 against 1.52 x 1.78) |
| K1 relay, Omron G5NB-1A-E DC5 (C48746) | `Relay_THT:Relay_SPST_Omron_G5NB` | Omron G5NB data sheet (`omronfs.omron.com/en_US/ecb/products/pdf/en-g5nb.pdf`), "PCB mounting holes (bottom view)" and "Terminal arrangement" | Equal: four holes 1.1; coil terminals 4.7 apart; contact terminals 11.5 and 18.5 from the coil, in line with one coil terminal. Coil is pads A1 and A2, contact is 13 and 14. No coil polarity |
| U1 buck converter, TI LM5164DDAR (C477928) | `Package_SO:TI_SO-PowerPAD-8` | TI LM5164 data sheet rev A: package drawing 4202561/F, thermal pad 4206322-6/L, land pattern 4208951-6/D | Thermal pad 2.40 x 3.10 at most; the footprint's pad 9 is 2.41 x 3.10. Lead lands: TI 0.5 x 2.2 on 5.75 row centres, KiCad 0.61 x 1.91 on 5.56. Pitch 1.27, equal. TI's example has six 0.33 vias under the pad on a 1.3 grid; the footprint has none, the layout adds them |
| L1, Bourns SRN6045TA-470M (C5151734) | `Inductor_SMD:L_Bourns_SRN6045TA` | Bourns SRN6045TA sheet, recommended layout | Equal: 6.5 over the lands, 5.1 high, 1.8 gap |
| F101 to F901 PTC, BHFUSE BSMD2920 (C3020148) | `Fuse:Fuse_2920_7451Metric` | BHFUSE BSMD2920 sheet, recommended pad | Maker: 2.00 x 5.30, gap 4.60. KiCad: 1.925 x 5.45, gap 4.85. Within 0.15 on every edge |
| F3 PTC, Littelfuse 1812L020/60 (C315900) | `Fuse:Fuse_1812_4532Metric` | Littelfuse 1812L series sheet, pad layout | Gap 3.15, equal. Height 3.45 against 3.40. Littelfuse draws wider lands, 1.78 against 1.125: the stock land still extends 0.33 mm or more past the end of the part (4.73 long at most) |
| F2 PTC, Littelfuse 1206L012 (C151160) | `Fuse:Fuse_1206_3216Metric` | Littelfuse 1206L series sheet, pad layout | Maker: 1.00 x 1.80, gap 1.80. KiCad: 1.25 x 1.75, gap 1.55 |
| TVS SMBJ48CA (Hongjiacheng C19077591 since 2026-11-12, Littelfuse C151264 before): D12 to D14, D18, D23, D101 to D901. TVS SMBJ6.0A (Littelfuse C83270), same case: D25 | `Diode_SMD:D_SMB` | Littelfuse SMBJ sheet, solder pad table. Hongjiacheng SMBJ sheet rev 1.1, package outline and pad figures | The table gives I 2.26 min, J 2.16 min, K 2.72 max, L 2.16 min (converted from inches). KiCad's lands are 2.5 x 2.3 with 1.8 between them, which meets all four whichever side each letter names; the figure that assigns the letters was not read. Hongjiacheng: body 4.25 to 4.85 x 3.30 to 3.94, over the leads 5.08 to 5.59, lead 1.85 to 2.21 wide, against Littelfuse 4.06 to 4.75 x 3.30 to 3.94, 5.21 to 5.59 and 1.93 to 2.20; its pad figures are M 2.26 min, J 2.10 min, K 2.74 max, which the same lands meet. The lands span 6.8 mm, 1.2 mm more than the longest part |
| D1 TVS, SMCJ48CA (Hongjiacheng C41411765 since 2026-11-12, Littelfuse C151287 before) | `Diode_SMD:D_SMC` | Hongjiacheng SMCJ sheet rev 2.2 and Littelfuse SMCJ sheet, package outlines | The two parts against each other, not against the lands: body 6.60 to 7.11 x 5.59 to 6.22 in both, over the leads 7.75 to 8.13 in both, lead 2.85 to 3.27 wide against 2.90 to 3.20. The pad figures of neither sheet were compared with the footprint. JEDEC DO-214AB footprint, lands 3.3 x 2.5 |
| J5, HRO TYPE-C-31-M-12 (C165948) | `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | HRO drawing TYPE-C-31-M-12, 2020-12-08, recommended layout | Equal: four 0.60 and eight 0.30 lands at 0.50 pitch; locating holes 5.78 apart; shell slots 8.65 apart (footprint 8.64) and 4.18 between the rows. The drawing puts the board edge 2.66 mm in front of the footprint origin, which is where the mechanical contract puts J5 (2.675) |
| J6, JST SM04B-SRSS-TB (C160404) | `Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal` | JST SH catalogue sheet, side entry type | Lands 0.6 x 1.55 at 1.0 pitch and mounting lands 1.2 x 1.8, as the sheet gives them. Positions were not compared number by number: the text layer of the sheet does not tie the numbers to features. In this footprint the mounting lands reach 0.15 mm past the front of the housing, so the housing cannot overhang the board edge: revision 2 of the mechanical contract had it 0.5 mm over, which put the lands 0.7 mm off the board. Revision 3 moves J6 in by 1.0 mm |
| SW1, SW2, XKB TS-1187A-B-A-B (C318884) | `Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A` | XKB drawing TS-1187A-X-X-X, revision A0 | Equal: lands 1.0 x 0.75; 7.0 over and 5.0 between along the terminals; 4.5 over and 3.0 between the rows. The two lands of a row are one contact |

## 3. Stock footprints for standard packages

Chip resistors and capacitors (0603, 0805, 1206, 1210, 2512), the 4 x 0603
resistor array, 0603 LEDs, SOD-123, SMA, SOT-23, SOT-23-6, TSOT-23-6, SOT-223, SOIC-8,
SOIC-14, TSSOP-24 and the 3.2 x 1.5 mm crystal use KiCad's footprints for the
package named in the maker's datasheet. Their land dimensions were **not**
compared one by one with each maker's drawing. What was checked for these is
the pin numbering:

- The design netlist carries the pin number of every IC pin ("3 EN/UVLO"), and
  the build fails unless KiCad's netlist puts the same net on the pad of that
  number.
- Diodes: pad 1 is the cathode in KiCad's SMA, SMB, SMC and SOD-123
  footprints. `design/parts.toml` maps K to 1 and A to 2.
- LEDs: pad 1 is the cathode in KiCad's `LED_0603_1608Metric`. Mapped the same
  way. All eleven are the KENTO KT-0603R since 2026-11-12. Its drawing
  numbers the anode 1 and the cathode 2 and marks the cathode end; its
  suggested lands are 0.70 long with 0.70 between them. The cathode mark on
  the reel was not checked against the pick-and-place rotation; `make
  cpl-check` compares the placement with JLCPCB's library footprint.
- MOSFETs AO3400A and AO3401A in SOT-23: gate 1, source 2, drain 3, KiCad's
  numbering for these two symbols. The Alpha and Omega sheets show the
  positions as a picture without numbers: drain alone on one side, gate and
  source on the other, which is the same arrangement.
- TLV803EA29DBZR (TLV803EA30DBZR until 2026-11-12, the same package and
  pins): the ordering code has no pinout letter, which the TI sheet defines
  as pin 1 GND, pin 2 RESET, pin 3 VDD.
- BAT54S: pin 1 anode, pin 2 cathode, pin 3 the common node, from the
  Nexperia sheet, table 2.
- LDL1117S33R in SOT-223: 1 GND, 2 OUT, 3 IN, tab OUT, from the ST datasheet.
- TPS3701DDCR (U13, C132788) in TI's DDC package, which TI calls SOT-23-thin
  and refers to JEDEC MO-193: `Package_TO_SOT_SMD:TSOT-23-6`. This one was
  compared, against TI drawing 4214841/E in data sheet SBVS240C. Pitch 0.95
  and land width 0.6, equal. TI draws lands 1.1 long on row centres 2.7
  apart, 3.8 over the lands and 1.6 between the rows. KiCad's are 1.325 long
  on 2.275 centres, 3.6 over and 0.95 between. The part is 2.75 to 3.05 over
  its leads with feet of 0.3 to 0.6, so the stock lands reach 0.275 mm or
  more past the lead tips and under the whole foot. Kept the stock lands.
  Pins from the same sheet: 1 OUTA, 2 GND, 3 INA, 4 INB, 5 VDD, 6 OUTB.

## 4. Parts with no footprint

- DS1, the display panel HS20HS072RX, plugs into J7. It is in the schematic
  and the BOM and not on the board.
- TP1 to TP30 are bare 1.5 mm pads, `TestPoint:TestPoint_Pad_D1.5mm`.
- J8 is a row of six holes at 2.54 mm for a debug header that is not fitted.
- JP1 is KiCad's open solder jumper, two pads.

## 5. Open points for the reviewer

1. J7: body position against the lands is not dimensioned by the maker (1.2).
2. SW3: the no-wiring areas are an estimate, and the frame lands have no net
   (1.4).
3. J6 mounting lands (`MP`) have no net.
4. Triac lands are KiCad's, smaller than ST's figure (section 2). Heat
   spreading is a layout matter and is reviewed there.
5. Stand-in 3D models: terminal headers and encoder. No model: J7, BT1, SW4,
   SW5, C1. The STEP export is therefore not a basis for an enclosure
   clearance check for those parts; the mechanical contract's height zones
   are.
