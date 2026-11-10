# ZoneFlow hardware design, revision A

Status: **paper design, 2026-11-10**. No board exists. Nothing in this document
has been measured. Every number is a datasheet figure, a calculation from
datasheet figures, or a simulation result, and the text says which. Section 20
lists what only a bench can confirm. Section 22 lists what I could not confirm
at all.

This document is the circuit design that schematic capture translates. I
wrote it against [system specification](system-spec.md) v0.3. It departs from
that version in the places listed in section 2, and the pull request that adds
this document raises the specification to v0.4 with those changes. Three files hold the same design as data:

- **`hardware/netlist.yaml`**: every component, its value, package, LCSC number and the net on every pin. If a table here disagrees with it, the file is right.
- **`hardware/pinmap.yaml`**: every ESP32-S3 GPIO, I2C address and expander bit. Firmware generates its board header from it.
- **`hardware/bom-preliminary.csv`**: one line per part number with stock, price and source, checked on 2026-11-10.

Simulation netlists are in `hardware/sim/`, with a README that states what each
run showed.

## 1. The design in brief

| Block | Choice | Main reason |
|---|---|---|
| Logic supply | Fuse in the ground leg, 1500 W TVS, half-wave rectifier, 470 uF, LM5164 100 V buck to 5.3 V, Schottky OR with USB, LDL1117 to 3.3 V | The TVS clamps at 77.4 V. A 100 V regulator survives that with no further argument. A 5 V bus makes USB and 24 VAC equal sources and gives the LDO room to reject ripple. |
| Output switch | T410-600B-TR 4 A three-quadrant triac in DPAK, fired by a MOC3023 opto-triac from its own T2 | The triacs JLCPCB stocks with real surge ratings are three-quadrant parts. Positive-only gate drive from logic cannot fire them on the negative half-cycle. Section 6 has the analysis. |
| Output control | TCA9539 I2C expander sinks the opto LEDs | The TFT took six GPIOs. Nine valve pins no longer fit. The expander is held in reset by the fail-safe circuit, so no valve bit survives a crash. |
| Fail-safe | Diode pump on the keep-alive pin, comparator with hysteresis, switched LED supply, expander reset | Gate drive needs an edge at least every 20 ms. It is gone 0.45 to 0.9 s after the edges stop. |
| Over-current | Window comparator on the shunt amplifier, NAND latch, same switch as the fail-safe | Removes drive about 70 us after a short begins. The triac stops at the next current zero if the fault current is inside its commutation rating. K1 opens within 10 ms either way. |
| Valve common | Relay in the COM feed, driven only while drive is enabled | A shorted triac or TVS otherwise leaves a valve on with no way to stop it. This is an addition to the specification. |
| Current measurement | 0.1 ohm shunt, two difference amplifiers biased at 0.8 V, sampled by ADC1 | True RMS in firmware. No precision rectifier, which would add millivolts of error to a 5 mV signal. |
| Display | HS20HS072RX 2.0 inch 240x320 IPS panel on a 12-way FPC connector, plugged in at final assembly | The only bare ST7789 panels with stock and a drawing that I found at LCSC. |
| RTC | PCF8563T with a 32.768 kHz crystal, CR2032 in a holder | JLCPCB stocks no supercapacitor. The coin cell lasts longer than its own shelf life. |

## 2. Where this design departs from the specification

The specification stays the source of truth. This pull request changes it for
each item below, and the reasons are in the sections named.

1. **Gate drive goes through opto-triacs, not straight from logic.** Specification 3.1 says gate drive comes from ESP32 GPIO through the fail-safe gate. The triac still sits in the OpenSprinkler topology, with one AC leg as ground and a shared shunt. Only the gate coupling changes. Section 6.2.
2. **Valve outputs are on an I2C expander.** Specification 3.1 says the module has enough GPIO without an expander. With the SPI display, two I2C buses, five control inputs and the fail-safe signals, 25 usable pins are not enough for nine direct outputs. Section 12.2.
3. **A relay in the valve common.** Not in the specification. It gives hardware a way to remove power from every valve when an output device has failed short. I propose a new safety rule 6. Section 7.
4. **The fuse is in the ground leg.** The specification says "fuse on AC input" and does not say which leg. Only a ground-leg fuse sits in the fault loops that run through a USB cable. Section 5.3.
5. **No rectifier in the current-sense front end.** Specification 3.1 asks for a rectifying, averaging front end. This design amplifies the AC waveform around a 0.8 V bias and lets firmware compute RMS. Section 10.
6. **5 V bus with an LDO.** Specification 3.1 says "buck converter, 3.3 V rail". The buck makes 5.3 V and an LDO makes 3.3 V. Section 4.6.

## 3. Conventions

### 3.1 Supply rails and ground

| Net | What it is |
|---|---|
| `AC_L` | 24 VAC terminal 1. Feeds the rectifier, the zero-cross detector and, through relay K1, the valve common. |
| `AC_N` | 24 VAC terminal 2. Connects to `GND` through fuse F1. |
| `GND` | Circuit ground. One transformer leg, the USB ground and the sensor ground are all this net. |
| `COM` | Valve common terminals. `AC_L` after relay K1. |
| `VIN_DC` | Rectified, unregulated supply. 26 to 31 V at 24 VAC, 44 V at 32 VAC with no load. |
| `V_BUCK` | Buck output, 5.28 V nominal. Present only with 24 VAC. |
| `V5` | 5 V bus after the OR diodes. About 4.95 V from 24 VAC, 4.1 to 4.9 V from USB. |
| `3V3` | Logic rail, 3.3 V, 3 % over temperature. |
| `V_DRV` | 3.3 V supply of the opto LEDs. On only while the fail-safe circuit allows drive. |
| `ISNS` | Common T1 node of all triacs. The shunt R37 connects it to `GND`. |

### 3.2 Reference designators

Components that exist once use numbers below 100. Output channel components
use three digits. The first digit is the channel and the last two are the same
in every channel.

| Channel | Terminal | Designators |
|---|---|---|
| 1 to 8 | Z1 to Z8 | 101 to 105, 201 to 205, and so on to 801 to 805 |
| 9 | MV | 901 to 905 |

So R301 is the opto LED resistor of zone 3 and Q901 is the master valve triac.

### 3.3 Operating envelope I designed to

The specification gives no temperature range. These are my assumptions.

| Item | Value | Source |
|---|---|---|
| Ambient outside the enclosure | -20 to +50 °C | Assumption for a garage. The brief asks for 50 °C. |
| Air inside the enclosure | up to 60 °C | Assumption, 10 K rise. Bench test in section 20. |
| Supply | 21.6 to 26.4 VAC at load, up to 32 VAC unloaded, 50 or 60 Hz | Specification section 1 |
| Load | 0.5 A rms per output, 1.5 A rms inrush for 100 ms, two zones plus master | Specification section 1 |
| Smallest load | 50 mA rms pump-start relay coil | Brief |
| Transformer | Class 2, short-circuit current up to 20 A rms | Assumption. 20 A is the PTC fault rating. |

## 4. 24 VAC input and logic supply

### 4.1 Circuit

`AC_N` goes through fuse F1 to `GND`. `AC_L` feeds three things: the TVS D1 to
`GND`, the half-wave rectifier D2, and the valve common through relay K1.
D2 charges the bulk capacitor C1 through R1. The LM5164 buck U1 turns `VIN_DC`
into `V_BUCK`, 5.28 V. Schottky D3 passes it to the 5 V bus.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J1 | Header 2-way 3.81 mm pluggable, right angle | THT | C441182 | `AC_L` to `AC_N` | 24 VAC input |
| F1 | Fuse 3 A time-lag in SMD holder | SMD 9.7x5 mm | C206913 | `AC_N` to `GND` | Fuse in the ground leg |
| D1 | TVS 48 V bidirectional 1500 W | SMC | C151287 | `AC_L` to `GND` | Input surge clamp |
| D2 | Rectifier 1 A 1000 V | SMA | C95872 | A `AC_L`, K `VRECT` | Half-wave rectifier |
| R1 | 3.3R 1% 1 W | 2512 | C39347 | `VRECT` to `VIN_DC` | Limits charging and surge current |
| C1 | 470 uF 63 V aluminium electrolytic | SMD 16x16.5 mm | C249935 | + `VIN_DC`, - `GND` | Bulk capacitor |
| C2 | 2.2 uF 100 V X7R | 1210 | C55151 | `VIN_DC` to `GND` | At U1 pins |
| C3 | 2.2 uF 100 V X7R | 1210 | C55151 | `VIN_DC` to `GND` | At U1 pins |
| U1 | LM5164DDAR 100 V 1 A synchronous buck | SO-8 PowerPAD | C477928 | 1 GND `GND`, 2 VIN `VIN_DC`, 3 EN/UVLO `UVLO`, 4 RON `RON`, 5 FB `FB`, 6 PGOOD `PGOOD`, 7 BST `BST`, 8 SW `SW`, EP `GND` |  |
| C4 | 2.2 nF 50 V X7R | 0603 | C16033 | `BST` to `SW` | Bootstrap, value fixed by the datasheet |
| L1 | 47 uH, Isat 2.0 A, 0.2R | 6x6x4.5 mm | C5151734 | `SW` to `V_BUCK` |  |
| C5 | 22 uF 25 V X5R | 1206 | C12891 | `V_BUCK` to `GND` |  |
| C6 | 22 uF 25 V X5R | 1206 | C12891 | `V_BUCK` to `GND` |  |
| R2 | 47k 1% | 0603 | C25819 | `RON` to `GND` | On-time, 281 kHz |
| R3 | 330k 1% | 0603 | C23137 | `V_BUCK` to `FB_MID` | Feedback top, with R4 = 340k |
| R4 | 10k 1% | 0603 | C25804 | `FB_MID` to `FB` |  |
| R5 | 100k 1% | 0603 | C25803 | `FB` to `GND` | Feedback bottom |
| R6 | 200k 1% | 0603 | C25811 | `SW` to `RIP` | Ripple injection RA |
| C7 | 3.3 nF 50 V X7R | 0603 | C1613 | `RIP` to `V_BUCK` | Ripple injection CA |
| C8 | 100 pF 50 V C0G | 0603 | C14858 | `RIP` to `FB` | Ripple injection CB |
| R7 | 1M 1% | 0603 | C22935 | `VIN_DC` to `UVLO` | UVLO on 9.0 V, off 8.4 V |
| R8 | 200k 1% | 0603 | C25811 | `UVLO` to `GND` |  |
| R9 | 300k 1% | 0603 | C23024 | `VIN_DC` to `VIN_MON` | Supply monitor divider, 1/31 |
| R10 | 10k 1% | 0603 | C25804 | `VIN_MON` to `GND` |  |
| C9 | 100 nF 50 V X7R | 0603 | C14663 | `VIN_MON` to `GND` |  |
| D3 | SS34 Schottky 3 A 40 V | SMA | C8678 | A `V_BUCK`, K `V5` | OR-ing, buck side |

### 4.2 Rail budget

Currents on `3V3`. "Typical" is WiFi connected and idle, two zones and the
master valve on, backlight on. "Peak" is every consumer at its limit together.

| Consumer | Typical mA | Peak mA | Basis |
|---|---|---|---|
| ESP32-S3 module | 100 | 340 | Datasheet v2.2 tables 5-7 and 5-9. Peak is 802.11b transmit at 21 dBm. Typical is my estimate between modem-sleep (33 to 108 mA) and receive (88 to 91 mA). |
| TFT logic | 10 | 15 | Assumption. The panel datasheet says "TBD". |
| Opto LEDs, three on | 38 | 45 | Section 6.4 |
| Indicator, status and fault LEDs | 9 | 10 | 1.6 mA per zone LED, 2 mA status |
| Expander, logic, op-amp, pull-ups | 5 | 8 | Datasheet supply currents plus switch pull-ups |
| Qwiic connector | 0 | 125 | Hold current of PTC F2 |
| **Total `3V3`** | **162** | **543** | |

Currents on `V5` and `V_BUCK`.

| Consumer | Typical mA | Peak mA | Basis |
|---|---|---|---|
| LDO input, equal to the `3V3` load | 162 | 543 | |
| Backlight | 58 | 74 | Section 15.2 |
| Relay K1 coil | 40 | 40 | 5 V across 125 ohm |
| Comparator U6, sensor pull-ups | 2 | 3 | |
| **Total `V5`** | **262** | **660** | |
| Sensor supply terminal, from `V_BUCK` | 20 | 150 | Limit of U11 |
| **Total `V_BUCK`** | **282** | **810** | U1 is rated 1 A |

Power drawn from `VIN_DC`, with the buck efficiency taken as 85 %. That figure
is an assumption. I did not read an efficiency curve for a 5 V output.

- **Typical**: 5.28 V x 0.282 A / 0.85 = 1.75 W.
- **Peak**: 5.28 V x 0.81 A / 0.85 = 5.0 W.

### 4.3 Fuse F1

F1 is a Littelfuse 154T-series holder with a 452-series 3 A time-lag fuse. The
user can replace it without soldering. The datasheet gives 125 V, 50 A
interrupting rating, 0.034 ohm and a melting I²t of 20.16 A²s. Opening times
are 1 to 60 s at 200 %, 0.2 to 3 s at 300 % and 20 to 100 ms at 800 %.

| Check | Calculation | Result |
|---|---|---|
| Steady load | 1.5 A valves and 0.35 A rectifier current, added as rms | 1.54 A, 51 % of rating |
| Valve inrush | 2.5 A for 100 ms gives 0.63 A²s | 3 % of melting I²t |
| Plug-in at the 32 VAC peak | 45 V / (3.3 + 2.1 ohm) = 8.3 A, decaying with 2.5 ms | 0.09 A²s, under 1 % |
| Zone short, one half-cycle at 20 A rms | 20² x 10 ms | 4 A²s, 20 % of melting I²t |

The last row is why the fuse is time-lag. A very fast 3 A fuse of the 451
series has a melting I²t of 0.459 A²s. It would open on every shorted valve
wire before the electronic cut-off of section 9 had finished, and a shorted wire
would become a service call.

### 4.4 Surge clamp D1 and rectifier D2

D1 is a Littelfuse SMCJ48CA. Its stand-off voltage of 48 V is above the 45.3 V
peak of 32 VAC, so it does not conduct in normal use. Breakdown is 53.3 to
58.9 V. It clamps at 77.4 V at its rated 19.4 A, 10/1000 us pulse. A 43 V part,
as OpenSprinkler uses, would sit past its stand-off voltage at 32 VAC.

D2 is an M7, 1 A and 1000 V. The worst reverse voltage is the clamp voltage on
the negative half-cycle plus `VIN_DC`, about 77 + 60 = 137 V. The simulation
gives a peak forward current of 1.5 A at the peak load. The surge rating is
30 A.

R1 is 3.3 ohm. It does two jobs. It limits the plug-in current into C1, and it
limits how far a surge can pump C1 up. With D1 clamping at 77.4 V for a full
rated 10/1000 us pulse, C1 charges from 45 V toward the clamp voltage through
R1 with a time constant of 3.5 ohm x 470 uF = 1.6 ms. The pulse has a decay
constant of 1.44 ms. That gives a rise of about 13 V, to 58 V. C1 is rated
63 V and U1 100 V. The simulation puts the loss in R1 at 0.09 W typical and
0.95 W at low line with every consumer at its limit. R1 is rated 1 W.

### 4.5 Bulk capacitor C1

The rectifier is half-wave, so C1 supplies the load for most of a 20 ms cycle
at 50 Hz. `hardware/sim/supply.cir` models the transformer as a source with
2.1 ohm in series, which is my assumption for a 40 VA unit.

| Supply | Load | `VIN_DC` valley to peak | Capacitor ripple |
|---|---|---|---|
| 24 VAC, 50 Hz | 1.75 W | 28.6 to 30.7 V | 0.16 A rms |
| 24 VAC, 60 Hz | 1.75 W | 28.8 to 30.6 V | 0.16 A rms |
| 21.6 VAC, 50 Hz | 1.75 W | 24.8 to 27.3 V | 0.17 A rms |
| 21.6 VAC, 50 Hz | 5.0 W | 16.9 to 24.7 V | 0.48 A rms |
| 16 VAC, 50 Hz | 1.75 W | 15.6 to 19.1 V | 0.23 A rms |
| 16 VAC, 50 Hz | 3.0 W | 11.2 to 17.8 V | 0.40 A rms |
| 18 VAC, 50 Hz | 5.0 W | 6.9 to 18.7 V | 0.69 A rms |
| 32 VAC, 50 Hz | 0.05 W | 44.3 V | under 0.01 A rms |

U1 regulates down to its under-voltage lockout, which R7 and R8 set to 9.0 V
rising and 8.4 V falling. So the logic supply holds through a sag to 16 VAC at
the typical load and at 3 W. That is the brownout margin for a small
transformer during valve inrush. Only the last-but-one row fails: an 18 VAC sag
with every consumer at its limit. With no input at all, C1 holds the typical
load from 29 V down to 9 V for 0.5 x 470 uF x (29² - 9²) / 1.75 W = 100 ms,
five cycles.

C1 is a Lelon VZH 470 uF 63 V in a 16 x 16.5 mm SMD can, 5000 h at 105 °C. Its
steady voltage is at most 44.3 V, 70 % of rating. I could not read the ripple
rating of the 63 V part from the datasheet text. The 50 V part in the same can
is rated 1000 mA at 100 kHz, with a multiplier of 0.6 at 50 and 60 Hz. The
through-hole alternative, Rubycon 63PX470MEFC12.5X20, is rated 700 mA at
120 Hz and 0.8 of that at 50 Hz, which is 560 mA. Both cover the 0.16 A typical
and 0.48 A peak.

