# Fault injection and corner analysis of the fail-safe circuits

Added by review A2 (`docs/design/hardware-review-a2.md`). It answers two
questions about the keep-alive fail-safe of design sections 7 and 8:

1. Which single fault leaves a valve on after the keep-alive stops, and
   which firmware test finds each fault?
2. Over tolerance, temperature and supply, do the two detectors hold while
   the pin toggles and let go in time when it stops?

The answers to the firmware's questions on the self-test
(`docs/design/hardware-answers-a2.md`) added four runs, which answer three
more:

3. What may a board store as its own release times, and how wide must the
   band around them be? (`commission`)
4. Does the self-test pass on a healthy board at every corner? (`sequence`)
5. Are the valves off if the 3.3 V regulator shorts, whatever the processor
   then does? (`ldo`, and `floatload` for the float test)

No board exists. The runs check the circuit as drawn in
`hardware/netlist.yaml` against behavioural part models. They do not replace
a bench test.

## Run

    uv run hardware/sim/failsafe/failsafe.py faults      # every single fault, five runs each
    uv run hardware/sim/failsafe/failsafe.py corners     # grid, Monte Carlo and the runs with no toggling
    uv run hardware/sim/failsafe/failsafe.py bridges     # resistance sweep of a bridge onto the keep-alive net
    uv run hardware/sim/failsafe/failsafe.py duty        # how lopsided the keep-alive may be
    uv run hardware/sim/failsafe/failsafe.py warmup      # release times against how long the keep-alive ran
    uv run hardware/sim/failsafe/failsafe.py commission  # learning limits and the band of step 9
    uv run hardware/sim/failsafe/failsafe.py sequence    # the self-test on a healthy board at every corner
    uv run hardware/sim/failsafe/failsafe.py ldo         # U3 shorted, with and without the detector U13
    uv run hardware/sim/failsafe/failsafe.py floatload   # what a bridged net must do for the float test to see it
    uv run hardware/sim/failsafe/failsafe.py compare --tag asfound   # against the author's table
    uv run hardware/sim/failsafe/failsafe.py deck Q3.S-D.short selftest --out /tmp/deck.cir

Needs `ngspice` (47 was used) and `uv`. `faults`, `corners` and
`commission` use every core and take 10 to 30 minutes each on a 16-core
machine. `commission --summary-only` and `ldo --summary-only` write the
tables again from the result file without running anything. Results are
written to `results/` and committed. `--netlist <file>` runs another
netlist and `--tag <name>` names the result files. The results for the
netlist as review A2 found it were made that way, from the file of pull
request #20:

    git show 5b0c4bf:hardware/netlist.yaml > /tmp/asfound.yaml
    uv run hardware/sim/failsafe/failsafe.py faults --netlist /tmp/asfound.yaml --tag asfound --sequence merged
    uv run hardware/sim/failsafe/failsafe.py compare --tag asfound
    uv run hardware/sim/failsafe/failsafe.py bridges --netlist /tmp/asfound.yaml --tag asfound-bridges

The neighbours of the keep-alive pin on the module edge are contact inputs.
`faults.py` reads each one's circuit from the netlist: the pull-up, the
series resistor if there is one, the capacitor if there is one. When the
debounce of the push contacts moved into firmware (design section 28) the
Back button, on the pin beside the keep-alive, was to lose its 10 kohm
series resistor R72. The result for a netlist without it is kept as
`results/back-without-r72.md`, and it is the reason R72 stayed:

    # a copy of hardware/netlist.yaml without R72, its two nets joined as BTN_BACK
    uv run hardware/sim/failsafe/failsafe.py faults --ids KA~ENC_B KA~ENC_B.20Hz KA~BTN_BACK KA~BTN_BACK.20Hz \
        --netlist /tmp/no-r72.yaml --tag back-without-r72

`deck` writes the ngspice file of one fault in one scenario, to look at a
single result by hand. `faults --ids <id> --update` runs single faults again
and replaces their rows in the result file. It is for runs that hit the
90 s limit only because the machine was busy with something else, and with
`--timeout 900` for the one run that needs three minutes, the self-test with
`KA_INP` shorted to `3V3`. `--sequence merged` makes the markdown table show
what the self-test found as it was merged in pull request #20; the CSV
always has both.

