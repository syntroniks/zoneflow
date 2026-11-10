# Simulations

ngspice netlists for the parts of the rev A design where a simulation
tells more than a hand calculation. `keepalive.cir`, `isense.cir`,
`supply.cir`, `relay_detector.cir` and `ka_bridge.cir` are the author's.
`keepalive_faults.cir`, `isense_corners.cir` and `zerocross.cir` were added
by review A1 (`docs/design/hardware-review-a1.md`). The design is in
`docs/design/hardware-design.md`. No board exists. These runs check the
circuit as drawn against idealised parts. They do not replace a bench test.

Run each file with `ngspice -b <file>`. Every result line starts with
`RESULT`, `FAULT` or `CHECK`. The results below are from ngspice 47 on
2026-11-10, with every file re-run after the changes that followed the
review. Run them all with:

    for f in *.cir; do ngspice -b "$f" | grep -E 'RESULT|FAULT|CHECK'; done

Two files of the review are gone. `relay_drive.cir` modelled the relay hold
network D21, R13, R14 and C50, which the review showed did nothing and which
has been removed. `relay_pump_proposal.cir` was the review's proposal for a
second detector. `relay_detector.cir` replaces it with the circuit that is
now in the netlist. Both old files are in the git history at commit 2522fd3.

All semiconductor models are generic. The Schottky diode is a plain diode
model with a BAT54-like forward drop. Comparators are ideal open-collector
switches with the LM2901's worst-case 500 nA input bias current added as
current sources. The op-amp is a single-pole, 1 MHz block whose output clips
25 mV from each rail. The NAND gates are behavioural. Component tolerances are
swept only where a result line says so.

## keepalive.cir

The keep-alive detector of section 8: the diode pump on GPIO38, the reservoir
C23 with its bleed, and comparator U6A with hysteresis. The bleed is R30 and
R91, 110 kohm each, in parallel; before the review it was one 56 kohm
resistor, and the figures moved by 2 % or less. The 3.3 V rail
ramps up in 1 ms at the start of every run, so power-up is included.

| Stimulus on `KA` | `KA_OK` |
|---|---|
| 10 Hz square wave for 0.95 s | High 104 ms after the first edge. Low 0.59 s after the last. |
| 20 Hz | High after 53 ms. Low after 0.65 s. |
| 100 Hz | High after 14 ms. Low after 0.73 s. |
| 1 kHz | High after 8 ms. Low after 0.74 s. |
| 2 Hz for 2.4 s | Never high. `KA_N2` peaks at 1.41 V. The threshold is 1.55 V. |
| One rising edge, then stuck high | Never high. `KA_N2` peaks at 1.00 V. |
| Stuck high from power-up | Never high |
| Stuck low from power-up | Never high |
| 100 Hz, then stuck high | Low 1.09 s after the last edge |
| 100 Hz, C23 at 7 uF | Low after 0.51 s |
| 1 kHz, C23 at 12 uF | Low after 0.88 s |

What it showed:

- **Hold-up**: 0.5 to 0.9 s when the pin stops low, longer when it stops high because C22 then discharges together with C23.
- **Slow toggling**: 2 Hz does not enable drive, with 9 % margin on the threshold.
- **Power-up**: an earlier version of the circuit had a capacitor on the reference and none on the comparator input. Its runs showed `KA_OK` high at the start, while the reference was still charging. Moving the capacitor to the input, C24, removed that. The stuck-low and stuck-high runs are the check: `KA_OK` never rises above 17 mV.

## isense.cir

The current-sense amplifiers of section 10, the window comparators and the
latch of section 9.

| Run | Result |
|---|---|
| 50 mA rms sine | `ADC_LO` 6.66 mV rms, 133.3 mV/A. `ADC_HI` 33.2 mV rms, 664.9 mV/A. |
| 0.2 A rms | 133.3 mV/A and 664.9 mV/A |
| 0.5 A rms | 133.3 mV/A and 664.9 mV/A. `ADC_HI` swings 0.32 to 1.26 V. |
| 1.5 A rms | `ADC_LO` 133.3 mV/A, swing 0.50 to 1.07 V. `ADC_HI` clips at 0.025 V. |
| 2.5 A rms | `ADC_LO` 133.3 mV/A, swing 0.32 to 1.26 V. No trip. |
| 20 mV peak ground shift, no current | 0.9 mV peak to peak on each output |
| 10 A rms short added to a 1 A load | `OC_TRIP_N` low 66 us after the short starts. `DRV_EN_N` high in the same microsecond. |
| Latch | Stays set after the short ends. Clears when `KA_OK` goes low. Drive returns when `KA_OK` returns. |

What it showed:

