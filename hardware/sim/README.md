# Simulations

ngspice netlists for the parts of the rev A design where a simulation
tells more than a hand calculation. `keepalive.cir`, `isense.cir`,
`supply.cir`, `relay_detector.cir` and `ka_bridge.cir` are the author's.
`keepalive_faults.cir`, `isense_corners.cir` and `zerocross.cir` were added
by review A1 (`docs/design/hardware-review-a1.md`).
`buck_overvoltage.cir`, `outstage_smallload.cir` and the directory
`failsafe/` were added by review A2 (`docs/design/hardware-review-a2.md`).
`vin_mon.cir` and four more runs in `failsafe/` were added with the answers
to the firmware's questions (`docs/design/hardware-answers-a2.md`).
The design is in
`docs/design/hardware-design.md`. No board exists. These runs check the
circuit as drawn against idealised parts. They do not replace a bench test.

`failsafe/` is different from the files here: it builds its ngspice decks
from `hardware/netlist.yaml`, injects every single fault into both
keep-alive detectors and what they drive, and runs tolerance corners. It has
its own `README.md`. The hand-written files below model one block each and
are kept. Section 11 of the review compares `failsafe/` with them where the
two overlap.

`study-failsafe-reduction/` is not about the product at all. It simulates a
circuit that is in no netlist: the reduced fail-safe that
`docs/design/failsafe-reduction-study.md` examines. It has its own
`README.md` and a `Makefile`.

Four of the older files predate one change of review A2.
`keepalive.cir`, `keepalive_faults.cir`, `relay_detector.cir` and
`ka_bridge.cir` have the pull-down R33 at 10 kohm. It is 2.2 kohm now. The
files are kept as merged, because the results quoted below and in design
sections 7.3 and 8.4 come from them. I ran each with only that value
changed, to see what it moves:

- **`keepalive.cir`**: every figure within 1.1 %.
- **`keepalive_faults.cir`**: half the lines within 1.1 %. The cases at the edge of enabling move more: 3 Hz enables after 1.0 s where it took 0.67 s, and the slowest enable at 10 Hz takes a fifth longer.
- **`relay_detector.cir`**: half the lines within 1.1 %. The gate margin of Q2 falls by 25 to 30 mV, because the pull-down loads the pin's high level.
- **`ka_bridge.cir`**: the reservoir of detector A reaches 0.18 to 0.40 V where it reached 0.89 to 1.03 V, and the gate of Q2 0.17 to 0.36 V where it reached 0.83 to 0.95 V. Detector A releases below 0.94 V and the lowest threshold of Q2 is 0.51 V, so the bridge now holds neither. That is the purpose of the change.

`failsafe/` reads the value from the netlist, so its fault table and
corners are for 2.2 kohm.

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

The keep-alive detector of section 8: the diode pump on GPIO17, the reservoir
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
latch of section 9. C28 and C29 are 330 pF since the BOM consolidation
(design section 27); they were 220 pF. Run with both values on 2026-11-11,
the table below is the same to the digits shown: the high-gain channel
gives 0.664935 V/A with 220 pF and 0.664932 V/A with 330 pF at 0.2 A, the
ground-shift ripple is 0.000943 V peak to peak with both, and the trip
comes 65.7 and 65.9 us after the short, one time step apart, in a path
these two capacitors are not in.

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

## vin_mon.cir

Added with the answers to the firmware's questions. The input stage of
`supply.cir` with the supply monitor added: R9 and R10, 300 kohm and
10 kohm, and C9 on the tap that GPIO3 reads. It answers "what is the normal
range of `VIN_MON`", design section 19.10. The reading is the mean of a
100 ms window. "Lowest" and "highest" are the extremes inside one window,
which is the half-wave ripple. The transformer is a sine source behind
0.92, 2.1 or 3.45 ohm, for 75, 40 and 20 VA by the 69 / VA ohm of design
section 6.6. Those are assumptions, not measured transformers.