## How it works

`model.py` reads `hardware/netlist.yaml` and writes one flat ngspice deck. A
part is in the deck if its reference designator is in `SCOPE`; the nets on
its pins come from the netlist and from nowhere else. So a change to the
netlist changes the analysis, and a part that is wired differently from what
the design document says is simulated as wired.

| File | What it holds |
|---|---|
| `model.py` | Part models, the supplies, the ESP32 pins, fault switches |
| `faults.py` | The fault list |
| `run.py` | The two scenarios, the ngspice runner, and the readings a firmware takes |
| `corners.py` | Tolerance, temperature, supply and waveform corners |
| `bridges.py` | The resistance sweep of a bridge onto the keep-alive net |
| `duty.py` | The two phases of the keep-alive swept at the worst corner for detector B |
| `warmup.py` | The stop test after 0.1 to 2 s of toggling |
| `commission.py` | 264 boards, each healthy and with one bleed resistor open, at seven temperatures: what a commissioning cycle measures, which limits tell the boards apart, and the band of step 9 |
| `sequence.py` | The self-test sequence with no fault at the 96 corners, and how long the relay read-back stays low after a single edge |
| `ldo.py` | U3 shorted: a dead processor, an erratic one, a pin joined to its supply, no edges, and the threshold of U13 |
| `floatload.py` | A bridge onto the keep-alive net from a net that is high for 10 to 90 % of the time, or steadily |
| `author_table.py` | The fault table of design sections 7.3 and 8.5 as merged in pull request #20, one entry per fault, for `compare` |
| `failsafe.py` | Command line, result tables |

### The two scenarios

- **Self-test.** The sequence of design section 19.7 with one short run of
  zone 1: idle test, pin-high test, float test, start, expander read, weld
  check, gate bleed check, relay, run, end, stop test. `run.py` applies the
  pass condition of every step to the simulated voltages, with the logic
  thresholds of the ESP32-S3 (0.25 and 0.75 x VDD) and of the TCA9539 (0.3
  and 0.7 x VCC). A level between the thresholds counts as not defined. The
  stop test is judged twice, with the limits of the sequence as merged and
  with the limits review A2 gives it.
- **Crash.** Zone 1 runs. Then the keep-alive pin stops low, stops high or
  goes high-impedance, and the firmware never writes again. The run goes on
  for 4 s.

Every fault is run five times: the self-test with the fault present from
power-up, the self-test with the fault appearing while the valve runs, and
the crash, fault appearing half a second before the stop, with each of the
three ways the pin can stop. A run with no fault comes first. Its two
release times are the values "stored for this board" that step 9 compares
with.

### What counts as "the valve can be on"

Valve current needs the relay closed and the triac fired. Both are judged on
the unsafe side:

- **Relay.** Closed from 27 mA of coil current, the lowest that may operate
  a G5NB (3.75 V over 137.5 ohm). Open only below 3.6 mA, the lowest that
  may hold one (the 10 % must-release voltage over 137.5 ohm).
- **Triac.** Fired by any opto LED current above 0.25 mA. The MOC3023
  guarantees firing at 5 mA and gives no lower limit; 0.25 mA is my
  allowance, 5 % of that.

A real relay lets go earlier and a real opto needs more, so the times in the
tables are upper bounds for this model. They do not include the 10 ms
release time of the relay or the half-cycle a triac conducts after its gate
drive ends.

### The fault list

For every part in the scope: each pin opened, and each pair of pins bridged
that the package allows (both pins of a two-pin part, all three pairs of a
SOT-23, neighbours on one side of an SOIC-14 or TSSOP-24). For the ICs also
each output stuck. An open pin on an input with no DC path (a CMOS input, a
MOSFET gate) is run three ways: holding its last level, leaking high and
leaking low. Then the board: every net of the scope shorted to ground and to
`3V3`, leaks onto the two reservoirs, bridges from the keep-alive net to its
neighbours at the module pins and to the nets that toggle without the
control loop, the read-back pins driven by mistake, and the supplies at
wrong voltages.

Not simulated, and listed as such in the table: bridges to expander pins
outside the model (other valve bits, spare pins, the I2C bus).