- **Gain**: both channels match the hand calculation, and the bias sits at 0.787 V with the resistor chain loaded by the amplifiers.
- **Trip level**: the largest legitimate current, 2.5 A rms, stays inside the window.
- **Kelvin sensing**: the difference amplifiers reject a ground shift between the shunt and the amplifier by a factor of about 40.

## supply.cir

The half-wave rectifier, R1 and the bulk capacitor C1 of section 4, with a
constant-power load standing in for the buck converter. R1 is 3.0 ohm, the
surge-rated part that replaced the 3.3 ohm one. The transformer is a
sine source with 2.1 ohm in series. That impedance is an assumption for a
40 VA unit, not a measured transformer.

| Supply | Load | `VIN_DC` | Diode peak | Capacitor ripple | R1 loss |
|---|---|---|---|---|---|
| 24 VAC, 50 Hz | 1.75 W | 28.7 to 30.9 V | 0.62 A | 0.16 A rms | 0.09 W |
| 24 VAC, 60 Hz | 1.75 W | 28.9 to 30.7 V | 0.62 A | 0.16 A rms | 0.09 W |
| 21.6 VAC, 50 Hz | 1.75 W | 25.0 to 27.4 V | 0.65 A | 0.17 A rms | 0.10 W |
| 21.6 VAC, 50 Hz | 5.0 W | 17.3 to 25.0 V | 1.51 A | 0.48 A rms | 0.85 W |
| 18 VAC, 50 Hz | 5.0 W | 8.1 to 19.2 V | 1.93 A | 0.66 A rms | 1.72 W |
| 16 VAC, 50 Hz | 1.75 W | 15.8 to 19.3 V | 0.77 A | 0.23 A rms | 0.18 W |
| 16 VAC, 50 Hz | 3.0 W | 11.5 to 18.1 V | 1.23 A | 0.40 A rms | 0.60 W |
| 32 VAC, 50 Hz | 0.05 W | 44.3 V | 0.05 A | under 0.01 A rms | 0 |
| 32 VAC, 50 Hz | 5.0 W | 35.7 to 40.3 V | 1.17 A | 0.32 A rms | 0.37 W |

What it showed:

- **Ripple**: at the typical load the valley stays above 24 V down to 21.6 VAC.
- **Brownout**: the valley stays above the buck's 8.4 V lockout for a sag to 16 VAC at up to 3 W. An 18 VAC sag with every consumer at its limit, 5 W, does not hold.
- **R1**: 1 W is enough for every case except that last one, which is a transient condition.

## zerocross.cir

Added by review A1. The AC-presence and zero-cross input of section 11: R55,
the BAT54S clamp D9, R88, R56, C34 and comparator U6D. It compares the circuit
with R88 against the first draft, where the clamp node went straight to the
LM2901 pin. The Schottky model is fitted to the BAT54S maximum forward voltage
at 25 °C, so the 25 °C rows are a worst-case part. The -20 °C and 60 °C rows
scale that model and are not data sheet limits.

| R88 | Temperature | Supply | Comparator pin | `AC_ZC` rises after the crossing | falls before the next |
|---|---|---|---|---|---|
| none | 25 °C | 24 VAC | -0.28 to 3.09 V | 0.28 ms | 0.08 ms |
| none | 25 °C | 32 VAC | -0.29 to 3.53 V | 0.23 ms | 0.03 ms |
| none | -20 °C | 32 VAC | -0.36 to 3.60 V | 0.23 ms | 0.03 ms |
| 10k | 25 °C | 24 VAC | -0.14 to 1.78 V | 0.30 ms | 0.09 ms |
| 10k | -20 °C | 32 VAC | -0.18 to 1.83 V | 0.25 ms | 0.04 ms |
| 10k | 60 °C | 24 VAC | -0.12 to 1.76 V | 0.30 ms | 0.09 ms |

What it showed:

- **Rating**: without R88 the pin sits at the LM2901's -0.3 V absolute maximum at 25 °C and goes past it when cold. With R88 it stays above -0.2 V.
- **Timing**: the rising edge lags the zero crossing by 0.25 to 0.30 ms and the falling edge leads the next one by 0.04 to 0.09 ms. The two are not symmetric, because the C34 delay adds to one and subtracts from the other. Firmware needs a separate offset for each edge.

## keepalive_faults.cir

Added by review A1 and updated for the two bleed resistors. One of R30 and
R91 open now leaves 110 kohm, and that is the fault the file runs. The same
detector as `keepalive.cir`, run 47 times: every
single component fault, toggle rates from 1 Hz to 1 MHz, and tolerance and
temperature corners. Result lines start `RESULT FAULT`, `RESULT RATE` or
`RESULT CORNER`. ngspice also prints "out of interval" errors for events that
do not happen, such as `KA_OK` never going low. That is expected.

