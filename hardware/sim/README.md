# Simulations

ngspice netlists for the parts of the rev A design where a simulation
tells more than a hand calculation. `keepalive.cir`, `isense.cir` and
`supply.cir` are the author's. The others were added by review A1
(`docs/design/hardware-review-a1.md`). The design is in
`docs/design/hardware-design.md`. No board exists. These runs check the
circuit as drawn against idealised parts. They do not replace a bench test.

Run each file with `ngspice -b <file>`. Every result line starts with
`RESULT`. The results below are from ngspice 47 on 2026-11-10.

All semiconductor models are generic. The Schottky diode is a plain diode
model with a BAT54-like forward drop. Comparators are ideal open-collector
switches with the LM2901's worst-case 500 nA input bias current added as
current sources. The op-amp is a single-pole, 1 MHz block whose output clips
25 mV from each rail. The NAND gates are behavioural. Component tolerances are
swept only where a result line says so.

## keepalive.cir

The keep-alive detector of section 8: the diode pump on GPIO38, the reservoir
C23 with its bleed R30, and comparator U6A with hysteresis. The 3.3 V rail
ramps up in 1 ms at the start of every run, so power-up is included.

| Stimulus on `KA` | `KA_OK` |
|---|---|
| 10 Hz square wave for 0.95 s | High 104 ms after the first edge. Low 0.61 s after the last. |
| 20 Hz | High after 53 ms. Low after 0.66 s. |
| 100 Hz | High after 14 ms. Low after 0.74 s. |
| 1 kHz | High after 8 ms. Low after 0.75 s. |
| 2 Hz for 2.4 s | Never high. `KA_N2` peaks at 1.42 V. The threshold is 1.55 V. |
| One rising edge, then stuck high | Never high. `KA_N2` peaks at 1.00 V. |
| Stuck high from power-up | Never high |
| Stuck low from power-up | Never high |
| 100 Hz, then stuck high | Low 1.11 s after the last edge |
| 100 Hz, C23 at 7 uF | Low after 0.52 s |
| 1 kHz, C23 at 12 uF | Low after 0.90 s |

What it showed:

- **Hold-up**: 0.5 to 0.9 s when the pin stops low, longer when it stops high because C22 then discharges together with C23.
- **Slow toggling**: 2 Hz does not enable drive, with 8 % margin on the threshold.
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
constant-power load standing in for the buck converter. The transformer is a
sine source with 2.1 ohm in series. That impedance is an assumption for a
40 VA unit, not a measured transformer.

| Supply | Load | `VIN_DC` | Diode peak | Capacitor ripple | R1 loss |
|---|---|---|---|---|---|
| 24 VAC, 50 Hz | 1.75 W | 28.6 to 30.7 V | 0.61 A | 0.16 A rms | 0.09 W |
| 24 VAC, 60 Hz | 1.75 W | 28.8 to 30.6 V | 0.61 A | 0.16 A rms | 0.09 W |
| 21.6 VAC, 50 Hz | 1.75 W | 24.8 to 27.3 V | 0.64 A | 0.17 A rms | 0.11 W |
| 21.6 VAC, 50 Hz | 5.0 W | 16.9 to 24.7 V | 1.50 A | 0.48 A rms | 0.95 W |
| 18 VAC, 50 Hz | 5.0 W | 6.9 to 18.7 V | 2.00 A | 0.69 A rms | 2.11 W |
| 16 VAC, 50 Hz | 1.75 W | 15.6 to 19.1 V | 0.76 A | 0.23 A rms | 0.20 W |
| 16 VAC, 50 Hz | 3.0 W | 11.2 to 17.8 V | 1.23 A | 0.40 A rms | 0.67 W |
| 32 VAC, 50 Hz | 0.05 W | 44.3 V | 0.05 A | under 0.01 A rms | 0 |
| 32 VAC, 50 Hz | 5.0 W | 35.4 to 40.1 V | 1.16 A | 0.32 A rms | 0.40 W |

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