A run that ngspice aborts is repeated with relaxed tolerances. ngspice pads
the output of an aborted run with the last values, which reads like a valid
result, so `run.py` looks for the abort message and does not trust the file.
A run that does not finish in 90 s of computing is reported as "run not
finished" and never as a result.

## Models and their limits

Every model is behavioural and is fitted to the data sheet figures quoted in
`model.py`.

| Part | Model | Fitted to |
|---|---|---|
| BAT54S | Diode, IS 2.2e-7 A, N 1.05, EG 0.69, XTI 2 | Nexperia data sheet of 1 July 2022, Fig. 2: 0.25 to 0.7 uA at 25 C, 18 to 35 uA at 85 C, 180 to 300 uA at 125 C (typical, 1 to 10 V). The model gives 0.22, 19 and 196 uA. Corners use 1.5e-8 A (the maximum forward voltages of table 7) and 6.6e-7 A (three times the typical leakage). The part fitted since the BOM consolidation is Hongjiacheng's (LCSC C7420333). Its sheet, rev 2.0, has the same maximum forward voltages up to 30 mA and typical leakage of about 0.1 to 0.3 uA at 25 C and 25 to 55 uA at 100 C from 0 to 10 V, where this model gives 0.22 and 48 uA: inside the same two ends. The decks do not change with the part number. |
| AO3400A, AO3401A | Square-law FET with a smooth sub-threshold corner | VGS(th) limits and Fig. 1 of the AOS sheets. Threshold drift with temperature is not in the sheet: -3 mV/K nominal, -1.5 to -4.5 mV/K in corners. |
| LM2901 | Four comparators: offset, input bias current out of the pins, open-collector output that sinks 16 mA | TI SLCS006S |
| 74HC132 | Four NAND gates, Schmitt inputs at 0.55 and 0.36 x VCC, 80 ohm outputs, input clamp diodes | Nexperia rev 8, interpolated to 3.3 V |
| TCA9539 | Reset input, three pins with an output-enable and a level bit each; a reset or a loss of supply clears the bits | TI SCPS202C. The model treats the device as reset only below 0.3 x VCC on RESET, the choice that keeps the bits longest. |
| TPS3701 | Comparator B only: a switch with 5.2 mV of hysteresis at 0.400 V on INB, an input current, a 22 us delay and an open-drain output of 30 ohm that is off below 1.7 V of supply | TI SBVS240C. `ldo` moves the threshold to 397 and 403 mV, the input current to +/-25 nA and the divider resistors by 1 %. Comparator A is not connected on the board and not modelled. |
| G5NB | 125 ohm and 0.2 H; the inductance is assumed | Omron |
| ESP32 pins | Keep-alive: push-pull through 30 ohm or high-impedance. Read-backs: inputs, 45 kohm pull-down on GPIO42 | ESP32-S3 datasheet v2.2, table 5-4 |
| Supplies | `V_BUCK`, `V5` and `3V3` as sources chained in that order, each limited to 1.5 A and unable to sink, with the bulk capacitance of the rail | Design section 4. The current limit is the peak limit of U1, taken for all three. |
| Supervisor U12 | Resets the processor when `3V3` is under 2.871 V. The processor then stays in reset for the rest of the run: its pins are inputs and it writes nothing. | TI TLV803EA29, lowest threshold: 2.93 V less 2 %, design section 12.4. Until 2026-11-12 the model took 2.94 V, the figure that section then gave for the TLV803EA30 |

Limits to keep in mind when reading a result:

- **Gates are ideal apart from their thresholds and a delay.** A fault that
  ties a gate output to its own input makes the model oscillate, the time
  step collapses, and the run is stopped after 90 s of computing. Those rows
  say so and are judged by hand in the review.
- **Gates and comparators decide through a delay of 47 and 22 us.** The real
  parts take 10 ns and 1.3 us. The delay is for the solver. U7 holds a
  latch, and without the delay one bad Newton iterate inside a time step can
  carry the solution to the latch's other state: the over-current latch
  then sets with nothing on its set input. That happened in corner runs
  where Q2 sat at its threshold with coil current flowing, and a run that
  loses gate drive that way looks safer than it is. Every run is now checked
  for a latch that set while `OC_TRIP_N` stayed high. A run that shows it is
  repeated with a tenth of the time step, and the result file says how many
  were repeated and whether any still showed it (column `solver`).
