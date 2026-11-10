# Simulations

ngspice netlists for the three parts of the rev A design where a simulation
tells more than a hand calculation. The design is in
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
