# ZoneFlow hardware design, revision A

Status: **paper design, 2026-11-11, after
[hardware review A1](hardware-review-a1.md), the decisions taken on its
findings, [hardware review A2](hardware-review-a2.md) of the fail-safe
circuits, and the [answers to the firmware's questions](hardware-answers-a2.md)
on section 19**. No board exists. Nothing in
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

Section 24 lists what changed after review A1 and where I did something
other than what the reviewer proposed. Section 25 lists what review A2
changed. Section 26 lists what the answers to the firmware's questions
changed: one part of the circuit and most of section 19.

## 1. The design in brief

| Block | Choice | Main reason |
|---|---|---|
| Logic supply | A fuse in each transformer leg, 1500 W TVS, half-wave rectifier, 470 uF, LM5164 100 V buck to 5.3 V with its own 0.5 A fuse and a clamp on its output, Schottky OR with USB, LDL1117 to 3.3 V | The TVS clamps at 77.4 V. A 100 V regulator survives that with no further argument. A 5 V bus makes USB and 24 VAC equal sources and gives the LDO room to reject ripple. The fuse and the clamp keep a failed regulator from putting 45 V on the logic. |
| Output switch | T810-600B-TR 8 A three-quadrant triac in DPAK, fired by a MOC3023 opto-triac from its own T2 | The triacs JLCPCB stocks with real surge ratings are three-quadrant parts. Positive-only gate drive from logic cannot fire them on the negative half-cycle. The 8 A part carries the short-circuit current of a 75 VA transformer until the PTC trips. Section 6 has the analysis. |
| Output control | TCA9539 I2C expander sinks the opto LEDs | The TFT took six GPIOs. Nine valve pins no longer fit. The expander is held in reset by the fail-safe circuit, so no valve bit survives a crash. |
| Fail-safe | Two independent detectors on the keep-alive pin. One, a diode pump with a comparator, switches the LED supply and the expander reset. The other, a diode pump straight into a transistor gate, holds the relay. | Gate drive needs an edge at least every 20 ms. It is gone 0.21 to 0.83 s after the edges stop, and with it the valve current. The relay transistor alone lets go within 1.45 s. No single component fault leaves a valve on after the edges stop. Sections 7 and 8, with the corners of review A2. |
| Over-current | Window comparator on the shunt amplifier, NAND latch, same switch as the fail-safe | Removes drive about 70 us after a short begins and releases K1 at once. The triac and the PTC are rated to carry the short until one of them or the relay interrupts it. |
| Over-voltage on the logic rail | A voltage detector on `3V3` that holds the set input of the over-current latch low above 4.0 V | A shorted 3.3 V regulator puts 5 V on the processor, and nothing can be said about a processor there. The detector removes drive without it. It does not protect the processor. Section 9.5. |
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
| `VIN_U1` | `VIN_DC` behind fuse F5: the input pin of U1 with C2 and C3. |
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

Two single resistors do not fit the rule, because the resistor numbers below
100 ran out. R13 was free since review A1 removed the relay hold network. It
is in use again as the upper leg of the over-voltage divider of section 9.5.
The lower leg is R100. No channel uses a number that ends in 00.

### 3.3 Operating envelope I designed to

| Item | Value | Source |
|---|---|---|
| Ambient outside the enclosure | -20 to +50 °C, indoor or garage, non-condensing | Specification section 1, from v0.5 |
| Air inside the enclosure | up to 70 °C | 50 °C plus the rise the mechanical design estimates. Revision 1 of the contract said 16 K at 3 W with vents, and 70 °C was set from that. Revision 2 says 14 K at 3.5 W (mechanical design, section 11), which leaves about 6 K in hand. The first draft assumed 10 K. Bench test in section 20. |
| Supply | 21.6 to 26.4 VAC at load, up to 32 VAC unloaded, 50 or 60 Hz | Specification section 1 |
| Load | 0.5 A rms per output, 1.5 A rms inrush for 100 ms, two zones plus master | Specification section 1 |
| Smallest load | 50 mA rms pump-start relay coil | Brief |
| Transformer | Class 2, 20 to 75 VA. Short-circuit current at the terminals 7 to 21 A rms. | Section 6.6 calculates it. The output PTC is rated for 40 A. |

## 4. 24 VAC input and logic supply

### 4.1 Circuit

Each transformer leg has a fuse. Terminal 2, `AC_N`, goes through F1 to `GND`.
Terminal 1, `AC_L_IN`, goes through F4 to `AC_L`. `AC_L` feeds three things: the TVS D1 to
`GND`, the half-wave rectifier D2, and the valve common through relay K1.
D2 charges the bulk capacitor C1 through R1. Fuse F5 takes `VIN_DC` to the
input of the LM5164 buck U1, which makes `V_BUCK`, 5.28 V. D25 clamps
`V_BUCK` if U1 fails; section 4.8 has F5 and D25. Schottky D3 passes
`V_BUCK` to the 5 V bus.

<!-- designsync:begin table block="A power" -->

| Ref | Part | Package | LCSC | Connections | Note |
|---|---|---|---|---|---|
| J1 | Header 2-way 3.81 mm pluggable, right angle | THT | C441182 | `AC_L_IN` to `AC_N` | 24 VAC input |
| F1 | Fuse 3 A time-lag in SMD holder | SMD 9.7x5 mm | C206913 | `AC_N` to `GND` | Fuse in the leg that becomes circuit ground |
| F4 | Fuse 3 A time-lag in SMD holder | SMD 9.7x5 mm | C206913 | `AC_L_IN` to `AC_L` | Fuse in the other leg |
| D1 | TVS 48 V bidirectional 1500 W | SMC | C151287 | `AC_L` to `GND` | Input surge clamp |
| D2 | Rectifier 1 A 1000 V | SMA | C95872 | A `AC_L`, K `VRECT` | Half-wave rectifier |
| R1 | 3R 5% 1 W, surge series | 2512 | C2936639 | `VRECT` to `VIN_DC` | Limits charging and surge current. Pulse rating in section 4.4 |
| C1 | 470 uF 63 V aluminium electrolytic | THT 12.5x20 mm | C2940725 | + `VIN_DC`, - `GND` | Bulk capacitor. Through-hole, on the bottom side, lying down |
| F5 | Fuse 0.5 A very fast-acting, 125 V | 2410 | C142836 | `VIN_DC` to `VIN_U1` | Opens when D25 clamps a failed U1. Section 4.8 |
| C2 | 2.2 uF 100 V X7R | 1210 | C55151 | `VIN_U1` to `GND` | At U1 pins |
| C3 | 2.2 uF 100 V X7R | 1210 | C55151 | `VIN_U1` to `GND` | At U1 pins |
| U1 | LM5164DDAR 100 V 1 A synchronous buck | SO-8 PowerPAD | C477928 | 1 GND `GND`, 2 VIN `VIN_U1`, 3 EN/UVLO `UVLO`, 4 RON `RON`, 5 FB `FB`, 6 PGOOD `PGOOD`, 7 BST `BST`, 8 SW `SW`, EP `GND` |  |
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
| D25 | TVS 6 V unidirectional 600 W | SMB | C83270 | A `GND`, K `V_BUCK` | Over-voltage clamp on the buck output. Section 4.8 |

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

C1 is a Rubycon 63ZLJ470M12.5X20: 470 uF, 63 V, a through-hole can of
12.5 x 20 mm, LCSC C2940725, 3180 in stock at JLCPCB on 2026-11-10. Its
steady voltage is at most 44.3 V, 70 % of rating.

**Why the part changed.** The first draft used a surface-mount Lelon VZH can,
16.5 mm tall. The enclosure allows 9 mm above the board and 17 mm below it,
and revision 2 of the mechanical contract allows no surface-mount part on
the bottom side, because one capacitor would need a second assembly side
(mechanical design, section 6). So C1 is a through-hole part on the bottom,
lying on its side. The contract named the Rubycon PX part that the BOM
carried as the alternative. I read the three data sheets before switching:

| | Lelon VZH471M1JTR-1616, first draft | Rubycon 63PX470MEFC12.5X20, named by the contract | Rubycon 63ZLJ470M12.5X20, fitted |
|---|---|---|---|
| Case | Surface-mount, 16 x 16.5 mm | Through-hole, 12.5 x 20 mm, 5.0 mm lead pitch, 0.6 mm leads | The same case, pitch and leads |
| Largest size | 17.0 mm tall | 13.0 mm diameter, 21.5 mm long | 13.0 mm diameter, 22.0 mm long |
| Endurance at 105 °C, rated voltage and rated ripple | 5000 h | 2000 h | 10,000 h |
| Rated ripple at 105 °C | 900 mA at 100 kHz | 700 mA at 120 Hz | 2400 mA at 100 kHz, 1320 mA at 120 Hz |
| The same at 50 and 60 Hz | 540 mA, multiplier 0.6 | 560 mA, multiplier 0.8 | Not given. About 1.05 A if the PX multiplier of 0.8 holds for it |
| Dissipation factor at 120 Hz | not read | 0.10 | 0.09 |
| The 0.48 A of the peak corner against the rating | 89 % | 86 % | 36 % of the 120 Hz figure |
| Stock at JLCPCB on 2026-11-10 | 968 | 14,076 | 3180 |
| Price at 100 pieces | 0.59 USD | 0.27 USD | 0.25 USD |

All three cover the 0.16 A typical and 0.48 A peak ripple. The PX part has
about the margin of the Lelon part and less than half its endurance, so I
did not take it.
The ZLJ part is from the same maker in the same case. Its sheet gives ripple
multipliers from 120 Hz upward only, 0.55 at 120 Hz for 330 to 680 uF, and
nothing for 50 or 60 Hz. The 1.05 A in the table borrows the PX sheet's
ratio and is an estimate. Even against half that figure the peak corner is
inside the rating.

**Life.** C1 has voltage and ripple on it for as long as the controller is
plugged in. The usual rule is that an aluminium electrolytic capacitor lasts
twice as long for every 10 K below its rated temperature. That is general
practice, not a Rubycon formula that I read, and it ignores the extra life
that comes from running below the rated ripple.

| Air around C1 | Factor on the 105 °C endurance | PX, 2000 h | Lelon VZH, 5000 h | ZLJ, 10,000 h |
|---|---|---|---|---|
| 70 °C, the design limit of section 3.3 | 2^3.5 = 11 | 2.6 years | 6.5 years | 13 years |
| 55 °C | 2^5 = 32 | 7.3 years | 18 years | 37 years |
| 40 °C, a 25 °C room | 2^6.5 = 91 | 21 years | 52 years | about 100 years |

The figures are continuous operation at that temperature. Past about 15
years other wear takes over and the arithmetic stops meaning anything. The
point is the first row: a controller in a hot garage would wear out a
2000 h part in a few summers.

**Second sources.** The PX part, C88758, fits the same holes. Fit it only
knowing its life. AISHI ERS1JM471K20OT, C106675, is listed with 10,000 h in
a 13 x 20 mm case and had 85,615 in stock; LCSC has no data sheet link for
it and I did not read one.

**Placement and fitting.** The mechanical contract fixes C1 on the bottom
side: can centred at (21, 30), axis along x, leads towards +x and bent
through 90 degrees into pads at (36.0, 27.5) and (36.0, 32.5). It is soldered
by hand after the surface-mount parts, in the same step as K1 and the
terminal headers, and JLCPCB does not machine-fit it. Two ribs in the
enclosure base carry its weight once the board is in. The contract was drawn
for the PX can, 21.5 mm long at most. The ZLJ can may be 22.0 mm, and zone
B_C1 has 0.75 mm beyond the can at the far end, so it fits; the enclosure
owner should take the 0.5 mm into `hardware/mechanical.yaml`. Bend the leads
no closer than 2 mm to the seal. That figure is common practice. I did not
read Rubycon's handling notes.

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

If U3 fails with its input joined to its output, `3V3` goes to `V5`. The
processor and the panel are then outside their ratings and nothing protects
them. Section 9.5 has the detector that keeps the valves off in that fault.

### 4.8 Over-voltage on `V_BUCK`: clamp D25 and fuse F5

Added by [review A2](hardware-review-a2.md), section 6, on the owner's
decision that a failed regulator must fail safe and must not destroy the
board silently.

**The fault.** The high-side switch inside U1 fails short, or stays on.
`VIN_DC`, 26 to 45 V, then reaches `V_BUCK` through L1. With nothing to stop
it, `hardware/sim/buck_overvoltage.cir` puts `V_BUCK` past 7 V after 11 to
14 us and at 43 to 65 V after the first ring of L1 with C5 and C6. Every
5 V and 3.3 V part is then beyond its rating, the processor included, and
the fail-safe analysis of section 8.5 has nothing to stand on.

**The protection.** Two parts.

- **D25**, a Littelfuse SMBJ6.0A, from `V_BUCK` to `GND`, cathode to
  `V_BUCK`. It stands off 6.0 V, breaks down between 6.67 and 7.37 V and
  clamps at 10.3 V at its rated 58.3 A.
- **F5**, a Littelfuse 0451.500MRL, 0.5 A very fast-acting, in series with
  the input of U1. It sits between `VIN_DC` and a new net, `VIN_U1`, which
  holds U1 pin 2 and the input capacitors C2 and C3.

When the switch shorts, D25 holds `V_BUCK` and the charge of C1 flows
through F5, the failed switch, L1 and D25. F5 opens. `V_BUCK` then falls to
zero and stays there.

**Why not a crowbar that opens the input fuses.** Every path from the
transformer to `V_BUCK` runs through D2 and R1, 3 ohm. With `V_BUCK` held
near zero the rectifier delivers half-wave pulses of 3.6 to 7.8 A peak,
1.6 to 3.7 A rms, for a 20 to 75 VA transformer (the "follow current" lines
of the simulation). F1 and F4 are 3 A time-lag fuses: at 1.6 to 3.7 A they
open after minutes or not at all. What would open is R1, at 9 to 42 W in a
1 W part, and D2. That is the board destroying itself in an uncontrolled
way. So the element that opens has to sit behind C1, where the capacitor
can deliver the current that the transformer cannot, and it has to be much
smaller than 3 A.

**Sizing F5.**

| Check | Calculation | Result |
|---|---|---|
| Normal load | 1.76 W typical at 29 V; 5.0 W at the 17.3 V valley of low line | 0.06 A typical, 0.29 A at the worst steady corner: 58 % of rating. Littelfuse asks for 75 % at most. |
| Voltage lost | 0.42 ohm x 0.29 A | 0.12 V on a 17 V input |
| Voltage rating | `VIN_DC` reaches 44.3 V, 59 V in a surge | 125 V |
| Fault current it must interrupt | (44.3 V - 6.67 V) over the resistance in the loop: C1 0.033 ohm at most (Rubycon ZLJ sheet, 100 kHz, 20 °C), F5 0.42 ohm, L1 0.2 ohm, D25 0.03 ohm, and a switch that has become 0.02 ohm | 53.5 A by this sum. The simulation gives 49 to 52 A with L1 saturated, because the fuse has opened before the current has fully risen. At a `VIN_DC` of 30 V it gives 25 to 30 A. The part interrupts 50 A at 125 V and 300 A at 32 V DC. |
| Melting | Nominal melting I²t 0.0824 A²s. C1 delivers I² x R x C / 2 into this loop: 0.18 A²s from 30 V, 0.47 A²s from 44.3 V, 0.04 A²s from 17 V | The charge of C1 alone opens it from about 22 V up. Below that the rectifier has to finish the job, see below. |

The fuse's own resistance does two jobs in that table. It is most of the
resistance in the loop, and it is what keeps the fault current near what the
fuse can interrupt. In the one worst case, the highest input with a welded
switch and a saturated inductor, the current reaches the 50 A that the data
sheet gives for 125 V, to within 3 %. The fuse then has to break it at 44 V.
Its rating at 32 V DC is 300 A, and the sheet has no figure in between. I
take the part as able to do it and cannot show that from the sheet. It is
the first thing risk 21 tests. **Do not fit a larger rating.** A 1 A part of
the same series has 0.153 ohm and seven times the melting I²t, and the
current would reach 86 A.

C1 matters here. The through-hole Rubycon part that replaced the Lelon part
has less than half its series resistance, 0.033 against 0.082 ohm, and that
raised the worst peak from 46 to 49 A. With a C1 three times better than
its limit, 0.010 ohm, the simulation gives 51 A.

**What the simulation shows.** `hardware/sim/buck_overvoltage.cir`, 36 cases
with a 20 VA transformer, 12 more up to 75 VA and 6 with other values for
C1: `VIN_DC` at 17, 30 and 44.3 V, the failed switch at 0.02, 0.725 and
5 ohm, L1 at 47 uH and saturated at 5 uH, F5 opening at its nominal melting
I²t and at three times that.

| | Range over the cases |
|---|---|
| Highest `V_BUCK`, D25 at its highest clamp | 7.5 to 9.8 V, 9.9 V with C1 at 0.010 ohm |
| Highest `V_BUCK`, D25 at its lowest clamp | 7.1 to 8.2 V |
| Peak current in F5 and D25 | 2.2 to 51.5 A. D25 is rated for 58.3 A. |
| F5 open after, welded switch, `VIN_DC` 30 V or more | 0.04 to 0.2 ms at nominal I²t; up to 18 ms at three times that on 30 V |
| F5 open after, every other case | 0.15 to 200 ms |
| Energy in D25 until F5 opens | 0.016 to 0.69 J. In the cases that end within 10 ms, where the pulse curve of the SMBJ data sheet applies (Fig. 2, 600 W at 1 ms), the curve allows 4.4 to 11 times what D25 takes. |
| Energy in R1 | 0.02 to 1.4 J |

The slow cases are a switch that has become several ohms, and a low
`VIN_DC`. The model opens the fuse on I²t alone, which is right for
milliseconds and optimistic for longer. The bound that does not rest on the
model: the follow current is 320 to 730 % of F5's rating, and the data
sheet gives 5 s at most at 200 %. For those seconds D25 takes 5.5 to 15 W
and R1 9 to 42 W. D25 is rated 5 W steady; if it overheats it fails short,
which is the normal failure of a TVS, the current rises and F5 opens. R1
may not survive the slow cases. `V_BUCK` stays under the clamp voltage in
all of them.

**What every other part sees while D25 clamps.**

| Part | Sees | Rating | Result |
|---|---|---|---|
| U3, and so `3V3` and everything on it | `V5` up to 9.5 V | 20 V absolute, 18 V operating | `3V3` stays regulated. The processor, the expander, U7, Q3 and `V_DRV` work as normal. |
| U6 | 9.5 V | 36 V | Normal |
| Coil of K1 | 9.8 V | 8.5 V continuous at 23 °C (170 %) | 0.77 W in the coil for at most seconds. No damage expected. |
| Read-back divider to GPIO7 | 9.8 V on R93 | (9.8 - 3.6) V / 110 kohm = 56 uA at most towards the pin's clamp | Harmless |
| Sensor pull-ups | 9.5 V on R74, R77, R80 | 0.3 mA each into the BAT54S clamps | Harmless |
| Backlight | (9.5 - 3.0) V / 33 ohm = 197 mA | 80 mA rated | For milliseconds in the fast cases. May age the LEDs in a slow one. |
| **U11** | 9.8 V on its input | **7 V absolute maximum** | The one part beyond its rating: 7.5 to 9.8 V for 0.04 ms to 0.2 s, seconds in the slow cases. It may be damaged. It switches the sensor terminal and nothing else. |

So the fail-safe circuits stay inside their ratings throughout, and section
8.5 applies unchanged: the fault is one fault, and both detectors are
healthy. While D25 clamps, the processor runs and the keep-alive is under
its control. When F5 has opened, `V_BUCK` is zero. The coil of K1 has no
supply and no valve can be on, whatever the keep-alive does, with or
without USB power.

**A feedback fault.** R3 or R4 open leaves U1 with no feedback and it runs
at full duty, but its current limit still works: 1.75 A at most. D25 then
takes 7.4 V x 1.75 A = 13 W and fails short within seconds. F5 does not
open, because the input current is under 0.5 A. U1 limits into the short.
The board is dead with `V_BUCK` at zero. That is fail-safe and it is
visible, and it costs D25.

**How the fault shows.** On 24 VAC alone the board is dark. With USB
attached, firmware sees mains on `AC_ZC` and a normal `VIN_MON`, and the
relay read-back on GPIO7 reads low at rest, which it does only when
`V_BUCK` is missing. Firmware rule 15 reports that combination as a
regulator fault. Repair is U1, F5 and, after a slow case, D25 and R1.

**What is not covered.**

- **D25 failing open before F5 has opened.** A TVS driven far past its
  rating can lose a bond wire. Here the fuse limits the energy to a
  fraction of the pulse rating, so I do not expect it. Bench item, risk 21.
- **U11 pushing its input voltage onto GPIO45.** If U11 is damaged in a way
  that joins its input to its enable pin, up to 9.8 V reaches a processor
  pin through no resistance. A 10 kohm resistor in `SENS_PWR_EN` would
  bound that current to 0.6 mA. Review A2 recommends it and did not apply
  it; it is one more part and one more net.
- **The same fault in U3.** If the LDO shorts input to output, `3V3` goes
  to 4.95 V. That is a different fault. Section 9.5 has it: the valves are
  held off by hardware and the processor is not protected.

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
| D1 shorted | Leg 1, F4, D1, `GND`, F1, leg 2 | In the loop | In the loop |
| C1 shorted | Leg 1, F4, D2, R1, C1, `GND`, F1, leg 2 | In the loop, but see below | In the loop, but see below |
| Leg 1 earthed, earthed computer on USB | Leg 1, earth, computer, USB ground, `GND`, F1, leg 2 | In the loop | Not in the loop |
| Two devices on one transformer with legs swapped, grounds joined | Leg 1, other device's ground, joined ground, `GND`, F1, leg 2 | In the loop | Not in the loop |
| COM or a valve wire leaking to earth, earthed computer on USB | Leg 1, F4, K1, COM, earth, computer, USB ground, `GND`, F1, leg 2 | In the loop | In the loop |
| Leg 2 earthed, earthed computer on USB, and a board fault from the first two rows | As those rows as far as `GND`, then F1 in parallel with USB ground, computer, earth, leg 2 | Bypassed | In the loop |
| A second device on the transformer with the same leg as ground, grounds joined, and a board fault | The same, with the other device's ground in place of earth | Bypassed | In the loop |

**A shorted C1 does not open a fuse.** Review A2 simulated the current the
rectifier delivers into a short behind R1 (`hardware/sim/buck_overvoltage.cir`,
the follow-current lines): 1.6 to 3.7 A rms for a 20 to 75 VA transformer,
because R1 is 3 ohm and the rectifier conducts on one half-cycle only. That
is 54 to 122 % of F1 and F4, which open after minutes at those currents or
not at all. R1 takes 9 to 42 W and burns open. The board is dead and no
valve runs, so the fault is safe, but it is R1 and not a fuse that ends it.
A short in C2, C3 or U1 is different since that review: those are behind
F5, section 4.8.

The first draft had F1 only, for the fourth and fifth rows: those loops do
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
and the module. The total is about 3.5 W in a closed box. The mechanical
design estimates the rise with vents: 16 K at 3 W in revision 1 of the
contract, from which the 70 °C air temperature of section 3.3 was set, and
14 K at 3.5 W in revision 2. The R8 module is rated to 65 °C
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
release and remove the hold parts, D21 and R13. The R13 of today is another
part, in section 9.5. So:

| Event | What K1 does |
|---|---|
| Over-current trip | `DRV_EN` falls within 70 us, Q1 turns off 0.1 ms later, the contacts part within the 10 ms release time. K1 may break fault current. Section 6.6. |
| Keep-alive stops, both detectors healthy | The gate-drive detector removes LED drive after 0.21 to 0.83 s and releases Q1. The relay detector releases Q2 after 0.10 to 1.45 s. Whichever is first opens K1. If the relay detector is first, K1 breaks the valve current, at most 1.5 A, inside its rating. |
| Keep-alive stops, gate-drive detector stuck on | Q2 opens K1 within 1.45 s, or 2.19 s if the pin stopped high. The triacs are still driven, and it does not matter. |
| Keep-alive stops, relay detector stuck on | `DRV_EN` falls within 0.83 s, or 1.25 s if the pin stopped high, and Q1 opens K1. |
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

**Review A2 widened this window.** Its corner runs, review section 4, take
the threshold drift between -1.5 and -4.5 mV/K where the table above takes
-3 mV/K, `3V3` from 3.0 to 3.6 V, diodes at both ends of their data and a
pin of 16 to 70 ohm. With the firmware's 5 ms edges Q2 lets go between 0.10
and 1.45 s after the last edge, and within 2.19 s if the pin stops high. The
long end is a Q2 with a 0.65 V threshold at 70 °C. Section 19.4 and the
limits of section 19.7 use these figures. Which end a real part sits at is a
bench item, risk 28.

The window is wide, a factor of eight. Most of it is the transistor's
threshold spread, which a comparator would remove. I kept the bare
transistor, because every part added between the pin and the gate is another
part that can stick. The gate-drive detector is the one with the comparator
and the tighter window, 0.45 to 0.9 s here and 0.21 to 0.83 s over the
corners of review A2. Nothing requires the relay to open
after the triacs stop: if it opens first it breaks at most 1.5 A, inside the
contact rating, into the clamp D23. The earliest release, 0.12 s, is six
times the 20 ms the keep-alive contract allows between edges.

**Gate drive margin.** At the worst corner, -20 °C, a 3.2 V rail, the
smallest capacitors and the slowest allowed keep-alive of one edge per 20 ms,
the gate sits at 2.09 V. The highest threshold there is 1.59 V. The margin is
0.50 V. At the firmware's own 100 Hz it is 0.83 V. Review A2 took a harsher
corner: a threshold of 1.66 V at -20 °C, diodes at their highest forward
voltage, a pin of 70 ohm and the pull-down R33 at 2.2 kohm. There the margin
is 0.22 V with edges 20 ms apart and 0.55 V at the firmware's 5 ms, and
0.08 and 0.38 V with `3V3` at 3.0 V. It also found that a keep-alive with
one phase of 1 ms does not hold Q2 at that corner, because R89 and the pin
charge C51 with a time constant of about 3 ms. Section 19.4 now forbids a
phase shorter than 2.5 ms. The AO3400A data sheet
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

**What the read-backs make testable.** GPIO42 reads `V_DRV`, the LED supply
behind Q3, section 8.6.
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
| R33 | 2.2k 1% | 0603 | C4190 | `KA` to `GND` | Pull-down. Low enough that a net behind a 10 kohm pull-up cannot run the detectors through a bridge |
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
| R92 | 4.7k 1% | 0603 | C23162 | `V_DRV` to `V_DRV_RB` | Read-back of the LED supply to GPIO42. Taken behind Q3, so that a shorted Q3 shows |

<!-- designsync:end -->

### 8.2 How it works

Firmware toggles `KA`, GPIO17. Each rising edge pushes charge through C22 and
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
Both tables are from the two files as merged, with the pull-down R33 at
10 kohm. With 2.2 kohm most figures move by about 1 %; `hardware/sim/README.md`
has the comparison.

**Review A2, corner runs.** `hardware/sim/failsafe/` runs both detectors
together over tolerance, -20 to 70 °C, `3V3` from 3.0 to 3.6 V and five
keep-alive waveforms: review section 4. For this detector, with the
firmware's 5 ms edges:

| Question | Result over every corner |
|---|---|
| `V_DRV` on after the first edge | 13 to 22 ms |
| Gate drive removed after the last edge, pin low or high-impedance | 0.21 to 0.83 s |
| The same, pin stops high | 1.25 s at most |
| Reservoir while toggling, any waveform | 1.82 V at least, against 0.94 V to release |
| A steady level, one edge, one pulse | Never enables. One edge charges the reservoir to 1.64 V at most, at a corner where the turn-on threshold is about 1.7 V. |
| Slow toggling | 1 Hz enables in 2 of 24 corners, 2 Hz in 9, 3 Hz in 12, 5 Hz in 13. The row "slowest rate that enables" above is too firm in both directions. |

The short corner is C23 at 7 uF, C22 at 3.3 uF, the bleed 1 % low, `3V3` at 3.2 V
and +15 mV of comparator offset. The slow corner is C23 at 12 uF, C22 at
3.3 uF, a high level of 2.9 V on the pin, +15 mV of offset and -20 °C.

**Specification for firmware.**

- **Accepted**: an edge at least every 20 ms, and no phase shorter than 2.5 ms. The firmware's 5 ms is the design point. 10 Hz works with nominal parts and has too little margin at the slow corner to rely on. The lower limit on a phase is for the relay detector, section 7.3.
- **No upper limit**: the detector is a rectifier, not a window. A square wave at 50 kHz or 1 MHz enables drive as well as 100 Hz does. An earlier version of this section gave 5 kHz as a limit. That was an estimate and it was wrong.
- **Hold-up**: drive is removed 0.21 to 0.83 s after the last edge if the pin stops low, and at most 1.25 s if it stops high. These are the corners of review A2. The first version of this line had 0.45 to 0.9 s and 1.2 s.
- **Not accepted**: a steady level, high or low. Slow toggling is not reliably rejected. From 1 Hz up the result depends on part tolerances. Treat only a steady level as guaranteed off.
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

A and B share the keep-alive net with its pull-down R33, the ground and
nothing else. A uses `3V3` and `V5`. B uses no supply rail. Their pump
diodes are in separate packages, D7 and D22. So a fault in A leaves B to
open K1, and a fault in B leaves A to remove gate drive and release Q1.

**How it was checked.** The first version of this section went through the
parts by hand, with `hardware/sim/keepalive_faults.cir` and
`relay_detector.cir` for the two pumps. [Review A2](hardware-review-a2.md)
did not start from that table. It built the circuit from
`hardware/netlist.yaml` and injected every fault into a simulation of the
whole chain: each pin of each part opened, each pair of pins bridged that
the package allows, each IC output stuck, each net shorted to ground and to
`3V3`, leaks onto the reservoirs, bridges onto the keep-alive net, and the
supplies at wrong voltages. 376 faults, each run through the self-test
sequence of section 19.7 and through a crash with the pin stopping low,
high and high-impedance. The generator is `hardware/sim/failsafe/`, and the
complete table is appendix A of the review. Its section 3 lists where the
result differs from what stood here. This section is corrected to match.

**The result.** With the netlist as it is now:

- **No fault in a component leaves a valve on.** The only faults that do are bridges from the keep-alive net to a net that toggles by itself. They are layout faults and have their own part below.
- **57 faults stick one layer on.** The other layer turns the valve off in every one, and a step of the self-test finds every one. They are in the next table.
- **Every fault that sticks a layer on is found**, at the latest at the next start. Before review A2 three were not: a shorted Q3, an open R12, and a bridge of a few kilohms onto the keep-alive net.

**Run again with U13.** The answers to the firmware's questions added the
over-voltage detector U13 with four passive parts (section 9.5) and
lengthened step 2 to 2.0 s. The list was run again on that netlist with
that sequence: 397 faults, 395 of them simulated. The 57 faults that stick
one layer on are the same, with the same times and the same step finding
each, so the table below stands. The same three bridges from `KA` are the
only faults that leave a valve on. Of the 21 new faults, 7 hold the set
input of the latch low, so that drive never enables and the start test
fails, and 14 leave U13 unable to act and show nothing. One old row
changed. With R53 open the set input of the latch floats. In the model of
review A2 it stayed high and nothing showed. With the output of U13 on the
net it falls, the latch sets and drive never enables. On a board it floats
either way, and both ways are safe.

"Found by" names the step of section 19.7. The idle test is step 1, the
pin-high test step 2, the float test step 2b, the start test steps 3 and 4,
the weld check step 5, the gate bleed check step 5b, the run check steps 6
and 7 and the stop test step 9.

**States of the keep-alive pin and of the firmware.** The times are from
the corners of section 8.4: every tolerance, -20 to 70 °C, `3V3` from 3.0 to
3.6 V.

| Fault or state | What happens | Outputs |
|---|---|---|
| `KA` stuck low | No edges. Both reservoirs drain. | Gate drive is off within 0.84 s, and valve current ends with it. Q2 alone lets go within 1.46 s. |
| `KA` stuck high | C22 and C51 block DC. | Gate drive is off within 1.25 s, and valve current ends with it. Q2 alone lets go within 2.19 s. |
| `KA` floating: chip in reset, EN low, bootloader, or unprogrammed | R33 pulls it low. | As stuck low |
| `KA` toggling at the wrong rate | Section 8.4: from 1 Hz up A may enable, depending on tolerances. From one edge per 20 ms it always does. B needs about the same, and both phases at least 2.5 ms long. | Neither detector catches a wrong rate. |
| USB flashing | The ROM loader runs. GPIO17 stays an input. No edges. | Off |
| Boot | GPIO17 is driven low for 60 us at power-up, table 2-2 of the datasheet, which is its idle level. One edge enables neither A nor the valves: section 8.4. | Off |
| Brownout of `3V3` | The supervisor U12 holds the ESP32 in reset and toggling stops. The expander keeps its bits down to about 1 V. | Off as for stuck low. Until then the outputs that were on stay on. |
| Firmware hangs with outputs on | Toggling stops. | Off as above, counted from the last edge |
| I2C bus stuck by a fault | Firmware cannot write the expander. It must stop toggling. | Off as above, counted from that decision |
| GPIO40, GPIO42 or GPIO7 driven as an output by mistake | R87 and R94 keep each from moving the node it reads. GPIO42 driven high feeds `V_DRV` through R92, 0.7 mA at most, with no path through an LED while the expander is in reset. | No effect on the valves. The self-test fails, because the levels it reads are wrong. |

**Faults that stick one layer on.** From the fault injection, for the
netlist as it is. "Gate drive stays" means LED current still flows 4 s
after the keep-alive stopped, so detector A or what follows it is stuck. "Q1
stays on" means the expander still drives `RLY_ON`, or the gate of Q1 kept
its charge. "Q2 stays on" means detector B is stuck. The valve is off in
every row, by the other layer.

| Fault | What stays on | Valve current ends after: pin low / high / high-impedance (s) | Found while the valve runs | Found at the next start |
|---|---|---|---|---|
| C22 short | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | not found | 2 pin-high |
| R31 open | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 3 start |
| R32 short | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| R34 open | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| R35 short | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| R36 short | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U6 pin 1 1OUT (KA_OK) open | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U6 pin 3 VCC (V5) open | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U6 pin 7 1IN+ (KA_INP) open | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U6 output 1 (KA_OK) never pulls low | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pin 9 3A (KA_OK) open, holds its last level | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 3 start |
| U7 pin 9 3A (KA_OK) open, leaks high | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pin 11 4Y (DRV_EN) open | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 3 start |
| U7 pin 12 4A (DRV_EN_N) open, holds its last level | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pin 12 4A (DRV_EN_N) open, leaks low | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pin 13 4B (DRV_EN_N) open, holds its last level | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pin 13 4B (DRV_EN_N) open, leaks low | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pins 9 3A and 10 3B bridged (KA_OK to FAULT_N) | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 9 stop |
| U7 pins 10 3B and 11 4Y bridged (FAULT_N to DRV_EN) | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 pins 11 4Y and 12 4A bridged (DRV_EN to DRV_EN_N) | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 output 3 (DRV_EN_N) stuck low | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U7 output 4 (DRV_EN) stuck high | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U5 pin 3 RESET (DRV_EN) open, holds its last level | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 3 start |
| U5 pin 3 RESET (DRV_EN) open, leaks high | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U5 RESET input ignored, bits kept | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| U5 pin P05 (RLY_ON) stuck high | Q1 stays on | 0.50 / 0.76 / 0.50 | not found | 4 expander |
| K1 contact welded, or JP1 bridged | relay contact closed | 0.67 / 1.01 / 0.67 | not found | 5 weld check |
| R12 open | Q1 stays on | 0.50 / 0.76 / 0.50 | not found | 5b gate bleed |
| Q2 pin G (RLY_G2) open, holds its last level | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 3 start |
| Q2 pin G (RLY_G2) open, leaks high | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| Q2 pins G and D bridged (RLY_G2 to RLY_B) | Q2 stays on | 0.66 / 1.00 / 0.66 | 9 stop | 1 idle |
| Q2 pins S and D bridged (GND to RLY_B) | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| C51 short | Q2 stays on | 0.55 / 1.01 / 0.55 | not found | 2 pin-high |
| net DRV_EN shorted to 3V3 | Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net DRV_EN_N shorted to GND | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net KA_INP shorted to 3V3 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net KA_N1 shorted to 3V3 | gate drive stays, Q1 stays on | 0.48 / 0.74 / 0.48 | 9 stop | 1 idle |
| net KA_N2 shorted to 3V3 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net KA_OK shorted to 3V3 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net KA_REF shorted to GND | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| net KB_N1 shorted to 3V3 | Q2 stays on | 0.66 / 0.99 / 0.66 | 9 stop | 1 idle |
| net RLY_A shorted to GND | relay stays closed | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| net RLY_A shorted to 3V3 | relay stays closed | 0.14 / 0.14 / 0.14 | 9 stop | valve current |
| net RLY_B shorted to GND | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| net RLY_G1 shorted to 3V3 | Q1 stays on | 0.50 / 0.76 / 0.50 | not found | 4 expander |
| net RLY_G2 shorted to 3V3 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| net RLY_ON shorted to 3V3 | Q1 stays on | 0.50 / 0.76 / 0.50 | not found | 4 expander |
| leak of 47 kohm from 3V3 to KA_N2 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 2 pin-high |
| leak of 100 kohm from 3V3 to KA_N2 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 9 stop |
| leak of 100 kohm from 3V3 to KA_INP | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 1 idle |
| leak of 330 kohm from 3V3 to KA_INP | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 2 pin-high |
| leak of 47 kohm from 3V3 to RLY_G2 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| leak of 100 kohm from 3V3 to RLY_G2 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 2 pin-high |
| leak of 100 kohm from V_BUCK to RLY_G2 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 1 idle |
| leak of 330 kohm from V_BUCK to RLY_G2 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 9 stop |
| leak of 100 kohm from 3V3 to KB_N1 | Q2 stays on | 0.67 / 1.01 / 0.67 | 9 stop | 2 pin-high |
| leak of 100 kohm from 3V3 to KA_N1 | gate drive stays, Q1 stays on | 0.50 / 0.76 / 0.50 | 9 stop | 9 stop |

Two faults belong in this table and are not in it, because of how the model
works.

- **U6 with its ground pin open.** The model's comparator finds ground through its input diodes and keeps working. A real LM2901 without ground cannot be relied on to pull `KA_OK` low. Count it as A stuck on, covered by B and found by the idle test.
- **Q1 shorted drain to source.** The model judges Q1 by its gate voltage. A shorted Q1 conducts whatever its gate does, K1 follows Q2 alone, and the weld check finds it.

Rows that the first version of this section had wrong or did not have:

- **R35 shorted.** The reference of the comparator is then at 0 V and `KA_OK` is high from power-up: A stuck on, exactly as with R34 open. The first version listed it with the faults that keep A from enabling.
- **Inputs left floating.** Pin 7 of U6, the comparator input, drifts high on its own bias current: A stuck on. Pin 9 of U7 and the reset pin of U5 can float either way. The first version covered IC outputs and supply pins, not inputs.
- **The gate of Q2 open.** It does not always mean "K1 never closes". A gate that opens while it is charged, or that leaks towards a rail, holds Q2 on.
- **R12 open.** Listed as found by the weld check. It is not: at the end of a normal run firmware drives `RLY_ON` low before it releases the pin, which discharges the gate of Q1 with or without R12. Only a reset with `RLY_ON` high leaves the gate charged. Step 5b reproduces that on purpose.
- **Q3 shorted.** Not found before. Found by the idle test since R92 reads `V_DRV`.
- **U7 output 4 stuck high, and U5 ignoring its reset.** Listed as "A stuck on". The LED supply still switches off, because Q3 follows output 3. What is lost is the expander reset, and with it the release of Q1.
- **Adjacent pins of U7 bridged.** Pins 9 and 10 (`KA_OK` to `FAULT_N`) and pins 11 and 12 (`DRV_EN` to `DRV_EN_N`) each stick A on. Pins 2 and 3, 5 and 6, and 8 and 9 tie a gate output to its own input. The model oscillates there and those runs did not finish; a real gate would oscillate or sit between levels. In none of the three can `DRV_EN_N` rest low, so they are counted as "drive not reliably enabled", and they are found at the start test.

**Output devices.**

| Fault | Valves after the keep-alive stops | Found by |
|---|---|---|
| Triac, TVS D101, opto-triac output or snubber capacitor shorted | K1 opens through Q1 and through Q2, 0.84 s after the last edge at the latest, 1.25 s if the pin stops high | Step 6: current with K1 closed and every output off |

**What A and B share.**

| Shared item | Fault | Effect | Answer |
|---|---|---|---|
| The pin, GPIO17 | Stuck at either level, or floating | No edges reach either detector. | Safe. First table. |
| The `KA` net | Bridged to a net that toggles with no control loop behind it | Both detectors are fed at once. | The one kind of board fault that defeats both. Next part. |
| R33 | Open | The net floats when the pin does. Nothing drives it, so no edges. But the protection R33 gives against a bridge is gone. | Not found. The float test would find the bridge. |
| The firmware | Keeps toggling the pin while the control loop is dead | Both detectors stay on. | Not a component fault. The contract of section 19.4 exists for it. |
| `3V3` | Low or absent | The ESP32 is held in reset. U7 has no supply and Q3 no source voltage. | Safe |
| `3V3` | High: U3 shorted input to output, 4.82 to 5.07 V | Both detectors still let go, if the keep-alive stops. The processor is far above its absolute maximum of 3.6 V and may not stop it. So U13 holds the set input of the over-current latch low while `3V3` is above 4.04 V, and `DRV_EN` is then low whatever the pin does: no LED supply, the expander in reset, Q1 off. An erratic pin can still pump detector B, and Q2 alone closes nothing. | Safe by simulation, section 9.5. The processor and the display are not protected. Risk 32. |
| `V5` | Absent | No `3V3` either. | Safe |
| `V_BUCK` | Absent | The coil of K1 has no supply. | Safe |
| `V_BUCK` | High: the switch of U1 shorted | D25 holds it under 10 V until F5 opens. `3V3` stays in regulation. Then `V_BUCK` is absent. | Safe by calculation and simulation, section 4.8. Risk 21. |
| Ground | Lost between the two detectors | A layout fault, not a component fault. | One ground plane |
| Diode packages | D7 fails in any way | Only A is affected. D22 is a separate package. | Safe |
| Same part type | C22 and C51 are the same 4.7 uF part, C23 and C50 the same 10 uF part | A cracked ceramic capacitor fails short, and board flex can crack two at once. Two shorted pump capacitors is a double fault, and each one alone is found by the pin-high test. | Place the two pumps apart and turn C22 and C51 at right angles to each other. |
| K1 | Welded | Gate drive still goes. | Weld check |

**The bridged `KA` net.** A bridge from `KA` to a net that toggles by
itself feeds both detectors with no firmware behind it. The first version
of this section named the backlight and the zero-cross signal. Review A2
found that it had missed the net next to the pin, and had the zero-cross
case half wrong.

What matters is the state of the processor pin. While firmware runs, the
pin is driven, low when idle, and it takes a bridge of a few ohms to
overpower it. While the processor is in reset or booting, the pin is an
input and only R33 holds the net. That is the state the fail-safe exists
for: a crash, a watchdog reset, a boot loop.

Nets that toggle with no control loop:

- **The PSRAM lines, GPIO35 to GPIO37.** They toggle whenever the processor reads the PSRAM, which the boot code does on every start. The keep-alive used to be on GPIO38, module pin 31, next to GPIO37 on pin 30. With that netlist a bridge of 0 ohm kept the valve on in every case, and a bridge of 1 or 4.7 kohm kept it on whenever the pin was high-impedance. The self-test of the time found the 0 ohm bridge and not the others. This is why the keep-alive moved to GPIO17.
- **`TFT_BL`**, the backlight PWM, 1 to 20 kHz from the LEDC peripheral. The same result as a PSRAM line.
- **`U0TXD`**, which carries the boot log after every reset.
- **`AC_ZC`**, 50 or 60 Hz whenever 24 VAC is present. It is an open-collector output with a 10 kohm pull-up. The first version found, with `hardware/sim/ka_bridge.cir`, that this bridge cannot enable A and can hold B on, and concluded "one detector, not both". That file's own numbers say otherwise. It puts A's reservoir at 1.0 V. A does not turn on below 1.55 V, but once on it does not turn off above 0.94 V. A valve that is running when the processor resets has A on already. With R33 at 10 kohm, as it was, the fault injection kept both detectors on at 27 and 70 °C for a Q2 from the lower third of its threshold range.
- **`SENS_FLOW`**, pulses from the flow meter while water runs. A valve that is stuck open makes its own flow pulses.
- **`ADC_LO` and `ADC_HI`**, the current waveform, at mains frequency while a valve draws current.
- **The USB data lines**, driven by the host.
- **The display clock and data**, if the display task outlives the control loop.
- **The buck switch node.** A bridge to it destroys the pin.

Three things were done.

1. **The pin moved.** GPIO17, module pin 10. Its neighbours are `ENC_B` and `BTN_BACK`, and the next pins out are `ENC_A` and `I2C0_SDA`. A contact behind 13 kohm cannot run either detector: the fault injection bridges `KA` to a contact that opens and closes at 20 Hz and both detectors stay off.
2. **R33 went from 10 kohm to 2.2 kohm.** A net behind a 10 kohm pull-up, such as `AC_ZC`, now lifts `KA` to 0.6 V at most. With the `AC_ZC` bridge and 2.2 kohm, `hardware/sim/ka_bridge.cir` puts A's reservoir at 0.40 V at most and the gate of Q2 at 0.36 V, where they reached 1.03 and 0.95 V. A push-pull output still gets through. A bridge from `TFT_BL` of up to about 3.3 kohm holds both detectors while the pin is high-impedance. The pull-down costs 1.5 mA while the pin is high.
3. **The self-test floats the pin.** Step 2b makes the pin an input for half a second with the display, the backlight and the PSRAM in use, and fails if the pin reads high or either read-back moves. In the fault injection it finds the 1 kohm bridges that every other step misses.

The sweep of `hardware/sim/failsafe/results/bridges.md`, for the netlist as
it is. Each row is run at four corners that make the detectors easy to hold.
The fifth column is the sequence before review A2 and the sixth the sequence
of section 19.7. In every case where a valve stays on, step 2b or the idle
test reports the bridge.

| Net bridged to `KA` | Bridge | Pin driven low after the stop | Pin high-impedance after the stop | Reported by the sequence as merged | Reported by the sequence with step 2b |
|---|---|---|---|---|---|
| push-pull, 3.3 V | 0 ohm | **valve stays on** in 4 of 4 corners | **valve stays on** in 4 of 4 corners | every corner: 1 idle | every corner: 1 idle |
| push-pull, 3.3 V | 100 ohm | detector B held, valve off | **valve stays on** in 4 of 4 corners | 2 of 4 corners: 1 idle | every corner: 1 idle, 2b float |
| push-pull, 3.3 V | 470 ohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 1 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 2.2 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 3.3 kohm | both let go | **valve stays on** in 1 of 4 corners | not reported | 3 of 4 corners: 2b float |
| push-pull, 3.3 V | 4.7 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| push-pull, 3.3 V | 10 kohm | both let go | detector B held, valve off | not reported | not reported |
| zero-cross signal AC_ZC | 0 ohm | both let go | detector B held, valve off | 1 of 4 corners: 2 pin-high | 2 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 100 ohm | both let go | detector B held, valve off | 1 of 4 corners: 2 pin-high | 2 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 470 ohm | both let go | detector B held, valve off | not reported | 2 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 1 kohm | both let go | detector B held, valve off | not reported | not reported |
| zero-cross signal AC_ZC | 2.2 kohm | both let go | both let go | not reported | not reported |
| zero-cross signal AC_ZC | 3.3 kohm | both let go | both let go | not reported | not reported |
| zero-cross signal AC_ZC | 4.7 kohm | both let go | both let go | not reported | not reported |
| zero-cross signal AC_ZC | 10 kohm | both let go | both let go | not reported | not reported |

"Detector B held" means that Q2 still conducts 4 s after the stop, by its
read-back or by its gate voltage. In the zero-cross rows that happens only
in the two corners at 70 °C, where the threshold of Q2 has fallen to 0.46 V.
Detector A lets go in every zero-cross row, so no valve stays on.

Layout rules that follow:

1. Route `KA` from module pin 10 to R33, R29 and R89 with ground on both sides, at least 1 mm from any other signal. Put R33 at the module pin. Keep the net away from every net in the list above, on every layer.
2. Put no test pad on `RLY_G2`, `KB_N1`, `KA_N1` or `KA_N2`'s diode side that a probe or a flux residue could bridge to a rail. TP8 on `KA_N2` stays, with ground around it.
3. Surround the gate node of Q2 with ground. The fault injection puts numbers on a leak from a rail to that node. 100 kohm from `3V3` or `V_BUCK` holds Q2 on. 330 kohm from `V_BUCK` doubles its release time and holds a Q2 with the lowest threshold. 330 kohm from `3V3` or 1 Mohm from `V_BUCK` makes it let go about a fifth later, which no test sees. The same for A's reservoir: 100 kohm from `3V3` to `KA_N2` holds A on, and 220 kohm makes it let go after 1.19 s where it takes 0.67 s.
4. Place the A pump (R29, C22, D7) and the B pump (R89, C51, D22) at least 10 mm apart.
5. Do not move the keep-alive to another pin without redoing this list for its neighbours, on the module edge and on the chip's own pin row.

**The regulator.** If the switch inside U1 shorts, `VIN_DC` reaches
`V_BUCK`. The first version of this section had no answer. Section 4.8 has
one now. A clamp and a fuse keep every part of the fail-safe circuits inside
its ratings, so the analysis above holds during the fault, and afterwards
the coil of K1 has no supply.

**The LDO.** The same fault in U3 puts `V5` on `3V3`. The expander, U7 and
the op-amp take that voltage. The processor and the display do not. Review
A2 left this open as finding M5, and the owner decided to accept the risk to
the processor for revision A if the valves are off in that fault whatever
the processor does. Section 9.5 shows that they were not, and adds the
detector U13, which removes drive by hardware while the rail is high.

**Resistor shorts.** A thick-film chip resistor does not normally fail
short, but a solder bridge across its pads is the same thing. The fault
injection ran every one. The rows that matter are in the table above: R32,
R35 and R36 each stick A on. R93 shorted puts `V_BUCK` on the drain of Q2,
which shorts the supply when Q2 turns on. The rest keep a detector from
enabling or change nothing.

**Faults that no test finds.** 120 of the 374 simulated
faults lose no layer and fail no step of the sequence as the model runs it.
With U13 in the netlist it is 133 of 395: R53 open is now found, and 14
faults of the new parts are not. None of them leaves a valve on. They are
of these kinds.

- **The over-voltage detector cannot act.** U13 with its output open or never pulling, without supply or ground, or with its input pin open and leaking low. R13 open. R100 or C55 shorted. Pins 5 and 6 of U13 bridged, which also keeps the over-current latch from setting. No test can raise `3V3`, so nothing reports these. Each matters only if U3 shorts as well. C55 or C56 open and pin 3 of U13 open change nothing. Section 9.5.
- **The other channels.** The model runs zone 1 only. The LED resistors, opto LEDs and indicator LEDs of the master valve and of the indicator of zone 1 show when their own output runs: no valve current, or no light.
- **The zero-cross input.** R55 to R58, R88, D9, C34, the fourth comparator of U6 and the nets between them. The model does not run step 0. By the wording of step 0, a board with no zero-cross edges does not start a run.
- **The over-current latch cannot set, or sets on one polarity only.** With `OC_TRIP_N` or `FAULT_N` held high, `FAULT` held low, or pin 1 of U7 open and floating high, the latch cannot set. With one of the two comparator outputs never pulling low, or one threshold resistor shorted, it has lost one polarity, and an AC over-current still sets it on the other half-cycle. This is from reading the logic. The fault injection ran no over-current together with these faults, which would be two faults. Nothing reports any of them. The PTC still carries a shorted output, section 6.6. This is outside the keep-alive fail-safe. Step 7 of section 21 tests the latch once at bring-up, and nothing tests it in service.
- **Protection lost with no symptom.** R33 open, which is the pull-down that keeps a bridged net from running the detectors. R92, R87 or R94 shorted, each the resistor that keeps a mis-driven read-back pin from moving its node. D5 open, the flyback path of the coil.
- **Hysteresis and filters.** R31 shorted, R32 or R36 open, and the filter and decoupling capacitors open. Detector A then lets go 0.05 to 0.08 s earlier with typical parts, inside every limit.
- **U7 with its supply pin open.** The model powers U7 through the clamp diodes of its inputs, from R36 and R53, at about 2.7 V, and both detectors still release. Before review A2 the read-back on `DRV_EN` saw the reduced level. The read-back on `V_DRV` does not. What a real 74HC132 does there is a bench question.
- **Series resistors shorted.** R29, R89, R11, R52 and R53. The pin and the gates limit the current without them.
- **Harmless bridges.** `KA` to `ENC_B` or `BTN_BACK` holds no detector. The firmware would see key presses whenever the keep-alive runs. `KA` to `AC_ZC` holds no detector either, now that R33 is 2.2 kohm, and loads the zero-cross signal to 0.6 V, which stops step 0.
- **Small leaks.** 330 kohm from `3V3` or 1 Mohm from `V_BUCK` onto the gate of Q2 make it let go about a fifth later, inside the band of step 9.
- **No effect.** R95 open: GPIO7 still reads both levels, with 15 uA into its clamp diode.

**Summary.** No single component fault leaves a valve on after the
keep-alive stops. Every fault that sticks one of the two layers on is found
by the self-test, at the latest at the next start, while the other layer
still covers it. What is left is of two kinds. A bridge from the keep-alive
net to a push-pull net that toggles by itself still defeats both layers if
it is under about 3.3 kohm. The layout must exclude it and step 2b looks
for it. And a processor outside its ratings. The clamp on `V_BUCK` prevents
that for a failed buck converter. For a failed LDO nothing prevents it, and
U13 keeps the valves off while it lasts. The analysis is in simulation with behavioural models. Nothing here
has been built, and section 21 has the bench tests that repeat it on
hardware by opening and shorting the parts one at a time.

### 8.6 Read-back of the gate drive

R92, 4.7 kohm, takes `V_DRV` to GPIO42. `V_DRV` is the opto LED supply
behind Q3. It is high only when detector A is on, the over-current latch is
clear and Q3 conducts, so it shows the end of the chain and not a signal in
the middle of it. `OC_FAULT_N` on GPIO40 tells a latch trip from a detector
that has let go.

**Changed by review A2.** Until then R92 was 10 kohm and came from
`DRV_EN`, the line that resets the expander. Two things were wrong with
that.

- **A shorted Q3 was found by nothing.** `DRV_EN` falls whether or not Q3 turns off. `V_DRV` does not: with Q3 shorted, or its gate held low, the read-back stays high with no keep-alive, and the idle test fails. What the old connection showed and the new one does not is `DRV_EN` itself. That is covered by the other half of the same tests: the expander answers on I2C exactly when `DRV_EN` is high, and the idle, start and stop tests all address it.
- **The high level had no margin that the data sheet backs.** Firmware enables the internal pull-down on GPIO42, so that an open R92 reads low. The ESP32-S3 data sheet gives that pull-down as 45 kohm typical with no limits, and the input high level as 0.75 x VDD. Through 10 kohm the pin sat at 3.3 V x 45 / 55 = 2.70 V against 2.48 V needed: a pull-down of 30 kohm, a third under typical, would have failed the start test on a healthy board. Through 4.7 kohm it is 2.99 V, and the pull-down may be as low as 14 kohm.

R26, 10 kohm, pulls `V_DRV` to ground when Q3 is off, so the low level is
defined on the board. The leakage of Q3, 5 uA at 55 °C from its data sheet,
raises it by 40 mV.

A GPIO42 driven high by mistake now feeds `V_DRV` through 4.7 kohm: 0.7 mA
at most. It cannot light an opto LED by itself, because with detector A off
the expander is in reset and no pin sinks current. GPIO42 is one of the
JTAG pins and is free because JTAG runs over the USB controller.

With the read-back firmware measures how long A takes to release after
every run, which is how an open R30 or R91 is found. `V_DRV` falls with a
time constant of 1 ms behind `DRV_EN`, which does not matter against limits
of 0.15 to 1.0 s.

### 8.7 Boot behaviour of the pins

The reset state and power-up glitch of every GPIO are in `hardware/pinmap.yaml`,
taken from tables 2-1 and 2-2 of the ESP32-S3 datasheet. The ones that matter
here:

| Pin | Use | At reset | Power-up glitch | Consequence |
|---|---|---|---|---|
| GPIO17 | `KA` | High impedance | Driven low for 60 us | R33 holds it low. The glitch is the idle level. |
| GPIO8, GPIO9 | I2C0 | High impedance | Driven low for 60 us | Not a valid I2C write. The expander is in reset anyway. |
| GPIO7 | Relay detector read-back | High impedance | Driven low for 60 us | 100 kohm in series. It cannot move the node it reads. |
| GPIO42 | Gate-drive read-back | High impedance | None | 4.7 kohm in series to `V_DRV`. Section 8.6. |
| GPIO38 | Encoder switch | High impedance | None | Input only |
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

### 9.5 Over-voltage on `3V3`: detector U13

Added with the [answers to the firmware's questions](hardware-answers-a2.md),
section 26. Review A2 left finding M5 to the owner: a shorted U3 puts about
5 V on the processor. The owner's decision was to accept that for revision A
as a documented risk, provided the valves are off in that fault whatever the
processor does, and to add the smallest fix if they are not. They were not.
This section has the evidence and the fix. The parts are in the table of
section 10.1.

**The fault.** U3 fails with its input joined to its output, and `3V3` goes
to `V5`. On 24 VAC that is 4.82 to 5.07 V in the simulation, which takes
0.3 V for D3. With 0.5 V in D3 and the lowest `V_BUCK` it is 4.66 V. On USB
alone it is 4.1 to 4.9 V, and no valve can run there, because K1 has no coil
supply.

**What is on the rail.**

| Part | Rating | At 5.07 V |
|---|---|---|
| U5, TCA9539 | 1.65 to 5.5 V, 6 V absolute (TI SCPS202C) | Works |
| U7, 74HC132 | 2.0 to 6.0 V, 7 V absolute (Nexperia rev 8) | Works |
| U8, MCP6002 | 1.8 to 6.0 V, 7.0 V absolute (Microchip DS20001733L) | Works. The bias and the trip thresholds are ratios of `3V3` and rise with it: the trip level becomes 7 A peak |
| U6, LM2901 | On `V5`, 36 V | Works. `KA_INP` goes above the common-mode range of U6A. The data sheet allows that while the other input, `KA_REF` at 2.03 V, stays inside it, and when the comparator decides both are |
| U10, PCF8563 | 6.5 V absolute (NXP rev 11.1) | Works |
| U12, TLV803E | 6.5 V absolute | Does nothing. It watches for a low rail |
| Opto LEDs | 60 mA continuous | 24 mA each where it was 12 mA: (5.07 - 1.2 - 0.3) V / 150 ohm |
| U13, TPS3701 | 36 V | Works |
| **U4, ESP32-S3** | **3.6 V absolute (datasheet v2.2, table 5-1)** | **Not known** |
| **DS1, the display** | **4.6 V absolute** | **Not known** |
| A device on the Qwiic connector | Its own | The owner's risk |

**What the hardware does.** `hardware/sim/failsafe/ldo.py` runs the fault in
945 cases; the tables are in `hardware/sim/failsafe/results/ldo.md`. Zone 1
runs, U3 shorts, and the processor then does one of the things in the first
column. "Without U13" is the board as review A2 left it.

| The processor | Without U13 | With U13 |
|---|---|---|
| Dead. The pin stops low, high or high-impedance 0.5 s after the fault and stays there | Valve off in 48 of 48 corners. Current ends 0.84 s after the stop at the latest, 1.25 s if the pin stops high: the figures of 3.3 V, because detector A compares ratios of the rail | Valve off 0.5 ms after the fault |
| Erratic. The pin keeps toggling at 1 kHz, 100 Hz or 25 Hz, or lopsided at 1 ms and 20 ms, with nothing behind it | **Valve stays on** in every corner | Valve off 0.5 ms after the fault |
| Erratic, toggling at 5 Hz | **Valve stays on** in 3 of 4 corners | Valve off 0.5 ms after the fault |
| Dead, with the keep-alive pin joined to its supply inside the chip, and loading the rail in bursts so that the rail swings between 2.0 V and `V5` | **Valve stays on** at 50 and 200 Hz in every corner and at 10 Hz in 1 of 4 | Valve off 0.5 ms after the fault |
| No edges. U3 is shorted from power-up and no valve is asked for | A steady pin, one edge or one pulse enables nothing, in 24 of 24 corners. Toggling at 3 Hz enables drive in 10 corners and at 5 Hz in 20. No valve follows, because nothing writes the expander | Nothing enables at any rate |
| The fault arrives 3.4 s after the keep-alive stopped | Nothing enables. A pin that follows the rail up makes one edge of 1.6 V, and the reservoir of detector A reaches 0.61 V | The same |

So the answer to the owner's question is no. With a dead processor the
valves are off, as they are at 3.3 V. With a processor that still moves the
keep-alive pin they stay on, and the hardware of review A2 cannot tell that
pin from working firmware. A processor at 140 % of its absolute maximum may
do either. The fourth row is a way for a dead chip to make edges as well. I
cannot rule it out and I do not say it is likely.

**The fix.** U13 is a TI TPS3701DDCR, LCSC C132788, 24,964 in stock at
JLCPCB on 2026-11-10, 0.39 USD at 100. From TI SBVS240C: two comparators
with a 400 mV reference and open-drain outputs, a supply range of 1.8 to
36 V and 11 uA of supply current at most. Comparator B pulls OUTB low while
INB is above 400 mV.

- **R13 and R100**, 1 Mohm and 110 kohm, divide `3V3` by 10.09 onto INB, net `OV_DIV`.
- **C55**, 10 nF, on `OV_DIV` to ground.
- **OUTB** is on `OC_TRIP_N`, the set input of the over-current latch, beside the open-collector outputs of U6B and U6C. R53 is its pull-up.
- **C56**, 100 nF, at the supply pin.
- **Comparator A is not used.** INA is at ground and OUTA is not connected.

Both resistor values and both capacitor values are on the BOM already. U13
is the one new line.

**Why it removes drive.** With `OC_TRIP_N` low, U7A drives `FAULT` high.
U7B then makes `FAULT_N` the inverse of `KA_OK`, and U7C makes `DRV_EN_N`
the NAND of `KA_OK` and its inverse, which is high whatever `KA_OK` is. So
`DRV_EN` is low for as long as U13 pulls. Q3 is off, the expander is in
reset and Q1 with it. This does not rely on the latch remembering anything,
and it needs only U7, which is inside its ratings. The keep-alive pin has no
say in it. D8 is lit meanwhile.

**Why this node and not `EN`.** Review A2 proposed a detector that holds the
processor in reset. That asks a chip outside its ratings to obey its reset
pin, and it does nothing about a pin that has failed inside the chip.
Removing drive needs nothing from the processor.

| Property | Calculation | Value |
|---|---|---|
| Threshold | 0.400 V x (1 Mohm + 110 kohm) / 110 kohm | 4.04 V |
| Tolerance | Threshold 397 to 403 mV, two 1 % resistors, 25 nA of input current in 99 kohm | 3.91 to 4.17 V. The simulation steps the rail and finds the trip between 3.89 and 4.19 V |
| Hysteresis | 5.2 mV typical and 12 mV at most at the pin | 0.05 V typical and 0.12 V at most at the rail |
| Room below | Highest `3V3` in normal use, 3.40 V | 0.51 V |
| Room above | Lowest `3V3` with U3 shorted on 24 VAC, 4.66 V | 0.49 V |
| Delay | R13 in parallel with R100, 99 kohm, with C55: 1.0 ms. A step from 3.3 to 4.9 V crosses the threshold after 0.6 ms. U13 itself answers in 10 us | Valve current ends 0.5 ms after the fault in the simulation, plus the half-cycle a triac conducts |
| A spike on `3V3` | The same 1.0 ms | A spike to 5 V shorter than 0.5 ms does not reach the threshold |
| While `3V3` ramps | OUTB is high-impedance below the under-voltage lock-out of 1.3 to 1.7 V, and below 0.8 V | No pull at power-up |
| Load on `3V3` | 3 uA in the divider, 11 uA in U13 | |

**Single faults of the new parts**, from the fault injection. None can turn
a valve on, because all U13 can do is pull the set input of the latch low.

| Fault | Effect | Found by |
|---|---|---|
| Output of U13 shorted low, R13 shorted, R100 open, `OV_DIV` bridged to `3V3`, pin 4 open and leaking high | The latch input is held low. Drive never enables and D8 is lit | The start test: no gate-drive read-back, and `OC` goes low at the first edges |
| Output of U13 open or never pulling, U13 without supply or ground, R13 open, R100 or C55 shorted, pin 4 open and leaking low | U13 cannot act. Everything else works | Nothing. It matters only if U3 shorts as well |
| Pins 5 and 6 of U13 bridged, `3V3` to `OC_TRIP_N` | U13 cannot act, and the over-current latch cannot set either: the same as R53 shorted | Nothing. Section 9.4 |
| C56 shorted | `3V3` is shorted | The board does not start |
| C55 or C56 open | No effect that the model shows | Nothing |

**What firmware sees.** At rest `OC` reads high, because `FAULT_N` follows
the inverse of `KA_OK`, and D8 is lit. At the first edges of step 3 `KA_OK`
goes high, `OC` goes low and `A_RB` never goes high: a trip with no valve
bit low. If U3 shorts during a run, the valves stop and `OC` goes low at
once. The rest level of the current channels, 0.24 times `3V3`, tells
firmware that the rail is high, if the processor still works. Rule 18 of
section 19.1 and section 19.8 say what firmware does with both.

**What this does not do.**

- **It does not protect the processor or the display.** They see 4.7 to 5.1 V for as long as the fault lasts and may be destroyed. That is the risk the owner accepted.
- **It does not act between 3.6 V and its threshold.** A regulator that drifts to 3.8 V is outside the processor's rating and inside U13's window. Rule 18 reports it from 3.63 to 3.92 V upward, if firmware runs.
- **Nothing tests it in service.** No test can raise `3V3`. Step 7 of the bring-up plan tests U13 and the path behind it once, with a resistor across R13 and without raising the rail. The threshold itself rests on the data sheet and two 1 % resistors. Risk 32.
- **It does not open K1 through detector B.** An erratic pin keeps Q2 on. K1 opens because Q1 loses its gate drive.

**For the layout.** New parts U13, R13, R100, C55 and C56. New net
`OV_DIV`. `OC_TRIP_N`, `3V3` and `GND` each gain pins. Put R13, R100 and
C55 at pin 4 of U13 and keep `OV_DIV` short, away from `SW`, `KA` and the
display lines: it is a 99 kohm node. C56 goes at pin 5. U13 belongs near
U7, whose pin 1 it drives. No rule of section 8.5 changes.

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
| U13 | TPS3701DDCR window voltage detector, 0.4 V thresholds, open-drain outputs | SOT-23-6 thin | C132788 | 1 OUTA NC, 2 GND `GND`, 3 INA `GND`, 4 INB `OV_DIV`, 5 VDD `3V3`, 6 OUTB `OC_TRIP_N` | Holds the set input of the latch low while 3V3 is above 4.0 V, so that a shorted U3 leaves no drive. Section 9.5 |
| R13 | 1M 1% | 0603 | C22935 | `3V3` to `OV_DIV` | Over-voltage divider, 1/10.09: 0.4 V at the pin with 3V3 at 4.04 V |
| R100 | 110k 1% | 0603 | C25805 | `OV_DIV` to `GND` |  |
| C55 | 10 nF 50 V X7R | 0603 | C1589 | `OV_DIV` to `GND` | With the divider, 1 ms: a spike on 3V3 does not set the latch |
| C56 | 100 nF 50 V X7R | 0603 | C14663 | `3V3` to `GND` | At U13 |

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

**What moved after review A2.** Two pins changed places: the keep-alive
went from GPIO38 to GPIO17 and the encoder switch from GPIO17 to GPIO38.
GPIO38 is module pin 31. Pin 30 beside it is GPIO37, which this document
called "not connected". On the N16R8 it is not free: it is the data strobe
of the octal PSRAM (ESP32-S3 datasheet v2.2, table 2-14), wired inside the
module, and it toggles whenever the processor reads the PSRAM, with or
without a control loop. Review A2 showed in simulation that a bridge of up
to about 5 kohm between the two pins keeps both detectors on while the
keep-alive pin is high-impedance, which is what it is during every boot,
and that no step of the self-test found it. GPIO17 is module pin 10. Its
neighbours are `ENC_B` on pin 9 and `BTN_BACK` on pin 11, contacts behind
13 kohm, and the pins beyond those are `ENC_A` and the I2C data line. The
same holds for the chip's own pins inside the module. The read-back on
GPIO42 now reads `V_DRV` and not `DRV_EN`, section 8.6.

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
| GPIO45 | Flash voltage | Low for 3.3 V, on a chip whose eFuses leave the choice to the pin | `SENS_PWR_EN`: R86, 100 kohm to `GND`, the enable input of U11 and test pad TP25 | Two reasons, either of which is enough. Nothing on the net can source current: one pull-down and one CMOS input, so with the internal pull-down the strap reads 0. And on this module the strap is not used: see below. Firmware drives the pin only after boot. Never add a pull-up or a second driver to this net. |
| GPIO46 | Boot mode, ROM messages | Low | R20 and LED D6 to `GND` | The LED path can only pull down. |

**GPIO45, checked in review A2.** The strap selects the voltage of
`VDD_SPI`, the supply of the flash and the PSRAM: 3.3 V with the pin low,
1.8 V with it high (ESP32-S3 datasheet v2.2, table 3-4). The flash and the
octal PSRAM of the N16R8 are 3.3 V parts (table 1-1: ESP32-S3R8, `VDD_SPI`
3.3 V). At 1.8 V they would not be damaged; the module would not boot, and
a module that does not boot makes no keep-alive edges.

- **The pull state.** Table 2-1 gives GPIO45 a weak pull-down with the input enabled, at reset and after it. R86 adds 100 kohm. The latch samples the pin when reset is released and wants it held for 3 ms after that (table 3-2); U12 releases reset 130 ms or more after `3V3` is good, and the net has a time constant under a microsecond.
- **A reset while the sensor supply is on.** Firmware then drives the pin high. Any reset that samples the straps also returns the pin to an input with its pull-down, so the pin is low long before the strap is read.
- **The module's eFuses.** The module data sheet v1.8 says, in the note on its schematic in section 8: "For modules with PSRAM, the VDD_SPI voltage is fixed to 3.3 V or 1.8 V via eFuse, so their VDD_SPI voltage will not be affected by the GPIO45 level." So on the N16R8 the pin's level at reset does not matter at all. Confirm it on the first modules with `espefuse.py summary`: `VDD_SPI_FORCE` must read 1. It is step 2 of the bring-up plan.
- **What could still pull it high.** U11 failing with its input joined to its enable pin. Then the pin sees `V_BUCK`, the strap would read 1 on a chip without the eFuse, and the processor pin is over-driven whatever the eFuse says. Section 4.8 has the resistor that would bound that; it is not fitted.

So the assignment stays.

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
board and suits J7 as chosen. The mechanical contract uses that fold. Section
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

The encoder switch is on GPIO38 since review A2, in exchange for the
keep-alive. Each of the five contacts has a 3.3 kohm pull-up, which gives the 1 mA the
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

### 16.4 Agreement with the mechanical contract

`hardware/mechanical.yaml` and the [mechanical design](mechanical.md) fix the
board outline, the position of every part the enclosure touches and the
height allowed over each region. This section follows **revision 2** of that
contract, which is on `main`. An earlier version of this section quoted
revision 1, where everything above y = 45 was 10 mm lower.

**Display connector J7, resolved.** The contract mounts the panel on a
carrier above the board, landscape, with its FPC edge towards +x. The tail
makes a C-fold under the panel and enters J7 travelling in -x. J7's body
centre is at x = 60.44, y = 88.43, rotation 0, with its mouth at x = 63.0.

- **Contact side.** The panel's contacts are on the display face. After the fold that face looks at the board. J7 is the bottom-contact part, FPC-05F-12PH20, as chosen. Consistent.
- **Pin 1.** The contract puts panel pin 1 at y = 85.68, the contact nearest the terminal-block edge, and pin 12 at y = 91.18. In the netlist J7 pin 1 is panel pin 1, `GND`. So the footprint's pad 1 is the pad at y = 85.68: **the left end of the connector when you look into its mouth with the board component side up.** The XUNPU drawing does not number the contacts on the connector body, so the footprint numbering is ours to set, and this sets it.
- **A correction to section 15.1 as the review left it.** It said the fold reverses the order of the pins as the board sees them. It does not. A fold about an axis parallel to the row of contacts leaves every contact at the same position along the row. What changes is the direction the connector's mouth faces, and so which end of the connector pin 1 is at.
- **Body size.** The XUNPU drawing gives (N - 1) x 0.5 + 4.9 = 10.4 mm along the contact row for 12 ways, 5.12 mm deep and 2.0 mm high. Revision 2 of the contract uses those figures. Revision 1 assumed 5.0 x 9.6 mm.

Both points rest on the panel drawing. Check them on a sample panel before
layout, as section 15.1 says.

**Terminal edge.** The contract's order along the edge is J5, J1, J2, J3, J4,
J6. Sections 16.1 and 16.2 agree with it: the four terminal blocks are in
the same order with the same pin order, and the contract adds the USB socket
at the left end and the Qwiic socket at the right. No conflict.

**Where the two documents meet.**

| Item | Contract, revision 2 | Circuit design | Open |
|---|---|---|---|
| Board | 114 x 110 mm, four layers, every surface-mount part on the top side | 346 components. Nothing in the circuit needs a second assembly side. | Nothing |
| Fuse holders | F4 at (15.0, 12.6) and F1 at (27.0, 12.6), in a walled well behind J1 that opens under the wiring cover. Body 9.73 x 5.03 x 3.81 mm from the Littelfuse drawing. | F4 is in the leg of J1 pin 1 and F1 in the leg of pin 2, as the contract places them. Section 4.3 asks for both next to J1. | Nothing. The height that this document could not read is 3.81 mm. |
| C1 | Through-hole, on the bottom side, lying down, fixed at (21, 30) with pads at (36.0, 27.5) and (36.0, 32.5). Named as the Rubycon PX part, 21.5 mm long at most. | Rubycon 63ZLJ470M12.5X20: the same case and lead pitch, five times the endurance. Section 4.5. | The ZLJ can may be 22.0 mm long. It stays inside zone B_C1. The contract's part name and length are for its owner to update. |
| K1 | Through-hole, inserted from the bottom, soldered by hand on the top. Suggested at (50, 22). | Through-hole because no surface-mount relay meets the ratings, section 7.2. | Whether the assembler hand-solders parts on the side opposite the SMT has not been asked. If not, the builder solders K1 and C1. |
| USB socket beside J1 | The shell ends at x = 12.47 and J1's first pin is at x = 17.8 | Electrically harmless. The shell is at `GND` through R17 and J1 pin 1 is at most 45 V away. This document asked for 2 mm. | Nothing |
| Fault LED D8 | No light pipe or opening | D8 shows the over-current latch and is a bring-up aid. Firmware reads the same latch on GPIO40 and shows it on the display. | Nothing |
| Enclosure temperature | Estimates 9 K at 2 W and 14 K at 3.5 W with vents | This document designs to 70 °C inside, section 3.3. | Keep the vents. A closed box would need the estimate redone. |
| Soldering access | Keep 2.5 mm around the top-side pads of C1 and K1 free of parts taller than 1.5 mm | | A layout rule. |

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

`hardware/bom-preliminary.csv` has 99 lines: 46 basic parts, 52 extended parts and 1 that JLCPCB does not fit.

| Build | 10 boards | 100 boards |
|---|---|---|
| 8-zone | 41.32 USD | 32.25 USD |
| 4-zone | 35.03 USD | 27.45 USD |

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
| 0154003.DRT | 2514 | 200 |
| EC11J1525402 | 1486 | 100 |
| 1812L020/60DR | 1650 | 100 |

<!-- designsync:end -->

This section prices parts only. The landed cost of a finished controller,
with the board, assembly fees, freight, US duty and the enclosure, re-priced
on 2026-11-11, is on [what a controller costs](../hardware/cost.md): about
134 USD each for 5 eight-zone controllers and 54 USD for 100.

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

The totals at the top of this section are 0.34 USD lower than that sum
gives. The through-hole bulk capacitor that replaced the surface-mount one
after the mechanical contract (section 4.5) costs 0.25 USD at 100 pieces
where the other cost 0.59 USD.

The over-voltage detector U13 of section 9.5 came after that table. It adds
0.41 USD to a board at 100 boards: 0.39 USD for the part and the rest for
two resistors and two capacitors of values that were on the BOM already. It
is an extended part at JLCPCB.

The CR2032 cell is on the BOM as a line with no LCSC number, marked as not
fitted by JLCPCB. Review A1 noted that the rule "an LCSC number on every
line" had a gap there. The JLCPCB parts search on 2026-11-10 returns holders
and contacts for "CR2032" and no cell, so the gap is now stated on the line
and not closed.

## 19. What firmware must do

These follow from the circuit. `hardware/pinmap.yaml` carries the pin-level
detail. Sections 19.1 to 19.5 keep their numbers from before the review,
because other documents refer to them. Sections 19.6 to 19.9 give the exact
procedures. Section 19.10 is new with the
[answers to the firmware's questions](hardware-answers-a2.md), which also
rewrote rules 4, 14, 15 and 17, added rules 18 to 20 and changed sections
19.6, 19.7 and 19.8. Where this section and an older text disagree, this
section is right.

### 19.1 Rules

1. **Keep-alive.** Follow the contract in section 19.4. In short: GPIO17 is a plain GPIO output, every edge is decided by code that has checked the control-loop heartbeat, one edge every 5 ms while outputs are wanted, low otherwise.
2. **Self-test.** Run the sequence of section 19.7 at start-up, before every run and after every run. On a failure, write every expander pin back to input if the expander answers, hold the keep-alive low, raise a fault and refuse to run valves until a person has acknowledged it.
3. **Expander.** Follow the register sequence of section 19.7. A valve bit is on when it is an output and low. Never more than three valve bits low. Read back after every write and once per control tick. A mismatch, a missing acknowledge or a stuck bus means stop the keep-alive.
4. **Relay sequencing.** `RLY_ON` high at least 20 ms before the first valve, low at least 40 ms after the last. Both are minimum times. Firmware may keep the relay closed for a few seconds after the last valve, so that a run that follows at once does not repeat the tests. Rule 6 applies for as long as it is closed.
5. **Over-current latch.** GPIO40 low means the hardware has cut drive. Follow section 19.8. There is no release pin.
6. **Unexpected current.** Current above the idle threshold with the relay closed and no valve bit low means a shorted output device. Open the relay and stop.
7. **PSRAM ECC.** Enable it. The design temperature inside the enclosure is 70 °C, the R8 module is rated to 65 °C without ECC and 85 °C with it, so this is no longer optional. It costs one sixteenth of the PSRAM. In ESP-IDF 5 the option is `CONFIG_SPIRAM_ECC_ENABLE`; check the name against the IDF version in use.
8. **Flash.** A 4 kB sector erase stalls both cores for up to about 400 ms. The keep-alive interrupt runs through it; the control loop does not. Keep 64 kB block erases, which can take 2 s, away from watering.
9. **Backlight.** PWM on GPIO21 between 1 and 20 kHz. Dim and time out.
10. **Stagger starts.** Never switch two outputs on within 100 ms of each other. The hardware trip can be as low as 4.0 A peak, and two valves holding 0.5 A with a third drawing 1.5 A of inrush already reach 3.5 A peak.
11. **RTC clock output.** Once at start-up, write 0x00 to PCF8563 register 0x0D. The chip powers up with its 32 kHz output enabled, which doubles its back-up current. Section 14.
12. **Read-back pins.** GPIO7 and GPIO42 are inputs for the life of the firmware. Enable the internal pull-down on GPIO42. Enable no pull on GPIO7. Never configure either as an output. GPIO7 is read both ways: as a digital input every keep-alive period, and as ADC1 channel 6 with the current windows, for step 1. Whether the two readings disturb each other behind 60 kohm is risk 33.
13. **Sensor supply enable.** GPIO45 is a strapping pin. Leave it alone until the application runs, then drive it high to turn the sensor supply on. Never enable its pull-up.
14. **Weld check.** Before every run, section 19.7 steps 5 and 5b. They need current readings: rule 19.
15. **Supply monitor and regulator fault.** Read `VIN_MON`, GPIO3, as section 19.10 says. It is a precondition of step 0. A missing `V_BUCK` cannot be told from a relay transistor that is on: both put 0 V on GPIO7 at rest, and no pin is free for a reading that would separate them. So there is no separate regulator check. A missing `V_BUCK` fails step 1 on the relay read-back, and the report names both causes: Q2 or its detector, and U1, F5 or D25 (section 4.8). A person tells them apart by unplugging USB: with a failed regulator the controller goes dark on 24 VAC alone. The one case firmware can separate is a rectified supply that is missing while mains is present, section 19.10.
16. **UART1.** GPIO17 is the default TXD pin of UART1 in ESP-IDF. Never open UART1 on its default pins: a UART idles high and sends bursts of edges, both on the keep-alive net.
17. **Float test.** Section 19.7 step 2b, before every run and at start-up. It looks for a net that drives the keep-alive net when the processor does not, so the nets that firmware can make toggle must toggle while it runs. The note to step 2b says exactly how.
18. **Over-voltage on `3V3`.** If U3 shorts, U13 removes drive by hardware (section 9.5). Firmware reports it, if it still runs, from two readings. The rest level of both current channels above 0.90 V means `3V3` is above about 3.6 V (section 19.10): stop the keep-alive, report a supply over-voltage fault that needs repair, and refuse runs. And `OC` going low while no valve bit is low is not an over-current (section 19.8).
19. **Without current readings.** Steps 5, 5b and 6 and rule 6 need the current measurement. If no window arrives, or the rest level of a channel is outside its limits (section 19.10), firmware skips those steps and may still run valves, and it must report that condition as a fault of its own for as long as it lasts, on the display and on every interface. It must never skip them without saying so. The reason it may run: a welded K1, a shorted Q1, an open R12 and a shorted output device each leave the other layer intact, so a valve stays on only with a second hardware fault on top, and a controller that stops watering because its ADC failed does harm for certain. The hardware over-current latch does not depend on the ADC.
20. **Commissioning.** A board stores its own two release times once, as section 19.7 says under "Commissioning". It is automatic. It is not a button.

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
| 2 | Keep-alive edge rate and hold time | Section 19.4. One edge at least every 20 ms and no phase under 2.5 ms; the firmware's 5 ms is right. Gate drive is gone 0.21 to 0.83 s after the last edge and the relay transistor has let go within 1.45 s. Flash sector erase up to about 400 ms. |
| 3 | Pull-downs on outputs and OE | There are no output GPIOs. `KA` has a 2.2 kohm pull-down, R33. The expander resets to inputs, and resistors hold every one of its pins at its off level. |
| 4 | Current-sense front end | 0.1 ohm shunt, two AC-coupled-by-arithmetic channels biased at 0.79 V. Section 19.2. Trip flag on GPIO40. |
| 5 | 24 VAC present | GPIO41, a mains-frequency square wave, not a level. Section 19.9. |
| 6 | Field input polarity | Contact closed reads low. External pull-ups fitted. Flow passes 200 Hz. |
| 7 | RTC | PCF8563 at 0x51, as assumed. CR2032, about ten years. Rule 11. |
| 8 | Display | HS20HS072RX, 240 x 320. The enclosure concept mounts it landscape with the tail towards the encoder. Chip select is on GPIO10 and reset on GPIO14 with a 10 kohm pull-up. Neither is tied off on the board, so firmware drives both. Data/command GPIO13, clock GPIO12, data GPIO11. Backlight GPIO21, high is on, 1 to 20 kHz. Back is GPIO18 and Manual/Home GPIO39, both low when pressed. The encoder switch is GPIO38. The encoder has 30 detents and 15 pulses per turn: two quadrature edges per detent. |
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
| Pin | GPIO17 as a plain GPIO output, idle low. No peripheral signal may be routed to it through the GPIO matrix. It was GPIO38 until review A2, section 12.2. |
| Who makes an edge | Code that has just checked the control-loop heartbeat. Today: the 5 ms GPTimer alarm interrupt calling `OutputEnableGuard::step()`. Forbidden: LEDC, RMT, MCPWM, a timer with a pin output, any other self-running source. |
| Edge interval, changed by review A2 | 5 ms high and 5 ms low. The hardware needs an edge at least every 20 ms and no phase shorter than 2.5 ms. A waveform that is high for 1 ms and low for 20 ms, or the reverse, does not hold the relay at the cold corner. With phases of 2.5 ms and 20 ms the gate of Q2 keeps 0.19 V of margin, and the 5 ms toggle 0.55 V, with `3V3` at 3.2 V. |
| When it runs | Only while an output is wanted and no fault is latched, and for the short test and polling cycles of sections 19.6 and 19.7. It is low between runs. |
| Enable delay, changed | With 5 ms edges `V_DRV` is on 13 to 22 ms after the first edge and the relay transistor Q2 after 1 to 53 ms, over the corners of review A2. Firmware waits until both read-backs agree and the expander acknowledges, 150 ms at most, before it writes the expander. |
| After every start | The expander has been in reset. Section 19.7 gives the register sequence. |
| Stopping, changed | Firmware drives the pin low. Gate drive is gone 0.21 to 0.83 s after the last edge, 1.25 s if a fault leaves the pin high. The relay transistor has let go between 0.10 and 1.45 s after the last edge, 2.19 s if the pin is high. Valve current needs both, so it ends within 0.84 s, 1.25 s if the pin is high. |
| From a dead control loop to no drive, changed | Heartbeat timeout 500 ms, plus one 5 ms period, plus the hold-up: 1.34 s at most to no gate drive and no valve current, 1.76 s with the pin left high. The relay transistor alone, which matters only if the gate-drive layer has failed, takes up to 1.96 s, and 2.70 s with the pin left high. The specification's "about one second" counts from the last edge. The gate drive meets it. The relay detector alone meets it for most parts and reaches 1.45 s, or 2.19 s with the pin high, at one corner: a Q2 with the lowest threshold at 70 °C. Review A2, finding m10. |
| Flash | A 4 kB sector erase, up to about 400 ms, plus one 50 ms control period stays inside the 500 ms heartbeat timeout by 50 ms. Do not lower the timeout. Do not run 64 kB block erases while a valve is on. |
| Clearing the over-current latch | Hold the keep-alive low for 1.5 s. This also resets the expander. Section 19.8. |
| Self-test, changed | Section 19.7: before every run, at start-up and after every run. It now reads GPIO42 and GPIO7 and times both detectors. |
| What the circuit does not check | The rate. Toggling from about 1 Hz to at least 1 MHz can enable drive. Correct timing is the firmware's job. |

### 19.5 Open points between hardware and firmware

1. **Firmware still drives valves as GPIOs.** *Resolved in the firmware (correction to this list after the rev A firmware work, firmware architecture section 20):* the board header is generated from `hardware/pinmap.yaml` at every build, the valves are driven through the expander with the relay, the latch is cleared by stopping the keep-alive, the zero-cross input is treated as a square wave, and the current is an RMS of 100 ms windows.
2. **The self-test is new work.** *Resolved in the firmware, same change:* the sequence of section 19.7 runs at start-up, before and after every run, with the polling cycle of 19.6, and reads GPIO42 and GPIO7 every 5 ms. Nothing has been measured; firmware architecture section 20 lists the questions it raised.
3. **PSRAM ECC.** *Resolved:* `CONFIG_SPIRAM_ECC_ENABLE=y` in `sdkconfig.defaults`.
4. **Heartbeat timeout against erase time.** The 50 ms of margin in section 19.4 rests on one flash data sheet, and Espressif does not fix the flash part. Measure the longest control-loop stall on real modules.
5. **Sensor supply enable moved.** It is on GPIO45, not GPIO7. Rule 13. *Resolved in the firmware.*
6. **Limits in section 19.7 are from simulation.** The pass windows for the two release times come from tolerance corners of behavioural models. Review A2 widened them, to 0.15 to 1.0 s and 0.05 to 1.6 s, and added a band around a value stored per board. The answers to the firmware's questions set the band to 0.40 to 1.70 and the hold of step 2 to 2.0 s, from more simulation. Measure both times on the first boards, from -20 to 70 °C, before the limits go into a release. Risk 30.
7. **Review A2 changed the procedure.** New in section 19.7: step 2b, the float test, step 5b, the gate bleed check, and the limits of step 9 with a stored value per board. New in section 19.1: rules 15 to 17. The contract of section 19.4 asks for phases of at least 2.5 ms. The keep-alive moved to GPIO17 and the encoder switch to GPIO38; firmware that takes its pins from the generated board header follows without a change. GPIO42 still means "gate drive enabled", read at `V_DRV` now. *Implemented in the firmware (correction to this list after the firmware work on review A2, firmware architecture section 20), except rule 15, which the firmware reports as the idle test's `idle_relay_on` because it cannot tell a missing `V_BUCK` from Q2 on; the questions that work raised are listed there.* Nothing has been measured.
8. **The answers to those questions changed this section again.** Rule 15 now says what the firmware already does. New for the firmware: the hold of step 2, step 2b before every run whatever step 2 does, the load of the float test written out, the band and the learning limits of step 9, the temperature window for commissioning, the supply monitor and the rest levels of section 19.10 with rules 18 and 19, and a trip with no valve bit low in section 19.8. The firmware on `main` implements section 19 as review A2 left it. [Hardware answers A2](hardware-answers-a2.md), section 7, lists each change.

### 19.6 Expander inputs between runs

`VARIANT_N`, P14, and `SENS_FLT_N`, P15, are inputs of the expander. The
expander is in reset whenever the keep-alive is stopped, so they can be read
only while it runs. Review finding m11 asked for GPIOs. None is free
(section 12.2), so firmware reads them like this:

- **`VARIANT_N`** is a strap. Read it once, in the start-up self-test, from input port 1 bit 4. Low is the 8-zone build.
- **`SENS_FLT_N`** during a run: read input port 1 bit 5 with the read-back of every control tick. Low means U11 is limiting or too hot.
- **`SENS_FLT_N`** between runs, while the sensor supply is on: every 10 minutes, and at once when flow pulses stop or start in a way the schedule does not explain, run a polling cycle. That is steps 0, 1, 3 and 4a of section 19.7, then a read of input port 1, then step 9. Toggle for at least 0.3 s before step 9; the firmware toggles for 1 s. No valve bit and no relay bit is touched. The cycle takes about 2 s after the wait of step 0. K1 does not move, because Q1 stays off.

The polling cycle is also a test of both detectors, so a controller that does
not water for a month still tests them every 10 minutes. No separate wait is
needed between a polling cycle and a run. Step 0 of the run waits until `KA`
has been low for 2.5 s, and that is the wait. An earlier version of this
section asked for 2 s, which was shorter than step 0 and shorter than the
2.19 s the relay detector can take at its slow corner if a fault leaves the
pin high.

### 19.7 Self-test and run sequence

Names: `KA` is GPIO17. `A_RB` is GPIO42, high when gate drive is enabled:
it reads `V_DRV`, the opto LED supply behind Q3 (section 8.6).
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
sequence in the fault state at the end of this section. Steps 2b and 5b are
from review A2, which simulated every step against every single fault:
`hardware/sim/failsafe/`. The answers to the firmware's questions ran the
sequence on healthy boards at every tolerance corner as well, which changed
the hold of step 2 and the limits of step 9, and they wrote out what the
first implementation had to guess: the notes after the table.

| Step | When | Do | Pass |
|---|---|---|---|
| 0 | Always | Check the preconditions: `AC_ZC` has had an edge in the last 25 ms, the latest `VIN_MON` window is 0.60 V or more and not above 1.50 V (section 19.10), `KA` has been low for at least 2.5 s, `OC` is high. | If one is missing, do not run and do not test. Without 24 VAC, and with `VIN_DC` too low for U1, `B_RB` reads low on a healthy board. This is not a fault state: wait and try again. The one exception is the rectifier fault of section 19.10. |
| 1 | Idle test, always | Read `A_RB` and `B_RB`. Address the expander. Take the latest window means of GPIO7 and of both current channels. | `A_RB` low. `B_RB` high. No acknowledge. The mean of GPIO7 not between 0.3 and 1.5 V. Not both rest levels above 0.90 V. A high `A_RB` with no acknowledge is Q3 conducting when it should not. An acknowledge with `A_RB` low is `DRV_EN` stuck high. A low `B_RB` is Q2 on or `V_BUCK` missing: rule 15. GPIO7 between 0.3 and 1.5 V is Q2 shorted gate to drain, whatever the digital read says. Both rest levels high is rule 18. One rest level outside its limits is rule 19 and does not stop the sequence. |
| 2 | Pin-high test: at start-up and before every run. The note below says when it may run less often. | Drive `KA` high and hold it. After 2.0 s read `A_RB` and `B_RB`. Drive `KA` low and wait 100 ms. | `A_RB` low. `B_RB` high. A high `A_RB` is a shorted C22 or a leak onto the reservoir of detector A. A low `B_RB` is a shorted C51 or a leak onto the gate of Q2. |
| 2b | Float test: at start-up and before every run, however often step 2 runs | Make `KA` an input with no pull for 0.5 s. Run the load of the note below from 100 ms before until the end. Read `KA` as an input every 1 ms. At the end read `A_RB` and `B_RB`. Make `KA` an output again, low, and wait 100 ms. | `KA` reads low every time: stop at the first high read. At least 400 reads. `A_RB` low. `B_RB` high. Anything else means another net drives the keep-alive net: a bridge to the backlight, the zero-cross signal, a display or memory line or the boot log. Section 8.5. |
| 3 | Start, always | Toggle `KA` every 5 ms. Poll `A_RB`, `B_RB` and `OC` every 5 ms and the expander every 5 ms or every control tick. | Within 150 ms: `A_RB` high and `B_RB` low. An acknowledge within 200 ms. `OC` high: low here is a trip with no valve bit low, section 19.8. |
| 4a | Expander, always | Read input ports 0 and 1. | Port 0 reads 0x1F: valve pins high, `RLY_ON` and spares low. Port 1 reads 0x2F on an 8-zone board and 0x3F on a 4-zone board. Bit 5 low instead means a sensor supply fault, which is reported and is not a reason to stop. |
| 4b | | Write output port 0 = 0xDF and output port 1 = 0xFF. Read both back. | Equal to what was written. |
| 4c | | Write configuration 0 = 0xC0. Write configuration 1 = 0xF0 on the 8-zone build, 0xFF on the 4-zone build. Read both back, and read polarity 0 and 1. | Configuration as written. Polarity 0x00. |
| 5 | Weld check, before every run. Needs current readings: rule 19. | With `RLY_ON` still low, turn on the first output of the run by clearing its bit. Read input port 0 or 1. Measure one 100 ms window. Set the bit again. | The valve pin reads low while its bit is low. Current under the idle threshold. More means a welded K1 contact, a bridged JP1 or a shorted Q1. |
| 5b | Gate bleed check, before every run. Needs current readings. | Set `RLY_ON` for 20 ms. Then make its pin an input without clearing it first: write configuration 0 = 0xE0. Wait 10 ms. Turn on the same output as in step 5 for one 100 ms window. Set the bit again, clear `RLY_ON` in the output port and write configuration 0 = 0xC0. | Current under the idle threshold. More means R12 is open: the gate of Q1 keeps its charge when the expander lets go, and Q1 would stay on through a reset. |
| 6 | Relay. The current part needs current readings. | Set `RLY_ON`. Read input port 0. Wait 20 ms. Measure one 100 ms window with every valve bit high. | Bit 5 of the input port is high. Current under the idle threshold. More means a shorted triac, TVS, opto-triac or snubber capacitor: clear `RLY_ON` and go to the fault state. |
| 7 | Run | Clear valve bits as the schedule asks, at least 100 ms apart, at most three low. After every write read the output port and the input port back. Every control tick, 50 ms, read input, output and configuration ports of both ports, `A_RB`, `B_RB` and `OC`. | Output and configuration ports equal the shadow copy. Valve bits of the input ports equal the output ports. `A_RB` high, `B_RB` low, `OC` high. |
| 8 | End of run | Set every valve bit. Wait 40 ms. Clear `RLY_ON`. Write configuration 0 and 1 = 0xFF. | Acknowledged. |
| 9 | Stop test, always. The keep-alive must have toggled for 0.3 s or more. | Drive `KA` low and start a timer. Sample `A_RB` and `B_RB` every 5 ms for up to 3 s. tA is the time until `A_RB` is low, tB the time until `B_RB` is high. Then address the expander. | 0.15 s <= tA <= 1.0 s and 0.05 s <= tB <= 1.6 s. Each within 0.40 to 1.70 times the value stored for this board, once one is stored. No acknowledge. |

Note to step 4a. These values are the resistors of section 8.2 showing
through: every pin is an input at this point. On port 1, bits 0 to 3 must be
high and bits 6 and 7 low on both builds; anything else is a hardware fault.
Bit 4 tells the build and bit 5 the state of the sensor supply.

Note to step 5b. In a healthy board the 20 ms of `RLY_ON` may close K1 for a
moment with no output on, which switches no current. R12 then discharges
the gate of Q1 in 0.1 ms. The step exists because the weld check alone does
not find an open R12: at the end of a normal run firmware drives `RLY_ON`
low before it releases the pin, and that discharges the gate whether R12 is
there or not.

**Note to step 2: the hold, and how often.** The hold was 1.0 s until the
answers to the firmware's questions. The one rising edge of this test lifts
the gate of Q2 to about 1 V. That turns on a Q2 with a low threshold, and
the read-back stays low until the bleed resistors have pulled the gate down
again. `hardware/sim/failsafe/sequence.py` runs the sequence on a board
with no fault at the 96 corners of review A2. With 1.0 s two of them failed
this step, both at 70 °C with large capacitors and a Q2 of 0.65 V that
drifts by -4.5 mV/K: the read-back needs 1.09 and 1.33 s there. At 27 °C
the longest is 0.83 s and at -20 °C 0.68 s. With 2.0 s all 96 corners pass
every step. The model's transistor conducts further below its threshold
than a real one, so the real figure is shorter. Risk 30 measures it.

The step no longer names an open bleed resistor. One open roughly doubles
that wait, which 2.0 s catches at some corners and not at others. Step 9 is
the test for it.

Step 2 may run less often than every run: at start-up and at least once in
24 hours. What it finds, a shorted C22 or C51 or a leak onto one reservoir,
takes one layer and leaves the other, so the other covers until the next
test. Before every run is the default and costs 2.1 s. Step 2b may not run
less often. It is the only step that finds a bridge onto the keep-alive
net, and that is the one fault that takes both layers at once, in the one
state that matters: a processor in reset while a valve is on. Nothing covers
it between tests, so the test belongs immediately before the valve opens.
Between runs no valve is on and the expander is in reset, so it need not
run then.

**Note to step 2b: what must run during the float test.** The simulation
behind the step, `hardware/sim/failsafe/bridges.py`, bridges `KA` to a
push-pull net that swings between 0 and 3.3 V at 500 Hz with equal high and
low times for the whole test. It finds the bridge in two ways.

- **The pin reads high.** Only under about 700 ohm: the bridge and R33 divide 3.3 V, and an input needs 2.48 V.
- **A read-back has moved.** Above that the pin sits between the logic levels. The test then finds the bridge because the net has pumped the detectors through it during the 0.5 s, and `B_RB` is low or `A_RB` high at the end. That works up to 3.3 or 4.7 kohm.

The second way depends on what the net does during the test.
`hardware/sim/failsafe/floatload.py` varies that, 288 runs:

| The bridged net | Holds the valve on, pin high-impedance | The float test reports it |
|---|---|---|
| High half of the time, at 500 Hz or 2 kHz | Up to 2.2 kohm in 4 of 4 corners, at 3.3 kohm in 1 | Up to 2.2 kohm in 4 of 4 corners, at 3.3 and 4.7 kohm in 3 |
| High for a quarter of the time | Up to 2.2 kohm in 4 of 4 | The same as at half |
| High for 10 % or for 90 % of the time | Up to 1 kohm in 4 of 4, at 2.2 kohm in 1 | Up to 1 kohm in 4 of 4, at 2.2 and 3.3 kohm in 3 |
| Steadily high | Never: no edges | Up to 470 ohm in 4 of 4, at 1 kohm in 1, not above |

The rate makes no difference, as `keepalive_faults.cir` found up to 1 MHz.
The share of time does: a net at 50 % pumps hardest and is found furthest.
No bridge that holds the valve at any share of time goes unreported by a
test with the net at 50 %. And a net that only sits high during the test is
found under 1 kohm only, although the same net toggling would hold the
valve up to 2.2 kohm. So every net that firmware can make toggle must
toggle during the test, at 50 % where firmware has the choice. From 100 ms
before the pin floats until the test ends:

1. **Backlight.** PWM at 50 % duty at a frequency between 1 and 20 kHz, whatever brightness the user has set, off included.
2. **Display.** Redraw the whole screen without pause, so that the SPI clock, data and the data/command line are busy for more than half of the time.
3. **PSRAM.** A task reads and writes a PSRAM buffer of 256 kB or more without pause. It has to be larger than the data cache, or the bus stays idle.
4. **`U0TXD`.** Send the byte 0x55 on UART0 without pause, which is a square wave at half the baud rate. The first implementation leaves the pin idle, and an idle `U0TXD` is the steadily high net of the table: a bridge of 1 or 2 kohm to it would pass the test and could still hold the valve while a reset loop prints the boot log. A terminal on J8 shows a line of the letter U at every test. That is the price.
5. **Nothing for the rest.** `AC_ZC` toggles by itself, and step 0 has checked that it does. The flow input, the current signals and the USB lines toggle only when water flows, a valve draws current or a host talks. No test can make them toggle. For those nets the layout rules of section 8.5 are the only protection.

Whether a real boot log, which leaves the pin low for a small share of the
time, can hold a detector at all is not simulated. Risk 27 has the bench
test.

One more thing in the first implementation needs a change. A backlight at
5.0 kHz and a pin read every 1.0 ms are locked together at five periods to
a read. Every read can then fall on the low part of the period, and a
bridge of 0 ohm to `TFT_BL` never reads high. The read-backs at the end
still find it. Run the backlight during the test at a frequency that is not
a whole multiple of 1 kHz, 4.7 kHz for example.

Stopping at the first high read is right. The test has its answer, and the
pin goes back to a driven low at once. A float that lasts up to 0.55 s
because firmware judges it on a 50 ms tick is fine.

**The limits of step 9.** The release times of a healthy board depend on
its capacitors, on the threshold of Q2 and on temperature. The corner runs
of section 8.4 put them between 0.21 and 0.83 s for tA and between 0.10 and
1.45 s for tB with the firmware's 5 ms edges. The absolute limits are wider
than that. They cannot find an open bleed resistor, which doubles a release
time, because a board with small capacitors and one bleed resistor open
looks like a board with large capacitors and both. So step 9 also compares
each time with a value the board stored for itself: the band.

`hardware/sim/failsafe/commission.py` sets the band and the rules for
storing the value. It draws 200 boards inside the tolerance ranges of
review A2 and adds the 64 combinations of their extremes, runs each board
healthy, with R30 open and with R14 open, at seven temperatures from -20 to
70 °C, and times both read-backs as a commissioning cycle does: 5544 runs,
`results/commission.md`.

| Question | Result |
|---|---|
| A healthy board against its own value stored at 10, 25 or 40 °C | tA between 0.55 and 1.06 times, tB between 0.54 and 1.62 times, over -20 to 70 °C |
| The band of review A2, 0.5 to 1.5 | 8 healthy stop tests outside it, all on boards that stored their value at 10 °C and were then tested hot |
| The band now, 0.40 to 1.70 | No healthy stop test outside it |
| A bleed resistor that opens later, board at 10 to 40 °C | Found in every stop test, on boards that stored their value at 10 or 25 °C. On boards that stored it at 40 °C, 2 of 64 extreme boards pass at 10 °C and are found at 25 °C |
| The same at 55 and 70 °C | Not found on up to half of the boards. The diodes leak, and a doubled time reads as 0.8 to 1.7 times |
| The same at -20 and 0 °C, relay detector | Not found on up to two thirds of the boards that stored their value at 40 °C, because the threshold of Q2 is higher in the cold |
| A board on which it is found at no temperature | None |

So the band finds an open bleed resistor when the board is between 10 and
40 °C, and may miss it outside. That is acceptable, because the fault takes
no layer away. It makes one detector slower, the absolute limits still
bound that, and the test comes round again at the next run.

The lower edge is wide on purpose. A release that is too fast is on the
safe side, and the absolute lower limits already guard the hold-up through
a flash erase. The model's capacitors do not change with temperature, and
an X5R part may lose up to 15 % at either end of the range. That shortens
the times at the extremes and would have put healthy boards under 0.5.

**Commissioning.** A board with no stored value applies the absolute limits
only and reports that it is not commissioned. It stores a value like this.

1. **When.** After the start-up self-test has passed, while no run is asked for, and again every 10 minutes for as long as nothing is stored. A run request goes first.
2. **Temperature.** Only while the die temperature sensor of the ESP32-S3 reads between 25 and 50 °C. Firmware cannot measure the board. The data sheet says that sensor follows the chip, which is warmer than its surroundings by an amount it does not give. I take 10 to 15 K, which puts the board between 10 and 40 °C, the range the simulation covers. Risk 30 measures the difference. The user manual should tell the builder to do the first start at room temperature.
3. **Cycle.** Steps 0, 1, 3, 4a and 9, with at least 1 s of toggling before step 9, and no register written. This is the polling cycle of section 19.6. "Run steps 3 and 9 five times", as this section said before, cannot be done as written, because each start needs the 2.5 s of step 0. A cycle takes about 4.5 s.
4. **Samples.** Five cycles in a row, of which the start-up test may be the first. Each tA must lie between 0.33 and 0.85 s and each tB between 0.12 and 1.25 s. The longest of the five may be at most 1.25 times the shortest, for each time. If a sample fails, start again, ten cycles at most in one attempt.
5. **Store** the medians of tA and tB in non-volatile memory that a factory reset does not clear. A stored value outside the limits of item 4 is ignored when it is read.
6. **Never** learn from the stop test of a run or of a polling cycle.
7. **Forget** the stored value when a person confirms the repair of a fault of step 1, 2, 2b, 5b or 9, and commission again.

It is automatic. A person who pressed a button would know no more about the
board than the firmware does, except the room temperature, and the
temperature window covers that.

**What the learning limits do and do not find.** They are the range of the
healthy boards between 10 and 40 °C with a few percent of room: tA 0.37 to
0.83 s and tB 0.14 to 1.23 s in the simulation. They are for a board that is
built with a bleed resistor already open, so that it does not store the
fault as its normal.

| Board | Stores a value |
|---|---|
| Healthy | 600 of 600 cycles of the random boards, 192 of 192 of the extremes |
| R30 or R91 open, detector A | 0 of 600. Of the extremes 16 of 192, all of them boards whose C23 is at the bottom of its allowance, 0.60 of nominal |
| R14 or R90 open, detector B | 434 of 600, and 144 of 192 of the extremes |

Detector A has a comparator, so its time depends on capacitance and
resistance alone, and a doubled time stands out. Detector B ends at the bare
gate of Q2, whose threshold of 0.65 to 1.45 V spreads tB over a factor of
four in the random healthy boards and nine at the extremes. A doubled tB is
inside that spread on nearly three boards in four.

**A limit on tB against tA does not help.** The firmware engineer asked for
one. Over the healthy boards the ratio of tB to tA runs from 0.32 to 1.98,
and from 0.18 to 3.10 at the extremes. With R14 open it runs from 0.71 to
4.04. A limit of 0.3 to 2.0, the tightest that refuses no healthy random
board, turns away 75 more of the 434 faulty ones and 42 of the 192 healthy
extremes, those whose two reservoir capacitors sit at opposite ends. If the
two capacitors are taken to be within 10 % of each other, as two parts of
one reel may be, it turns away 18 of 119. The ratio removes the capacitors
from the comparison and leaves the threshold of Q2, which is the larger
unknown. So section 19.7 has no ratio.

I looked for another reading that firmware could take. Stopping the pin
high instead of low multiplies both times by 1.3 to 1.9 whether a bleed
resistor is open or not. The time a detector takes to switch on is 11 to
32 ms for A and 2 to 42 ms for B in either case. Every time the firmware
can measure is the product of the bleed resistance and a capacitance, and a
capacitor that may be anywhere from 0.60 to 1.15 of its value hides a
factor of two.

**What a faulty board that commissions does.** Its times stay inside the
absolute limits at every temperature: the longest in the simulation are
0.95 s for tA and 1.54 s for tB, against limits of 1.0 and 1.6 s. So it is
no slower than the slowest healthy board the design accepts. What it has
lost is the spare bleed resistor. If the second one opens as well, that
detector never lets go, the stop test and the idle test report it, and the
other layer covers it meanwhile. A resistance reading finds the open
resistor on a new board, and firmware cannot: step 1 of the bring-up plan
and risk 34.

**Start-up self-test.** Steps 0 to 4c, then 8 and 9, with no valve and no
relay. Read the build variant in step 4a. Without 24 VAC, on USB alone, skip
it and refuse runs. Then commission, if nothing is stored.

**Toggling before a stop test.** The release times depend on how long the
keep-alive ran before the stop, because the reservoirs take time to fill.
After 0.3 s of toggling both times are within about 5 % of their final
values at every corner. After 0.1 s tB is 10 % short with typical parts and
60 % short at the corner that charges slowest, which would fail the band:
`hardware/sim/failsafe/results/warmup.md`. So the start-up test and the
polling cycle of section 19.6 must toggle for at least 0.3 s before step 9.
The firmware toggles for 1 s.

**The fault state.** Drive `KA` low. If the expander still acknowledges,
which means detector A or `DRV_EN` is stuck on, write configuration 0 and
1 = 0xFF so
that no valve bit and no relay bit is driven, and repeat that write every
control tick. Show the fault and its step number on the display and report
it. Do not start a run until a person has acknowledged the fault. A fault
from step 1, 2, 2b, 5b or 9 means one of the layers can no longer be relied
on: the board needs repair, and acknowledging it must not be a one-press
action. The same holds for the over-voltage of rule 18, the rectifier fault
of section 19.10 and a set input of the latch that is held low (section
19.8).

**After an over-current trip** the sequence is the same. Step 5 then also
tells whether K1 survived breaking the fault current.

### 19.8 Over-current latch: read and clear

- **Read.** `OC`, GPIO40, is low while the latch is set. Read it every control tick, or take an interrupt on the falling edge. When it is low the hardware has already removed gate drive and released K1: `A_RB` is low and the expander does not answer.
- **Do not confuse it with a stopped keep-alive.** `A_RB` low with `OC` high while the keep-alive runs is a fault in detector A, not a trip.
- **On a trip.** Drive `KA` low. Mark the output that was switched on last as faulted, if it was switched on within the last 500 ms; otherwise mark every output that was on as suspect. Record the last current window.
- **Clear.** Keep `KA` low for 1.5 s. The latch clears when detector A lets go, 0.21 to 0.83 s after the last edge. Then read `OC`: it must be high. If it is still low after 3 s, the latch or detector A is faulty: go to the fault state of section 19.7.
- **Restart.** Run the sequence of section 19.7 from step 0 without the faulted output. Allow at most three trips per output per day before the output is disabled until a person clears it.
- **There is no release pin and no release pulse.** Nothing the firmware writes clears the latch except stopping the keep-alive.
- **A trip with no valve bit low.** If `OC` goes low while no valve bit is low, in step 3 or 4 of any start or polling cycle, no load current caused it. Do not count it against an output. Hold `KA` low for 1.5 s and try once more. If it happens again, go to the fault state with a check of its own: the set input of the latch is held low. The causes are `3V3` above 4 V (U3 shorted, rule 18), U13 or its divider, U6B or U6C, or the threshold chain R48 to R51. With the keep-alive stopped `OC` reads high in this fault, because the latch needs `KA_OK` to hold. The red LED D8 is lit at rest in this fault and in no other.

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

### 19.10 Supply monitor and rest levels

Two readings that the firmware did not take before. Both are window means
of an ADC1 channel at 6 dB, taken with the current windows of section 19.2.

**`VIN_MON`, GPIO3, ADC1 channel 2.** It is `VIN_DC` divided by 31 through
R9 and R10, with C9. The half-wave ripple is inside a 100 ms window, so use
the mean of the window and not single samples.
`hardware/sim/vin_mon.cir` runs the input stage over the supply range of
section 3.3, with transformers of 20, 40 and 75 VA and loads from 0.6 W, the
display dark, to the 5 W of every consumer at its limit:

| Supply | `VIN_DC`, mean | `VIN_MON`, mean of a window |
|---|---|---|
| 21.6 VAC, 20 VA, 5 W: the lowest corner of the specified range | 19.6 V | 0.63 V |
| 21.6 VAC, 40 VA, typical load | 26.2 V | 0.85 V |
| 24 VAC, 40 VA, typical load | 29.8 V | 0.96 V |
| 26.4 VAC, 75 VA, display dark: the highest corner | 35.3 V | 1.14 V |
| 32 VAC with no load | 44.3 V | 1.43 V |
| 16 VAC, a sag that the logic supply still rides through | 15.0 to 17.6 V | 0.48 to 0.57 V |

The divider adds 1.9 % from two 1 % resistors and the ADC 10 mV. With both,
the specified range reads 0.61 to 1.17 V.

| `VIN_MON`, window mean | Means | What firmware does |
|---|---|---|
| Under 0.10 V | No rectified supply | With edges on `AC_ZC` for 2 s or more: D2, R1 or C1 has failed. Report a rectifier fault and refuse runs. Without edges: there is no 24 VAC, which is normal on USB. |
| 0.10 to 0.60 V | `VIN_DC` under 18.6 V, below the specified supply. U1 switches off between 8.4 and 9.0 V, 0.27 to 0.29 V here. | Step 0 does not pass. Do not test and do not run. Report supply low. It clears itself. |
| 0.60 to 1.20 V | Normal. U1 is certain to run, so `V_BUCK` must be there. | |
| 1.20 to 1.50 V | Above the specified range and inside the 32 VAC the board allows with no load | Normal. |
| Above 1.50 V | `VIN_DC` above 46.5 V | Do not run. Report supply high. |

A mean that falls by a tenth or more when a valve starts means the
transformer is too small. That is worth a line in the diagnostics and is not
a fault.

Why step 0 needs it: in a brown-out that leaves `VIN_DC` under 9 V, mains
still makes edges on `AC_ZC`, U1 is off and GPIO7 reads low. On USB power
step 1 would report a relay fault that needs repair, for a supply dip.

**Rest level of the current channels.** Both amplifiers rest at `VB`, which
is 0.2385 times `3V3` with the amplifiers loading the chain: 0.787 V at
3.30 V (section 10.3). So the mean of a window with the keep-alive stopped
reads the logic rail. The budget: `3V3` from 3.20 to 3.40 V, 1.5 % on the
ratio from 1 % resistors, 4.5 mV of op-amp offset (MCP6002 data sheet) times
a noise gain of 2.33 on `ISENSE_LO` and 7.65 on `ISENSE_HI`, and 10 mV in the
ADC.

| Window mean at rest | `ISENSE_LO` | `ISENSE_HI` |
|---|---|---|
| Calculated range of a healthy board | 0.73 to 0.85 V | 0.71 to 0.87 V |
| Limits for firmware | 0.70 to 0.88 V | 0.68 to 0.90 V |
| Both above this: `3V3` is too high, rule 18 | 0.90 V | 0.90 V |

Both channels above 0.90 V cannot happen with `3V3` under 3.63 V and is
certain from 4.02 V, by the same budget. A shorted U3 gives 4.66 V or more
on 24 VAC, which reads 1.07 V or more. One channel outside its limits while
the other is inside is a fault of the front end: U8, R38 to R51 or C32. Then
the current readings cannot be trusted and rule 19 applies. This answers
question 6 of the firmware architecture, section 20: the rest level was
learned and never judged.

Whether the ADC reads correctly with its own supply at 5 V is not known. The
reading is a report. The valves do not depend on it: U13 does that.

**Two smaller answers** to the same list of the firmware architecture.

- **Zero-cross offsets, question 8.** Do not correct them with `VIN_MON`. From 24 to 32 VAC they move by 0.05 ms, and firmware uses the signal for presence and frequency only.
- **DC component, question 9.** 30 % of the rms current and at least 20 mA is right. A current that flows in one polarity only has a mean of 64 % of its rms value or more, and a healthy one has none.

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
| 19 | Bulk capacitor ripple and life | The ZLJ sheet gives no ripple rating below 120 Hz; 0.8 of the 120 Hz figure is borrowed from the PX sheet. Life by the 10 K rule, section 4.5. The leads are bent through 90 degrees and the can lies on the board. | Capacitor case temperature at peak load and low line. Inspect the lead seal after forming, and after a vibration or drop test of the assembled unit. |
| 20 | Output LED brightness | 1.6 mA is visible through a light pipe | Look at it in the enclosure. |
| 21 | Regulator over-voltage | D25 clamps `V_BUCK` under 10 V and F5 opens, section 4.8. In the worst case the current reaches the 50 A that F5 is rated to interrupt at 125 V, and it has to break it at 44 V. The fuse model has no arc and no heat loss; the resistance of a failed switch is a guess; D25 is assumed to fail short and not open. | On sacrificial boards, short SW to VIN of U1 with a valve running, at `VIN_DC` of 17, 30 and 44 V, on 20 and 75 VA transformers. Scope `V_BUCK`: under 10.3 V throughout. F5 open, no arcing, no charring. `3V3` in regulation until `V_BUCK` has gone. The valve off. Then check which parts survived: U11 above all. |
| 22 | Gate drive of Q2 | 2.09 V at the worst corner against a 1.59 V threshold. No on-resistance is guaranteed below 2.5 V. | Drain voltage of Q2 with K1 energised at -20 °C, with the keep-alive slowed to one edge per 20 ms. Under 50 mV. |
| 23 | Single-fault behaviour of the two detectors | Section 8.5, on paper and in simulation | Fit each fault marked "stuck on" in section 8.5 by hand, one at a time: lift R31 with drive enabled, lift R34, bridge C22, bridge C51, tie the gate of Q2 to its drain, bridge K1. For each, halt the CPU with a valve running and with the pin low and high. The valve must close within the time the table gives, and the self-test must report the fault at the next start. |
| 24 | Supervisor resets on supply dips | Dips of tens of millivolts on `3V3` | Scope `3V3` and `EN` during WiFi transmit bursts at 21.6 VAC and on USB at 4.4 V. No reset. |
| 25 | Negative miswire on the sensor supply terminal | D24 holds the output pin of U11 at about -0.25 V; the limit is -0.3 V | -32 VAC peak on the 5V terminal with the supply off, at -20 and 25 °C. Scope `SP_OUT`. |
| 26 | R1 on repeated power-up | 237 to 385 W for about 1 ms, inside the Yageo single-pulse curve by 1.9 to 3 | 1000 power-ups at the peak of 32 VAC from a 75 VA transformer with C1 discharged. Resistance change under 1 %. |
| 27 | A bridge from `KA` to another net | Layout rules of section 8.5. The float test of section 19.7 finds a bridge that can turn Q2 on, if the other net toggles during the test. | Inspect the bare board and the assembled board under magnification around module pin 10, R29, R33 and R89. Measure resistance from `KA` to `TFT_BL`, `AC_ZC`, `ENC_B` and `BTN_BACK`. Then fit bridges by hand, 1 kohm and 0 ohm from `KA` to `TFT_BL` and to `AC_ZC`, and check that step 2b reports each. Repeat the `TFT_BL` bridges with the backlight at 10 % and at 90 %, and bridges of 1 kohm to `TFT_SCLK` and to `U0TXD`, to see how far the result depends on what the note to step 2b asks firmware to keep running. With the `U0TXD` bridge, hold the board in a reset loop with a valve on and see whether the boot log alone holds either detector. With a scope, check that the 1 ms reads of the pin do not all fall on the same point of the backlight period. |
| 28 | The release window of detector B | AO3400A threshold drift of -1.5 to -4.5 mV/K, which the data sheet does not give. With the worst of it Q2 lets go as late as 1.45 s after the last edge, 2.2 s if the pin stops high. Section 7.3. | Part of risk 10: measure the gate threshold of Q2 at 250 uA at -20, 25 and 70 °C on ten boards. |
| 29 | Hold margin of detector B with a slow or lopsided keep-alive | 0.08 V at one edge per 20 ms and a 3.0 V rail, at -20 °C with every tolerance against it. Phases shorter than 2.5 ms are not held at that corner. Section 7.3. | With risk 22: repeat with the keep-alive at 2.5 ms high and 20 ms low, and the reverse. |
| 30 | The stop-test limits, the learning limits and the hold of step 2 | Section 19.7: absolute limits, a band around a value stored per board, limits on what a board may store, a die temperature window for storing it, and 2.0 s of hold in step 2. All from `hardware/sim/failsafe/commission.py` and the corner runs. The model's capacitors do not change with temperature, where an X5R part may move by 15 %. Its transistor conducts further below its threshold than a real one. How far the die runs above the board is an estimate of 10 to 15 K. | Record tA, tB and the die temperature reading of the first boards from -20 to 70 °C in steps of 10 K, after a cold start and after an hour of running. Set the band, the learning limits and the temperature window from that. On one board lift R30 and then R14 and repeat: each must fail step 9 at 25 °C and below. Hold `KA` high from rest and time GPIO7 back to high at 70 °C: 1.33 s at most in the model. |
| 31 | The eFuses of the module | `VDD_SPI` forced to 3.3 V by eFuse on modules with PSRAM, from a note in the module data sheet. Section 12.3. | `espefuse.py summary` on the first modules. |
| 32 | A shorted 3.3 V regulator | U13 removes drive above 3.91 to 4.17 V, from its data sheet and a behavioural model. A processor and a display at 4.7 to 5.1 V are not modelled and may be destroyed: accepted by the owner for revision A. Section 9.5. | On every prototype, the U13 test of bring-up step 7. On one board that may be lost: with a valve running on 24 VAC, join the input and output of U3. The valve must stop within a mains cycle and D8 light. Then drive `KA` from a signal generator at 100 Hz through 1 kohm: `DRV_EN` must stay low. Record what the processor and the display did and what survived. |
| 33 | GPIO7 read as a level and by the ADC | The pin sits behind R94 and R95, 60 kohm. Assumed: the ADC's sampling does not move the pin, and the digital input still works on a pin the ADC samples. The ESP32-S3 data sheet gives no input charge or source resistance for its ADC. If the digital read were lost the pin would read low and step 1 would fail, which is the safe side. | With Q2 off, with Q2 on, and with 1.0 V forced on `RLY_B` through 10 kohm: compare the ADC mean of GPIO7 with a meter, and check the digital read at 5 ms. If the ADC pulls the level, 10 nF at the pin cures it and needs a footprint in the next revision. |
| 34 | The bleed resistors at assembly | A board built with one bleed resistor open is inside every limit firmware can apply: it looks like a board with large capacitors. Section 19.7. | The two resistance readings of bring-up step 1, on every board that is built. |

## 21. Bring-up test plan

For the first prototype. Stop at the first step that fails.

1. **Before power.** Inspect. Measure resistance from `VIN_DC`, `V5`, `3V3` and `V_DRV` to `GND`. No cell in BT1, no display, no valve wiring. Then the bleed resistors, on every board: from TP8 (`KA_N2`) to `GND`, and from the gate of Q2 at C50 to `GND`, with the positive lead on the node and a few seconds for the capacitor to charge. Each reads about 50 kohm: R30 with R91, and R14 with R90, with a little leakage beside them. A reading above 80 kohm means one of the pair is open. This is the only test that finds it on a new board (risk 34).
2. **USB only.** Feed 5 V from a bench supply limited to 200 mA into VBUS. Expect `V5` 4.4 to 4.9 V, `3V3` 3.20 to 3.40 V, under 80 mA with a blank module. Lower the supply slowly: `EN` must fall when `3V3` passes 2.94 to 3.06 V and rise 130 to 270 ms after it recovers. Then use a USB port and read the ROM boot log. GPIO45 must read under 0.3 V during reset. Run `espefuse.py summary` and check that `VDD_SPI_FORCE` is 1 and `VDD_SPI_TIEH` is 1 (section 12.3).
3. **Fail-safe at rest.** `KA_OK` low, `DRV_EN` low, `V_DRV` at 0 V, GPIO42 low, the gate of Q2 at 0 V, K1 open, no I2C answer at 0x74. The RTC at 0x51 answers. With `KA` as an input and the display running, `KA` stays under 0.1 V.
4. **Keep-alive.** Test firmware toggles `KA` at 100 Hz. `KA_N2` rises to about 2.9 V and `DRV_EN` goes high within 20 ms. The gate of Q2, probed at C50, rises to about 2.7 V within 50 ms. Stop toggling and time both falls on a scope: `DRV_EN` after 0.21 to 0.83 s, the gate through 1.5 V and 0.6 V between 0.10 and 1.45 s. Repeat stuck high, where `DRV_EN` must fall within 1.25 s and the gate within 2.19 s, and at 25 Hz. Then try 0.5 Hz, which must not enable, and 1 Hz, 2 Hz, 1 kHz and 100 kHz, and record what each does to both. Then a lopsided keep-alive, 2.5 ms and 20 ms each way round, with the board at -20 °C: Q2 must stay fully on.
5. **Expander and LEDs.** Turn each output on in turn. Its LED lights. The voltage across R101 is about 1.8 V, 12 mA. `V_DRV` falls when the keep-alive stops, and the expander stops answering.
6. **Current channels.** Pass a known 50 or 60 Hz current through `ISNS` to `GND` from a low-voltage source. Check 133 mV/A and 665 mV/A and the 0.79 V bias.
7. **Over-current latch.** Raise that current. `FAULT` sets between 4.0 and 5.4 A peak, D8 lights, `DRV_EN` falls. It stays set until the keep-alive stops. Then the over-voltage detector, without raising the rail: with the keep-alive stopped, bridge R13 with 470 kohm, which puts 0.85 V on pin 4 of U13. D8 must light. Start the keep-alive: `DRV_EN` must stay low and `OC_FAULT_N` go low. Remove the resistor: D8 goes out, and drive enables again at the next start.
8. **First AC power.** Unplug USB. Check F1, F4 and F5 for continuity. Feed J1 from a variable 0 to 32 VAC isolated source. The buck starts at about 9 V on `VIN_DC`. At 24 VAC: `VIN_DC` 28 to 31 V, `V_BUCK` 5.16 to 5.40 V, `AC_ZC` a clean square wave, `VIN_MON` about 0.97 V. Raise to 32 VAC for 30 minutes: `VIN_DC` under 45 V, D1 cold.
9. **Both supplies.** Add USB from a battery laptop. No current flows back into VBUS beyond 1 mA.
10. **Relay.** With 24 VAC and the keep-alive stopped, GPIO7 reads about 3.05 V. Start the keep-alive: it reads 0 V. Command K1 closed. `COM` is live. Stop the keep-alive and time K1 opening against `DRV_EN` and the gate of Q2: it opens when the first of the two lets go. Trip the over-current latch with K1 closed: it opens within 10 ms. Run the firmware sequence of section 19.7 and check the times it reports against the scope. From rest, hold `KA` high for 3 s and time GPIO7 falling and returning to high: step 2 allows 2.0 s for it.
11. **Resistive load.** 47 ohm, 25 W from `COM` to each output in turn: 0.5 A. Scope T2: firing voltage a few volts each half-cycle. Compare the current reading with a meter.
12. **Valve.** A real solenoid on each output. Scope T2 at turn-off: one clean stop at the current zero, ringing under the 77 V clamp. Then a 50 mA relay coil.
13. **Short circuit.** With a 40 VA transformer, short an output to `COM` and turn it on. One or two half-cycles of current, latch set, fuses intact, triac still working, weld check passes. Repeat with the short applied while the output is on. Risk 4 has the full test.
14. **Fail-safe with a valve running.** Halt the CPU, hold reset, start a USB flash. The valve closes within 0.84 s each time, or 1.25 s if the pin stopped high, and does not reopen. Then the fault-injection runs of risk 23.
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
| Bulk capacitor ripple rating | For the first draft's Lelon part: 900 mA at 100 kHz, 540 mA at 50 and 60 Hz. For the Rubycon ZLJ part now fitted: 2400 mA at 100 kHz, 1320 mA at 120 Hz, no figure below 120 Hz. | Lelon VZH table, 63 V column. Rubycon ZLJ sheet, standard size table and multiplier table. |
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
- **BAT54S leakage when hot.** The Nexperia data sheet limits it at 25 °C only. It does give typical curves at 85 and 125 °C, figure 2: about 20 uA and 200 uA at a few volts. Review A2 found that the model in the simulations reproduces those curves, and ran its corners at three times typical as well. No maximum above 25 °C exists. Risk 10 measures it.
- **PTC trip time above 8 A.** One data sheet point, scaled. Section 6.6.
- **Bulk capacitor ripple rating at 50 and 60 Hz, and Rubycon's rules for forming leads.** Section 4.5.
- **Resistor array pin pairs.** The UNI-ROYAL data sheet shows the circuit as a picture. Pairs 1-8, 2-7, 3-6 and 4-5 are assumed from the usual numbering.
- **Regulator over-voltage.** Covered on paper since review A2, section 4.8. Not built, not tested: risk 21.
- **A shorted LDO.** `3V3` at 4.7 to 5.1 V. The valves are off by U13, section 9.5. What the processor and the display do there is not known, and the owner accepted that for revision A. Risk 32.
- **The ADC input of the ESP32-S3.** The data sheet gives no figure for the charge its sampling takes from a source of 60 kohm. Risk 33.
- **The die temperature against the board temperature.** The data sheet says only that the chip is warmer than its surroundings. The window of section 19.7 assumes 10 to 15 K. Risk 30.
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
| 63ZLJ470M12.5X20 | Rubycon ZLJ series | <https://www.lcsc.com/datasheet/C2940725.pdf> |
| SRN6045TA-470M | Bourns SRN6045TA | <https://www.lcsc.com/datasheet/C5151734.pdf> |
| HS20HS072RX | Hansheng datasheet v1.0, 2020-11-04 | <https://www.lcsc.com/datasheet/C5329582.pdf> |
| EC11J1525402 | ALPS product sheet | <https://www.lcsc.com/datasheet/C209762.pdf> |
| G5NB-1A-E | Omron G5NB, Chinese edition; English edition read in review A1 | <https://www.lcsc.com/datasheet/C48746.pdf>, <https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5nb.pdf> |
| AO3401A, SS34, SS210, M7, BAT54S, USBLC6-2SC6 | Maker datasheets, key ratings only | LCSC pages for C15127, C8678, C14996, C95872, C47546, C7519 |
| SMBJ6.0A | Littelfuse SMBJ series, revised 2025 (review A2) | <https://www.lcsc.com/datasheet/C83270.pdf> |
| 0451.500MRL | Littelfuse 451/453 series, revised 2015 (review A2) | <https://www.lcsc.com/datasheet/C142836.pdf> |
| BAT54S, figures 1 and 2 | Nexperia product data sheet, 1 July 2022 (review A2) | <https://www.lcsc.com/datasheet/C47546.pdf> |
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
2. **The release window is stated in full.** The proposal quoted the time at which Q2 is certainly off. The transistor's threshold spread means K1 may release much earlier. The window is 0.12 to 0.95 s, and section 7.3 says why that is acceptable. Review A2 widened it to 0.10 to 1.45 s.
3. **A read-back for the relay detector.** The proposal read back `DRV_EN` only. A stuck relay transistor would then be found by nothing, and the second detector could fail silently. GPIO7 reads the drain of Q2. To free it, the sensor supply enable moved to GPIO45. Sections 7.4 and 12.2.
4. **A stocked pulse-rated resistor exists.** The review found none in stock. The Yageo SR series is, and its 5 % parts have four times the pulse rating of its 1 % parts. Section 4.4.
5. **The output PTC.** The review noted that 20 A is not a guaranteed ceiling for a Class 2 source and left the part. I replaced it with one rated for 40 A. Section 6.6.
6. **The fold of the display tail.** The review's text said the fold reverses the pin order as the board sees it. It does not. Section 16.4.
7. **A bridge from the keep-alive net to the zero-cross net.** Not in the review. I estimated by hand that it could not run either detector, simulated it and found that it can hold the relay detector on. It cannot enable the other. Section 8.5. Review A2 found that it also holds the other detector once that one is on, and lowered R33 so that it holds neither.
8. **The operating temperature inside the enclosure** is 70 °C, from the enclosure concept's own estimate, not the 60 °C the first draft and the review used. PSRAM ECC becomes mandatory. Sections 3.3 and 6.7.

**After revision 2 of the mechanical contract.** The contract was merged
after this document, and two things here followed from it at schematic
capture.

1. **C1 is a through-hole part.** The contract allows no surface-mount part on the bottom side and fixes C1 there, lying down. It named the Rubycon PX part. Its data sheet gives 2000 h at 105 °C, against 5000 h for the Lelon part it would replace, so I chose the Rubycon ZLJ part in the same case instead: 10,000 h and about twice the ripple rating. Section 4.5 has the comparison. Netlist, BOM and the tables here changed with it.
2. **Section 16.4 quotes revision 2 coordinates.** J7 is at (60.44, 88.43) with pad 1 at y = 85.68. The fuse holders, C1 and K1 have the positions and the assembly method the contract gives.

**What I did not resolve.**

- **Regulator over-voltage.** If U1's switch shorts, the fail-safe analysis has no proof. Section 8.5 and risk 21. Resolved by review A2: section 4.8.
- **Q3 shorted** is found by no self-test. Section 8.5. Resolved by review A2: section 8.6.
- **Who solders C1 and K1.** The contract settles where they go. Whether the assembly house hand-solders parts on the side opposite the SMT has not been asked. Section 16.4.
- **Everything in section 20.** No bench work has been done.

## 25. What changed after review A2

[Hardware review A2](hardware-review-a2.md) is the second, independent
review of the fail-safe circuits that the changes of section 24 asked for.
It rebuilt the single-fault analysis from the netlist by fault injection,
ran tolerance corners of both detectors, and answered the two points this
document had left open. The reviewer changed the design files in the same
pull request. One blocking finding, seven major, thirteen minor.

**Circuit.** Every changed designator and net is in section 9 of the review.

| Finding | What was wrong | What was done | Section |
|---|---|---|---|
| B1 | The keep-alive pin was next to GPIO37, the PSRAM data strobe. A bridge kept both detectors on while the pin floated. | `KA` moved from GPIO38 to GPIO17. `ENC_SW` took GPIO38. | 8.5, 12.2 |
| M1 | A bridge to `AC_ZC` could hold both detectors. This document said one. | R33 from 10 kohm to 2.2 kohm | 8.5 |
| M2 | Regulator over-voltage, risk 21 | D25 on `V_BUCK`, F5 in the input of U1, new net `VIN_U1` | 4.8 |
| M4 | Q3 shorted was found by no test | R92 reads `V_DRV`. Net `DRV_EN_RB` is now `V_DRV_RB`. | 8.6 |
| m1 | The read-back level at GPIO42 had no margin that the data sheet backs | R92 from 10 kohm to 4.7 kohm | 8.6 |

**Procedure and contract.** No part changes; firmware does.

| Finding | What was wrong | What was done | Section |
|---|---|---|---|
| M3 | The self-test could not see a net that drives `KA` while the pin floats | Step 2b, the float test | 19.7 |
| M6 | A keep-alive with one phase of 1 ms is not held by detector B at the cold corner | The contract asks for phases of 2.5 ms or more | 7.3, 19.4 |
| M7 | The time limits of the stop test rejected healthy boards at tolerance corners | Wider absolute limits and a band around a value stored per board | 19.7 |
| m3 | An open R12 was not found by the weld check | Step 5b, the gate bleed check | 19.7 |
| m11 | GPIO17 is the default TXD pin of UART1 | Rule 16 | 19.1 |
| | A failed regulator should be reported, not only survived | Rule 15 | 4.8, 19.1 |

**Corrections to this document.**

- **Section 8.5** is rewritten from the fault injection. R35 shorted, floating inputs, an open gate of Q2 and bridged pins of U7 were wrong or missing. The complete table is appendix A of the review.
- **Sections 7.3 and 8.4** carry the corner results. The release window of detector B is wider than the first version said, and the statements about slow toggling are less firm.
- **Section 5.3**: a shorted C1 does not open a fuse.
- **Section 12.3**: the GPIO45 strap is checked against both data sheets. On this module it is not used.
- **Section 22**: the BAT54S data sheet does have leakage curves above 25 °C.

**What review A2 left open.**

- **A shorted LDO.** `3V3` at 4.95 V and a processor outside its ratings. Finding M5. A decision for the owner. Decided since: sections 9.5 and 26.
- **A resistor in `SENS_PWR_EN`.** Finding m6. Recommended, not fitted.
- **The release time of the relay detector alone.** Up to 1.45 s, and 2.19 s if the pin stops high, at one corner. The specification says "in about the same time" as the one second of the gate drive. Finding m10. The threshold drift of the AO3400A has to be measured before anyone decides whether that needs a change.
- **U7 without its supply pin** is no longer found, since the read-back moved from `DRV_EN` to `V_DRV`. Finding m13. Both layers still work with it in the model.
- **A smaller R89.** 220 ohm would hold the relay through a 1 ms phase. Review section 4.2. Not fitted, because it moves the single-edge results and wants its own pass through the fault table.
- **Everything in section 20.** No bench work has been done. Risks 21 and 27 to 31 are new or rewritten.

## 26. What changed after the firmware's questions

The firmware work that followed review A2 (pull request #37) listed nine
places where section 19 was ambiguous or could not be implemented as
written, and a few smaller ones. Issue #38 tracks them.
[Hardware answers A2](hardware-answers-a2.md) has every question with its
decision and the evidence, the owner's decision on finding M5, and the list
of what firmware must change. This section records what changed in this
document and in the design files.

**Circuit.** One change, for finding M5. Nothing that existed before has a
new value or a new connection.

| | |
|---|---|
| New parts | U13, TPS3701DDCR, LCSC C132788, SOT-23-thin with 6 pins. R13, 1 Mohm. R100, 110 kohm. C55, 10 nF. C56, 100 nF. All 0603 except U13. |
| New net | `OV_DIV`: U13 pin 4, R13, R100, C55 |
| Nets with new pins | `OC_TRIP_N`: U13 pin 6. `3V3`: U13 pin 5, R13, C56. `GND`: U13 pins 2 and 3, R100, C55, C56. |
| Not connected | U13 pin 1 |
| BOM | One new line, 99 in all. One more each of the 1 Mohm, 110 kohm, 10 nF and 100 nF parts. 0.41 USD more per 8-zone board at 100 boards. |
| Schematic | Sheet "Fail-safe gate and over-current latch", a new frame at the top right. `make` in `hardware/zoneflow-a/` passes the netlist comparison and ERC. |
| Layout | Section 9.5, last paragraph. No rule of section 8.5 changes. |

**Procedure.** No part changes; firmware does.

| Question | Decision | Section |
|---|---|---|
| 1. Rule 15 against step 1 | No separate regulator check and no new reading. A missing `V_BUCK` fails step 1 with both causes named. A person separates them by unplugging USB. | 19.1 rule 15 |
| 2. The normal range of `VIN_MON` | 0.60 to 1.20 V as a window mean, from a simulation of the supply corners. Firmware reads it, and step 0 requires it. | 19.10 |
| 3. Commissioning at room temperature | Automatic, not a button, and only while the die temperature reads 25 to 50 °C. | 19.7 |
| 4. Learning limits, and tB against tA | tA 0.33 to 0.85 s, tB 0.12 to 1.25 s. No ratio: it does not separate. An open bleed resistor of detector B at assembly needs an ohmmeter. | 19.7, 21 |
| 5. The commissioning cycle | Steps 0, 1, 3, 4a and 9. | 19.7 |
| 6. 2 s or 2.5 s | 2.5 s, and it is the wait of step 0. | 19.6 |
| 7. "In use" in the float test | Written out: backlight at 50 %, display redrawn without pause, PSRAM read without pause, 0x55 on UART0 without pause. | 19.7 |
| 8. Step 2b with a daily step 2 | Step 2b before every run, always. Step 2 may be daily. | 19.7 |
| 9. Steps 5 and 5b without current readings | Allowed, if firmware reports the condition for as long as it lasts. | 19.1 rule 19 |

**Found while answering.** Three things that nobody had asked about.

- **Step 2 failed healthy boards.** With 1.0 s of hold, two of 96 tolerance corners fail the pin-high test with no fault on the board. The hold is 2.0 s. Section 19.7, note to step 2.
- **The band of step 9 was too narrow** for a board that stores its value at 10 °C: 8 healthy stop tests fall outside 0.5 to 1.5. It is 0.40 to 1.70.
- **A brown-out looked like a relay fault.** With `VIN_DC` under 9 V and USB attached, step 1 reported a fault that needs repair. Step 0 now requires `VIN_MON`.

**Corrections to this document.**

- **Section 7.4**: GPIO42 reads `V_DRV`, not `DRV_EN`.
- **Section 8.6**: the lower limit of tA is 0.15 s, not 0.2 s.
- **Section 19.3**, row 3: the pull-down of `KA` is 2.2 kohm.
- **Section 19.6**: the polling cycle starts with step 0 and takes 2 s, not 1.2 s.
- **Section 3.2**: R13 and R100.

**Specification.** v0.7. The logic supply row asks for a detector that
removes the output-enable while the 3.3 V rail is above its range, and
records that a shorted linear regulator is accepted as a risk to the
processor and not to the valves.

**Simulation.** New in `hardware/sim/failsafe/`: `commission.py`,
`sequence.py`, `ldo.py` and `floatload.py`, each with its results, 6969
runs in all. New in
`hardware/sim/`: `vin_mon.cir`. The fault table and the bridge sweep are
run again for the netlist with U13 and the sequence with the longer step 2.
`hardware/sim/failsafe/README.md` says what each run shows and on which
netlist it ran.

**What this leaves open.**

- **Everything in section 20.** No bench work has been done. Risks 32 to 34 are new, and risks 27 and 30 are rewritten.
- **The processor at 5 V.** Accepted, not solved. Section 9.5.
- **An open bleed resistor of detector B on a new board.** Firmware cannot find it. Bring-up step 1 can.
- **The firmware.** It implements section 19 as review A2 left it. The list of changes is in the answers document and in the issue opened with it.