Added by review A1. The same detector as `keepalive.cir`, run 47 times: every
single component fault, toggle rates from 1 Hz to 1 MHz, and tolerance and
temperature corners. Result lines start `RESULT FAULT`, `RESULT RATE` or
`RESULT CORNER`. ngspice also prints "out of interval" errors for events that
do not happen, such as `KA_OK` never going low. That is expected.

Faults that leave `KA_OK` high when it should be low:

| Fault | `KA_OK` |
|---|---|
| R30 open | High 13 ms after the first edge and never low again. The reservoir rises to 3.26 V. Same at 60 °C. |
| R31 opens while enabled | Stays at 3.3 V |
| R34 open | High from power-up with no keep-alive at all |
| C22 short | Normal while the pin toggles. High with the pin held high and no edges. |
| U6A output never pulls low | High from power-up |

Faults that leave it low or harmless: R29 open, D7A short, D7B short or open,
C23 short, C24 short, R35 open and R31 open from power-up never enable. C23
open makes `KA_OK` follow the keep-alive. R32 open, R33 open and R36 open still
work; R32 open shortens the hold-up to 0.46 s.

| Toggle rate | `KA_OK` |
|---|---|
| 1 Hz | Never high. Reservoir peaks at 1.16 V. |
| 2 Hz, nominal parts | Never high. 1.42 V. |
| 2 Hz, C23 30 % low, C22 20 % high, -15 mV offset, `3V3` 3.4 V | High after 0.50 s |
| 3 Hz, 5 Hz | High after 0.67 s and 0.40 s |
| 25 Hz | High after 43 ms |
| 50 kHz, 1 MHz | High after 9 ms and 28 ms |
| 100 Hz at 2 % and at 98 % duty | High after 0.11 s and 0.09 s |

| Corner | Hold-up after the last edge |
|---|---|
| Nominal parts at -20, 27, 60, 85 °C | 0.71, 0.74, 0.67, 0.44 s |
| Short corner at -20, 60, 85 °C | 0.50, 0.47, 0.30 s |
| Long corner at -20 °C | 0.80 s |
| Long corner, pin stops high | 1.19 s |

The slowest enable, at the slow corner and -20 °C, is 41 ms at 100 Hz, 0.16 s
at 25 Hz and 0.50 s at 10 Hz. With `3V3` ramping over 0.1 s or 1 s, `KA_OK`
stays low when the pin is low. With the pin tied to the rail it follows the
rail to 0.95 V and falls again.

What it showed:

- **Single faults**: five faults leave drive enabled with no working keep-alive. This is finding B1 of the review.
- **Rates**: the detector has no upper frequency limit and does not reliably reject slow toggling above about 2 Hz. It detects edges, not a correct rate.
- **Temperature**: the hold-up shortens above 60 °C because Schottky leakage adds to the bleed resistor. The leakage figure comes from a generic model.

## relay_drive.cir

Added by review A1. The K1 coil driver of section 7.2: Q1 on expander bit P05,
Q2 on `DRV_EN` with the hold network D21, R14 and C50. `DRV_EN` falls at
100 ms and, as on the board, the expander pin goes high impedance at the same
moment. The coil inductance is not in the Omron data sheet, so the run tries
three values.

| Coil inductance | Q1 gate under 1.05 V | Q2 gate under 1.05 V | Coil current under 4 mA |
|---|---|---|---|
| 50 mH | 0.11 ms | 45.9 ms | 0.7 ms |
| 100 mH | 0.11 ms | 45.9 ms | 1.3 ms |
| 300 mH | 0.11 ms | 45.9 ms | 3.8 ms |

What it showed:

- **The hold does not hold**: Q2's gate stays up for 46 ms as designed, but Q1 is in series and turns off in 0.1 ms, because resetting the expander releases its gate. The coil has lost its current within 4 ms. K1 opens within its own release time, 10 ms at most, not 30 to 50 ms later.

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