| Supply | Transformer | Load | `VIN_DC`, mean | `VIN_MON`, mean | Lowest | Highest |
|---|---|---|---|---|---|---|
| 21.6 VAC, 50 Hz | 3.45 ohm | 5 W | 19.6 V | 0.634 V | 0.505 V | 0.741 V |
| 21.6 VAC, 50 Hz | 3.45 ohm | 1.75 W | 25.6 V | 0.826 V | 0.789 V | 0.857 V |
| 21.6 VAC, 50 Hz | 2.1 ohm | 5 W | 21.4 V | 0.690 V | 0.570 V | 0.791 V |
| 21.6 VAC, 50 Hz | 2.1 ohm | 1.75 W | 26.2 V | 0.845 V | 0.809 V | 0.876 V |
| 21.6 VAC, 60 Hz | 2.1 ohm | 1.75 W | 26.3 V | 0.847 V | 0.818 V | 0.871 V |
| 21.6 VAC, 50 Hz | 0.92 ohm | 0.6 W | 28.4 V | 0.916 V | 0.904 V | 0.926 V |
| 24 VAC, 50 Hz | 3.45 ohm | 1.75 W | 29.2 V | 0.943 V | 0.910 V | 0.970 V |
| 24 VAC, 50 Hz | 2.1 ohm | 5 W | 25.5 V | 0.822 V | 0.719 V | 0.908 V |
| 24 VAC, 50 Hz | 2.1 ohm | 1.75 W | 29.8 V | 0.961 V | 0.929 V | 0.988 V |
| 24 VAC, 60 Hz | 2.1 ohm | 1.75 W | 29.8 V | 0.962 V | 0.936 V | 0.984 V |
| 24 VAC, 50 Hz | 2.1 ohm | 0.6 W | 31.6 V | 1.020 V | 1.009 V | 1.029 V |
| 24 VAC, 50 Hz | 0.92 ohm | 0.6 W | 31.9 V | 1.028 V | 1.017 V | 1.037 V |
| 26.4 VAC, 50 Hz | 3.45 ohm | 5 W | 28.0 V | 0.905 V | 0.813 V | 0.984 V |
| 26.4 VAC, 50 Hz | 2.1 ohm | 1.75 W | 33.3 V | 1.075 V | 1.046 V | 1.100 V |
| 26.4 VAC, 50 Hz | 0.92 ohm | 0.6 W | 35.3 V | 1.139 V | 1.129 V | 1.147 V |
| 32 VAC, 50 Hz | 2.1 ohm | 1.75 W | 41.5 V | 1.340 V | 1.316 V | 1.360 V |
| 32 VAC, 50 Hz | 0.92 ohm | 0.6 W | 43.3 V | 1.397 V | 1.389 V | 1.404 V |
| 32 VAC, 50 Hz | 0.92 ohm | 0.05 W | 44.3 V | 1.430 V | 1.429 V | 1.431 V |
| 18 VAC, 50 Hz | 2.1 ohm | 5 W | 14.3 V | 0.460 V | 0.278 V | 0.602 V |
| 16 VAC, 50 Hz | 2.1 ohm | 1.75 W | 17.6 V | 0.568 V | 0.515 V | 0.613 V |
| 16 VAC, 50 Hz | 2.1 ohm | 3 W | 15.0 V | 0.484 V | 0.382 V | 0.569 V |
| 12 VAC, 50 Hz | 2.1 ohm | 1.75 W | 10.8 V | 0.348 V | 0.265 V | 0.416 V |

What it showed:

- **The specified supply**, 21.6 to 26.4 VAC with 20 to 75 VA and 0.6 to 5 W, reads 0.63 to 1.14 V. With 1.9 % for the divider and 10 mV for the ADC that is 0.61 to 1.17 V. Design section 19.10 takes 0.60 to 1.20 V as normal.
- **The mean is the reading to use.** Inside one window the tap moves by up to 0.24 V at the heavy corners.
- **A sag to 16 VAC**, which the logic supply still rides through, reads 0.48 to 0.57 V. Step 0 of the self-test waits there, so that a brown-out is not taken for a relay fault.
- **The mean of `VIN_DC` at 24 VAC and the typical load**, 29.8 V, agrees with the 28.7 to 30.9 V of `supply.cir`.

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

## buck_overvoltage.cir

Added by review A2. The switch inside U1 fails short and `VIN_DC` reaches
`V_BUCK` through L1 (design section 4.8, risk 21). The file runs the fault
with no protection, then with the clamp D25 (SMBJ6.0A on `V_BUCK`) and the
fuse F5 (0451.500MRL in the input of U1) that the review added. F5 is a
resistor whose value rises by a factor of 100 000 when the integral of the
square of its current reaches a set value: the nominal melting I²t of the
data sheet, 0.0824 A²s, or three times that. D25 is a threshold and a
resistance fitted to its data sheet limits.

| Case | `V_BUCK` | Peak current | F5 opens after | D25 takes |
|---|---|---|---|---|
| No protection, `VIN_DC` 30 V | Past 7 V after 14 us, peak 43.1 V | | | |
| No protection, `VIN_DC` 44.3 V | Past 7 V after 11 us, peak 64.8 V | | | |
| Welded switch, 44.3 V, L1 saturated | 9.81 V | 49.0 A | 0.04 ms, 0.13 ms at three times the I²t | 0.016 J, 0.049 J |
| Welded switch, 44.3 V, L1 47 uH | 9.26 to 9.33 V | 37.9 to 39.5 A | 0.10 ms, 0.21 ms | 0.022 J, 0.059 J |
| Welded switch, 30 V | 8.59 to 8.87 V | 24.7 to 30.2 A | 0.10 to 0.18 ms, 18 ms | 0.022 to 0.029 J, 0.16 J |
| Welded switch, 17 V | 7.91 to 8.01 V | 11.3 to 13.1 A | 2.7 ms, 58 ms | 0.091 J, 0.39 J |
| Switch 0.725 ohm, 17 to 44.3 V | 7.68 to 8.64 V | 6.5 to 25.5 A | 0.15 to 20 ms, 1.2 to 63 ms | 0.027 to 0.14 J, 0.12 to 0.45 J |
| Switch 5 ohm, 17 to 44.3 V | 7.46 to 7.68 V | 2.2 to 6.8 A | 22 to 61 ms, 160 to 198 ms | 0.15 to 0.23 J, 0.62 to 0.69 J |
| D25 at its lowest clamp, welded switch, 17 and 44.3 V | 7.09 and 8.21 V | 14.4 to 14.6 and 51.4 to 51.5 A | | |
| 40 and 75 VA instead of 20 VA | The same within 0.01 V | The same within 0.2 A | | |
| The worst case with C1 at 0.099, 0.033 and 0.010 ohm | 9.61, 9.81 and 9.89 V | 45.1, 49.0 and 50.7 A | 0.047, 0.042 and 0.040 ms | |

