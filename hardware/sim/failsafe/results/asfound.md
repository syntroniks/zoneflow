<!-- Written by hardware/sim/failsafe/failsafe.py faults. Do not edit. -->

The two columns on the right are for the self-test sequence as merged in pull request #20: no step 2b, no step 5b, the old limits in step 9. `asfound.csv` has the columns for the sequence of review A2 as well.

| Fault | Valve current ends after (s): pin low / high / high-impedance | Layer lost | Found while the valve runs | Found at the next start |
|---|---|---|---|---|
| **reference** | | | | |
| no fault | 0.51 / 0.77 / 0.51 |  |  |  |
| **detector A** | | | | |
| R33 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R33 short | 0.01 / 0.01 / 0.01 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| R29 open | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R29 short | 0.51 / 0.77 / 0.51 |  |  |  |
| C22 open | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| C22 short | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on |  | 2 pin-high: gate-drive read-back high with the pin held high |
| D7 pin 1 A (GND) open | 0.35 / 0.53 / 0.35 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pin 2 K (KA_N2) open | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pin 3 common (KA_N1) open | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 1 A and 2 K bridged (GND to KA_N2) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 1 A and 3 common bridged (GND to KA_N1) | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 2 K and 3 common bridged (KA_N2 to KA_N1) | never on / never on / never on |  | 8 end: expander does not answer at the end of the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| C23 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 4 expander: valve pin Z1 does not read high as an input |
| C23 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R30 open | 0.51 / 0.77 / 0.51 |  | 9 stop: gate-drive read-back releases after 1.57 s, limits 0.3 to 1.0 s | 9 stop: gate-drive read-back releases after 1.57 s, limits 0.3 to 1.0 s |
| R30 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R91 open | 0.51 / 0.77 / 0.51 |  | 9 stop: gate-drive read-back releases after 1.57 s, limits 0.3 to 1.0 s | 9 stop: gate-drive read-back releases after 1.57 s, limits 0.3 to 1.0 s |
| R91 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R31 open | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R31 short | 0.46 / 0.69 / 0.46 |  |  |  |
| R32 open | 0.42 / 0.64 / 0.42 |  |  |  |
| R32 short | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| C24 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C24 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R34 open | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| R34 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R35 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R35 short | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| R36 open | 0.42 / 0.64 / 0.42 |  |  |  |
| R36 short | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| **logic** | | | | |
| U6 pin 1 1OUT (KA_OK) open | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| U6 pin 2 2OUT (OC_TRIP_N) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 3 VCC (V5) open | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U6 pin 4 2IN- (OC_CMP) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 5 2IN+ (VH) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 6 1IN- (KA_REF) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 7 1IN+ (KA_INP) open | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| U6 pin 8 3IN- (VL) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 9 3IN+ (OC_CMP) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 10 4IN- (VL) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 11 4IN+ (ZC_DIV) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 12 GND (GND) open *The model finds ground through the input diodes, about 0.9 V up, and keeps working. A real LM2901 without ground is not defined: its low level may not read as low at U7, and then detector A is stuck on. Detector B covers that.* | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 13 4OUT (AC_ZC) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pin 14 3OUT (OC_TRIP_N) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 1 1OUT and 2 2OUT bridged (KA_OK to OC_TRIP_N) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 2 2OUT and 3 VCC bridged (OC_TRIP_N to V5) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 3 VCC and 4 2IN- bridged (V5 to OC_CMP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 4 2IN- and 5 2IN+ bridged (OC_CMP to VH) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 5 2IN+ and 6 1IN- bridged (VH to KA_REF) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 6 1IN- and 7 1IN+ bridged (KA_REF to KA_INP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 8 3IN- and 9 3IN+ bridged (VL to OC_CMP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 9 3IN+ and 10 4IN- bridged (OC_CMP to VL) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 10 4IN- and 11 4IN+ bridged (VL to ZC_DIV) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 11 4IN+ and 12 GND bridged (ZC_DIV to GND) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 12 GND and 13 4OUT bridged (GND to AC_ZC) | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 pins 13 4OUT and 14 3OUT bridged (AC_ZC to OC_TRIP_N) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 4 expander: valve pin Z1 does not read high as an input |
| U6 output 1 (KA_OK) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 1 (KA_OK) never pulls low | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| U6 output 2 (OC_TRIP_N) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 2 never pulls low | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 output 3 (OC_TRIP_N) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 3 never pulls low | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 output 4 (AC_ZC) shorted low | 0.51 / 0.77 / 0.51 |  |  |  |
| U6 output 4 never pulls low | 0.51 / 0.77 / 0.51 |  |  |  |
| C19 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C19 short | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 1 1A (OC_TRIP_N) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 1 1A (OC_TRIP_N) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 1 1A (OC_TRIP_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 2 1B (FAULT_N) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 2 1B (FAULT_N) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 2 1B (FAULT_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 3 1Y (FAULT) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 4 2A (KA_OK) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 4 2A (KA_OK) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 4 2A (KA_OK) open, leaks low | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 5 2B (FAULT) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 5 2B (FAULT) open, leaks high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 5 2B (FAULT) open, leaks low | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 6 2Y (FAULT_N) open | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 7 GND (GND) open *As simulated. A real 74HC132 without ground is not defined.* | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| U7 pin 8 3Y (DRV_EN_N) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 9 3A (KA_OK) open, holds its last level | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 9 3A (KA_OK) open, leaks high | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 9 3A (KA_OK) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 10 3B (FAULT_N) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 10 3B (FAULT_N) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 10 3B (FAULT_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 11 4Y (DRV_EN) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 12 4A (DRV_EN_N) open, holds its last level | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 12 4A (DRV_EN_N) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 12 4A (DRV_EN_N) open, leaks low | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 13 4B (DRV_EN_N) open, holds its last level | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 13 4B (DRV_EN_N) open, leaks high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pin 13 4B (DRV_EN_N) open, leaks low | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 14 VCC (3V3) open *The model powers U7 through the clamp diodes of its inputs, from the pull-ups R36 and R53, at about 2.7 V, and it keeps working. A real 74HC132 on such a supply is a bench question.* | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high |
| U7 pins 1 1A and 2 1B bridged (OC_TRIP_N to FAULT_N) | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 pins 2 1B and 3 1Y bridged (FAULT_N to FAULT) *A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 3 1Y and 4 2A bridged (FAULT to KA_OK) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 4 2A and 5 2B bridged (KA_OK to FAULT) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 5 2B and 6 2Y bridged (FAULT to FAULT_N) *As pins 2 and 3: the same two nets.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 6 2Y and 7 GND bridged (FAULT_N to GND) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 8 3Y and 9 3A bridged (DRV_EN_N to KA_OK) *A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 9 3A and 10 3B bridged (KA_OK to FAULT_N) *The gate output and the comparator output fight over the node, and which one wins at power-up depends on the two parts. Either way detector A stays on once it has been enabled.* | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| U7 pins 10 3B and 11 4Y bridged (FAULT_N to DRV_EN) | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| U7 pins 11 4Y and 12 4A bridged (DRV_EN to DRV_EN_N) | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| U7 pins 13 4B and 14 VCC bridged (DRV_EN_N to 3V3) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 1 (FAULT) stuck high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 1 (FAULT) stuck low | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 output 2 (FAULT_N) stuck high | 0.51 / 0.77 / 0.51 |  |  |  |
| U7 output 2 (FAULT_N) stuck low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 3 (DRV_EN_N) stuck high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 3 (DRV_EN_N) stuck low | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 output 4 (DRV_EN) stuck high | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 output 4 (DRV_EN) stuck low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| C20 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C20 short | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| R53 open | 0.51 / 0.77 / 0.51 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R53 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R87 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R87 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R54 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R54 short | 0.51 / 0.77 / 0.51 |  |  |  |
| D8 open | 0.51 / 0.77 / 0.51 |  |  |  |
| D8 short | 0.51 / 0.77 / 0.51 |  |  |  |
| **LED supply switch** | | | | |
| Q3 pin G (DRV_EN_N) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  | 4 expander: valve pin Z1 does not read high as an input |
| Q3 pin G (DRV_EN_N) open, leaks high | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| Q3 pin G (DRV_EN_N) open, leaks low | 0.51 / 0.77 / 0.51 |  |  |  |
| Q3 pin S (3V3) open | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| Q3 pin D (V_DRV) open | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| Q3 pins G and S bridged (DRV_EN_N to 3V3) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| Q3 pins G and D bridged (DRV_EN_N to V_DRV) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| Q3 pins S and D bridged (3V3 to V_DRV) | 0.51 / 0.77 / 0.51 |  |  |  |
| R25 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R25 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R26 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R26 short | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| C21 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C21 short | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| **read-back** | | | | |
| R92 open | 0.51 / 0.77 / 0.51 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| R92 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R93 open *The read-back loses its pull-up and reads low for good: a false alarm on the safe side. Q2 itself works.* | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| R93 short *V_BUCK is then on the drain of Q2 with no coil in between, and Q2 shorts the supply as soon as it conducts.* | never on / never on / never on |  | 7 run: expander stops answering during the run | 5 weld check: valve pin does not read low when driven low |
| R94 open | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| R94 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R95 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R95 short | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| **expander** | | | | |
| U5 pin 3 RESET (DRV_EN) open, holds its last level | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 3 start: within 150 ms no expander acknowledge |
| U5 pin 3 RESET (DRV_EN) open, leaks high | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 1 idle: expander answers with no keep-alive |
| U5 pin 3 RESET (DRV_EN) open, leaks low | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| U5 pin 4 P00 (ZD_Z1_N) open | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| U5 pin 8 P04 (ZD_MV_N) open | 0.51 / 0.77 / 0.51 |  |  |  |
| U5 pin 9 P05 (RLY_ON) open | never on / never on / never on |  | 7 run: RLY_ON not high during the run | 6 relay: RLY_ON does not read high after it is set |
| U5 pin 12 GND (GND) open *As simulated. A real TCA9539 without ground is not defined.* | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| U5 pin 24 VCC (3V3) open | 0.51 / 0.77 / 0.51 |  | 8 end: expander does not answer at the end of the run | 3 start: within 150 ms no expander acknowledge |
| U5 pins 2 A1 and 3 RESET bridged (GND to DRV_EN) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U5 pins 3 RESET and 4 P00 bridged (DRV_EN to ZD_Z1_N) | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| U5 pins 8 P04 and 9 P05 bridged (ZD_MV_N to RLY_ON) | 0.51 / 0.77 / 0.51 |  |  | 4 expander: RLY_ON does not read low as an input |
| U5 other adjacent pins bridged: 1 INT-2 A1, 4 P00-5 P01, 5 P01-6 P02, 6 P02-7 P03, 7 P03-8 P04, 9 P05-10 P06, 10 P06-11 P07, 11 P07-12 GND, 13 P10-14 P11, 14 P11-15 P12, 15 P12-16 P13, 16 P13-17 P14, 17 P14-18 P15, 18 P15-19 P16, 19 P16-20 P17, 20 P17-21 A0, 21 A0-22 SCL, 22 SCL-23 SDA, 23 SDA-24 VCC | not simulated | | | Not simulated: these pins are valve bits of other channels, spare pins, the address straps and the I2C bus. Two valve pins bridged make two outputs follow one bit. A stuck bus stops the firmware from writing, and it must then stop the keep-alive. None of these pins can hold RESET high or drive a valve pin low while the device is in reset. |
| U5 RESET input ignored, bits kept | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 1 idle: expander answers with no keep-alive |
| U5 pin P00 (Z1) stuck low | 0.51 / 0.77 / 0.51 |  |  | 4 expander: valve pin Z1 does not read high as an input |
| U5 pin P00 (Z1) stuck high | 0.51 / 0.77 / 0.51 |  |  |  |
| U5 pin P04 (MV) stuck low | 0.51 / 0.77 / 0.51 |  |  | 4 expander: valve pin MV does not read high as an input |
| U5 pin P05 (RLY_ON) stuck high | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| U5 pin P05 (RLY_ON) stuck low | 0.50 / 0.77 / 0.50 |  |  |  |
| C18 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C18 short | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| RN1 element 1-8 (ZD_Z1_N) open | 0.51 / 0.77 / 0.51 |  |  |  |
| RN1 element 1-8 (ZD_Z1_N) short | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| RN1 elements 2 to 4 open or short, pins 1-2, 2-3, 3-4 bridged | not simulated | | | The other three elements pull the pins of Z2 to Z4 up in the same way. A bridge between two valve pins makes two outputs follow one bit; it adds no path that keeps a pin low after the expander is reset. |
| R96 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R96 short | 0.51 / 0.77 / 0.51 |  |  |  |
| **output LEDs (Z1 and MV)** | | | | |
| R101 open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| R101 short | 0.51 / 0.77 / 0.51 |  |  |  |
| U101 LED open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| U101 LED short | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| R102 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R102 short | 0.51 / 0.77 / 0.51 |  |  |  |
| D102 open | 0.51 / 0.77 / 0.51 |  |  |  |
| D102 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R901 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R901 short | 0.51 / 0.77 / 0.51 |  |  |  |
| U901 LED open | 0.51 / 0.77 / 0.51 |  |  |  |
| U901 LED short | 0.51 / 0.77 / 0.51 |  |  |  |
| R902 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R902 short | 0.51 / 0.77 / 0.51 |  |  |  |
| D902 open | 0.51 / 0.77 / 0.51 |  |  |  |
| D902 short | 0.51 / 0.77 / 0.51 |  |  |  |
| **relay drive** | | | | |
| K1 coil open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| K1 coil short *Q1 and Q2 then short V_BUCK whenever both conduct. The supply collapses, the supervisor resets the processor and the expander loses its bits. K1 cannot close with no voltage across its coil.* | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| K1 contact welded, or JP1 bridged | 0.68 / 1.02 / 0.68 | relay contact closed |  | 5 weld check: current flows with RLY_ON low |
| D5 open *Nothing in the model. On the board Q1 or Q2 would avalanche at every release, 0.16 mJ by the design document's estimate, and one of them may fail short in time. That fault has its own row.* | 0.51 / 0.77 / 0.51 |  |  |  |
| D5 short *The same as a shorted coil.* | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin G (RLY_G1) open, holds its last level | 0.51 / 0.77 / 0.51 |  |  | valve current: no valve current during the run |
| Q1 pin G (RLY_G1) open, leaks high | 0.51 / 0.77 / 0.51 |  |  | 5 weld check: current flows with RLY_ON low |
| Q1 pin G (RLY_G1) open, leaks low | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin S (RLY_B) open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin D (RLY_A) open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pins G and S bridged (RLY_G1 to RLY_B) | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pins G and D bridged (RLY_G1 to RLY_A) | 0.52 / 0.79 / 0.52 |  |  | 5 weld check: current flows with RLY_ON low |
| Q1 pins S and D bridged (RLY_B to RLY_A) *Q1 conducts whatever its gate does. K1 then follows Q2 alone.* | 0.51 / 0.77 / 0.51 |  |  | 5 weld check: current flows with RLY_ON low |
| R11 open | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: RLY_ON does not read low as an input |
| R11 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R12 open | 0.51 / 0.77 / 0.51 | Q1 stays on |  |  |
| R12 short | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q2 pin G (RLY_G2) open, holds its last level | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 3 start: within 150 ms no relay read-back low |
| Q2 pin G (RLY_G2) open, leaks high | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| Q2 pin G (RLY_G2) open, leaks low | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pin S (GND) open | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pin D (RLY_B) open | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pins G and S bridged (RLY_G2 to GND) | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pins G and D bridged (RLY_G2 to RLY_B) | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| Q2 pins S and D bridged (GND to RLY_B) | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| **detector B** | | | | |
| R89 open | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| R89 short | 0.54 / 0.81 / 0.54 |  |  |  |
| C51 open | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| C51 short | 0.56 / 1.02 / 0.56 | Q2 stays on |  | 2 pin-high: relay read-back not high with the pin held high |
| D22 pin 1 A (GND) open | 0.18 / 0.28 / 0.18 |  |  | 3 start: within 150 ms no relay read-back low |
| D22 pin 2 K (RLY_G2) open | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| D22 pin 3 common (KB_N1) open | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| D22 pins 1 A and 2 K bridged (GND to RLY_G2) | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| D22 pins 1 A and 3 common bridged (GND to KB_N1) | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| D22 pins 2 K and 3 common bridged (RLY_G2 to KB_N1) | 0.00 / 0.40 / 0.00 |  | 9 stop: relay read-back releases after 0.00 s, limits 0.08 to 1.0 s | 3 start: within 150 ms no relay read-back low |
| R14 open | 0.68 / 1.02 / 0.68 |  | 9 stop: relay read-back releases after 1.21 s, limits 0.08 to 1.0 s | 9 stop: relay read-back releases after 1.21 s, limits 0.08 to 1.0 s |
| R14 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| R90 open | 0.68 / 1.02 / 0.68 |  | 9 stop: relay read-back releases after 1.21 s, limits 0.08 to 1.0 s | 9 stop: relay read-back releases after 1.21 s, limits 0.08 to 1.0 s |
| R90 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| C50 open | never on / 0.27 / never on |  | 7 run: relay read-back high during the run | 7 run: relay read-back high during the run |
| C50 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| **thresholds and zero-cross (share U6)** | | | | |
| R48 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R48 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R49 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R49 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R50 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R50 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R51 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R51 short | 0.51 / 0.77 / 0.51 |  |  |  |
| C32 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C32 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R52 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R52 short | 0.51 / 0.77 / 0.51 |  |  |  |
| C33 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C33 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R55 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R55 short *Puts 24 VAC on 3V3 through the clamp diode D9. Every logic part is then outside its ratings and the row means nothing. A 1206 resistor rated 200 V does not fail short; a solder bridge across it would do this.* | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 1 idle: gate-drive read-back high with no keep-alive |
| R56 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R56 short | 0.51 / 0.77 / 0.51 |  |  |  |
| C34 open | 0.51 / 0.77 / 0.51 |  |  |  |
| C34 short | 0.51 / 0.77 / 0.51 |  |  |  |
| D9 pin 1 A (GND) open | 0.51 / 0.77 / 0.51 |  |  |  |
| D9 pin 2 K (3V3) open | 0.51 / 0.77 / 0.51 |  |  |  |
| D9 pin 3 common (ZC_CLAMP) open | 0.51 / 0.77 / 0.51 |  |  |  |
| D9 pins 1 A and 2 K bridged (GND to 3V3) | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| D9 pins 1 A and 3 common bridged (GND to ZC_CLAMP) | 0.51 / 0.77 / 0.51 |  |  |  |
| D9 pins 2 K and 3 common bridged (3V3 to ZC_CLAMP) | 0.51 / 0.77 / 0.51 |  |  |  |
| R88 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R88 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R57 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R57 short | 0.51 / 0.77 / 0.51 |  |  |  |
| R58 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R58 short | 0.51 / 0.77 / 0.51 |  |  |  |
| **output devices** | | | | |
| triac, TVS, opto-triac output or snubber capacitor shorted | 0.51 / 0.77 / 0.51 |  |  | 6 relay: current flows with every valve bit high |
| **module** | | | | |
| U4 pin 31 (KA) not connected to the board | 0.01 / 0.01 / 0.01 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| GPIO of the gate-drive read-back driven high as an output by mistake | 0.51 / 0.77 / 0.51 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| GPIO of the gate-drive read-back driven low as an output by mistake | 0.51 / 0.77 / 0.51 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| U4 pin 35 (DRV_EN_RB) not connected to the board | 0.51 / 0.77 / 0.51 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| GPIO of the relay read-back driven high as an output by mistake | 0.51 / 0.77 / 0.51 |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| GPIO of the relay read-back driven low as an output by mistake | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| U4 pin 7 (RLY_B_RB) not connected to the board | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| GPIO of the over-current flag driven high as an output by mistake | 0.51 / 0.77 / 0.51 |  |  |  |
| GPIO of the over-current flag driven low as an output by mistake | 0.51 / 0.77 / 0.51 |  | 7 run: over-current flag low during the run | 7 run: over-current flag low during the run |
| U4 pin 33 (OC_FAULT_N) not connected to the board | 0.51 / 0.77 / 0.51 |  |  |  |
| KA bridged to module pin 30 IO37 (octal PSRAM line, toggling), 0.01 ohm | **stays on** / **stays on** / **stays on** | gate drive stays, relay stays closed | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| KA bridged to module pin 30 IO37 (octal PSRAM line, toggling), 1000 ohm | 0.49 / 0.75 / **stays on** | gate drive stays, relay stays closed |  |  |
| KA bridged to module pin 30 IO37 (octal PSRAM line, toggling), 4700 ohm | 0.50 / 0.77 / **stays on** | gate drive stays, relay stays closed |  |  |
| KA bridged to module pin 30 IO37 (octal PSRAM line, toggling), 10000 ohm | 0.51 / 0.77 / 0.68 | Q2 stays on |  |  |
| KA bridged to module pin 30 IO37 (octal PSRAM line, toggling), 22000 ohm | 0.51 / 0.77 / 0.51 |  |  |  |
| KA bridged to module pin 32 IO39 (BTN_HOME, contact open) | 0.51 / 0.77 / 0.52 |  |  |  |
| KA bridged to module pin 32 IO39 (BTN_HOME, contact worked at 20 Hz) | 0.51 / 0.77 / 0.52 |  |  |  |
| **board** | | | | |
| KA bridged to AC_ZC anywhere on the board | 0.48 / 0.75 / 0.78 | gate drive stays, Q1 stays on, Q2 stays on |  |  |
| KA bridged to TFT_BL (backlight PWM, keeps running), 0.01 ohm | **stays on** / **stays on** / **stays on** | gate drive stays, relay stays closed | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| KA bridged to TFT_BL (backlight PWM, keeps running), 1000 ohm | 0.49 / 0.75 / **stays on** | gate drive stays, relay stays closed |  |  |
| KA bridged to U0TXD (ROM boot log, prints in a reset loop) | **stays on** / **stays on** / **stays on** | gate drive stays, relay stays closed | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net AC_ZC shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net AC_ZC shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net DRV_EN shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net DRV_EN shorted to 3V3 | 0.51 / 0.77 / 0.51 | Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net DRV_EN_N shorted to GND | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net DRV_EN_N shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net DRV_EN_RB shorted to GND | 0.51 / 0.77 / 0.51 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| net DRV_EN_RB shorted to 3V3 | 0.51 / 0.77 / 0.51 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net FAULT shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net FAULT shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net FAULT_N shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net FAULT_N shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net ISENSE_LO shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net ISENSE_LO shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA shorted to GND | 0.01 / 0.01 / 0.01 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| net KA shorted to 3V3 | 0.27 / 0.27 / 0.27 |  |  | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| net KA_INP shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_INP shorted to 3V3 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: expander answers with no keep-alive |
| net KA_N0 shorted to GND | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N0 shorted to 3V3 | 0.44 / 0.52 / 0.52 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N1 shorted to GND | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, limits 0.3 to 1.0 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N1 shorted to 3V3 | 0.49 / 0.75 / 0.49 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_N2 shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N2 shorted to 3V3 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_OK shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_OK shorted to 3V3 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_REF shorted to GND | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_REF shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KB_N0 shorted to GND | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| net KB_N0 shorted to 3V3 | 0.27 / 0.27 / 0.27 |  |  | 3 start: within 150 ms no relay read-back low |
| net KB_N1 shorted to GND | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no relay read-back low |
| net KB_N1 shorted to 3V3 | 0.66 / 1.00 / 0.66 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net LEDA_MV shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net LEDA_MV shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net LEDA_Z1 shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net LEDA_Z1 shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net LED_FLT shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net LED_FLT shorted to 3V3 | 0.50 / 0.77 / 0.50 |  |  |  |
| net OC_CMP shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_CMP shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_FAULT_N shorted to GND | 0.51 / 0.77 / 0.51 |  | 7 run: over-current flag low during the run | 7 run: over-current flag low during the run |
| net OC_FAULT_N shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net OC_TRIP_N shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_TRIP_N shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net OLA_MV shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net OLA_MV shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net OLA_Z1 shorted to GND | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| net OLA_Z1 shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net RLY_A shorted to GND | 0.68 / 1.02 / 0.68 | relay stays closed | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_A shorted to 3V3 *The coil then sees V_BUCK less 3V3, about 2 V, whatever Q1 and Q2 do. That cannot pull the relay in and may hold it: Omron gives 40 % of the rated voltage as a holding figure. With Q1 and Q2 on, the bridge also shorts 3V3 to ground, and the supervisor resets the processor.* | 0.15 / 0.15 / 0.15 | relay stays closed | 9 stop: gate-drive read-back releases after 0.19 s, limits 0.3 to 1.0 s | valve current: no valve current during the run |
| net RLY_B shorted to GND | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_B shorted to 3V3 *With Q2 on this shorts 3V3 to ground, and the supervisor resets the processor.* | never on / never on / never on |  | valve current: no valve current during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_B_RB shorted to GND | 0.51 / 0.77 / 0.51 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_B_RB shorted to 3V3 | 0.51 / 0.77 / 0.51 |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_G1 shorted to GND | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| net RLY_G1 shorted to 3V3 | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| net RLY_G2 shorted to GND | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_G2 shorted to 3V3 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_ON shorted to GND | never on / never on / never on |  | 7 run: RLY_ON not high during the run | 6 relay: RLY_ON does not read high after it is set |
| net RLY_ON shorted to 3V3 | 0.51 / 0.77 / 0.51 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| net VB shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VB shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VH shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VH shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net VL shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net VL shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net V_DRV shorted to GND | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: valve pin Z1 does not read high as an input |
| net V_DRV shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZC_CLAMP shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZC_CLAMP shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZC_DIV shorted to GND | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZC_DIV shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZD_MV_N shorted to GND | 0.51 / 0.77 / 0.51 |  | 7 run: valve pin MV not high during the run | 4 expander: valve pin MV does not read high as an input |
| net ZD_MV_N shorted to 3V3 | 0.51 / 0.77 / 0.51 |  |  |  |
| net ZD_Z1_N shorted to GND | 0.51 / 0.77 / 0.51 |  |  | 4 expander: valve pin Z1 does not read high as an input |
| net ZD_Z1_N shorted to 3V3 | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| leak of 47 kohm from 3V3 to KA_N2 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 2 pin-high: gate-drive read-back high with the pin held high |
| leak of 100 kohm from 3V3 to KA_N2 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| leak of 220 kohm from 3V3 to KA_N2 | 0.51 / 0.77 / 0.51 |  | 9 stop: gate-drive read-back releases after 1.19 s, limits 0.3 to 1.0 s | 9 stop: gate-drive read-back releases after 1.19 s, limits 0.3 to 1.0 s |
| leak of 100 kohm from 3V3 to KA_INP | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| leak of 330 kohm from 3V3 to KA_INP | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 2 pin-high: gate-drive read-back high with the pin held high |
| leak of 47 kohm from 3V3 to RLY_G2 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| leak of 100 kohm from 3V3 to RLY_G2 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 2 pin-high: relay read-back not high with the pin held high |
| leak of 330 kohm from 3V3 to RLY_G2 | 0.62 / 0.95 / 0.62 |  |  |  |
| leak of 100 kohm from V_BUCK to RLY_G2 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| leak of 330 kohm from V_BUCK to RLY_G2 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back releases after 1.14 s, limits 0.08 to 1.0 s | 9 stop: relay read-back releases after 1.14 s, limits 0.08 to 1.0 s |
| leak of 1000 kohm from V_BUCK to RLY_G2 | 0.58 / 0.88 / 0.58 |  |  |  |
| leak of 100 kohm from 3V3 to KB_N1 | 0.68 / 1.02 / 0.68 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 2 pin-high: relay read-back not high with the pin held high |
| leak of 100 kohm from 3V3 to KA_N1 | 0.51 / 0.77 / 0.51 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| **supplies** | | | | |
| U3 shorted input to output: 3V3 at V5, 4.95 V. The processor is taken to keep running until the stop *Both detectors let go at 4.95 V, if the keep-alive stops. The processor is outside its ratings and whether it stops is not known: finding M5.* | 0.69 / 1.04 / 0.69 |  | 9 stop: relay read-back does not release within 3 s | 9 stop: relay read-back does not release within 3 s |
| 3V3 falls to 2.0 V: the supervisor holds the processor in reset | 0.01 / 0.01 / 0.01 |  |  | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| 3V3 falls to 0 V | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| V_BUCK falls to 3.0 V, V5 and 3V3 with it | 0.02 / 0.02 / 0.02 |  |  | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| V_BUCK falls to 0 V on 24 VAC alone: every rail goes | never on / never on / never on |  | 7 run: expander stops answering during the run | 1 idle: gate-drive read-back high with no keep-alive |
| V_BUCK falls to 0 V with USB attached: the logic stays up (F5 has opened, section 4.8) | never on / never on / never on |  | valve current: no valve current during the run | 1 idle: relay read-back not high with no keep-alive |
| V_BUCK at 10.3 V, V5 at 10 V: the highest the clamp D25 allows | 0.51 / 0.77 / 0.51 |  |  |  |
