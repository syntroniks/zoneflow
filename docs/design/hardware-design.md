# ZoneFlow hardware design, revision A

Status: **paper design, 2026-11-10, after
[hardware review A1](hardware-review-a1.md) and the decisions taken on its
findings**. No board exists. Nothing in
this document has been measured. Every number is a datasheet figure, a calculation from
datasheet figures, or a simulation result, and the text says which. Section 20
lists what only a bench can confirm. Section 22 lists what I could not confirm
at all.

This document is the circuit design that schematic capture translates. I
wrote it against [system specification](system-spec.md) v0.3. It departs from
that version in the places listed in section 2, and the pull request that adds
this document raises the specification to v0.5 with those changes. Three files hold the same design as data:

- **`hardware/netlist.yaml`**: every component, its value, package, LCSC number and the net on every pin. It is the source. The component tables in this document are written from it.
- **`hardware/pinmap.yaml`**: every ESP32-S3 GPIO, I2C address and expander bit. Firmware generates its board header from it.
- **`hardware/bom-preliminary.csv`**: one line per part number with stock, price and source, checked on 2026-11-10.

`tools/designsync/designsync.py` keeps the four in step. `make -C hardware
design-check` fails if a table here, the BOM or the pin map disagrees with the
netlist, and CI runs it on every change to them. `make -C hardware
design-sync` rewrites the tables, which sit between `designsync` comment
lines. Do not edit a table by hand.

Simulation netlists are in `hardware/sim/`, with a README that states what each
run showed.

Section 24 lists what changed after the review and where I did something
other than what the reviewer proposed.

## 1. The design in brief

| Block | Choice | Main reason |
|---|---|---|
| Logic supply | A fuse in each transformer leg, 1500 W TVS, half-wave rectifier, 470 uF, LM5164 100 V buck to 5.3 V, Schottky OR with USB, LDL1117 to 3.3 V | The TVS clamps at 77.4 V. A 100 V regulator survives that with no further argument. A 5 V bus makes USB and 24 VAC equal sources and gives the LDO room to reject ripple. |
| Output switch | T810-600B-TR 8 A three-quadrant triac in DPAK, fired by a MOC3023 opto-triac from its own T2 | The triacs JLCPCB stocks with real surge ratings are three-quadrant parts. Positive-only gate drive from logic cannot fire them on the negative half-cycle. The 8 A part carries the short-circuit current of a 75 VA transformer until the PTC trips. Section 6 has the analysis. |
| Output control | TCA9539 I2C expander sinks the opto LEDs | The TFT took six GPIOs. Nine valve pins no longer fit. The expander is held in reset by the fail-safe circuit, so no valve bit survives a crash. |
| Fail-safe | Two independent detectors on the keep-alive pin. One, a diode pump with a comparator, switches the LED supply and the expander reset. The other, a diode pump straight into a transistor gate, holds the relay. | Gate drive needs an edge at least every 20 ms. It is gone 0.45 to 0.9 s after the edges stop, and the relay has opened within 0.95 s. No single component fault leaves a valve on after the edges stop. Sections 7 and 8. |
| Over-current | Window comparator on the shunt amplifier, NAND latch, same switch as the fail-safe | Removes drive about 70 us after a short begins and releases K1 at once. The triac and the PTC are rated to carry the short until one of them or the relay interrupts it. |
| Valve common | Relay in the COM feed. Its coil current passes through two transistors: one follows an expander bit, the other its own keep-alive detector. | A shorted triac or TVS otherwise leaves a valve on with no way to stop it. This is an addition to the specification. |
| Current measurement | 0.1 ohm shunt, two difference amplifiers biased at 0.8 V, sampled by ADC1 | True RMS in firmware. No precision rectifier, which would add millivolts of error to a 5 mV signal. |
| Display | HS20HS072RX 2.0 inch 240x320 IPS panel on a 12-way FPC connector, plugged in at final assembly | The only bare ST7789 panels with stock and a drawing that I found at LCSC. |
| RTC | PCF8563T with a 32.768 kHz crystal, CR2032 in a holder | JLCPCB stocks no supercapacitor. The coin cell lasts longer than its own shelf life. |

## 2. Where this design departs from the specification

The specification stays the source of truth. This pull request changes it for
each item below, and the reasons are in the sections named.

1. **Gate drive goes through opto-triacs, not straight from logic.** Specification 3.1 says gate drive comes from ESP32 GPIO through the fail-safe gate. The triac still sits in the OpenSprinkler topology, with one AC leg as ground and a shared shunt. Only the gate coupling changes. Section 6.2.
2. **Valve outputs are on an I2C expander.** Specification 3.1 says the module has enough GPIO without an expander. With the SPI display, two I2C buses, five control inputs and the fail-safe signals, 25 usable pins are not enough for nine direct outputs. Section 12.2.
3. **A relay in the valve common.** Not in the specification. It gives hardware a way to remove power from every valve when an output device has failed short. I propose a new safety rule 6. Section 7.
4. **A fuse in each transformer leg.** The specification said "fuse on AC input" and did not say which leg. The first draft put one fuse in the ground leg, because only that fuse sits in the fault loops that run through a USB cable. The review showed the loops that a ground-leg fuse leaves open, and the specification now asks for both. Section 5.3.
5. **No rectifier in the current-sense front end.** Specification 3.1 asks for a rectifying, averaging front end. This design amplifies the AC waveform around a 0.8 V bias and lets firmware compute RMS. Section 10.
6. **5 V bus with an LDO.** Specification 3.1 says "buck converter, 3.3 V rail". The buck makes 5.3 V and an LDO makes 3.3 V. Section 4.6.
7. **The relay has its own keep-alive detector.** Safety rule 3 described one output-enable. The review found single faults that hold it on. Rule 3 now asks for two independent detectors, one for gate drive and one for the relay. Sections 7.3 and 8.5.

## 3. Conventions

### 3.1 Supply rails and ground

| Net | What it is |
|---|---|
| `AC_L_IN` | 24 VAC terminal 1, before fuse F4. |
| `AC_L` | Terminal 1 after F4. Feeds the rectifier, the zero-cross detector and, through relay K1, the valve common. |
| `AC_N` | 24 VAC terminal 2. Connects to `GND` through fuse F1. |
| `GND` | Circuit ground. One transformer leg, the USB ground and the sensor ground are all this net. |
| `COM` | Valve common terminals. `AC_L` after relay K1. |
| `VIN_DC` | Rectified, unregulated supply. 26 to 31 V at 24 VAC, 44 V at 32 VAC with no load. |
| `V_BUCK` | Buck output, 5.28 V nominal. Present only with 24 VAC. Also feeds the coil of K1 and the sensor supply. |
| `V5` | 5 V bus after the OR diodes. About 4.95 V from 24 VAC, 4.1 to 4.9 V from USB. |
| `3V3` | Logic rail, 3.3 V, 3 % over temperature. |
| `V_DRV` | 3.3 V supply of the opto LEDs. On only while the fail-safe circuit allows drive. |
| `ISNS` | Common T1 node of all triacs. The shunt R37 connects it to `GND`. |

### 3.2 Reference designators

Components that exist once use numbers below 100. The three resistor arrays
are RN1 to RN3. Output channel components
use three digits. The first digit is the channel and the last two are the same
in every channel.

| Channel | Terminal | Designators |
|---|---|---|
| 1 to 8 | Z1 to Z8 | 101 to 105, 201 to 205, and so on to 801 to 805 |
| 9 | MV | 901 to 905 |

So R301 is the opto LED resistor of zone 3 and Q901 is the master valve triac.

### 3.3 Operating envelope I designed to

| Item | Value | Source |
|---|---|---|
| Ambient outside the enclosure | -20 to +50 °C, indoor or garage, non-condensing | Specification section 1, from v0.5 |
| Air inside the enclosure | up to 70 °C | 50 °C plus the rise the enclosure concept estimates, 16 K at 3 W with vents (pull request 24, mechanical design section 10), rounded up. The first draft assumed 10 K. Bench test in section 20. |
| Supply | 21.6 to 26.4 VAC at load, up to 32 VAC unloaded, 50 or 60 Hz | Specification section 1 |
| Load | 0.5 A rms per output, 1.5 A rms inrush for 100 ms, two zones plus master | Specification section 1 |
| Smallest load | 50 mA rms pump-start relay coil | Brief |
| Transformer | Class 2, 20 to 75 VA. Short-circuit current at the terminals 7 to 21 A rms. | Section 6.6 calculates it. The output PTC is rated for 40 A. |

## 4. 24 VAC input and logic supply

### 4.1 Circuit

Each transformer leg has a fuse. Terminal 2, `AC_N`, goes through F1 to `GND`.
Terminal 1, `AC_L_IN`, goes through F4 to `AC_L`. `AC_L` feeds three things: the TVS D1 to
`GND`, the half-wave rectifier D2, and the valve common through relay K1.
D2 charges the bulk capacitor C1 through R1. The LM5164 buck U1 turns `VIN_DC`
into `V_BUCK`, 5.28 V. Schottky D3 passes it to the 5 V bus.

<!-- designsync:begin table block="A power" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J1 | Header 2-way 3.81 mm pluggable, right angle | THT | C441182 | `AC_L_IN` to `AC_N` | 24 VAC input |
| F1 | Fuse 3 A time-lag in SMD holder | SMD 9.7x5 mm | C206913 | `AC_N` to `GND` | Fuse in the leg that becomes circuit ground |
| F4 | Fuse 3 A time-lag in SMD holder | SMD 9.7x5 mm | C206913 | `AC_L_IN` to `AC_L` | Fuse in the other leg |
| D1 | TVS 48 V bidirectional 1500 W | SMC | C151287 | `AC_L` to `GND` | Input surge clamp |
| D2 | Rectifier 1 A 1000 V | SMA | C95872 | A `AC_L`, K `VRECT` | Half-wave rectifier |
| R1 | 3R 5% 1 W, surge series | 2512 | C2936639 | `VRECT` to `VIN_DC` | Limits charging and surge current. Pulse rating in section 4.4 |
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

<!-- designsync:end -->

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
| Qwiic connector | 0 | 80 | Budget. PTC F2 holds 0.125 A at 20 °C and 0.08 A at 60 °C, and has up to 6 ohm, so 80 mA is what the connector is good for. Section 16.2. |
| **Total `3V3`** | **162** | **498** | |

Currents on `V5` and `V_BUCK`.

| Consumer | Typical mA | Peak mA | Basis |
|---|---|---|---|
| LDO input, equal to the `3V3` load | 162 | 498 | |
| Backlight | 58 | 74 | Section 15.2 |
| Comparator U6, sensor pull-ups | 2 | 3 | |
| **Total `V5`** | **222** | **575** | |
| Relay K1 coil, from `V_BUCK` | 42 | 42 | 5.28 V across 125 ohm |
| Sensor supply terminal, from `V_BUCK` | 20 | 158 | Highest limit of U11, section 13.5 |
| **Total `V_BUCK`** | **284** | **775** | U1 is rated 1 A |

Power drawn from `VIN_DC`, with the buck efficiency taken as 85 %. That figure
is an assumption. I did not read an efficiency curve for a 5 V output.

- **Typical**: 5.28 V x 0.284 A / 0.85 = 1.76 W. The simulations use 1.75 W.
- **Peak**: 5.28 V x 0.775 A / 0.85 = 4.8 W. The simulations use 5.0 W.

### 4.3 Fuses F1 and F4

F1 and F4 are the same part, one in each transformer leg. Every current the
board draws passes through both. Section 5.3 says why one is not enough.

Each is a Littelfuse 0154003.DRT: a 154-series holder supplied with a 3 A
time-lag fuse of the 454 series fitted. The 452 series is the same fuse
without the silver-plated caps and is the replacement part. The user can
replace it without soldering. The 452/454 datasheet gives 125 V, 50 A
interrupting rating, 0.034 ohm and a melting I²t of 20.16 A²s. Opening times
are 1 to 60 s at 200 %, 0.2 to 3 s at 300 % and 20 to 100 ms at 800 %.

| Check | Calculation | Result |
|---|---|---|
| Steady load | 1.5 A valves and 0.35 A rectifier current, added as rms | 1.54 A, 51 % of rating |
| Valve inrush | 2.5 A for 100 ms gives 0.63 A²s | 3 % of melting I²t |
| Plug-in at the 32 VAC peak | 45.3 V / (3.0 + 2.1 ohm) = 8.9 A, decaying with 2.4 ms | 0.09 A²s, under 1 % |
| Zone short, one half-cycle at 21 A rms, the 75 VA figure of section 6.6 | 21² x 10 ms | 4.4 A²s, 22 % of melting I²t |
| Voltage lost in both fuses at 1.54 A | 2 x 0.034 ohm x 1.54 A | 0.10 V |

Two fuses in series do not coordinate: on a fault that passes through both,
either may open, and the user has to check both. Both holders are on the top
side next to J1.

The zone-short row is why the fuses are time-lag. A very fast 3 A fuse of the 451
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

R1 is 3 ohm. It does two jobs. It limits the plug-in current into C1, and it
limits how far a surge can pump C1 up. With D1 clamping at 77.4 V for a full
rated 10/1000 us pulse, C1 charges from 45 V toward the clamp voltage through
R1 with a time constant of 3.2 ohm x 470 uF = 1.5 ms. The pulse has a decay
constant of 1.44 ms. That gives a rise of about 14 V, to 59 V. C1 is rated
63 V and U1 100 V. The simulation puts the loss in R1 at 0.09 W typical and
0.85 W at low line with every consumer at its limit. R1 is rated 1 W up to
70 °C.

**R1 is a pulse-rated part.** The first draft used a general-purpose
thick-film resistor with no pulse curve, and review A1 objected. R1 is now a
Yageo SR2512JK-073RL: 3 ohm, 5 %, 1 W, 2512, from Yageo's surge series, LCSC
C2936639, 3972 in stock at JLCPCB on 2026-11-10. The review found no stocked
anti-surge part; this one is.

The pulse it has to take is the charging of an empty C1 when the board is
plugged in at the peak of 32 VAC, 45.3 V:

| | 40 VA transformer | 75 VA transformer |
|---|---|---|
| Transformer series resistance, assumed | 2.1 ohm | 1.0 ohm |
| Peak current, 45.3 V / (3 ohm + transformer) | 8.9 A | 11.3 A |
| Peak power in R1, I² x 3 ohm | 237 W | 385 W |
| Energy in R1, 0.5 x 470 uF x 45.3² x 3 / (3 + transformer) | 0.28 J | 0.36 J |
| Rectangular pulse of the same peak and energy | 1.2 ms | 0.94 ms |
| Yageo limit for a single pulse of that length, 2512, 5 % | about 700 W | about 750 W |
| Margin in power | 3.0 | 1.9 |

The limit is read from figure 7-2 of the Yageo SR data sheet, V.14 of
2025-02-14, which covers the 5, 10 and 20 % tolerance parts: about 750 W at
1 ms falling to about 200 W at 10 ms for size 2512. The figures are read off
a log-log graph and are good to perhaps 15 %. The 1 % parts of the same
series follow figure 7-1 and take only about 200 W at 1 ms, so the tolerance
letter matters: **SR2512JK, not SR2512FK.** The 5 % tolerance costs nothing
here. R1 only limits current.

A real power-up is milder than the table. The sine wave falls away from its
peak during the 2 ms the pulse lasts, and the calculation ignores the leakage
inductance of the transformer. A surge clamped by D1 at 77.4 V puts about
(77.4 - 45) / 3 = 11 A through R1 for 1.4 ms, the same size of pulse. A Vishay
MELF (MMB0207, 3.3 ohm) was the other candidate; its data sheet allows about
70 W for a single 1 ms pulse below 10 ohm, which is not enough.

### 4.5 Bulk capacitor C1

The rectifier is half-wave, so C1 supplies the load for most of a 20 ms cycle
at 50 Hz. `hardware/sim/supply.cir` models the transformer as a source with
2.1 ohm in series, which is my assumption for a 40 VA unit.

| Supply | Load | `VIN_DC` valley to peak | Capacitor ripple |
|---|---|---|---|
| 24 VAC, 50 Hz | 1.75 W | 28.7 to 30.9 V | 0.16 A rms |
| 24 VAC, 60 Hz | 1.75 W | 28.9 to 30.7 V | 0.16 A rms |
| 21.6 VAC, 50 Hz | 1.75 W | 25.0 to 27.4 V | 0.17 A rms |
| 21.6 VAC, 50 Hz | 5.0 W | 17.3 to 25.0 V | 0.48 A rms |
| 16 VAC, 50 Hz | 1.75 W | 15.8 to 19.3 V | 0.23 A rms |
| 16 VAC, 50 Hz | 3.0 W | 11.5 to 18.1 V | 0.40 A rms |
| 18 VAC, 50 Hz | 5.0 W | 8.1 to 19.2 V | 0.66 A rms |
| 32 VAC, 50 Hz | 0.05 W | 44.3 V | under 0.01 A rms |

U1 regulates down to its under-voltage lockout, which R7 and R8 set to 9.0 V
rising and 8.4 V falling. So the logic supply holds through a sag to 16 VAC at
the typical load and at 3 W. That is the brownout margin for a small
transformer during valve inrush. Only the last-but-one row fails: an 18 VAC sag
with every consumer at its limit. With no input at all, C1 holds the typical
load from 29 V down to 9 V for 0.5 x 470 uF x (29² - 9²) / 1.75 W = 100 ms,
five cycles.

C1 is a Lelon VZH 470 uF 63 V in a 16 x 16.5 mm SMD can, 5000 h at 105 °C. Its
steady voltage is at most 44.3 V, 70 % of rating. The Lelon table for 63 V
gives this part 900 mA at 100 kHz and 105 °C and 0.082 ohm, with a multiplier
of 0.6 at 50 and 60 Hz: 540 mA. The
through-hole alternative, Rubycon 63PX470MEFC12.5X20, is rated 700 mA at
120 Hz and 0.8 of that at 50 Hz, which is 560 mA. Both cover the 0.16 A typical
and 0.48 A peak. The peak figure is 89 % of the Lelon rating, at an ambient
far below 105 °C.

**Placement.** The Lelon can is 16.5 mm tall. The enclosure concept allows
9 mm above the board and 17 mm below it, so C1 has to sit on the bottom
side, and that needs a second assembly side for one part. Section 16.4 has
the options.

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
| Peak inductor current, eq. 19 | IOUT + ripple / 2 | 0.82 + 0.17 = 0.99 A. Current limit is 1.25 A minimum. |
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
U2 clamps D+, D- and VBUS. D+ and D- reach GPIO20 and GPIO19 through R97 and
R98, 0 ohm, with footprints C52 and C53 to ground on the module side, not
fitted. Review A1 quotes Espressif's hardware design guidelines for this:
reserve a series resistor, 22 or 33 ohm to start with, and an unpopulated
capacitor on each line, close to the module. Place all four within a few
millimetres of the module pins. The values are for EMC work on the built
board.