- **A processor that has been reset stays reset.** After a brownout a real
  ESP32 boots again and its firmware decides afresh what to do. The model
  does not follow that. It matters for one kind of fault, a short that
  collapses the supply whenever Q1 and Q2 conduct: with a processor that
  simply carried on, the pin would be driven high again after every
  recovery, and those edges alone kept both detectors charged in an earlier
  version of the model.
- **A dead IC is not modelled.** A supply pin opened, a ground pin opened and
  a rail at the wrong voltage are simulated with the models above, which
  keep working by their equations. What a real part does outside its
  ratings is not known.
- **The capacitors do not change with temperature.** A board keeps the
  values it was given. An X5R part may move by 15 % over its range. This
  matters for `commission`, where one board is run from -20 to 70 C: the
  temperature factors it finds are a lower bound.
- **The MOSFET conducts further below its threshold than a real one.** The
  model gives 27 mA at the data sheet threshold, which the sheet defines at
  250 uA. Release times that end at "the read-back is high again" are
  therefore upper bounds. This is the pessimistic direction for the hold of
  step 2 and for tB.
- **Sources that stand in for nets outside the scope run at 500 Hz**: the
  PSRAM strobe, the backlight PWM, the boot log. The real signals are faster.
  Both detectors are rectifiers, and `../keepalive_faults.cir` shows the
  result does not depend on the rate up to 1 MHz.
- **The 24 VAC on the zero-cross input is a trapezoid**, on only in the runs
  that involve the zero-cross nets.
- **While the rail ramps in the first millisecond** the gate thresholds are
  already at their final values, so levels in the first 10 ms mean nothing.
  No reading is taken there.

## Results

Sections 3 and 4 of the review discuss them.

| File in `results/` | What it holds |
|---|---|
| `faults.csv`, `faults.md` | Every fault on the netlist with the changes of review A2. The review prints the table as its appendix A. |
| `asfound.csv`, `asfound.md` | The same on the netlist of pull request #20 |
| `asfound-compare.md` | The author's table against `asfound.csv`, row by row |
| `bridges.md`, `asfound-bridges.md` | The resistance sweep of a bridge onto the keep-alive net, for both netlists |
| `corners-grid.csv`, `corners-mc.csv`, `corners-noedge.csv`, `corners.md` | The corner runs on the netlist with the changes, and their summary |
| `duty.csv`, `duty.md` | Gate margin of Q2 against the two phases of the keep-alive |
| `warmup.csv`, `warmup.md` | Release times against how long the keep-alive ran before the stop |
| `commission.csv`, `commission.md` | What a commissioning cycle measures on healthy and faulty boards, the learning limits and the band |
| `sequence.csv`, `sequence.md` | The sequence on a healthy board at every corner |
| `ldo.csv`, `ldo.md` | A shorted 3.3 V regulator, with and without U13 |
| `floatload.csv`, `floatload.md` | The float test against the duty of the bridged net |

### Which netlist and which sequence each result is for

The answers to the firmware's questions changed two things that results
depend on: the netlist gained the over-voltage detector U13 with four
passive parts, and step 2 of the sequence holds the pin high for 2.0 s
where it held it for 1.0 s.

| Result | Netlist | Sequence |
|---|---|---|
| `faults`, `bridges`, `sequence`, `ldo`, `floatload` | With U13 | Step 2 at 2.0 s |
| `commission` | Before U13 | Does not run the sequence |
| `corners` | With U13, run again with the BOM consolidation on 2026-11-11. Against the files of review A2, 197 of 2,104 rows moved by one unit in the last printed digit, no verdict changed and the summary has the same figures | Does not run the sequence |
| `duty`, `warmup` | Before U13, as review A2 ran them | Do not run the sequence |
| `asfound*` | Pull request #20 | As merged there, and as review A2 left it |

U13 pulls the latch input only with `3V3` above 3.9 V, and no run of
`commission`, `corners`, `duty` or `warmup` goes above 3.6 V. As a check, `sequence` ran the 96
corners of the grid with U13 in the deck and found the same range of
release times as `corners`: tA 0.22 to 0.83 s and tB 0.10 to 1.45 s.
Appendix A of review A2 is the fault table as that review ran it, without
U13 and with 1.0 s in step 2. `faults.md` is the table for the board as it
is now.
