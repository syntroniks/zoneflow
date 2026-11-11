# ZoneFlow hardware answers A2

Status: **answers of the hardware design authority to the firmware's
questions on section 19 of the [hardware design](hardware-design.md),
2026-11-11**. The questions came from the firmware work that followed
[hardware review A2](hardware-review-a2.md): pull request #37, section
"Questions for hardware", tracked in
[issue #38](https://github.com/syntroniks/zoneflow/issues/38). This document
also settles finding M5 of the review, which the review left to the owner.

No board exists and I had no bench. Every answer rests on a data sheet that
I opened, a calculation from its figures, or an ngspice run committed under
`hardware/sim/`. Section 8 says which checks I ran. Where only a bench can
answer, the answer says so and names the item in the risk register.

The decisions are written into the hardware design itself, mostly section
19. This document is the record of why. Where the two differ, the hardware
design is right.

## 1. The answers in short

| No. | Question | Decision | Evidence | Written in |
|---|---|---|---|---|
| 1 | Rule 15 cannot be told from step 1 | No new reading and no separate check. A missing `V_BUCK` fails step 1, and the report names both causes. A person separates them by unplugging USB. | The read-back table of design section 7.4: both states put 0 V on GPIO7. No pin is free. | 19.1 rule 15 |
| 2 | The normal range of `VIN_MON` | 0.60 to 1.20 V as the mean of a 100 ms window. Firmware reads it, and step 0 requires 0.60 to 1.50 V. | `hardware/sim/vin_mon.cir`, 22 supply corners | 19.10 |
| 3 | Commissioning "at room temperature" | Automatic, as implemented. Not a button. Only while the die temperature reads 25 to 50 °C. | `failsafe/commission.py`: the stored tB moves by -13 to +14 % between 10 and 40 °C | 19.7 |
| 4 | Learning limits, and tB against tA | tA 0.33 to 0.85 s and tB 0.12 to 1.25 s. No ratio. The limits refuse every board with an open bleed resistor on detector A and about a quarter of those with one on detector B. The rest need an ohmmeter. | `failsafe/commission.py`, 5544 runs | 19.7, 21 step 1, risk 34 |
| 5 | "Run steps 3 and 9 five times" | The cycle is steps 0, 1, 3, 4a and 9, as implemented. | Step 0 needs 2.5 s of low keep-alive before every start | 19.7 |
| 6 | 2 s or 2.5 s after a polling cycle | 2.5 s. It is the wait of step 0, and no second number is needed. | Review A2: 2.19 s at the slow corner | 19.6 |
| 7 | What "in use" means in the float test | Backlight at 50 %, display redrawn without pause, PSRAM read without pause, and 0x55 sent on UART0 without pause. | `failsafe/bridges.py` and `failsafe/floatload.py` | 19.7, note to step 2b |
| 8 | Step 2b when step 2 is daily | Step 2b before every run and at start-up, always. Step 2 may be daily. | The fault table: what step 2 finds takes one layer, what step 2b finds takes both | 19.7, note to step 2 |
| 9 | Steps 5 and 5b without current readings | Allowed, if firmware reports the condition for as long as it lasts. | The fault table: each fault those steps find leaves the other layer intact | 19.1 rule 19 |
| M5 | A shorted 3.3 V regulator | The valves were not guaranteed off. A detector, U13, now removes drive by hardware above 4.0 V. The processor is not protected, which the owner accepted. | `failsafe/ldo.py`, 945 runs | 9.5 |

Three things came up that nobody had asked about. They are in section 5:
step 2 failed healthy boards at one corner, the band of step 9 was too
narrow, and a brown-out looked like a relay fault.

## 2. The nine questions

### 2.1 Rule 15 and step 1

**Question.** With 24 VAC present, GPIO7 reads 0 V at rest both when
`V_BUCK` is missing and when Q2 is on. Step 0 put rule 15 before step 1,
which would report every Q2 fault as a regulator fault. Is a reading that
separates the two wanted?

**Decision.** No. Rule 15 as it stood asked for something the board cannot
give. The firmware's choice, to report the idle test's relay read-back with
both causes named, is the rule now.

**Why.** Every reading firmware has is the same in both cases: GPIO7 is at
0 V as a level and as a voltage, `AC_ZC` has edges, `VIN_MON` is normal,
because `VIN_DC` is in front of the fuse F5 that a failed regulator opens.
A reading of `V_BUCK` needs a pin, and section 12.2 of the design has none.
The two cases do not need different handling by firmware. Both refuse runs
and both need a repair. They differ in what a person does next, and a
person has a test that firmware lacks: unplug USB. With a failed regulator
the controller is dark on 24 VAC alone. With Q2 or its detector at fault it
keeps running.

`VIN_MON` does separate one case, and that is why firmware should read it:
mains present on `AC_ZC` and no rectified supply means D2, R1 or C1. Design
section 4.8 says R1 may not survive a slow regulator fault, so this case is
real.

### 2.2 The normal range of `VIN_MON`

**Decision.** 0.60 to 1.20 V, as the mean of a 100 ms window on GPIO3 at
6 dB. The bands and what firmware does in each are in design section 19.10.

**How.** `VIN_MON` is `VIN_DC` through R9 and R10, 300 kohm and 10 kohm.
`hardware/sim/vin_mon.cir` is the input stage of `supply.cir` with the
divider added. It runs 21.6, 24 and 26.4 VAC with transformers of 20, 40
and 75 VA and loads of 0.6, 1.75 and 5 W, and 32 VAC with no load.

| Corner | `VIN_MON`, mean |
|---|---|
| Lowest of the specified range: 21.6 VAC, 20 VA, 5 W | 0.63 V |
| Typical: 24 VAC, 40 VA, 1.75 W | 0.96 V |
| Highest of the specified range: 26.4 VAC, 75 VA, 0.6 W | 1.14 V |
| 32 VAC, no load | 1.43 V |

Two 1 % resistors add 1.9 % and the ADC 10 mV, which gives 0.61 to 1.17 V
for the specified range. Inside one window the reading swings by up to
0.24 V with the half-wave ripple, so single samples are no use.

**It became a precondition.** While deriving the range I found that step 1
misreads a brown-out. With `VIN_DC` under the 9 V at which U1 starts, mains
still makes edges on `AC_ZC`, `V_BUCK` is off and GPIO7 reads low. On USB
power the firmware would report a relay fault that needs a repair. Step 0
now requires `VIN_MON` at 0.60 V or more.

### 2.3 Commissioning and temperature

**Question.** Firmware cannot measure the board's temperature. Should
commissioning be a deliberate action of a person?

**Decision.** No. It stays automatic. It runs only while the die
temperature sensor of the ESP32-S3 reads 25 to 50 °C, and it tries again
every 10 minutes until it has a value.

**Why.** A person at a button knows nothing about the board that the
firmware does not, except the room temperature. The simulation says how
much that matters. Between 10 and 40 °C the stored tB moves by -13 to
+14 % and tA by -5 to +2 %. The band of step 9 has room for that
(section 2.4). Outside that range it does not: a board that stored its
value cold fails healthy stop tests when hot.

The die sensor is a coarse tool. The data sheet says it senses the chip,
which is warmer than its surroundings by an amount it does not give
(ESP32-S3 datasheet v2.2, section 4.2.2.2). The ESP-IDF v5.0 page on the
sensor gives the reading an error under 1 °C between -10 and 80 °C, and the
v5.5 page says it is not meant for ambient temperature. I take the die to
be 10 to 15 K
above the board, so that 25 to 50 °C on the die is 10 to 40 °C on the
board. That offset is an estimate. Risk 30 measures it.

### 2.4 The learning limits, and tB against tA

**Question.** The limits were derived from the review's tables, not
simulated. A board whose tB is under about 0.55 s at 27 °C can still learn
an open R14 or R90 as its normal. Would a check of tB against tA help, and
with what ratio?

**Decision.** Limits on each time: tA 0.33 to 0.85 s and tB 0.12 to 1.25 s.
No ratio. The firmware engineer's suspicion is right and no reading that
firmware can take removes it. A resistance reading on the new board does.

**The simulation.** `hardware/sim/failsafe/commission.py` draws 200 boards
inside the tolerance ranges of review A2 and adds 64 boards at the extremes,
with the two reservoir capacitors also at opposite ends. Each board runs
healthy, with R30 open and with R14 open, at seven temperatures. A run is a
commissioning cycle: 1 s of the firmware's toggling, a stop, and both
read-backs timed. 5544 runs, none failed.

What a commissioning cycle sees with the board at 10 to 40 °C:

| | Healthy | R30 open | R14 open |
|---|---|---|---|
| tA, random boards | 0.38 to 0.82 s | 0.89 to 1.99 s | as healthy |
| tA, extremes | 0.37 to 0.83 s | 0.79 to 2.01 s | as healthy |
| tB, random boards | 0.21 to 0.93 s | as healthy | 0.45 to 1.86 s |
| tB, extremes | 0.14 to 1.23 s | as healthy | 0.32 to 2.51 s |
| tB / tA, random boards | 0.32 to 1.98 | 0.14 to 0.91 | 0.71 to 4.04 |
| tB / tA, extremes | 0.18 to 3.10 | 0.07 to 1.36 | 0.42 to 6.33 |

- **tA separates.** Detector A ends at a comparator whose thresholds are ratios of the rail. Its time is the bleed resistance times a capacitance and little else. Healthy and faulty overlap only where C23 is at the very bottom of its allowance.
- **tB does not.** Detector B ends at the bare gate of Q2. Its threshold, 0.65 to 1.45 V, spreads the healthy tB over a factor of four to nine. A doubled tB is inside that spread for most boards.
- **The ratio does not either.** It removes the capacitance from the comparison, if the two capacitors are alike, and keeps the threshold of Q2, which is the larger unknown.

How many commissioning cycles each rule accepts:

| Rule | Healthy | R30 open | R14 open |
|---|---|---|---|
| tA and tB inside their limits, random boards | 600 of 600 | 0 of 600 | 434 of 600 |
| The same, extremes | 192 of 192 | 16 of 192 | 144 of 192 |
| With tB / tA between 0.3 and 2.0 as well, random boards | 600 of 600 | 0 of 600 | 359 of 600 |
| The same, extremes | 150 of 192 | 14 of 192 | 101 of 192 |
| With tB / tA between 0.35 and 1.6, random boards | 585 of 600 | 0 of 600 | 262 of 600 |

The widest ratio that turns away no healthy random board removes 75 of 434
faulty ones, and it turns away 42 of the 192 healthy extremes. A tighter one
starts to refuse healthy random boards. If C23 and C50 are taken to be
within 10 % of each other, the wide ratio removes 18 of 119. That is not
worth a rule that can leave a healthy board uncommissioned for good.

**Other readings I tried.** Stopping the pin high instead of low multiplies
both times by 1.3 to 1.9 with or without the fault. The time to switch on
is the same with or without it: 11 to 32 ms for A, 2 to 42 ms for B. The
reason is general. Every time firmware can measure is the bleed resistance
times a capacitance. The capacitors may be anywhere from 0.60 to 1.15 of
their value, a factor of 1.9, and the fault is a factor of 2.

**What a board that commissions with the fault does.** Its release times
stay inside the absolute limits at every temperature: at most 0.95 s for tA
and 1.54 s for tB in the simulation. So it is no slower than the slowest
healthy board the design accepts. It has lost its spare bleed resistor. If
the second one opens, that detector never lets go, the stop test and the
idle test report it, and the other layer covers it.

**The bench item.** An ohmmeter from TP8 to ground and from the gate of Q2
to ground reads about 50 kohm on a good board and about 90 kohm with one
bleed resistor open. It is step 1 of the bring-up plan and risk 34, for
every board that is built.

**The band of step 9**, from the same runs, is 0.40 to 1.70 times the
stored value. With the 0.5 to 1.5 of review A2, 8 healthy stop tests fell
outside, all at 70 °C on boards that had stored their value at 10 °C. With
the new band none does. A bleed resistor that opens later is found in every
stop test with the board between 10 and 40 °C, apart from 2 of 64 extreme
boards that stored their value at 40 °C and are found at 25 °C. At 55 and
70 °C, and for detector B at -20 and 0 °C, it can hide until the board is
back at a moderate temperature. No board hides it at every temperature.

**Limits of the model.** Its capacitors do not change with temperature, and
an X5R part may move by 15 %. Its transistor conducts further below its
threshold than a real one. The ranges are review A2's allowances, not
measurements. Risk 30.

### 2.5 The commissioning cycle

**Decision.** Steps 0, 1, 3, 4a and 9, with at least 1 s of toggling before
step 9 and no register written. That is what the firmware does. "Run steps
3 and 9 five times" could not be done as written, because every start needs
the 2.5 s of step 0. Design section 19.7 now has the procedure in seven
numbered items, with the limits of section 2.4.

### 2.6 Two seconds or 2.5 seconds

**Decision.** 2.5 s. Section 19.6 no longer has a number of its own. The
polling cycle and the run that follows it both start with step 0, and step
0 waits until the keep-alive has been low for 2.5 s. The 2 s was an
oversight: it predates the 2.19 s that review A2 found for the relay
detector at its slow corner with the pin left high.

### 2.7 "In use" in the float test

**Question.** Section 19 gave no backlight duty, refresh rate or PSRAM
traffic. Does the firmware's choice make the nets toggle the way the
simulation assumed?

**What the simulation assumed.** `hardware/sim/failsafe/bridges.py` bridges
the keep-alive net to a push-pull source of 3.3 V behind 30 ohm that
toggles at 500 Hz with equal high and low times for the whole test. The
test finds the bridge in two ways. Under about 700 ohm the pin itself reads
high. Above that the pin sits between the logic levels, and the bridge
shows because the net has pumped the detectors during the 0.5 s, so that a
read-back has moved by the end.

**What I added.** `hardware/sim/failsafe/floatload.py` varies the share of
time the net is high, and its rate: 288 runs.

| The bridged net | Holds the valve on | The float test reports it |
|---|---|---|
| High half of the time, 500 Hz or 2 kHz | Up to 2.2 kohm in 4 of 4 corners, at 3.3 kohm in 1 | Up to 2.2 kohm in 4 of 4, at 3.3 and 4.7 kohm in 3 |
| High for 10 % or 90 % of the time | Up to 1 kohm in 4 of 4, at 2.2 kohm in 1 | Up to 1 kohm in 4 of 4, at 2.2 and 3.3 kohm in 3 |
| Steadily high | Never | Up to 470 ohm in 4 of 4, at 1 kohm in 1 |

The rate does not matter. The share of time does: a net at 50 % pumps
hardest and is found furthest, and no bridge that holds the valve at any
share goes unreported by a test with the net at 50 %. A net that is only
high during the test is found under 1 kohm, though the same net toggling
would hold the valve up to 2.2 kohm.

**What firmware must therefore do** is in the note to step 2b in design
section 19.7. Every net that firmware can make toggle must toggle during
the test, from 100 ms before the pin floats: backlight PWM at 50 %, the
display redrawn without pause, a PSRAM buffer larger than the data cache
read and written without pause. The first implementation does these three.

**Two changes for the firmware.** The first: `U0TXD` is idle during the
test, which makes it the steadily high net of the table. Send 0x55 on UART0
without pause during the test. A terminal on J8 then shows a line of the
letter U at every test.

The second: a backlight at 5.0 kHz and a pin read
every 1.0 ms are locked at five periods to a read. Every read can fall on
the low part of the period, and then a bridge of 0 ohm to `TFT_BL` never
reads high. The read-backs still find it. The backlight should run at
4.7 kHz or another frequency that is not a whole multiple of 1 kHz during
the test.

**What no test can cover.** The flow input, the current signals and the USB
lines toggle only when water flows, a valve draws current or a host talks.
The layout rules of design section 8.5 are the only protection there.

**Bench.** Whether real nets pump like the model's is risk 27, which now
names the bridges and duties to try.

### 2.8 Step 2b before every run

**Decision.** Yes, regardless of what is chosen for step 2.

**From the fault table.** Step 2 is the only step that finds a shorted C22
or C51, and it also finds leaks onto one reservoir. Each of those takes one
layer and leaves the other, which turns the valve off until the next test
finds the fault. Once a day is defensible for it.

Step 2b is the only step that finds a bridge of a kilohm or so onto the
keep-alive net. That fault takes both layers at once, in one state: a
processor in reset while a valve is on. Nothing covers it. The test belongs
directly before the valve opens. Between runs no valve is on and the
expander is in reset, so the test need not run then.

Review A2 said this in its section 4.6. The table of section 19.7 said
"with step 2", and that was the contradiction the firmware engineer found.

### 2.9 Steps 5 and 5b without current readings

**Decision.** Acceptable, with a condition: firmware reports the missing
measurement as a fault of its own for as long as it lasts. Skipping the
steps without saying so is not acceptable.

**Why it may run.** Without the current measurement four faults go unfound:
a welded K1 or shorted Q1 (step 5), an open R12 (step 5b) and a shorted
output device (step 6 and rule 6). In the fault table each of them leaves
the other layer working. A valve stays on only if a second hardware fault
is added. Against that stands a certain harm: a controller that stops
watering because its ADC failed.

**What it costs.** The statement "every fault that sticks a layer on is
found at the next start" holds only while the current measurement works.
The hardware over-current latch does not depend on the ADC.

Design section 19.10 also says when the measurement counts as lost: no
window arrives, or the rest level of one channel is outside its limits.

## 3. The smaller questions

From the same list in pull request #37.

- **10, stale text.** Corrected: section 7.4 (GPIO42 reads `V_DRV`), section 8.6 (0.15 s), section 19.3 row 3 (2.2 kohm).
- **11, GPIO7 as ADC input and digital input behind 100 kohm.** The source the ADC sees is R94 in parallel with R95, 60 kohm. The data sheet gives no input charge or allowed source resistance, so I cannot calculate the error. The levels to tell apart are far from each other: 0 V, 0.3 to 1.5 V and 3.05 V. If the ADC does pull the pin, 10 nF at the pin cures it and needs a footprint. Risk 33 is the bench item. No part is added on a guess.
- **12, the float test fails at the first high read and lasts 0.5 to 0.55 s.** Both are right. Section 19.7 says so.

From the earlier list in firmware architecture section 20, which review A2
left unanswered.

- **4, relay linger.** A relay kept closed for 2 s after the last valve is within rule 4, which now says "at least".
- **6, bias check.** The rest level of each channel has limits now, and it doubles as a reading of `3V3`: design section 19.10.
- **8, zero-cross offsets.** No correction from `VIN_MON`. They move by 0.05 ms from 24 to 32 VAC.
- **9, DC component.** 30 % of the rms current and at least 20 mA is right.

## 4. Finding M5: a shorted 3.3 V regulator

**The owner's decision.** Accept for revision A that a shorted U3 puts
about 5 V on the processor, as a documented risk, provided the valves are
guaranteed off in that fault. Show by analysis and simulation whether they
are, for a dead and for an erratic processor. If not, add the smallest fix.

**Analysis.** Every part of the fail-safe chain is inside its ratings at
5.1 V: the expander to 5.5 V, U7 and U8 to 6 V, the comparator on its own
36 V rating. Detector A compares ratios of the rail and behaves as at
3.3 V. Detector B uses no rail. The table of ratings is in design section
9.5. So the chain does what the keep-alive pin tells it. The question is
only what the pin does.

**Simulation.** `hardware/sim/failsafe/ldo.py`, 945 runs, none failed.

| The processor after the fault | The board as review A2 left it |
|---|---|
| Dead: pin low, high or high-impedance for good | Valve off in 48 of 48 corners, within 0.84 s, or 1.25 s if the pin stops high |
| Erratic: the pin keeps toggling at 25 Hz to 1 kHz | Valve stays on, in every corner |
| Dead, with the pin joined to its supply and the rail swinging between 2.0 V and `V5` at 50 Hz or more | Valve stays on, in every corner |
| No edges at all, from power-up | Nothing enables |

**Verdict.** Not guaranteed. A dead processor is safe. A processor that
keeps moving the pin is not, and nothing on the board could tell it from
working firmware. I cannot say how likely either is at 140 % of the
absolute maximum, and that is the point: the fail-safe claim must not
depend on it.

**The fix.** U13, a TPS3701 voltage detector, with a divider of two
resistors, a filter capacitor and a decoupling capacitor. Its open-drain
output pulls the set input of the over-current latch low while `3V3` is
above 4.04 V (3.91 to 4.17 V). U7 then holds `DRV_EN` low whatever
the keep-alive does. With U13 the valve current ends 0.5 ms after the fault
in every case of the table. The calculation, the reason for this node
instead of `EN`, the single faults of the new parts and what the fix does
not do are in design section 9.5.

**Why this is the smallest.** It needs a reference, because the window
between the highest normal rail, 3.40 V, and the lowest faulty one,
4.66 V, is too narrow for a zener and a transistor: a 3.6 V zener is soft
by a volt at microamps, and a MOSFET threshold spreads by another volt.
The four comparators of U6 and the four gates of U7 are all in use. One
small IC and two resistors is what remains. Both resistor values and both
capacitor values are already on the BOM.

**What stays a risk.** The processor and the display are not protected.
U13 cannot be tested in service. Risk 32.

## 5. Found while answering

- **Step 2 failed healthy boards.** I ran the whole sequence on a board with no fault at the 96 tolerance corners of the review, which the review had not done: `hardware/sim/failsafe/sequence.py`. The pin-high test failed at two corners. Its one rising edge turns on a Q2 with a low threshold for up to 1.33 s at 70 °C, and the step read after 1.0 s. A user would have been told on a hot day that the board needs repair. The hold is 2.0 s. All 96 corners pass every step.
- **The band of step 9 was too narrow.** Section 2.4.
- **A brown-out looked like a relay fault.** Section 2.2.
- **The backlight and the pin reads were locked.** Section 2.7.

## 6. The circuit change, for the layout

The board is being laid out on branch `hw/rev-a-layout`. This is everything
that changed in `hardware/netlist.yaml`.

| | |
|---|---|
| New parts | U13 TPS3701DDCR, LCSC C132788, footprint `Package_TO_SOT_SMD:TSOT-23-6`. R13 1 Mohm, R100 110 kohm, C55 10 nF, C56 100 nF, all 0603 |
| New net | `OV_DIV`: U13 pin 4, R13 pin 2, R100 pin 1, C55 pin 1 |
| `OC_TRIP_N` | gains U13 pin 6 |
| `3V3` | gains U13 pin 5, R13 pin 1, C56 pin 1 |
| `GND` | gains U13 pins 2 and 3, R100 pin 2, C55 pin 2, C56 pin 2 |
| No connection | U13 pin 1 |
| Unchanged | Every other part, value, footprint and net |

Placement: R13, R100 and C55 at pin 4 of U13, with `OV_DIV` short and away
from `SW`, `KA` and the display lines. C56 at pin 5. U13 near U7. The
keep-alive rules of design section 8.5 are untouched. `board.toml` needs
positions for the five parts.

The footprint was compared with TI's drawing: `design/footprints.md`,
section 3.

## 7. What the firmware must change

The firmware on `main` implements section 19 as review A2 left it. These
are the differences, in the order of the section.
[Issue #41](https://github.com/syntroniks/zoneflow/issues/41) tracks them. Names in brackets are the
constants and checks of `zf/hw/output_stage.hpp` as they are today.

**Changed values.**

1. **Step 2, hold.** 2.0 s, was 1.0 s (`kPinHighHoldMs`). The simulated board must keep the relay read-back low for up to 1.33 s after a single edge at the hot corner, or the test of this change proves nothing.
2. **Step 9, band.** 0.40 to 1.70 times the stored value, was 0.5 to 1.5 (`kBandLowPercent`, `kBandHighPercent`).
3. **Learning limits.** tA 0.33 to 0.85 s, was 0.34 to 0.9 s. tB 0.12 to 1.25 s, was 0.16 to 1.1 s (`kLearnDriveMinMs` and the three beside it). The agreement of the five samples within 1.25 stays. A stored value outside the new limits is ignored when read, as now.

**Changed behaviour.**

4. **Step 2b is no longer tied to step 2.** It runs at start-up and before every run even where step 2 is daily (`pinHighTestEveryRun` false, ESPHome `self_test_before_every_run: false`).
5. **Float test load.** Send the byte 0x55 on UART0 without pause during the test, at the console baud rate. Run the backlight during the test at a frequency that is not a whole multiple of 1 kHz, 4.7 kHz for example, or read the pin at uneven intervals. Keep 50 % duty, the continuous redraw and the PSRAM task. The ESPHome display must redraw without pause during the test: a frame takes about 80 ms at 15 MHz, so an update every 20 ms already does.
6. **Commissioning temperature.** Take a commissioning sample only while the die temperature sensor reads 25 to 50 °C. Try again every 10 minutes while nothing is stored, not only ten cycles after a start.
7. **Step 0 reads the supply.** Add GPIO3, ADC1 channel 2, to the continuous ADC pattern at 6 dB and take the mean of each window. Step 0 passes only with 0.60 to 1.50 V. Under 0.60 V: wait, and report supply low, which clears itself. Under 0.10 V with edges on `AC_ZC` for 2 s or more: a rectifier fault that needs repair (D2, R1, C1). Above 1.50 V: do not run, report supply high. Design section 19.10.
8. **Step 1 judges the rest levels.** Window means with the keep-alive stopped: `ISENSE_LO` 0.70 to 0.88 V, `ISENSE_HI` 0.68 to 0.90 V. Both above 0.90 V: a supply over-voltage fault that needs repair (rule 18). One outside its limits: the current measurement is lost (rule 19).
9. **Rule 19, lost current measurement.** Steps 5, 5b and 6 are skipped as now. New: a fault condition of its own, shown on the display and on every interface for as long as it lasts.
10. **Step 3 watches `OC`.** `OC` low while no valve bit is low is not an over-current. Do not count it against an output. Stop the keep-alive for 1.5 s and try once. On the second time, a fault of its own that needs repair: the set input of the latch is held low. Design section 19.8.
11. **Rule 15.** No code change. The text of `idle_relay_on` already names Q2, U1, F5 and D25. Add the test a person can do: unplug USB.

**No change needed.** The cycle of commissioning (question 5), the 2.5 s
(question 6), the relay linger, stopping the float test at the first high
read, the DC component threshold, and the analogue reading of GPIO7.

**New things to show a user.** Supply low, supply high, rectifier fault,
supply over-voltage, current measurement lost, latch input held low. The
red LED D8 lit while nothing runs means the last of these, or U3.

**Documentation.** Firmware architecture section 20 and the troubleshooting
page quote the old values of items 1 to 3 and the old rule for item 4.

## 8. What I ran and what I did not

All with ngspice 47 on the netlist of this pull request unless a line says
otherwise.

| Check | Result |
|---|---|
| `failsafe.py commission`, 5544 runs | Section 2.4. Run on the netlist before U13 was added. U13 does not act below 3.9 V, and the 96 corners of `sequence`, run with it, give the same range of tA and tB as the corner runs of review A2: 0.22 to 0.83 s and 0.10 to 1.45 s. |
| `failsafe.py sequence`, 192 runs | With 2.0 s of hold, 0 of 96 healthy corners fail a step. After one edge the relay read-back is low for up to 0.68 s at -20 °C, 0.83 s at 27 °C and 1.33 s at 70 °C. |
| `failsafe.py ldo`, 945 runs | Section 4 |
| `failsafe.py floatload`, 288 runs | Section 2.7. 0 cases in which the valve stays on and the test, with the net at 50 %, reports nothing. |
| `failsafe.py faults`, 397 single faults, 1980 runs, on the new netlist with the new sequence | The 57 faults that stick one layer on are those of review A2, with the same times and the same step finding each. The same three bridges from `KA` leave the valve on. Of the 21 faults of the new parts, 7 keep drive from enabling and fail the start test, and 14 leave U13 unable to act and show nothing. R53 open is now found. Three rows hit the time limit on a busy machine and were run again alone. Two rows, the bridges at U7 that tie a gate output to its own input, do not finish in the model, as in the review. |
| `failsafe.py bridges`, 200 runs | The table is the same as review A2's, to the byte: neither U13 nor the longer step 2 moves it. |
| `ngspice -b vin_mon.cir`, 22 corners | Section 2.2 |
| `make -C hardware design-check` | Passes: 353 components, 99 BOM lines, the committed schematic netlist equals `netlist.yaml` pin for pin |
| `make` in `hardware/zoneflow-a/` | Passes the netlist comparison, the page lint and ERC |
| `uv run mkdocs build --strict`, `tools/docs/check_links.py site` | Passes. 6206 internal links in 56 pages, 0 broken |
| `tools/cost`: `pytest`, `ruff check`, `zfcost render --check` | 18 tests pass. The cost page is current with the TPS3701 and the five new placements counted |

**Not run.** The corner grid, the Monte Carlo set, the duty sweep and the
warm-up sweep of review A2 were not repeated. Their committed results are
for the netlist before U13.

**Not checked, because it needs a board.** Everything in section 20 of the
design. For these answers in particular: the real release times and their
change with temperature, the offset of the die temperature, whether the
float test sees a real bridge, the ADC on GPIO7, U13 at a real 5 V rail and
what the processor does there.

**Data sheets opened for this work.** TI TPS3701 SBVS240C and TPS3700
SBVS187G. TI TCA9539 SCPS202C, LM2901 SLCS006Z, TLV803E. Nexperia 74HC132
rev 8. Microchip MCP6002 DS20001733L. NXP PCF8563 rev 11.1. Espressif
ESP32-S3 datasheet v2.2 and the ESP-IDF pages on the temperature sensor,
v5.0 and v5.5. onsemi MOC3023M rev 3. JLCPCB parts search on 2026-11-10 for stock
and price of the TPS3700, TPS3701, TPS3710 and TPS3711.