<!-- designsync:begin table block="C USB and 3V3" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J5 | USB-C receptacle 16-pin, USB 2.0 | SMD | C165948 | VBUS (A4 A9 B4 B9) `VBUS`, GND (A1 A12 B1 B12) `GND`, CC1 (A5) `CC1`, CC2 (B5) `CC2`, D+ (A6 B6) `USB_DP`, D- (A7 B7) `USB_DM`, SBU1 SBU2 NC, shell `USB_SHLD` |  |
| R15 | 5.1k 1% | 0603 | C23186 | `CC1` to `GND` |  |
| R16 | 5.1k 1% | 0603 | C23186 | `CC2` to `GND` |  |
| U2 | USBLC6-2SC6 ESD array | SOT-23-6 | C7519 | 1 I/O1 `USB_DM`, 2 GND `GND`, 3 I/O2 `USB_DP`, 4 I/O2 `USB_DP`, 5 VBUS `VBUS`, 6 I/O1 `USB_DM` | Place at J5 |
| R97 | 0R | 0603 | C21189 | `USB_DP` to `USB_DP_MCU` | Series footprint |
| R98 | 0R | 0603 | C21189 | `USB_DM` to `USB_DM_MCU` | Series footprint |
| C52 | Not fitted | 0603 | none | `USB_DP_MCU` to `GND` | Footprint only (not fitted) |
| C53 | Not fitted | 0603 | none | `USB_DM_MCU` to `GND` | Footprint only (not fitted) |
| C13 | 100 nF 50 V X7R | 0603 | C14663 | `VBUS` to `GND` |  |
| D4 | SS34 Schottky 3 A 40 V | SMA | C8678 | A `VBUS`, K `V5` | OR-ing, USB side |
| C10 | 4.7 uF 25 V X5R | 0805 | C1779 | `V5` to `GND` |  |
| U3 | LDL1117S33R 3.3 V 1.2 A LDO | SOT-223 | C435835 | 1 GND `GND`, 2 VOUT `3V3`, 3 VIN `V5`, tab `3V3` |  |
| C11 | 10 uF 25 V X5R | 0805 | C15850 | `3V3` to `GND` |  |
| C12 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` |  |
| R17 | 0R | 0603 | C21189 | `USB_SHLD` to `GND` |  |

<!-- designsync:end -->

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
cannot close and the sensor supply is off, because the coil of K1 and U11 are
both fed from `V_BUCK`. The read-back of the relay detector on GPIO7 reads
low for the same reason, section 7.4.

### 5.3 The ground question

`GND` is one leg of the transformer. A USB cable connects `GND` to the
computer's ground, and a desktop computer's ground is protective earth. So
plugging in USB earths one leg of the 24 VAC secondary.

**Safe.**

- **Floating secondary**: a Class 2 sprinkler transformer whose secondary has no connection to earth. Nearly all plug-in and DIN-rail sprinkler transformers are built this way. USB earths the `AC_N` leg and nothing flows.
- **Battery laptop or USB isolator**: no earth path at all.

**Safe only while nothing on the board fails.**

- **Earthed on the `AC_N` side**: if the installation earths the leg wired to terminal 2, USB earths the same leg again. Nothing flows through the cable while the board is healthy. The USB ground is then a second path from board `GND` to that leg, in parallel with F1.

**Not safe.**

- **Secondary earthed on the other leg**: if the leg wired to terminal 1 is earthed and a USB cable from an earthed computer is plugged in, the transformer is short-circuited through the USB cable. The loop is transformer, earth, computer, USB ground wire, board `GND`, F1, terminal 2, transformer.
- **Two controllers on one transformer with the legs swapped**: controller A has leg 1 as ground, controller B has leg 2. Anything that joins their grounds completes a short across the transformer. That includes one computer on both USB ports, a shared sensor cable or a Qwiic cable. It also applies to a ZoneFlow sharing a transformer with any other half-wave-rectified device.
- **A valve wire or COM faulted to earth, with USB attached**: COM is the other transformer leg. Wet outdoor splices do leak to earth. With an earthed computer attached, that leak is a path across the transformer.

**Why there are two fuses.** No single fuse position is in every loop. The
table follows review A1, section 6, with the second fuse now fitted.

| Fault | Loop | F1, ground leg | F4, other leg |
|---|---|---|---|
| Valve wire shorted to COM | Leg 1, F4, K1, COM, short, PTC, triac, shunt, `GND`, F1, leg 2 | In the loop | In the loop |
| D1 or C1 shorted | Leg 1, F4, the part, `GND`, F1, leg 2 | In the loop | In the loop |
| Leg 1 earthed, earthed computer on USB | Leg 1, earth, computer, USB ground, `GND`, F1, leg 2 | In the loop | Not in the loop |
| Two devices on one transformer with legs swapped, grounds joined | Leg 1, other device's ground, joined ground, `GND`, F1, leg 2 | In the loop | Not in the loop |
| COM or a valve wire leaking to earth, earthed computer on USB | Leg 1, F4, K1, COM, earth, computer, USB ground, `GND`, F1, leg 2 | In the loop | In the loop |
| Leg 2 earthed, earthed computer on USB, and a board fault from the first two rows | As those rows as far as `GND`, then F1 in parallel with USB ground, computer, earth, leg 2 | Bypassed | In the loop |
| A second device on the transformer with the same leg as ground, grounds joined, and a board fault | The same, with the other device's ground in place of earth | Bypassed | In the loop |

The first draft had F1 only, for the third and fourth rows: those loops do
not touch terminal 1, so a fuse there does not see them. The last two rows
are what F1 alone leaves open. A board fault then has two ways back to leg 2,
and when F1 opens all of the current takes the USB cable or the joined
ground, with nothing but the transformer to limit it. F4 is in those loops.
With both fitted, every loop that passes through the board's own circuit
between the two terminals goes through at least one fuse.

What neither fuse covers is a connection made outside the board between the
two transformer legs, for example the leg-1 wire touching a sensor ground
wire while leg 2 is earthed and a computer is attached. That current enters
at one connector and leaves at another through the ground copper, and no
position on the board is in series with it.

**How well a fuse protects a USB cable.** F1 and F4 are 3 A time-lag fuses.
Section 6.6 puts the short-circuit current of a 20 VA transformer at about
7 A rms and of a 75 VA one at about 21 A. At 8 A, 270 % of rating, the fuse
datasheet bounds the opening time between 0.2 s and 60 s. A USB cable's
ground conductor is thin. I estimate that 8 A heats 28 AWG copper at about
50 K per second with no cooling. So **the fuses protect the board traces and
limit the damage, but they do not make the unsafe cases safe for the cable or
the computer.** A fast fuse would clear sooner and would also open on every
shorted valve wire. I chose the time-lag fuse and the warning.

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

<!-- designsync:begin table block="Z output channels" channel="1" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| F101 | PTC 1.1 A hold, 60 V, 40 A maximum | 2920 | C3020148 | `OUT_Z1` to `T2_Z1` | Fault limiter |
| D101 | TVS 48 V bidirectional 600 W | SMB | C151264 | `OUT_Z1` to `ISNS` | Across the triac, at the terminal |
| Q101 | T810-600B-TR triac 8 A 600 V, 3-quadrant | DPAK | C155588 | 1 T1 `ISNS`, 2 T2 (tab) `T2_Z1`, 3 G `G_Z1` |  |
| R105 | 47R 1% 150 V | 0805 | C17714 | `T2_Z1` to `SNB_Z1` | Snubber |
| C101 | 10 nF 100 V X7R | 0805 | C18741 | `SNB_Z1` to `ISNS` | Snubber, returned to the shunt so that a shorted capacitor shows as current |
| U101 | MOC3023SR2M random-phase opto-triac | SMD-6 | C40701 | 1 anode `OLA_Z1`, 2 cathode `ZD_Z1_N`, 3 NC NC, 4 MT `G_Z1`, 5 do not connect NC, 6 MT `OGT_Z1` |  |
| R101 | 150R 1% | 0603 | C22808 | `V_DRV` to `OLA_Z1` | Opto LED current |
| R103 | 100R 1% 150 V | 0805 | C17408 | `T2_Z1` to `OGT_Z1` | Gate series |
| R104 | 1k 1% | 0603 | C21190 | `G_Z1` to `ISNS` | Gate to T1 |
| R102 | 680R 1% | 0603 | C23228 | `V_DRV` to `LEDA_Z1` | Indicator current |
| D102 | LED yellow-green | 0603 | C125094 | A `LEDA_Z1`, K `ZD_Z1_N` | Output indicator |

<!-- designsync:end -->

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
| T810-600B-TR | ST | 8 A, 80 A surge | I, II, III | 3384 |
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

### 6.3 Triac Q101: T810-600B-TR

Figures from the ST datasheet for the BTA08, BTB08, T810, T835 and T850,
DocID7472 rev 12, read as LCSC's copy for C155588.

| Parameter | Value |
|---|---|
| On-state current | 8 A rms at 110 °C case |
| Surge, one cycle, junction at 25 °C before it | 80 A at 50 Hz, 84 A at 60 Hz |
| I²t, 10 ms | 36 A²s |
| Gate trigger current, quadrants I, II, III | 10 mA maximum at 25 °C |
| Gate trigger voltage | 1.2 V maximum |
| Holding current | 15 mA maximum |
| Latching current | 25 mA maximum in I and III, 30 mA in II |
| dV/dt, gate open, 125 °C | 40 V/us minimum at 67 % of 600 V |
| Commutation (dI/dt)c, 125 °C | 5.4 A/ms minimum at 0.1 V/us re-applied, 2.8 A/ms at 10 V/us |
| On-state model, 125 °C maximum | 0.85 V + 0.05 ohm |
| Thermal resistance to ambient, DPAK | 70 K/W with 1 cm² of copper under the tab |

Figure 8 of the datasheet gives the change with temperature. At -20 °C the
trigger current is about 1.8 times its 25 °C value, and holding and latching
current about 1.45 times.

**Why not the 4 A part of the first draft.** The first draft used the
T410-600B-TR from the same maker. Its gate, holding and latching figures are
the same as the T810's, so nothing in section 6.4 changes. What differs is
the silicon area:

| | T410-600B-TR, first draft | T810-600B-TR, now |
|---|---|---|
| Surge, one cycle | 30 A | 80 A |
| I²t, 10 ms | 5.1 A²s | 36 A²s |
| (dI/dt)c at 0.1 V/us, 125 °C | 2.7 A/ms | 5.4 A/ms |
| On-state model | 0.89 V + 0.12 ohm | 0.85 V + 0.05 ohm |
| LCSC, stock at JLCPCB on 2026-11-10 | C2970064, 1320 | C155588, 3384 |
| Price at 100 pieces | 0.43 USD | 0.46 USD |

Review A1 showed that the T410 was not certain to stop conducting at the
first current zero of a dead short, and that it would then survive only one
or two cycles. Section 6.6 shows that the T810 carries the short until the
PTC ends it, whether it stops or not. Nine of them add about 0.25 USD to a
board at the 100-piece price. The T810-800B-TR, C2935379, is the same part at 800 V and fits if the
600 V one runs out.

**Other options I looked at for finding M1.**

- **A snubberless 8 A part, T835-600B-TR.** Same footprint and surge rating. Its gate needs 35 mA and it latches at 50 mA, so the firing delay of section 6.4 triples and a small pump-start coil no longer latches. Its commutation figure, 4.5 A/ms with no snubber, is no better for a resistive short than the T810's 5.4 A/ms. Rejected.
- **A faster or smaller series element.** A fast fuse clears in milliseconds, and then every shorted wire costs a fuse. A PTC with a lower hold current trips sooner, but a 0.75 A part holds only about 0.44 A at 70 °C, below the load. A series resistor large enough to keep a 75 VA short inside the T410's commutation limit is about 3.5 ohm, which costs 0.9 W per running valve. Rejected.
- **Keep the T410 and rely on K1.** K1 would then have to break up to 31 A peak on every dead short, against a contact rated to switch 5 A, with the triac past its two-cycle surge figure if the relay is slow. Rejected.

The WeEn BTA204S-600D fits the same footprint and has a 5 mA gate and 6 mA
holding current, with 25 A surge and 3.1 A²s. It is the part to fit at Q901
if the master output must hold a relay coil under 100 mA, section 6.4. It
does not meet the short-circuit calculation of section 6.6.

All three parts have the same pin order: pin 1 T1, pin 2 and tab T2, pin 3
gate. The ST order is from the package drawings of DocID7699 (T410) and
DocID7472 (T810), which show the DPAK with A1 on the left lead, A2 on the tab
and G on the right lead. ST names the main terminals A1 and A2 where this
document says T1 and T2. The WeEn order is from table 2 of its data sheet.

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
output turns off during a 1.5 A rms inrush. The T810 guarantees 2.8 A/ms at a
re-applied dV/dt of 10 V/us. The margin is 3.5 without relying
on the snubber.

**Re-applied dV/dt.** When the triac stops, the valve's inductance and the
snubber capacitor ring. I estimate a solenoid at 0.13 to 0.25 H from typical
catalogue currents of 0.4 A inrush and 0.25 A holding at 24 V, 60 Hz, with
35 ohm of winding resistance. Those are assumed values, not measurements. With
C101 = 10 nF the ring frequency is 1 / (2 pi sqrt(0.13 x 10 nF)) = 4.4 kHz and
the fastest slope is 34 V x 27.7 krad/s = 0.94 V/us.

**Static dV/dt.** The T810 withstands 40 V/us to 400 V. Nothing on this board
can step more than the 77 V of the TVS clamp. A surge edge faster than 40 V/us
could still turn a triac on. It would conduct for at most one half-cycle and
then stop, because the gate is not driven. I assume a valve does not open in
10 ms. The onsemi MOC3023 withstands 1000 V/us; that figure belongs to this
maker's part.

**Snubber values.** R105 = 47 ohm and C101 = 10 nF are the values that every
published design in the prior-art survey uses. With the triac off, the snubber
passes 24 V x 2 pi x 60 Hz x 10 nF = 90 uA through the valve. It returns to
`ISNS`, like the TVS. The first draft returned it to `GND`. Review A1 pointed
out that a shorted C101 then feeds its valve through R105, about 0.3 A into a
35 ohm coil, by a path the shunt cannot see. Through `ISNS` that current is
measured, and the check for current with every output off finds it. The
snubber current of nine healthy channels is 0.8 mA, which is 0.5 mV at
`ISENSE_HI` and below what the measurement resolves. R105 dissipates
about C x V² x 2f = 1.4 mW in normal use and 4 W with C101 shorted, so it
will not last long in that fault; K1 is what removes the valve current.
C101 is rated 100 V.

### 6.6 Surge, short circuit and the fault limiter

**Inrush.** 1.5 A rms for 100 ms is inside the 8 A continuous rating.

**Dead short on a valve wire.** The specification says a shorted valve or
wire must not destroy the output. Three things act on a short, at different
speeds, and the output has to survive whichever of them ends it.

| What acts | When | What it needs |
|---|---|---|
| The comparator of section 9 removes gate drive | 70 us after the current passes 4.0 to 5.4 A | The triac must then stop at a current zero |
| K1 opens | Within 10 ms of the trip, section 7.2 | The contact must break the fault current |
| PTC F101 trips | Tens of milliseconds to about one second | Nothing. It is passive. |

**The fault current.** Inputs: 27.5 V rms open circuit, 38.9 V peak. A
transformer with 12 % regulation has an output impedance of about
0.12 x 24² / VA = 69 / VA ohm, which I treat as resistance. That is my
estimate, not a transformer data sheet. The board adds at least 0.31 ohm:
0.065 ohm of PTC at its lowest, 0.1 ohm of shunt, 0.05 ohm of triac, 0.068 ohm
for the two fuses and 0.03 ohm for the relay contact and copper. The triac
also drops 0.85 V.

| Transformer | Its impedance | Short at the terminals | Short behind 30 m of 18 AWG pair, 1.26 ohm |
|---|---|---|---|
| 20 VA | 3.45 ohm | 10.1 A peak, 7.2 A rms | 7.6 A peak, 5.4 A rms |
| 40 VA | 1.73 ohm | 18.7 A peak, 13.2 A rms | 11.5 A peak, 8.2 A rms |
| 75 VA | 0.92 ohm | 30.9 A peak, 21.9 A rms | 15.3 A peak, 10.8 A rms |

Peak current is (38.9 - 0.85) V over the sum of the resistances.

**Does the triac stop at the first current zero?** Only if the current falls
through zero slowly enough. A sine of peak Ip falls through zero at
2 pi f Ip. The T810 guarantees 5.4 A/ms at 125 °C when the voltage comes back
slowly, which it does across a short: the transformer's own sine rises at
0.015 V/us.

| Transformer | Fall at the zero, terminals, 50 and 60 Hz | Behind the cable, 50 and 60 Hz | Guaranteed at 125 °C |
|---|---|---|---|
| 20 VA | 3.2 and 3.8 A/ms | 2.4 and 2.9 A/ms | 5.4 A/ms |
| 40 VA | 5.9 and 7.0 A/ms | 3.6 and 4.3 A/ms | 5.4 A/ms |
| 75 VA | 9.7 and 11.6 A/ms | 4.8 and 5.8 A/ms | 5.4 A/ms |

Figure 11 of the data sheet shows the limit rising as the junction cools,
typically 1.6 times at 100 °C and 2.6 times at 75 °C. So the triac certainly
stops on a 20 VA transformer and on a 40 VA one behind a cable. On a 75 VA
transformer shorted at the terminals it is likely to conduct on, because the
half-cycle of fault current itself heats the junction. **I do not rely on
the triac stopping.**

**Does the triac survive if it does not stop, and K1 does not break the
current either?** Then the PTC ends the fault. Its data sheet gives one
point, a trip within 0.5 s at 8 A. I scale the time with the inverse square
of the current, which assumes the PTC heats with no loss during the trip.
That is an estimate and a bench item. Figure 6 of the triac data sheet gives
the surge the T810 survives against the number of cycles, read off the graph:
80 A for one cycle, 67 A for two, 60 A for three, 52 A for five, 47 A for
ten, 33 A for thirty and 27 A for a hundred.

| Transformer, short at the terminals | Fault current | PTC trips after about | Triac limit for that time | Margin in current |
|---|---|---|---|---|
| 20 VA | 10.1 A peak | 0.6 s, 37 cycles at 60 Hz | 31 A peak | 3.1 |
| 40 VA | 18.7 A peak | 0.18 s, 11 cycles | 46 A peak | 2.5 |
| 75 VA | 30.9 A peak | 0.07 s, 4 cycles | 55 A peak | 1.8 |

**By calculation the T810 carries a dead short on a transformer of 20 to
75 VA until the PTC trips, with no help from the comparator, the triac's own
turn-off or the relay.** Two things weaken the claim and both are in the
bench list. Figure 6 is for a junction that starts at 25 °C, and a triac
that was running a valve starts at 70 to 100 °C. And the PTC trip times
above 8 A are scaled, not specified. A short behind a cable is milder in
every row.

What the PTC-only case costs is a fuse. The 3 A time-lag fuses melt at
20 A²s. A 75 VA short that lasts 0.07 s is 21.9² x 0.07 = 34 A²s, and a 40 VA
one that lasts 0.18 s is 31 A²s. When the triac or K1 ends the fault within
two half-cycles, the worst figure is 21.9² x 0.02 = 9.6 A²s and the fuses
hold.

**K1 and the fault.** K1 releases at once on an over-current trip (section
7.2). If the triac has not stopped, its contact breaks up to 31 A peak at up
to 39 V. The contact is rated to switch 5 A. One such break may weld or
erode it. The firmware weld check of section 7.4 finds a welded contact
before the next run, and the surge clamp D23 on `COM` takes the inductive
kick. **K1's behaviour when it breaks a fault current is a bench item.** The
calculation above does not depend on it.

**The series element.** The specification allows a PTC, a fuse or a resistor.

| Element | In a dead short | In normal use | After the fault |
|---|---|---|---|
| Fuse, 1 A fast | Clears in milliseconds. I did not size one. | No loss | Someone must replace it. Every shorted wire is a service call. |
| Resistor, 2.2 ohm | Limits the peak. Carries the fault until something else acts, at tens of watts. | 0.55 W at 0.5 A, all the time | Nothing to reset, if it survived |
| PTC, 1.1 A hold | Trips in 0.07 to 0.6 s, by the estimate above | 0.02 to 0.1 W | Resets when power is removed |

F101 is a BHFUSE BSMD2920-100-60V, LCSC C3020148, 23,608 in stock at JLCPCB
on 2026-11-10. From its data sheet: 60 V, 1.10 A hold and 2.20 A trip at
25 °C, **40 A maximum fault current**, 0.5 s to trip at 8 A, 0.065 ohm at
its lowest and 0.41 ohm at most one hour after a trip. The first draft used
the Littelfuse 2920L110/60, which has the same hold and trip currents and a
maximum fault current of 20 A. A 75 VA transformer exceeds that, as the
review pointed out, so the part changed. The Littelfuse part fits the same
pads.

The hold current falls with temperature: 0.83 A at 50 °C, 0.73 A at 60 °C and
0.64 A at 70 °C, still above the 0.5 A load. At 70 °C the trip current is
about 1.3 A, below the 1.5 A inrush. The inrush lasts 100 ms and a PTC just
above its trip current takes seconds, so I do not expect a trip. That is a
bench item.

**TVS D101.** A Littelfuse SMBJ48CA from the output terminal to `ISNS`. Its
48 V stand-off is above the 45.3 V peak that appears across an off triac at
32 VAC. It clamps at 77.4 V, far below the triac's 600 V. It returns to `ISNS`
and not to `GND` so that a TVS that has failed short puts the valve current
through the shunt, where firmware can see current with every output off. A TVS
normally fails short, and a shorted TVS energises its valve. Section 7 is the
answer to that.

### 6.7 Dissipation and junction temperature

Two zones and the master valve on, 0.5 A rms each, T810 maximum on-state
model at 125 °C.

- **Per triac**: 0.85 V x 0.45 A average + 0.05 ohm x (0.5 A)² = 0.40 W.
- **Rise**: 0.40 W x 70 K/W = 28 K with 1 cm² of copper on the tab, the only figure the data sheet gives for the DPAK.
- **Junction**: 70 °C air + 28 K + 5 K allowed for neighbours = 103 °C. The limit is 125 °C.

Each tab is its own net, `T2_Zn`. The layout must give every triac its own
copper island of 2 cm² or more, on both sides with vias if the top side lacks
room. That is twice the area the calculation assumes. The
other heat sources while watering are the shunt at 0.23 W, three PTCs at up to
0.10 W each, the LDO at 0.27 W, the relay coil at 0.22 W, the backlight at 0.3 W
and the module. The total is about 3.5 W in a closed box. The enclosure
concept estimates a rise of 16 K at 3 W with vents, and the 70 °C air
temperature of section 3.3 follows from that. The R8 module is rated to 65 °C
and to 85 °C only with PSRAM error correction enabled, so firmware must enable
it: section 19.1, rule 7. See section 20.

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

Since review A1 the relay has a second job. Its coil current passes through a
transistor that has its own keep-alive detector, separate from the one that
gates the LED drive. If a fault holds the gate-drive detector on, the relay
still opens when the keep-alive stops. Section 8.5 has the combined fault
analysis.

### 7.2 Circuit

<!-- designsync:begin table block="B valve common relay" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| K1 | G5NB-1A-E DC5 relay SPST-NO 5 A, 5 V 125R coil | THT 20.4x7 mm | C48746 | coil+ `V_BUCK`, coil- `RLY_A`, contact 1 `AC_L`, contact 2 `COM` |  |
| D5 | 1N4148W | SOD-123 | C81598 | A `RLY_A`, K `V_BUCK` | Coil flyback |
| Q1 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `RLY_G1`, S `RLY_B`, D `RLY_A` | On when the expander bit RLY_ON is high. Off within 0.1 ms when DRV_EN resets the expander |
| R11 | 1k 1% | 0603 | C21190 | `RLY_ON` to `RLY_G1` |  |
| R12 | 100k 1% | 0603 | C25803 | `RLY_G1` to `GND` |  |
| Q2 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `RLY_G2`, S `GND`, D `RLY_B` | On while the relay detector sees keep-alive edges |
| R89 | 1k 1% | 0603 | C21190 | `KA` to `KB_N0` | Series resistor of the relay detector pump |
| C51 | 4.7 uF 25 V X5R | 0805 | C1779 | `KB_N0` to `KB_N1` | Pump capacitor. The gate of Q2 has no other path from the pin |
| D22 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `RLY_G2`, 3 common `KB_N1` | Its own package, not shared with D7 |
| R14 | 100k 1% | 0603 | C25803 | `RLY_G2` to `GND` | Bleed, with R90. Sets the relay hold-up |
| R90 | 100k 1% | 0603 | C25803 | `RLY_G2` to `GND` | Second bleed: one open resistor doubles the hold-up and no more |
| C50 | 10 uF 25 V X5R | 0805 | C15850 | `RLY_G2` to `GND` | Reservoir |
| R93 | 10k 1% | 0603 | C25804 | `V_BUCK` to `RLY_B` | Pulls the node between Q1 and Q2 up, so that firmware can see Q2 off |
| R94 | 100k 1% | 0603 | C25803 | `RLY_B` to `RLY_B_RB` | Read-back divider to GPIO7 |
| R95 | 150k 1% | 0603 | C22807 | `RLY_B_RB` to `GND` | 3.05 V at the pin when Q2 is off |
| D23 | TVS 48 V bidirectional 600 W | SMB | C151264 | `COM` to `GND` | Clamps the valve common while K1 is open and when it opens under load |
| JP1 | Solder jumper, open | PCB | none | `AC_L` to `COM` | Bridge only if K1 is not fitted |
| J2 | Header 7-way 3.81 mm pluggable, right angle | THT | C441187 | 1 `COM`, 2 `COM`, 3 `OUT_MV`, 4 `OUT_Z1`, 5 `OUT_Z2`, 6 `OUT_Z3`, 7 `OUT_Z4` | Valves A |
| J3 | Header 4-way 3.81 mm pluggable, right angle | THT | C441184 | 1 `OUT_Z5`, 2 `OUT_Z6`, 3 `OUT_Z7`, 4 `OUT_Z8` | Valves B (8-zone build only) |

<!-- designsync:end -->

The coil is between `V_BUCK` and two MOSFETs in series, Q1 above Q2.

- **Q1** conducts when the expander bit `RLY_ON` is high. The expander works only while `DRV_EN` is high, so Q1 needs firmware to ask for the relay and the gate-drive detector of section 8 to agree. When `DRV_EN` falls the expander resets, the pin floats and R12 discharges Q1's gate in 0.1 ms.
- **Q2** conducts while the relay detector of section 7.3 sees edges on the keep-alive pin. Nothing else can turn it on: its gate connects to a capacitor, two diodes and two resistors, and to no supply rail, logic output or GPIO.

K1 closes only when both conduct.

**Release is fast, on purpose.** The first draft had a network that was meant
to hold Q2 on for 30 to 50 ms after `DRV_EN` fell, so that the triacs would
stop before the contacts opened. Review A1 showed that it did nothing,
because Q1 let go first. The decision on finding M3 was to keep the fast
release and remove the hold parts, D21 and R13. So:

| Event | What K1 does |
|---|---|
| Over-current trip | `DRV_EN` falls within 70 us, Q1 turns off 0.1 ms later, the contacts part within the 10 ms release time. K1 may break fault current. Section 6.6. |
| Keep-alive stops, both detectors healthy | The gate-drive detector removes LED drive after 0.45 to 0.9 s and releases Q1. The relay detector releases Q2 after 0.12 to 0.95 s. Whichever is first opens K1. If the relay detector is first, K1 breaks the valve current, at most 1.5 A, inside its rating. |
| Keep-alive stops, gate-drive detector stuck on | Q2 opens K1 within 0.95 s, or 1.4 s if the pin stopped high. The triacs are still driven, and it does not matter. |
| Keep-alive stops, relay detector stuck on | `DRV_EN` falls within 0.9 s, or 1.2 s, and Q1 opens K1. |
| Firmware ends a run | Firmware turns the outputs off, waits 40 ms and clears `RLY_ON`. The contacts break no current. |

K1 is an Omron G5NB-1A-E with a 5 V coil of 125 ohm, 40 mA. From the English
Omron data sheet: the high-capacity contact is rated 5 A at 250 VAC and 3 A at
30 VDC, resistive, with 5 A maximum switching current. Operate and release
times are 10 ms maximum each. The must-operate voltage is 75 % of rated,
3.75 V, at a coil temperature of 23 °C. LCSC's copy of the sheet gives the
must-release voltage as 10 % of rated or more, 0.5 V, and the coil resistance
tolerance as 10 %.

**Pull-in margin.** Copper gains 0.39 % per kelvin, so a coil at 85 °C needs
3.75 V x 1.24 = 4.65 V. The coil is now fed from `V_BUCK`, as review finding
m4 recommended, not from `V5` behind the OR diode. The lowest `V_BUCK` is
5.16 V. Q1 and Q2 drop under 10 mV between them at 42 mA. The margin at the
hot corner is 5.15 / 4.65 = 1.11. From `V5` it was 1.03. The relay has
nothing to switch on USB power, so it loses nothing by having no supply
there.

**Surge clamp on COM.** D23, an SMBJ48CA from `COM` to `GND`, is new. While
K1 is open, which is most of the time, the `COM` terminals and the longest
wire of the installation were clamped only by the 750 VAC withstand of the
open contact. D23 clamps them at 77.4 V like every other field terminal. It
also takes the inductive kick when K1 opens with valve current flowing. A
valve at 0.5 A with 0.25 H stores 0.5 x 0.25 x 0.71² = 63 mJ at the current
peak; the TVS is rated 600 W for 1 ms, which is 0.6 J. With K1 closed D23 is
in parallel with D1 across the supply and does nothing.

**D5** returns the coil current to `V_BUCK` when Q1 or Q2 turns off.

**Why K1 is still a through-hole part.** The specification allows
through-hole parts only where no surface-mount one exists. I searched the
JLCPCB assembly library on 2026-11-10 for a surface-mount relay. What it
stocks are signal relays: Omron G6K (1 A) and G6S-2F (2 A), Panasonic TQ2SA,
TX2SA and AGQ (2 A), Hongfa HFD3 and HFD4 (2 A). Their contacts are rated to
carry 1 to 2 A and to switch 0.3 to 0.5 A at 125 VAC. This design needs 1.5 A
continuous and 2.5 A of inrush, must break 1.5 A of inductive current when
the keep-alive stops, and must carry tens of amps for the cycles a dead short
lasts. None of the stocked surface-mount relays meets that, and I found no
surface-mount power relay in stock. K1 stays through-hole, with the terminal
blocks.

JP1 is an open solder jumper across the contacts. If the project decides
against the relay, leave out K1, Q1, Q2, D5, D22, D23, R11, R12, R14, R89,
R90, R93 to R95, C50 and C51, and bridge JP1. The fault analysis of section
8.5 then no longer holds.

### 7.3 The relay keep-alive detector

R89, C51, D22, C50, R14 and R90 make a second diode pump on the keep-alive
pin. It is the same kind of circuit as the pump of section 8, and it ends at
a transistor gate where that one ends at a comparator.

Each rising edge of `KA` pushes charge through C51 and the upper diode of D22
into C50. Each falling edge refills C51 through the lower diode. R14 and R90
in parallel, 50 kohm, drain C50 all the time. While the pin toggles, the gate
of Q2 sits near 3.3 V less two Schottky drops. When it stops, at either
level, C51 passes no more charge and the gate decays with 50 kohm x 10 uF =
0.5 s until Q2 turns off.

The gate has no DC path from anything. That is the point of the circuit. A
pin stuck high, a logic output stuck high or a supply rail at the wrong
voltage cannot hold Q2 on.

**Values, and where they differ from the review's proposal.** Review A1,
section 3.4, proposed 1 kohm, 1 uF, a BAT54S, a 1 uF reservoir and two
1 Mohm bleed resistors, and said the proposal needed review. Its own
simulation showed the hold-up falling from 0.71 s at 27 °C to 0.36 s at
60 °C. The cause is leakage. At 2.7 V, 500 kohm draws 5 uA, and at the
turn-off threshold 1.3 uA. The Schottky model in that simulation leaks about
3 uA at 60 °C, back through the upper diode into C51. So when warm, the diode
discharges the reservoir as fast as the resistors do, and the timing depends
on a leakage that the diode's data sheet does not limit above 25 °C.

I kept the topology and changed three values: R14 and R90 are 100 kohm each,
C50 is 10 uF and C51 is 4.7 uF. The bleed current is ten times larger, 54 uA
at 2.7 V and 13 uA at the lowest threshold, so the same leakage moves the
timing by about 7 % instead of 50 %. C51 had to grow with it, because the
pump has to supply the bleed current: its output resistance is 1 / (f x C51),
2.1 kohm at the firmware's 100 Hz and 8.5 kohm at the slowest allowed rate.
Both capacitors are values that the gate-drive detector already uses, so the
change adds no BOM line.

**Timing.** From `hardware/sim/relay_detector.cir`. The AO3400A's gate
threshold is 0.65 to 1.45 V at 25 °C. Above the highest threshold every part
conducts, and below the lowest none does. K1 holds on a few milliamps, so it
may release anywhere between those two moments. I shifted both thresholds by
-3 mV/K for other temperatures. That figure is my assumption; the data sheet
gives none.

| Case | Gate while toggling | On after the first edge | Earliest release | Certainly released |
|---|---|---|---|---|
| Nominal parts, 100 Hz, -20 °C | 2.54 V | 40 ms | 0.23 s | 0.58 s |
| Nominal parts, 100 Hz, 27 °C | 2.69 V | 30 ms | 0.31 s | 0.70 s |
| Nominal parts, 100 Hz, 70 °C | 2.81 V | 22 ms | 0.32 s | 0.65 s |
| Small capacitors, 3.2 V rail, 25 Hz, -20 °C | 2.09 V | 82 ms | 0.12 s | |
| Small capacitors, 3.2 V rail, 100 Hz, -20 °C | 2.42 V | | 0.15 s | |
| Large capacitors, 3.4 V rail, 27 °C | | | | 0.80 s |
| The same, pin stops high | | | | 1.21 s |
| Large capacitors, 27 °C diodes read at the 70 °C threshold | | | | 0.93 s |
| The same, pin stops high | | | | 1.41 s |
| R14 or R90 open, nominal parts, 27 °C | | | | 1.45 s |
| R14 or R90 open, the worst case above | | | | 1.91 s, 2.86 s if the pin stops high |

"Small capacitors" is C50 at 7.0 uF and C51 at 3.3 uF, "large" is 11 uF and
5.2 uF: tolerance, temperature and DC bias taken together. Those ranges are
my allowance for a generic X5R part, not read from a curve. The row with
27 °C diodes and the 70 °C threshold is not a state any real part is in. It
is the upper limit that does not depend on the leakage model, because at
27 °C the model leaks 0.2 uA against 10 uA or more of bleed current.

**So the relay detector releases Q2 between 0.12 and 0.95 s after the last
edge, and within 1.4 s if the pin stops high.** With one bleed resistor open
the limits are 1.9 s and 2.9 s. The pin-high case is longer because C51 then
discharges through the diode into the reservoir as well.

The window is wide, a factor of eight. Most of it is the transistor's
threshold spread, which a comparator would remove. I kept the bare
transistor, because every part added between the pin and the gate is another
part that can stick. The gate-drive detector is the one with the comparator
and the tighter window, 0.45 to 0.9 s. Nothing requires the relay to open
after the triacs stop: if it opens first it breaks at most 1.5 A, inside the
contact rating, into the clamp D23. The earliest release, 0.12 s, is six
times the 20 ms the keep-alive contract allows between edges.

**Gate drive margin.** At the worst corner, -20 °C, a 3.2 V rail, the
smallest capacitors and the slowest allowed keep-alive of one edge per 20 ms,
the gate sits at 2.09 V. The highest threshold there is 1.59 V. The margin is
0.50 V. At the firmware's own 100 Hz it is 0.83 V. The AO3400A data sheet
(AOS, rev 3) guarantees 48 mohm at a gate voltage of 2.5 V and gives a
transconductance of 33 S at 5.7 A. Its on-resistance is not specified below
2.5 V. A transistor with 33 S that is 0.5 V above its threshold passes amps,
and the coil needs 0.042 A, so I expect millivolts of drop, and I cannot
quote a guaranteed figure. The level-1 model in the simulation is not
evidence for it. Measuring the drain voltage of Q2 at -20 °C is in the
bench list.

**Single faults inside the detector**, from the same simulation:

| Fault | Effect | Safe? |
|---|---|---|
| R89 open, C51 open, D22 either diode open, D22 lower diode short, C50 short, R14 or R90 short | Gate stays below 0.3 V. K1 never closes. | Yes. Found at the first run: no relay, no valve current. |
| R89 short | No change in timing, 0.74 s. The pin's own resistance limits the edge current. | Yes |
| R14 or R90 open | Release takes twice as long: 1.45 s typical, 1.9 s at worst, 2.9 s if the pin stops high. | Yes, slower. The timing test of section 7.4 finds it. |
| D22 upper diode short | Gate swings to 0.85 V at most. K1 does not pull in reliably. Off within 0.07 s of the last edge. | Yes |
| C50 open | Gate follows the pin through C51. Coil current swings between 0 and 40 mA at 100 Hz and K1 may chatter. Off within 10 ms if the pin stops low and 0.36 s if it stops high. | Yes |
| **C51 short** | Normal while the pin toggles. **If the pin stops high, Q2 stays on.** | No, alone. The gate-drive detector still releases Q1. The pin-high step of the self-test finds it. |
| **Q2 shorted drain to source, or gate to drain** | Q2 conducts whatever the detector does. | No, alone. The gate-drive detector still releases Q1. The read-back finds it before the next run. |

### 7.4 Read-back, self-test and weld check

GPIO7 reads the node between Q1 and Q2, `RLY_B`, through the divider R94 and
R95. R93 pulls that node up to `V_BUCK`.

| State | `RLY_B` | At GPIO7 |
|---|---|---|
| Q2 off | 5.08 V through R93 | 3.05 V, high. 2.94 to 3.15 V over the tolerance of `V_BUCK`. |
| Q2 on | 0 V | 0 V, low |
| Q2 shorted gate to drain | Its own threshold, 0.65 to 1.6 V | 0.4 to 0.95 V. Low as a logic level for most parts. GPIO7 is ADC1 channel 6, and a reading between 0.3 and 1.5 V is this fault. |
| No 24 VAC, USB only | 0 V, because `V_BUCK` is off | 0 V |

The read-back connects only to the drain of Q2, through 100 kohm. Driving
GPIO7 by mistake cannot turn Q2 on or energise the coil. It must never be
connected to the gate: a pin on the gate would be a DC path that the detector
exists to avoid.

**What the read-backs make testable.** GPIO42 reads `DRV_EN`, section 8.6.
With both, firmware can prove before every run that both detectors are off
when the keep-alive is stopped, and after every run how long each takes to
let go. A fault that sticks one detector on is then found at the next run,
while the other detector is still good. The full sequence is in section
19.7.

**Weld check.** A welded K1 contact and a shorted Q1 look the same: `COM` is
live although `RLY_ON` is low. Neither defeats the gate-drive detector, but
either removes the back-up that this section exists for. Firmware finds both
before every run, after the detector tests:

1. Keep-alive running, `RLY_ON` low, every valve bit off. K1 should be open.
2. Turn on the output that is about to run, with `RLY_ON` still low, for one measurement window of 100 ms.
3. Measure the current over that window. With K1 open it must be under the idle threshold of section 19.2.
4. If current flows, the contact is welded or Q1 is shorted. Turn the output off, stop the keep-alive, raise a fault and refuse to run until a person has acknowledged it.

A valve given 100 ms of current may click and does not pass water. The check needs a connected
valve on that output; on an output with nothing connected it proves nothing,
which is why it uses the output that is about to run. After an over-current
trip, where K1 may have broken a fault current, firmware runs the check on
the next run like any other.

### 7.5 What firmware does with it in normal use

1. Close K1 at least 20 ms before the first output turns on, and open it at least 40 ms after the last one turns off. The contacts then never make or break current.
2. With K1 closed and every output off, measure the current. More than the idle threshold means a shorted triac, TVS, opto-triac or snubber capacitor. Open K1, raise a fault and do not close it again until a person has acknowledged the fault.
3. With K1 open the current must read zero. That is a free check of the amplifier offset.

## 8. Fail-safe gate

### 8.1 Circuit

<!-- designsync:begin table block="F keep-alive detector" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| R33 | 10k 1% | 0603 | C25804 | `KA` to `GND` | Pull-down |
| R29 | 220R 1% | 0603 | C22962 | `KA` to `KA_N0` |  |
| C22 | 4.7 uF 25 V X5R | 0805 | C1779 | `KA_N0` to `KA_N1` | Pump capacitor |
| D7 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `KA_N2`, 3 common `KA_N1` |  |
| C23 | 10 uF 25 V X5R | 0805 | C15850 | `KA_N2` to `GND` | Reservoir |
| R30 | 110k 1% | 0603 | C25805 | `KA_N2` to `GND` | Bleed, with R91. Sets the hold-up time |
| R91 | 110k 1% | 0603 | C25805 | `KA_N2` to `GND` | Second bleed: one open resistor doubles the hold-up and no more |
| R31 | 100k 1% | 0603 | C25803 | `KA_N2` to `KA_INP` |  |
| R32 | 510k 1% | 0603 | C23192 | `KA_INP` to `KA_OK` | Hysteresis |
| C24 | 10 nF 50 V X7R | 0603 | C1589 | `KA_INP` to `GND` |  |
| R34 | 15k 1% | 0603 | C22809 | `3V3` to `KA_REF` |  |
| R35 | 10k 1% | 0603 | C25804 | `KA_REF` to `GND` |  |
| R36 | 10k 1% | 0603 | C25804 | `3V3` to `KA_OK` | Pull-up |

<!-- designsync:end -->

<!-- designsync:begin table block="E expander and drive enable" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| U5 | TCA9539PWR 16-bit I2C expander with reset | TSSOP-24 | C131972 | 1 INT `EXP_INT_N`, 2 A1 `GND`, 3 RESET `DRV_EN`, 4 P00 `ZD_Z1_N`, 5 P01 `ZD_Z2_N`, 6 P02 `ZD_Z3_N`, 7 P03 `ZD_Z4_N`, 8 P04 `ZD_MV_N`, 9 P05 `RLY_ON`, 10 P06 `P06`, 11 P07 `P07`, 12 GND `GND`, 13 P10 `ZD_Z5_N`, 14 P11 `ZD_Z6_N`, 15 P12 `ZD_Z7_N`, 16 P13 `ZD_Z8_N`, 17 P14 `VARIANT_N`, 18 P15 `SENS_FLT_N`, 19 P16 `P16`, 20 P17 `P17`, 21 A0 `GND`, 22 SCL `I2C0_SCL`, 23 SDA `I2C0_SDA`, 24 VCC `3V3` | Address 0x74 |
| C18 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U5 |
| RN1 | 100k x4 5% resistor array | 0603x4 | C1996 | 1 `ZD_Z1_N`, 8 `V_DRV`, 2 `ZD_Z2_N`, 7 `V_DRV`, 3 `ZD_Z3_N`, 6 `V_DRV`, 4 `ZD_Z4_N`, 5 `V_DRV` | Holds the valve pins at the LED supply while the expander is in reset |
| RN2 | 100k x4 5% resistor array | 0603x4 | C1996 | 1 `ZD_Z5_N`, 8 `V_DRV`, 2 `ZD_Z6_N`, 7 `V_DRV`, 3 `ZD_Z7_N`, 6 `V_DRV`, 4 `ZD_Z8_N`, 5 `V_DRV` | As RN1. Fitted on both builds, so that P10 to P13 never float |
| RN3 | 100k x4 5% resistor array | 0603x4 | C1996 | 1 `P06`, 8 `GND`, 2 `P07`, 7 `GND`, 3 `P16`, 6 `GND`, 4 `P17`, 5 `GND` | Spare pins held low |
| R96 | 100k 1% | 0603 | C25803 | `ZD_MV_N` to `V_DRV` | As RN1, for the master valve pin |
| R27 | 10k 1% | 0603 | C25804 | `3V3` to `VARIANT_N` |  |
| R28 | 1k 1% | 0603 | C21190 | `VARIANT_N` to `GND` | Variant strap (8-zone build only) |
| U6 | LM2901DR quad comparator | SOIC-14 | C7932 | 1 1OUT `KA_OK`, 2 2OUT `OC_TRIP_N`, 3 VCC `V5`, 4 2IN- `OC_CMP`, 5 2IN+ `VH`, 6 1IN- `KA_REF`, 7 1IN+ `KA_INP`, 8 3IN- `VL`, 9 3IN+ `OC_CMP`, 10 4IN- `VL`, 11 4IN+ `ZC_DIV`, 12 GND `GND`, 13 4OUT `AC_ZC`, 14 3OUT `OC_TRIP_N` |  |
| C19 | 100 nF 50 V X7R | 0603 | C14663 | `V5` to `GND` | At U6 |
| U7 | 74HC132D quad NAND, Schmitt-trigger inputs | SOIC-14 | C5601 | 1 1A `OC_TRIP_N`, 2 1B `FAULT_N`, 3 1Y `FAULT`, 4 2A `KA_OK`, 5 2B `FAULT`, 6 2Y `FAULT_N`, 7 GND `GND`, 8 3Y `DRV_EN_N`, 9 3A `KA_OK`, 10 3B `FAULT_N`, 11 4Y `DRV_EN`, 12 4A `DRV_EN_N`, 13 4B `DRV_EN_N`, 14 VCC `3V3` |  |
| C20 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U7 |
| Q3 | AO3401A P-MOSFET 30 V | SOT-23 | C15127 | G `DRV_EN_N`, S `3V3`, D `V_DRV` | Switches the opto LED supply |
| R25 | 100k 1% | 0603 | C25803 | `3V3` to `DRV_EN_N` | Keeps Q3 off while 3V3 ramps |
| R26 | 10k 1% | 0603 | C25804 | `V_DRV` to `GND` | Bleed |
| C21 | 100 nF 50 V X7R | 0603 | C14663 | `V_DRV` to `GND` |  |
| R92 | 10k 1% | 0603 | C25804 | `DRV_EN` to `DRV_EN_RB` | Read-back of DRV_EN to GPIO42 |

<!-- designsync:end -->

### 8.2 How it works

Firmware toggles `KA`, GPIO38. Each rising edge pushes charge through C22 and
D7 into the reservoir C23. R30 and R91 in parallel, 55 kohm, drain C23 all the time. They are two resistors so that one of them failing open doubles the hold-up and does not remove the bleed. Comparator U6A compares
the reservoir voltage with a reference and drives `KA_OK` high when the
reservoir is charged.

`KA_OK` goes to U7, a 74HC132. It is the 74HC00 of the first draft with
Schmitt-trigger inputs, in the same pinout: two of its inputs rise through
10 kohm pull-ups from open-collector comparators, more slowly than the
74HC00 data sheet allows. Two gates form the over-current latch of
section 9. The third makes `DRV_EN_N` = NAND(`KA_OK`, `FAULT_N`). The fourth
inverts it to `DRV_EN`. So drive is enabled when the keep-alive is running and
the latch is clear.

`DRV_EN` does three things, and any one of them alone turns the valves off.

| Path | Mechanism | Effect when `DRV_EN` is low |
|---|---|---|
| LED supply | `DRV_EN_N` high turns off Q3 | `V_DRV` falls. No opto LED can conduct, whatever the expander pins do. |
| Expander reset | `DRV_EN` is the TCA9539 RESET pin | Every expander pin becomes an input. The stored output bits are lost, so nothing comes back on when drive returns. Resistors hold every pin at a defined level meanwhile. |
| Relay, first transistor | The reset expander releases `RLY_ON`, the gate of Q1 | K1 opens within its 10 ms release time. |

All three hang on one signal. The relay's second transistor, Q2, does not:
it has the detector of section 7.3. Section 8.5 shows why that matters.

No ESP32 pin other than `KA` and the I2C bus can influence a valve. Turning one
on takes a running keep-alive and two valid I2C writes to address 0x74.

**Defined levels on the expander pins.** While the TCA9539 is in reset, which
is whenever no valve runs, every pin is an input. Review finding m10: the
data sheet specifies the supply current only for inputs at a rail. RN1, RN2
and R96 pull the nine valve pins up to `V_DRV`, 100 kohm each. They go to
`V_DRV` and not to `3V3` so that an off opto LED never sees a reverse
voltage: its limit is 3 V. With `V_DRV` off, R26 holds the pins at ground.
RN3 pulls the four spare pins down. `RLY_ON` already had R12, and the two
inputs their pull-ups. RN2 is fitted on the 4-zone build as well, where P10
to P13 have nothing else on them.

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
| 10 Hz square wave | Enabled 104 ms after the first edge. Disabled 0.59 s after the last. |
| 20 Hz | Enabled after 53 ms. Disabled after 0.65 s. |
| 100 Hz, the firmware's 5 ms toggle | Enabled after 14 ms. Disabled after 0.73 s. |
| 1 kHz | Enabled after 8 ms. Disabled after 0.74 s. |
| 100 Hz, then stuck high instead of low | Disabled after 1.09 s |
| 100 Hz, C23 30 % low | Disabled after 0.51 s |
| 1 kHz, C23 20 % high | Disabled after 0.88 s |
| 2 Hz for 2.4 s | Never enabled. Reservoir peaks at 1.41 V against the 1.55 V threshold. |
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
| Hold-up, pin stops low, -20 to 60 °C with tolerances | 0.46 to 0.78 s |
| Hold-up, pin stops high, longest | 1.17 s |
| Hold-up at 85 °C | 0.43 s with nominal parts, 0.30 s at the short corner. Schottky leakage adds to the bleed. The leakage model is generic. |
| Hold-up with R30 or R91 open | 1.74 s at 27 °C, 1.35 s at 60 °C |
| Time to enable at 100 Hz | 14 ms nominal, 41 ms at the slow corner |
| Time to enable at 25 Hz | 43 ms nominal, 0.16 s at the slow corner |
| Time to enable at 10 Hz | 0.10 s nominal, 0.50 s at the slow corner, where the reservoir clears the threshold by 11 % only |
| `3V3` ramps over 0.1 s or 1 s | `KA_OK` stays low with the pin low. With the pin tied to the rail it follows the rail to 0.95 V and falls again, below the voltage at which U7 or the expander work. |

All figures are for the bleed of two 110 kohm resistors, 55 kohm, that
replaced the single 56 kohm of the first draft. They moved by 2 % or less.

The short corner is C23 at 7 uF, C22 at 3.3 uF, the bleed 1 % low, `3V3` at 3.2 V
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

**The claim.** After the keep-alive stops, no single component fault leaves
a valve on. A valve has current only if two things are true together:

- **K1 is closed.** That needs Q1 and Q2 both conducting. Q1 needs the expander bit `RLY_ON`, and so `DRV_EN` from the gate-drive detector of this section, called A below. Q2 needs the relay detector of section 7.3, called B.
- **Its triac conducts.** That needs LED current from `V_DRV`, which A switches, into an expander pin that A holds in reset. Or an output device has failed short.

A and B share the keep-alive pin, the ground and nothing else. A uses `3V3`
and `V5`. B uses no supply rail. Their pump diodes are in separate packages,
D7 and D22. So a fault in A leaves B to open K1, and a fault in B leaves A to
remove gate drive and release Q1. The tables go through every part to check
that, and then through what the two share.

"Found by" names the firmware test of section 19.7 that finds a fault while
the other detector still covers it. The idle test is step 1 there, the
pin-high test step 2, the start test steps 3 and 4, the weld check step 5,
the run check steps 6 and 7 and the stop test step 9. A fault that hides until a second fault
arrives is the thing to avoid, so every row that says "stuck on" has a test.

**States of the keep-alive pin and of the firmware.**

| Fault or state | What happens | Outputs |
|---|---|---|
| `KA` stuck low | No edges. Both reservoirs drain. | Gate drive off within 0.9 s, K1 open within 0.95 s |
| `KA` stuck high | C22 and C51 block DC. | Gate drive off within 1.2 s, K1 open within 1.4 s |
| `KA` floating: chip in reset, EN low, or unprogrammed | R33 pulls it low. | Off |
| `KA` toggling at the wrong rate | Anything from about 3 Hz up enables A, whatever the duty cycle. B needs about the same. | Neither detector catches this. Section 8.4. |
| USB flashing | The ROM loader runs. GPIO38 stays an input. No edges. | Off |
| Boot | GPIO38 has no power-up glitch in datasheet table 2-2. One edge enables neither detector: A's reservoir reaches 1.0 V against 1.55 V, and B's gate about 1.0 V for under 0.5 s with Q1 off. | Off |
| Brownout of `3V3` | The supervisor U12 holds the ESP32 in reset and toggling stops. The expander keeps its bits down to about 1 V. | Off within 0.95 s. Until then the outputs that were on stay on. |
| Firmware hangs with outputs on | Toggling stops. | Off within 1.4 s of the last edge |
| I2C bus stuck by a fault | Firmware cannot write the expander. It must stop toggling. | Off within 0.95 s of that decision |
| GPIO40, GPIO42 or GPIO7 driven as an output by mistake | R87, R92 and R94 keep each from moving the node it reads. | No effect |

**Detector A and the logic behind it.** Each fault was simulated in
`hardware/sim/keepalive_faults.cir`. A resistor is taken to fail open, a
capacitor open or short, a diode or an IC output either way. Resistor shorts
are in the last table.

| Fault | What happens | Valves after the keep-alive stops | Found by |
|---|---|---|---|
| R29 open, C22 open, D7 either diode short or open, C23 short, C24 short, R35 open | No charge reaches the comparator, or the reference sits at the rail. A never enables. | Off | Start test: `DRV_EN_RB` stays low |
| R31 open from power-up | The comparator input stays near 0.27 V. A never enables. | Off | Start test |
| C23 open | `KA_OK` follows the keep-alive and resets the expander on every cycle. | Off | Start test: expander does not stay configured |
| R32 open | No hysteresis. Hold-up falls to 0.45 s. | Off | Not found. Harmless. |
| R33 open | No pull-down. A pin in reset floats and gives no edges. | Off | Not found. Harmless. |
| R36 open | `KA_OK` is pulled up through R32 only, to 2.9 V. | Off | Not found. Harmless. |
| R30 or R91 open | The bleed is 110 kohm. Hold-up 1.7 s at 27 °C. | Gate drive off after 1.7 s. K1 open within 0.95 s through B. | Stop test: `DRV_EN_RB` takes more than 1.0 s to fall |
| **R31 opens while drive is enabled** | R32 holds the comparator input at 3.3 V. **A stuck on** until power is cycled. | K1 opens through Q2 within 0.95 s, 1.4 s if the pin stopped high | Stop test: `DRV_EN_RB` does not fall |
| **R34 open** | The reference is at 0 V. **A stuck on** from power-up. | As above | Idle test: `DRV_EN_RB` high with no keep-alive |
| **C22 short** | A passes DC. Normal with a toggling pin. **A stuck on while the pin is high.** | As above | Pin-high test |
| **U6: output 1 never pulls low, or the IC loses its supply or ground pin** | `KA_OK` is pulled high. **A stuck on.** With the supply pin open, `AC_ZC` and the over-current comparators stop as well. | As above | Idle test |
| **U7: output 3 stuck low, or output 4 stuck high** | `DRV_EN` high. Output 3 stuck low also holds the LED supply on. **A stuck on.** | As above | Idle test |
| U7: output 3 stuck high, output 4 stuck low | Drive never enabled. | Off | Start test |
| U7: latch output stuck so that `FAULT_N` stays high | The over-current latch cannot trip. Keep-alive behaviour unchanged. | Off | Not found by a keep-alive test. Section 9.4. |
| Q3 shorted drain to source | `V_DRV` stays on. The expander reset still floats every pin, and RN1, RN2 and R96 hold the LED cathodes at `V_DRV`. | Off | Not found. See the note below. |
| R25, R26 or C21 open | `V_DRV` falls more slowly, or Q3's gate floats while `3V3` ramps. | Off | Not found. Harmless. |
| U5: `RESET` input ignored, bits kept | Valve bits stay set. `V_DRV` is off, so no LED current. `RLY_ON` stays high, so Q1 stays on. | K1 opens through Q2 | Idle test: the expander answers on I2C |
| U5: a valve pin stuck low | That valve follows `V_DRV` during runs. | Off, because `V_DRV` is off | Run check: current with that output off |
| U5: `RLY_ON` pin stuck high, or R12 open | Q1 conducts or floats on. K1 follows Q2 alone. | K1 opens through Q2 | Weld check |
| RN1, RN2, R96: one element open | That pin floats in reset. A floating pin sinks no LED current. | Off | Not found. Harmless. |

**Detector B, the relay and its drive.** From
`hardware/sim/relay_detector.cir`; the table in section 7.3 has the numbers.

| Fault | What happens | Valves after the keep-alive stops | Found by |
|---|---|---|---|
| R89 open, C51 open, D22 either diode open, D22 lower diode short, C50 short | Q2 never turns on. K1 never closes. | Off | Start test: `RLY_B_RB` stays high |
| D22 upper diode short, C50 open | K1 does not pull in reliably, or chatters. | Off within 0.4 s | Start test, or no valve current |
| R14 or R90 open | Q2 releases after 1.45 s typical, 1.9 s at worst, 2.9 s if the pin stops high. | Gate drive off within 0.9 s through A. K1 open late. | Stop test: `RLY_B_RB` takes more than 1.0 s to rise |
| **C51 short** | B passes DC. **B stuck on while the pin is high.** | Gate drive off within 1.2 s through A, which also releases Q1 | Pin-high test |
| **Q2 shorted drain to source or gate to drain** | **B stuck on.** | As above | Idle test: `RLY_B_RB` not high |
| Q2 gate shorted to source, or open | K1 never closes. | Off | Start test |
| **Q1 shorted** | K1 follows Q2 alone. | K1 opens through Q2. Gate drive off through A. | Weld check |
| Q1 open, R11 open | K1 never closes. | Off | No valve current |
| **K1 contact welded, or JP1 bridged** | `COM` is live always. | Gate drive off within 0.9 s through A. A healthy triac then stops. | Weld check |
| K1 coil open | K1 never closes. | Off | No valve current |
| D5 open | No path for the coil current at turn-off. Q1 or Q2 avalanches at each release, with 0.16 mJ by my estimate for a 0.2 H coil. Not a rated use of the AO3400A; either may fail short in time. | Off through A in any case | Weld check, or the idle test, when one of them has failed |
| D5 short | The coil is bypassed. Q1 and Q2 short `V_BUCK` when both conduct and the supply collapses. | Off | Board resets when the relay is asked for |
| D23 short | `COM` is shorted to `GND`. F1 or F4 opens when K1 closes. | Off | No valve current |
| D23 open | No clamp on `COM`. | Off | Not found |
| R93 open, R94 open, R95 short | `RLY_B_RB` reads low always. | Unchanged | Idle test fails. A false alarm on the safe side. |
| R95 open | The pin reaches about 4.8 V through 110 kohm, 15 uA into its protection diode. Reads high as it should. | Unchanged | Not found. Harmless. |
| R92 open | `DRV_EN_RB` floats; the internal pull-down reads it low. | Unchanged | Start test fails. A false alarm on the safe side. |

**Output devices.**

| Fault | Valves after the keep-alive stops | Found by |
|---|---|---|
| Triac, TVS D101, opto-triac output or snubber capacitor shorted | K1 opens through Q1 and through Q2 | Run check: current with K1 closed and every output off |

**What A and B share.**

| Shared item | Fault | Effect | Answer |
|---|---|---|---|
| The pin, GPIO38 | Stuck at either level, or floating | No edges reach either detector. | Safe. First table. |
| The `KA` net | Bridged to a net that toggles with no firmware behind it | Both detectors could be fed at once. This is the one board fault that could defeat both. | Layout rules below. One net on the board can do it. |
| The firmware | Keeps toggling the pin while the control loop is dead | Both detectors stay on. | Not a component fault. The contract of section 19.4 exists for it. |
| `3V3` | Low or absent | The ESP32 stops. U7 has no supply and Q3 no source voltage. | Safe |
| `3V3` | High, for example U3 shorted input to output | Either the ESP32 keeps running, and then nothing is wrong with the keep-alive, or it stops and there are no edges. A's thresholds scale with the rail. B does not use it. | Safe |
| `V5` | Absent | No `3V3` either. | Safe |
| `V_BUCK` | Absent | The coil of K1 has no supply. | Safe |
| `V_BUCK` | High: the switch of U1 shorted, up to 45 V on every 5 V part | Not predictable. See below. | Open point |
| Ground | Lost between the two detectors | A layout fault, not a component fault. | One ground plane |
| Diode packages | D7 fails in any way | Only A is affected. D22 is a separate package. | Safe |
| Same part type | C22 and C51 are the same 4.7 uF part, C23 and C50 the same 10 uF part | A cracked ceramic capacitor fails short, and board flex can crack two at once. Two shorted pump capacitors is a double fault, and each one alone is found by the pin-high test. | Place the two pumps apart and turn C22 and C51 at right angles to each other. |
| K1 | Welded | In the table above. | Weld check |

**The bridged `KA` net.** A solder bridge or a conductive deposit from `KA`
to a net that toggles by itself would feed both detectors with no firmware
behind it. The nets that toggle with a dead processor are these:

- **`AC_ZC`**, 50 or 60 Hz from comparator U6D whenever 24 VAC is present. It is an open-collector output with a 10 kohm pull-up, so against R33 it raises `KA` to 1.4 V at most. `hardware/sim/ka_bridge.cir` ties the two nets together with the ESP32 pin floating. A's reservoir settles at 0.89 to 1.03 V, short of the 1.55 V that enables it. B's gate settles at 0.83 to 0.95 V, which is enough to turn on a Q2 from the low end of the threshold range. So this bridge can hold B on and cannot enable A: one detector, not both. I first estimated that neither would charge, and the simulation showed otherwise. The bridge also shows itself. While firmware drives the pin, `AC_ZC` follows the keep-alive and not the mains, and firmware sees a wrong mains frequency or no 24 VAC.
- **`TFT_BL`**, the backlight PWM, 1 to 20 kHz from the LEDC peripheral. It keeps running when the firmware hangs, it is a push-pull 3.3 V output, and it would run both pumps. **This bridge must be made impossible by layout.**
- **The buck switch node**, 281 kHz at up to 45 V. A bridge to it destroys the pin.
- The SPI and I2C clocks stop when the firmware stops.

Layout rules that follow:

1. Route `KA` from module pin 31 to R29 and R89 with ground on both sides, at least 1 mm from any other signal, and nowhere next to `TFT_BL`, `AC_ZC` or the buck. The neighbouring module pins are GPIO37, not connected, and GPIO39, a button input.
2. Put no test pad on `RLY_G2`, `KB_N1`, `KA_N1` or `KA_N2`'s diode side that a probe or a flux residue could bridge to a rail. TP8 on `KA_N2` stays, with ground around it.
3. Surround the gate node of Q2 with ground. A leakage path from any rail to that node is the only way to hold Q2 on without edges.
4. Place the A pump (R29, C22, D7) and the B pump (R89, C51, D22) at least 10 mm apart.

**The regulator fault that this analysis does not cover.** If the switch
inside U1 shorts, `V_BUCK` rises to `VIN_DC`, 30 to 45 V. U3, the ESP32, U5,
U6, U7 and U8 are all far beyond their ratings, and what a destroyed part
does cannot be read from a data sheet. Two properties of the relay path do
not depend on any of them. Q2's gate has only an AC path, so with no edges
it turns off after its hold-up whatever the rails do. And Q1 and Q2 in
series withstand 60 V, so the over-voltage cannot push current through the
coil by itself. A dying processor that keeps toggling GPIO38 at more than
3 Hz is the case that would defeat it, and I cannot rule that out on paper.
The usual cure is an over-voltage clamp or crowbar on `V_BUCK`. Revision A
has none. It is risk 21 in section 20 and a decision for the owner.

**Resistor shorts.** The brief for this analysis asks for every part open
and short. A thick-film chip resistor does not normally fail short, and the
simulations did not include those cases. By inspection:

| Short | Effect |
|---|---|
| R29, R89 | The pin's own resistance limits the pump current. No change in function. |
| R30, R91, R14, R90, R35, R33 | The reservoir, reference or pin is grounded. That detector never enables. Safe. |
| R31 | No hysteresis divider. The comparator sees the reservoir directly. Still releases. |
| R32 | The comparator input is tied to its output. With the output high it stays high: **A stuck on.** Covered by B, found by the idle or stop test. |
| R34 | The reference is at `3V3`. A never enables. Safe. |
| R36 | `KA_OK` is tied to `3V3`: **A stuck on.** Covered by B, found by the idle test. |
| R92 | GPIO42 sits directly on `DRV_EN`. Harmless while the pin is an input, which it always is. |
| R93 | `V_BUCK` is on the drain of Q2. When Q2 turns on it shorts the supply and the board resets. Safe. |
| R94 | GPIO7 sees up to 5.1 V through R93. No effect on the valves. |
| R11, R12 | Q1 is driven without its series resistor, or its gate is grounded. Safe. |

**Faults that no test finds.** Q3 shorted is the only one in these tables
that is not harmless and not found: `V_DRV` then stays on, and the expander
reset is the only thing that removes LED current. It is still one of two
layers, because B opens K1 regardless. Firmware could find it with one more
read-back, and the pins are used up. I left it.

**Summary.** Every fault marked "stuck on" in A is covered by B and the
relay. Every such fault in B, in Q1 or in K1 is covered by A. Each is found
by a firmware test at the next run. What is left is a bridge from `KA` to
the backlight net, which the layout must exclude, and the regulator
over-voltage, which is open. The analysis is on paper and in simulation.
Nothing here has been built, and section 21 has the bench tests that repeat
it on hardware by opening and shorting the parts one at a time.

### 8.6 Read-back of `DRV_EN`

R92, 10 kohm, takes `DRV_EN` to GPIO42. It is the signal that resets the
expander, read after the over-current latch, so it shows the state of
detector A and of the latch together. `OC_FAULT_N` on GPIO40 tells the two
apart.

Firmware enables the internal pull-down on GPIO42. U7 drives the node
through 10 kohm against about 45 kohm, which reads as a solid high, and an
open R92 reads low. GPIO42 is one of the JTAG pins and is free because JTAG
runs over the USB controller.

The first draft could tell whether drive was enabled only by asking whether
the expander answered on I2C. That check stays, because it also finds an
expander that ignores its reset pin, but it cannot time anything. With the
read-back firmware measures how long A takes to release after every run,
which is how an open R30 or R91 is found.

### 8.7 Boot behaviour of the pins

The reset state and power-up glitch of every GPIO are in `hardware/pinmap.yaml`,
taken from tables 2-1 and 2-2 of the ESP32-S3 datasheet. The ones that matter
here:

| Pin | Use | At reset | Power-up glitch | Consequence |
|---|---|---|---|---|
| GPIO38 | `KA` | High impedance | None | R33 holds it low |
| GPIO8, GPIO9 | I2C0 | High impedance | Driven low for 60 us | Not a valid I2C write. The expander is in reset anyway. |
| GPIO7 | Relay detector read-back | High impedance | Driven low for 60 us | 100 kohm in series. It cannot move the node it reads. |
| GPIO42 | `DRV_EN` read-back | High impedance | None | 10 kohm in series against a push-pull output. No effect. |
| GPIO45 | Sensor supply enable | Weak pull-down | None | R86 holds it low. Supply stays off, and the strap reads 0. |
| GPIO21 | Backlight | High impedance | None | R62 holds the MOSFET off |
| GPIO18 | Back button | High impedance | Low, then high, 60 us each | Input only. Harmless. |
| GPIO19, GPIO20 | USB | USB pull-up on D+ | High pulses | USB only |
| GPIO0, 3, 46 | Strapping | See section 12.3 | None listed for 0 and 46 | No valve function |

## 9. Hardware over-current path

**Decision: yes.** A comparator on the shunt signal removes drive without
firmware and releases K1. Firmware samples current in 100 ms
windows. In 100 ms a shorted wire puts ten half-cycles of fault current through
the triac, the PTC and the fuses. The hardware path usually limits it to one
or two. With the 8 A triac the output survives without this path (section
6.6). What the path still buys is that a short ends before a fuse opens, and
that firmware gets a latched signal that says why.

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
15 mV of comparator offset and 10 mV of amplifier offset. I added these as
worst case and got 4.2 to 5.1 A peak. Review A1 ran the corners in
`hardware/sim/isense_corners.cir`, which also moves the two resistors that
set the amplifier's rest level: 4.0 to 5.4 A peak. The nominal trip is 4.56 A
for one polarity and 4.67 A for the other, not 4.64 A for both, because the
amplifier's bias network loads the chain. The largest legitimate current is
two zones holding at 0.5 A and a third in inrush at 1.5 A, which is 2.5 A rms
or 3.54 A peak if all are in phase. The lowest trip level clears that by
13 %. It does not clear two outputs in inrush together, and switching on at
a random point of the cycle can add up to a fifth to the first peak of an
inductive load. So firmware must stagger starts, rule 10 of section 19.1, and
nuisance trips at the specification's load limit are a bench item.

The latch is two NAND gates of U7. `OC_TRIP_N` low sets `FAULT`. `KA_OK` low
clears it. The red LED D8 shows it.

### 9.2 Timing

`hardware/sim/isense.cir` adds a 10 A rms short to a 1 A load at a rising point
of the cycle. `OC_TRIP_N` falls 66 us after the short begins and `DRV_EN_N`
rises within a microsecond of that. The opto LED goes dark. The triac conducts
to the end of its half-cycle, at most 10 ms at 50 Hz, and is not fired again.
On a large transformer its data sheet does not guarantee that
it blocks at that zero (section 6.6). K1 loses its coil current at the same
moment, because the expander resets and Q1 turns off, and opens within
10 ms. If neither interrupts the current, the PTC does.

### 9.3 Clearing the latch

There is no clear pin. Firmware clears the latch by stopping the keep-alive
for longer than the hold-up time of detector A. That also resets the expander, so every
output is off when drive returns. Firmware reads the latch on GPIO40,
`OC_FAULT_N`, low when set. Section 19.8 has the exact read and clear
procedure.

GPIO40 reads the latch through R87, 10 kohm. `FAULT_N` is the feedback node
of the latch: it feeds U7 pins 2 and 10. Review A1 found that with GPIO40
wired straight to it, a pin left configured as an output and driven high
could hold the node above the gate threshold against the gate's output. That
clears or masks the latch, and firmware then reads "no fault". With R87 the
pin can move the node by at most 3.3 V x 10 kohm against the gate's output
resistance of under 100 ohm, about 30 mV.

After a trip firmware knows which output it switched on last. It should mark
that output faulted, wait, restart the keep-alive and continue with the others.

### 9.4 What this path does not cover

- **A weak transformer.** If a short draws less than 4.0 A peak, about 2.8 A rms, the comparator may not trip. Firmware's rms limit catches it within a few hundred milliseconds. The triac is rated 8 A continuous, so it survives that.
- **A latch that cannot set.** If a fault in U6 or U7 keeps `FAULT_N` high, nothing in hardware removes drive on a short. The triac and the PTC carry the short as section 6.6 calculates, and firmware's rms limit turns the output off. Firmware cannot test the latch without a real over-current. The bring-up plan injects one.
- **A shorted triac.** Section 7.
- **Noise.** A trip from a noise spike latches and costs one retry. R52 and C33 reject anything shorter than about 50 us. Whether that is enough is a bench question.

## 10. Current measurement

### 10.1 Circuit

<!-- designsync:begin table block="G current sense and over-current latch" -->

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

<!-- designsync:end -->

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

<!-- designsync:begin table block="H AC zero-cross" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| R55 | 100k 1% 200 V 0.25 W | 1206 | C17900 | `AC_L` to `ZC_CLAMP` |  |
| R56 | 10k 1% | 0603 | C25804 | `ZC_DIV` to `GND` |  |
| C34 | 10 nF 50 V X7R | 0603 | C1589 | `ZC_DIV` to `GND` |  |
| D9 | BAT54S dual Schottky, series | SOT-23 | C47546 | 1 A `GND`, 2 K `3V3`, 3 common `ZC_CLAMP` | Clamp |
| R88 | 10k 1% | 0603 | C25804 | `ZC_CLAMP` to `ZC_DIV` | Halves the clamped level at the comparator pin |
| R57 | 1M 1% | 0603 | C22935 | `ZC_DIV` to `AC_ZC` | Hysteresis |
| R58 | 10k 1% | 0603 | C25804 | `3V3` to `AC_ZC` | Pull-up |

<!-- designsync:end -->

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
- **Temperature**: the R8 modules are rated to 65 °C ambient, not 85 °C. The module datasheet says the limit rises to 85 °C when PSRAM ECC is enabled, at the cost of one sixteenth of the PSRAM. **Firmware must enable PSRAM ECC**, because the design temperature inside the enclosure is 70 °C (section 3.3). Section 20 lists the enclosure temperature as a risk.

<!-- designsync:begin table block="D MCU" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| U4 | ESP32-S3-WROOM-1-N16R8 | module 18x25.5 mm | C2913202 | see pin map |  |
| C14 | 22 uF 25 V X5R | 1206 | C12891 | `3V3` to `GND` | At module pin 2 |
| C15 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At module pin 2 |
| R18 | 10k 1% | 0603 | C25804 | `3V3` to `EN` |  |
| C16 | 1 uF 50 V X5R | 0603 | C15849 | `EN` to `GND` |  |
| U12 | TLV803EA30DBZR supervisor 3.0 V, open drain, 200 ms | SOT-23 | C5218924 | 1 GND `GND`, 2 RESET `EN`, 3 VDD `3V3` | Holds EN low while 3V3 is below 3.0 V and for 200 ms after |
| C54 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U12 |
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
| R99 | 0R | 0603 | C21189 | `U0TXD_MCU` to `U0TXD` | Series footprint |

<!-- designsync:end -->

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
| Current sense, two ADC channels | 2 |
| Keep-alive, over-current flag, zero-cross | 3 |
| Read-back of the two keep-alive detectors | 2 |
| **Total without valves** | **25** |

Nine direct valve pins would bring it to 34. With the expander all 25 are
used and none is spare. Three more signals sit on strapping pins where
they can do no harm: the supply monitor on GPIO3, the status LED on GPIO46
and the sensor supply enable on GPIO45.

**What moved after the review.** The decision on finding B1 asked for a
read-back of `DRV_EN`, and GPIO42, the one spare pin, took it. Making a stuck
relay transistor visible needed a second read-back, section 7.4. I took GPIO7
for it, because it is an ADC pin and the read-back is more useful as a
voltage, and moved the sensor supply enable from GPIO7 to GPIO45. That is my
choice, not the review's. It costs nothing in function: section 12.3 says why
the strap is safe.

**Review finding m11** asked for `SENS_FLT_N` and `VARIANT_N` to move from
the expander to GPIOs if any were free. None is. They stay on expander bits
P15 and P14, which can be read only while the keep-alive runs, and section
19.6 gives the firmware procedure for reading them between runs.

Qwiic has its own I2C controller, I2C1. A faulty or badly wired expansion board
can then hang only its own bus. The valve expander and the RTC are on I2C0.

### 12.3 Strapping pins

| Pin | Strap function | Needed at reset | What is connected | Why it is safe |
|---|---|---|---|---|
| GPIO0 | Boot mode | High for normal boot | SW2 to `GND`, R19 10k to `3V3`, J8 | Standard boot button. Not on the front panel. |
| GPIO3 | JTAG source | Ignored unless eFuses are burned | `VIN_MON` divider, 10k to `GND` | Reads low, the same as the default floating case would be treated. Analogue input only. |
| GPIO45 | Flash voltage | Low for 3.3 V | `SENS_PWR_EN`: R86, 100 kohm to `GND`, the enable input of U11 and test pad TP25 | Nothing on the net can source current: one pull-down and one CMOS input. With the internal pull-down the strap reads 0. Firmware drives the pin only after boot. Never add a pull-up or a second driver to this net. |
| GPIO46 | Boot mode, ROM messages | Low | R20 and LED D6 to `GND` | The LED path can only pull down. |

### 12.4 Supply, reset and boot

C14, 22 uF, and C15, 100 nF, sit at module pin 2, as in the module datasheet's
reference circuit. R18, 10 kohm, and C16, 1 uF, delay EN by about 10 ms after
`3V3` rises. The datasheet recommends those values. SW1 pulls EN low.

Espressif's hardware design guidelines add a condition, quoted in review A1.
Where the supply rises or falls slowly, is switched often or is unstable,
the RC circuit alone may not meet the reset timing, the chip may fail to
start or a flash erase may fail, and a power monitor chip with a threshold
near 3.0 V should be used. The decision on finding m9 was to fit one unless
the supply ramp made it unnecessary. It does not: C1 holds the logic for
about 100 ms after the 24 VAC goes away (section 4.5), `3V3` then decays as
slowly as the load lets it, and a weak transformer sags on valve inrush.
That is the slow, unstable supply the guideline describes.

U12 is a TI TLV803EA30DBZR, LCSC C5218924, 7160 in stock at JLCPCB on
2026-11-10. Its open-drain output pulls `EN` low while `3V3` is below the
threshold and for 130 to 270 ms after it has recovered. R18 remains the
pull-up, and SW1 and J8 pull the same node low. From the TI data sheet, read
as LCSC's copy:

| | |
|---|---|
| Falling threshold | 3.0 V, 2.94 to 3.06 V over temperature |
| Hysteresis | 1.2 % typical, 1.5 % maximum, so `EN` is released at 3.11 V at most |
| Lowest `3V3` in normal use | 3.20 V, 0.09 V above that |
| Supply current | 250 nA typical |
| Pins, SOT-23 | 1 `GND`, 2 RESET, 3 VDD. The parts with an R or V before the package code have other pinouts; this one has neither. |

The margin between the lowest rail and the highest release threshold is
90 mV, and the part answers a dip of 5 % below its threshold in 10 us. A dip
on `3V3` during a WiFi transmit burst that reaches 3.06 V would reset the
board. I expect dips of tens of millivolts behind 32 uF, and it is a bench
item. The 2.93 V version, TLV803SDBZR, C132016, is the fallback and fits the
same pads.

Flashing over USB needs no extra parts. The ESP32-S3's USB Serial/JTAG
controller resets the chip and enters download mode by itself. For a factory
jig, J8 carries `3V3`, `GND`, TXD0, RXD0, EN and IO0 on 2.54 mm pads.

### 12.5 Layout notes for this block

- **Antenna**: the module's land pattern in datasheet figure 11-1 marks an antenna area of 18 x 6 mm at the top of the module. Put the module on a board edge with that area beyond the edge, or with no copper on any layer under it. Espressif's hardware design guidelines, section 1.4.7, read in review A1, ask for the antenna to be outside the base board with its feed point at the board edge. Where that is not possible the board is to be cut away beside and below the antenna. Inside the housing they recommend at least 15 mm of clearance around the antenna in all directions.
- **Metal near the antenna**: keep C1, K1, the display frame and the terminal blocks at least 15 mm from the antenna, the figure from the guidelines. RF performance is a bench item.
- **USB**: route D+ and D- as a pair from J5 through U2 to module pins 14 and 13. Keep it short and without stubs. This is full-speed USB at 12 Mbit/s.
- **ADC**: C30 and C31 go at module pins 39 and 38. Route `ISENSE_LO` and `ISENSE_HI` away from L1, the display clock and the antenna.
- **Display SPI**: see section 15.3.

## 13. Sensor inputs and sensor supply

### 13.1 Circuit

Three identical inputs: `T_RAIN`, `T_FLOW` and `T_AUX` at the terminals.

<!-- designsync:begin table block="L sensor inputs and sensor supply" -->

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
| D24 | SS34 Schottky 3 A 40 V | SMA | C8678 | A `GND`, K `SP_OUT` | Keeps the switch output above -0.3 V on a negative miswire |
| R83 | 200k 1% | 0603 | C25811 | `SP_ILIM` to `GND` | Current limit 135 mA nominal, 116 to 158 mA |
| R85 | 10k 1% | 0603 | C25804 | `3V3` to `SENS_FLT_N` |  |
| R86 | 100k 1% | 0603 | C25803 | `SENS_PWR_EN` to `GND` | Keeps the switch off, and GPIO45 low, at reset |
| D19 | SS210 Schottky 2 A 100 V | SMA | C14996 | A `SP_OUT`, K `SP_B` | Blocks a positive miswire |
| R84 | 4.7R 1% 0.25 W | 1206 | C17937 | `SP_B` to `SP_A` |  |
| D20 | SS210 Schottky 2 A 100 V | SMA | C14996 | A `GND`, K `SP_A` | Clamps a negative miswire |
| C48 | 10 nF 100 V X7R | 0805 | C18741 | `SP_A` to `GND` |  |
| F3 | PTC 0.2 A hold, 60 V | 1812 | C315900 | `SP_A` to `SENS_5V` |  |
| D18 | TVS 48 V bidirectional 600 W | SMB | C151264 | `SENS_5V` to `GND` | Terminal clamp |

<!-- designsync:end -->

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
`V_BUCK`, so it is off on USB power. GPIO45 switches it.

U11 is a TPS2553 current-limited switch. With R83 = 200 kohm the datasheet
equations (SLVS841, equation 1) give a limit of 135 mA nominal, 116 mA minimum
and 158 mA maximum. Its fault output goes to expander bit P15. The PTC F3
behind it holds 0.20 A at 20 °C but only 0.14 A at 60 °C and 0.12 A at 70 °C,
so in a hot enclosure F3 can trip before U11 limits. Treat 100 mA as the
usable sensor current.

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
half-cycle. Meanwhile U11 limits its own current to at most 158 mA, and R84
with D19 keeps its output pin at about 0 V, inside its -0.3 V limit. That
holds while U11 is on. With the sensor supply switched off, U11 sources
nothing, and D19 pulls its output pin negative for the second or
two that F3 takes to trip. Review A1 found this and recommended a Schottky
from `GND` to `SP_OUT`. D24 is that diode, an SS34 like D3 and D4. It carries
the current that R84 lets through, about (1 V - two diode drops) / 4.7 ohm =
60 to 70 mA, and at that current I estimate its drop at 0.25 V from the
typical curve of a 3 A Schottky. The pin's limit is -0.3 V. So D24 brings the
pin inside its rating with little to spare, and less at -20 °C. It is a
bench item. I chose the SS34 over a smaller diode because its drop at this
current is the lowest of the Schottkys already on the BOM.

**Short to ground.** U11 limits at 116 to 158 mA and reports the fault. F3
holds at 0.2 A at 20 °C and does not trip; at 60 °C and above it may.

## 14. Real-time clock and backup

<!-- designsync:begin table block="I real-time clock" -->

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

<!-- designsync:end -->

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
datasheet. Those figures need the 32 kHz clock output off: the PCF8563 powers
up with it on, which raises the typical current to 550 nA, and firmware rule
11 turns it off. The cell is a BOM line with no LCSC number, section 18.

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
| Controller | ST7789T3 in the table, "ST7789V2+HSD" on the drawing. The sample code at the end of the datasheet is headed ST7735R. Treat the variant as unknown until a panel is read out. |
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
| Other marked figures | 33.6 wide, 43.94 and 46.6 long | Note 8 of the drawing says the "LCD V.A" is the recommended window in the housing. 33.6 x 43.94 is the likely viewing area; the drawing does not label it. |
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

J7 as chosen has its contacts on the bottom, the board side. Its drawing calls
it flip-lock, bottom contact, and asks for a tail 0.3 mm thick with (N+1) x
0.5 mm = 6.5 mm width and at least 3 mm of exposed contact. The panel tail is
0.3 mm thick, 6.5 mm wide, with 3.5 mm contacts.

Review A1 read the panel drawing as an image. The front view, the one that
shows the active area, shows the twelve contact fingers, with pin 1 at the
left when the tail points down. The rear view shows a hatched 5 x 6.5 mm
stiffener in the same place. **So the contacts are on the same face as the
screen.** With the screen facing away from the PCB, a tail that runs straight
into a connector has its contacts facing away from the board and needs a
top-contact connector. A tail folded back under the panel has them facing the
board and suits J7 as chosen. The enclosure concept uses that fold. Section
16.4 fixes the contact side and the position of pin 1 from it, and corrects
a statement about the fold that stood here after the review. **Still check with a
sample before layout.** The drawing is the only evidence.

The panel should sit in a pocket of the enclosure front, not on the PCB. The
tail is 20.7 mm long, so J7 must be within about 15 mm of the panel's FPC edge
once the enclosure is closed.

**Second source.** None confirmed. The 2.4 inch panel from the same maker,
HS24CG003-ARX, has a 16-way FPC with a different pin order and four touch
pins. A second source would need a different J7. That is a supply risk for a
part with 460 in stock.

<!-- designsync:begin table block="J display" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J7 | FPC connector 12-way 0.5 mm, bottom contact, flip lock, 2.0 mm high | SMD | C2856799 | 1 `GND`, 2 `TFT_CS`, 3 `TFT_DC`, 4 `TFT_SCLK`, 5 `TFT_MOSI`, 6 `TFT_RST`, 7 NC, 8 `3V3`, 9 `3V3`, 10 `TFT_LEDA`, 11 `TFT_LEDK`, 12 `GND` | Pin numbers follow the panel FPC |
| DS1 | HS20HS072RX 2.0 inch 240x320 IPS TFT, ST7789, 12-way FPC | panel | C5329582 | FPC NC | Not a schematic symbol; listed for the BOM. Its tail plugs into J7 |
| R60 | 33R 1% 0.25 W | 1206 | C18005 | `V5` to `TFT_LEDA` | Backlight current |
| Q4 | AO3400A N-MOSFET 30 V | SOT-23 | C20917 | G `BL_G`, S `GND`, D `TFT_LEDK` |  |
| R61 | 100R 1% | 0603 | C22775 | `TFT_BL` to `BL_G` |  |
| R62 | 100k 1% | 0603 | C25803 | `BL_G` to `GND` |  |
| R63 | 10k 1% | 0603 | C25804 | `3V3` to `TFT_RST` |  |
| C37 | 1 uF 50 V X5R | 0603 | C15849 | `3V3` to `GND` | At J7 pins 8 and 9 |
| C38 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At J7 pins 8 and 9 |

<!-- designsync:end -->

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
command and GPIO14 is reset. The Sitronix ST7789V datasheet, version 1.3,
gives a minimum serial write cycle of 66 ns with 15 ns high and 15 ns low: a
guaranteed clock of 15 MHz. Start there. Faster clocks are common practice
with these panels and are outside that figure; the panel's exact controller
variant is also not certain. A full frame of 154 kB takes 82 ms at 15 MHz.

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

<!-- designsync:begin table block="K controls" -->

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

<!-- designsync:end -->

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
| J1 | 1 | 24V~ | `AC_L_IN` | Transformer. Becomes `AC_L` through F4. |
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
| J8 | 1x6 pads, 2.54 mm | 1 `3V3`, 2 `GND`, 3 TXD0 through R99, 4 RXD0, 5 EN, 6 IO0 |

The Qwiic supply goes through PTC F2, 0.125 A hold, and the bus lines through
ESD array U9. A short on an expansion cable then cannot hold `3V3` down for
long.

**The Qwiic connector supplies 80 mA.** F2 holds 0.125 A at 20 °C and 0.08 A
at 60 °C, and has up to 6 ohm, which drops 0.3 V at 50 mA. Review A1, finding
m14, asked for the budget to say 80 mA and not 125 mA. Section 4.2 does, and
the user documentation must. At 80 mA the expansion board sees 2.8 V at
worst.

R99, 0 ohm, is in series with TXD0 between the module and J8. Review A1,
finding m12, quotes Espressif's guidelines for reserving it. Its value is for
EMC work on the built board.

### 16.3 Test pads

<!-- designsync:begin testpads -->

| Pad | Net | Pad | Net | Pad | Net |
|---|---|---|---|---|---|
| TP1 | `VIN_DC` | TP2 | `V_BUCK` | TP3 | `V5` |
| TP4 | `3V3` | TP5 | `GND` | TP6 | `GND` |
| TP7 | `V_DRV` | TP8 | `KA_N2` | TP9 | `KA_OK` |
| TP10 | `OC_TRIP_N` | TP11 | `FAULT` | TP12 | `DRV_EN` |
| TP13 | `ISENSE_LO` | TP14 | `ISENSE_HI` | TP15 | `VB` |
| TP16 | `AC_ZC` | TP17 | `ISNS` | TP18 | `I2C0_SDA` |
| TP19 | `I2C0_SCL` | TP20 | `PGOOD` | TP21 | `EXP_INT_N` |
| TP22 | `COM` | TP23 | `RTC_INT_N` | TP24 | `RLY_B` |
| TP25 | `SENS_PWR_EN` | TP26 | `P06` | TP27 | `P07` |
| TP28 | `P16` | TP29 | `P17` | TP30 | `SENS_5V` |

<!-- designsync:end -->

### 16.4 Agreement with the enclosure concept

The enclosure work in pull request 24 fixes a mechanical contract in
`hardware/mechanical.yaml` and `docs/design/mechanical.md`. That pull request
is not merged; I read it at commit 2f7e375. This is how the circuit design
stands against it.

**Display connector J7, resolved.** The contract mounts the panel on a
carrier above the board, landscape, with its FPC edge towards +x. The tail
makes a C-fold under the panel and enters J7 travelling in -x. J7 is at
x = 60.5, y = 78.43, rotation 0, mouth at x = 63.0.

- **Contact side.** The panel's contacts are on the display face. After the fold that face looks at the board. J7 is the bottom-contact part, FPC-05F-12PH20, as chosen. Consistent.
- **Pin 1.** The contract puts panel pin 1 at y = 75.68, the contact nearest the terminal-block edge, and pin 12 at y = 81.18. In the netlist J7 pin 1 is panel pin 1, `GND`. So the footprint's pad 1 is the pad at y = 75.68: **the left end of the connector when you look into its mouth with the board component side up.** The XUNPU drawing does not number the contacts on the connector body, so the footprint numbering is ours to set, and this sets it.
- **A correction to section 15.1 as the review left it.** It said the fold reverses the order of the pins as the board sees them. It does not. A fold about an axis parallel to the row of contacts leaves every contact at the same position along the row. What changes is the direction the connector's mouth faces, and so which end of the connector pin 1 is at.
- **Body size.** The contract assumes a body of 5.0 x 9.6 mm. The XUNPU drawing gives (N - 1) x 0.5 + 4.9 = 10.4 mm along the contact row for 12 ways, 5.12 mm deep and 2.0 mm high. It is 0.8 mm longer than assumed and still inside the 9 x 23 mm cut-out in the carrier.

Both points rest on the panel drawing. Check them on a sample panel before
layout, as section 15.1 says.

**Terminal edge.** The contract's order along the edge is J5, J1, J2, J3, J4,
J6. Sections 16.1 and 16.2 agree with it: the four terminal blocks are in
the same order with the same pin order, and the contract adds the USB socket
at the left end and the Qwiic socket at the right. No conflict.

**Points the enclosure owner needs to know.**

| Item | Contract | Circuit design | What to do |
|---|---|---|---|
| Fuse holders | One holder is assumed, under 9 mm, height not read | Two now, F1 and F4, 9.7 x 5 mm each. The Littelfuse sheet I have gives the pad layout, and I could not read a height from its text. | Both must be reachable with the wiring cover off, next to J1. Read the height from the drawing at capture. |
| C1 | Bottom side, 16.5 mm against 17.0 mm allowed | An SMD can. On the bottom it is the only SMD part there. | Either pay for a second assembly side for one part, or fit C1 by hand. A through-hole capacitor does not help: a 12.5 x 16 mm one standing is 16 mm plus its length tolerance, which I did not read, and lying down it is 12.5 mm high, over the 9 mm allowed on top. |
| K1 | Bottom side, 15.3 mm | Through-hole, so it is inserted from the bottom and soldered from the top, by hand or selectively, after the SMD parts. | Ask the assembler before layout. I found no relay of this rating under 9 mm. |
| USB socket beside J1 | Flagged in the mechanical design as a problem | Electrically harmless. The shell is at `GND` through R17 and J1 pin 1 is at most 45 V away. | Keep 2 mm between the shell and the J1 pads. |
| Fault LED D8 | No light pipe or opening | D8 shows the over-current latch and is a bring-up aid. Firmware reads the same latch on GPIO40 and shows it on the display. | None needed. |
| Enclosure temperature | Estimates 6 K at 1 W, 11 K at 2 W and 16 K at 3 W with vents | This document now designs to 70 °C inside, section 3.3. | Keep the vents. A closed box would need the estimate redone. |
| New parts since the contract was written | | F4, U12, D22 to D24, RN1 to RN3 and about twenty small passives. None is taller than 2.5 mm apart from F4. | Area only. |

## 17. The 4-zone variant

The 4-zone build uses the same PCB. These parts are not fitted:

- **Channels 5 to 8**: every designator from 501 to 805. For each of the four channels that is F, D, Q, C and U number 01, D number 02, and R numbers 01 to 05. Eleven parts per channel, 44 in all.
- **J3** and its plug.
- **R28**, the variant strap.

Everything else is fitted, including U5 and both of its ports. With R28
absent, expander bit P14 reads high and firmware knows it has four zones. The
unused expander pins P10 to P13 stay inputs. RN2 is fitted on both builds and
holds them at `V_DRV`.

## 18. Bill of materials and cost estimate

Each line of `hardware/bom-preliminary.csv` gives the
reference designators, quantities for both builds, value, package,
manufacturer, part number, LCSC number, JLCPCB library class, stock, price at
10, 100 and 1000 pieces, the LCSC page, the datasheet link and the date
checked. The `datasheet` column says `read` where I opened the datasheet and
took ratings from it, and `listing only` where I relied on the distributor's
listing. Section 22 says which kinds of part are `listing only`.

Every LCSC number was confirmed on 2026-11-10 against the JLCPCB parts search,
which returns the manufacturer, part number and package for a number. Stock is
JLCPCB assembly stock on that date. The reference and quantity columns are
written from the netlist by `tools/designsync`, and so is everything between
the two comment lines below.

**Parts cost per board. This is an estimate.** It is the sum over the lines of
quantity times a price. The price is the one in the BOM for the largest of
10, 100 and 1000 pieces that the order of that line reaches: nine triacs on
100 boards are 900 pieces and take the 100-piece price. JLCPCB has more price
breaks than those three, so its own quote will be a little lower. The
estimate leaves out the PCB, assembly, the fee JLCPCB charges per extended
part type, shipping, duty, the coin cell and the enclosure.

<!-- designsync:begin cost -->

`hardware/bom-preliminary.csv` has 96 lines: 46 basic parts, 49 extended parts and 1 that JLCPCB does not fit.

| Build | 10 boards | 100 boards |
|---|---|---|
| 8-zone | 40.65 USD | 31.84 USD |
| 4-zone | 34.37 USD | 27.04 USD |

Not in the totals, because the line has no price: CR2032 lithium coin cell, 3 V.

The six most expensive lines on an 8-zone board at the 100-board price:

| Line | Quantity | USD per board |
|---|---|---|
| T810-600B-TR | 9 | 4.11 |
| ESP32-S3-WROOM-1-N16R8 | 1 | 3.75 |
| MOC3023SR2M | 9 | 3.18 |
| HS20HS072RX | 1 | 2.41 |
| 0154003.DRT | 2 | 2.38 |
| EC11J1525402 | 1 | 1.80 |

Lines with the least stock against a 100-board order of the 8-zone build:

| Part | Stock on the date checked | Needed for 100 boards |
|---|---|---|
| T810-600B-TR | 3384 | 900 |
| SMBJ48CA | 5665 | 1400 |
| HS20HS072RX | 460 | 100 |
| KF2EDGR-3.81-7P | 1806 | 200 |
| KF2EDGK-3.81-7P | 1827 | 200 |
| VZH471M1JTR-1616 | 968 | 100 |
| 0154003.DRT | 2514 | 200 |
| EC11J1525402 | 1486 | 100 |

<!-- designsync:end -->

**What the review changes cost.** By this method the BOM as it stood before
the changes comes to 38.90 and 30.35 USD for the 8-zone board at 10 and 100
boards. The first draft quoted 37.54 and 28.07 USD, from JLCPCB's full price
breaks. So the changes add 1.49 USD to an 8-zone board at 100 boards:

| Change | USD per 8-zone board at 100 boards |
|---|---|
| Second fuse and holder, F4 | +1.19 |
| Nine T810 triacs in place of T410 | +0.24 |
| Supervisor U12 | +0.24 |
| 74HC132 in place of 74HC00 | +0.11 |
| TVS D23 on `COM` | +0.11 |
| Surge-rated R1 | +0.09 |
| Second pump, read-back resistors, arrays, series footprints, D24 | +0.14 |
| Nine 40 A PTCs in place of the 20 A parts | -0.63 |

The CR2032 cell is on the BOM as a line with no LCSC number, marked as not
fitted by JLCPCB. Review A1 noted that the rule "an LCSC number on every
line" had a gap there. The JLCPCB parts search on 2026-11-10 returns holders
and contacts for "CR2032" and no cell, so the gap is now stated on the line
and not closed.

## 19. What firmware must do

These follow from the circuit. `hardware/pinmap.yaml` carries the pin-level
detail. Sections 19.1 to 19.5 keep their numbers from before the review,
because other documents refer to them. Sections 19.6 to 19.9 are new and
give the exact procedures.

### 19.1 Rules

1. **Keep-alive.** Follow the contract in section 19.4. In short: GPIO38 is a plain GPIO output, every edge is decided by code that has checked the control-loop heartbeat, one edge every 5 ms while outputs are wanted, low otherwise.
2. **Self-test.** Run the sequence of section 19.7 at start-up, before every run and after every run. On a failure, write every expander pin back to input if the expander answers, hold the keep-alive low, raise a fault and refuse to run valves until a person has acknowledged it.
3. **Expander.** Follow the register sequence of section 19.7. A valve bit is on when it is an output and low. Never more than three valve bits low. Read back after every write and once per control tick. A mismatch, a missing acknowledge or a stuck bus means stop the keep-alive.
4. **Relay sequencing.** `RLY_ON` high 20 ms before the first valve, low 40 ms after the last.
5. **Over-current latch.** GPIO40 low means the hardware has cut drive. Follow section 19.8. There is no release pin.
6. **Unexpected current.** Current above the idle threshold with the relay closed and no valve bit low means a shorted output device. Open the relay and stop.
7. **PSRAM ECC.** Enable it. The design temperature inside the enclosure is 70 °C, the R8 module is rated to 65 °C without ECC and 85 °C with it, so this is no longer optional. It costs one sixteenth of the PSRAM. In ESP-IDF 5 the option is `CONFIG_SPIRAM_ECC_ENABLE`; check the name against the IDF version in use.
8. **Flash.** A 4 kB sector erase stalls both cores for up to about 400 ms. The keep-alive interrupt runs through it; the control loop does not. Keep 64 kB block erases, which can take 2 s, away from watering.
9. **Backlight.** PWM on GPIO21 between 1 and 20 kHz. Dim and time out.
10. **Stagger starts.** Never switch two outputs on within 100 ms of each other. The hardware trip can be as low as 4.0 A peak, and two valves holding 0.5 A with a third drawing 1.5 A of inrush already reach 3.5 A peak.
11. **RTC clock output.** Once at start-up, write 0x00 to PCF8563 register 0x0D. The chip powers up with its 32 kHz output enabled, which doubles its back-up current. Section 14.
12. **Read-back pins.** GPIO7 and GPIO42 are inputs for the life of the firmware. Enable the internal pull-down on GPIO42. Enable no pull on GPIO7. Never configure either as an output.
13. **Sensor supply enable.** GPIO45 is a strapping pin. Leave it alone until the application runs, then drive it high to turn the sensor supply on. Never enable its pull-up.
14. **Weld check.** Before every run, section 19.7 step 5.

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
- **Idle threshold**: 30 mA rms on `ISENSE_HI` as a starting value. The smallest load in the brief is 50 mA, and a shorted output device passes a valve's full current. Set the final value from the noise floor measured at bring-up.
- **Not an average.** The reading is the rms of the waveform. Averaging the samples gives the bias, 790 mV, whatever the current.

### 19.3 Answers to the firmware team's open questions

The numbers refer to section 9 of the firmware architecture document as it
stands on `main` after the phase 2 firmware work, which has fourteen
questions.

| No. | Question | Answer |
|---|---|---|
| 1 | Pin map | `hardware/pinmap.yaml`. Valve outputs are expander bits, not GPIOs. |
| 2 | Keep-alive edge rate and hold time | Section 19.4. One edge at least every 20 ms; the firmware's 5 ms is right. Gate drive is gone 0.45 to 0.9 s after the last edge and the relay has opened within 0.95 s. Flash sector erase up to about 400 ms. |
| 3 | Pull-downs on outputs and OE | There are no output GPIOs. `KA` has a 10 kohm pull-down. The expander resets to inputs, and resistors hold every one of its pins at its off level. |
| 4 | Current-sense front end | 0.1 ohm shunt, two AC-coupled-by-arithmetic channels biased at 0.79 V. Section 19.2. Trip flag on GPIO40. |
| 5 | 24 VAC present | GPIO41, a mains-frequency square wave, not a level. Section 19.9. |
| 6 | Field input polarity | Contact closed reads low. External pull-ups fitted. Flow passes 200 Hz. |
| 7 | RTC | PCF8563 at 0x51, as assumed. CR2032, about ten years. Rule 11. |
| 8 | Display | HS20HS072RX, 240 x 320. The enclosure concept mounts it landscape with the tail towards the encoder. Chip select is on GPIO10 and reset on GPIO14 with a 10 kohm pull-up. Neither is tied off on the board, so firmware drives both. Data/command GPIO13, clock GPIO12, data GPIO11. Backlight GPIO21, high is on, 1 to 20 kHz. Back is GPIO18 and Manual/Home GPIO39, both low when pressed. The encoder has 30 detents and 15 pulses per turn: two quadrature edges per detent. |
| 9 | GPIO15 and 16 | No 32 kHz crystal on the module pins. They carry the encoder. I2C0 is on GPIO8 and 9. |
| 10 | Current windows | 80 to 700 mA per output is consistent with the hardware. A 3000 mA rms short limit is close to the hardware trip of 4.6 A peak, which is 3.3 A rms for a sine. The hardware will usually act first. |
| 11 | Output LEDs | They follow the real drive in hardware. Firmware drives only the status LED on GPIO46. |
| 12 | Hardware over-current trip | Status on GPIO40, low when tripped, driven by a logic gate through 10 kohm: not open drain, no pull-up wanted. There is no release pin and no release pulse. Section 19.8. It trips at 4.6 A peak nominal, 4.0 to 5.4 A with every tolerance stacked, and removes drive from every output. |
| 13 | Zero-cross detector | GPIO41. Not one pulse per crossing. Section 19.9. |
| 14 | Pin budget | Settled by the expander. The display has its own five pins, the trip and zero-cross inputs are on GPIO40 and GPIO41, and GPIO42 and GPIO7 read back the two keep-alive detectors. No pin is spare. The shared-pin build option can go. |

### 19.4 Keep-alive contract agreed with the firmware

Review A1 compared section 8 with the merged firmware (`OutputEnableGuard`,
firmware architecture section 3). The first draft of this document said the
keep-alive must never come from a timer. The firmware makes its edges in a
GPTimer alarm interrupt. The two are consistent, for this reason: the timer
does not make the waveform. Its interrupt handler does, one edge at a time,
and only while a heartbeat counter that the control loop increments has moved
within the last 500 ms. A stuck control loop, a stopped timer and a halted
CPU all end the edges. What the hardware rule forbids is a peripheral that
keeps toggling the pin with no code deciding each edge.

The contract is as the review agreed it. The second detector changes four
rows, marked "changed", and none of them asks more of the firmware's
keep-alive code.

| Item | Agreed |
|---|---|
| Pin | GPIO38 as a plain GPIO output, idle low. No peripheral signal may be routed to it through the GPIO matrix. |
| Who makes an edge | Code that has just checked the control-loop heartbeat. Today: the 5 ms GPTimer alarm interrupt calling `OutputEnableGuard::step()`. Forbidden: LEDC, RMT, MCPWM, a timer with a pin output, any other self-running source. |
| Edge interval | 5 ms. The hardware needs an edge at least every 20 ms. Both detectors are designed and simulated at that limit. |
| When it runs | Only while an output is wanted and no fault is latched, and for the short test and polling cycles of sections 19.6 and 19.7. It is low between runs. |
| Enable delay, changed | `DRV_EN` is high 14 to 41 ms after the first edge at 100 Hz, and the relay transistor Q2 is on after 22 to 82 ms. Firmware waits until both read-backs agree and the expander acknowledges, 150 ms at most, before it writes the expander. |
| After every start | The expander has been in reset. Section 19.7 gives the register sequence. |
| Stopping, changed | Firmware drives the pin low. Gate drive is gone 0.45 to 0.9 s after the last edge, 1.2 s if a fault leaves the pin high. The relay transistor has let go between 0.12 and 0.95 s after the last edge, 1.4 s if the pin is high. |
| From a dead control loop to no drive, changed | Heartbeat timeout 500 ms, plus one 5 ms period, plus the hold-up: 1.4 s at most to no gate drive, and 1.5 s to an open relay. With the pin left high, 1.7 s and 1.9 s. The specification's "about one second" counts from the last edge and is met. |
| Flash | A 4 kB sector erase, up to about 400 ms, plus one 50 ms control period stays inside the 500 ms heartbeat timeout by 50 ms. Do not lower the timeout. Do not run 64 kB block erases while a valve is on. |
| Clearing the over-current latch | Hold the keep-alive low for 1.5 s. This also resets the expander. Section 19.8. |
| Self-test, changed | Section 19.7: before every run, at start-up and after every run. It now reads GPIO42 and GPIO7 and times both detectors. |
| What the circuit does not check | The rate. Any toggling from about 3 Hz to at least 1 MHz enables drive. Correct timing is the firmware's job. |

### 19.5 Open points between hardware and firmware

1. **Firmware still drives valves as GPIOs.** The merged firmware has no expander or relay driver, expects a release pulse for the over-current latch, expects one pulse per zero crossing, and averages the current reading. Sections 19.1 to 19.9 are the hardware side of each change. The board header must be regenerated from `hardware/pinmap.yaml`.
2. **The self-test is new work.** Nothing in the merged firmware reads GPIO42 or GPIO7, times the detectors or does the weld check. Section 19.7.
3. **PSRAM ECC.** `sdkconfig.defaults` on `main` does not enable it. Rule 7. It is now required.
4. **Heartbeat timeout against erase time.** The 50 ms of margin in section 19.4 rests on one flash data sheet, and Espressif does not fix the flash part. Measure the longest control-loop stall on real modules.
5. **Sensor supply enable moved.** It is on GPIO45, not GPIO7. Rule 13.
6. **Limits in section 19.7 are from simulation.** The pass windows for the two release times, 0.3 to 1.0 s and 0.08 to 1.0 s, come from tolerance corners of generic models. Measure both on the first boards, at -20 and 50 °C, before the limits go into a release.

### 19.6 Expander inputs between runs

`VARIANT_N`, P14, and `SENS_FLT_N`, P15, are inputs of the expander. The
expander is in reset whenever the keep-alive is stopped, so they can be read
only while it runs. Review finding m11 asked for GPIOs. None is free
(section 12.2), so firmware reads them like this:

- **`VARIANT_N`** is a strap. Read it once, in the start-up self-test, from input port 1 bit 4. Low is the 8-zone build.
- **`SENS_FLT_N`** during a run: read input port 1 bit 5 with the read-back of every control tick. Low means U11 is limiting or too hot.
- **`SENS_FLT_N`** between runs, while the sensor supply is on: every 10 minutes, and at once when flow pulses stop or start in a way the schedule does not explain, run a polling cycle. That is steps 1, 3 and 4a of section 19.7, then a read of input port 1, then step 9. No valve bit and no relay bit is touched. The cycle takes about 1.2 s. K1 does not move, because Q1 stays off.

The polling cycle is also a test of both detectors, so a controller that does
not water for a month still tests them every 10 minutes. Leave 2 s between
the end of a polling cycle and the start of a run, so that both detectors
have let go before the idle test of the run.

### 19.7 Self-test and run sequence

Names: `KA` is GPIO38. `A_RB` is GPIO42, high when gate drive is enabled.
`B_RB` is GPIO7, high when the relay transistor Q2 is off. `OC` is GPIO40,
low when the over-current latch is set. The expander is at 0x74 on I2C0 at
400 kHz.

TCA9539 registers, from TI SCPS202C:

| Register | Address | After reset |
|---|---|---|
| Input port 0, 1 | 0x00, 0x01 | Pin levels |
| Output port 0, 1 | 0x02, 0x03 | 0xFF |
| Polarity inversion 0, 1 | 0x04, 0x05 | 0x00 |
| Configuration 0, 1 | 0x06, 0x07 | 0xFF, all inputs |

Port 0: bits 0 to 3 are Z1 to Z4, bit 4 is MV, bit 5 is `RLY_ON`, bits 6 and
7 are spare. Port 1: bits 0 to 3 are Z5 to Z8, bit 4 is `VARIANT_N`, bit 5 is
`SENS_FLT_N`, bits 6 and 7 are spare. A valve is on when its bit is an output
and low. `RLY_ON` is on when high.

**The sequence.** Every limit is in the step. A step that fails ends the
sequence in the fault state at the end of this section.

| Step | When | Do | Pass |
|---|---|---|---|
| 0 | Always | Check the preconditions: `AC_ZC` has had an edge in the last 25 ms, `KA` has been low for at least 2.0 s, `OC` is high. | If there is no 24 VAC, do not run and do not test: `B_RB` reads low without it. |
| 1 | Idle test, always | Read `A_RB` and `B_RB`. Address the expander. | `A_RB` low. `B_RB` high. No acknowledge. |
| 2 | Pin-high test: at start-up and at least once every 24 h. Before every run is better and costs 1.1 s. | Drive `KA` high and hold it. After 1.0 s read `A_RB` and `B_RB`. Drive `KA` low and wait 100 ms. | `A_RB` low. `B_RB` high. A high `A_RB` is a shorted C22. A low `B_RB` is a shorted C51 or an open bleed resistor. |
| 3 | Start, always | Toggle `KA` every 5 ms. Poll `A_RB`, `B_RB` and the expander every 5 ms. | Within 150 ms: `A_RB` high, `B_RB` low, acknowledge. |
| 4a | Expander, always | Read input ports 0 and 1. | Port 0 reads 0x1F: valve pins high, `RLY_ON` and spares low. Port 1 reads 0x2F on an 8-zone board and 0x3F on a 4-zone board. Bit 5 low instead means a sensor supply fault, which is reported and is not a reason to stop. |
| 4b | | Write output port 0 = 0xDF and output port 1 = 0xFF. Read both back. | Equal to what was written. |
| 4c | | Write configuration 0 = 0xC0. Write configuration 1 = 0xF0 on the 8-zone build, 0xFF on the 4-zone build. Read both back, and read polarity 0 and 1. | Configuration as written. Polarity 0x00. |
| 5 | Weld check, before every run | With `RLY_ON` still low, turn on the first output of the run by clearing its bit. Measure one 100 ms window. Set the bit again. | Current under the idle threshold. More means a welded K1 contact, a bridged JP1 or a shorted Q1. |
| 6 | Relay | Set `RLY_ON`. Read input port 0. Wait 20 ms. Measure one 100 ms window with every valve bit high. | Bit 5 of the input port is high. Current under the idle threshold. More means a shorted triac, TVS, opto-triac or snubber capacitor: clear `RLY_ON` and go to the fault state. |
| 7 | Run | Clear valve bits as the schedule asks, at least 100 ms apart, at most three low. After every write read the output port and the input port back. Every control tick, 50 ms, read input, output and configuration ports of both ports, `A_RB`, `B_RB` and `OC`. | Output and configuration ports equal the shadow copy. Valve bits of the input ports equal the output ports. `A_RB` high, `B_RB` low, `OC` high. |
| 8 | End of run | Set every valve bit. Wait 40 ms. Clear `RLY_ON`. Write configuration 0 and 1 = 0xFF. | Acknowledged. |
| 9 | Stop test, always | Drive `KA` low and start a timer. Sample `A_RB` and `B_RB` every 5 ms for up to 3 s. tA is the time until `A_RB` is low, tB the time until `B_RB` is high. Then address the expander. | 0.3 s <= tA <= 1.0 s. 0.08 s <= tB <= 1.0 s. No acknowledge. |

Note to step 4a. These values are the resistors of section 8.2 showing
through: every pin is an input at this point. On port 1, bits 0 to 3 must be
high and bits 6 and 7 low on both builds; anything else is a hardware fault.
Bit 4 tells the build and bit 5 the state of the sensor supply.

**What a healthy board gives in step 9**, from the simulations: tA 0.45 to
0.9 s and tB 0.12 to 0.95 s. The pass limits are wider at the short end,
where nothing is at risk, and tight at 1.0 s. An open bleed resistor shows
as tA near 1.7 s or tB near 1.45 s. A detector that is stuck on never
releases, and the 3 s timeout ends the wait.

**Start-up self-test.** Steps 0 to 4c, then 8 and 9, with no valve and no
relay. Read the build variant in step 4a. Without 24 VAC, on USB alone, skip
it and refuse runs.

**The fault state.** Drive `KA` low. If the expander still acknowledges,
which means detector A is stuck on, write configuration 0 and 1 = 0xFF so
that no valve bit and no relay bit is driven, and repeat that write every
control tick. Show the fault and its step number on the display and report
it. Do not start a run until a person has acknowledged the fault. A fault
from step 1, 2 or 9 means one of the two detectors can no longer be relied
on: the board needs repair, and acknowledging it must not be a one-press
action.

**After an over-current trip** the sequence is the same. Step 5 then also
tells whether K1 survived breaking the fault current.

### 19.8 Over-current latch: read and clear

- **Read.** `OC`, GPIO40, is low while the latch is set. Read it every control tick, or take an interrupt on the falling edge. When it is low the hardware has already removed gate drive and released K1: `A_RB` is low and the expander does not answer.
- **Do not confuse it with a stopped keep-alive.** `A_RB` low with `OC` high while the keep-alive runs is a fault in detector A, not a trip.
- **On a trip.** Drive `KA` low. Mark the output that was switched on last as faulted, if it was switched on within the last 500 ms; otherwise mark every output that was on as suspect. Record the last current window.
- **Clear.** Keep `KA` low for 1.5 s. The latch clears when detector A lets go, 0.45 to 0.9 s after the last edge. Then read `OC`: it must be high. If it is still low after 3 s, the latch or detector A is faulty: go to the fault state of section 19.7.
- **Restart.** Run the sequence of section 19.7 from step 0 without the faulted output. Allow at most three trips per output per day before the output is disabled until a person clears it.
- **There is no release pin and no release pulse.** Nothing the firmware writes clears the latch except stopping the keep-alive.

### 19.9 Zero-cross signal

`AC_ZC`, GPIO41, is a square wave at mains frequency. It is not a pulse at
each zero crossing.

| Property | Value |
|---|---|
| High | While `AC_L` is more than about 2.3 V above `GND`: the half-cycle in which the valve common is positive |
| Rising edge | 0.30 ms after the upward zero crossing, at 24 VAC |
| Falling edge | 0.09 ms before the downward zero crossing, at 24 VAC |
| Edges per mains period | One rising, one falling |
| Mains frequency | The reciprocal of the time between two rising edges |
| Loss of 24 VAC | No edge of either polarity for 25 ms |
| Source | `hardware/sim/zerocross.cir`. Both offsets grow at lower supply voltage and shrink at higher. |

Firmware needs both edges and a separate offset for each if it wants the
crossings themselves. For the current measurement it needs only the
frequency, to check that a 100 ms window holds whole cycles. `AC_ZC` is taken
after fuse F4, so a blown F4 reads as loss of 24 VAC. A reading of 100 Hz, or
of edges that follow the keep-alive, means `AC_ZC` is bridged to `KA`
(section 8.5).

## 20. Design risk register

Everything here needs a bench. "Assumed" is what this document took as true to
get its numbers.

| No. | Risk | Assumed | Bench test |
|---|---|---|---|
| 1 | Triac junction temperature in the closed enclosure | 70 °C air inside at 50 °C outside, 70 K/W | Two zones and master at 0.5 A for 2 h in the enclosure in a 50 °C chamber. Thermocouples on the tabs. Junction under 110 °C. |
| 2 | Module temperature | Same 70 °C air. The limit is 85 °C with PSRAM ECC on, which rule 7 requires. | Same run, thermocouple on the module can, ECC enabled. Under 85 °C. |
| 3 | LDO temperature | 0.27 W typical, 120 K/W | Same run. Also with an 80 mA load on Qwiic. |
| 4 | The output survives a dead short | Transformer impedance of 69 / VA ohm. PTC trip time scaled with the inverse square of the current from one data sheet point. Triac surge curve for a junction that starts at 25 °C, used for one that starts at up to 100 °C. | Short an output 20 times on 20, 40 and 75 VA transformers, cold and after an hour at 0.5 A at 50 °C. Then repeat with the comparator disabled and K1 bridged, so that only the PTC ends the fault. Record `ISNS` and the K1 contact voltage. Count the half-cycles. The triac must still block and switch, the PTC must return below 0.41 ohm, and K1 must pass the weld check. Note which runs open a fuse. |
| 5 | Over-current latch trips on noise | 47 us filter is enough | Switch inductive loads and apply fast transients. Count false trips. |
| 6 | Surge immunity of outputs, input and sensors | None. The TVS ratings are the only basis. | Combination-wave surge on valve, power and sensor lines at increasing levels until a part fails. Record the level. |
| 7 | Firing and holding with small loads | T810 latching figures, the same as the T410's | Drive a 50 mA relay coil and a 150 mA valve at -20 °C and 50 °C. Check for chatter and dropout. |
| 8 | Commutation with real solenoids | 0.13 to 0.25 H, 35 ohm | Scope T2 at turn-off for several valve makes. No re-firing after the gate is removed. |
| 9 | PTC trips on inrush when hot | Trip takes seconds at the trip current | 1.5 A for 100 ms, repeated, at 70 °C. |
| 10 | Keep-alive thresholds and hold-up, both detectors | Simulation with generic models. Schottky leakage when hot is a model, not a data sheet limit. AO3400A threshold drift of -3 mV/K is assumed. | Measure enable delay and release time of `DRV_EN` and of the drain of Q2 at 25 Hz, 100 Hz and 1 kHz, with the pin stopped low and high, at -20, 25 and 70 °C, on at least five boards. Compare with sections 7.3 and 8.4, and set the firmware limits of section 19.7 from the result. |
| 11 | Buck regulator at light load and low input | Datasheet ripple design | Output ripple and stability from 9 to 45 V input, 0.05 to 0.8 A. Start-up into a sagging transformer. |
| 12 | ADC accuracy and noise with WiFi active | 2 mV rms noise, 1.5 % gain error | Compare readings with a true-rms meter from 50 mA to 2.5 A while WiFi transmits. |
| 13 | Half-wave rectifier loads the transformer with DC | 0.06 A average is harmless | Transformer temperature and primary current with and without the board, on a 20 VA unit. |
| 14 | RF range and emissions | Module approval carries over | Range test in the enclosure with the display fitted. Pre-compliance scan for the buck, SPI and backlight PWM. |
| 15 | Display fit and supply | Dimensions read from a drawing. 3.4 V is acceptable on a 3.3 V recommended pin. | Measure a sample. Check the FPC contact side. Run at 3.4 V. |
| 16 | Relay | Must-operate at 75 % of rated at 23 °C, which leaves 11 % with a hot coil on `V_BUCK`. Contacts do not break current in normal use. They break valve current when the keep-alive stops and the relay detector lets go first, and fault current after an over-current trip, section 7.2. | Operate at `V_BUCK` = 5.0 V after an hour energised at 70 °C. Open under a 1.5 A inductive load 1000 times. Open under the short-circuit current of 40 VA and 75 VA transformers 20 times each, and run the weld check after every one. |
| 17 | Fuses and USB ground faults | 8 A fault, 28 AWG ground | Not a test to run on a good computer. Check fuse opening time at 8 A on the bench. |
| 18 | Sensor miswire heating | 0.11 W in a 1206 | 32 VAC on each sensor terminal for an hour. Part temperatures. |
| 19 | Bulk capacitor ripple and life | 63 V part has the ripple rating of the 50 V part | Capacitor case temperature at peak load and low line. |
| 20 | Output LED brightness | 1.6 mA is visible through a light pipe | Look at it in the enclosure. |
| 21 | Regulator over-voltage | None. If the switch of U1 shorts, `V_BUCK` reaches `VIN_DC` and the logic is destroyed. Section 8.5 argues that K1 still opens and cannot prove it. | Decide whether revision A gets an over-voltage clamp or crowbar on `V_BUCK`. On a sacrificial board, short SW to VIN with a valve running and record what the valve does. |
| 22 | Gate drive of Q2 | 2.09 V at the worst corner against a 1.59 V threshold. No on-resistance is guaranteed below 2.5 V. | Drain voltage of Q2 with K1 energised at -20 °C, with the keep-alive slowed to one edge per 20 ms. Under 50 mV. |
| 23 | Single-fault behaviour of the two detectors | Section 8.5, on paper and in simulation | Fit each fault marked "stuck on" in section 8.5 by hand, one at a time: lift R31 with drive enabled, lift R34, bridge C22, bridge C51, tie the gate of Q2 to its drain, bridge K1. For each, halt the CPU with a valve running and with the pin low and high. The valve must close within the time the table gives, and the self-test must report the fault at the next start. |
| 24 | Supervisor resets on supply dips | Dips of tens of millivolts on `3V3` | Scope `3V3` and `EN` during WiFi transmit bursts at 21.6 VAC and on USB at 4.4 V. No reset. |
| 25 | Negative miswire on the sensor supply terminal | D24 holds the output pin of U11 at about -0.25 V; the limit is -0.3 V | -32 VAC peak on the 5V terminal with the supply off, at -20 and 25 °C. Scope `SP_OUT`. |
| 26 | R1 on repeated power-up | 237 to 385 W for about 1 ms, inside the Yageo single-pulse curve by 1.9 to 3 | 1000 power-ups at the peak of 32 VAC from a 75 VA transformer with C1 discharged. Resistance change under 1 %. |
| 27 | A bridge from `KA` to another net | Layout rules of section 8.5 | Inspect the bare board and the assembled board under magnification around module pin 31, R29 and R89. Measure resistance from `KA` to `TFT_BL` and `AC_ZC`. |

## 21. Bring-up test plan

For the first prototype. Stop at the first step that fails.

1. **Before power.** Inspect. Measure resistance from `VIN_DC`, `V5`, `3V3` and `V_DRV` to `GND`. No cell in BT1, no display, no valve wiring.
2. **USB only.** Feed 5 V from a bench supply limited to 200 mA into VBUS. Expect `V5` 4.4 to 4.9 V, `3V3` 3.20 to 3.40 V, under 80 mA with a blank module. Lower the supply slowly: `EN` must fall when `3V3` passes 2.94 to 3.06 V and rise 130 to 270 ms after it recovers. Then use a USB port and read the ROM boot log. GPIO45 must read under 0.3 V during reset.
3. **Fail-safe at rest.** `KA_OK` low, `DRV_EN` low, `V_DRV` at 0 V, the gate of Q2 at 0 V, K1 open, no I2C answer at 0x74. The RTC at 0x51 answers.
4. **Keep-alive.** Test firmware toggles `KA` at 100 Hz. `KA_N2` rises to about 2.9 V and `DRV_EN` goes high within 20 ms. The gate of Q2, probed at C50, rises to about 2.7 V within 50 ms. Stop toggling and time both falls on a scope: `DRV_EN` after 0.45 to 0.9 s, the gate through 1.5 V and 0.6 V between 0.12 and 0.95 s. Repeat stuck high, where `DRV_EN` must fall within 1.2 s and the gate within 1.4 s, and at 25 Hz. Then try 1 Hz, which must not enable, and 2 Hz, 1 kHz and 100 kHz, and record what each does to both.
5. **Expander and LEDs.** Turn each output on in turn. Its LED lights. The voltage across R101 is about 1.8 V, 12 mA. `V_DRV` falls when the keep-alive stops, and the expander stops answering.
6. **Current channels.** Pass a known 50 or 60 Hz current through `ISNS` to `GND` from a low-voltage source. Check 133 mV/A and 665 mV/A and the 0.79 V bias.
7. **Over-current latch.** Raise that current. `FAULT` sets between 4.0 and 5.4 A peak, D8 lights, `DRV_EN` falls. It stays set until the keep-alive stops.
8. **First AC power.** Unplug USB. Check both fuses for continuity. Feed J1 from a variable 0 to 32 VAC isolated source. The buck starts at about 9 V on `VIN_DC`. At 24 VAC: `VIN_DC` 28 to 31 V, `V_BUCK` 5.16 to 5.40 V, `AC_ZC` a clean square wave, `VIN_MON` about 0.97 V. Raise to 32 VAC for 30 minutes: `VIN_DC` under 45 V, D1 cold.
9. **Both supplies.** Add USB from a battery laptop. No current flows back into VBUS beyond 1 mA.
10. **Relay.** With 24 VAC and the keep-alive stopped, GPIO7 reads about 3.05 V. Start the keep-alive: it reads 0 V. Command K1 closed. `COM` is live. Stop the keep-alive and time K1 opening against `DRV_EN` and the gate of Q2: it opens when the first of the two lets go. Trip the over-current latch with K1 closed: it opens within 10 ms. Run the firmware sequence of section 19.7 and check the times it reports against the scope.
11. **Resistive load.** 47 ohm, 25 W from `COM` to each output in turn: 0.5 A. Scope T2: firing voltage a few volts each half-cycle. Compare the current reading with a meter.
12. **Valve.** A real solenoid on each output. Scope T2 at turn-off: one clean stop at the current zero, ringing under the 77 V clamp. Then a 50 mA relay coil.
13. **Short circuit.** With a 40 VA transformer, short an output to `COM` and turn it on. One or two half-cycles of current, latch set, fuses intact, triac still working, weld check passes. Repeat with the short applied while the output is on. Risk 4 has the full test.
14. **Fail-safe with a valve running.** Halt the CPU, hold reset, start a USB flash. The valve closes within 0.95 s each time, or 1.4 s if the pin stopped high, and does not reopen. Then the fault-injection runs of risk 23.
15. **Sensors.** Contact on each input. 200 Hz square wave on FLOW with no lost counts. 24 VAC onto each sensor terminal for ten minutes, then check the input still works. Short and miswire the 5V terminal.
16. **RTC.** Fit the cell. Measure cell current with `3V3` off: under 1 uA. Check the clock against a reference over a day.
17. **Display and controls.** Plug the panel in. Backlight current about 59 mA at full duty. Run the SPI at 20 MHz, then higher. Encoder and buttons.
18. **Thermal run, RF and the remaining items of section 20.**

## 22. What I could not confirm

Review A1 went through this list against primary sources. The first table is
what it settled. The list after it is what is still open.

| Item | Result | Source |
|---|---|---|
| T410 and T810 pin order | Confirmed: pin 1 T1, pin 2 and tab T2, pin 3 gate | ST DocID7699 rev 9 and DocID7472 rev 12, page 1 drawings |
| Bulk capacitor ripple rating | 900 mA at 100 kHz, 540 mA at 50 and 60 Hz for the 63 V part | Lelon VZH table, 63 V column |
| Relay must-operate voltage | 75 % of rated at a coil temperature of 23 °C. 11 % margin with a hot coil now that the coil is on `V_BUCK`, section 7.2 | Omron G5NB data sheet, English edition |
| Display FPC contact side | Same face as the screen | Panel drawing, front and rear views |
| Display viewing area | 33.6 x 43.94 mm is the likely window. The drawing does not label it. | Panel drawing, note 8 |
| Display SPI clock | 15 MHz guaranteed for an ST7789V | Sitronix ST7789V v1.3, serial interface timing |
| Antenna clearance | 15 mm in all directions inside the housing; antenna beyond the board edge | Espressif hardware design guidelines, 1.4.7 |
| Module flash erase time | Espressif does not name the part. A representative 16 MB part: 45 ms typical, 400 ms maximum per 4 kB sector; 2 s maximum per 64 kB block | Module datasheet v1.8 note 3; Winbond W25Q128JV rev C |
| TPS2553 limit | 116, 135 and 158 mA minimum, nominal and maximum at 200 kohm | TI SLVS841, equation 1 |
| Buck efficiency | The data sheet has curves for a 12 V output only. A loss estimate from its on-resistances gives about 90 % at 0.3 to 0.8 A, so 85 % is on the safe side. | TI SNVSAU4D, electrical table |
| MOC3023 dv/dt | 1000 V/us minimum is the onsemi figure for this part number. A part from another maker with the same type number needs its own check. | onsemi MOC3023M/D rev 3, electrical table |
| `listing only` parts opened in the review | AO3400A, 1N4148W, 1206L012 PTC, both LEDs, the crystal, the USB-C receptacle, the 3.81 mm header, the FPC connector, the Uniroyal resistor series. Ratings and pin tables agree with the netlist. The resistor series has no pulse rating, which is why R1 is no longer from it, section 4.4. | LCSC copies of the maker data sheets |
| Surface-mount relay | None of the stocked ones meets the ratings | JLCPCB parts search, 2026-11-10, section 7.2 |
| Pulse-rated R1 | Yageo SR2512JK-073RL, stocked | Yageo SR data sheet V.14, figure 7-2 |

Still open:

- **Class 2 short-circuit limit.** Secondary sources agree on 8 A and 100 VA for an inherently limited source up to 30 V. I could not read the NEC table itself. A Class 2 source that relies on a fuse may deliver more for a short time.
- **USB attach capacitance.** The 10 uF figure in section 5.2 is from memory.
- **Display.** The controller variant, the logic supply current and a second source. JLCPCB lists no other maker's ST7789 panel in stock, and a search found no panel with this 12-pin order.
- **Coin cell.** Capacity and shelf life assumed. The cell is on the BOM with no LCSC number and no data sheet read.
- **Solenoid inductance and transformer impedance.** Estimates, used in sections 6.5 and 6.6.
- **Typical values that the datasheets do not give.** Expander output voltage at 13 mA and ADC noise.
- **Generic capacitors, the tactile switches, the JST connector and the coin-cell holder.** Ratings are from the listing. The switch and JST files are drawings without extractable text and were not read in detail. LCSC has no data sheet for the holder.
- **Second sources.** PCA9539PW for the expander is named in the BOM note and its datasheet is not read. For the output PTC the BHFUSE part is now the primary and the Littelfuse part the alternative; both data sheets are read.
- **AO3400A threshold over temperature.** The data sheet gives 0.65 to 1.45 V at 25 °C. The -3 mV/K used in section 7.3 is an assumption.
- **BAT54S leakage when hot.** The data sheet limits it at 25 °C only. Both detectors are sized so that it matters little, and risk 10 measures it.
- **PTC trip time above 8 A.** One data sheet point, scaled. Section 6.6.
- **Fuse holder height.** Not read. Section 16.4.
- **Resistor array pin pairs.** The UNI-ROYAL data sheet shows the circuit as a picture. Pairs 1-8, 2-7, 3-6 and 4-5 are assumed from the usual numbering.
- **Regulator over-voltage.** Section 8.5.
- **Lifecycle status.** The JLCPCB listing does not give it. Only the LM5164DDAR is confirmed active, from the TI data sheet addendum.
- **The micro-crystal RV-8263-C7 and RV-3028-C7.** Not evaluated.

## 23. Sources

Datasheets I read, with the revision I had. Where the link is an LCSC page, it
is LCSC's copy of the maker's PDF for that LCSC number.

| Part | Document | Link |
|---|---|---|
| ESP32-S3 | Series datasheet v2.2 | <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf> |
| ESP32-S3-WROOM-1 | Module datasheet v1.8 | <https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf> |
| LM5164 | SNVSAU4D, February 2026 | <https://www.ti.com/lit/ds/symlink/lm5164.pdf> |
| T810-600B | BTA08, BTB08, T810, T835, T850, DocID7472 rev 12 | <https://www.lcsc.com/datasheet/C155588.pdf> |
| T410-600B, the first draft's triac | T4 series, DocID7699 rev 9 | <https://www.lcsc.com/datasheet/C2970064.pdf> |
| BTA204S-600D | Product data sheet, 14 August 2017 | <https://www.lcsc.com/datasheet/C256464.pdf> |
| MOC3023 | MOC3023M/D rev 3, June 2023 | <https://www.onsemi.com/download/data-sheet/pdf/moc3023m-d.pdf> |
| TCA9539 | SCPS202C | <https://www.lcsc.com/datasheet/C131972.pdf> |
| LM2901 | SLCS006S | <https://www.lcsc.com/datasheet/C7932.pdf> |
| 74HC132, and the 74HC00 it replaced | Nexperia data sheets | <https://www.lcsc.com/datasheet/C5601.pdf>, <https://www.lcsc.com/datasheet/C5586.pdf> |
| TLV803E | TI TLV803E, TLV809E, TLV810E, LCSC copy ZHCSJX8J | <https://www.lcsc.com/datasheet/C5218924.pdf> |
| AO3400A | AOS, rev 3, December 2011 | <https://www.lcsc.com/datasheet/C20917.pdf> |
| SR2512JK-073RL | Yageo SR series, V.14, 2025-02-14 | <https://www.lcsc.com/datasheet/C2936639.pdf> |
| MMB0207, rejected for R1 | Vishay MELF resistors | <https://www.lcsc.com/datasheet/C2092879.pdf> |
| BSMD2920-100-60V | BHFUSE BSMD2920 series | <https://www.lcsc.com/datasheet/C3020148.pdf> |
| 4D03WGJ0104T5E | UNI-ROYAL chip resistor arrays | <https://www.lcsc.com/datasheet/C1996.pdf> |
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
| ESP32-S3 hardware design | Espressif hardware design guidelines, release of 2026-11-10 (review A1) | <https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/> |
| ST7789V | Sitronix datasheet v1.3 (review A1) | <https://dl.espressif.com/dl/schematics/LCD_ST7789.pdf> |
| W25Q128JV | Winbond datasheet rev C (review A1) | <https://www.pjrc.com/teensy/W25Q128JV.pdf> |
| 452/454 fuse | Littelfuse 452/454 series (review A1) | <https://www.lcsc.com/datasheet/C99550.pdf> |
| FPC connector, USB-C, header, AO3400A, 1N4148W, 1206L012, LEDs, crystal, thick-film resistors | Maker datasheets (review A1) | LCSC pages for C2856799, C165948, C441187, C20917, C81598, C151160, C125094, C2286, C32346, C39347 |

Stock, price and library class came from the JLCPCB parts search on
2026-11-10. The prior-art evidence is in the [prior-art survey](prior-art.md).

## 24. What changed after review A1

The review, [hardware review A1](hardware-review-a1.md), had one blocking
finding, seven major ones, sixteen minor ones and six notes. The project
owner decided each. This section records what was done, and where I did
something other than what the reviewer proposed, with the reason.

| Finding | Decision | What was done | Section |
|---|---|---|---|
| B1, single faults hold the fail-safe gate on | Adopt a second, independent detector for the relay. Review it. Add the `DRV_EN` read-back. | Done, with different values from the proposal. Read-backs of both detectors. Combined fault table over both. | 7.3, 7.4, 8.5, 8.6 |
| M1, triac not certain to stop on a short | Evaluate options, then choose | T810-600B-TR and a PTC rated for 40 A. The output is shown by calculation to carry a short until the PTC trips. | 6.3, 6.6 |
| M2, one fuse | A fuse in each leg | F4 added | 4.3, 5.3 |
| M3, relay hold network did nothing | Fast release | D21 and R13 removed. Weld check specified. | 7.2, 7.4 |
| M4, GPIO40 on the latch node | Fixed in the review | R87 | 9.3 |
| M5, comparator input below ground | Fixed in the review | R88 | 11 |
| M6, R1 has no pulse rating | A part with a published rating and stock | Yageo SR2512JK-073RL, with the calculation | 4.4 |
| M7, no clamp on COM | Add it | D23 | 7.2 |
| m4 | Coil from `V_BUCK` | Done. Margin 11 %, was 3 %. | 7.2 |
| m5 | Snubber return to `ISNS` | Done on all nine channels | 6.5 |
| m7 | Schottky on `SP_OUT` | D24, an SS34. Its margin is thin and is a bench item. | 13.5 |
| m9 | Supervisor on `EN` unless shown unnecessary | U12 fitted. The supply ramp is slow, so it is needed. | 12.4 |
| m10 | Defined levels on every expander pin | RN1 to RN3 and R96 | 8.2 |
| m11 | Move two expander inputs to GPIOs if any are free | None is free. Firmware procedure given. | 12.2, 19.6 |
| m12 | Series footprints on USB and TXD0 | R97 to R99, C52 and C53 | 5.1, 16.2 |
| m13 | 74HC132 | Done | 8.2 |
| m14 | Qwiic budget 80 mA | Done | 4.2, 16.2 |
| m16 | CR2032 on the BOM | A line with no LCSC number, marked not machine-fitted | 18 |
| Surface-mount relay | Search, and switch if one meets the ratings | Searched. None does. | 7.2 |
| n4, no generator in the branch | Commit it, with a check in CI | `tools/designsync`, `make -C hardware design-check`, workflow `hardware` | Introduction |
| m1 to m3, m6, m8, m15, n1 to n3, n5, n6 | Fixed or noted in the review | Kept. n6 is firmware rule 11. | |

**Where I departed from the review, or went further.**

1. **The relay detector's values.** The proposal used 1 uF capacitors and 1 Mohm bleed resistors, and its own simulation showed the hold-up halving at 60 °C. The cause is Schottky leakage of the same size as the bleed current. I used 100 kohm, 10 uF and 4.7 uF, which makes the bleed ten times the leakage. Section 7.3.
2. **The release window is stated in full.** The proposal quoted the time at which Q2 is certainly off. The transistor's threshold spread means K1 may release much earlier. The window is 0.12 to 0.95 s, and section 7.3 says why that is acceptable.
3. **A read-back for the relay detector.** The proposal read back `DRV_EN` only. A stuck relay transistor would then be found by nothing, and the second detector could fail silently. GPIO7 reads the drain of Q2. To free it, the sensor supply enable moved to GPIO45. Sections 7.4 and 12.2.
4. **A stocked pulse-rated resistor exists.** The review found none in stock. The Yageo SR series is, and its 5 % parts have four times the pulse rating of its 1 % parts. Section 4.4.
5. **The output PTC.** The review noted that 20 A is not a guaranteed ceiling for a Class 2 source and left the part. I replaced it with one rated for 40 A. Section 6.6.
6. **The fold of the display tail.** The review's text said the fold reverses the pin order as the board sees it. It does not. Section 16.4.
7. **A bridge from the keep-alive net to the zero-cross net.** Not in the review. I estimated by hand that it could not run either detector, simulated it and found that it can hold the relay detector on. It cannot enable the other. Section 8.5.
8. **The operating temperature inside the enclosure** is 70 °C, from the enclosure concept's own estimate, not the 60 °C the first draft and the review used. PSRAM ECC becomes mandatory. Sections 3.3 and 6.7.

**What I did not resolve.**

- **Regulator over-voltage.** If U1's switch shorts, the fail-safe analysis has no proof. Section 8.5 and risk 21.
- **Q3 shorted** is found by no self-test. Section 8.5.
- **C1 and K1 on the bottom side.** A decision for the owner and the assembler. Section 16.4.
- **Everything in section 20.** No bench work has been done.