Faults that leave `KA_OK` high when it should be low:

| Fault | `KA_OK` |
|---|---|
| R31 opens while enabled | Stays at 3.3 V |
| R34 open | High from power-up with no keep-alive at all |
| C22 short | Normal while the pin toggles. High with the pin held high and no edges. |
| U6A output never pulls low | High from power-up |

Faults that leave it low or harmless: R29 open, D7A short, D7B short or open,
C23 short, C24 short, R35 open and R31 open from power-up never enable. C23
open makes `KA_OK` follow the keep-alive. R32 open, R33 open and R36 open still
work; R32 open shortens the hold-up to 0.45 s. R30 or R91 open doubles the
bleed resistance: `KA_OK` goes low 1.74 s after the last edge at 27 °C and
1.35 s at 60 °C. Before the review this fault, with a single bleed resistor,
left `KA_OK` high for ever.

| Toggle rate | `KA_OK` |
|---|---|
| 1 Hz | Never high. Reservoir peaks at 1.15 V. |
| 2 Hz, nominal parts | Never high. 1.41 V. |
| 2 Hz, C23 30 % low, C22 20 % high, -15 mV offset, `3V3` 3.4 V | High after 0.50 s |
| 3 Hz, 5 Hz | High after 0.67 s and 0.40 s |
| 25 Hz | High after 43 ms |
| 50 kHz, 1 MHz | High after 9 ms and 29 ms |
| 100 Hz at 2 % and at 98 % duty | High after 0.11 s and 0.09 s |

| Corner | Hold-up after the last edge |
|---|---|
| Nominal parts at -20, 27, 60, 85 °C | 0.70, 0.73, 0.66, 0.43 s |
| Short corner at -20, 60, 85 °C | 0.49, 0.46, 0.30 s |
| Long corner at -20 °C | 0.78 s |
| Long corner, pin stops high | 1.17 s |

The slowest enable, at the slow corner and -20 °C, is 41 ms at 100 Hz, 0.16 s
at 25 Hz and 0.50 s at 10 Hz. With `3V3` ramping over 0.1 s or 1 s, `KA_OK`
stays low when the pin is low. With the pin tied to the rail it follows the
rail to 0.95 V and falls again.

What it showed:

- **Single faults**: four faults still leave this detector enabled with no working keep-alive. Before the bleed was split there were five. This was finding B1 of the review. The relay now has its own detector, `relay_detector.cir`, so that none of the four leaves a valve on.
- **Rates**: the detector has no upper frequency limit and does not reliably reject slow toggling above about 2 Hz. It detects edges, not a correct rate.
- **Temperature**: the hold-up shortens above 60 °C because Schottky leakage adds to the bleed resistor. The leakage figure comes from a generic model.

## isense_corners.cir

Added by review A1. The amplifier U8A, the threshold chain and the window
comparators of `isense.cir`, with every tolerance as a parameter. A slow
current ramp of each polarity finds the trip current. `ISENSE_LO` falls for
positive shunt current, so U6C and `VL` trip that polarity and U6B and `VH`
the other. U8B and its small load on `VB` are left out.

| Corner | Positive current | Negative current |
|---|---|---|
| Nominal | 4.56 A | 4.67 A |
| Lowest trip | 4.05 A | 4.03 A |
| Highest trip | 5.10 A | 5.36 A |

The lowest-trip corner is 1 % resistors and shunt all in the direction of
more gain and a narrower window, `3V3` at 3.2 V, 4.5 mV of op-amp offset and
15 mV of comparator offset towards the threshold. The highest-trip corner is
the opposite.

What it showed:

- **Trip window**: 4.0 to 5.4 A peak, wider than the 4.2 to 5.1 A of the hand calculation, which left out the resistors that set the amplifier's rest level.
- **Margin**: the lowest trip is 13 % above the largest current the specification allows, 3.54 A peak.

## relay_detector.cir

The relay keep-alive detector of design section 7.3: R89, C51, D22 and the
reservoir C50 with its bleed R14 and R90, ending at the gate of Q2. It
replaces the review's proposal file. The values differ from the proposal:
100 kohm bleed resistors, 10 uF and 4.7 uF, where the proposal had 1 Mohm
and 1 uF.

The run reports the gate voltage against the AO3400A's threshold range. The
data sheet gives 0.65 to 1.45 V at 25 °C. For -20 °C and 70 °C the file
shifts both limits by -3 mV/K, which is an assumption. Above the highest
threshold every part is on; below the lowest every part is off. K1 can
release anywhere between.