Where two figures are given, the first is for F5 opening at its nominal
melting I²t and the second at three times that. In the cases that end
within 10 ms, the pulse curve of the SMBJ data sheet (600 W at 1 ms, falling
with the square root of the pulse length) allows 4.4 to 11 times the energy
D25 takes.

C1 is the Rubycon 63ZLJ470M12.5X20 that `main` changed to while review A2
ran. Its sheet gives an impedance of at most 0.033 ohm at 20 °C, which the
runs use. The Lelon part before it had 0.082 ohm, and with that the worst
peak was 46 A.

The last block holds F5 closed and reports the current the rectifier
delivers through R1 into the clamp, which is what would flow if F5 were not
there:

| Transformer | Open-circuit voltage | Current in F5 | Of F5's rating | D25 | R1 |
|---|---|---|---|---|---|
| 20 VA | 24.2 V | 1.62 A rms, 3.6 A peak | 323 % | 5.5 W | 8.7 W |
| 20 VA | 32 V | 2.37 A rms | 474 % | 9.0 W | 18 W |
| 40 VA | 27.5 V | 2.56 A rms | 512 % | 9.8 W | 21 W |
| 75 VA | 24.2 V | 2.52 A rms | 503 % | 9.5 W | 20 W |
| 75 VA | 32 V | 3.67 A rms, 7.8 A peak | 734 % | 15 W | 42 W |

What it showed:

- **The fault needs protection.** Without it `V_BUCK` is past the 7 V limit of U11 in 11 to 14 us and reaches 43 to 65 V.
- **The clamp holds.** 9.8 V at most, with the part that clamps highest, and 9.9 V if C1 is three times better than its limit. `3V3` stays regulated: U3 takes 20 V.
- **The peak current reaches the interrupting rating of F5.** 49 to 52 A in the one worst case, at 44.3 V with a welded switch and a saturated inductor, against 50 A at 125 V and 300 A at 32 V DC in the data sheet. At 30 V it is 25 to 30 A.
- **F5 opens in every case**, on the charge of C1 from about 23 V up and with the help of the rectifier below that.
- **A fuse rated for the board's 3 A input could not do this.** The rectifier and R1 limit the current to 1.6 to 3.7 A rms. The time-lag input fuses F1 and F4 would not open, and R1 would burn at 9 to 42 W.
- **Limits of the model**: a fuse heats without loss only for milliseconds, so the rows that end after tens of milliseconds are optimistic. The data sheet row that bounds them is 5 s at 200 %. The resistance of the failed switch is a parameter, not a known value. No arc is modelled.

## outstage_smallload.cir

Added by review A2. The output stage of section 6 with a small load, which
is under the triac's latching current for part of every half-cycle. In that
part the load current flows through the opto-triac, R103 and the gate, and
the output drops several volts instead of about one. The file is not a triac
model: it is a voltage drop that depends on the current and on a latch with
hysteresis (latched above IL, released below IH). The loads are assumed: a
relay coil of 160 ohm and 1.44 H for 50 mA, a valve of 60 ohm and 0.47 H
for 150 mA.

| Load | Supply | Triac | Load voltage | Lost | Latched for |
|---|---|---|---|---|---|
| 50 mA coil | 21.6 VAC | Typical (IGT 5, IH 8, IL 12 mA: my estimate) | 21.24 V | 1.7 % | 89 % of the time |
| 50 mA coil | 21.6 VAC | Data sheet limits at 25 °C (10, 15, 25 mA) | 20.83 V | 3.6 % | 77 % |
| 50 mA coil | 21.6 VAC | Data sheet limits at -20 °C (17.5, 22, 36 mA) | 20.32 V | 5.9 % | 65 % |
| 50 mA coil | 24 VAC | Data sheet limits at -20 °C | 22.88 V | 4.7 % | 70 % |
| 150 mA valve | 21.6 VAC | Data sheet limits at -20 °C | 21.03 V | 2.7 % | 89 % |
| 150 mA valve | 24 VAC | Data sheet limits at -20 °C | 23.46 V | 2.2 % | 90 % |

What it showed:

- **A 50 mA coil keeps 94 % of its voltage at the worst corner.** That is 20.3 V from a 21.6 VAC supply. A coil that pulls in at 80 % of 24 V works. One that needs 85 %, 20.4 V, has no margin at -20 °C on the lowest supply with a triac at every data sheet limit.
- **The T810 is no worse than the T410 here.** Their trigger, holding and latching limits are the same, so this result does not depend on the change of triac.

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