### 4.6 Buck converter U1

**Why 5 V and an LDO.** Three reasons.

- **USB**: USB arrives at 5 V. With a 5 V bus the two sources OR through one Schottky each, and one regulator makes `3V3` whichever is present. A direct 3.3 V buck needs a second regulator for USB and an ideal-diode multiplexer at 3.3 V.
- **Ripple**: the LDL1117 rejects 87 dB at 120 Hz and 65 dB at 100 kHz. The buck's 281 kHz ripple and the 50 Hz ripple on `VIN_DC` stay out of the ADC and the radio supply.
- **Other loads**: the backlight, relay, comparators and sensor supply need more than 3.3 V.

The cost is heat. The LDO drops 1.65 V, which is 0.27 W at the typical load.
Section 20 lists it as a thermal risk.

**Why the LM5164.** The specification asks for at least 60 V. D1 can let 77.4 V
through. The LM5164 has an absolute maximum of 100 V on VIN and is in stock at
JLCPCB. It is synchronous, so there is no catch diode. The 60 V and 65 V parts
I looked at, LMR16010 and LMR51610, would rely on C1 absorbing the surge.

Design values, using the equations of datasheet SNVSAU4D.

| Item | Equation | Value |
|---|---|---|
| Output voltage | 1.2 V x (1 + (R3 + R4) / R5) | 1.2 x (1 + 340k / 100k) = 5.28 V |
| Tolerance | Reference 1.181 to 1.218 V, 1 % resistors | 5.16 to 5.40 V |
| Switching frequency, eq. 1 | VOUT x 2500 / RRON(kohm) | 5.28 x 2500 / 47 = 281 kHz |
| On-time at 45 V, eq. 11 | RRON / (VIN x 2.5) | 47 / 112.5 = 0.42 us. Minimum allowed is 50 ns. |
| Inductor ripple at 34 V, eq. 18 | VOUT / (FSW x L) x (1 - VOUT / VIN) | 5.28 / (281k x 47u) x 0.845 = 0.34 A |
| Peak inductor current, eq. 19 | IOUT + ripple / 2 | 0.81 + 0.17 = 0.98 A. Current limit is 1.25 A minimum. |
| Output capacitor, eq. 21 | ripple / (8 x FSW x 0.5 % of VOUT) | 5.7 uF minimum. Fitted 2 x 22 uF, 25 V. |
| Ripple capacitor CA, eq. 24 | 10 / (FSW x (RFB1 parallel RFB2)) | 10 / (281k x 77.3k) = 460 pF minimum. Fitted 3.3 nF. |
| Ripple resistor RA, eq. 25 | tON x (VIN - VOUT) / (20 mV x CA) | At 34 V: 0.553 us x 28.7 V / (20 mV x 3.3 nF) = 240k maximum. Fitted 200k. |
| Ripple at FB | tON x (VIN - VOUT) / (RA x CA) | 24 mV at 34 V, 16 mV at 12 V, 13 mV at 10 V. The datasheet asks for 12 mV at minimum input. |
| Coupling capacitor CB, eq. 26 | settling time / (3 x RFB1) | 100 pF gives 100 us |
| UVLO, eq. 13 and 14 | 1.5 V and 1.4 V x (1 + R7 / R8) | On at 9.0 V, off at 8.4 V |

L1 is a Bourns SRN6045TA-470M, 47 uH, 0.2 ohm, saturation 2.0 A. The datasheet
asks for a saturation current above the current limit, which is 1.75 A at most.
C4 is 2.2 nF as the datasheet requires. A larger value damages the part.

**Light load.** Below about 0.17 A the converter enters diode emulation and
skips pulses. With all outputs off and the backlight off the load is near
0.1 A, so it will spend much of its life in that mode. The type 3 ripple network
keeps it stable there by design. Output ripple in that mode is a bench item.

### 4.7 LDO U3

The LDL1117S33R takes 2.6 to 18 V in and drops 0.6 V at most at 1.2 A. From
USB, `V5` can fall to 4.1 V, which still leaves 0.8 V of headroom. Output
accuracy is 3 % over temperature, so `3V3` is 3.20 to 3.40 V. The module needs
3.0 to 3.6 V. The panel's recommended maximum is 3.3 V and its absolute maximum
is 4.6 V, so the top 0.1 V of the LDO range is above the panel recommendation.
I accept that and list it in section 22.

## 5. USB-C and power OR-ing

### 5.1 Circuit

