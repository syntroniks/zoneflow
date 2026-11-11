# ZoneFlow hardware review A2

Status: **second independent review of the rev A fail-safe circuits,
2026-11-10**. It covers the keep-alive detectors, the logic behind them, the
relay drive, the regulator over-voltage question and the output stage of
[hardware design](hardware-design.md) as merged in pull request #20, after
[review A1](hardware-review-a1.md) and the changes that followed it. It is
tracked in [issue #11](https://github.com/syntroniks/zoneflow/issues/11).

No board exists and I had no bench. Every statement here comes from a
manufacturer data sheet that I opened, a calculation from its figures, or an
ngspice run that is committed under `hardware/sim/`. Section 11 says which is
which and what a bench still has to show.

The author wrote of the second detector and the combined fault table: "The
new detector and the combined fault table are mine and nobody has reviewed
them." This is that review. I did not start from the author's table. I
rebuilt the analysis from `hardware/netlist.yaml` with a generator that
injects each fault into a simulation, and compared the two afterwards.

## 1. Verdict

**The two-detector fail-safe meets the single-fault rule for component
faults. As merged it did not meet it for one fault of the board, and that
is changed in this pull request.**

- **Components.** I injected 376 single faults into a simulation built from the netlist. No fault in a component leaves a valve on after the keep-alive stops. 57 faults stick one of the two layers on. The other layer turns the valve off in every one of them, and a step of the self-test finds every one. The author's central claim holds.
- **Blocking, B1, fixed here.** The keep-alive pin sat next to a pin that the author took to be unconnected. On this module it is the data strobe of the PSRAM. A bridge of up to about 5 kohm between the two kept both detectors on while the processor was in reset or booting, and no step of the self-test found it. The keep-alive is now on a pin whose neighbours cannot do that.
- **Major, fixed here.** A bridge to the zero-cross net could hold both detectors, where the design document said one (M1). The regulator over-voltage was open (M2). The self-test could not see any net that drives the keep-alive net while the pin floats (M3). A shorted Q3 was found by no test (M4). A lopsided keep-alive that met the contract did not hold the relay when cold (M6). The time limits of the stop test would have rejected healthy boards (M7).
- **Major, open.** A shorted 3.3 V regulator puts 4.95 V on the processor. The design document calls that safe, and nothing shows it (M5). It needs the owner's decision, like the buck regulator did.
- **Corners.** With every tolerance against it, gate drive is removed within 0.84 s of the last edge, and valve current ends with it. If the pin stops high it takes 1.25 s. The relay transistor alone lets go within 1.46 s, 2.19 s if the pin stops high, which is later than the design document said (m10). A steady level never enables drive. The firmware's 5 ms toggle holds the relay in every corner with 0.38 V to spare at the gate of Q2.

Counts: 1 blocking, 7 major, 13 minor, 6 notes. This pull request fixes the
blocking finding, six of the seven major findings and ten of the thirteen
minor ones. M5 is open. For m6 and m12 I recommend a change and did not
make it. m13 is a cost of one of the fixes, and I accepted it.

## 2. Findings

Severity as in review A1. **Blocking** must be resolved before capture.
**Major** needs a decision or a bench test before boards are ordered.
**Minor** should be corrected and can be done during capture. **Note**
needs no change to the design.

| ID | Severity | Block | What is wrong | Evidence | Fix |
|---|---|---|---|---|---|
| B1 | Blocking | Keep-alive pin | `KA` was on GPIO38, module pin 31. Pin 30 is GPIO37, which the design document calls "not connected". On the N16R8 it is the data strobe of the octal PSRAM and toggles whenever the processor reads the PSRAM, which every boot does. A bridge of 0 to about 5 kohm from it to `KA` holds both detectors on while the pin is high-impedance. The 1 and 4.7 kohm bridges pass every step of the self-test. | ESP32-S3 datasheet v2.2 table 2-14; module datasheet v1.8, pin table note b. `results/asfound.md`, rows "KA bridged to module pin 30". Section 3.3. | **Fixed.** `KA` is on GPIO17, module pin 10, between `ENC_B` and `BTN_BACK`. `ENC_SW` took GPIO38. |
| M1 | Major | Keep-alive net | A bridge from `KA` to `AC_ZC` holds both detectors on once drive is enabled, for a Q2 from the lower third of its threshold range at 27 °C and above. The design document says it holds B only, because it compared A's reservoir with the turn-on threshold of 1.55 V. A is already on when it matters and turns off at 0.94 V. The author's own file puts the reservoir at 1.0 V. | `hardware/sim/ka_bridge.cir`, re-run. `results/asfound-bridges.md`. Section 3.3. | **Fixed.** R33 from 10 kohm to 2.2 kohm: a net behind a 10 kohm pull-up cannot lift `KA` past 0.6 V. Design section 8.5 corrected. |
| M2 | Major | Logic supply | Regulator over-voltage, risk 21: the switch of U1 failing short put `VIN_DC` on every 5 V and 3.3 V part, with nothing known about the keep-alive after that. A crowbar onto the input fuses cannot work, because R1 limits the fault current to 1.6 to 3.7 A rms. | `hardware/sim/buck_overvoltage.cir`. Section 6. | **Fixed.** D25, SMBJ6.0A, on `V_BUCK`. F5, 0.5 A very fast, in the input of U1. `V_BUCK` stays under 9.7 V and `3V3` in regulation until F5 opens. |
| M3 | Major | Self-test | No step looks at the keep-alive net while the processor does not drive it. A bridge of a kilohm or more to a net that toggles is invisible while firmware drives the pin and takes over when the pin floats. | `results/asfound.md`: the 1 kohm bridges are found by no step of the sequence as merged. | **Fixed in the procedure.** Step 2b, the float test, design section 19.7. Firmware has to implement it. |
| M4 | Major | LED supply switch | Q3 shorted is found by no test. The author left it for lack of a pin. | Design section 8.5 as merged. `results/asfound.md`, rows Q3. | **Fixed.** R92 reads `V_DRV` behind Q3 in place of `DRV_EN`. The idle test now fails. Section 7. |
| M5 | Major | 3.3 V supply | U3 shorted input to output puts 4.95 V on `3V3`. The design document lists it as safe: "either the ESP32 keeps running, and then nothing is wrong with the keep-alive, or it stops". A processor at 4.95 V against an absolute maximum of 3.6 V can do a third thing, and that is the same argument the author did not accept for the buck regulator. Both detectors themselves still release at 4.95 V. | ESP32-S3 datasheet v2.2 table 5-1. `results/faults.md`, row "U3 shorted". | **Open.** Recommended: a detector that holds `EN` low when `3V3` is above about 3.6 V. Section 10. |
| M6 | Major | Detector B | With the keep-alive lopsided, 1 ms in one state and 20 ms in the other, Q2 is not held with a high-threshold part and `3V3` at 3.0 V: 24 and 24 of 288 corner runs, most of them at -20 °C. At 3.2 V the gate is still 0.13 V short. The contract says only "an edge at least every 20 ms", which that waveform meets. With symmetric edges 20 ms apart the margin at the gate is 0.08 V. | `results/corners.md`, `results/duty.md`. Section 4.2. | **Fixed in the contract**: no phase shorter than 2.5 ms. The firmware's 5 ms toggle has 0.38 V. Bench item. |
| M7 | Major | Self-test | The pass limits of the stop test, 0.3 to 1.0 s for tA and 0.08 to 1.0 s for tB, reject healthy boards at tolerance corners: 16 and 18 of 192 corner runs at the firmware's own toggle rate. A failed stop test tells the user the board needs repair. | `results/corners-grid.csv`. Section 4.5. | **Fixed in the procedure.** Wider absolute limits, and a band around a value stored for each board, which is what finds an open bleed resistor. Design section 19.7. |
| m1 | Minor | Read-back | The high level at GPIO42 was 2.70 V against 2.48 V needed, with a pull-down that the data sheet gives as "typical 45 kohm". | ESP32-S3 datasheet v2.2 table 5-4. | **Fixed.** R92 is 4.7 kohm: 2.99 V. |
| m2 | Minor | Detector A | R35 shorted sticks A on. The design document lists it with the faults that keep A from enabling. | `results/asfound.md`, row R35 short. | **Corrected** in design section 8.5. B covers it and the idle test finds it. |
| m3 | Minor | Relay drive | R12 open is listed as found by the weld check. It is not. Q1 then stays on through an expander reset, and nothing reports it. | `results/asfound.md`, row R12 open. | **Fixed in the procedure.** Step 5b, design section 19.7. |
| m4 | Minor | Detector B | "Q2 gate open: K1 never closes." A gate that opens while charged, or leaks high, holds Q2 on. | `results/asfound.md`, rows Q2 pin G open. | **Corrected.** A covers it; the idle or stop test finds it. |
| m5 | Minor | Fault table | Floating IC inputs are not in the author's table. U6 pin 7 open sticks A on. U7 pin 9 and U5 pin 3 can stick a layer on, depending on which way they float. Bridges between pins 9 and 10 and between pins 11 and 12 of U7 stick A on. | Appendix A. | **Corrected**: design section 8.5 has them. All are covered and found. |
| m6 | Minor | Sensor supply | While D25 clamps, U11 sees up to 9.7 V on a 7 V absolute maximum. If it failed with its input joined to its enable pin it would drive GPIO45. | TI TPS2553, absolute maximum ratings. Section 6.5. | Recommended, not applied: 10 kohm in `SENS_PWR_EN`. |
| m7 | Minor | Input protection | The design document's fuse table has F1 and F4 "in the loop" for a shorted C1. They are, and they do not open: R1 limits the current to 54 to 122 % of their rating. R1 burns. | `hardware/sim/buck_overvoltage.cir`, follow-current lines. | **Corrected** in design section 5.3. No circuit change. |
| m8 | Minor | Models | "The BAT54S data sheet does not limit leakage above 25 °C" is true of limits. The sheet has typical curves at 85 and 125 °C, and the model matches them: 19 and 196 uA against about 20 and 200 uA. The header of `relay_detector.cir` says 4 uA at 70 °C; the model gives 7 uA. | Nexperia BAT54S, 1 July 2022, figure 2. | **Corrected** in design section 22 and in the file header. |
| m9 | Minor | Detector A | The statements on slow toggling are too firm both ways. The document has "1 Hz never enables" and "3 Hz and faster always did". Over my corners 1 Hz enables A in 2 of 24 and 5 Hz fails to in 11 of 24. | `results/corners.md`, no-toggling table. Section 4.4. | **Corrected** in design section 8.4. The contract is unchanged: an edge every 20 ms is on, a steady level is off, nothing in between is promised. |
| m10 | Minor | Detector B | The relay transistor can let go later than the document says: 1.46 s after the last edge, 2.19 s if the pin stops high, against 0.95 and 1.4 s. The difference is the range I gave the threshold drift and the capacitors. It matters only with detector A already stuck. The specification says the relay detector releases "in about the same time" as the gate drive's one second. At this corner it does not. | `results/corners.md`. Section 4.3. | **Corrected** in design sections 7.3 and 19.4 and in the pin map. I did not change the specification. Whether 2.2 s in a double event needs a tighter circuit is the owner's call, and the drift has to be measured first. Risk 28. |
| m11 | Minor | Firmware | GPIO17 is the default TXD pin of UART1 in ESP-IDF. A UART opened on its defaults would drive the keep-alive net. | Pin choice of B1. | Firmware rule 16 in design section 19.1. |
| m12 | Minor | Output stage | A 50 mA relay coil keeps 94 % of its voltage at the worst corner. A coil that needs 85 % of 24 V to pull in has no margin on a 21.6 VAC supply at -20 °C. | `hardware/sim/outstage_smallload.cir`. Section 8.3. | A line for the user documentation. Not a change of triac. |
| m13 | Minor | Read-back | The fix for M4 costs one detection. U7 with its supply pin open was found by the low level it put on `DRV_EN`. It is not found at `V_DRV`. In the model U7 is then powered through its input diodes and both layers still work. | `results/asfound.csv` and `results/faults.csv`, row U7 pin 14. Section 7. | Accepted. Design section 8.5 lists it with the faults that no test finds. |
| n1 | Note | GPIO45 | The strap is safe twice over: the net cannot be high at reset, and the module ignores the strap. | Section 5. | Bring-up step added: read the eFuses. |
| n2 | Note | Output stage | The T810 has the same trigger, holding and latching limits as the T410. The pinout is right. The short-circuit table reproduces. | Section 8. | None. |
| n3 | Note | Output stage | The short-circuit calculation assumes 27.5 V open circuit and 12 % regulation. At the specification's 32 V the margin in triac surge current is 1.5 instead of 1.8. A stiff 75 VA transformer comes close to the 40 A limit of the PTC. | Section 8.4. | State the assumed regulation in the envelope. Risk 4 covers the bench. |
| n4 | Note | Detector A | One rising edge at the worst tolerance corner charges A's reservoir to 1.64 V, against a turn-on threshold of about 1.69 V at that corner, `3V3` at 3.6 V. It did not enable in any run, with 3 % to spare, which is less than the model can vouch for. The author's 1.00 against 1.55 V is the nominal case. | `results/corners.md`. Section 4.4. | None. With the expander fresh out of reset nothing would follow anyway. |
| n5 | Note | Model limits | Three bridges at U7 tie a gate output to its own input and the model oscillates. I judged them by hand. | Section 3.4. | None. |
| n6 | Note | Over-current latch | Outside the brief. By my reading of the logic about ten single faults stop the over-current latch from ever setting, and no test reports them: `OC_TRIP_N` or `FAULT_N` held high, `FAULT` held low, pin 1 of U7 open. I did not simulate an over-current together with each. The PTC is then the only protection of an output. | Appendix A, the rows of U6 and U7 with no test. Design section 8.5, "faults that no test finds". | None here. The bring-up plan tests the latch once. A test in service would need a test current. |

## 3. The single-fault analysis, rebuilt from the netlist

### 3.1 Method

`hardware/sim/failsafe/` reads `hardware/netlist.yaml` and writes an ngspice
deck of every part of the two detectors, the logic, the LED supply switch,
the expander, the relay drive, and the parts that share a package or a net
with them: 68 parts. The nets on each pin come from the netlist and
from nowhere else. Each part type has a behavioural model fitted to data
sheet figures; `hardware/sim/failsafe/README.md` lists them and their
limits.

The fault list is written from the netlist and the package pinouts:

- **Every part**: each pin opened, and each pair of pins bridged that the package allows. Both pins of a two-pin part, all three pairs of a SOT-23, neighbours along one side of an SOIC-14 or TSSOP-24.
- **Every IC**: each output stuck each way. An open pin on an input with no DC path of its own is run three ways: holding its last level, leaking high, leaking low.
- **Every net** of the scope shorted to ground and to `3V3`.
- **Leaks** of 47 kohm to 1 Mohm from a rail onto the two reservoirs and the nodes beside them.
- **The keep-alive net** bridged to each neighbour of its pin on the module edge, and to the nets that toggle with no control loop behind them.
- **The read-back pins** driven as outputs by mistake, and left unconnected.
- **The supplies** at wrong voltages.

Each fault is run five times. Twice through the self-test sequence of design
section 19.7 with a short run of zone 1 in the middle: once with the fault
there from power-up, once with the fault appearing during the run. Three
times as a crash: zone 1 runs, the fault appears, and half a second later
the keep-alive pin stops low, stops high or goes high-impedance and the
firmware writes nothing more.

A valve counts as on while the relay may be closed and the opto LED may fire
the triac. Both are judged on the unsafe side: the relay holds down to
3.6 mA of coil current, and 0.25 mA of LED current fires the triac. The
readings firmware takes are judged with the data sheet thresholds of the
ESP32-S3 and the TCA9539, and a level between them counts as not defined.

I ran the whole list twice: on the netlist of pull request #20, to compare
with the author's table, and on the netlist as this pull request leaves it.

**Two errors of my own model, found and removed.** Both made the circuit
look better or worse than it is, and a reader of the results should know
that they existed.

- **The latch set by itself.** U7 holds the over-current latch, and a latch has two valid states. In corner runs with Q2 at its threshold the latch set with nothing on its set input, because the solver had jumped to the other state inside one time step. A run that loses gate drive that way looks safe when it is not. The gates and comparators of the model now decide through a delay of some tens of microseconds, which removes the jump. Every run is also checked for a latch that set while `OC_TRIP_N` stayed high, and repeated with a tenth of the time step if it did. The check fired in none of the roughly 6300 runs behind this review.
- **The processor carried on after a reset.** With the coil of K1 shorted, the supply collapses whenever Q1 and Q2 conduct. The model's processor drove the pin high again after each recovery, and those edges alone kept both detectors charged for as long as the run lasted. A real ESP32 behind the supervisor U12 boots instead. The model now keeps a processor in reset once the supervisor has reset it.

### 3.2 Result for the netlist as merged

379 faults, 377 of them simulated. The list is three longer than
for the changed netlist, because the old pin had the PSRAM strobe beside it
and the bridge to it is run at five resistances.

- **Six faults leave a valve on.** All six are bridges from `KA` to a push-pull net that toggles by itself: the PSRAM strobe on the next module pin at 0 ohm, 1 kohm and 4.7 kohm, the backlight at 0 ohm and 1 kohm, and the boot-log pin at 0 ohm. The 0 ohm bridges keep the valve on however the pin stops, and the idle test of the sequence as merged finds them. The bridges of 1 and 4.7 kohm keep it on when the pin is high-impedance, and no step of the sequence as merged finds them.
- **No fault in a component leaves a valve on.** The author's central claim holds on his own netlist.
- **58 faults stick one layer on**, and the other layer ends the valve current in each. In the model the sequence as merged finds all but three. R12 open is finding m3. The strobe bridge at 10 kohm holds a Q2 with the lowest threshold and nothing else. The bridge to `AC_ZC` is finding M1, and the model misses its symptom: on the board it takes the zero-cross signal away while the pin is driven, and step 0, which the model does not run, refuses to start.
- **Q3 shorted, and Q3 with its gate open and leaking low, are found by nothing** (M4). The table does not count them as a layer stuck on, because the expander reset still removes the LED current.
- **85 of the 97 rows of the author's table agree with the simulation.** Section 3.4 has the other 12.

### 3.3 The faults that left a valve on

All of them are bridges from the keep-alive net to a net that toggles with
no control loop behind it. The design document knew the kind: "the one board
fault that could defeat both". It had the list of nets wrong in two places.

**The pin next to the keep-alive pin.** Design section 8.5 says: "The
neighbouring module pins are GPIO37, not connected, and GPIO39, a button
input." GPIO37 is not connected on the board. Inside the N16R8 it is the
data strobe of the octal PSRAM: table 2-14 of the ESP32-S3 datasheet maps
GPIO33 to GPIO37 to DQ4 to DQ7 and DQS, and the module datasheet says of
pins IO35 to IO37 that they "are connected to the Octal SPI PSRAM and are
not available for other uses". The strobe toggles whenever the processor
reads the PSRAM. The start-up code tests the PSRAM on every boot, so it
toggles for a large part of every start, before any firmware has touched
the keep-alive pin, which is an input until then.

`results/asfound-bridges.md` sweeps the resistance of that bridge:

| Net bridged to `KA` | Bridge | Pin driven low after the stop | Pin high-impedance after the stop | Reported by the sequence as merged | Reported by the sequence with step 2b |
|---|---|---|---|---|---|
| push-pull, 3.3 V | 0 ohm | **valve stays on** in 4 of 4 corners | **valve stays on** in 4 of 4 corners | every corner: 1 idle | every corner: 1 idle |
| push-pull, 3.3 V | 100 ohm | detector B held, valve off | **valve stays on** in 4 of 4 corners | 2 of 4 corners: 1 idle | every corner: 1 idle, 2b float |
| push-pull, 3.3 V | 470 ohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 1 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 2.2 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 3.3 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 4.7 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 10 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 0 ohm | both let go | **valve stays on** in 3 of 4 corners | 2 of 4 corners: 2 pin-high | 3 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 100 ohm | both let go | **valve stays on** in 2 of 4 corners | 1 of 4 corners: 2 pin-high | 3 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 470 ohm | both let go | **valve stays on** in 3 of 4 corners | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 1 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 2.2 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 3.3 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 4.7 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 10 kohm | both let go | detector B held, valve off | not reported | 1 of 4 corners: 2b float |

Four corners each: typical parts at 27 °C, a Q2 with a 0.65 V threshold at 27 °C, the same at 70 °C with a drift of -4.5 mV/K, and the same again with low-leakage diodes and `3V3` at 3.4 V. A step counts as reporting the bridge only if it passes at the same corner with no bridge. In 28 of the 64 combinations of net, bridge and corner a valve stays on and the sequence as merged reports nothing.

What this means in practice. A solder bridge of zero ohms would probably
have shown at the first power-up. It hangs both pump capacitors on the
strobe, and I would expect the PSRAM to fail its test. The bridge to fear
is the one of a kilohm or so, a flux residue or a dendrite between two
castellations 1.27 mm apart. It disturbs nothing
while firmware runs. The self-test as merged passes it, because every step
of it drives the pin. It acts only when the processor resets with a valve
on and comes back fast enough to read the PSRAM before detector A has let
go, or resets again and again. That is the case the fail-safe exists for.

**The zero-cross net.** The author simulated this bridge in
`hardware/sim/ka_bridge.cir` and found A's reservoir at 0.89 to 1.03 V and
the gate of Q2 at 0.83 to 0.95 V. I re-ran the file and got the same. His
conclusion was "this bridge can hold B on and cannot enable A: one detector,
not both". The first half is right. But A has hysteresis. It turns on at
1.55 V and off at 0.94 V. A valve that is running has A on already, and
1.0 V keeps it on. With the bridge, the pin high-impedance and a Q2 whose
threshold is under about 0.9 V, both detectors stay on and so does the
valve. The second half of the table above has the corners. The model does
not run step 0, so it reports nothing for these rows. On the board, with
the pin driven low, a bridge of up to about 30 kohm keeps the zero-cross
signal under 0.75 x VDD against its 10 kohm pull-up, and step 0 refuses to
run. The author says the same. That limits the exposure to a bridge that
forms during a run.

**What I changed.**

1. **The pin.** `KA` is on GPIO17, module pin 10. Its neighbours are `ENC_B` on pin 9 and `BTN_BACK` on pin 11, each a contact behind 3.3 kohm to `3V3` and 10 kohm in series, with `ENC_A` and `I2C0_SDA` beyond them. On the chip's own pin row the neighbours are the same two. `ENC_SW` took GPIO38. I considered GPIO39, one pin along: its neighbours would also be harmless, but the PSRAM strobe and `AC_ZC` would each be two pins away, and GPIO39 has a pull-up after reset.
2. **R33**, from 10 kohm to 2.2 kohm. A net behind a 10 kohm pull-up now lifts `KA` to 0.6 V at most. The author's own file, run with 2.2 kohm, puts A's reservoir at 0.40 V at most and the gate of Q2 at 0.36 V, against 0.94 V to hold A and 0.51 V for the lowest threshold of Q2.
3. **A float test in the self-test**, step 2b: the pin is made an input for half a second while the display, the backlight and the PSRAM are in use, and must read low, with both read-backs unmoved.

The same sweep on the netlist as changed:

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

The zero-cross net no longer holds detector A at any resistance. A
push-pull net that toggles by itself still defeats both detectors through a
bridge of up to about 3.3 kohm while the pin is high-impedance. No
circuit on a single pin can prevent that. What has changed is that no such
net is next to the pin, and that the self-test looks for it: in every case
of this sweep where a valve stays on, the float test or the idle test
reports the bridge at the same corner.

### 3.4 Differences from the author's table

`results/asfound-compare.md` sets each of the 97 rows of design
sections 7.3 and 8.5 that name a fault against the simulation of the same
netlist. 85 agree. The rest:

| Fault | The design document | The simulation | Comment |
|---|---|---|---|
| U6 pin 12 GND (GND) open | Stuck on: gate drive (detector A) stuck on. Found by: 1 idle. | Stuck on: none. Found by: not found. | A limit of the model, not a disagreement. The model's comparator finds ground through its input diodes and keeps working. The author's reading, that a comparator without ground cannot pull `KA_OK` low, is the safer one and the design document keeps it. |
| U7 output 4 (DRV_EN) stuck high | Stuck on: gate drive (detector A) stuck on. Found by: 1 idle. | Stuck on: Q1 stays on. Found by: 9 stop; 1 idle. | The LED supply still switches off, because Q3 follows output 3. What stays on is the expander, out of reset, and with it Q1. The same tests find it. |
| U5 pin P00 (Z1) stuck low | Stuck on: none. Found by: 7 run. | Stuck on: none. Found by: 4 expander. | Found earlier than the document says. Step 4a reads the pin low before any output is driven. |
| U5 pin P05 (RLY_ON) stuck high | Stuck on: relay path (detector B, Q1, Q2 or K1) stuck on. Found by: 5 weld check. | Stuck on: Q1 stays on. Found by: 4 expander. | The same layer. Found one step earlier, by the read of the input port in step 4a. |
| R12 open | Stuck on: relay path (detector B, Q1, Q2 or K1) stuck on. Found by: 5 weld check. | Stuck on: Q1 stays on. Found by: not found. | **Not found by the weld check.** At the end of a normal run firmware drives `RLY_ON` low before it releases the pin, which discharges the gate of Q1 with or without R12. Finding m3. |
| C50 open | Stuck on: none. Found by: 3 start. | Stuck on: none. Found by: 7 run. | The gate of Q2 follows the pin through C51 and the read-back goes low in time, so the start test passes. The read-back then reads high between pulses and the run check fails. |
| Q2 pin G (RLY_G2) open, holds its last level | Stuck on: none. Found by: 3 start. | Stuck on: Q2 stays on. Found by: 9 stop; 3 start. | A gate that opens while it is charged holds Q2 on. The document has "K1 never closes", which is true only of a gate that opens discharged. Finding m4. |
| Q2 pin G (RLY_G2) open, leaks high | Stuck on: none. Found by: 3 start. | Stuck on: Q2 stays on. Found by: 9 stop; 1 idle. | As the row above, for a gate that leaks towards a rail. Finding m4. |
| Q1 pins S and D bridged (RLY_B to RLY_A) | Stuck on: relay path (detector B, Q1, Q2 or K1) stuck on. Found by: 5 weld check. | Stuck on: none. Found by: 5 weld check. | Bookkeeping of the model, not a disagreement. It judges Q1 by its gate voltage. A shorted Q1 conducts whatever its gate does, so the relay path has lost Q1 as the document says, and K1 follows Q2 alone. |
| R35 short | Stuck on: none. Found by: 3 start. | Stuck on: gate drive stays, Q1 stays on. Found by: 9 stop; 1 idle. | **A stuck on**, not kept from enabling. The reference of the comparator is at 0 V and `KA_OK` is high from power-up. Finding m2. |
| KA bridged to AC_ZC anywhere on the board | Stuck on: relay path (detector B, Q1, Q2 or K1) stuck on. Found by: by its symptom. | Stuck on: gate drive stays, Q1 stays on, Q2 stays on. Found by: not found. | **Both detectors**, not one. With typical parts the bridge holds detector A, and it holds Q2 for a part with a threshold under about 0.9 V. Finding M1. The document's symptom is real: with the pin driven, `AC_ZC` follows the keep-alive and not the mains, and step 0 refuses to run. The model does not run step 0, which is why it says not found. |
| KA bridged to TFT_BL (backlight PWM, keeps running), 0.01 ohm | Stuck on: both. Found by: not found. | Stuck on: gate drive stays, relay stays closed, **valve stays on**. Found by: 9 stop; 1 idle. | Agreement on the effect. The document names no test. A 0 ohm bridge to a net that is toggling fails the idle test. A bridge of a kilohm does not, and nothing in the sequence as merged finds it. Finding M3. |

And what the author's table did not have at all:

- **Inputs left floating.** U6 pin 7, the comparator input: A stuck on. U7 pin 9: A stuck on if it floats high. U5 pin 3, the reset input: the expander never resets if it floats high.
- **Adjacent pins bridged.** U7 pins 9 and 10, `KA_OK` to `FAULT_N`: A stuck on, because the latch output overpowers the comparator. U7 pins 11 and 12, `DRV_EN` to `DRV_EN_N`: the two gates fight, `DRV_EN_N` sits low enough to keep Q3 on, A stuck on.
- **Nets stuck.** `KA_INP`, `KA_N1`, `KA_N2` or `KA_OK` to `3V3`, `KA_REF` or `DRV_EN_N` to ground: A stuck on. `RLY_G2` or `KB_N1` to `3V3`: Q2 stuck on. `RLY_A` to ground: the coil is energised for good. `RLY_A` to `3V3`: the coil keeps about 2 V, which cannot pull the relay in and may hold it.
- **Leaks.** 100 kohm from `3V3` onto `KA_N2` or `KA_INP` holds A, and 330 kohm onto `KA_INP` still does. 100 kohm from `3V3` or 330 kohm from `V_BUCK` onto `RLY_G2` holds Q2.

Every one of these is covered by the other layer and found by a test.

**Three bridges I judged by hand.** U7 pins 2 and 3, 5 and 6, and 8 and 9
each tie a gate output to an input of the same loop. The model oscillates
there and the runs did not finish.

- **Pins 2 and 3, and 5 and 6**: `FAULT_N` to `FAULT`, the two outputs of the latch. With `KA_OK` low, gate 2 drives `FAULT_N` high and gate 3 holds `DRV_EN_N` high whatever `FAULT_N` does, because its other input is low. Drive is off.
- **Pins 8 and 9**: `DRV_EN_N` to `KA_OK`. Gate 3 then drives its own input. `DRV_EN_N` low needs `KA_OK` high, and they are the same node. It cannot rest low. It may oscillate, and gate 4 then resets the expander on every swing, which clears the valve bits.

In none of them can drive stay enabled with the keep-alive stopped, and
detector B is untouched by all three.

### 3.5 Result for the netlist as changed

376 faults, 374 of them simulated. The two that are not are groups
of bridges at expander pins and resistor-array elements outside the model.
Appendix A says for each why it cannot hold a valve on.

- **Three faults leave a valve on**, and all three are bridges from `KA` to a push-pull net that toggles by itself: the backlight at 0 ohm and 1 kohm, and the boot-log pin at 0 ohm. Neither net is next to the pin. The two 0 ohm bridges fail the idle test. The 1 kohm bridge acts only while the pin is high-impedance, and the float test finds it.
- **No fault in a component leaves a valve on.**
- **57 faults stick one layer on.** The other layer ends the valve current in each, within 0.67 s of the stop with typical parts and 1.01 s if the pin stops high. Every one fails a step of the sequence. Fifty fail the stop test of the same run. Seven are found at the next start: C22 or C51 shorted, by the pin-high test; `RLY_ON` stuck high or shorted to `3V3`, and `RLY_G1` shorted to `3V3`, by step 4a; a welded K1, by the weld check; R12 open, by the gate bleed check.
- **120 faults lose no layer and fail no step.** Design section 8.5 sorts them. The ones to remember are R33 open, which removes the protection against a bridge without a symptom, and U7 without its supply pin, finding m13.
- **Three rows did not finish.** They are the bridges at U7 of section 3.4.
- **The solver check fired in no run.**

The complete table is appendix A.

## 4. Corners of both detectors

### 4.1 What was varied

`hardware/sim/failsafe/corners.py`, no fault fitted.

| Quantity | Range | Source |
|---|---|---|
| Temperature | -20, 27, 70 °C | Design section 3.3 |
| `3V3` | 3.0 and 3.6 V | The brief: the supply range of the ESP32-S3. U3 itself delivers 3.20 to 3.40 V. |
| `V_BUCK` | 5.16 to 5.40 V | Design section 4.6 |
| Reservoirs C23, C50 | 0.60 to 1.15 of nominal | My allowance for a 10 uF, 25 V X5R 0805 part at up to 3 V: tolerance, temperature, bias, ageing. Not read from a curve. |
| Pump capacitors C22, C51 | 0.65 to 1.15 | The same, with under 1 V of bias |
| Bleed resistors | +/-1.5 % | 1 % and 100 ppm/K |
| BAT54S | Saturation current 1.5e-8 A and 6.6e-7 A | The first fits the maximum forward voltages of the Nexperia data sheet, table 7. The second is three times the typical leakage of its figure 2: 21 uA at 70 °C, 57 uA at 85 °C. |
| AO3400A threshold at 25 °C | 0.65 and 1.45 V | AOS data sheet, rev 3 |
| AO3400A threshold drift | -1.5 and -4.5 mV/K | Not in the data sheet. Its figure 2 moves by -2 mV/K at 1 A. The author used -3 mV/K. |
| LM2901 offset and bias | +/-15 mV, 25 to 500 nA | TI SLCS006S |
| Pin resistance | 16 to 70 ohm | 40 mA at 0.66 V of drop at the highest drive strength. GPIO17 defaults to the 10 mA setting. |
| Coil resistance of K1 | +/-10 % | Omron |
| Keep-alive waveform | 1/1, 5/5, 20/20, 1/20 and 20/1 ms high/low | The contract of design section 19.4 and the brief |
| How the pin stops | Low, high, high-impedance | |

Two sets of runs. **The grid**: every combination of the extremes above
that move a result one way, 1440 runs. **Monte Carlo**: 400 runs with
every part drawn separately inside the same ranges, waveform phases drawn
between 1 and 20 ms. Then **264 runs with no toggling**. Two small
sweeps at single corners are in sections 4.2 and 4.6.

### 4.2 Does toggling hold the relay and the gate drive?

The grid, 288 runs for each waveform. "Gate margin" is the lowest voltage at
the gate of Q2 while the pin toggles, less the threshold of that Q2 at that
temperature. The relay counts as driven when Q2 drops 0.1 V or less.

| Keep-alive, high / low | Lowest gate margin of Q2 | Relay not driven | Lowest opto LED current | Lowest reservoir of A |
|---|---|---|---|---|
| 1 / 1 ms | +0.42 V | 0 of 288 | 10.1 mA | 2.21 V |
| 5 / 5 ms | +0.38 V | 0 of 288 | 10.1 mA | 2.15 V |
| 20 / 20 ms | +0.08 V | 0 of 288 | 10.1 mA | 1.82 V |
| 1 / 20 ms | -0.25 V | 24 of 288 | 10.1 mA | 1.84 V |
| 20 / 1 ms | -0.22 V | 24 of 288 | 10.1 mA | 1.88 V |

- **The firmware's own waveform holds everywhere.** 5 ms and 5 ms: 0.38 V of margin at the worst corner, which is `3V3` at 3.0 V. At 3.6 V it is 0.89 V.
- **Symmetric edges 20 ms apart hold, with little to spare.** 0.08 V at 3.0 V. The duty sweep below gives 0.22 V at 3.2 V, the lowest voltage U3 delivers.
- **The gate drive never failed to hold.** The LED current stays at 10.1 mA or more against the 5.6 mA the opto needs at -20 °C, and the reservoir of detector A stays 0.88 V above its release level of 0.94 V.
- **A lopsided waveform does not hold the relay: finding M6.** High for 1 ms and low for 20 ms, or the reverse, meets "an edge at least every 20 ms" and leaves Q2 short of gate voltage in 24 and 24 of 288 runs. All of them have `3V3` at 3.0 V and a Q2 at the top of its threshold range, 42 of the 48 at -20 °C and 6 at 70 °C. The cause is R89. With the pin's own resistance it charges C51 with a time constant of about 3 ms, so a 1 ms phase moves a quarter of the charge.

`results/duty.md` sweeps the two phases at that corner:

| Keep-alive, high / low | 3V3 at 3.0 V | 3V3 at 3.2 V, the lowest U3 delivers | 3V3 at 3.0 V, R89 220 ohm |
|---|---|---|---|
| 1 / 1 ms | +0.42 V, held | +0.60 V, held | +0.55 V, held |
| 2.5 / 2.5 ms | +0.41 V, held | +0.58 V, held | +0.53 V, held |
| 5 / 5 ms | +0.38 V, held | +0.55 V, held | +0.48 V, held |
| 10 / 10 ms | +0.29 V, held | +0.45 V, held | +0.35 V, held |
| 20 / 20 ms | +0.08 V, held | +0.22 V, held | +0.10 V, held |
| 1 / 20 ms | -0.25 V, **not held** | -0.13 V, **not held** | +0.13 V, held |
| 2 / 20 ms | -0.00 V, **not held** | +0.13 V, held | +0.22 V, held |
| 2.5 / 20 ms | +0.05 V, held | +0.19 V, held | +0.24 V, held |
| 3 / 20 ms | +0.09 V, held | +0.23 V, held | +0.24 V, held |
| 5 / 20 ms | +0.15 V, held | +0.30 V, held | +0.24 V, held |
| 20 / 1 ms | -0.22 V, **not held** | -0.10 V, **not held** | +0.18 V, held |
| 20 / 2 ms | +0.03 V, **not held** | +0.17 V, held | +0.28 V, held |
| 20 / 2.5 ms | +0.09 V, held | +0.23 V, held | +0.29 V, held |
| 20 / 3 ms | +0.12 V, held | +0.27 V, held | +0.30 V, held |
| 20 / 5 ms | +0.19 V, held | +0.34 V, held | +0.30 V, held |
| 1 / 9 ms | +0.10 V, held | +0.25 V, held | +0.36 V, held |
| 2.5 / 7.5 ms | +0.32 V, held | +0.48 V, held | +0.45 V, held |
| 2.5 / 17.5 ms | +0.10 V, held | +0.25 V, held | +0.28 V, held |
| 17.5 / 2.5 ms | +0.13 V, held | +0.28 V, held | +0.32 V, held |

So the contract needs one more line: **no phase shorter than 2.5 ms**. At
2.5 ms the margin is 0.05 V at 3.0 V and 0.19 V at 3.2 V. The
firmware's 5 ms toggle is not affected, and section 19.4 of the design
document has the line now. With R89 at 220 ohm the detector would hold even
the 1 ms phase, with 0.13 V. I did not make that change. A smaller R89
also puts more charge into the reservoir per edge, which moves the
single-edge and slow-toggling results of section 4.4, and it wants its own
pass through the fault table. It is the first thing I would try if the bench
shows less margin than the model.

Monte Carlo: 400 runs with both phases drawn between 1 and 20 ms. Q2
held in all of them, and the lowest gate margin was +0.16 V.

### 4.3 Release after the keep-alive stops

Seconds after the last edge. Valve current ends when the first of the two
layers lets go. The rows for detector A alone and detector B alone are what
one layer does if the other is stuck on, and they are also the two times
the firmware measures in the stop test. Runs that did not hold the relay
while toggling are left out.

| | Pin stops low | Pin high-impedance | Pin stops high |
|---|---|---|---|
| Valve current ends, grid | 0.03 to 0.84 | 0.03 to 0.84 | 0.05 to 1.25 |
| Detector A alone: gate-drive read-back low, tA, grid | 0.17 to 0.84 | 0.17 to 0.84 | 0.27 to 1.25 |
| Detector B alone: relay read-back high, tB, grid | 0.05 to 1.46 | 0.05 to 1.46 | 0.08 to 2.19 |
| Valve current ends, Monte Carlo | 0.09 to 0.68 | 0.11 to 0.68 | 0.29 to 1.09 |
| tA, Monte Carlo | 0.28 to 0.77 | 0.28 to 0.80 | 0.45 to 1.21 |
| tB, Monte Carlo | 0.14 to 0.93 | 0.15 to 0.96 | 0.35 to 1.27 |

- **No run left a valve on.** 1440 grid runs and 400 Monte Carlo runs.
- **Valve current ends within 0.84 s of the last edge, 1.25 s if the pin stops high.** The specification's "about one second" holds with both layers working.
- **Detector A alone meets the same figures.** It is the tighter of the two layers because it has a comparator.
- **The relay transistor alone is slower than the design document said: finding m10.** Up to 1.46 s, and 2.19 s if the pin stops high, against 0.95 and 1.4 s. The long corner is a Q2 with a 0.65 V threshold whose threshold falls by 4.5 mV/K, at 70 °C, with large capacitors and diodes that leak little. The author's figures assumed -3 mV/K. The data sheet gives no figure, so the bench has to: risk 28. It matters only when detector A is already stuck on, which the idle and stop tests report.
- **The earliest release** among the runs that held is 0.17 s for the gate drive and 0.05 s for the relay transistor. The second is two and a half times the 20 ms that the contract allows between edges. At the firmware's 5 ms edges the figures are 0.21 and 0.10 s.

### 4.4 Nothing enables drive without edges

264 runs with no toggling, over the same corners, each 6 s long.

| Keep-alive pin | Detector A enabled | Reservoir of A, highest (V) | Gate of Q2, highest (V) | Q2 above its lowest threshold |
|---|---|---|---|---|
| pin low from power-up | 0 of 24 runs | 0.02 | 0.00 | 0 of 24 runs |
| pin high from power-up | 0 of 24 runs | 1.63 | 1.58 | 12 of 24 runs |
| pin high-impedance from power-up | 0 of 24 runs | 0.02 | 0.00 | 0 of 24 runs |
| one rising edge, then high | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| one pulse of 60 us (a power-up glitch) | 0 of 24 runs | 0.14 | 0.03 | 0 of 24 runs |
| one pulse of 100 ms | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| 0.5 Hz | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| 1 Hz | 2 of 24 runs | 1.71 | 1.64 | 12 of 24 runs |
| 2 Hz | 9 of 24 runs | 1.98 | 1.85 | 12 of 24 runs |
| 3 Hz | 12 of 24 runs | 2.21 | 2.03 | 12 of 24 runs |
| 5 Hz | 13 of 24 runs | 2.45 | 2.29 | 16 of 24 runs |

- **A steady level never enables detector A**, from power-up or after one edge, in any corner. That is the claim that matters, and it holds.
- **One edge can turn Q2 on for a moment.** A single rising edge lifts the gate of Q2 to 1.58 V at most, which is above the threshold of a low-threshold part. Q2 alone closes nothing: Q1 needs the expander, and the expander is in reset because A is not enabled. The design document says the same.
- **One edge does not enable A, with less room than the document shows: note n4.** The reservoir of A reaches 1.64 V at the worst corner. The document's 1.00 V against 1.55 V is the nominal case.
- **The 60 us pulse in the table is a high pulse.** The power-up glitch of GPIO17 is a low level of that length, which is the idle level of the pin. The real pin makes no edge at all.
- **Slow toggling is not rejected: finding m9.** 1 Hz enables A in 2 of 24 corners, 2 Hz in 9, 5 Hz in 13. The design document had "1 Hz never" and "3 Hz and faster always". Neither holds over tolerances. The contract never relied on a lower limit, and it is right not to: only a steady level is guaranteed off.

### 4.5 What the firmware measures in the stop test

Step 9 times both read-backs after the stop. The limits as merged were 0.3
to 1.0 s for tA and 0.08 to 1.0 s for tB. At the firmware's 5 ms edges the
grid gives:

| | tA, gate-drive read-back | tB, relay read-back |
|---|---|---|
| Range over the grid, pin low or high-impedance | 0.21 to 0.83 s | 0.10 to 1.45 s |
| Outside the limits as merged | 16 of 192 runs | 18 of 192 runs |
| Outside the new absolute limits | 0 of 192 runs | 0 of 192 runs |

A healthy board at a tolerance corner would have failed the stop test, and
that failure tells the user the board needs repair. Finding M7.

The new absolute limits, 0.15 to 1.0 s and 0.05 to 1.6 s, contain every
corner. They are too wide to find an open bleed resistor, which doubles a
release time, because a board with small capacitors and one resistor open
looks like a board with large capacitors and both. So step 9 has a second
condition, a band of 0.5 to 1.5 times a value stored for the board at
commissioning. For one set of parts over -20 to 70 °C the grid moves tA
between 0.56 and 1.03 times its value at 27 °C, and tB between
0.55 and 1.38 times. In the fault injection an open R30 or R91
gives 1.56 s against 0.67 s, and an open R14 or R90 1.20 s
against 0.57 s. Both are found.

The band has little room at its low end and none of this is measured. The
times and the band are bring-up items, risks 28 and 30 and steps 4 and 10
of the bring-up plan.

Enabling is not critical. At 5 ms edges the gate-drive read-back is high
after 13 to 22 ms and the relay read-back low after 1 to 53 ms, against the
150 ms that step 3 allows.

### 4.6 Questions the firmware left for the hardware author

The firmware work that merged while this review ran lists questions for the
hardware author in section 20 of the firmware architecture. The simulations
and the data sheets answer five of them.

**1. Do the limits of step 9 hold after a short toggle?** After 0.3 s of
toggling, yes. After 0.1 s, no. `results/warmup.md` runs the keep-alive for
0.1 to 2 s with no valve and no relay, stops it and times both read-backs:

| Toggled for | typical parts, 27 C | latest release: 70 C, 3.6 V, large capacitors, Q2 at 0.65 V | earliest release: 70 C, 3.0 V, small capacitors, Q2 at 1.45 V, leaky diodes | slowest to charge: -20 C, 3.0 V, large reservoirs, small pump capacitors, Q2 at 1.45 V |
|---|---|---|---|---|
| 0.1 s | 0.66 / 0.51 | 0.81 / 1.35 | 0.22 / 0.12 | 0.61 / 0.08 |
| 0.15 s | 0.67 / 0.55 | 0.82 / 1.41 | 0.22 / 0.13 | 0.66 / 0.14 |
| 0.2 s | 0.67 / 0.57 | 0.82 / 1.43 | 0.22 / 0.13 | 0.67 / 0.17 |
| 0.3 s | 0.67 / 0.57 | 0.82 / 1.44 | 0.22 / 0.13 | 0.68 / 0.19 |
| 0.5 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |
| 1 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |
| 2 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |

After 0.3 s both times are within about 5 % of their final values in every
corner. After 0.1 s tB is 10 % short with typical parts and 60 % short at
the corner that charges slowest, which would fail the band of step 9. The
firmware's choice, at least 1 s of toggling before a stop test, is right.
0.3 s is the least I would allow.

**2. Is an acknowledge up to 200 ms after the first edge acceptable?** Yes.
The expander leaves reset when `DRV_EN` goes high, and TI gives the TCA9539
a reset recovery time of 0 ns. `V_DRV`, which GPIO42 reads, switches on from
the same gate one inversion earlier. An acknowledge at any time after
`A_RB` is high means the same thing.

**3. The pin-high test before every run, or at start-up and once a day?**
Before every run, with the float test. Step 2 is the only step that finds a
shorted C22 or C51. Each of those is covered by the other layer until it is
found, so once a day would be defensible for step 2 alone, although C22 and
C51 are the same part and can crack together. Step 2b is different. It is
the only step that finds a bridge of a kilohm onto the keep-alive net, and
that is the one fault in this review that defeats both layers, so nothing
covers it in the meantime. The two steps together take 1.7 s before a zone
opens, which costs an irrigation controller nothing.

**5. What does the fault output of U11 do while its enable is low?** It is
high-impedance, so bit 5 reads high through its pull-up. TI SLVS841E, in the
auto-retry description: "The part is disabled when EN is pulled low, and
FAULT goes high impedance". The data sheet says this of a part that has just
been disabled after a fault. It does not say it of a part that was never
enabled, and I read it the same way. Masking the bit, as the firmware does,
is the safe choice until a bench confirms it.

**7. Is the analogue reading of GPIO7 wanted for a Q2 shorted gate to
drain?** Yes. Q2 then sits at its own threshold, and GPIO7 sees 0.6 times
that. The pin reads low below 0.25 x VDD, 0.83 V, which is a threshold of
1.38 V. An AO3400A may have 1.45 V at 25 °C and more when cold, and then the
level is between the logic thresholds and the idle test may pass. In the
fault injection, with a typical part, the digital reading finds it. Read
ADC1 channel 6 in step 1 as well and take 0.3 to 1.5 V as this fault.

## 5. The sensor supply enable on GPIO45

**Verdict: safe. The assignment stays.**

GPIO45 is the strapping pin for the voltage of `VDD_SPI`, the supply of the
flash and the PSRAM inside the module. The author moved the sensor supply
enable onto it to free GPIO7 for the relay read-back.

| Question | Answer | Source |
|---|---|---|
| What the strap does | Pin low at reset: `VDD_SPI` is 3.3 V, taken from the 3.3 V supply. Pin high: 1.8 V from the internal regulator. Both only while eFuse `VDD_SPI_FORCE` is 0. | ESP32-S3 datasheet v2.2, table 3-4 |
| What the module needs | 3.3 V. The N16R8 holds an ESP32-S3R8, whose octal PSRAM is a 3.3 V part, and a 3.3 V quad flash. Only the R16V modules are 1.8 V. | Datasheet table 1-1; module datasheet v1.8, note 7 of table 1-2 |
| Pull state of the pin | Weak pull-down, input enabled, at reset and after reset | Datasheet table 2-1 |
| Power-up glitch | None listed for GPIO45 | Datasheet table 2-2 |
| When the strap is read | At reset release. The pin must hold for 3 ms after `EN` rises. | Datasheet table 3-2 |
| Does the strap matter on this module | **No.** "For modules with PSRAM, the VDD_SPI voltage is fixed to 3.3 V or 1.8 V via eFuse, so their VDD_SPI voltage will not be affected by the GPIO45 level." | Module datasheet v1.8, section 8, note on the schematic |

So there are two independent reasons, and either is enough.

- **The net cannot be high at reset.** It holds R86, 100 kohm to ground, the enable input of U11 and a test pad. The TPS2553 enable input is a logic input with no pull-up; TI gives its thresholds as 0.66 and 1.1 V and its absolute maximum as 7 V. With the internal pull-down the net has a time constant under a microsecond, and U12 releases `EN` 130 ms or more after `3V3` is good.
- **On the N16R8 the level is not used.** Espressif programs the eFuse on modules with PSRAM.

Three things I checked because they could have broken this.

- **A reset with the sensor supply on.** Firmware drives the pin high while a flow meter is powered. Every reset that samples the straps also resets the pin to an input with its pull-down. The pin is low within microseconds and the latch reads it milliseconds later.
- **A strap read as 1 on a module without the eFuse.** `VDD_SPI` would be 1.8 V. The flash and PSRAM are not damaged by a low supply. The module does not boot, makes no keep-alive edges, and the valves stay off. That is a board that does not start, not a valve that stays on.
- **What else could drive the net.** Only a failed U11 with its input joined to its enable pin. Section 6 has that case.

What I did not do is read the eFuses of a real module. The first step with
real modules is `espefuse.py summary`: `VDD_SPI_FORCE` must read 1 and
`VDD_SPI_TIEH` 1. It is in the bring-up plan now.

## 6. Regulator over-voltage, risk 21

**Verdict: was open, now covered by two parts. Fail-safe by calculation and
simulation.**

The owner's decision: if the switch of the buck converter fails short, the
board must fail safe and must not be destroyed silently.

### 6.1 The fault, simulated without protection

`hardware/sim/buck_overvoltage.cir` closes the high-side switch of U1 with
`VIN_DC` at 30 V and at 44.3 V. `V_BUCK` passes 7 V, the absolute maximum of
U11, after 14 and 11 us. It peaks at 42.7 and 63.4 V, above `VIN_DC`,
because L1 rings with C5 and C6. From there the 5 V and 3.3 V parts are
destroyed. The author's argument that K1 still opens rested on a dead
processor staying quiet, and he said he could not show that. I agree. With
the processor at ten times its supply rating nothing can be shown.

### 6.2 Why a crowbar onto the input fuses does not work

The brief suggested a clamp or crowbar sized to open the input fuses. I
checked that first and it fails on one resistor.

Every path from the transformer to `V_BUCK` runs through D2 and R1, 3 ohm.
With `V_BUCK` held near zero the rectifier delivers half-wave pulses. The
last block of the simulation holds the clamp on and measures them:

| Transformer | Open-circuit voltage | Current, rms | Of the 3 A fuses F1, F4 | Power in R1 |
|---|---|---|---|---|
| 20 VA | 24.2 V | 1.6 A | 54 % | 8.7 W |
| 20 VA | 32 V | 2.4 A | 79 % | 18 W |
| 40 VA | 27.5 V | 2.6 A | 85 % | 21 W |
| 75 VA | 27.5 V | 3.0 A | 100 % | 28 W |
| 75 VA | 32 V | 3.7 A | 122 % | 42 W |

The Littelfuse 452/454 data sheet gives these fuses 4 hours at 100 % and 1
to 60 s at 200 %. At 54 to 122 % they do not open in any useful time. R1 is
a 1 W part at 9 to 42 W. It would burn open, perhaps with D2, and that is
the uncontrolled destruction the owner ruled out. The same is true today of
a shorted C1, C2 or C3, which is finding m7.

So the part that opens has to be behind C1, where 470 uF at 30 V can deliver
tens of amps that the transformer cannot, and it has to be far smaller than
3 A.

### 6.3 The design: D25 and F5

- **D25**, Littelfuse SMBJ6.0A, a 600 W TVS from `V_BUCK` to `GND`, cathode to `V_BUCK`. LCSC C83270, 6788 in stock at JLCPCB on 2026-11-10. It is in the SMBJ series data sheet the BOM already cites: stand-off 6.0 V, breakdown 6.67 to 7.37 V at 10 mA, clamp 10.3 V at 58.3 A, leakage 800 uA at 6.0 V.
- **F5**, Littelfuse 0451.500MRL, 0.5 A very fast-acting, 125 V, in a 2410 body. LCSC C142836, 11,512 in stock. From the 451/453 data sheet: 0.42 ohm cold, nominal melting I²t 0.0824 A²s, interrupting rating 50 A at 125 V AC or DC and 300 A at 32 V DC.
- **A new net, `VIN_U1`**: F5 sits between `VIN_DC` and U1 pin 2. C2 and C3 move to `VIN_U1` with the pin. R7 and R9 stay on `VIN_DC`.

I chose a clamp and not a crowbar. A crowbar needs a thyristor, a trigger
with a tolerance tight enough to sit between 5.4 and 7 V, and still the
fuse. A TVS is one part, has no trigger to get wrong, and fails short when
it is overloaded, which is the safe direction here.

**Margins in normal use.**

| Check | Calculation | Result |
|---|---|---|
| D25 against the highest `V_BUCK` | 5.40 V against a breakdown of 6.67 V at 25 °C. The breakdown falls 0.046 %/K: 6.53 V at -20 °C. | 1.1 V of margin. Leakage under 0.8 mA. |
| F5 load | 5.0 W at the 17.3 V valley of low line | 0.29 A, 58 % of rating. Littelfuse asks for 75 % at most. Typical is 0.06 A. |
| F5 voltage drop | 0.42 ohm x 0.29 A | 0.12 V |
| F5 voltage | `VIN_DC` 44.3 V, 59 V in a clamped surge | 125 V rating |

**Why 0.5 A and no more.** The resistance of the fuse is part of the
design. With the switch welded and L1 saturated the current is limited by
what is left in the loop:

(44.3 V - 6.67 V) / (0.082 ohm of C1 + 0.42 ohm of F5 + 0.2 ohm of L1 + 0.03 ohm of D25 + 0.02 ohm of switch) = 50 A

That is the interrupting rating of F5 at 125 V, met at 44 V. The simulation
gives 46 to 48 A. A 1 A fuse of the same series has 0.153 ohm and would
pass 80 A. A 1206 thin-film fuse of 0.075 ohm, rated to break 50 A at 63 V,
would pass 90 A. Both would be asked to break more than they are rated for.

### 6.4 Simulation with the protection

48 cases. `VIN_DC` at 17, 30 and 44.3 V. The failed switch at 0.02 ohm
(welded), 0.725 ohm (the data sheet's on-resistance: stuck on) and 5 ohm
(damaged). L1 at 47 uH and at 5 uH for a saturated core. F5 opening at its
nominal melting I²t and at three times that, for tolerance and arc. A 20 VA
transformer for those, then 40 and 75 VA and both clamp limits.

| | Result |
|---|---|
| Highest `V_BUCK`, D25 at its highest clamp | 7.5 to 9.7 V |
| Highest `V_BUCK`, D25 at its lowest clamp | 7.1 to 8.1 V |
| Peak current | 2.2 to 48.5 A, against 58.3 A rated for D25 and 50 A for F5 |
| F5 opens after, welded switch, 30 V and above | 0.04 to 0.2 ms, 19 ms at three times the I²t on 30 V |
| F5 opens after, every other case | 0.15 to 200 ms |
| Energy in D25 | 0.015 to 0.70 J |
| D25 against its pulse curve, cases under 10 ms | The curve allows 4.5 to 11 times what D25 takes |
| Energy in R1 | 0.02 to 1.4 J |

The cases that take tens of milliseconds are the ones with little current: a
switch of several ohms, or `VIN_DC` at 17 V. The fuse model integrates I²t
with no heat loss, which is right for a millisecond and optimistic for
200 ms. The bound that does not need the model is the follow current of
section 6.2 expressed against F5: 323 to 734 % of its rating, where the data
sheet promises 5 s at most at 200 %. During those seconds D25 takes 5.5 to
15 W against a steady rating of 5 W. If it overheats it fails short, the
current rises and F5 opens. R1 takes 9 to 42 W and may be lost. `V_BUCK`
never exceeds the clamp voltage.

### 6.5 What the valves do

**While D25 clamps**, `V_BUCK` is at most 9.7 V and `V5` 9.4 V.

- **`3V3` stays regulated.** U3 is rated 20 V. The processor, the expander, U7, Q3 and `V_DRV` are inside their ratings and work. The fault-injection run with `V_BUCK` at 10.3 V shows both detectors releasing at their normal times.
- **U6**, on `V5`, is rated 36 V.
- **The relay coil** sees 9.7 V against a continuous limit of 8.5 V at 23 °C, for seconds at most. If firmware has K1 closed it stays closed. Firmware is alive and in control, so that is not a valve that stays on after the keep-alive stops.
- **The read-back divider** puts 53 uA into the clamp diode of GPIO7.

So the single-fault analysis of section 3 applies unchanged while the rail is
high. The regulator is the one fault, and both detectors are healthy.

**After F5 has opened**, `V_BUCK` is zero. The coil of K1 is fed from
`V_BUCK` and from nothing else, so K1 is open whatever the keep-alive does.
On 24 VAC alone the logic is off as well. With USB attached the logic runs
and K1 still cannot close.

**The one part over its rating is U11**, the sensor supply switch: 7.5 to
9.7 V on a 7 V absolute maximum, for 0.04 ms to 0.2 s and up to seconds in
the slow cases. It may be damaged. It cannot switch a valve. If it failed
with its input joined to its enable pin it would put up to 9.7 V on GPIO45
through no resistance. A 10 kohm resistor in `SENS_PWR_EN` would limit that
to 0.6 mA. I recommend it and did not fit it. It is finding m6.

### 6.6 Related faults

- **Feedback lost, R3 or R4 open.** U1 runs at full duty with its current limit of 1.75 A at most still working. D25 takes 7.4 V x 1.75 A = 13 W and fails short in seconds. F5 sees under 0.5 A and stays closed. U1 limits into the short. `V_BUCK` is zero and the board is dead. Fail-safe, and it costs D25.
- **C2 or C3 shorted.** These are now behind F5, which opens. Before, the current ran through R1 until R1 burned.
- **U3 shorted**, the same fault one stage on. Not covered. Finding M5.

### 6.7 How the fault shows

On 24 VAC alone the board is dark and no valve runs. With USB attached the
firmware can name the fault: mains is present on `AC_ZC`, `VIN_MON` is
normal, and the relay read-back on GPIO7 is low at rest, which happens only
when `V_BUCK` is missing. That is firmware rule 15 in the design document.
The repair is U1 and F5, and after a slow case D25 and R1.

## 7. Q3 shorted is found by no self-test

**Verdict: fixed with one connection.**

Q3 switches the opto LED supply. With Q3 shorted, `V_DRV` stays on when
detector A lets go, and only the expander reset removes LED current. The
author left it because no pin was free for a third read-back.

No new pin is needed. The read-back on GPIO42 read `DRV_EN`, the line that
resets the expander. I moved R92 so that it reads `V_DRV`, the node behind
Q3.

| What firmware must be able to see | Before | Now |
|---|---|---|
| Detector A and the latch have let go | `DRV_EN` low on GPIO42 | `V_DRV` low on GPIO42. `V_DRV` follows `DRV_EN_N`, the same gate one inversion earlier. |
| The expander is in reset | No acknowledge on I2C | No acknowledge on I2C |
| Q3 has turned off | Not visible | `V_DRV` low on GPIO42 |
| `DRV_EN` stuck high with `DRV_EN_N` working (U7 output 4, U5 reset pin) | `DRV_EN` high on GPIO42 | The expander answers on I2C with no keep-alive. The idle and stop tests already address it. |

The fault-injection results for the netlist as changed, appendix A:

- **Q3 shorted drain to source**: the idle test fails, gate-drive read-back high with no keep-alive. Before: not found.
- **Q3 gate open and leaking low**, which also holds it on: the same. Before: not found.
- **U7 output 4 stuck high**, the fault the old connection was best at: the stop test fails, because the expander still answers 3 s after the stop.
- **R92 open**: the start test fails on the safe side, as before.

One fault that the old connection found is missed by the new one, and it
is finding m13. With the supply pin of U7 open, the model powers U7 through
the clamp diodes of its inputs, from the pull-ups R36 and R53, and it keeps
working at about 2.7 V. `DRV_EN` then reaches 2.74 V, which gave 2.24 V at
GPIO42 through the old connection and failed the start test. `V_DRV` is
switched to the full rail by Q3 and reads as normal. Both detectors still
release at their normal times in the model, so no layer is lost. What a real
74HC132 does on a supply taken through its inputs is a bench question. I
kept the change, because a shorted Q3 removes half of a layer and this fault
removes none.

The same change corrects a margin. With 10 kohm and the pin's internal
pull-down, the high level at GPIO42 was 2.70 V against 0.75 x VDD = 2.48 V
needed. The pull-down is 45 kohm "typical" in the ESP32-S3 data sheet with
no limits. One third less and a healthy board would have failed the start
test. R92 is now 4.7 kohm, which gives 2.99 V, and the pull-down may be as
low as 14 kohm.

## 8. The output stage after the change to T810-600B-TR

**Verdict: the change is sound. Small loads are not worse than before. One
limit to write down for 50 mA relay coils.**

### 8.1 Pinout

The ST data sheet DocID7472 rev 12 for the T810, page 1, read as an image:
the DPAK has A2 on the tab, A1 on the left lead and G on the right lead with
the tab up and the leads towards the viewer. The ordering scheme on page 14
gives T810-600B-TR as 8 A, 10 mA, 600 V, package B = DPAK, tape and reel.
The netlist has pin 1 T1, pin 2 and tab T2, pin 3 gate. **Correct.**

### 8.2 Gate sensitivity, old triac against new

| | T410-600B, DocID7699 rev 9 | T810-600B, DocID7472 rev 12 |
|---|---|---|
| Gate trigger current, quadrants I to III, maximum at 25 °C | 10 mA | 10 mA |
| Gate trigger voltage, maximum | 1.3 V | 1.2 V |
| Holding current, maximum | 15 mA | 15 mA |
| Latching current, maximum, quadrants I and III | 25 mA | 25 mA |
| Latching current, quadrant II | 30 mA | 30 mA |
| Trigger current at -20 °C, from the curve | | 1.75 times: 17.5 mA |
| Holding and latching current at -20 °C | | 1.45 times: 22 and 36 mA |

The worry in the brief was that a larger triac needs more gate, holding or
latching current. For these two parts it does not. They are both "10" parts
of the same family and the limits are the same. The design document's
figures in section 6.4 are right.

The opto path has current to spare. The MOC3023 output carries 1 A peak.
The gate current is whatever the line voltage pushes through U101, R103 and
the gate, so a trigger current of 17.5 mA at -20 °C costs voltage, not
drive: 3.0 V of opto-triac, 1.5 V of gate and 1.9 V in R103 and R104, 6.4 V,
which is 12 degrees into the half-cycle at 21.6 VAC. The design document
says 6.5 V. The opto-triac itself fires at 5 mA of LED current, times 1.12
at -20 °C, and section 6.4 has 7.1 mA at the worst corner. I recalculated
that row and agree.

### 8.3 Latching and holding with small loads

Below the latching current the triac does not stay on by itself. The load
current then flows through U101, R103 and the gate, and the output drops
several volts. `hardware/sim/outstage_smallload.cir` models that drop with a
latch that sets at the latching current and clears at the holding current.
It is not a triac model, and the loads are assumed: 160 ohm and 1.44 H for a
50 mA relay coil, 60 ohm and 0.47 H for a 150 mA valve.

| Load | Supply | Triac | Voltage at the load | Lost | Latched for |
|---|---|---|---|---|---|
| 50 mA coil | 21.6 VAC | Limits at 25 °C | 20.8 V | 3.6 % | 77 % of the time |
| 50 mA coil | 21.6 VAC | Limits at -20 °C | 20.3 V | 5.9 % | 65 % |
| 50 mA coil | 24 VAC | Limits at -20 °C | 22.9 V | 4.7 % | 70 % |
| 150 mA valve | 21.6 VAC | Limits at -20 °C | 21.0 V | 2.7 % | 89 % |
| 150 mA valve | 24 VAC | Limits at -20 °C | 23.5 V | 2.2 % | 90 % |

- **A 150 mA valve works.** It loses under 3 % at the worst corner.
- **A 50 mA relay coil works with a condition.** It keeps 94 % of its voltage with a triac at every limit at -20 °C. A coil that pulls in at 80 % of 24 V has margin on the lowest supply. A coil that needs 85 %, 20.4 V, gets 20.3 V there and has none. A relay that has pulled in holds far below that.
- **Nothing to fix.** The loss is the same with the old triac. The design document offers a BTA204S-600D at Q901 for small coils. I would not fit it by default, because it gives up the short-circuit margin of section 6.6 on the one output that is most likely to drive a long cable to a pump house. The user documentation should say that a pump-start relay needs a coil that pulls in at 80 % or draws more than 100 mA.

### 8.4 The fault-current calculation of section 6.6

I redid the table. Every figure reproduces: 10.1, 18.7 and 30.9 A peak for
20, 40 and 75 VA with 27.5 V open circuit, 69 / VA ohm of transformer and
0.31 ohm of board.

Three inputs deserve more caution than the text gives them.

| Input | The document | What moves it | Effect at 75 VA |
|---|---|---|---|
| Open-circuit voltage | 27.5 V | The specification's own limit is 32 V unloaded | 36 A peak, 25.5 A rms |
| Transformer impedance | 12 % regulation | A stiffer unit of 6 %: 0.46 ohm, 28 V open circuit at high line | 50 A peak, 36 A rms. Close to the 40 A limit of the PTC. |
| PTC trip time | 0.5 s at 8 A, scaled with the inverse square of the current | The data sheet figure is for 25 °C. From -20 °C the PTC has 1.45 times as far to heat. | 0.10 s in place of 0.07 s |

- **Triac surge against the trip time.** At 30.9 A peak the PTC trips after 0.07 s, 4 cycles, 0.10 s when cold. Figure 6 of the triac data sheet allows 55 A for 4 cycles and about 50 A for 6. The margin in current is 1.8, or 1.6 cold. At 36 A peak it is 1.5.
- **Triac I²t.** The let-through is 21.9² x 0.07 = 34 A²s. The data sheet's 36 A²s is for one half-cycle of 10 ms and is the wrong figure to compare with over four cycles, where the junction has time to pass heat on. The surge curve is the right one, and it is what the document uses. I mention it because the two numbers look alarmingly close and are not.
- **A junction that starts hot.** Figure 6 is for 25 °C. The document says so and lists it for the bench. My estimate, scaling the allowed temperature rise from 100 °C in place of 25 °C: about 0.8 times the current, 45 A for 4 cycles. Still above 31 A.
- **The PTC rating.** 40 A maximum fault current at 60 V. The 75 VA case at 32 V is 25.5 A rms. A stiff 75 VA transformer would be near the limit, and a 100 VA one over it. The design envelope stops at 75 VA and should say that it assumes at least 10 % regulation.

None of this changes the conclusion. The output is shown to carry a dead
short on a 20 to 75 VA transformer until the PTC trips, by calculation, with
a margin of 1.5 to 3 in current. It stays risk 4 for the bench.

### 8.5 PTC F101

BHFUSE BSMD2920-100-60V, read from the maker's data sheet: 1.10 A hold,
2.20 A trip, 60 V, 40 A, 0.5 s to trip at 8 A, 0.065 ohm lowest, 0.41 ohm
after a trip. The derating table gives 0.64 A hold at 70 °C and 1.47 A at
-20 °C. The design document's figures match.

The trip current at 70 °C is about 1.3 A, under the 1.5 A inrush. The
document expects no trip in 100 ms. I agree, from the data sheet's own
point: 0.5 s at 8 A. At 1.5 A the heating power is (8 / 1.5)² = 28 times
lower, so with no heat lost at all the trip would take 14 s from 25 °C and
about 8 s from 70 °C. It is risk 9.

## 9. What this pull request changes

A schematic-capture engineer is working from `hardware/netlist.yaml`. This is
every reference designator and net that changed. No existing part was
renumbered. I checked the table against a comparison of the two netlists,
net by net.

| Item | Before | After | Finding |
|---|---|---|---|
| U4 pin 10, IO17 | `ENC_SW` | `KA` | B1 |
| U4 pin 31, IO38 | `KA` | `ENC_SW` | B1 |
| R33 | 10 kohm, C25804 | 2.2 kohm, C4190. Same nets, `KA` to `GND`. | M1 |
| R92 | 10 kohm, C25804, `DRV_EN` to `DRV_EN_RB` | 4.7 kohm, C23162, `V_DRV` to `V_DRV_RB` | M4, m1 |
| U4 pin 35, IO42 | `DRV_EN_RB` | `V_DRV_RB` | M4 |
| Net `DRV_EN_RB` | R92, U4 pin 35 | Renamed `V_DRV_RB`. Same two pins. | M4 |
| Net `DRV_EN` | U7 pin 11, U5 pin 3, R92, TP12 | U7 pin 11, U5 pin 3, TP12 | M4 |
| Net `V_DRV` | Q3, R26, C21, RN1, RN2, R96, R101 to R901, R102 to R902, TP7 | The same and R92 | M4 |
| F5, new | | 0.5 A very fast fuse, 2410, C142836, `VIN_DC` to `VIN_U1` | M2 |
| Net `VIN_U1`, new | | F5, U1 pin 2, C2 pin 1, C3 pin 1 | M2 |
| U1 pin 2, C2 pin 1, C3 pin 1 | `VIN_DC` | `VIN_U1` | M2 |
| D25, new | | SMBJ6.0A, SMB, C83270, anode `GND`, cathode `V_BUCK` | M2 |
| Net `VIN_DC` | C1, R1, R7, R9, TP1, C2, C3, U1 pin 2 | C1, R1, R7, R9, TP1, F5 pin 1 | M2 |
| Net `V_BUCK`, net `GND` | | Each gains one pin of D25 | M2 |
| Net `KA` | R29, R33, R89, U4 pin 31 | R29, R33, R89, U4 pin 10 | B1 |
| Net `ENC_SW` | C41, R71, U4 pin 10 | C41, R71, U4 pin 31 | B1 |

Files:

- **`hardware/netlist.yaml`**: the table above.
- **`hardware/pinmap.yaml`**: GPIO17 is `KEEPALIVE`, GPIO38 is `ENC_SW`. GPIO42 keeps its signal name `DRV_EN_RB`, which the firmware on `main` uses, and gets `net: V_DRV_RB`. The timing figures in the notes are the corner results of section 4.
- **`hardware/bom-preliminary.csv`**: two new lines, F5 and D25, both confirmed against the JLCPCB parts search on 2026-11-10. R33 and R92 moved to lines that existed. 98 lines, was 96.
- **`docs/design/hardware-design.md`**: new section 4.8. Sections 7.3, 8.4, 8.5, 8.6, 12.2, 12.3, 19, 20, 21 and 22 corrected. New section 25 lists the changes.
- **`docs/design/system-spec.md`**: v0.6. The logic supply row now asks for the clamp and the fuse, on the owner's decision.
- **`hardware/sim/`**: `failsafe/`, `buck_overvoltage.cir`, `outstage_smallload.cir`, and their sections in the README.

Firmware is not in this pull request. The firmware on `main` generates its
board header from the pin map at every build and uses signal names, so the
two moved pins need no firmware change. I ran the generator of `main`,
`firmware/zoneflow-fw/tools/gen_board_header.py`, on the changed pin map: it
accepts it and gives keep-alive 17, encoder switch 38, read-back 42. What
firmware does have to change is in section 19 of the design document:
steps 2b and 5b of the self-test, the limits of step 9, and rules 15 to
17. Section 19.5 lists them as open.

## 10. Proposed and not applied

| Finding | Change | Why not applied |
|---|---|---|
| M5 | An over-voltage detector on `3V3` that pulls `EN` low above about 3.6 V. A candidate from the JLCPCB listing is the TI TPS3700DDCR, a window comparator with open-drain outputs, C33002, 5461 in stock on 2026-11-10. I have read its listing and not its data sheet. | A new IC, a divider and a decision. It needs its own review. |
| m6 | 10 kohm in series with `SENS_PWR_EN`, between GPIO45 with R86 and the enable pin of U11 | One more part and net for a failure that needs two faults in a row. The owner may want it anyway. |
| M6 | R89 from 1 kohm to 220 ohm, so that detector B holds through a 1 ms phase | It moves the single-edge and slow-toggling results and wants its own pass through the fault table. The contract change covers the firmware's waveform. |
| m7 | Nothing. R1 will burn if C1 fails short. | The alternative is a fusible resistor that also meets the pulse rating of section 4.4. I did not find one and did not look long. |
| m12 | A line in the user documentation about pump-start relay coils | It belongs with the wiring page, which this review did not touch. |

## 11. What I verified and what I did not

**Opened and read for this review**, from the maker or LCSC's copy of the
maker's file:

- **Espressif**: ESP32-S3 Series Datasheet v2.2 and ESP32-S3-WROOM-1 module datasheet v1.8. Pin table 2-1, glitch table 2-2, flash and PSRAM pin mapping table 2-14, strapping pins section 3, DC characteristics table 5-4, the module pin table and the note on its schematic.
- **ST**: T810 DocID7472 rev 12 and T410 DocID7699 rev 9. The electrical tables as text; the package drawing and figures 6 and 8 as images.
- **Nexperia**: BAT54S, 1 July 2022, table 7 and figures 1 and 2 as an image. 74HC132, rev 8.
- **AOS**: AO3400A rev 3 with figures 1 and 2 as an image, AO3401A rev 3.1.
- **TI**: LM2901 SLCS006S, TCA9539 SCPS202C, LM5164 SNVSAU4, TPS2553.
- **Littelfuse**: SMBJ series (for SMBJ6.0A), figure 2 as an image. 451/453 series (for 0451.500MRL), the table as an image. 452/454 series. 466 series, which I rejected.
- **Others**: onsemi MOC3023M, Omron G5NB in English, BHFUSE BSMD2920, ST LDL1117, MDD SS34 and M7.

**Confirmed against a live listing**, JLCPCB parts search, 2026-11-10: C83270
(SMBJ6.0A, Littelfuse, DO-214AA, 6788 in stock), C142836 (0451.500MRL,
Littelfuse, 2410, 11,512), C23162 (4.7 kohm), C4190 (2.2 kohm).

**Simulated**, every file committed with its results:

- **`hardware/sim/failsafe/`**: the single-fault campaign, the corners and three sweeps. `results/faults.*`, `corners*`, `bridges.*`, `duty.*` and `warmup.*` are the netlist as changed. `results/asfound*` is the netlist of pull request #20.
- **`hardware/sim/buck_overvoltage.cir`**: section 6.
- **`hardware/sim/outstage_smallload.cir`**: section 8.3.
- **The author's files**, re-run: `relay_detector.cir`, `keepalive.cir` and `ka_bridge.cir` give the published numbers. The fault-injection model, run on the netlist of pull request #20 with typical parts, agrees with them where the two overlap. The gate of Q2 passes 1.45 V and 0.65 V at 0.31 and 0.70 s after the last edge, the same as `relay_detector.cir`. Detector A releases after 0.68 s against 0.73 s in `keepalive.cir`. It turns on 20.5 ms after the first edge against 13.6 ms: in both models the reservoir reaches the threshold at the second rising edge, give or take a few millivolts, and mine waits for the third. I also ran the four older files with R33 at its new value; `hardware/sim/README.md` has what moved.

**Recalculated**: the short-circuit table of section 6.6, the opto LED
current and firing voltage of section 6.4, the read-back levels, the fuse
sizing of section 6.

**Checks run on the repository**: `make -C hardware design-check` and
`uv run mkdocs build --strict`.

**Not verified. A bench has to show these.**

- **Anything the models assume.** Every semiconductor model is behavioural. The fault table says what the circuit as drawn does with idealised parts. Risk 23 of the design document repeats the stuck-on faults on hardware.
- **AO3400A threshold over temperature.** The data sheet has no curve. The corners sweep -1.5 to -4.5 mV/K. The release window of detector B rests on this more than on anything else.
- **Capacitance of C22, C23, C50 and C51 in circuit.** The ranges are an allowance. Samsung publishes bias curves that I did not obtain.
- **BAT54S leakage above the typical curve.** The data sheet gives typical curves at 85 and 125 °C and a limit only at 25 °C. Three times typical is my allowance.
- **The eFuses of a real N16R8 module**, section 5.
- **F5 and D25 in a real fault.** The fuse model has no arc and no heat loss. Short SW to VIN on a sacrificial board at 17, 30 and 44 V and record `V_BUCK`. Risk 21.
- **What an over-driven processor does**, findings M5 and m6.
- **The float test on real firmware.** Whether the display, the backlight and the PSRAM can be kept busy for 0.5 s with the keep-alive pin as an input is a firmware question.
- **Relay hold current.** The G5NB data sheet gives must-operate and must-release voltages. Where a relay actually lets go between 10 and 40 % of its rated voltage decides when K1 opens inside the window of section 4.
- **Small loads on a real triac**, section 8.3. Risk 7.
- **The transformer.** Its impedance and open-circuit voltage are estimates in every short-circuit figure.
- **Layout.** Every bridge fault in section 3 is a statement about what is next to what. Nothing is laid out yet. The rules are in section 8.5 of the design document.

## Appendix A. The complete fault table

The netlist as changed by this pull request. Written by
`hardware/sim/failsafe/failsafe.py faults`; the same table is in
`hardware/sim/failsafe/results/faults.md` and the figures behind it in
`faults.csv`.

How to read it:

- **Valve current ends after.** Seconds after the keep-alive pin stops low, stops high, or goes high-impedance, with zone 1 running and the fault appearing 0.5 s before the stop. "never on" means the fault kept the valve from running at all. The relay is taken to hold down to 3.6 mA and the triac to fire on 0.25 mA of LED current, so these are upper bounds for this model. They leave out the 10 ms release time of the relay and the half-cycle a triac finishes.
- **Layer lost.** What is still on 4 s after the stop: gate drive, Q1, Q2 or the relay. One layer lost is a fault that the other layer covers and a test must find. A valve that stays on needs gate drive and the relay.
- **Found while the valve runs.** The step of the self-test sequence that fails when the fault appears during a run: the checks of every control tick, the end of the run, or the stop test.
- **Found at the next start.** The first step that fails when the fault is there from power-up. Steps 2b and 5b are the ones this review adds.
- **A level in brackets** means a reading between the logic thresholds: the test may or may not fail.

The two columns on the right are for the self-test sequence of design section 19.7 as review A2 leaves it.

| Fault | Valve current ends after (s): pin low / high / high-impedance | Layer lost | Found while the valve runs | Found at the next start |
|---|---|---|---|---|
| **reference** | | | | |
| no fault | 0.50 / 0.76 / 0.50 |  |  |  |
| **detector A** | | | | |
| R33 open | 0.51 / 0.77 / 0.51 |  |  |  |
| R33 short | 0.00 / 0.00 / 0.00 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| R29 open | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R29 short | 0.50 / 0.76 / 0.50 |  |  |  |
| C22 open | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| C22 short | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on |  | 2 pin-high: gate-drive read-back high with the pin held high |
| D7 pin 1 A (GND) open | 0.34 / 0.52 / 0.34 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pin 2 K (KA_N2) open | 0.18 / 0.18 / 0.18 |  | 9 stop: gate-drive read-back releases after 0.22 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pin 3 common (KA_N1) open | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 1 A and 2 K bridged (GND to KA_N2) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 1 A and 3 common bridged (GND to KA_N1) | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| D7 pins 2 K and 3 common bridged (KA_N2 to KA_N1) | never on / never on / never on |  | 8 end: expander does not answer at the end of the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| C23 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 4 expander: valve pin Z1 does not read high as an input |
| C23 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R30 open | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back releases after 1.56 s, limits 0.15 to 1.0 s | 9 stop: gate-drive read-back releases after 1.56 s, limits 0.15 to 1.0 s |
| R30 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R91 open | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back releases after 1.56 s, limits 0.15 to 1.0 s | 9 stop: gate-drive read-back releases after 1.56 s, limits 0.15 to 1.0 s |
| R91 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R31 open | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R31 short | 0.45 / 0.68 / 0.45 |  |  |  |
| R32 open | 0.42 / 0.63 / 0.42 |  |  |  |
| R32 short | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| C24 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C24 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R34 open | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| R34 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R35 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R35 short | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| R36 open | 0.42 / 0.63 / 0.42 |  |  |  |
| R36 short | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| **logic** | | | | |
| U6 pin 1 1OUT (KA_OK) open | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U6 pin 2 2OUT (OC_TRIP_N) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 3 VCC (V5) open | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U6 pin 4 2IN- (OC_CMP) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 5 2IN+ (VH) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 6 1IN- (KA_REF) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 7 1IN+ (KA_INP) open | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U6 pin 8 3IN- (VL) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pin 9 3IN+ (OC_CMP) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 10 4IN- (VL) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 11 4IN+ (ZC_DIV) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 12 GND (GND) open *The model finds ground through the input diodes, about 0.9 V up, and keeps working. A real LM2901 without ground is not defined: its low level may not read as low at U7, and then detector A is stuck on. Detector B covers that.* | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 13 4OUT (AC_ZC) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pin 14 3OUT (OC_TRIP_N) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 1 1OUT and 2 2OUT bridged (KA_OK to OC_TRIP_N) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 2 2OUT and 3 VCC bridged (OC_TRIP_N to V5) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 3 VCC and 4 2IN- bridged (V5 to OC_CMP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 4 2IN- and 5 2IN+ bridged (OC_CMP to VH) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 5 2IN+ and 6 1IN- bridged (VH to KA_REF) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 6 1IN- and 7 1IN+ bridged (KA_REF to KA_INP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 8 3IN- and 9 3IN+ bridged (VL to OC_CMP) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 9 3IN+ and 10 4IN- bridged (OC_CMP to VL) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 pins 10 4IN- and 11 4IN+ bridged (VL to ZC_DIV) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 11 4IN+ and 12 GND bridged (ZC_DIV to GND) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 12 GND and 13 4OUT bridged (GND to AC_ZC) | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 pins 13 4OUT and 14 3OUT bridged (AC_ZC to OC_TRIP_N) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 1 (KA_OK) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 1 (KA_OK) never pulls low | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U6 output 2 (OC_TRIP_N) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 2 never pulls low | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 output 3 (OC_TRIP_N) shorted low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U6 output 3 never pulls low | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 output 4 (AC_ZC) shorted low | 0.50 / 0.76 / 0.50 |  |  |  |
| U6 output 4 never pulls low | 0.50 / 0.76 / 0.50 |  |  |  |
| C19 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C19 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| U7 pin 1 1A (OC_TRIP_N) open, holds its last level | 0.50 / 0.76 / 0.50 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 1 1A (OC_TRIP_N) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 1 1A (OC_TRIP_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 2 1B (FAULT_N) open, holds its last level | 0.50 / 0.76 / 0.50 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 2 1B (FAULT_N) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 2 1B (FAULT_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 3 1Y (FAULT) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 4 2A (KA_OK) open, holds its last level | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 4 2A (KA_OK) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 4 2A (KA_OK) open, leaks low | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 5 2B (FAULT) open, holds its last level | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 5 2B (FAULT) open, leaks high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 5 2B (FAULT) open, leaks low | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 6 2Y (FAULT_N) open | 0.50 / 0.76 / 0.50 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 7 GND (GND) open *As simulated. A real 74HC132 without ground is not defined.* | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| U7 pin 8 3Y (DRV_EN_N) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 9 3A (KA_OK) open, holds its last level | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 9 3A (KA_OK) open, leaks high | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pin 9 3A (KA_OK) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 10 3B (FAULT_N) open, holds its last level | 0.50 / 0.76 / 0.50 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 10 3B (FAULT_N) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 10 3B (FAULT_N) open, leaks low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pin 11 4Y (DRV_EN) open | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 3 start: within 150 ms no expander acknowledge |
| U7 pin 12 4A (DRV_EN_N) open, holds its last level | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 pin 12 4A (DRV_EN_N) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 12 4A (DRV_EN_N) open, leaks low | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 pin 13 4B (DRV_EN_N) open, holds its last level | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 pin 13 4B (DRV_EN_N) open, leaks high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pin 13 4B (DRV_EN_N) open, leaks low | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 pin 14 VCC (3V3) open *The model powers U7 through the clamp diodes of its inputs, from the pull-ups R36 and R53, at about 2.7 V, and it keeps working. A real 74HC132 on such a supply is a bench question.* | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pins 1 1A and 2 1B bridged (OC_TRIP_N to FAULT_N) | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 pins 2 1B and 3 1Y bridged (FAULT_N to FAULT) *A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 3 1Y and 4 2A bridged (FAULT to KA_OK) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 4 2A and 5 2B bridged (KA_OK to FAULT) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 5 2B and 6 2Y bridged (FAULT to FAULT_N) *As pins 2 and 3: the same two nets.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 6 2Y and 7 GND bridged (FAULT_N to GND) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 pins 8 3Y and 9 3A bridged (DRV_EN_N to KA_OK) *A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.* | never on / never on / never on (run not finished: T0 Tm Cm-low Cm-high Cm-hiz) |  |  |  |
| U7 pins 9 3A and 10 3B bridged (KA_OK to FAULT_N) *The gate output and the comparator output fight over the node, and which one wins at power-up depends on the two parts. Either way detector A stays on once it has been enabled.* | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| U7 pins 10 3B and 11 4Y bridged (FAULT_N to DRV_EN) | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 pins 11 4Y and 12 4A bridged (DRV_EN to DRV_EN_N) | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 pins 13 4B and 14 VCC bridged (DRV_EN_N to 3V3) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 1 (FAULT) stuck high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 1 (FAULT) stuck low | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 output 2 (FAULT_N) stuck high | 0.50 / 0.76 / 0.50 |  |  |  |
| U7 output 2 (FAULT_N) stuck low | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 3 (DRV_EN_N) stuck high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| U7 output 3 (DRV_EN_N) stuck low | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| U7 output 4 (DRV_EN) stuck high | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U7 output 4 (DRV_EN) stuck low | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| C20 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C20 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| R53 open | 0.50 / 0.76 / 0.50 |  |  | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R53 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R87 open | 0.50 / 0.76 / 0.50 |  |  | (7 run: level not defined) |
| R87 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R54 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R54 short | 0.50 / 0.76 / 0.50 |  |  |  |
| D8 open | 0.50 / 0.76 / 0.50 |  |  |  |
| D8 short | 0.50 / 0.76 / 0.50 |  |  |  |
| **LED supply switch** | | | | |
| Q3 pin G (DRV_EN_N) open, holds its last level | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 3 start: within 150 ms no gate-drive read-back high |
| Q3 pin G (DRV_EN_N) open, leaks high | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| Q3 pin G (DRV_EN_N) open, leaks low | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| Q3 pin S (3V3) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| Q3 pin D (V_DRV) open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| Q3 pins G and S bridged (DRV_EN_N to 3V3) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| Q3 pins G and D bridged (DRV_EN_N to V_DRV) | never on / never on / never on |  | valve current: no valve current during the run | 1 idle: gate-drive read-back high with no keep-alive |
| Q3 pins S and D bridged (3V3 to V_DRV) | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| R25 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R25 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R26 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R26 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| C21 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C21 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| **read-back** | | | | |
| R92 open | 0.50 / 0.76 / 0.50 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| R92 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R93 open *The read-back loses its pull-up and reads low for good: a false alarm on the safe side. Q2 itself works.* | 0.50 / 0.77 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| R93 short *V_BUCK is then on the drain of Q2 with no coil in between, and Q2 shorts the supply as soon as it conducts.* | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 5 weld check: valve pin does not read low when driven low |
| R94 open | 0.50 / 0.76 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| R94 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R95 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R95 short | 0.50 / 0.76 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| **expander** | | | | |
| U5 pin 3 RESET (DRV_EN) open, holds its last level | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 3 start: within 150 ms no expander acknowledge |
| U5 pin 3 RESET (DRV_EN) open, leaks high | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U5 pin 3 RESET (DRV_EN) open, leaks low | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| U5 pin 4 P00 (ZD_Z1_N) open | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| U5 pin 8 P04 (ZD_MV_N) open | 0.50 / 0.76 / 0.50 |  |  |  |
| U5 pin 9 P05 (RLY_ON) open | never on / never on / never on |  | 7 run: RLY_ON not high during the run | 6 relay: RLY_ON does not read high after it is set |
| U5 pin 12 GND (GND) open *As simulated. A real TCA9539 without ground is not defined.* | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| U5 pin 24 VCC (3V3) open | 0.50 / 0.76 / 0.50 |  | 8 end: expander does not answer at the end of the run | 3 start: within 150 ms no expander acknowledge |
| U5 pins 2 A1 and 3 RESET bridged (GND to DRV_EN) | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| U5 pins 3 RESET and 4 P00 bridged (DRV_EN to ZD_Z1_N) | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| U5 pins 8 P04 and 9 P05 bridged (ZD_MV_N to RLY_ON) | 0.50 / 0.76 / 0.50 |  |  | 4 expander: RLY_ON does not read low as an input |
| U5 other adjacent pins bridged: 1 INT-2 A1, 4 P00-5 P01, 5 P01-6 P02, 6 P02-7 P03, 7 P03-8 P04, 9 P05-10 P06, 10 P06-11 P07, 11 P07-12 GND, 13 P10-14 P11, 14 P11-15 P12, 15 P12-16 P13, 16 P13-17 P14, 17 P14-18 P15, 18 P15-19 P16, 19 P16-20 P17, 20 P17-21 A0, 21 A0-22 SCL, 22 SCL-23 SDA, 23 SDA-24 VCC | not simulated | | | Not simulated: these pins are valve bits of other channels, spare pins, the address straps and the I2C bus. Two valve pins bridged make two outputs follow one bit. A stuck bus stops the firmware from writing, and it must then stop the keep-alive. None of these pins can hold RESET high or drive a valve pin low while the device is in reset. |
| U5 RESET input ignored, bits kept | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| U5 pin P00 (Z1) stuck low | 0.50 / 0.76 / 0.50 |  |  | 4 expander: valve pin Z1 does not read high as an input |
| U5 pin P00 (Z1) stuck high | 0.50 / 0.76 / 0.50 |  |  | (5 weld check: level not defined) |
| U5 pin P04 (MV) stuck low | 0.50 / 0.76 / 0.50 |  |  | 4 expander: valve pin MV does not read high as an input |
| U5 pin P05 (RLY_ON) stuck high | 0.50 / 0.76 / 0.50 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| U5 pin P05 (RLY_ON) stuck low | 0.50 / 0.76 / 0.50 |  |  | (6 relay: level not defined) |
| C18 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C18 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| RN1 element 1-8 (ZD_Z1_N) open | 0.50 / 0.76 / 0.50 |  |  |  |
| RN1 element 1-8 (ZD_Z1_N) short | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| RN1 elements 2 to 4 open or short, pins 1-2, 2-3, 3-4 bridged | not simulated | | | The other three elements pull the pins of Z2 to Z4 up in the same way. A bridge between two valve pins makes two outputs follow one bit; it adds no path that keeps a pin low after the expander is reset. |
| R96 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R96 short | 0.50 / 0.76 / 0.50 |  |  |  |
| **output LEDs (Z1 and MV)** | | | | |
| R101 open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| R101 short | 0.50 / 0.76 / 0.50 |  |  | (5 weld check: level not defined) |
| U101 LED open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| U101 LED short | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| R102 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R102 short | 0.50 / 0.76 / 0.50 |  |  | (5 weld check: level not defined) |
| D102 open | 0.50 / 0.76 / 0.50 |  |  |  |
| D102 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R901 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R901 short | 0.50 / 0.76 / 0.50 |  |  |  |
| U901 LED open | 0.50 / 0.76 / 0.50 |  |  |  |
| U901 LED short | 0.50 / 0.76 / 0.50 |  |  |  |
| R902 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R902 short | 0.50 / 0.76 / 0.50 |  |  |  |
| D902 open | 0.50 / 0.76 / 0.50 |  |  |  |
| D902 short | 0.50 / 0.76 / 0.50 |  |  |  |
| **relay drive** | | | | |
| K1 coil open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| K1 coil short *Q1 and Q2 then short V_BUCK whenever both conduct. The supply collapses, the supervisor resets the processor and the expander loses its bits. K1 cannot close with no voltage across its coil.* | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| K1 contact welded, or JP1 bridged | 0.67 / 1.01 / 0.67 | relay contact closed |  | 5 weld check: current flows with RLY_ON low |
| D5 open *Nothing in the model. On the board Q1 or Q2 would avalanche at every release, 0.16 mJ by the design document's estimate, and one of them may fail short in time. That fault has its own row.* | 0.50 / 0.76 / 0.50 |  |  |  |
| D5 short *The same as a shorted coil.* | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin G (RLY_G1) open, holds its last level | 0.50 / 0.77 / 0.50 |  |  | valve current: no valve current during the run |
| Q1 pin G (RLY_G1) open, leaks high | 0.50 / 0.76 / 0.50 |  |  | 5 weld check: current flows with RLY_ON low |
| Q1 pin G (RLY_G1) open, leaks low | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin S (RLY_B) open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pin D (RLY_A) open | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pins G and S bridged (RLY_G1 to RLY_B) | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q1 pins G and D bridged (RLY_G1 to RLY_A) | 0.51 / 0.78 / 0.51 |  |  | 5 weld check: current flows with RLY_ON low |
| Q1 pins S and D bridged (RLY_B to RLY_A) *Q1 conducts whatever its gate does. K1 then follows Q2 alone.* | 0.50 / 0.77 / 0.50 |  |  | 5 weld check: current flows with RLY_ON low |
| R11 open | never on / never on / never on |  | valve current: no valve current during the run | 4 expander: RLY_ON does not read low as an input |
| R11 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R12 open | 0.50 / 0.76 / 0.50 | Q1 stays on |  | 5b gate bleed: current flows after RLY_ON was released without being driven low |
| R12 short | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| Q2 pin G (RLY_G2) open, holds its last level | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 3 start: within 150 ms no relay read-back low |
| Q2 pin G (RLY_G2) open, leaks high | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| Q2 pin G (RLY_G2) open, leaks low | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pin S (GND) open | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pin D (RLY_B) open | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pins G and S bridged (RLY_G2 to GND) | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| Q2 pins G and D bridged (RLY_G2 to RLY_B) | 0.66 / 1.00 / 0.66 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| Q2 pins S and D bridged (GND to RLY_B) | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| **detector B** | | | | |
| R89 open | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| R89 short | 0.53 / 0.80 / 0.53 |  |  |  |
| C51 open | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| C51 short | 0.55 / 1.01 / 0.55 | Q2 stays on |  | 2 pin-high: relay read-back not high with the pin held high |
| D22 pin 1 A (GND) open | 0.18 / 0.27 / 0.18 |  | 9 stop: relay read-back releases after 0.28 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| D22 pin 2 K (RLY_G2) open | 0.01 / 0.01 / 0.01 |  | 9 stop: relay read-back releases after 0.12 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| D22 pin 3 common (KB_N1) open | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| D22 pins 1 A and 2 K bridged (GND to RLY_G2) | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| D22 pins 1 A and 3 common bridged (GND to KB_N1) | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| D22 pins 2 K and 3 common bridged (RLY_G2 to KB_N1) | 0.00 / 0.39 / 0.00 |  | 9 stop: relay read-back releases after 0.00 s, limits 0.05 to 1.6 s | 3 start: within 150 ms no relay read-back low |
| R14 open | 0.67 / 1.01 / 0.67 |  | 9 stop: relay read-back releases after 1.20 s, this board's own value is 0.57 s | 9 stop: relay read-back releases after 1.20 s, this board's own value is 0.57 s |
| R14 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| R90 open | 0.67 / 1.01 / 0.67 |  | 9 stop: relay read-back releases after 1.20 s, this board's own value is 0.57 s | 9 stop: relay read-back releases after 1.20 s, this board's own value is 0.57 s |
| R90 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| C50 open | never on / 0.26 / never on |  | 7 run: relay read-back high during the run | 7 run: relay read-back high during the run |
| C50 short | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| **thresholds and zero-cross (share U6)** | | | | |
| R48 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R48 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R49 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R49 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R50 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R50 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R51 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R51 short | 0.50 / 0.76 / 0.50 |  |  |  |
| C32 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C32 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R52 open | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R52 short | 0.50 / 0.76 / 0.50 |  |  |  |
| C33 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C33 short | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| R55 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R55 short *Puts 24 VAC on 3V3 through the clamp diode D9. Every logic part is then outside its ratings and the row means nothing. A 1206 resistor rated 200 V does not fail short; a solder bridge across it would do this.* | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 2 pin-high: relay read-back not high with the pin held high |
| R56 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R56 short | 0.50 / 0.76 / 0.50 |  |  |  |
| C34 open | 0.50 / 0.76 / 0.50 |  |  |  |
| C34 short | 0.50 / 0.76 / 0.50 |  |  |  |
| D9 pin 1 A (GND) open | 0.50 / 0.76 / 0.50 |  |  |  |
| D9 pin 2 K (3V3) open | 0.50 / 0.76 / 0.50 |  |  |  |
| D9 pin 3 common (ZC_CLAMP) open | 0.50 / 0.76 / 0.50 |  |  |  |
| D9 pins 1 A and 2 K bridged (GND to 3V3) | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| D9 pins 1 A and 3 common bridged (GND to ZC_CLAMP) | 0.50 / 0.76 / 0.50 |  |  |  |
| D9 pins 2 K and 3 common bridged (3V3 to ZC_CLAMP) | 0.50 / 0.76 / 0.50 |  |  |  |
| R88 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R88 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R57 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R57 short | 0.50 / 0.76 / 0.50 |  |  |  |
| R58 open | 0.50 / 0.76 / 0.50 |  |  |  |
| R58 short | 0.50 / 0.76 / 0.50 |  |  |  |
| **output devices** | | | | |
| triac, TVS, opto-triac output or snubber capacitor shorted | 0.50 / 0.76 / 0.50 |  |  | 6 relay: current flows with every valve bit high |
| **module** | | | | |
| U4 pin 10 (KA) not connected to the board | 0.00 / 0.00 / 0.00 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| GPIO of the gate-drive read-back driven high as an output by mistake | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| GPIO of the gate-drive read-back driven low as an output by mistake | 0.50 / 0.76 / 0.50 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| U4 pin 35 (V_DRV_RB) not connected to the board | 0.50 / 0.76 / 0.50 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| GPIO of the relay read-back driven high as an output by mistake | 0.50 / 0.76 / 0.50 |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| GPIO of the relay read-back driven low as an output by mistake | 0.50 / 0.76 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| U4 pin 7 (RLY_B_RB) not connected to the board | 0.50 / 0.76 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| GPIO of the over-current flag driven high as an output by mistake | 0.50 / 0.76 / 0.50 |  |  |  |
| GPIO of the over-current flag driven low as an output by mistake | 0.50 / 0.76 / 0.50 |  | 7 run: over-current flag low during the run | 7 run: over-current flag low during the run |
| U4 pin 33 (OC_FAULT_N) not connected to the board | 0.50 / 0.76 / 0.50 |  |  | (7 run: level not defined) |
| KA bridged to module pin 9 IO16 (ENC_B, contact open) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 9 IO16 (ENC_B, contact worked at 20 Hz) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 11 IO18 (BTN_BACK, contact open) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 11 IO18 (BTN_BACK, contact worked at 20 Hz) | 0.50 / 0.76 / 0.50 |  |  |  |
| **board** | | | | |
| KA bridged to AC_ZC anywhere on the board | 0.48 / 0.74 / 0.48 |  |  |  |
| KA bridged to TFT_BL (backlight PWM, keeps running), 0.01 ohm | **stays on** / **stays on** / **stays on** | gate drive stays, relay stays closed | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| KA bridged to TFT_BL (backlight PWM, keeps running), 1000 ohm | 0.48 / 0.74 / **stays on** | gate drive stays, relay stays closed |  | 2b float: gate-drive read-back high with the pin an input |
| KA bridged to U0TXD (ROM boot log, prints in a reset loop) | **stays on** / **stays on** / **stays on** | gate drive stays, relay stays closed | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net AC_ZC shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net AC_ZC shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net DRV_EN shorted to GND | never on / never on / never on |  | 7 run: expander stops answering during the run | 3 start: within 150 ms no expander acknowledge |
| net DRV_EN shorted to 3V3 | 0.50 / 0.76 / 0.50 | Q1 stays on | 9 stop: expander still answers 3 s after the stop | 1 idle: expander answers with no keep-alive |
| net DRV_EN_N shorted to GND | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net DRV_EN_N shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net FAULT shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net FAULT shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net FAULT_N shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net FAULT_N shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net ISENSE_LO shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net ISENSE_LO shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA shorted to GND | 0.00 / 0.00 / 0.00 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| net KA shorted to 3V3 | 0.27 / 0.27 / 0.27 |  |  | 2b float: keep-alive pin reads high while it is an input |
| net KA_INP shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_INP shorted to 3V3 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_N0 shorted to GND | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N0 shorted to 3V3 | 0.43 / 0.51 / 0.51 |  |  | 2b float: keep-alive pin reads high while it is an input |
| net KA_N1 shorted to GND | 0.17 / 0.17 / 0.17 |  | 9 stop: gate-drive read-back releases after 0.21 s, this board's own value is 0.67 s | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N1 shorted to 3V3 | 0.48 / 0.74 / 0.48 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_N2 shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_N2 shorted to 3V3 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_OK shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KA_OK shorted to 3V3 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_REF shorted to GND | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net KA_REF shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net KB_N0 shorted to GND | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| net KB_N0 shorted to 3V3 | 0.27 / 0.27 / 0.27 |  |  | 3 start: within 150 ms no relay read-back low |
| net KB_N1 shorted to GND | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.11 s, this board's own value is 0.57 s | 3 start: within 150 ms no relay read-back low |
| net KB_N1 shorted to 3V3 | 0.66 / 0.99 / 0.66 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net LEDA_MV shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net LEDA_MV shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net LEDA_Z1 shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net LEDA_Z1 shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net LED_FLT shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net LED_FLT shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net OC_CMP shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_CMP shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_FAULT_N shorted to GND | 0.50 / 0.76 / 0.50 |  | 7 run: over-current flag low during the run | 7 run: over-current flag low during the run |
| net OC_FAULT_N shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net OC_TRIP_N shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net OC_TRIP_N shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net OLA_MV shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net OLA_MV shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net OLA_Z1 shorted to GND | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| net OLA_Z1 shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net RLY_A shorted to GND | 0.67 / 1.01 / 0.67 | relay stays closed | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_A shorted to 3V3 *The coil then sees V_BUCK less 3V3, about 2 V, whatever Q1 and Q2 do. That cannot pull the relay in and may hold it: Omron gives 40 % of the rated voltage as a holding figure. With Q1 and Q2 on, the bridge also shorts 3V3 to ground, and the supervisor resets the processor.* | 0.14 / 0.14 / 0.14 | relay stays closed | 9 stop: gate-drive read-back releases after 0.18 s, this board's own value is 0.67 s | valve current: no valve current during the run |
| net RLY_B shorted to GND | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_B shorted to 3V3 *With Q2 on this shorts 3V3 to ground, and the supervisor resets the processor.* | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_B_RB shorted to GND | 0.50 / 0.76 / 0.50 |  | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_B_RB shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_G1 shorted to GND | never on / never on / never on |  | valve current: no valve current during the run | valve current: no valve current during the run |
| net RLY_G1 shorted to 3V3 | 0.50 / 0.76 / 0.50 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| net RLY_G2 shorted to GND | never on / never on / never on |  | 7 run: relay read-back high during the run | 3 start: within 150 ms no relay read-back low |
| net RLY_G2 shorted to 3V3 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| net RLY_ON shorted to GND | never on / never on / never on |  | 7 run: RLY_ON not high during the run | 6 relay: RLY_ON does not read high after it is set |
| net RLY_ON shorted to 3V3 | 0.50 / 0.76 / 0.50 | Q1 stays on |  | 4 expander: RLY_ON does not read low as an input |
| net VB shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VB shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VH shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net VH shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net VL shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net VL shorted to 3V3 | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no expander acknowledge |
| net V_DRV shorted to GND | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| net V_DRV shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net V_DRV_RB shorted to GND | 0.50 / 0.76 / 0.50 |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high |
| net V_DRV_RB shorted to 3V3 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| net ZC_CLAMP shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net ZC_CLAMP shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net ZC_DIV shorted to GND | 0.50 / 0.76 / 0.50 |  |  |  |
| net ZC_DIV shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net ZD_MV_N shorted to GND | 0.50 / 0.76 / 0.50 |  | 7 run: valve pin MV not high during the run | 4 expander: valve pin MV does not read high as an input |
| net ZD_MV_N shorted to 3V3 | 0.50 / 0.76 / 0.50 |  |  |  |
| net ZD_Z1_N shorted to GND | 0.50 / 0.76 / 0.50 |  |  | 4 expander: valve pin Z1 does not read high as an input |
| net ZD_Z1_N shorted to 3V3 | never on / never on / never on |  | 7 run: valve pin Z1 not low during the run | 5 weld check: valve pin does not read low when driven low |
| leak of 47 kohm from 3V3 to KA_N2 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 2 pin-high: gate-drive read-back high with the pin held high |
| leak of 100 kohm from 3V3 to KA_N2 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| leak of 220 kohm from 3V3 to KA_N2 | 0.50 / 0.76 / 0.50 |  | 9 stop: gate-drive read-back releases after 1.19 s, limits 0.15 to 1.0 s | 9 stop: gate-drive read-back releases after 1.19 s, limits 0.15 to 1.0 s |
| leak of 100 kohm from 3V3 to KA_INP | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 1 idle: gate-drive read-back high with no keep-alive |
| leak of 330 kohm from 3V3 to KA_INP | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 2 pin-high: gate-drive read-back high with the pin held high |
| leak of 47 kohm from 3V3 to RLY_G2 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| leak of 100 kohm from 3V3 to RLY_G2 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 2 pin-high: relay read-back not high with the pin held high |
| leak of 330 kohm from 3V3 to RLY_G2 | 0.62 / 0.94 / 0.62 |  |  |  |
| leak of 100 kohm from V_BUCK to RLY_G2 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 1 idle: relay read-back not high with no keep-alive |
| leak of 330 kohm from V_BUCK to RLY_G2 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back releases after 1.13 s, this board's own value is 0.57 s | 9 stop: relay read-back releases after 1.13 s, this board's own value is 0.57 s |
| leak of 1000 kohm from V_BUCK to RLY_G2 | 0.58 / 0.87 / 0.58 |  |  |  |
| leak of 100 kohm from 3V3 to KB_N1 | 0.67 / 1.01 / 0.67 | Q2 stays on | 9 stop: relay read-back does not release within 3 s | 2 pin-high: relay read-back not high with the pin held high |
| leak of 100 kohm from 3V3 to KA_N1 | 0.50 / 0.76 / 0.50 | gate drive stays, Q1 stays on | 9 stop: gate-drive read-back does not release within 3 s | 9 stop: gate-drive read-back does not release within 3 s |
| **supplies** | | | | |
| U3 shorted input to output: 3V3 at V5, 4.95 V. The processor is taken to keep running until the stop *Both detectors let go at 4.95 V, if the keep-alive stops. The processor is outside its ratings and whether it stops is not known: finding M5.* | 0.69 / 1.03 / 0.69 |  | 9 stop: relay read-back does not release within 3 s | 9 stop: relay read-back does not release within 3 s |
| 3V3 falls to 2.0 V: the supervisor holds the processor in reset | 0.00 / 0.00 / 0.00 |  | 9 stop: relay read-back releases after 0.09 s, this board's own value is 0.57 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| 3V3 falls to 0 V | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| V_BUCK falls to 3.0 V, V5 and 3V3 with it | 0.01 / 0.01 / 0.01 |  | 9 stop: relay read-back releases after 0.18 s, this board's own value is 0.57 s | 3 start: within 150 ms no gate-drive read-back high, no relay read-back low, no expander acknowledge |
| V_BUCK falls to 0 V on 24 VAC alone: every rail goes | never on / never on / never on |  | 7 run: gate-drive read-back low during the run | 1 idle: relay read-back not high with no keep-alive |
| V_BUCK falls to 0 V with USB attached: the logic stays up (F5 has opened, section 4.8) | never on / never on / never on |  | valve current: no valve current during the run | 1 idle: relay read-back not high with no keep-alive |
| V_BUCK at 10.3 V, V5 at 10 V: the highest the clamp D25 allows | 0.50 / 0.76 / 0.50 |  |  |  |