| Case | Gate while toggling | Above the highest threshold after the first edge | Below the highest threshold after the last edge | Below the lowest |
|---|---|---|---|---|
| Nominal, 100 Hz, -20 °C | 2.54 V | 40 ms | 0.23 s | 0.58 s |
| Nominal, 100 Hz, 27 °C | 2.69 V | 30 ms | 0.31 s | 0.70 s |
| Nominal, 100 Hz, 70 °C | 2.81 V | 22 ms | 0.32 s | 0.65 s |
| C50 7.0 uF, C51 3.3 uF, 3.2 V, 25 Hz, -20 °C | 2.09 V | 82 ms | 0.12 s | |
| The same at 27 and 70 °C | 2.23 and 2.27 V | 46 and 42 ms | 0.17 and 0.18 s | |
| The same at 100 Hz, -20 and 70 °C | 2.42 and 2.69 V | | 0.15 and 0.21 s | |
| C50 11 uF, C51 5.2 uF, 3.4 V, pin stops low, -20, 27, 70 °C | | | | 0.67, 0.80, 0.74 s |
| The same, pin stops high | | | | 1.01, 1.21, 1.12 s |
| The same, 27 °C diodes read at the 70 °C threshold, pin low and high | | | | 0.93 and 1.41 s |
| One rising edge, then the pin stays high | Peaks at 1.01 to 1.34 V | | | 0.63 s at most |

Single faults, nominal parts at 27 °C:

| Fault | Result |
|---|---|
| R14 or R90 open | Below the lowest threshold after 1.45 s. With the large capacitors and the 70 °C threshold, 1.91 s, and 2.86 s if the pin stops high. |
| C51 short | Gate decays to zero if the pin stops low. **Stays at 3.07 V if the pin stops high.** |
| D22 ground diode open | Gate at most 0.29 V |
| D22 ground diode short, D22 output diode open, R89 open, C50 short | Gate stays at zero |
| D22 output diode short | Gate swings to 0.85 V at most. At zero 0.1 s after the pin stops low, below 0.65 V 0.06 s after it stops high. |
| R89 short | Gate 2.87 V while toggling, off after 0.74 s |
| C50 open | Coil current swings between 0 and 40 mA at 100 Hz. Gate at zero 10 ms after the pin stops low, below 0.65 V 0.36 s after it stops high. |

The last block adds a level-1 MOSFET with a 1.05 V threshold and the coil of
K1 as 125 ohm and 0.2 H. The inductance is assumed; Omron gives none. Coil
current is 42 mA while toggling and falls from 8 mA to 2 mA between 0.456 and
0.462 s after the last edge, with the gate at 1.06 V. So the relay follows
the threshold crossing closely, which is what the table assumes.

What it showed:

- **Release window**: 0.12 to 0.95 s after the last edge, 1.4 s if the pin stops high. Most of the width is the threshold spread of the transistor.
- **Temperature**: the release time moves by 7 % between 27 and 70 °C. In the proposal file it halved, because the bleed current there was the same size as the Schottky leakage of the model.
- **Gate drive**: 2.09 V at the worst corner against a highest threshold of 1.59 V there.
- **One dangerous fault**: a shorted C51 with the pin high. The other detector covers it.
- **Limits of the model**: generic diode with a leakage that the BAT54S data sheet does not limit above 25 °C, capacitor ranges that are my allowance for X5R parts, and an assumed threshold drift.

## ka_bridge.cir

A layout fault, not a component fault: the keep-alive net bridged to
`AC_ZC`, the zero-cross signal, which toggles at mains frequency with no
firmware behind it. The ESP32 pin floats, as in reset. The run asks how far
the two reservoirs charge. Capacitors are at the values that charge them
furthest.

| Mains | Temperature | `KA` peak | `KA_N2`, enables at 1.55 V | `RLY_G2`, thresholds 0.51 to 1.59 V |
|---|---|---|---|---|
| 50 Hz | -20 °C | 1.41 V | 0.89 V | 0.83 V |
| 50 Hz | 27 °C | 1.37 V | 1.02 V | 0.95 V |
| 50 Hz | 70 °C | 1.25 V | 1.03 V | 0.95 V |
| 60 Hz | -20, 27, 70 °C | 1.40, 1.36, 1.24 V | 0.89, 1.01, 1.03 V | 0.83, 0.95, 0.95 V |

What it showed:

- **The gate-drive detector is not enabled.** Its reservoir stays 0.5 V short of the threshold.
- **The relay detector can be held on.** 0.83 to 0.95 V turns on a Q2 from the low half of the threshold range. A hand estimate had said neither reservoir would charge. It was wrong.
- **So this bridge defeats one detector, not both.** The design document's layout rules keep `KA` away from `AC_ZC` anyway, and away from the backlight PWM net, which could run both.