J5 is a 16-pin USB 2.0 Type-C receptacle. R15 and R16, 5.1 kohm from CC1 and
CC2 to `GND`, mark the board as a device so that a Type-C source turns VBUS on.
U2 clamps D+, D- and VBUS. D+ and D- go straight to GPIO20 and GPIO19 with no
series resistors, as in the Espressif reference circuit.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J5 | USB-C receptacle 16-pin, USB 2.0 | SMD | C165948 | VBUS (A4 A9 B4 B9) `VBUS`, GND (A1 A12 B1 B12) `GND`, CC1 (A5) `CC1`, CC2 (B5) `CC2`, D+ (A6 B6) `USB_DP`, D- (A7 B7) `USB_DM`, SBU1 SBU2 NC, shell `USB_SHLD` |  |
| R15 | 5.1k 1% | 0603 | C23186 | `CC1` to `GND` |  |
| R16 | 5.1k 1% | 0603 | C23186 | `CC2` to `GND` |  |
| U2 | USBLC6-2SC6 ESD array | SOT-23-6 | C7519 | 1 I/O1 `USB_DM`, 2 GND `GND`, 3 I/O2 `USB_DP`, 4 I/O2 `USB_DP`, 5 VBUS `VBUS`, 6 I/O1 `USB_DM` | Place at J5 |
| C13 | 100 nF 50 V X7R | 0603 | C14663 | `VBUS` to `GND` |  |
| D4 | SS34 Schottky 3 A 40 V | SMA | C8678 | A `VBUS`, K `V5` | OR-ing, USB side |
| C10 | 4.7 uF 25 V X5R | 0805 | C1779 | `V5` to `GND` |  |
| U3 | LDL1117S33R 3.3 V 1.2 A LDO | SOT-223 | C435835 | 1 GND `GND`, 2 VOUT `3V3`, 3 VIN `V5`, tab `3V3` |  |
| C11 | 10 uF 25 V X5R | 0805 | C15850 | `3V3` to `GND` |  |
| C12 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` |  |
| R17 | 0R | 0603 | C21189 | `USB_SHLD` to `GND` |  |

### 5.2 How the two sources combine

`V5` has two feeds, each through an SS34 Schottky: D3 from `V_BUCK` and D4 from
`VBUS`. The higher one supplies the board. Neither can feed the other, because
each diode is reverse-biased by the bus.

| Situation | `V_BUCK` | `VBUS` | `V5` | What works |
|---|---|---|---|---|
| 24 VAC only | 5.28 V | 0 | about 4.95 V | Everything |
| USB only | 0 | 4.4 to 5.25 V | 4.1 to 4.9 V | Logic, display, radio. No valve power, no sensor supply. |
| Both | 5.28 V | 4.4 to 5.25 V | about 4.95 V | Everything. The buck carries the load unless VBUS is above 5.3 V. |

The buck runs at 5.28 V so that `V5` is about 5.0 V after D3. The SS34 is
specified at 0.55 V maximum at 3 A. At 0.3 A I expect 0.3 to 0.35 V, read from
the typical curve.

Reverse leakage of an SS34 is 0.5 mA at 25 °C and rated voltage. With 24 VAC
present and a USB host attached, up to that much leaks from `V5` back into
VBUS. A host does not notice 0.5 mA. With no host attached, VBUS floats up to a
few volts through the leakage. That is harmless. Do not treat VBUS as a "USB
present" signal for that reason.

`V5` carries 4.7 uF, C10, plus the 100 nF decouplers on it. I recall a USB 2.0
limit of 10 uF of bypass capacitance at attach and did not re-read it. The buck
output capacitors are behind D3 and do not charge from USB.

USB alone powers the logic, as the specification requires. On USB the relay
coil has no AC to switch and the sensor supply is off, because U11 is fed from
`V_BUCK`.

### 5.3 The ground question

`GND` is one leg of the transformer. A USB cable connects `GND` to the
computer's ground, and a desktop computer's ground is protective earth. So
plugging in USB earths one leg of the 24 VAC secondary.

**Safe.**

- **Floating secondary**: a Class 2 sprinkler transformer whose secondary has no connection to earth. Nearly all plug-in and DIN-rail sprinkler transformers are built this way. USB earths the `AC_N` leg and nothing flows.
- **Battery laptop or USB isolator**: no earth path at all.

**Safe only while nothing on the board fails.**

- **Earthed on the `AC_N` side**: if the installation earths the leg wired to terminal 2, USB earths the same leg again. Nothing flows through the cable while the board is healthy. But the USB ground is then a second path from board `GND` to that leg, in parallel with F1. Review A1 moved this case out of the safe list for the reason given under "What F1 does not cover" below.

**Not safe.**

- **Secondary earthed on the other leg**: if the leg wired to terminal 1 is earthed and a USB cable from an earthed computer is plugged in, the transformer is short-circuited through the USB cable. The loop is transformer, earth, computer, USB ground wire, board `GND`, F1, terminal 2, transformer.
- **Two controllers on one transformer with the legs swapped**: controller A has leg 1 as ground, controller B has leg 2. Anything that joins their grounds completes a short across the transformer. That includes one computer on both USB ports, a shared sensor cable or a Qwiic cable. It also applies to a ZoneFlow sharing a transformer with any other half-wave-rectified device.
- **A valve wire or COM faulted to earth, with USB attached**: COM is the other transformer leg. Wet outdoor splices do leak to earth. With an earthed computer attached, that leak is a path across the transformer.

**What the hardware does about it.** In all three cases the fault current
flows between the USB ground and terminal 2 through board `GND`. In the first
two it does not touch terminal 1 at all, so a fuse in the `AC_L` leg would not
see it. That is why F1 is in the `AC_N` leg. Every loop that goes through board
`GND` to the transformer goes through F1. In the third case relay K1 also helps,
because COM is disconnected from the transformer whenever no valve is running.

**What F1 does not cover.** F1 is between board `GND` and terminal 2. Any
other connection from board `GND` to that transformer leg is in parallel with
it. There are two ways to get one: the terminal-2 leg is earthed and an
earthed computer is on USB, or a second device on the same transformer, with
the same leg as its ground, shares a sensor, Qwiic or USB ground with this
board. A fault on the board then has two ways back: a shorted TVS D1, a
shorted C1, or a shorted valve wire behind a triac that will not turn off.
The current divides between F1 and the other path. When F1 opens, all of it
takes the other path, with nothing but the transformer to limit it. A fuse in
the `AC_L` leg would be in that loop, and would miss the three loops above
instead. No single fuse position covers every loop. Review A1 recommends a
fuse in each leg, finding M2. This document keeps one fuse and relies on the
warning below; the choice is the owner's.

F1 is a 3 A time-lag fuse. I take 8 A as the short-circuit limit of a Class 2
source. That figure is from memory of the NEC Class 2 tables and I did not
re-read it. At 8 A, 270 % of rating, the fuse datasheet bounds the opening time
between 0.2 s and 60 s. A USB cable's ground conductor is thin. I estimate
that 8 A heats 28 AWG copper at about 50 K per second with no cooling. So
**F1 protects the board traces and limits the damage, but it does not make
these cases safe for the cable or the computer.** A fast fuse would clear
sooner and would also open on every shorted valve wire. I chose the time-lag
fuse and the warning.

**What the documentation must say.** These warnings belong in the wiring page
and the safety page.

1. Use a transformer whose 24 V output is not connected to earth on either side. If you are not sure, measure for continuity between each output terminal and the earth pin with the transformer unplugged. If either terminal is earthed, never connect a mains-powered computer by USB while the 24 VAC plug is in, whichever way round the plug is wired.
2. Never connect a computer by USB while the controller shares its transformer with another device, unless you have checked that both devices have the same leg on the same terminal.
3. For firmware work on an installed controller, prefer a laptop on battery, or unplug the 24 VAC plug block J1 first. The board runs from USB alone.
4. Sensor grounds and the Qwiic ground are the same net as transformer terminal 2. Do not join them to any other equipment that has its own connection to the transformer.

## 6. Output stage

Nine identical channels: Z1 to Z8 and MV. The table shows channel 1. For
channel n replace the first digit of each designator with n, and the net
suffix `Z1` with `Z2` to `Z8` or `MV`.

### 6.1 Circuit

The valve sits between a `COM` terminal and its output terminal `OUT_Z1`. From
the terminal the current goes through PTC F101 to the triac's T2, through the
triac to `ISNS`, and through the shunt R37 to `GND`.

The gate is fired from T2. The opto-triac U101 connects T2 to the gate through
R103. When the expander pulls `ZD_Z1_N` low and `V_DRV` is on, the opto LED
conducts, the opto-triac conducts, and gate current flows from T2 until the
main triac turns on and collapses the voltage across itself.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| F101 | PTC 1.1 A hold, 60 V | 2920 | C315903 | `OUT_Z1` to `T2_Z1` | Fault limiter |
| D101 | TVS 48 V bidirectional 600 W | SMB | C151264 | `OUT_Z1` to `ISNS` | Across the triac, at the terminal |
| Q101 | T410-600B-TR triac 4 A 600 V, 3-quadrant | DPAK | C2970064 | 1 T1 `ISNS`, 2 T2 (tab) `T2_Z1`, 3 G `G_Z1` |  |
| R105 | 47R 1% 150 V | 0805 | C17714 | `T2_Z1` to `SNB_Z1` | Snubber |
| C101 | 10 nF 100 V X7R | 0805 | C18741 | `SNB_Z1` to `GND` | Snubber |
| U101 | MOC3023SR2M random-phase opto-triac | SMD-6 | C40701 | 1 anode `OLA_Z1`, 2 cathode `ZD_Z1_N`, 3 NC NC, 4 MT `G_Z1`, 5 do not connect NC, 6 MT `OGT_Z1` |  |
| R101 | 150R 1% | 0603 | C22808 | `V_DRV` to `OLA_Z1` | Opto LED current |
| R103 | 100R 1% 150 V | 0805 | C17408 | `T2_Z1` to `OGT_Z1` | Gate series |
| R104 | 1k 1% | 0603 | C21190 | `G_Z1` to `ISNS` | Gate to T1 |
| R102 | 680R 1% | 0603 | C23228 | `V_DRV` to `LEDA_Z1` | Indicator current |
| D102 | LED yellow-green | 0603 | C125094 | A `LEDA_Z1`, K `ZD_Z1_N` | Output indicator |

### 6.2 Quadrant analysis and why the gate is opto-coupled

T1 of every triac is at `ISNS`, within 0.3 V of `GND`. Both logic rails are at
or above T1. Logic can therefore only push current into a gate, never pull it
out. Positive gate current fires a triac in quadrant I when T2 is positive and
in quadrant IV when T2 is negative.

Quadrant IV is the problem. Three-quadrant triacs do not trigger in it at all.
Four-quadrant parts do, with their highest trigger current and their lowest
immunity. On 2026-11-10 the JLCPCB parts library held these candidates:

| Part | Maker | Rating | Quadrants | Stock |
|---|---|---|---|---|
| T410-600B-TR | ST | 4 A, 30 A surge | I, II, III | 1320 |
| BTA204S-600D | WeEn | 4 A, 25 A surge | I, II, III | 1620 |
| ACST410-8BTR | ST | 4 A, 32 A surge at 60 Hz | I, II, III | 1334 |
| Z0103MN, Z0109NN | ST | 1 A, 8 A surge | all four | 3385, 932 |
| BT136S-600D | WeEn | 4 A, 25 A surge | all four | 40 |
| BT136S-600E | Minos | 4 A | not checked | 10232 |

The Z01 parts have the 8 A surge rating of a MAC97, which the specification
rules out. The genuine BT136S is not stocked in useful quantity. The Minos part
is a second-source copy whose datasheet I did not evaluate. Every well-rated
part that is in stock is three-quadrant. So a direct logic-driven gate, as the
specification describes, cannot be built from stocked parts with good ratings.

I looked at three ways to give a three-quadrant triac the gate current it
needs.

- **Negative gate supply.** A charge pump makes a rail below ground and each gate sinks into it. Every channel then needs a two-transistor level shifter, because a 0 to 3.3 V logic signal cannot switch a transistor whose emitter is at -3 V.
- **Logic supply referenced the other way.** ST recommends tying the triac's T1 to the logic supply's positive rail so the controller sinks gate current. Here that makes sensor ground and USB ground a different net from both transformer legs. A sensor ground wire touching COM would then put 24 VAC across the 3.3 V rail. I rejected it for that reason.
- **Gate current taken from T2 through an opto-triac.** Gate current then has the polarity of T2: quadrant I on one half-cycle, quadrant III on the other. These are the two quadrants in which every triac triggers most easily. This is the standard solid-state-relay circuit.

I chose the third. It has three more advantages. No logic pin touches the power
stage, so a surge that kills a triac does not also reach the expander. Gate
power comes from the load circuit, not from `3V3`. And the main triac's gate
drive stops by itself once the triac is on.

It costs nine opto-triacs, 2 to 3 USD per 8-zone board. It also means the triac needs a few volts across it before it fires
each half-cycle. Section 6.4 puts numbers on that.

### 6.3 Triac Q101: T410-600B-TR

Figures from the ST T4 series datasheet, DocID7699 rev 9.

| Parameter | Value |
|---|---|
| On-state current | 4 A rms at 110 °C case |
| Surge, one 20 ms cycle | 30 A |
| I²t, 10 ms | 5.1 A²s |
| Gate trigger current, quadrants I, II, III | 10 mA maximum at 25 °C |
| Gate trigger voltage | 1.3 V maximum |
| Holding current | 15 mA maximum |
| Latching current | 25 mA maximum in I and III |
| dV/dt, gate open, 125 °C | 40 V/us minimum at 67 % of 600 V |
| Commutation (dI/dt)c without snubber, 125 °C | 2.0 A/ms minimum at 10 V/us, 2.7 A/ms at 0.1 V/us |
| On-state model, 125 °C maximum | 0.89 V + 0.12 ohm |
| Thermal resistance to ambient, DPAK | 70 K/W with 0.5 cm² of copper, about 55 K/W with 2 cm² (figure 11) |

Figure 8 of the datasheet gives the change with temperature. At -20 °C the
trigger current is about 1.8 times its 25 °C value, and holding and latching
current about 1.45 times.

The WeEn BTA204S-600D fits the same footprint. It has a 5 mA gate and 6 mA
holding current, but 25 A surge, 3.1 A²s and a higher on-state voltage model
of 1.27 V + 0.091 ohm. I made the T410 the primary part because the
specification's short-circuit requirement matters more than behaviour with very
small loads. Section 6.6 has the surge numbers.

Both parts have the same pin order: pin 1 T1, pin 2 and tab T2, pin 3 gate.
The WeEn order is from table 2 of its data sheet. The ST order is from the
package drawing on page 1 of DocID7699 rev 9, read as an image during review
A1: left lead A1, cropped centre lead and tab A2, right lead G. ST names the
main terminals A1 and A2 where this document says T1 and T2.

### 6.4 Drive margins

**Opto LED current.** U101 is an onsemi MOC3023, a random-phase opto-triac.
Its datasheet guarantees triggering at 5 mA of LED current at 25 °C, and
figure 4 puts the requirement at 1.12 times that at -20 °C. The LED forward
voltage is 1.15 V typical and 1.5 V maximum at 10 mA. The TCA9539 guarantees
8 mA of sink current at 0.5 V and 10 mA at 0.7 V, which I treat as a worst
output resistance of 70 ohm.

| Case | `V_DRV` | LED drop | Expander pin | LED current | Requirement |
|---|---|---|---|---|---|
| Worst, -20 °C | 3.19 V | 1.56 V | 70 ohm, 0.57 V | 7.1 mA | 5.6 mA |
| Worst, 25 °C | 3.19 V | 1.50 V | 70 ohm, 0.57 V | 7.5 mA | 5.0 mA |
| Typical | 3.30 V | 1.18 V | about 0.28 V | 12.3 mA | |
| Highest | 3.40 V | 1.00 V | about 0.15 V | 15.0 mA | 60 mA absolute maximum |

Each row solves (`V_DRV` - LED drop - pin voltage) / 150 ohm, with the 1.2 to
1.9 mA of the indicator LED branch added to the pin current. The typical and
highest pin voltages are my estimates. The datasheet gives no typical value.
The worst-case margin is 1.27. That corner needs the LDO at its low limit, the
LED at its maximum drop, the expander at its guaranteed minimum drive and
-20 °C together.

The highest pin current is 15.0 + 1.9 = 16.9 mA. The TCA9539 recommends at
most 18 mA per pin at 85 °C junction, and 100 mA per 8-bit port. Port 0 carries
Z1 to Z4 and MV, which is 85 mA with all five on. Port 1 carries Z5 to Z8,
68 mA. Both limits hold even if firmware turns everything on.

**Firing voltage.** The triac fires each half-cycle when T2 has risen far
enough to push the trigger current through the opto-triac, R103 and the gate.

| Case | Opto-triac | Gate | R103 and R104 | Firing voltage | Angle |
|---|---|---|---|---|---|
| Worst, -20 °C, 21.6 VAC | 3.0 V | 1.5 V | (18 + 1.5 mA) x 100 ohm | 6.5 V | 12° |
| Typical, 24 VAC | 1.2 V | 0.8 V | (5 + 0.8 mA) x 100 ohm | 2.6 V | 4° |

The 3.0 V is the opto-triac's maximum on-state voltage, specified at 100 mA.
The 1.5 V gate voltage at -20 °C is my estimate from the 1.3 V limit at 25 °C.
R104 takes the gate voltage divided by 1 kohm. Losing the first 12° of each
half-cycle lowers the power in the valve by 0.2 %. With an inductive
load the triac re-fires at the current zero, when the line voltage is already
tens of volts, so the delay is shorter still.

R103 also limits the opto-triac's current if it turns on at the line peak:
45 V / 100 ohm = 0.45 A. The MOC3023 allows 1 A peak.

**Small loads.** At -20 °C the triac needs 36 mA to latch and drops out below
22 mA. A valve drawing 150 mA rms peaks at 212 mA, so the triac is out of
conduction for about 10° after each current zero and 6° before it. A 50 mA rms
relay coil peaks at 71 mA, and those angles become 30° and 18°. In those
intervals the triac conducts only while gate current flows, at a few volts of
drop. I expect a 50 mA coil to hold, with some loss of rms current. I cannot
show it from the datasheet. If the master output must drive a coil under
100 mA, fit a BTA204S-600D at Q901. It latches at 9 mA at -20 °C.

### 6.5 dV/dt, commutation and the snubber

**Commutation.** The rate of fall of current at turn-off is the peak current
times 2 pi f. At 60 Hz that is 0.27 A/ms for 0.5 A rms, and 0.80 A/ms if the
output turns off during a 1.5 A rms inrush. The T410 guarantees 2.0 A/ms at a
re-applied dV/dt of 10 V/us with no snubber. The margin is 2.5 without relying
on the snubber.

**Re-applied dV/dt.** When the triac stops, the valve's inductance and the
snubber capacitor ring. I estimate a solenoid at 0.13 to 0.25 H from typical
catalogue currents of 0.4 A inrush and 0.25 A holding at 24 V, 60 Hz, with
35 ohm of winding resistance. Those are assumed values, not measurements. With
C101 = 10 nF the ring frequency is 1 / (2 pi sqrt(0.13 x 10 nF)) = 4.4 kHz and
the fastest slope is 34 V x 27.7 krad/s = 0.94 V/us.

**Static dV/dt.** The T410 withstands 40 V/us to 400 V. Nothing on this board
can step more than the 77 V of the TVS clamp. A surge edge faster than 40 V/us
could still turn a triac on. It would conduct for at most one half-cycle and
then stop, because the gate is not driven. I assume a valve does not open in
10 ms. The MOC3023 withstands 1000 V/us.

**Snubber values.** R105 = 47 ohm and C101 = 10 nF are the values that every
published design in the prior-art survey uses. With the triac off, the snubber
passes 24 V x 2 pi x 60 Hz x 10 nF = 90 uA through the valve. It returns to
`GND`, not `ISNS`, so the current measurement does not see it. R105 dissipates
about C x V² x 2f = 1.4 mW. C101 is rated 100 V.

### 6.6 Surge, short circuit and the fault limiter

**Inrush.** 1.5 A rms for 100 ms is inside the 4 A continuous rating.

**Dead short on a valve wire.** The comparator of section 9 removes drive about
70 us after the current passes 4.6 A. The triac then conducts until the next
current zero, at most one half-cycle. The triac must survive that half-cycle at
the transformer's short-circuit current. Whether it then stops is a separate
question, taken after the table.

| Triac | I²t, 10 ms | Peak for one half-cycle | Minimum loop impedance at 27.5 VAC open circuit |
|---|---|---|---|
| T410-600B | 5.1 A²s | 31.9 A | 1.18 ohm |
| BTA204S-600D | 3.1 A²s | 24.9 A | 1.51 ohm |

Peak is sqrt(2 x I²t / 10 ms). The loop impedance is (38.9 V - 1.3 V) / peak.
The board contributes at least 0.31 ohm: 0.12 ohm of PTC, 0.1 ohm of shunt and
0.09 to 0.12 ohm of triac. So the transformer and wiring must supply 0.87 ohm
for the T410. A transformer with 12 % regulation has an output impedance of
about 69 / VA ohm by my estimate, which is 0.92 ohm at 75 VA and 1.7 ohm at
40 VA. **By calculation the T410 survives one half-cycle of a dead short on
transformers up to about 75 VA. Larger ones need a bench test.** The PTC's own
fault rating is 20 A rms.

**The triac is not certain to stop at that first current zero.** Review A1
added this check. A triac turns off only if the current falls through zero
slowly enough. The T410 guarantees (dI/dt)c of 2.7 A/ms at 125 °C with
0.1 V/us re-applied, table 4 of the data sheet. A sine of peak Ip falls
through zero at 2 pi f Ip. So the guaranteed limit is 8.6 A peak at 50 Hz and
7.2 A peak at 60 Hz, which is 6.1 and 5.1 A rms.

| Source | Short-circuit current | Fall at the zero, 50 and 60 Hz | Guaranteed at 125 °C |
|---|---|---|---|
| 40 VA transformer, short at the terminals | 13.5 A rms, 19.1 A peak | 6.0 and 7.2 A/ms | 2.7 A/ms |
| 40 VA transformer, short behind 30 m of 18 AWG pair, 1.26 ohm | 8.3 A rms, 11.8 A peak | 3.7 and 4.4 A/ms | 2.7 A/ms |
| 75 VA transformer, short at the terminals | 22.4 A rms, 31.6 A peak | 9.9 and 11.9 A/ms | 2.7 A/ms |

The currents are 27.5 V over the transformer impedance above plus the board's
0.31 ohm. Figure 10 of the data sheet shows the limit rising as the junction
cools, typically 1.5 times at 100 °C and 2.2 times at 75 °C, so a cool triac
on a long cable run will usually stop. A triac that was already carrying
0.5 A in a warm enclosure, shorted at the terminals, may not. It then conducts
a second half-cycle and more with no gate drive.

What ends the fault in that case is K1. It opens within 10 ms of the trip
(section 7.2), so the fault lasts about one mains cycle, two at most. Figure 6
of the data sheet gives the surge the T410 survives over more than one cycle:
30 A peak for one cycle, 25 A for two, 22.5 A for three. On a 40 VA
transformer, 19.1 A peak, the triac survives whether it commutates or not. On
75 VA, 31.6 A peak, it survives only if it stops at the first zero. K1 breaks
the fault current in the cases where the triac does not stop, and that
current is above the 5 A its contact is rated to switch. **Both the triac's
turn-off and K1's breaking capacity under a dead short are bench items, and
"a shorted wire does not destroy the output" is not shown by calculation.**

**The series element.** The specification allows a PTC, a fuse or a resistor.

| Element | In a dead short | In normal use | After the fault |
|---|---|---|---|
| Fuse, 1 A fast | Clears in milliseconds. I did not size one. | No loss | Someone must replace it. Every shorted wire is a service call. |
| Resistor, 2.2 ohm | Limits the peak. Carries the fault until something else acts, at tens of watts. | 0.55 W at 0.5 A, all the time | Nothing to reset, if it survived |
| PTC, 1.1 A hold | Too slow to protect the triac by itself: 0.5 s at 8 A | 0.03 to 0.1 W | Resets when power is removed |

I chose the PTC, F101, a Littelfuse 2920L110/60. It is not what saves the
triac. The comparator does that. The PTC is the backstop for the cases where
the electronics cannot help: a triac that has failed short onto a shorted wire,
or a board with no working logic. Its ratings are 60 V, 20 A maximum fault
current, 1.1 A hold and 2.2 A trip at 20 °C. The hold current falls to 0.73 A
at 60 °C and 0.64 A at 70 °C, still above the 0.5 A load. At 60 °C the trip
current is about 1.5 A, the same as the inrush. The inrush lasts 100 ms and a
PTC at its trip current takes seconds, so I do not expect a trip. That is a
bench item.

**TVS D101.** A Littelfuse SMBJ48CA from the output terminal to `ISNS`. Its
48 V stand-off is above the 45.3 V peak that appears across an off triac at
32 VAC. It clamps at 77.4 V, far below the triac's 600 V. It returns to `ISNS`
and not to `GND` so that a TVS that has failed short puts the valve current
through the shunt, where firmware can see current with every output off. A TVS
normally fails short, and a shorted TVS energises its valve. Section 7 is the
answer to that.

### 6.7 Dissipation and junction temperature

Two zones and the master valve on, 0.5 A rms each, T410 maximum on-state
model at 125 °C.

- **Per triac**: 0.89 V x 0.45 A average + 0.12 ohm x (0.5 A)² = 0.43 W.
- **Rise**: 0.43 W x 55 K/W = 24 K with 2 cm² of copper on the tab.
- **Junction**: 60 °C air + 24 K + 5 K allowed for neighbours = 89 °C. The limit is 125 °C.

With the BTA204S model the loss is 0.59 W and the junction 98 °C.

Each tab is its own net, `T2_Zn`. The layout must give every triac its own
2 cm² copper island, on both sides with vias if the top side lacks room. The
other heat sources while watering are the shunt at 0.23 W, three PTCs at up to
0.10 W each, the LDO at 0.27 W, the relay coil at 0.2 W, the backlight at 0.3 W
and the module. The total is about 3.5 W in a closed box. The 60 °C air
temperature is my assumption for that. The module's limit is 65 °C. See
section 20.

### 6.8 Zero-cross switching

I recommend random turn-on, with no zero-cross hardware and no firmware timing.

- **Inrush**: for an inductive load, closing at the voltage zero gives the largest flux offset, not the smallest current. A solenoid's inrush comes from its open air gap and lasts until the plunger seats, whenever it is switched.
- **Stress**: the worst step at random turn-on is 45 V into more than 0.1 H. The current rises at under 0.5 A/ms. The triac allows 50 A/us.
- **Zero-cross optos**: a MOC304x-type driver refuses to fire above its inhibit voltage. An inductive load needs re-firing at the current zero, when the line voltage is near its peak. The random-phase MOC3023 has no such limit.

Turn-off is always at a current zero, because that is how a triac stops.
Firmware does use `AC_ZC`, but for mains-frequency detection and for timing the
current measurement, not for switching.

### 6.9 Output LED

D102 is in parallel with the opto LED branch, between `V_DRV` and the same
expander pin. It lights only when the expander bit is low and the fail-safe
circuit has `V_DRV` on. That is the real drive state, not the commanded state.
It does not prove that the triac conducts. The current measurement does that.

## 7. Valve common relay

### 7.1 Why it is here

Removing LED drive stops a healthy output. It does nothing for these three
failures, each of which leaves one valve energised:

- **Triac shorted T2 to T1.** The usual way a triac dies after a surge or an overload. The prior-art survey lists it as the most common field failure.
- **TVS D101 shorted.** The usual way a TVS dies after a surge beyond its rating.
- **Opto-triac output shorted.** The main triac then fires every half-cycle.

A stuck-on sprinkler valve floods something. The specification's rule 1 says
every output is off when the controller is in reset or crashed. With a shorted
output device that is not true, and no firmware can make it true.

K1 is in the feed from `AC_L` to the `COM` terminals. With K1 open no valve
has power, whatever the output devices are doing.

### 7.2 Circuit

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| K1 | G5NB-1A-E DC5 relay SPST-NO 5 A, 5 V 125R coil | THT 20.4x7 mm | C48746 | coil+ `V5`, coil- `RLY_A`, contact 1 `AC_L`, contact 2 `COM` |  |
| D5 | 1N4148W | SOD-123 | C81598 | A `RLY_A`, K `V5` | Coil flyback |
| Q1 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `RLY_G1`, S `RLY_B`, D `RLY_A` | On when the expander bit COM_RELAY is high |
| R11 | 1k 1% | 0603 | C21190 | `RLY_ON` to `RLY_G1` |  |
| R12 | 100k 1% | 0603 | C25803 | `RLY_G1` to `GND` |  |
| Q2 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `RLY_G2`, S `GND`, D `RLY_B` | On when DRV_EN is high |
| D21 | 1N4148W | SOD-123 | C81598 | A `DRV_EN`, K `RLY_D` | Lets DRV_EN charge C50 but not discharge it |
| R13 | 1k 1% | 0603 | C21190 | `RLY_D` to `RLY_G2` |  |
| R14 | 47k 1% | 0603 | C25819 | `RLY_G2` to `GND` | With C50, holds Q2 on for 30 to 50 ms after DRV_EN falls. Q1 does not wait, see section 7.2 |
| C50 | 1 uF 50 V X5R | 0603 | C15849 | `RLY_G2` to `GND` |  |
| JP1 | Solder jumper, open | PCB | none | `AC_L` to `COM` | Bridge only if K1 is not fitted |
| J2 | Header 7-way 3.81 mm pluggable, right angle | THT | C441187 | 1 `COM`, 2 `COM`, 3 `OUT_MV`, 4 `OUT_Z1`, 5 `OUT_Z2`, 6 `OUT_Z3`, 7 `OUT_Z4` | Valves A |
| J3 | Header 4-way 3.81 mm pluggable, right angle | THT | C441184 | 1 `OUT_Z5`, 2 `OUT_Z6`, 3 `OUT_Z7`, 4 `OUT_Z8` | Valves B (8-zone build only) |

The coil is between `V5` and two MOSFETs in series. Q1 conducts when the
expander bit COM_RELAY is high. Q2 conducts when `DRV_EN` is high. So K1 closes
only when firmware has asked for it over I2C and the fail-safe circuit allows
drive. Either one alone opens it. The expander bit also falls when `DRV_EN`
falls, because `DRV_EN` resets the expander.

D21, R14 and C50 hold Q2 on after `DRV_EN` falls. D21 lets `DRV_EN` charge
C50 to about 2.7 V and stops it discharging C50. R14 then discharges it with a
47 ms time constant, and Q2 turns off between its 1.45 V maximum and 1.0 V
typical threshold, 30 to 50 ms later. The intent was that after an
over-current trip the triacs stop first and K1 opens afterwards with no
current in its contacts.

**The circuit as drawn does not do that.** Review A1 found that Q1 lets go
first. `DRV_EN` low resets the expander, P05 becomes an input, and R12
discharges Q1's gate in 0.1 ms. `hardware/sim/relay_drive.cir` shows the coil
current under 10 % of its rated value 0.7 to 3.8 ms after `DRV_EN` falls, for
a coil inductance of 50 to 300 mH, while Q2's gate is still held for 46 ms. So
on every loss of `DRV_EN`, from an over-current trip or a stopped keep-alive,
K1 starts to open at once and its contacts part within the 10 ms release time
of the data sheet. That can be before the triac's current zero. K1 then
breaks the current: at most 1.5 A of valve current after a stopped
keep-alive, and up to the transformer's short-circuit current after a trip.
The contact is rated to switch 5 A.

D21, C50 and the hold function of R14 therefore do nothing useful as drawn.
There are two ways to settle it, and the choice is open:

- **Make the hold real.** Give Q1 the same network: a diode from `RLY_ON` to its gate and a capacitor across R12. Both transistors then hold for 30 to 50 ms. K1 opens without current whenever the triac has stopped by itself.
- **Keep the fast release and drop the hold parts.** Remove D21 and C50. K1 is then the back-up that interrupts a fault within about one mains cycle when the triac fails to stop, which section 6.6 shows can happen. The price is that K1 breaks current on these rare events.

Review A1 recommends the second, for the reason in its finding M3. Firmware
sequencing in section 7.3 is not affected by either choice: in normal use the
relay never switches current.

K1 is an Omron G5NB-1A-E with a 5 V coil of 125 ohm, 40 mA. From the English
Omron data sheet: the high-capacity contact is rated 5 A at 250 VAC and 3 A at
30 VDC, resistive, with 5 A maximum switching current. Operate and release
times are 10 ms maximum each. The must-operate voltage is 75 % of rated,
3.75 V, at a coil temperature of 23 °C. Copper gains 0.39 % per kelvin, so a
coil at 85 °C needs 3.75 V x 1.24 = 4.65 V. The coil sees `V5` less 4 mV of
MOSFET drop, and the lowest `V5` on 24 VAC is 5.16 V - 0.35 V = 4.81 V. The
margin at that corner is 3 %. Feeding the coil from `V_BUCK` instead of `V5`
would add the 0.35 V of D3 to it; the relay has nothing to switch on USB power
anyway. That change is recommended in the review and not made here.

K1 is a through-hole part. The specification limits those to parts with no
surface-mount equivalent. I did not look for a surface-mount relay with this
rating, and that is an open point for review.

JP1 is an open solder jumper across the contacts. If the project decides
against the relay, leave out K1, Q1, Q2, D5, D21, R11 to R14 and C50, and
bridge JP1.

### 7.3 What firmware does with it

1. Close K1 at least 20 ms before the first output turns on, and open it at least 40 ms after the last one turns off. The contacts then never make or break current.
2. With K1 closed and every output off, measure the current. More than the idle threshold means a shorted triac, TVS or opto-triac. Open K1, raise a fault and do not close it again until a person has acknowledged the fault.
3. With K1 open the current must read zero. That is a free check of the amplifier offset.

## 8. Fail-safe gate

### 8.1 Circuit

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| R33 | 10k 1% | 0603 | C25804 | `KA` to `GND` | Pull-down |
| R29 | 220R 1% | 0603 | C22962 | `KA` to `KA_N0` |  |
| C22 | 4.7 uF 25 V X5R | 0805 | C1779 | `KA_N0` to `KA_N1` | Pump capacitor |
| D7 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `KA_N2`, 3 common `KA_N1` |  |
| C23 | 10 uF 25 V X5R | 0805 | C15850 | `KA_N2` to `GND` | Reservoir |
| R30 | 56k 1% | 0603 | C23206 | `KA_N2` to `GND` | Sets the hold-up time |
| R31 | 100k 1% | 0603 | C25803 | `KA_N2` to `KA_INP` |  |
| R32 | 510k 1% | 0603 | C23192 | `KA_INP` to `KA_OK` | Hysteresis |
| C24 | 10 nF 50 V X7R | 0603 | C1589 | `KA_INP` to `GND` |  |
| R34 | 15k 1% | 0603 | C22809 | `3V3` to `KA_REF` |  |
| R35 | 10k 1% | 0603 | C25804 | `KA_REF` to `GND` |  |
| R36 | 10k 1% | 0603 | C25804 | `3V3` to `KA_OK` | Pull-up |

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| U5 | TCA9539PWR 16-bit I2C expander with reset | TSSOP-24 | C131972 | 1 INT `EXP_INT_N`, 2 A1 `GND`, 3 RESET `DRV_EN`, 4 P00 `ZD_Z1_N`, 5 P01 `ZD_Z2_N`, 6 P02 `ZD_Z3_N`, 7 P03 `ZD_Z4_N`, 8 P04 `ZD_MV_N`, 9 P05 `RLY_ON`, 10 P06 `P06`, 11 P07 `P07`, 12 GND `GND`, 13 P10 `ZD_Z5_N`, 14 P11 `ZD_Z6_N`, 15 P12 `ZD_Z7_N`, 16 P13 `ZD_Z8_N`, 17 P14 `VARIANT_N`, 18 P15 `SENS_FLT_N`, 19 P16 `P16`, 20 P17 `P17`, 21 A0 `GND`, 22 SCL `I2C0_SCL`, 23 SDA `I2C0_SDA`, 24 VCC `3V3` | Address 0x74 |
| C18 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U5 |
| R27 | 10k 1% | 0603 | C25804 | `3V3` to `VARIANT_N` |  |
| R28 | 1k 1% | 0603 | C21190 | `VARIANT_N` to `GND` | Variant strap (8-zone build only) |
| U6 | LM2901DR quad comparator | SOIC-14 | C7932 | 1 1OUT `KA_OK`, 2 2OUT `OC_TRIP_N`, 3 VCC `V5`, 4 2IN- `OC_CMP`, 5 2IN+ `VH`, 6 1IN- `KA_REF`, 7 1IN+ `KA_INP`, 8 3IN- `VL`, 9 3IN+ `OC_CMP`, 10 4IN- `VL`, 11 4IN+ `ZC_DIV`, 12 GND `GND`, 13 4OUT `AC_ZC`, 14 3OUT `OC_TRIP_N` |  |
| C19 | 100 nF 50 V X7R | 0603 | C14663 | `V5` to `GND` | At U6 |
| U7 | 74HC00D quad NAND | SOIC-14 | C5586 | 1 1A `OC_TRIP_N`, 2 1B `FAULT_N`, 3 1Y `FAULT`, 4 2A `KA_OK`, 5 2B `FAULT`, 6 2Y `FAULT_N`, 7 GND `GND`, 8 3Y `DRV_EN_N`, 9 3A `KA_OK`, 10 3B `FAULT_N`, 11 4Y `DRV_EN`, 12 4A `DRV_EN_N`, 13 4B `DRV_EN_N`, 14 VCC `3V3` |  |
| C20 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U7 |
| Q3 | AO3401A P-MOSFET 30 V | SOT-23 | C15127 | G `DRV_EN_N`, S `3V3`, D `V_DRV` | Switches the opto LED supply |
| R25 | 100k 1% | 0603 | C25803 | `3V3` to `DRV_EN_N` | Keeps Q3 off while 3V3 ramps |
| R26 | 10k 1% | 0603 | C25804 | `V_DRV` to `GND` | Bleed |
| C21 | 100 nF 50 V X7R | 0603 | C14663 | `V_DRV` to `GND` |  |

### 8.2 How it works

Firmware toggles `KA`, GPIO38. Each rising edge pushes charge through C22 and
D7 into the reservoir C23. R30 drains C23 all the time. Comparator U6A compares
the reservoir voltage with a reference and drives `KA_OK` high when the
reservoir is charged.

`KA_OK` goes to the 74HC00, U7. Two gates form the over-current latch of
section 9. The third makes `DRV_EN_N` = NAND(`KA_OK`, `FAULT_N`). The fourth
inverts it to `DRV_EN`. So drive is enabled when the keep-alive is running and
the latch is clear.

`DRV_EN` does three things, and any one of them alone turns the valves off.

| Path | Mechanism | Effect when `DRV_EN` is low |
|---|---|---|
| LED supply | `DRV_EN_N` high turns off Q3 | `V_DRV` falls. No opto LED can conduct, whatever the expander pins do. |
| Expander reset | `DRV_EN` is the TCA9539 RESET pin | Every expander pin becomes an input. The stored output bits are lost, so nothing comes back on when drive returns. |
| Relay | `DRV_EN` gates Q2, and resets the expander bit that gates Q1 | K1 opens within its 10 ms release time. Section 7.2 explains why the 30 to 50 ms hold does not apply. |

No ESP32 pin other than `KA` and the I2C bus can influence a valve. Turning one
on takes a running keep-alive and two valid I2C writes to address 0x74.

### 8.3 Thresholds

The reference is 3.3 V x R35 / (R34 + R35) = 1.32 V. R31 and R32 add
hysteresis. The reservoir voltage `KA_N2` at which U6A switches is:

- **Turning on**, output low at 0.15 V: (1.32 - 0.15 x 100 / 610) x 610 / 510 = 1.55 V.
- **Turning off**, output high at 3.3 V through R36: (1.32 - 3.3 x 100 / 620) x 620 / 520 = 0.94 V.

U6 is an LM2901 on `V5`. Its input bias current, up to 500 nA, flows out of the
inputs. Through R31 in parallel with R32 that is a 42 mV error. C24 keeps the
comparator input below the reference while `3V3` ramps up, so `KA_OK` cannot
pulse at power-on. The simulation includes the ramp and the bias current.

### 8.4 Timing, from `hardware/sim/keepalive.cir` and `keepalive_faults.cir`

| Keep-alive signal | Result |
|---|---|
| 10 Hz square wave | Enabled 104 ms after the first edge. Disabled 0.61 s after the last. |
| 20 Hz | Enabled after 53 ms. Disabled after 0.66 s. |
| 100 Hz, the firmware's 5 ms toggle | Enabled after 14 ms. Disabled after 0.74 s. |
| 1 kHz | Enabled after 8 ms. Disabled after 0.75 s. |
| 100 Hz, then stuck high instead of low | Disabled after 1.11 s |
| 100 Hz, C23 30 % low | Disabled after 0.52 s |
| 1 kHz, C23 20 % high | Disabled after 0.90 s |
| 2 Hz for 2.4 s | Never enabled. Reservoir peaks at 1.42 V against the 1.55 V threshold. |
| One rising edge, then stuck high | Never enabled. Reservoir peaks at 1.00 V. |
| Stuck high from power-up | Never enabled |
| Stuck low from power-up | Never enabled |

The hold-up is longer when the pin stops high, because C22 then stays in
circuit and discharges with C23.

Review A1 added `hardware/sim/keepalive_faults.cir`. It runs more toggle
rates, tolerance and temperature corners, and every single component fault.
The fault results are in section 8.5. The rest:

| Question | Result |
|---|---|
| Slowest rate that enables | 1 Hz never did. 2 Hz does not with nominal parts but does with C23 30 % low, C22 20 % high and -15 mV of comparator offset. 3 Hz and faster always did. |
| Fastest rate that enables | No limit found. 50 kHz enables after 9 ms and 1 MHz after 28 ms. |
| Duty cycle | 2 % and 98 % at 100 Hz both enable, after about 0.1 s. |
| Hold-up, pin stops low, -20 to 60 °C with tolerances | 0.47 to 0.80 s |
| Hold-up, pin stops high, longest | 1.19 s |
| Hold-up at 85 °C | 0.44 s with nominal parts, 0.30 s at the short corner. Schottky leakage adds to R30. The leakage model is generic. |
| Time to enable at 100 Hz | 14 ms nominal, 41 ms at the slow corner |
| Time to enable at 25 Hz | 43 ms nominal, 0.16 s at the slow corner |
| Time to enable at 10 Hz | 0.10 s nominal, 0.50 s at the slow corner, where the reservoir clears the threshold by 11 % only |
| `3V3` ramps over 0.1 s or 1 s | `KA_OK` stays low with the pin low. With the pin tied to the rail it follows the rail to 0.95 V and falls again, below the voltage at which U7 or the expander work. |

The short corner is C23 at 7 uF, C22 at 3.3 uF, R30 1 % low, `3V3` at 3.2 V
and +15 mV of comparator offset. The slow corner is C23 at 12 uF, C22 at
3.3 uF, a high level of 2.9 V on the pin, +15 mV of offset and -20 °C.

**Specification for firmware.**

- **Accepted**: an edge at least every 20 ms. The firmware's 5 ms is the design point. 10 Hz works with nominal parts and has too little margin at the slow corner to rely on.
- **No upper limit**: the detector is a rectifier, not a window. A square wave at 50 kHz or 1 MHz enables drive as well as 100 Hz does. An earlier version of this section gave 5 kHz as a limit. That was an estimate and it was wrong.
- **Hold-up**: drive is removed 0.45 to 0.9 s after the last edge if the pin stops low, and at most 1.2 s if it stops high. Above 60 °C on the board the lower figure falls towards 0.3 s.
- **Not accepted**: a steady level, high or low. Slow toggling is not reliably rejected: between 2 and 3 Hz the result depends on part tolerances. Treat only a steady level as guaranteed off.
- **Source**: code that decides every edge from evidence that the control loop is running. Never an output that a peripheral generates by itself: LEDC, RMT, MCPWM or a timer with a pin output keep toggling when the firmware hangs, and the circuit cannot tell the difference. Section 19.4 has the contract agreed with the firmware.

The firmware team asked about the flash sector-erase time, during which both
cores stall. Espressif does not name the flash part. The module data sheet
v1.8 says only that the flash runs at 80 MHz and does not support
auto-suspend. The Winbond W25Q128JV, one 16 MB part of the kind fitted, gives
45 ms typical and 400 ms maximum for a 4 kB sector erase, and 150 ms typical
and 2 s maximum for a 64 kB block erase. Section 19.4 says what that means for
the firmware's heartbeat timeout.

### 8.5 Single-fault analysis

States of the keep-alive pin and of the firmware:

| Fault or state | What happens | Outputs |
|---|---|---|
| `KA` stuck low | No edges. C23 drains through R30. | Off within 0.9 s |
| `KA` stuck high | C22 blocks DC. C23 drains. | Off within 1.2 s |
| `KA` floating: chip in reset, EN low, or unprogrammed | R33 pulls it low. | Off |
| `KA` toggling at the wrong rate | Anything from about 3 Hz up enables drive, whatever the duty cycle. | The circuit does not catch this. See section 8.4. |
| USB flashing | The ROM loader runs. GPIO38 stays an input. No edges. | Off |
| Boot | GPIO38 has no power-up glitch in datasheet table 2-2. One edge could not enable drive anyway. | Off |
| Brownout of `3V3` | The ESP32 resets and toggling stops. The expander keeps its bits down to about 1 V. | Off within 0.9 s. Until then the outputs that were on stay on. |
| Firmware hangs with outputs on | Toggling stops. | Off within 1.2 s of the last edge |
| I2C bus stuck by a fault | Firmware cannot write the expander. It must stop toggling. | Off within 0.9 s of that decision |
| Expander bit stuck low inside the chip | That valve follows `V_DRV`. Firmware sees current it did not ask for and stops toggling. | Off after firmware reacts |
| GPIO40 driven as an output by mistake | R87 keeps it from moving the latch node. | No effect |

Single component faults in the detector, each simulated in
`hardware/sim/keepalive_faults.cir`. A resistor is taken to fail open, a
capacitor open or short, and a diode or an IC output either way.

| Fault | What happens | Drive |
|---|---|---|
| R29 open, C22 open, D7A short or open, D7B short or open, C23 short, C24 short, R35 open | No charge reaches the comparator, or the reference sits at the rail. | Never enabled. Found at the first run. |
| R31 open from power-up | The comparator input stays near 0.27 V. | Never enabled |
| C23 open | `KA_OK` follows the keep-alive and resets the expander on every cycle. | Unusable. Found at the first run. |
| R32 open | No hysteresis. Hold-up falls to 0.46 s. | Works |
| R33 open | No pull-down. A pin in reset floats and gives no edges. | Works |
| R36 open | `KA_OK` is pulled up through R32 only, to 2.9 V. | Works |
| **R30 open** | Nothing drains C23. R32 feeds it from `KA_OK`, so it rises to 3.26 V and stays. | **Enabled for ever after the first run** |
| **R31 opens while drive is enabled** | R32 holds the comparator input at 3.3 V. | **Enabled until power is cycled** |
| **R34 open** | The reference is at 0 V. The comparator output is high from power-up. | **Enabled with no keep-alive at all** |
| **C22 short** | The detector passes DC. With a toggling pin it behaves normally. | **A pin stuck high keeps drive enabled** |
| **U6A output open, or U7 gate 3 or 4 stuck** | `DRV_EN` stays high. | **Enabled for ever** |
| Q3 shorted drain to source | `V_DRV` stays on. | Expander reset and relay still turn everything off |
| Triac, TVS or opto-triac shorted | LED drive is irrelevant. | Relay K1, section 7 |

**The five bold rows are the weak point of this circuit.** In each one the
valves still switch normally through the expander, so nothing shows. If the
firmware then stops with a valve on, nothing removes drive: the LED supply,
the expander reset and the relay all hang on the one `DRV_EN` signal. The
first draft of this section listed only the U6A and U7 case and relied on a
test at start-up. A controller that stays powered for a season is not tested
often enough that way. Review A1 rates this as a blocking finding and proposes
a second, independent detector for the relay. See hardware review A1 (`hardware-review-a1.md`), finding B1. Until that is
decided, the test below is the only cover.

**Self-test before every run.** Firmware stops the keep-alive whenever no
output is wanted, so `DRV_EN` should be low before each run. The expander
answers on I2C only when `DRV_EN` is high. That makes a complete test
possible at no cost, before every run and not only at start-up:

1. With `KA` low, probe address 0x74. It must not answer. An answer means a stuck-enabled fault.
2. At start-up, also hold `KA` high for 1.5 s and probe again. It must not answer. An answer means C22 has failed short.
3. Start toggling. The expander must answer within 110 ms.
4. After the run, stop toggling. Within 1.5 s the expander must stop answering.

If step 1, 2 or 4 fails, firmware must write every expander pin back to input,
because the hardware will not do it, then raise a fault and refuse to run
valves until the board has been repaired.

### 8.6 Boot behaviour of the pins

The reset state and power-up glitch of every GPIO are in `hardware/pinmap.yaml`,
taken from tables 2-1 and 2-2 of the ESP32-S3 datasheet. The ones that matter
here:

| Pin | Use | At reset | Power-up glitch | Consequence |
|---|---|---|---|---|
| GPIO38 | `KA` | High impedance | None | R33 holds it low |
| GPIO8, GPIO9 | I2C0 | High impedance | Driven low for 60 us | Not a valid I2C write. The expander is in reset anyway. |
| GPIO7 | Sensor supply enable | High impedance | Driven low for 60 us | R86 holds it low. Supply stays off. |
| GPIO21 | Backlight | High impedance | None | R62 holds the MOSFET off |
| GPIO18 | Back button | High impedance | Low, then high, 60 us each | Input only. Harmless. |
| GPIO19, GPIO20 | USB | USB pull-up on D+ | High pulses | USB only |
| GPIO0, 3, 45, 46 | Strapping | See section 12.3 | None listed for 0, 45, 46 | No valve function |

## 9. Hardware over-current path

**Decision: yes.** A comparator on the shunt signal removes drive without
firmware. The reason is the triac's I²t. Firmware samples current in 100 ms
windows. In 100 ms a shorted wire puts ten half-cycles of fault current through
the triac. The hardware path limits it to one.

### 9.1 Circuit

The parts are in the table of section 10.1. U6B and U6C are a window comparator
on `ISENSE_LO`, the low-gain current signal, after the 47 us filter R52 and
C33. Their open-collector outputs are tied together as `OC_TRIP_N`.

- **U6B** pulls `OC_TRIP_N` low when the signal is above `VH`.
- **U6C** pulls it low when the signal is below `VL`.

`VH`, `VB` and `VL` come from one resistor chain, R48 to R51, so the two
thresholds stay centred on the amplifier's bias.

| Node | Calculation | Value |
|---|---|---|
| `VL` | 3.3 x 100 / 1760 | 0.188 V |
| `VB` | 3.3 x 430 / 1760 | 0.806 V |
| `VH` | 3.3 x 760 / 1760 | 1.425 V |
| Trip current | 0.619 V / 0.1333 V/A | 4.64 A peak, either polarity |

Tolerances: 1 % resistors in the chain and the gain, 1 % shunt, 3 % on `3V3`,
15 mV of comparator offset and 10 mV of amplifier offset. Added as worst case
they give 4.2 to 5.1 A peak. The largest legitimate current is two zones
holding at 0.5 A and a third in inrush at 1.5 A, which is 2.5 A rms or 3.54 A
peak if all are in phase. The trip level clears it.

The latch is two NAND gates of U7. `OC_TRIP_N` low sets `FAULT`. `KA_OK` low
clears it. The red LED D8 shows it.

### 9.2 Timing

`hardware/sim/isense.cir` adds a 10 A rms short to a 1 A load at a rising point
of the cycle. `OC_TRIP_N` falls 66 us after the short begins and `DRV_EN_N`
rises within a microsecond of that. The opto LED goes dark. The triac conducts
to the end of its half-cycle, at most 10 ms at 50 Hz, and is not fired again.
At fault currents above about 6 A rms its data sheet does not guarantee that
it blocks at that zero (section 6.6). K1 loses its coil current at the same
moment and opens within 10 ms, so the total still meets "within about one
mains cycle", with K1 as the device that guarantees it.

### 9.3 Clearing the latch

There is no clear pin. Firmware clears the latch by stopping the keep-alive
for longer than the hold-up time. That also resets the expander, so every
output is off when drive returns. Firmware reads the latch on GPIO40,
`OC_FAULT_N`, low when set.

GPIO40 reads the latch through R87, 10 kohm. `FAULT_N` is the feedback node
of the latch: it feeds U7 pins 2 and 10. Review A1 found that with GPIO40
wired straight to it, a pin left configured as an output and driven high
could hold the node above the gate threshold against the 74HC00 output. That
clears or masks the latch, and firmware then reads "no fault". With R87 the
pin can move the node by at most 3.3 V x 10 kohm against the gate's output
resistance of under 100 ohm, about 30 mV.

After a trip firmware knows which output it switched on last. It should mark
that output faulted, wait, restart the keep-alive and continue with the others.

### 9.4 What this path does not cover

- **A weak transformer.** If a short draws less than 4.2 A peak, about 3 A rms, the comparator does not trip. Firmware's rms limit catches it within a few hundred milliseconds. The triac is rated 4 A continuous, so it survives that.
- **A shorted triac.** Section 7.
- **Noise.** A trip from a noise spike latches and costs one retry. R52 and C33 reject anything shorter than about 50 us. Whether that is enough is a bench question.

## 10. Current measurement

### 10.1 Circuit

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| R37 | 0.1R 1% 2 W metal strip | 2512 | C160587 | `ISNS` to `GND` | Kelvin-sense both pads |
| U8 | MCP6002T-I/SN dual op-amp | SOIC-8 | C7377 | 1 OUTA `ISENSE_LO`, 2 INA- `SA_N`, 3 INA+ `SA_P`, 4 VSS `GND`, 5 INB+ `SB_P`, 6 INB- `SB_N`, 7 OUTB `ISENSE_HI`, 8 VDD `3V3` |  |
| C25 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U8 |
| R38 | 7.5k 1% | 0603 | C23234 | `ISNS` to `SA_N` | From the shunt high pad |
| R39 | 10k 1% | 0603 | C25804 | `SA_N` to `ISENSE_LO` |  |
| C26 | 1 nF 50 V X7R | 0603 | C1588 | `SA_N` to `ISENSE_LO` |  |
| R40 | 7.5k 1% | 0603 | C23234 | `GND` to `SA_P` | From the shunt ground pad |
| R41 | 10k 1% | 0603 | C25804 | `SA_P` to `VB` |  |
| C27 | 1 nF 50 V X7R | 0603 | C1588 | `SA_P` to `VB` |  |
| R42 | 7.5k 1% | 0603 | C23234 | `ISNS` to `SB_N` | From the shunt high pad |
| R43 | 49.9k 1% | 0603 | C23184 | `SB_N` to `ISENSE_HI` |  |
| C28 | 220 pF 50 V C0G | 0603 | C27675 | `SB_N` to `ISENSE_HI` |  |
| R44 | 7.5k 1% | 0603 | C23234 | `GND` to `SB_P` | From the shunt ground pad |
| R45 | 49.9k 1% | 0603 | C23184 | `SB_P` to `VB` |  |
| C29 | 220 pF 50 V C0G | 0603 | C27675 | `SB_P` to `VB` |  |
| R46 | 1k 1% | 0603 | C21190 | `ISENSE_LO` to `ADC_LO` |  |
| C30 | 100 nF 50 V X7R | 0603 | C14663 | `ADC_LO` to `GND` | At the module pin |
| R47 | 1k 1% | 0603 | C21190 | `ISENSE_HI` to `ADC_HI` |  |
| C31 | 100 nF 50 V X7R | 0603 | C14663 | `ADC_HI` to `GND` | At the module pin |
| R48 | 1k 1% | 0603 | C21190 | `3V3` to `VH` |  |
| R49 | 330R 1% | 0603 | C23138 | `VH` to `VB` |  |
| R50 | 330R 1% | 0603 | C23138 | `VB` to `VL` |  |
| R51 | 100R 1% | 0603 | C22775 | `VL` to `GND` |  |
| C32 | 1 uF 50 V X5R | 0603 | C15849 | `VB` to `GND` |  |
| R52 | 10k 1% | 0603 | C25804 | `ISENSE_LO` to `OC_CMP` |  |
| C33 | 4.7 nF 50 V X7R | 0603 | C1621 | `OC_CMP` to `GND` |  |
| R53 | 10k 1% | 0603 | C25804 | `3V3` to `OC_TRIP_N` | Pull-up |
| R54 | 1k 1% | 0603 | C21190 | `FAULT` to `LED_FLT` |  |
| D8 | LED red | 0603 | C2286 | A `LED_FLT`, K `GND` | Lit while the over-current latch is set |
| R87 | 10k 1% | 0603 | C25804 | `FAULT_N` to `OC_FAULT_N` | Stops GPIO40 from overdriving the latch node |

### 10.2 Shunt R37

0.1 ohm, 1 %, 2 W metal strip in 2512. At the 1.5 A rms continuous limit it
dissipates 0.23 W and drops 0.15 V. At 2.5 A during inrush, 0.63 W. In a 20 A
short the voltage on `ISNS` reaches 2.8 V peak for one half-cycle.

OpenSprinkler 3.4 uses 1 ohm, which would be 2.3 W here. 0.1 ohm keeps the
heat down. It leaves a small signal, 5 mV rms at 50 mA, so the amplifier must
sense the shunt with Kelvin connections. R38 and R42 go to the pad on the
`ISNS` side. R40 and R44 go to the pad on the `GND` side, with their own trace,
not to the ground plane.

### 10.3 Amplifiers

Two difference amplifiers in U8, an MCP6002, both referenced to `VB`.

| Output | Gain | Scale | Linear range | Use |
|---|---|---|---|---|
| `ISENSE_LO` | 10k / 7.5k = 1.333 | 133.3 mV/A | +/-5.8 A peak | Totals above 0.7 A rms, inrush, the comparators |
| `ISENSE_HI` | 49.9k / 7.5k = 6.653 | 665.3 mV/A | +/-1.17 A peak | One valve, open-circuit detection |

The outputs are AC waveforms centred on about 0.79 V. The simulation gives
0.787 V with the load of the chain included. The ranges keep both outputs
between 0.025 V and 1.6 V, the span that ADC attenuation 6 dB covers.

Because both amplifiers are difference amplifiers fed from the same two shunt
pads, a voltage between the shunt's ground pad and the amplifier's ground does
not appear as signal. The simulation applies 20 mV peak of ground shift and
finds 0.9 mV peak to peak at each output.

C26 to C29 set a low-pass at about 15 kHz. R46 with C30, and R47 with C31, are the
1.6 kHz filters at the ADC pins. They attenuate 60 Hz by 0.07 %.

**Input protection.** The inputs connect to `ISNS` through 7.5 kohm. The
MCP6002 allows 2 mA into an input that is driven past a rail. So `ISNS` may
reach about 15 V beyond the rails before that limit. A 20 A short gives 2.8 V
and a TVS discharge of 50 A gives 5 V. No clamp diodes are needed.

### 10.4 Why no rectifier, and why the internal ADC

The specification asks for a rectifying and averaging front end. I left the
rectifier out for three reasons.

- **Error**: a precision rectifier adds the offset of its op-amp to a signal that is 5 mV rms at the shunt for a 50 mA load.
- **The bias requirement**: a rectified, averaged output sits near 0 V at low current, the region of the ESP32 ADC that the specification says to avoid. It would need an offset added back.
- **Information**: the waveform gives true rms, the inrush peak and any DC component. A DC component means a triac is conducting in one direction only.

I chose ADC1 over an external I2C ADC because the measurement needs a few
thousand samples per second for 100 ms, which suits the ESP32's continuous ADC
mode and suits I2C badly. ESPHome's `ct_clamp` sensor already does this
calculation on a biased AC input.

This changes what firmware does. The architecture document assumes a DC level
sampled 16 times per 50 ms. Averaging this signal gives the bias, not the
current. Section 19 has the method.

### 10.5 Resolution and error budget

The ADC at 6 dB attenuation covers 0 to 1600 mV with a total error of 10 mV
after calibration, DNL of 4 and INL of 8 counts, per datasheet tables 5-5 and
5-6. One count is about 0.4 mV.

| Current | Channel | Signal | In counts |
|---|---|---|---|
| 50 mA rms | `ISENSE_HI` | 33 mV rms, 94 mV peak to peak | 83 rms |
| 50 mA rms | `ISENSE_LO` | 6.7 mV rms | 17 rms |
| 250 mA rms | `ISENSE_HI` | 166 mV rms | 415 rms |
| 1.5 A rms | `ISENSE_LO` | 200 mV rms, 566 mV peak to peak | 500 rms |
| 1.5 A rms | `ISENSE_HI` | clipped | not usable |

Error of an rms reading taken over 100 ms, after firmware subtracts the mean:

| Source | Size | Note |
|---|---|---|
| Shunt | 1 %, plus 0.2 % for 40 K at 50 ppm/K | |
| Gain resistors | 2 % worst case, two 1 % parts | |
| ADC gain and linearity | about 1.5 % | My reading of the 10 mV total error against a 0.6 V swing. The datasheet does not split gain from offset. |
| ADC noise | adds in quadrature | The datasheet gives no noise figure. At 2 mV rms it adds 0.2 % at 50 mA on the high-gain channel. |
| Amplifier and ADC offset | none | Removed with the mean |
| **Total** | **about 4 % of reading** | Plus a floor of a few milliamps set by noise |

The fault thresholds in firmware compare a reading with a value learned on the
same board, so the absolute error matters less than repeatability. An open
valve at 0 mA against 200 mA, or a short at 3 A against 0.3 A, is far outside
4 %.

## 11. AC presence and zero-cross

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| R55 | 100k 1% 200 V 0.25 W | 1206 | C17900 | `AC_L` to `ZC_CLAMP` |  |
| D9 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `3V3`, 3 common `ZC_CLAMP` | Clamp |
| R88 | 10k 1% | 0603 | C25804 | `ZC_CLAMP` to `ZC_DIV` | Halves the clamped level at the comparator pin |
| R56 | 10k 1% | 0603 | C25804 | `ZC_DIV` to `GND` |  |
| C34 | 10 nF 50 V X7R | 0603 | C1589 | `ZC_DIV` to `GND` |  |
| R57 | 1M 1% | 0603 | C22935 | `ZC_DIV` to `AC_ZC` | Hysteresis |
| R58 | 10k 1% | 0603 | C25804 | `3V3` to `AC_ZC` | Pull-up |

R55, R88 and R56 divide `AC_L` by 12 at the comparator pin. D9 clamps the node
between R55 and R88 to one Schottky drop outside the rails. Comparator U6D
compares `ZC_DIV` with `VL`, 0.188 V, and R57 adds 30 mV of hysteresis.
`AC_ZC` is high while `AC_L` is above about 2.3 V and low otherwise. It is a
square wave at mains frequency.

**Why R88 is there.** The first draft had no R88: the clamp node went straight
to the comparator pin. The LM2901 allows -0.3 V at an input, and its data
sheet (TI SLCS006S, note 4 of the electrical table) says neither input may go
more than 0.3 V negative. The BAT54S is specified at 240 mV at 0.1 mA and
320 mV at 1 mA, maximum, at 25 °C. With 0.3 to 0.45 mA in the clamp every
negative half-cycle that is -0.28 to -0.29 V at 25 °C and about -0.36 V at
-20 °C. U6 also holds the keep-alive and over-current comparators, so an input
at its limit is not acceptable there. R88 and R56 halve the clamped level:
`hardware/sim/zerocross.cir` gives -0.18 V at the pin at -20 °C and 32 VAC, and
+1.83 V at the positive clamp.

- **Edge timing**: the rising edge is at 2.3 V on `AC_L`. At 24 VAC and 50 Hz the simulation puts it 0.30 ms after the zero crossing: 0.21 ms for the threshold and 0.09 ms for C34. The falling edge is 0.09 ms before the next zero crossing, because the same C34 delay now subtracts. So the high period is not centred on the positive peak. Its midpoint is 0.1 ms late. At 32 VAC the figures are 0.25 ms and 0.04 ms.
- **Presence**: no edge of either polarity for 25 ms means the 24 VAC is gone or F1 has opened. C1 holds the logic for about 100 ms after that, long enough to log it.
- **Frequency**: firmware gets 50 or 60 Hz from the period.
- **Dissipation**: R55 is a 1206 part rated 200 V. It dissipates 9 mW at 32 VAC.

The detector is on `AC_L`, ahead of relay K1, so it works whether or not the
valves have power. `VIN_MON` on GPIO3 complements it: `VIN_DC` divided by 31
tells firmware how high the supply is. About 0.97 V at the pin means 24 VAC,
1.43 V means 32 VAC, and a reading that sags when a valve starts means the
transformer is too small.

## 12. MCU block

### 12.1 Module

U4 is the ESP32-S3-WROOM-1-N16R8: PCB antenna, 16 MB quad flash, 8 MB octal
PSRAM. JLCPCB had 24,221 in stock on 2026-11-10.

- **PSRAM**: one 320x240 frame at 16 bits is 154 kB. LVGL draw buffers of that size, plus TLS, BLE provisioning and the web server, do not fit in the internal RAM together. 8 MB of PSRAM removes the question.
- **Flash**: 16 MB holds two OTA slots and the web assets with room to spare. The specification asks for at least 8 MB.
- **Pins lost**: the octal PSRAM takes GPIO35, 36 and 37. The pin map does not use them.
- **Temperature**: the R8 modules are rated to 65 °C ambient, not 85 °C. The module datasheet says the limit rises to 85 °C when PSRAM ECC is enabled, at the cost of one sixteenth of the PSRAM. **Firmware should enable PSRAM ECC.** Section 20 lists the enclosure temperature as a risk.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| U4 | ESP32-S3-WROOM-1-N16R8 | module 18x25.5 mm | C2913202 | see pin map |  |
| C14 | 22 uF 25 V X5R | 1206 | C12891 | `3V3` to `GND` | At module pin 2 |
| C15 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At module pin 2 |
| R18 | 10k 1% | 0603 | C25804 | `3V3` to `EN` |  |
| C16 | 1 uF 50 V X5R | 0603 | C15849 | `EN` to `GND` |  |
| SW1 | Tactile switch 5.1x5.1 mm, 1.5 mm high | SMD | C318884 | `EN` to `GND` | Reset |
| R19 | 10k 1% | 0603 | C25804 | `3V3` to `BOOT` |  |
| SW2 | Tactile switch 5.1x5.1 mm, 1.5 mm high | SMD | C318884 | `BOOT` to `GND` | Boot |
| R20 | 680R 1% | 0603 | C23228 | `STATUS_LED` to `LED_ST` |  |
| D6 | LED yellow-green | 0603 | C125094 | A `LED_ST`, K `GND` | Power / status |
| R21 | 2.2k 1% | 0603 | C4190 | `3V3` to `I2C0_SDA` |  |
| R22 | 2.2k 1% | 0603 | C4190 | `3V3` to `I2C0_SCL` |  |
| J6 | JST SH 4-way 1.0 mm, side entry | SMD | C160404 | 1 `GND`, 2 `QW_3V3`, 3 `QW_SDA`, 4 `QW_SCL` | Qwiic |
| F2 | PTC 0.125 A hold, 30 V | 1206 | C151160 | `3V3` to `QW_3V3` |  |
| C17 | 100 nF 50 V X7R | 0603 | C14663 | `QW_3V3` to `GND` |  |
| R23 | 4.7k 1% | 0603 | C23162 | `3V3` to `QW_SDA` |  |
| R24 | 4.7k 1% | 0603 | C23162 | `3V3` to `QW_SCL` |  |
| U9 | USBLC6-2SC6 ESD array | SOT-23-6 | C7519 | 1 I/O1 `QW_SDA`, 2 GND `GND`, 3 I/O2 `QW_SCL`, 4 I/O2 `QW_SCL`, 5 VBUS `QW_3V3`, 6 I/O1 `QW_SDA` | Place at J6 |
| J8 | Header pads 1x6, 2.54 mm | PCB | none | 1 `3V3`, 2 `GND`, 3 `U0TXD`, 4 `U0RXD`, 5 `EN`, 6 `BOOT` | Factory and debug |

The net on every module pin is in `hardware/netlist.yaml` under U4, and the
same assignment with electrical notes is in `hardware/pinmap.yaml`.

### 12.2 Pin budget

The module brings out 36 GPIOs. Not usable for general signals: GPIO35 to 37
for PSRAM, GPIO19 and 20 for USB, GPIO43 and 44 for the UART on the debug
header, and the four strapping pins. That leaves 25.

| Function | Pins |
|---|---|
| Display: SCLK, MOSI, CS, DC, RST, backlight | 6 |
| Encoder A, B, push, and two buttons | 5 |
| Internal I2C | 2 |
| Qwiic I2C | 2 |
| Rain, flow, spare sensor | 3 |
| Sensor supply enable | 1 |
| Current sense, two ADC channels | 2 |
| Keep-alive, over-current flag, zero-cross | 3 |
| **Total without valves** | **24** |

Nine direct valve pins would bring it to 33. With the expander, 24 of the 25
are used and GPIO42 is spare. Two more signals sit on strapping pins where
they can do no harm: the supply monitor on GPIO3 and the status LED on GPIO46.

Qwiic has its own I2C controller, I2C1. A faulty or badly wired expansion board
can then hang only its own bus. The valve expander and the RTC are on I2C0.

### 12.3 Strapping pins

| Pin | Strap function | Needed at reset | What is connected | Why it is safe |
|---|---|---|---|---|
| GPIO0 | Boot mode | High for normal boot | SW2 to `GND`, R19 10k to `3V3`, J8 | Standard boot button. Not on the front panel. |
| GPIO3 | JTAG source | Ignored unless eFuses are burned | `VIN_MON` divider, 10k to `GND` | Reads low, the same as the default floating case would be treated. Analogue input only. |
| GPIO45 | Flash voltage | Low for 3.3 V | Nothing. Test pad TP25. | The internal pull-down sets it. |
| GPIO46 | Boot mode, ROM messages | Low | R20 and LED D6 to `GND` | The LED path can only pull down. |

### 12.4 Supply, reset and boot

C14, 22 uF, and C15, 100 nF, sit at module pin 2, as in the module datasheet's
reference circuit. R18, 10 kohm, and C16, 1 uF, delay EN by about 10 ms after
`3V3` rises. The datasheet recommends those values. SW1 pulls EN low.

Flashing over USB needs no extra parts. The ESP32-S3's USB Serial/JTAG
controller resets the chip and enters download mode by itself. For a factory
jig, J8 carries `3V3`, `GND`, TXD0, RXD0, EN and IO0 on 2.54 mm pads.

### 12.5 Layout notes for this block

- **Antenna**: the module's land pattern in datasheet figure 11-1 marks an antenna area of 18 x 6 mm at the top of the module. Put the module on a board edge with that area beyond the edge, or with no copper on any layer under it. I read only the land pattern. The clearance to keep around the antenna comes from Espressif's hardware design guidelines, which I did not read for this document.
- **Metal near the antenna**: keep C1, K1, the display frame and the terminal blocks away from the antenna end. I have no number for "away". RF performance is a bench item.
- **USB**: route D+ and D- as a pair from J5 through U2 to module pins 14 and 13. Keep it short and without stubs. This is full-speed USB at 12 Mbit/s.
- **ADC**: C30 and C31 go at module pins 39 and 38. Route `ISENSE_LO` and `ISENSE_HI` away from L1, the display clock and the antenna.
- **Display SPI**: see section 15.3.

## 13. Sensor inputs and sensor supply

### 13.1 Circuit

Three identical inputs: `T_RAIN`, `T_FLOW` and `T_AUX` at the terminals.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J4 | Header 7-way 3.81 mm pluggable, right angle | THT | C441187 | 1 `SENS_5V`, 2 `T_FLOW`, 3 `GND`, 4 `T_RAIN`, 5 `GND`, 6 `T_AUX`, 7 `GND` | Sensors |
| D12 | TVS 48 V bidirectional 600 W | SMB | C151264 | `T_RAIN` to `GND` | Terminal clamp |
| R74 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `V5` to `T_RAIN` | Pull-up |
| R75 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `T_RAIN` to `N_RAIN` | Series |
| D15 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `3V3`, 3 common `N_RAIN` | Clamp to the rails |
| C44 | 22 nF 50 V X7R | 0603 | C21122 | `N_RAIN` to `GND` |  |
| R76 | 1k 1% | 0603 | C21190 | `N_RAIN` to `SENS_RAIN` |  |
| D13 | TVS 48 V bidirectional 600 W | SMB | C151264 | `T_FLOW` to `GND` | Terminal clamp |
| R77 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `V5` to `T_FLOW` | Pull-up |
| R78 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `T_FLOW` to `N_FLOW` | Series |
| D16 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `3V3`, 3 common `N_FLOW` | Clamp to the rails |
| C45 | 22 nF 50 V X7R | 0603 | C21122 | `N_FLOW` to `GND` |  |
| R79 | 1k 1% | 0603 | C21190 | `N_FLOW` to `SENS_FLOW` |  |
| D14 | TVS 48 V bidirectional 600 W | SMB | C151264 | `T_AUX` to `GND` | Terminal clamp |
| R80 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `V5` to `T_AUX` | Pull-up |
| R81 | 10k 1% 200 V 0.25 W | 1206 | C17902 | `T_AUX` to `N_AUX` | Series |
| D17 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `3V3`, 3 common `N_AUX` | Clamp to the rails |
| C46 | 22 nF 50 V X7R | 0603 | C21122 | `N_AUX` to `GND` |  |
| R82 | 1k 1% | 0603 | C21190 | `N_AUX` to `SENS_AUX` |  |
| U11 | TPS2553DBVR current-limited switch | SOT-23-6 | C55266 | 1 IN `V_BUCK`, 2 GND `GND`, 3 EN `SENS_PWR_EN`, 4 FAULT `SENS_FLT_N`, 5 ILIM `SP_ILIM`, 6 OUT `SP_OUT` |  |
| C47 | 1 uF 50 V X5R | 0603 | C15849 | `V_BUCK` to `GND` | At U11 |
| C49 | 1 uF 50 V X5R | 0603 | C15849 | `SP_OUT` to `GND` |  |
| R83 | 200k 1% | 0603 | C25811 | `SP_ILIM` to `GND` | Current limit about 137 mA |
| R85 | 10k 1% | 0603 | C25804 | `3V3` to `SENS_FLT_N` |  |
| R86 | 100k 1% | 0603 | C25803 | `SENS_PWR_EN` to `GND` |  |
| D19 | SS210 Schottky 2 A 100 V | SMA | C14996 | A `SP_OUT`, K `SP_B` | Blocks a positive miswire |
| R84 | 4.7R 1% 0.25 W | 1206 | C17937 | `SP_B` to `SP_A` |  |
| D20 | SS210 Schottky 2 A 100 V | SMA | C14996 | A `GND`, K `SP_A` | Clamps a negative miswire |
| C48 | 10 nF 100 V X7R | 0805 | C18741 | `SP_A` to `GND` |  |
| F3 | PTC 0.2 A hold, 60 V | 1812 | C315900 | `SP_A` to `SENS_5V` |  |
| D18 | TVS 48 V bidirectional 600 W | SMB | C151264 | `SENS_5V` to `GND` | Terminal clamp |

### 13.2 Signal path

Each terminal has a 10 kohm pull-up to `V5`, a second 10 kohm in series, a
BAT54S clamp to `3V3` and `GND`, 22 nF to ground and 1 kohm into the GPIO.

- **Open contact**: the GPIO node sits at `3V3` plus one Schottky drop, about 3.55 V. The ESP32 allows VDD + 0.3 V.
- **Closed contact**: the node is at 0 V. The input reads low for any contact and cable resistance under 1.9 kohm, from the 0.25 x VDD low threshold.
- **Wetting current**: 0.5 mA at 5 V. A pull-up to `3V3` would give 0.33 mA at 3.3 V. Outdoor contacts want what they can get.
- **Open-collector sensors**: the same input works. The sensor pulls the terminal low.

The filter passes 200 Hz. Falling, the 22 nF discharges through 10 kohm and
crosses the low threshold after 0.22 ms x ln(3.55 / 0.83) = 0.32 ms. Rising, it
charges through 20 kohm toward 4.95 V and crosses the high threshold of 2.48 V
after 0.44 ms x ln(4.95 / 2.47) = 0.31 ms. A 200 Hz square wave has 2.5 ms
half-periods, eight times that. The filter does not remove the 1 to 2 ms
contact bounce of a reed-switch meter. Firmware must do that.

### 13.3 A 24 VAC miswire on a signal terminal

Worst case is 32 VAC, 45.3 V peak, applied between the terminal and `GND`
indefinitely.

| Part | Stress | Rating |
|---|---|---|
| TVS D12 | 45.3 V, below its 48 V stand-off. Does not conduct. | |
| Pull-up R74 | 4.0 mA peak into `V5`. About (32² + 5²) / 10k = 0.11 W. | 0.25 W, 200 V |
| Series R75 | 4.2 mA peak into the clamp. About 0.10 W. | 0.25 W, 200 V |
| Clamp D15 | 4.2 mA. | 200 mA |
| GPIO | Node held between -0.35 V and 3.65 V, then 1 kohm | -0.3 V to VDD + 0.3 V |

The clamp current flows into `3V3` on positive half-cycles, 1.3 mA on average
per input. The rail always carries far more load than that, so it cannot be
pushed up. The input reads a 50 or 60 Hz square wave while the miswire lasts.

A wire touching the sensor `GND` terminals is different. Those terminals are
`AC_N`. Touching `AC_L` or `COM` to them short-circuits the transformer and
opens F1.

### 13.4 Surge on a sensor cable

The TVS limits the terminal to 77.4 V at its rated 7.8 A, 10/1000 us. Behind it
the series resistor sees at most 74 V and passes 7 mA into the clamp. I did not
calculate against a specific surge standard. The 600 W TVS on a line with no
other series impedance is the weak point, and surge immunity is a bench item.
The specification also asks for PCB spark gaps on field terminals where layout
allows. That is a layout task for every terminal of J2, J3 and J4.

### 13.5 Sensor supply terminal

One terminal, labelled 5V, for flow meters with electronics. It comes from
`V_BUCK`, so it is off on USB power. GPIO7 switches it.

U11 is a TPS2553 current-limited switch. With R83 = 200 kohm the limit is about
137 mA. I scaled that from the datasheet's 130 mA at 210 kohm and did not use
the datasheet's formula. Its fault output goes to expander bit P15.

Voltage at the terminal:

| Load | D19 | R84 | F3 | Terminal |
|---|---|---|---|---|
| 20 mA | 0.30 V | 0.09 V | 0.03 to 0.09 V | about 4.8 V |
| 50 mA | 0.35 V | 0.24 V | 0.07 to 0.22 V | 4.5 to 4.6 V |

The diode drops are my estimates from the SS210's 0.85 V at 2 A. **A sensor
that needs 5 V within 5 % at more than about 30 mA is not supported.** Most
Hall-effect flow meters accept 4.5 to 24 V. I chose one voltage because a
second rail would need its own protection chain. `VIN_DC` is not offered: it
reaches 44 V.

**Miswire, terminal forced to +45 V.** D19 blocks. D20, C48 and the TVS D18
all see less than their ratings of 100 V, 100 V and 48 V. No current flows.

**Miswire, terminal forced to -45 V on alternate half-cycles.** D20 conducts
and holds node `SP_A` near -1 V. The PTC F3, 1.4 to 4.4 ohm cold, takes the
rest and trips. Its datasheet gives 2 s to trip at 1 A, and the fault current
here is several amps, so it trips much sooner. D20 is rated 50 A for one
half-cycle. Meanwhile U11 limits its own current to 137 mA, and R84 with D19
keeps its output pin at about 0 V, inside its -0.3 V limit.

**Short to ground.** U11 limits at 137 mA and reports the fault. F3 holds at
0.2 A and does not trip.

## 14. Real-time clock and backup

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| U10 | PCF8563T/5 real-time clock | SO-8 | C7440 | 1 OSCI `OSCI`, 2 OSCO `OSCO`, 3 INT `RTC_INT_N`, 4 VSS `GND`, 5 SDA `I2C0_SDA`, 6 SCL `I2C0_SCL`, 7 CLKOUT NC, 8 VDD `VDD_RTC` | Address 0x51 |
| Y1 | Crystal 32.768 kHz, 12.5 pF, 20 ppm | 3215 | C32346 | `OSCI` to `OSCO` |  |
| C36 | 18 pF 50 V C0G | 0603 | C1647 | `OSCI` to `GND` |  |
| C35 | 100 nF 50 V X7R | 0603 | C14663 | `VDD_RTC` to `GND` |  |
| D10 | 1N4148W | SOD-123 | C81598 | A `3V3`, K `VDD_RTC` |  |
| D11 | 1N4148W | SOD-123 | C81598 | A `VBAT_R`, K `VDD_RTC` |  |
| R59 | 1k 1% | 0603 | C21190 | `VBAT` to `VBAT_R` | Limits current into the cell if D11 fails |
| BT1 | CR2032 holder | SMD | C70377 | + `VBAT`, - `GND` |  |

U10 is an NXP PCF8563T at I2C address 0x51. It is the part the firmware team
already assumed, and ESPHome has a driver for it.

**Backup: coin cell, not supercapacitor.** For a machine-assembled board I
would have preferred a supercapacitor, because it needs no manual step. The
JLCPCB parts library returned seven supercapacitor entries on 2026-11-10 and
none had stock. A coin cell holder is a normal placed part. The cell goes in at
final assembly, the same step in which the display is plugged in.

**Hold-up.** The PCF8563 draws 250 nA typical and 650 nA maximum over
temperature at 3 V with the bus idle. D10 leaks a few nanoamps back into the
dead `3V3` rail. I assume 210 mAh for a CR2032 and did not read a cell
datasheet.

- **Typical**: 210 mAh / 0.3 uA is about 80 years.
- **Worst case**: 210 mAh / 0.75 uA is about 32 years.

Both are longer than the cell's shelf life, which I take as 10 years. So the
answer is "about ten years, set by the cell". For comparison, a 0.22 F
supercapacitor falling from 3.0 V to 1.0 V at 1 uA of total leakage lasts five
days.

**Supply switching.** D10 feeds the RTC from `3V3` and D11 from the cell
through R59. They are silicon diodes, not Schottky, because a Schottky's
leakage would be of the same order as the RTC's own current. The RTC runs at
about 2.7 V on mains power and 2.6 V on the cell. It keeps time down to 1.0 V.
Its bus pins accept 5.5 V whatever its supply, and its high threshold is
0.7 x 2.7 V = 1.9 V, so the 3.3 V bus is fine. R59 limits the current into the
cell to under 1 mA if D11 ever fails short.

**Crystal.** Y1 is an Epson FC-135, 32.768 kHz, 12.5 pF, 20 ppm. The PCF8563
has 25 pF, 15 to 35 pF, built in on OSCO and wants an external capacitor of 5
to 25 pF on OSCI. C36 is 18 pF. With about 4 pF of stray capacitance the load
is 22 x 25 / 47 = 11.7 pF. I expect the clock within 40 ppm, 1.7 minutes a
month, before the temperature drift of a tuning-fork crystal. Firmware sets
the RTC from the network whenever it can.

## 15. Display and controls

### 15.1 Panel

DS1 is the HS20HS072RX from Shenzhen Hansheng, sold at LCSC as brand "HS",
part C5329582. JLCPCB had 460 on 2026-11-10 at 2.41 USD for 100.

Figures from its datasheet, version 1.0, 2020-11-04:

| Item | Value |
|---|---|
| Size and resolution | 2.0 inch, 240 x 320, RGB stripe |
| Type | Transmissive TFT, viewing direction "ALL" in the table and "FREE" on the drawing, marked IPS on the drawing |
| Controller | ST7789T3 in the table, "ST7789V2" on the drawing |
| Interface | 4-wire SPI, write only. There is no data output pin. |
| Logic supply | IOVCC 1.65 to 3.3 V, VCI 2.4 to 3.3 V, absolute maximum 4.6 V |
| Backlight | 4 white LEDs in parallel, 80 mA, 2.8 to 3.2 V at 80 mA |
| Backlight life | 20,000 h minimum to half brightness at 80 mA |
| Operating temperature | -20 to +70 °C |

Mechanical dimensions, read from the outline drawing in that datasheet. The
general tolerance on the drawing is 0.2 mm.

| Dimension | mm | Note |
|---|---|---|
| Outline | 36.2 x 51.8 | Short side by long side |
| Active area | 30.6 x 40.8 | |
| Thickness | 2.05 | A second figure, 1.1, is marked on the side view. I take it as the glass. |
| Active area to the long edge | 2.8 | |
| Active area to the short edge opposite the FPC | 2.64 | |
| Other marked figures | 33.6 wide, 43.94 and 46.6 long | Frame opening or viewing area. I could not tell which is which. |
| FPC | 12 contacts, 0.5 mm pitch, 6.5 mm wide, 0.3 mm thick at the contacts | Leaves from one short edge |
| FPC length beyond the outline | 20.7, tolerance 0.5 | |
| FPC offset from one long edge | 15.28, tolerance 0.5 | Whether to the tail's centre or edge is not clear |

**The enclosure must be designed from a physical sample, or from a drawing
the supplier confirms. Treat this table as a first estimate.** One sentence of
the datasheet calls the panel 2.4 inch, which is a copy error and a reason for
caution.

FPC pinout, from the datasheet's interface table:

| Pin | Panel name | Net |
|---|---|---|
| 1 | GND | `GND` |
| 2 | CS | `TFT_CS` |
| 3 | RS | `TFT_DC` |
| 4 | SCL | `TFT_SCLK` |
| 5 | SDA | `TFT_MOSI` |
| 6 | RST | `TFT_RST` |
| 7 | NC | not connected |
| 8 | I/O-VCC | `3V3` |
| 9 | VCC | `3V3` |
| 10 | A | `TFT_LEDA` |
| 11 | K | `TFT_LEDK` |
| 12 | GND | `GND` |

**How it is fitted.** J7 is a 12-way, 0.5 mm, flip-lock FPC connector that the
assembly house places. The panel is plugged in at final assembly. The
alternatives are worse for a machine-assembled product. A panel with a
solder-type tail needs hand or hot-bar soldering. A display module on a
carrier board needs a header, a second PCB and hand assembly, and it sets the
display height by the header.

J7 as chosen has its contacts on the bottom, the board side. I could not tell
from the drawing which face of the panel's tail carries the contacts. **Check
with a sample before layout.** If it is the other face, use the top-contact
version of the connector on the same footprint.

The panel should sit in a pocket of the enclosure front, not on the PCB. The
tail is 20.7 mm long, so J7 must be within about 15 mm of the panel's FPC edge
once the enclosure is closed.

**Second source.** None confirmed. The 2.4 inch panel from the same maker,
HS24CG003-ARX, has a 16-way FPC with a different pin order and four touch
pins. A second source would need a different J7. That is a supply risk for a
part with 460 in stock.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J7 | FPC connector 12-way 0.5 mm, bottom contact, flip lock, 2.0 mm high | SMD | C2856799 | 1 `GND`, 2 `TFT_CS`, 3 `TFT_DC`, 4 `TFT_SCLK`, 5 `TFT_MOSI`, 6 `TFT_RST`, 7 NC, 8 `3V3`, 9 `3V3`, 10 `TFT_LEDA`, 11 `TFT_LEDK`, 12 `GND` | Pin numbers follow the panel FPC |
| DS1 | HS20HS072RX 2.0 inch 240x320 IPS TFT, ST7789, 12-way FPC | panel | C5329582 | FPC `J7` | Not a schematic symbol; listed for the BOM |
| R60 | 33R 1% 0.25 W | 1206 | C18005 | `V5` to `TFT_LEDA` | Backlight current |
| Q4 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `BL_G`, S `GND`, D `TFT_LEDK` |  |
| R61 | 100R 1% | 0603 | C22775 | `TFT_BL` to `BL_G` |  |
| R62 | 100k 1% | 0603 | C25803 | `BL_G` to `GND` |  |
| R63 | 10k 1% | 0603 | C25804 | `3V3` to `TFT_RST` |  |
| C37 | 1 uF 50 V X5R | 0603 | C15849 | `3V3` to `GND` | At J7 pins 8 and 9 |
| C38 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At J7 pins 8 and 9 |

### 15.2 Backlight

The four LEDs are in parallel inside the panel, so the board sees one LED of
about 3.0 V. R60, 33 ohm, feeds it from `V5`. Q4 switches the cathode to
ground under PWM from GPIO21. R62 holds Q4 off in reset.

| Condition | `V5` | LED voltage | Current |
|---|---|---|---|
| 24 VAC, nominal | 4.95 V | 3.0 V | 59 mA |
| 24 VAC, highest | 5.10 V | 2.8 V | 70 mA |
| USB, lowest | 4.10 V | 3.2 V | 27 mA |

The rated current is 80 mA. I chose about 60 mA at full duty because the
backlight life is quoted at 80 mA and falls with current. The LED voltage in
the table is the datasheet's value at 80 mA. At 60 mA it is a little lower and
the current a little higher. R60 dissipates 0.11 W nominal and 0.16 W at most
in a 0.25 W part.

A resistor is enough here. A constant-current driver would hold the brightness
steady between USB and 24 VAC power, which nobody needs.

Use a PWM frequency between 1 and 20 kHz. Firmware dims the backlight and
turns it off after a timeout, as the owner asked. That also extends its life.

### 15.3 SPI

The display is on SPI2 using the pins that the ESP32-S3's IO MUX wires to it
directly: GPIO12 clock, GPIO11 data, GPIO10 chip select. GPIO13 is data or
command and GPIO14 is reset. Start at 20 MHz. I did not read the ST7789 timing
table, so the maximum clock is not confirmed. A full frame of 154 kB takes
62 ms at 20 MHz.

Layout: keep J7 within about 40 mm of module pins 18 to 22. Route the clock
over unbroken ground, away from the antenna and from the current-sense traces.
There are no series resistors. Add pads for them if the layout has room.

### 15.4 Encoder and buttons

SW3 is an ALPS EC11J1525402, a reflow-solderable encoder with 30 detents, 15
pulses per turn and a push switch with 1.5 mm travel. Its contacts are rated
10 mA at 5 V with a minimum of 1 mA. The datasheet gives 3 ms of chatter and
2 ms of bounce at most.

SW4, Back, and SW5, Manual or Home, are 6 x 6 mm tactile switches with a 10 mm
actuator. The actuator height is a placeholder. It must be chosen together
with the encoder's body height and the enclosure front, and I did not take the
encoder's height from its drawing.

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| SW3 | EC11J1525402 encoder, 30 detents, 15 pulses, push switch | SMD | C209762 | A `ENC_A_SW`, B `ENC_B_SW`, C `GND`, switch 1 `ENC_SW_SW`, switch 2 `GND` |  |
| SW4 | Tactile switch 6x6 mm, 10 mm high | SMD | C381050 | `BTN_BACK_SW` to `GND` | Back |
| SW5 | Tactile switch 6x6 mm, 10 mm high | SMD | C381050 | `BTN_HOME_SW` to `GND` | Manual / Home |
| R64 | 3.3k 1% | 0603 | C22978 | `3V3` to `ENC_A_SW` | Pull-up, 1 mA contact current |
| R69 | 10k 1% | 0603 | C25804 | `ENC_A_SW` to `ENC_A` |  |
| C39 | 100 nF 50 V X7R | 0603 | C14663 | `ENC_A` to `GND` |  |
| R65 | 3.3k 1% | 0603 | C22978 | `3V3` to `ENC_B_SW` | Pull-up, 1 mA contact current |
| R70 | 10k 1% | 0603 | C25804 | `ENC_B_SW` to `ENC_B` |  |
| C40 | 100 nF 50 V X7R | 0603 | C14663 | `ENC_B` to `GND` |  |
| R66 | 3.3k 1% | 0603 | C22978 | `3V3` to `ENC_SW_SW` | Pull-up, 1 mA contact current |
| R71 | 10k 1% | 0603 | C25804 | `ENC_SW_SW` to `ENC_SW` |  |
| C41 | 100 nF 50 V X7R | 0603 | C14663 | `ENC_SW` to `GND` |  |
| R67 | 3.3k 1% | 0603 | C22978 | `3V3` to `BTN_BACK_SW` | Pull-up, 1 mA contact current |
| R72 | 10k 1% | 0603 | C25804 | `BTN_BACK_SW` to `BTN_BACK` |  |
| C42 | 100 nF 50 V X7R | 0603 | C14663 | `BTN_BACK` to `GND` |  |
| R68 | 3.3k 1% | 0603 | C22978 | `3V3` to `BTN_HOME_SW` | Pull-up, 1 mA contact current |
| R73 | 10k 1% | 0603 | C25804 | `BTN_HOME_SW` to `BTN_HOME` |  |
| C43 | 100 nF 50 V X7R | 0603 | C14663 | `BTN_HOME` to `GND` |  |

Each of the five contacts has a 3.3 kohm pull-up, which gives the 1 mA the
encoder asks for, then 10 kohm and 100 nF to the GPIO. A closing contact
discharges the capacitor with a 1.0 ms time constant and an opening one
charges it with 1.3 ms. At three turns per second the encoder gives 45 pulses
per second per phase, 11 ms per half-period. All inputs are active low.

### 15.5 LEDs

| LED | Colour | Current | Driven by |
|---|---|---|---|
| D102 to D902, one per output | Yellow-green | 1.6 mA | Expander pin and `V_DRV`, section 6.9 |
| D6, power and status | Yellow-green | 2 mA | GPIO46 |
| D8, over-current latch | Red | 1.3 mA | `FAULT`, hardware only |

I chose yellow-green because it works from 3.3 V. A 2.0 V LED leaves headroom
that a 3 V blue or pure-green LED does not. Whether 1.6 mA is bright enough
behind a light pipe is a bench item. R102 sets it.

## 16. Connectors and test points

### 16.1 Terminal blocks

Pluggable 3.81 mm blocks of the common "15EDG" pattern. The board carries
right-angle headers from Cixi Kefa, KF2EDGR-3.81, and the screw plugs
KF2EDGK-3.81 are supplied loose. The listings give 300 V, 8 A for the header
and 16 to 22 AWG for the plug. Sprinkler cable is 18 AWG. I chose 3.81 mm over
5.08 mm because twenty positions take 76 mm instead of 102 mm. Other makers
sell the same pattern. I did not check a second source's drawing.

Order along the bottom edge, left to right as the installer sees it:

| Block | Position | Label | Net | Note |
|---|---|---|---|---|
| J1 | 1 | 24V~ | `AC_L` | Transformer |
| J1 | 2 | 24V~ | `AC_N` | Transformer. This leg becomes `GND` through F1. |
| J2 | 1 | COM | `COM` | Valve common |
| J2 | 2 | COM | `COM` | Second common, for a second cable |
| J2 | 3 | MV | `OUT_MV` | Master valve or pump-start relay |
| J2 | 4 to 7 | 1 to 4 | `OUT_Z1` to `OUT_Z4` | Zones |
| J3 | 1 to 4 | 5 to 8 | `OUT_Z5` to `OUT_Z8` | Zones. Not fitted on the 4-zone build. |
| J4 | 1 | 5V | `SENS_5V` | Sensor supply |
| J4 | 2 | FLOW | `T_FLOW` | |
| J4 | 3 | GND | `GND` | |
| J4 | 4 | RAIN | `T_RAIN` | |
| J4 | 5 | GND | `GND` | |
| J4 | 6 | AUX | `T_AUX` | |
| J4 | 7 | GND | `GND` | |

The order follows how the cables arrive. Power is on the left, separate from
everything else. COM comes before the valves because the common wire is
connected first and is the one people look for. A three-wire flow meter lands
on three adjacent terminals, 5V, FLOW and GND. Each two-wire sensor has its
own ground next to it. Separate plugs mean the valve wiring can be unplugged
as one piece when the controller is replaced.

The two 24V~ terminals are not interchangeable in the two unsafe cases of
section 5.3. Mark terminal 2 on the silkscreen, for example with a ground
symbol, so that the instructions can refer to it.

### 16.2 Other connectors

| Ref | Part | Pins |
|---|---|---|
| J5 | USB-C | Section 5 |
| J6 | JST SH 4-way, Qwiic | 1 `GND`, 2 `QW_3V3`, 3 `QW_SDA`, 4 `QW_SCL` |
| J7 | FPC 12-way | Section 15.1 |
| J8 | 1x6 pads, 2.54 mm | 1 `3V3`, 2 `GND`, 3 TXD0, 4 RXD0, 5 EN, 6 IO0 |

The Qwiic supply goes through PTC F2, 0.125 A hold, and the bus lines through
ESD array U9. A short on an expansion cable then cannot hold `3V3` down for
long.

### 16.3 Test pads

| Pad | Net | Pad | Net | Pad | Net |
|---|---|---|---|---|---|
| TP1 | `VIN_DC` | TP2 | `V_BUCK` | TP3 | `V5` |
| TP4 | `3V3` | TP5 | `GND` | TP6 | `GND` |
| TP7 | `V_DRV` | TP8 | `KA_N2` | TP9 | `KA_OK` |
| TP10 | `OC_TRIP_N` | TP11 | `FAULT` | TP12 | `DRV_EN` |
| TP13 | `ISENSE_LO` | TP14 | `ISENSE_HI` | TP15 | `VB` |
| TP16 | `AC_ZC` | TP17 | `ISNS` | TP18 | `I2C0_SDA` |
| TP19 | `I2C0_SCL` | TP20 | `PGOOD` | TP21 | `EXP_INT_N` |
| TP22 | `COM` | TP23 | `RTC_INT_N` | TP24 | `IO42` |
| TP25 | `IO45` | TP26 | `P06` | TP27 | `P07` |
| TP28 | `P16` | TP29 | `P17` | TP30 | `SENS_5V` |

## 17. The 4-zone variant

The 4-zone build uses the same PCB. These parts are not fitted:

- **Channels 5 to 8**: every designator from 501 to 805. For each of the four channels that is F, D, Q, C and U number 01, D number 02, and R numbers 01 to 05. Eleven parts per channel, 44 in all.
- **J3** and its plug.
- **R28**, the variant strap.

Everything else is fitted, including U5 and both of its ports. With R28
absent, expander bit P14 reads high and firmware knows it has four zones. The
unused expander pins P10 to P13 stay inputs.

## 18. Bill of materials and cost estimate

`hardware/bom-preliminary.csv` has 92 lines. Each line gives the
reference designators, quantities for both builds, value, package,
manufacturer, part number, LCSC number, JLCPCB library class, stock, price at
10, 100 and 1000 pieces, the LCSC page, the datasheet link and the date
checked. The `datasheet` column says `read` where I opened the datasheet and
took ratings from it, and `listing only` where I relied on the distributor's
listing. Section 22 says which kinds of part are `listing only`.

Every LCSC number was confirmed on 2026-11-10 against the JLCPCB parts search,
which returns the manufacturer, part number and package for a number. Stock is
JLCPCB assembly stock on that date. 46 lines are basic parts and
46 are extended.

**Parts cost per board. This is an estimate.** It is the sum of quantity times
the JLCPCB price break that the total order quantity reaches. It leaves out the
PCB, assembly, the fee JLCPCB charges per extended part type, shipping, duty,
the coin cell and the enclosure.

| Build | 10 boards | 100 boards |
|---|---|---|
| 8-zone | 37.54 USD | 28.07 USD |
| 4-zone | 31.57 USD | 23.94 USD |

The six most expensive lines on an 8-zone board at the 100-board price are the
module at 3.75 USD, the nine triacs at 3.17, the display at 2.41, the nine
opto-triacs at 2.12, the nine output PTCs at 2.06 and the encoder at 1.80.

Parts with the least stock against a 100-board order:

| Part | Stock | Needed for 100 boards |
|---|---|---|
| HS20HS072RX display | 460 | 100 |
| Lelon VZH471M1JTR-1616 bulk capacitor | 968 | 100 |
| T410-600B-TR triac | 1320 | 900 |
| EC11J1525402 encoder | 1486 | 100 |
| 1812L020/60DR PTC | 1650 | 100 |
| KF2EDGR-3.81-7P header | 1806 | 200 |
| LM5164DDAR buck | 1840 | 100 |

## 19. What firmware must do

These follow from the circuit. `hardware/pinmap.yaml` carries the pin-level
detail.

### 19.1 Rules

1. **Keep-alive.** Toggle GPIO38 from supervised task code, an edge every 5 to 50 ms. Never from a timer peripheral. Toggle only while outputs are wanted.
2. **Start-up self-test.** Before the first toggle, probe I2C address 0x74. An answer means the fail-safe circuit has failed. Refuse to run valves.
3. **Expander.** A valve bit is on when it is an output and low. After drive is enabled, write the output register to all ones except COM_RELAY at zero, then set the direction register. Never more than three valve bits low.
4. **Relay sequencing.** COM_RELAY high 20 ms before the first valve, low 40 ms after the last.
5. **Over-current latch.** GPIO40 low means the hardware has cut drive. Mark the last output switched on as faulted. Clear the latch by stopping the keep-alive for 1.5 s.
6. **Unexpected current.** Current above the idle threshold with the relay closed and no valve bit low means a shorted output device. Open the relay and stop.
7. **PSRAM ECC.** Enable it. It raises the module's ambient limit from 65 to 85 °C.
8. **Flash writes.** Avoid them while a valve is on. A long erase can outlast the 0.5 s minimum hold-up and drop the outputs.
9. **Backlight.** PWM on GPIO21 between 1 and 20 kHz. Dim and time out.

### 19.2 Measuring current

Run ADC1 in continuous mode on GPIO1 and GPIO2 at 6 dB attenuation and at
least 2000 samples per second per channel. Process windows of exactly 100 ms.
That is five cycles at 50 Hz and six at 60 Hz, so the window always holds whole
cycles.

For each window and channel, convert to millivolts with the calibration API,
subtract the window's mean, and take the root of the mean of the squares.

- **Scale**: `ISENSE_HI` 1.503 mA per mV, `ISENSE_LO` 7.50 mA per mV.
- **Which channel**: use `ISENSE_HI` unless any of its samples is below 60 mV or above 1500 mV. Then use `ISENSE_LO`.
- **Mean**: the mean itself should stay near 790 mV. A shift of the mean while current flows is a DC component, which means one triac polarity is not conducting.
- **Zero**: with the relay open, the result is the noise floor. Subtract it in quadrature.

### 19.3 Answers to the firmware team's open questions

The numbers refer to section 9 of the firmware architecture document.

| No. | Question | Answer |
|---|---|---|
| 1 | Pin map | `hardware/pinmap.yaml`. Valve outputs are expander bits, not GPIOs. |
| 2 | Keep-alive edge rate and hold time | Minimum one edge per 50 ms. Hold-up 0.5 to 0.9 s. Flash erase time not confirmed, see rule 8. |
| 3 | Pull-downs on outputs and OE | There are no output GPIOs. `KA` has a 10 kohm pull-down. The expander resets to inputs. |
| 4 | Current-sense front end | 0.1 ohm shunt, two AC-coupled-by-arithmetic channels biased at 0.79 V. Section 19.2. Trip flag on GPIO40. |
| 5 | 24 VAC present | GPIO41, a mains-frequency square wave, not a level. No edge for 25 ms means no AC. |
| 6 | Field input polarity | Contact closed reads low. External pull-ups fitted. Flow passes 200 Hz. |
| 7 | RTC | PCF8563 at 0x51, as assumed. CR2032, about ten years. |
| 8 | OLED address | No OLED. ST7789 TFT on SPI2, section 15. |
| 9 | GPIO15 and 16 | No 32 kHz crystal on the module pins. They carry the encoder. I2C0 is on GPIO8 and 9. |
| 10 | Current windows | 80 to 700 mA per output is consistent with the hardware. A 3000 mA rms short limit is close to the hardware trip of 4.6 A peak, which is 3.3 A rms for a sine. The hardware will usually act first. |
| 11 | Output LEDs | They follow the real drive in hardware. Firmware drives only the status LED on GPIO46. |

## 20. Design risk register

Everything here needs a bench. "Assumed" is what this document took as true to
get its numbers.

| No. | Risk | Assumed | Bench test |
|---|---|---|---|
| 1 | Triac junction temperature in the closed enclosure | 60 °C air inside at 50 °C outside, 55 K/W with 2 cm² of copper | Two zones and master at 0.5 A for 2 h in the enclosure in a 50 °C chamber. Thermocouples on the tabs. Junction under 110 °C. |
| 2 | Module above its 65 °C ambient limit | Same 60 °C air | Same run, thermocouple on the module can. If over 65 °C, PSRAM ECC must be on or the layout must change. |
| 3 | LDO temperature | 0.27 W typical, 120 K/W | Same run. Also with a 125 mA load on Qwiic. |
| 4 | Triac survives a dead short, and stops at the first current zero | Transformer impedance of 69 / VA ohm. One half-cycle of let-through if the triac commutates, up to two cycles and K1 breaking the current if it does not | Short an output 20 times on 40 VA and 75 VA transformers, cold and after an hour at 0.5 A. Record `ISNS` and the K1 contact voltage. Count the half-cycles. Triac must still block and switch. K1 must still open and close. |
| 5 | Over-current latch trips on noise | 47 us filter is enough | Switch inductive loads and apply fast transients. Count false trips. |
| 6 | Surge immunity of outputs, input and sensors | None. The TVS ratings are the only basis. | Combination-wave surge on valve, power and sensor lines at increasing levels until a part fails. Record the level. |
| 7 | Firing and holding with small loads | T410 latching figures | Drive a 50 mA relay coil and a 150 mA valve at -20 °C and 50 °C. Check for chatter and dropout. |
| 8 | Commutation with real solenoids | 0.13 to 0.25 H, 35 ohm | Scope T2 at turn-off for several valve makes. No re-firing after the gate is removed. |
| 9 | PTC trips on inrush when hot | Trip takes seconds at the trip current | 1.5 A for 100 ms, repeated, at 60 °C. |
| 10 | Keep-alive thresholds and hold-up | Simulation with generic models | Measure enable delay and hold-up at 10 Hz, 100 Hz and 1 kHz, and with the pin stuck high. Compare with section 8.4. |
| 11 | Buck regulator at light load and low input | Datasheet ripple design | Output ripple and stability from 9 to 45 V input, 0.05 to 0.8 A. Start-up into a sagging transformer. |
| 12 | ADC accuracy and noise with WiFi active | 2 mV rms noise, 1.5 % gain error | Compare readings with a true-rms meter from 50 mA to 2.5 A while WiFi transmits. |
| 13 | Half-wave rectifier loads the transformer with DC | 0.06 A average is harmless | Transformer temperature and primary current with and without the board, on a 20 VA unit. |
| 14 | RF range and emissions | Module approval carries over | Range test in the enclosure with the display fitted. Pre-compliance scan for the buck, SPI and backlight PWM. |
| 15 | Display fit and supply | Dimensions read from a drawing. 3.4 V is acceptable on a 3.3 V recommended pin. | Measure a sample. Check the FPC contact side. Run at 3.4 V. |
| 16 | Relay | Must-operate at 75 %. Contacts never break current. | Operate at `V5` = 4.1 V. Open under a 1.5 A inductive load 1000 times. |
| 17 | Fuse and USB ground faults | 8 A fault, 28 AWG ground | Not a test to run on a good computer. Check fuse opening time at 8 A on the bench. |
| 18 | Sensor miswire heating | 0.11 W in a 1206 | 32 VAC on each sensor terminal for an hour. Part temperatures. |
| 19 | Bulk capacitor ripple and life | 63 V part has the ripple rating of the 50 V part | Capacitor case temperature at peak load and low line. |
| 20 | Output LED brightness | 1.6 mA is visible through a light pipe | Look at it in the enclosure. |

## 21. Bring-up test plan

For the first prototype. Stop at the first step that fails.

1. **Before power.** Inspect. Measure resistance from `VIN_DC`, `V5`, `3V3` and `V_DRV` to `GND`. No cell in BT1, no display, no valve wiring.
2. **USB only.** Feed 5 V from a bench supply limited to 200 mA into VBUS. Expect `V5` 4.4 to 4.9 V, `3V3` 3.20 to 3.40 V, under 80 mA with a blank module. Then use a USB port and read the ROM boot log.
3. **Fail-safe at rest.** `KA_OK` low, `DRV_EN` low, `V_DRV` at 0 V, K1 open, no I2C answer at 0x74. The RTC at 0x51 answers.
4. **Keep-alive.** Test firmware toggles `KA` at 100 Hz. `KA_N2` rises to about 2.9 V and `DRV_EN` goes high within 20 ms. Stop toggling and time the fall on a scope: 0.45 to 0.9 s. Repeat stuck high, which must fall within 1.2 s, and at 25 Hz. Then try 1 Hz, which must not enable, and 2 Hz, 1 kHz and 100 kHz, and record what each does.
5. **Expander and LEDs.** Turn each output on in turn. Its LED lights. The voltage across R101 is about 1.8 V, 12 mA. `V_DRV` falls when the keep-alive stops, and the expander stops answering.
6. **Current channels.** Pass a known 50 or 60 Hz current through `ISNS` to `GND` from a low-voltage source. Check 133 mV/A and 665 mV/A and the 0.79 V bias.
7. **Over-current latch.** Raise that current. `FAULT` sets between 4.2 and 5.1 A peak, D8 lights, `DRV_EN` falls. It stays set until the keep-alive stops.
8. **First AC power.** Unplug USB. Feed J1 from a variable 0 to 32 VAC isolated source. The buck starts at about 9 V on `VIN_DC`. At 24 VAC: `VIN_DC` 28 to 31 V, `V_BUCK` 5.16 to 5.40 V, `AC_ZC` a clean square wave, `VIN_MON` about 0.97 V. Raise to 32 VAC for 30 minutes: `VIN_DC` under 45 V, D1 cold.
9. **Both supplies.** Add USB from a battery laptop. No current flows back into VBUS beyond 1 mA.
10. **Relay.** Command K1 closed. `COM` is live. Stop the keep-alive: time K1 opening against `DRV_EN` falling. As drawn it opens within 10 ms, see section 7.2.
11. **Resistive load.** 47 ohm, 25 W from `COM` to each output in turn: 0.5 A. Scope T2: firing voltage a few volts each half-cycle. Compare the current reading with a meter.
12. **Valve.** A real solenoid on each output. Scope T2 at turn-off: one clean stop at the current zero, ringing under the 77 V clamp. Then a 50 mA relay coil.
13. **Short circuit.** With a 40 VA transformer, short an output to `COM` and turn it on. One half-cycle of current, latch set, fuse intact, triac still working. Repeat with the short applied while the output is on.
14. **Fail-safe with a valve running.** Halt the CPU, hold reset, start a USB flash. The valve closes within 0.9 s each time, or 1.2 s if the pin stopped high, and does not reopen.
15. **Sensors.** Contact on each input. 200 Hz square wave on FLOW with no lost counts. 24 VAC onto each sensor terminal for ten minutes, then check the input still works. Short and miswire the 5V terminal.
16. **RTC.** Fit the cell. Measure cell current with `3V3` off: under 1 uA. Check the clock against a reference over a day.
17. **Display and controls.** Plug the panel in. Backlight current about 59 mA at full duty. Run the SPI at 20 MHz, then higher. Encoder and buttons.
18. **Thermal run, RF and the remaining items of section 20.**

## 22. What I could not confirm

- **Class 2 short-circuit limit.** The 8 A figure in sections 5.3 and 20 is from memory.
- **USB attach capacitance.** The 10 uF figure in section 5.2 is from memory.
- **Bulk capacitor ripple rating.** I read the Lelon table for 50 V, not 63 V.
- **Surface-mount relay.** No surface-mount alternative to K1 was searched for.
- **Display.** FPC contact side, which marked dimension is the viewing area, the controller variant, the logic supply current, the maximum SPI clock and a second source. I did not read an ST7789 datasheet.
- **Antenna clearance.** I did not read Espressif's hardware design guidelines.
- **Module flash.** Its part number and sector-erase time.
- **TPS2553 limit.** Estimated by scaling, not from the datasheet formula.
- **Buck efficiency.** Assumed 85 %.
- **Coin cell.** Capacity and shelf life assumed.
- **Solenoid inductance and transformer impedance.** Estimates, used in sections 6.5 and 6.6.
- **Typical values that the datasheets do not give.** Expander output voltage at 13 mA, opto-triac drop at 10 mA, ADC noise.
- **Parts marked `listing only` in the BOM.** I confirmed the LCSC number, maker, part number, package and stock, and took the ratings from the listing. I did not open datasheets for the generic resistors and capacitors, the LEDs, tactile switches, terminal blocks, the JST and USB-C connectors, the crystal, the coin-cell holder, the AO3400A, the 1N4148W and the 1206 PTC.
- **Second sources.** PCA9539PW for the expander and BSMD2920-100-60V for the output PTC are named in the BOM notes. I did not read their datasheets. LCSC's own page for the PCA9539PW returned "not found" although JLCPCB lists the part.
- **The micro-crystal RV-8263-C7 and RV-3028-C7.** I wanted to evaluate them as RTCs without a crystal and could not fetch a datasheet.

## 23. Sources

Datasheets I read, with the revision I had. Where the link is an LCSC page, it
is LCSC's copy of the maker's PDF for that LCSC number.

| Part | Document | Link |
|---|---|---|
| ESP32-S3 | Series datasheet v2.2 | <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf> |
| ESP32-S3-WROOM-1 | Module datasheet v1.8 | <https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf> |
| LM5164 | SNVSAU4D, February 2026 | <https://www.ti.com/lit/ds/symlink/lm5164.pdf> |
| T410-600B | T4 series, DocID7699 rev 9 | <https://www.lcsc.com/datasheet/C2970064.pdf> |
| BTA204S-600D | Product data sheet, 14 August 2017 | <https://www.lcsc.com/datasheet/C256464.pdf> |
| MOC3023 | MOC3023M/D rev 3, June 2023 | <https://www.onsemi.com/download/data-sheet/pdf/moc3023m-d.pdf> |
| TCA9539 | SCPS202C | <https://www.lcsc.com/datasheet/C131972.pdf> |
| LM2901 | SLCS006S | <https://www.lcsc.com/datasheet/C7932.pdf> |
| 74HC00 | Nexperia data sheet | <https://www.lcsc.com/datasheet/C5586.pdf> |
| MCP6002 | DS20001733L | <https://www.lcsc.com/datasheet/C7377.pdf> |
| LDL1117 | DocID030319 rev 3 | <https://www.lcsc.com/datasheet/C435835.pdf> |
| TPS2553 | SLVS841E | <https://www.lcsc.com/datasheet/C55266.pdf> |
| PCF8563 | Rev. 11.1, 19 January 2026 | <https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf> |
| SMBJ48CA, SMCJ48CA | Littelfuse SMBJ and SMCJ series | <https://www.lcsc.com/datasheet/C151264.pdf>, <https://www.lcsc.com/datasheet/C151287.pdf> |
| 2920L110/60, 1812L020/60 | Littelfuse POLY-FUSE 2920L and 1812L | <https://www.lcsc.com/datasheet/C315903.pdf>, <https://www.lcsc.com/datasheet/C315900.pdf> |
| 0154003.DRT, 0452003.MRL | Littelfuse 154 and 452 series | <https://www.lcsc.com/datasheet/C206913.pdf>, <https://www.lcsc.com/datasheet/C99550.pdf> |
| RLP25FEER100 | TA-I RLP series | <https://www.lcsc.com/datasheet/C160587.pdf> |
| VZH471M1JTR-1616 | Lelon VZH series | <https://www.lcsc.com/datasheet/C249935.pdf> |
| 63PX470MEFC12.5X20 | Rubycon PX series | <https://www.lcsc.com/datasheet/C88758.pdf> |
| SRN6045TA-470M | Bourns SRN6045TA | <https://www.lcsc.com/datasheet/C5151734.pdf> |
| HS20HS072RX | Hansheng datasheet v1.0, 2020-11-04 | <https://www.lcsc.com/datasheet/C5329582.pdf> |
| EC11J1525402 | ALPS product sheet | <https://www.lcsc.com/datasheet/C209762.pdf> |
| G5NB-1A-E | Omron G5NB, Chinese edition; English edition read in review A1 | <https://www.lcsc.com/datasheet/C48746.pdf>, <https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5nb.pdf> |
| AO3401A, SS34, SS210, M7, BAT54S, USBLC6-2SC6 | Maker datasheets, key ratings only | LCSC pages for C15127, C8678, C14996, C95872, C47546, C7519 |

Stock, price and library class came from the JLCPCB parts search on
2026-11-10. The prior-art evidence is in the [prior-art survey](prior-art.md).
