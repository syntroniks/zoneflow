# ZoneFlow hardware review A1

Status: **independent review of the rev A paper design, 2026-11-10**. It
covers [hardware design](hardware-design.md), `hardware/netlist.yaml`,
`hardware/pinmap.yaml`, `hardware/bom-preliminary.csv` and `hardware/sim/` as
they stood in pull request #20, against [system specification](system-spec.md)
v0.4 and the [firmware architecture](firmware-architecture.md) on `main`. It
is tracked in [issue #11](https://github.com/syntroniks/zoneflow/issues/11).

No board exists and I had no bench. Every statement here comes from a
manufacturer data sheet that I opened, a calculation from its figures, or an
ngspice run in `hardware/sim/`. Section 12 says which is which and what I
could not check.

## 1. Verdict

**The design is not ready for schematic capture as it stands.** One finding is
blocking and four more need a decision from the owner before the netlist is
final. None of them changes the architecture. Each is a handful of parts.

- **Blocking, B1.** The keep-alive detector is a single chain. Five single component faults in it leave gate drive, the expander and the relay enabled with no working keep-alive, and nothing shows it in normal use. Section 3 has the faults and a proposed fix of four extra parts.
- **Decisions needed.** A second fuse (M2), how the relay releases (M3), a pulse-rated inrush resistor (M6) and a surge clamp on the valve common (M7).
- **Already fixed in this pull request.** Two circuit corrections, each one resistor (M4, M5), and the document, pin map and simulation corrections listed in section 2.

What I found sound: every pinout I could check against a package drawing,
including the T410 that the author had to assume. The part ratings in the
power and protection paths. The buck converter design against the TI
procedure. The opto-triac drive and the reasoning for it. The ESP32-S3 pin
assignment, including the status LED on GPIO46. The author's own list of what
was unconfirmed was accurate, and most of it is now settled.

Counts: 1 blocking, 7 major, 16 minor, 6 notes.

## 2. Findings

Severity: **blocking** must be resolved before capture. **Major** needs a
decision or a bench test before boards are ordered. **Minor** should be
corrected and can be done during capture. **Note** needs no change to the
design.

"Fixed" means this pull request changes the design files. "Recommended" means
it does not, because the choice belongs to the owner.

| ID | Severity | Block | What is wrong | Evidence | Fix |
|---|---|---|---|---|---|
| B1 | Blocking | Fail-safe gate | Five single faults leave `DRV_EN` high with no working keep-alive: R30 open, R31 opening while enabled, R34 open, C22 short with the pin high, U6A or U7 output stuck. The LED supply, the expander reset and the relay all depend on that one signal. The design tested for this at start-up only. | `hardware/sim/keepalive_faults.cir`, FAULT lines. Section 3. | Document corrected: section 8.5 lists every part, and the self-test now runs before every run. Circuit change recommended, not applied: section 3. |
| M1 | Major | Output stage, over-current | The text said the triac stops at the first current zero after a short. Its data sheet guarantees that only up to 8.6 A peak at 50 Hz and 7.2 A at 60 Hz. A 40 VA transformer shorted at the terminals gives 19 A peak. "A shorted wire does not destroy the output" is not shown by calculation. | ST DocID7699 rev 9, table 4, (dI/dt)c 2.7 A/ms at 125 °C. Figures 6 and 10. | Fixed in sections 6.6 and 9.2 and risk 4. K1 is the back-up, see M3. Bench test. |
| M2 | Major | Input protection | A fuse only in the ground leg is bypassed by any second path from board ground to transformer leg 2. With leg 2 earthed and an earthed computer on USB, or with a shared ground to another device on the same transformer, a board fault keeps flowing through that path after F1 opens. The design listed the first case as safe. | Loop analysis, section 6. | Fixed in section 5.3 and on the safety page. Recommended, not applied: a second fuse of the same type in the `AC_L` leg. |
| M3 | Major | Valve common relay | The hold network that should keep K1 closed for 30 to 50 ms after `DRV_EN` falls does not work. Q1 is in series and releases in 0.1 ms, because `DRV_EN` resets the expander. K1 opens within 10 ms and can break fault current above the 5 A its contact is rated to switch. D21 and C50 do nothing. | `hardware/sim/relay_drive.cir`. Omron G5NB data sheet, contact ratings. | Fixed in section 7.2: the text now describes the circuit as it is. Decision needed, section 4.3. |
| M4 | Major | Over-current latch | GPIO40 was wired to `FAULT_N`, the feedback node of the latch. A pin left as an output and driven high can clear or mask the latch and read back "no fault". | Netlist. ESP32-S3 datasheet v2.2 table 5-4: 40 mA source current. | Fixed: R87, 10 kohm, between the latch and the pin. |
| M5 | Major | AC presence, U6 | The zero-cross input of U6D sat one Schottky drop below ground on every negative half-cycle: -0.28 V at 25 °C, about -0.36 V at -20 °C. The LM2901 limit is -0.3 V, and U6 also holds the keep-alive and over-current comparators. | TI SLCS006S, absolute maximum ratings and note 4. Nexperia BAT54S, table 7. `hardware/sim/zerocross.cir`. | Fixed: R88, 10 kohm, halves the level at the pin to -0.18 V. |
| M6 | Major | Logic supply | R1 takes 220 to 365 W at the peak and about 0.3 J when the board is plugged in near a mains peak. It is a 1 W general-purpose thick-film resistor whose data sheet has no pulse rating. | Uniroyal thick-film data sheet for C39347. 45 V over 3.3 ohm plus the transformer. | Stated in section 4.4. Recommended: a 2512 part with a published single-pulse rating. None chosen: the anti-surge series I found at JLCPCB had no stock. |
| M7 | Major | Valve common | The COM terminals have no surge clamp while K1 is open, which is most of the time. The specification asks for protection on field terminals, and COM is the longest wire in the installation. The only limit is the 750 VAC withstand of K1's open contact. | Netlist: no TVS on `COM`. Omron G5NB data sheet, dielectric strength between contacts. | Recommended, not applied: one more SMBJ48CA from `COM` to `GND`. It also absorbs the inductive kick when K1 opens under load. |
| m1 | Minor | Document | The summary table named the BTA204S-600D as the output triac. Netlist, BOM and section 6.3 use the T410-600B-TR. | Section 1 against section 6.3. | Fixed. |
| m2 | Minor | Fail-safe gate | Four statements about the keep-alive were wrong or optimistic: a 5 kHz upper limit that does not exist, 2 Hz rejected when one tolerance corner accepts it, a 0.5 s shortest hold-up that is 0.45 s and less when hot, and 10 Hz accepted with 11 % margin at the slow corner. | `keepalive_faults.cir`, RATE and CORNER lines. | Fixed in section 8.4 and the pin map. Required edge interval is now 20 ms. |
| m3 | Minor | Over-current | The trip window is 4.0 to 5.4 A, not 4.2 to 5.1 A, and not symmetric. The lowest trip is 13 % above the largest load the specification allows. Two outputs in inrush at once trip it. | `hardware/sim/isense_corners.cir`. | Fixed in section 9.1. Firmware rule 10 added: stagger starts by 100 ms. |
| m4 | Minor | Valve common relay | The contact was quoted as 5 A at 30 VDC. It is 3 A. With a hot coil and the lowest `V5` the pull-in margin is 3 %. | Omron G5NB data sheet: 75 % at 23 °C coil temperature. | Rating fixed. Recommended: feed the coil from `V_BUCK`, which adds 0.35 V. |
| m5 | Minor | Output stage | The snubber returns to `GND`, not `ISNS`. A shorted C101 then feeds its valve through 47 ohm with a current the shunt cannot see, so the unexpected-current check misses it. The TVS was deliberately returned to `ISNS` for this reason. | Netlist, C101 to C901. | Recommended: return the snubber to `ISNS`. The 0.8 mA of total snubber current is below the measurement floor. |
| m6 | Minor | Logic supply | The ripple rating of C1 was taken from the 50 V row. The fuse was described as a 452 series; the holder ships with a 454. | Lelon VZH table, 63 V. Littelfuse 154 series. | Fixed. C1 is at 89 % of its rating at the peak corner. |
| m7 | Minor | Sensor supply | The TPS2553 limit was scaled, not calculated: it is 116 to 158 mA. F3 holds only 0.14 A at 60 °C, so it can trip before U11 limits. With U11 off, a negative miswire pulls its output pin below -0.3 V until F3 trips. | TI SLVS841 equation 1. Littelfuse 1812L table. | Figures fixed. Recommended: a Schottky from `GND` to `SP_OUT`. |
| m8 | Minor | Display | The contact side of the FPC was unknown, and J7 is bottom-contact. The contacts are on the screen face. SPI was to start at 20 MHz; the ST7789V guarantees 15 MHz. `3V3` can reach 3.40 V on a pin with a 3.3 V recommended maximum. | Panel drawing, front and rear views. Sitronix ST7789V v1.3. | Document fixed. J7's contact side and pin 1 position wait for the enclosure. Check on a sample. |
| m9 | Minor | MCU | No supply supervisor. Espressif asks for one where the supply rises or falls slowly or is unstable, and says the RC circuit alone may then fail to start the chip or spoil a flash erase. | Espressif hardware design guidelines, 1.3.3. | Recommended: a footprint for a 3.0 V open-drain supervisor on `EN`. |
| m10 | Minor | Expander | Every expander pin floats while the device is in reset, which is whenever no valve runs. P06, P07, P16, P17 float always, and P10 to P13 on the 4-zone build. The TCA9539 has no pull-ups, and its supply current is specified only for inputs at a rail. | TI SCPS202C, 6.5: supply current is given for inputs at `VCC` or `GND`. | Recommended: 100 kohm pull resistors on the sixteen pins, or measure the supply current on the prototype and accept it. |
| m11 | Minor | Expander | `SENS_FLT_N` and `VARIANT_N` are expander inputs. They can be read only while the keep-alive runs. The sensor supply is used between runs, for leak detection. | Netlist, U5 pins 17 and 18. | Recommended: move `SENS_FLT_N` to a GPIO if one is free after B1, or have firmware start the keep-alive briefly to read it. |
| m12 | Minor | USB, debug | No series resistor footprints on D+ and D-, and none on U0TXD. Espressif recommends reserving both. | Hardware design guidelines, 1.3.13 and UART section. | Recommended. Stated in section 5.1. |
| m13 | Minor | Logic | U7 is a 74HC00. Two of its inputs rise through 10 kohm pull-ups from open-collector comparators. The data sheet limits the input transition rate. | Nexperia 74HC00, recommended operating conditions: 625 ns/V at 2 V, 139 ns/V at 4.5 V. | Recommended: Nexperia 74HC132D, LCSC C5601, 19,757 in stock at JLCPCB on 2026-11-10. Its data sheet, table 2, gives the same pinout as the 74HC00, with Schmitt-trigger inputs. |
| m14 | Minor | Qwiic | The rail budget allows 125 mA on Qwiic. F2 holds 0.08 A at 60 °C and has up to 6 ohm, which drops 0.3 V at 50 mA. | Littelfuse 1206L table. | Recommended: budget 80 mA and say so in the user documentation. |
| m15 | Minor | AC presence | The pin map said 50 ms without an edge means no AC; the document said 25 ms. The text said the two edges are symmetric about the zero crossings; they are not. | `zerocross.cir`. | Fixed: 25 ms in both, and the edge offsets are 0.30 ms and 0.09 ms. |
| m16 | Minor | BOM | The T410 stock of 1320 covers one batch of 100 eight-zone boards. The display has 455 in stock from one maker. No BOM line for the CR2032 cell, so the "LCSC number on every line" rule has a gap. | JLCPCB listing, 2026-11-10. | Section 9. |
| n1 | Note | Output stage | Opto LED current is 7.1 mA against 5.6 mA needed at the worst corner, a margin of 1.27 with no allowance for LED ageing. The four worst cases have to coincide. | onsemi MOC3023M/D figure 4. TI SCPS202C, 8 mA at 0.5 V. | None. Confirm firing at -20 °C on the bench. |
| n2 | Note | Logic supply | The half-wave rectifier draws DC from the transformer: 0.06 A at the typical load and 0.2 A at the 5 W peak. | `supply.cir`. | Already risk 13. Test at peak load too. |
| n3 | Note | BOM | The cost table reproduces to within 0.50 USD per board with today's full JLCPCB price tiers. | Section 9. | None. |
| n4 | Note | Tooling | The pull request says the document tables are generated from the netlist. No generator is in the branch. | Section 11. | A checker should be committed with the capture work. |
| n5 | Note | Firmware | The firmware on `main` drives valves as GPIOs, averages the current reading, expects a release pulse for the latch and one pulse per zero crossing, and does not enable PSRAM ECC. | Firmware architecture section 9. `sdkconfig.defaults`. | Hardware design sections 19.3 to 19.5. |
| n6 | Note | RTC | The PCF8563 powers up with its 32 kHz clock output enabled, which raises the back-up current from 250 nA to 550 nA typical. The battery estimate assumes it is off. | NXP PCF8563 rev 11.1, table 22 and static characteristics. | Firmware writes 0x00 to register 0x0D once. Battery life stays above ten years either way. |

## 3. B1: the fail-safe gate does not fail safe

### 3.1 What the simulation shows

`hardware/sim/keepalive_faults.cir` takes the detector of section 8 and
breaks one part at a time. A resistor is opened, a capacitor is opened or
shorted, a diode or an IC output is failed either way.

| Fault | `KA_OK` | Detected by the start-up test of the first draft |
|---|---|---|
| R30 open | High 13 ms after the first edge of the first run, then never low again. R32 feeds the reservoir from `KA_OK` and nothing drains it. | No. It passes at start-up and fails during the first run. |
| R31 opens while drive is enabled | Held at 3.3 V by R32 until power is cycled | No |
| R34 open | High from power-up with no keep-alive at all | Yes |
| C22 short | Normal while the pin toggles. High whenever the pin is high. | No. The test never holds the pin high. |
| U6A output never pulls low, or U7 gate 3 or 4 stuck | High from power-up | Yes |

Thirteen other faults leave drive off or the circuit working. They are in
section 8.5 of the design document.

### 3.2 Why it is blocking

In each of the five cases firmware still switches valves through the
expander, so the controller looks healthy. If the firmware then stops with a
valve on, nothing turns the valve off. The three things `DRV_EN` does, the LED
supply, the expander reset and the relay, are three actuators behind one
signal. They cover a fault in each other. They do not cover a fault in what
makes the signal.

The design relied on a test at start-up. A sprinkler controller stays powered
for months. A resistor that opens in June is found in the autumn, or when a
valve floods something. The brief for this review sets the line at any single
fault that leaves a valve able to stay on, and this is on the wrong side of
it.

### 3.3 What I changed

- **The single-fault table** in section 8.5 of the design document now has a row for every part, with the five dangerous ones in bold.
- **The self-test** now runs before every run, at start-up and after every run. Firmware stops the keep-alive between runs anyway, so `DRV_EN` should be low before each one, and the expander answers on I2C only when `DRV_EN` is high. The test costs nothing. It also adds a step that holds the pin high, which finds a shorted C22.
- **On a failed test** firmware must write every expander pin back to input. The first draft said only "refuse to run valves", which leaves a valve on if the test fails after a run.

That closes the gap for a firmware that is alive at the next run. It does not
close it for the case the circuit exists for, a firmware that stops during
the run in which the part failed.

### 3.4 What I recommend, and did not apply

Give the relay its own detector. It is a second diode pump on the same pin
that drives Q2's gate directly, with no comparator and no logic gate.

| Change | Parts | Purpose |
|---|---|---|
| Add R89, 1 kohm, from `KA` to a new net `KB_N0` | 1 | Series resistor of the second pump |
| Add C51, 1 uF, from `KB_N0` to `KB_N1` | 1 | Pump capacitor |
| Add D22, BAT54S, anode `GND`, common `KB_N1`, cathode `RLY_G2` | 1 | Pump diodes |
| Keep C50 on `RLY_G2` as the reservoir. Change R14 to 1 Mohm and add R90, 1 Mohm, in parallel | 1 | Two bleed resistors, so that one open does not hold Q2 on |
| Remove D21 and R13 | -2 | Q2 no longer listens to `DRV_EN` |
| Replace R30 by two 110 kohm in parallel, R30 and R91 | 1 | One open resistor then doubles the hold-up and no more |
| Add R92, 10 kohm, from `DRV_EN` to GPIO42 | 1 | Firmware reads `DRV_EN` directly in the self-test |

Net: four more parts, all on BOM lines that exist, except the 110 kohm value.

How it covers the faults. K1's coil needs Q1 and Q2 both on. Q1 follows the
expander bit, which needs `DRV_EN`, the old detector. Q2 would follow the new
one. A fault that sticks the old detector on leaves Q2 free to open K1 when
the keep-alive stops. A fault that sticks the new one on leaves `DRV_EN` free
to turn off the LED supply, reset the expander and so release Q1. The two
share only the `KA` pin and K1 itself.

`hardware/sim/relay_pump_proposal.cir` runs the second detector alone:

| Case | Q2 gate above 1.45 V after the first edge | Below 0.65 V after the last edge |
|---|---|---|
| -20 °C | 1.7 ms | 0.73 s |
| 27 °C | 1.5 ms | 0.71 s |
| 60 °C | 1.3 ms | 0.36 s |
| 27 °C, one bleed resistor open | | 1.33 s |
| 27 °C, pin stops high | | 1.44 s |

1.45 V and 0.65 V are the highest and lowest gate thresholds of the AO3400A,
so these are the times at which Q2 is certainly on and certainly off.

This is a proposal from a reviewer. Nobody has reviewed it in turn. It
should be checked like any other part of the design before it goes into the
netlist.

## 4. The six departures from the specification

### 4.1 Opto-triac gate drive

**Verdict: right, and the best of the available answers.**

- **The quadrant claim holds.** The ST T4 series data sheet lists "three quadrants Triac" and gives trigger current for quadrants I, II and III only. The WeEn BTA204S-600D data sheet says "triggering in three quadrants only". Logic referenced to T1 can only source gate current, which is quadrants I and IV.
- **LED current.** The TCA9539 guarantees 8 mA at 0.5 V and 10 mA at 0.7 V. With 150 ohm, a 1.5 V LED and `3V3` at 3.19 V that gives 7.1 mA at -20 °C against a need of 5 mA x 1.12 = 5.6 mA. I get the same figures. The margin is thin and positive. Lowering R101 would push the best-case pin current past the expander's 18 mA limit at 85 °C junction, so 150 ohm is a fair balance. Finding n1.
- **Gate resistor.** 100 ohm limits the opto-triac to 0.45 A at the 45 V peak, under its 1 A rating and the T410's 4 A gate rating. At low line the triac needs 6.5 V across it before it fires, 12° into the half-cycle, which costs 0.2 % of the power into a resistive load. An inductive load re-fires at its current zero, where the line is already tens of volts.
- **dV/dt of the opto.** The onsemi sheet for this part number gives 1000 V/us minimum. I expected less, from older sheets for the MOC302x family, and checked the table as an image. The figure is right for onsemi. It should not be assumed for another maker's MOC3023.
- **Commutation of the opto.** The onsemi sheet for the zero-cross family states that commutating dV/dt "is a function of the load-driving thyristor(s) only". The opto carries current for microseconds until the main triac latches, so its own commutation is not the limit.
- **A zero-cross opto would be worse.** The MOC3043 will not trigger with more than 20 V across it. A solenoid at a power factor near 0.4 reaches its current zero with 30 to 40 V on the line, so a zero-cross part would not re-fire there and the triac would conduct in fragments. The author's argument is correct.

What the departure costs is nine opto-triacs and a firing threshold of a few
volts. What it buys is gate drive in the two quadrants every triac handles
best, and a power stage with no logic pin attached to it.

### 4.2 Valve bits on a TCA9539 held in reset

**Verdict: right. Two limits to write down.**

| State | Expander pins | Source |
|---|---|---|
| Power-up | Inputs, output register all ones. "No glitch on power up." | TI SCPS202C, features and 8.4.1 |
| `RESET` low | Registers and the I2C state machine held at their defaults | 8.3.2 |
| `3V3` between 1 V and 3.3 V | Keeps its registers. Power-on reset releases at 1.2 to 1.5 V rising and re-arms below 0.75 to 1 V. | 6.5 |
| SDA or SCL stuck | Firmware cannot write. Valves stay as they were until firmware stops the keep-alive. | Contract |
| Address collision | Not possible on the board: the only other device on I2C0 is the RTC at 0x51, and Qwiic has its own bus. | Pin map |

- **A single wrong write can open too many valves.** Writing 0xE0 to output port 0 turns on Z1 to Z4 and MV with the relay closed. The hardware cannot count valves. Two things limit it. The relay bit has the opposite sense to the valve bits, so the all-zeros and all-ones patterns, the likeliest corruptions, cannot produce "valves on, relay closed". And firmware must read both registers back after every write and once per tick, rule 3 of section 19.1.
- **Two inputs sit behind the reset.** Finding m11.

### 4.3 Relay in the valve common

**Verdict: right to add. It does cover a shorted triac. Its drive circuit needs a decision.**

- **Contact.** 5 A at 250 VAC resistive, 5 A maximum switching current. In normal use firmware makes sure it never switches current, and 1.5 A of valve current is well inside it.
- **Coil.** 40 mA from `V5` through two AO3400A. Pull-in margin is 3 % at the worst corner, finding m4.
- **Does it cover a shorted triac?** Yes. With K1 open no valve has power, and K1 is open whenever no valve is wanted. A shorted triac, TVS or opto shows as current with every valve bit off as soon as K1 closes for another zone, and firmware then opens K1 and refuses to run. The price is that one shorted output stops all watering until someone repairs it. That is the right way round.
- **What happens when it opens under load.** That is finding M3. The circuit as drawn opens K1 within 10 ms of any loss of `DRV_EN`. After a stopped keep-alive it breaks at most 1.5 A, which is fine. After an over-current trip it can break the transformer's short-circuit current.
- **My recommendation on M3.** Keep the fast release and remove D21 and C50. M1 shows the triac cannot be relied on to stop a dead short by itself. A relay that waits 50 ms gives the triac three more cycles of fault current in exactly the case where it needs help. Shorted wires are rare events, and a 5 A contact breaking 15 A at 24 VAC a few dozen times in its life is a smaller risk than a dead triac. It is still outside the contact's rating, so it goes on the bench list.
- **New failure modes.** A welded contact is latent. Firmware can find it for free: with K1 commanded open, turn one valve bit on for 40 ms and check that no current flows. A surge on the floating COM wire has no clamp, finding M7.

### 4.4 Fuse in the ground leg

**Verdict: the reasoning is right as far as it goes, and one fuse is not the best answer.**

Section 6 walks through every loop. The author's point stands: three fault
loops run from the USB ground through board `GND` to terminal 2 and never
touch terminal 1, so a fuse in `AC_L` misses them. But a ground-leg fuse has
the mirror-image weakness. Anything else that connects board ground to that
transformer leg is in parallel with it, and after F1 opens the fault current
takes the other path. A fuse in each leg covers both sets. It costs one more
holder and fuse, about 1.2 USD.

What the user documentation must say is now on the safety page: use a
transformer with neither output wire earthed, and if either is earthed never
connect a mains-powered computer by USB with the 24 VAC plug in.

### 4.5 Firmware RMS current measurement

**Verdict: right.** A rectifier in front of the ADC would add its own offset
to a 5 mV signal and park the result near 0 V, where the specification says
not to measure. Sampling the waveform around a bias gives true RMS, the inrush
peak and a DC term that reveals a triac conducting one way only. The ESP32-S3
figures the error budget uses are the data sheet's: 100 nF at the pin, ±10 mV
total error at 6 dB attenuation, DNL ±4 and INL ±8 counts. The cost is on the
firmware side: it averages sixteen samples per tick today, which gives the
bias, not the current.

### 4.6 5 V bus and LDO

**Verdict: right.** The LDL1117 figures check: 0.6 V maximum dropout at 1.2 A,
±3 % over temperature, 87 dB at 120 Hz. The LDO dissipates 0.27 W at the
typical load. At the budget's 543 mA peak it would dissipate 0.9 W, which a
SOT-223 at 120 K/W cannot do at 60 °C. That peak needs WiFi transmitting
continuously and a full Qwiic load together, so it is not a steady state, but
the layout should give the tab several square centimetres of copper.

## 5. Fail-safe gate and over-current latch

### 5.1 States of the keep-alive pin

| State | Result | Checked by |
|---|---|---|
| Stuck low | Off within 0.9 s | `keepalive.cir` |
| Stuck high | Off within 1.2 s. Never enabled from power-up. | `keepalive.cir`, `keepalive_faults.cir` |
| Floating in reset | R33 holds it low. GPIO38 is high impedance at reset with no power-up glitch. | ESP32-S3 datasheet v2.2 tables 2-1 and 2-2 |
| ROM download mode, USB flashing | GPIO38 is not a strapping pin and has no boot function on a module with quad flash. No edges. | Datasheet section 3, module datasheet table 3-1 |
| Toggling too slowly | 1 Hz never enables. 2 to 3 Hz depends on tolerances. | `keepalive_faults.cir` RATE lines |
| Toggling too fast | Enables. No upper limit up to 1 MHz. | Same |
| Any duty cycle | 2 % and 98 % enable. | Same |
| `3V3` ramping over 0.1 s or 1 s | No pulse on `KA_OK` above 0.95 V, at a rail voltage where U7 and the expander do not work yet. | CORNER lines |
| Brownout with a valve on | The expander keeps its bits down to about 1 V. Off within the hold-up, 0.9 s. | TCA9539 power-on reset thresholds |

The circuit detects edges. It does not detect a correct rate. The
specification asks only for toggling, so this is not a defect, but the design
document claimed a window it does not have, finding m2.

### 5.2 Component faults

Section 3 and section 8.5 of the design document. Besides B1:

- **Q3 shorted.** `V_DRV` stays on. The expander reset and the relay still act. Covered.
- **R36 open.** `KA_OK` is pulled up through the hysteresis resistor only. Works.
- **R53 open.** `OC_TRIP_N` has no pull-up and floats at a CMOS input. Over-current protection is then undefined. Not a flooding fault. A 74HC132 (m13) does not cure it, but firmware can test the latch at start-up if a way to trip it is added. I did not pursue this.
- **R48 to R51.** An open at R48 or R51 trips the latch and holds it. An open at R49 removes the upper threshold and stops the current measurement, which firmware sees as a bias far from 0.79 V.
- **GPIO40.** Finding M4, fixed.

### 5.3 The simulations, re-run and extended

I re-ran the author's three files with ngspice 47 and got the published
numbers. Five files are new:

| File | Question | Answer |
|---|---|---|
| `keepalive_faults.cir` | Faults, rates, tolerance and temperature | B1 and m2 |
| `isense_corners.cir` | Trip level with every tolerance | 4.0 to 5.4 A, m3 |
| `relay_drive.cir` | Which transistor releases K1 first | Q1, in 0.1 ms. M3 |
| `zerocross.cir` | Level at the comparator pin | M5 |
| `relay_pump_proposal.cir` | Does the proposed second detector work | Section 3.4 |

One thing I got wrong on the way and corrected: my first temperature runs set
the temperature before a circuit reset, and ngspice then ran them at 27 °C.
The committed files set it after the reset, and the results differ between
temperatures as they should.

## 6. Fault loops and the fuse

Terminal 1 is `AC_L`. Terminal 2 is `AC_N` and reaches board `GND` through F1.
"L fuse" is the second fuse I recommend in `AC_L`.

| Fault | Loop | Through F1? | Through an L fuse? |
|---|---|---|---|
| Valve wire shorted to COM | Leg 1, K1, COM, short, PTC, triac, shunt, `GND`, F1, leg 2 | Yes | Yes |
| D1 or C1 shorted | Leg 1, D1 or D2 and R1 and C1, `GND`, F1, leg 2 | Yes | Yes |
| Sensor `GND` terminal touched to COM or to leg 1 | Leg 1, wire, `GND`, F1, leg 2 | Yes | Only if the wire touches COM, not leg 1 itself |
| Leg 1 earthed, earthed computer on USB | Leg 1, earth, computer, USB ground, `GND`, F1, leg 2 | Yes | No |
| Two devices on one transformer with legs swapped, grounds joined | Leg 1, other device's ground, joined ground, `GND`, F1, leg 2 | Yes | No |
| COM or a valve wire leaking to earth, earthed computer on USB | Leg 1, K1, COM, earth, computer, USB ground, `GND`, F1, leg 2 | Yes | Yes |
| **Leg 2 earthed, earthed computer on USB, any board fault from the first two rows** | As those rows up to `GND`, then F1 in parallel with USB ground, computer, earth, leg 2 | **Shared. After F1 opens, all of it goes through the USB cable.** | Yes |
| **Second device on the same transformer with the same leg as ground, grounds joined, any board fault** | As above, with the other device's ground in place of earth | **Shared, then all through the joined ground** | Yes |
| Input surge | Clamped by D1 from `AC_L` to `GND`, returning through F1 | Yes | Yes |

The two bold rows are what a ground-leg fuse alone leaves open. They need an
unusual installation and a board fault together, which is also true of the
three loops the author moved the fuse to cover.

One assumption in section 5.3 I could not confirm from the code text: 8 A as
the short-circuit limit of a Class 2 source. Secondary sources give 8 A and
100 VA for an inherently limited source up to 30 V. A source that relies on a
fuse may deliver more for a short time, so the 20 A the PTC is rated for is
not a guaranteed ceiling.

## 7. Pinouts and footprint-critical facts

Checked against the package drawing or pin table for the ordering code in the
BOM.

| Part | Netlist | Source | Result |
|---|---|---|---|
| T410-600B-TR, DPAK | 1 T1, 2 and tab T2, 3 G | ST DocID7699 rev 9, page 1 drawing, as an image | Correct. The author's assumption holds. |
| BTA204S-600D, alternative | Same | WeEn data sheet, table 2 | Correct |
| MOC3023SR2M | 1 anode, 2 cathode, 3 NC, 4 and 6 main terminals, 5 do not connect | onsemi MOC3023M/D rev 3, figure 1 | Correct |
| LM5164DDAR | 1 GND, 2 VIN, 3 EN/UVLO, 4 RON, 5 FB, 6 PGOOD, 7 BST, 8 SW, pad GND | TI SNVSAU4D, table 4-1 | Correct |
| LDL1117S33R | 1 GND, 2 VOUT, 3 VIN, tab VOUT | ST DocID030319 rev 3, table 1 | Correct |
| TCA9539PWR | 24 pins as listed, address 0x74 with A0 and A1 low | TI SCPS202C, pin functions and table 2 | Correct |
| LM2901DR | 1OUT pin 1 with inputs on 6 and 7, 2OUT pin 2 with 4 and 5, 3OUT pin 14 with 8 and 9, 4OUT pin 13 with 10 and 11 | TI SLCS006S | Correct |
| 74HC00D | Standard | Nexperia data sheet | Correct |
| MCP6002T-I/SN | Standard dual op-amp | Microchip DS20001733 | Correct |
| TPS2553DBVR | 1 IN, 2 GND, 3 EN, 4 FAULT, 5 ILIM, 6 OUT. EN active high. | TI SLVS841 | Correct |
| PCF8563T/5 | 1 OSCI, 2 OSCO, 3 INT, 4 VSS, 5 SDA, 6 SCL, 7 CLKOUT, 8 VDD | NXP rev 11.1, table 3 | Correct |
| USBLC6-2SC6 | 1 and 6 I/O1, 3 and 4 I/O2, 2 GND, 5 VBUS | ST data sheet, page 1 | Correct |
| BAT54S | 1 anode, 2 cathode, 3 common | Nexperia, pinning table | Correct in all five uses |
| AO3400A, AO3401A | Named G, S, D in the netlist | AOS data sheets | Map to the SOT-23 footprint at capture: 1 G, 2 S, 3 D |
| 1N4148W | Named A, K | LCSC copy: pin 1 cathode, pin 2 anode | Map at capture |
| ESP32-S3-WROOM-1-N16R8 | 41 pins as listed | Espressif module datasheet v1.8, table 3-1 | Correct, every pin |
| USB-C TYPE-C-31-M-12 | A and B rows as listed | Hroparts drawing, pin table | Correct |
| FPC connector FPC-05F-12PH20 | 12 way, 0.5 mm | XUNPU drawing: flip lock, bottom contact, tail 0.3 mm thick and 6.5 mm wide | Matches the panel tail. Contact side and pin 1 depend on the fold, m8. |
| HS20HS072RX panel | 1 GND, 2 CS, 3 RS, 4 SCL, 5 SDA, 6 RESET, 7 NC, 8 IOVCC, 9 VCC, 10 LEDA, 11 LEDK, 12 GND | Hansheng datasheet v1.0, interface table and drawing | Correct |
| KF2EDGR-3.81 headers | Positional | Kefa drawing: 3.81 mm pitch, 1.4 mm holes, 300 V 8 A | Correct |
| G5NB-1A-E | Named coil and contact pins | Omron data sheet | Pin numbers to take from the drawing at capture. Not done here. |
| EC11J1525402, JST SM04B-SRSS-TB, tactile switches, CR2032 holder | Named pins | Listing, or a drawing without readable text | Not checked pin by pin. Footprints need the drawings at capture. |

The Qwiic order, 1 GND, 2 3V3, 3 SDA, 4 SCL, is the convention. I did not
find it in a JST document, because it is not a JST matter.

## 8. ESP32-S3 pin use

Against ESP32-S3 Series Datasheet v2.2, the module datasheet v1.8 and the
hardware design guidelines of 2026-11-10.

- **Strapping pins.** GPIO0 has a 10 kohm pull-up and the boot button. GPIO3 is ignored unless eFuses are burned. GPIO45 is open and pulled down inside. GPIO46 carries the status LED through 680 ohm to ground. A LED to ground can only pull down, the strap wants low or does not care, and the ROM does not drive the pin in download mode. **The LED on GPIO46 is safe at boot and during flashing.**
- **Glitches.** Table 2-2 lists low-level glitches on GPIO1 to 17 and high-level ones on GPIO18 to 20. None of those pins can move a valve. GPIO38, 40 and 41 are not in the table.
- **ADC.** GPIO1, 2 and 3 are ADC1 channels 0, 1 and 2, as the pin map says. Nothing analogue is on ADC2.
- **USB.** GPIO19 is D-, GPIO20 is D+, on module pins 13 and 14.
- **Octal PSRAM.** Module pins 28 to 30, GPIO35 to 37, are not connected.
- **GPIO47 and 48.** 3.3 V on this module. They are 1.8 V only on the R16V variant.
- **JTAG pins.** GPIO39 to 42 are the JTAG pins and are free while the JTAG source is the USB controller, the default. GPIO40 is MTDO. That made the direct connection to the latch worse than it looked, and is one more reason for R87.
- **Antenna.** The guidelines want the antenna beyond the board edge and 15 mm of clearance in the housing. Now in section 12.5.
- **Decoupling.** 22 uF and 100 nF at module pin 2. The guidelines ask for 10 uF at least at the power entry.
- **EN.** 10 kohm and 1 uF, as recommended. The guidelines add the supervisor condition, finding m9.
- **Module temperature.** 65 °C for the R8 module, 85 °C with PSRAM ECC, as the design says. Firmware has not enabled it.

## 9. BOM

- **LCSC numbers.** I queried all 92 lines against the JLCPCB parts search on 2026-11-10. Every number returns the maker, part number and package in the BOM. Stock had not moved by more than a few parts in any line.
- **Low stock against 100 boards.** T410-600B-TR 1320 for 900 needed. HS20HS072RX 455. VZH471M1JTR-1616 968. EC11J1525402 1486. 1812L020/60DR 1650. KF2EDGR-3.81-7P 1806. LM5164DDAR 1825.
- **No second source.** The display. The encoder. The LM5164. The fuse holder. The G5NB relay footprint has compatible parts from other makers; I did not read one.
- **Lifecycle.** The JLCPCB listing does not give it. The TI addendum lists LM5164DDAR as active. I did not check the others.
- **4-zone variant.** Correct. Eleven parts per channel for channels 5 to 8, plus J3, its plug and R28. The BOM quantities for both builds match a count over the netlist.
- **Cost.** Recomputing from JLCPCB's full price tiers gives 37.03 and 27.57 USD for the 8-zone board at 10 and 100, and 31.08 and 23.45 USD for the 4-zone. The document says 37.54, 28.07, 31.57 and 23.94. The 0.50 USD difference is the same in all four, so it is one line whose price moved or one tier boundary. I did not chase it.

**The display.** Keep it for revision A. The panel is plugged in by hand at
final assembly, so it does not have to come from JLCPCB's assembly stock at
all. Tens of boards need tens of panels and 455 are on the shelf. Buy the
whole revision A quantity in one order, with spares, before layout, and use
the first one to settle the contact side, the controller variant and the
mechanical drawing. The risk is for revision B: I found no other maker's
panel with this 12-pin order, at JLCPCB or in a web search. Before a second
batch, either find one or accept a new J7 footprint.

## 10. Specification compliance

| Requirement | Status |
|---|---|
| Rule 1, outputs off by default, enforced in hardware | Met for reset, crash and flashing, within the hold-up. Not met under the single faults of B1. |
| Rule 2, no strapping pins on outputs | Met. No output is on a GPIO. |
| Rule 3, drive removed about one second after toggling stops | Met: 0.45 to 0.9 s, 1.2 s if the pin stops high. From a dead control loop it is 1.4 s, because firmware allows the heartbeat 500 ms. |
| Rule 6, relay in the valve common | Met. See M3 and M7. |
| Up to two zones plus master | Not limited in hardware. Firmware only. |
| A shorted valve or wire must not destroy the output | Not shown. M1. Bench test. |
| Survive 32 VAC continuous | Met by calculation: 45.3 V peak against 48 V stand-off parts, 63 V and 100 V capacitors, a 100 V regulator. The margin is small: the TVS stand-off voltage is reached at 33.9 VAC and their lowest breakdown voltage at 37.7 VAC. |
| Clamp on every sensor terminal | Met. |
| Protection on field terminals | COM is unclamped while K1 is open. M7. Spark gaps are left to layout. |
| Through-hole parts only where no SMT part exists | K1 is through-hole. No SMT alternative was searched for, by the author or by me. |
| LCSC number on every BOM line | Met for what is on the BOM. The coin cell is not on it. |
| Temperature range | The specification gives none. The design assumes -20 to +50 °C outside and 60 °C inside. The specification should state it. |

## 11. How the files are kept consistent

The pull request says the document tables are generated from the same data as
the netlist. There is no generator in the branch. I wrote a checker and ran it
before and after my changes:

- **Document against netlist.** Every component table row in the design document against the netlist entry: value, package, LCSC number, the set of nets on its pins, and the note. 205 rows before my changes, 207 after. No differences.
- **BOM against netlist.** References and both build quantities per LCSC number. No differences.
- **Pin map against netlist.** Every GPIO against the net on that module pin. No differences.
- **Netlist alone.** No duplicate references and no net with a single connection.

The checker is not committed. It belongs with the capture tooling, written
properly, and with a generator if the tables are to stay generated. Finding
n4.

Where I changed a part, I changed the netlist, the document table, the pin
map and the BOM in the same commit and re-ran the checker.

## 12. What I verified and what I did not

**Opened and read, from the maker or LCSC's copy of the maker's file:**
ST T4 series, WeEn BTA204S-600D, onsemi MOC3023M and MOC3043M, TI TCA9539,
LM2901, LM5164 and TPS2553, Nexperia 74HC00 and BAT54S, Microchip MCP6002, ST
LDL1117 and USBLC6-2SC6, NXP PCF8563, Littelfuse SMBJ, SMCJ, 2920L, 1812L,
1206L, 154 and 452/454 series, Omron G5NB in English, Lelon VZH, Bourns
SRN6045TA, TA-I RLP25, AOS AO3400A and AO3401A, MDD SS34, SS210 and M7, the
Nexperia 74HC132, the
1N4148W, Uniroyal thick-film resistors, the Hansheng panel, the XUNPU FPC
connector, the Hroparts USB-C receptacle, the Kefa header, the ALPS encoder
product sheet, Epson FC-135, both LEDs, Espressif's ESP32-S3 datasheet v2.2,
module datasheet v1.8 and hardware design guidelines, Sitronix ST7789V v1.3,
Winbond W25Q128JV rev C.

**Read as rendered images, because text extraction lost them:** the T410
package drawing and figures 5 to 11, the MOC3023 electrical table and
figures, the panel outline drawing, the FPC connector drawing, the USB-C and
header drawings.

**Recalculated:** opto LED current and firing voltage, triac dissipation and
junction temperature, the LM5164 design table, the fuse loading, the bulk
capacitor ripple and voltage, sensor miswire dissipation, the TPS2553 limit,
the relay pull-in margin, the short-circuit currents and their rate of fall,
the trip window.

**Simulated:** section 5.3.

**Not verified:**

- **Anything that needs a bench.** The risk register in section 20 of the design document stands, with item 4 widened.
- **The NEC Class 2 table.** Secondary sources only.
- **The USB 10 uF attach limit.** Not re-read.
- **The module's actual flash part.** Espressif does not fix it.
- **The panel's controller variant** and its logic supply current, which the panel datasheet gives as "TBD".
- **A CR2032 cell datasheet.** No cell is chosen, and the one maker site I tried refused the download.
- **Pin-by-pin footprints** of the relay, encoder, JST connector, tactile switches and coin cell holder.
- **Generic capacitors.** DC bias and voltage ratings are from the listing.
- **Second sources** named in the BOM notes, and the lifecycle status of every part but one.
- **A surface-mount relay.** Not searched for.
- **Schottky leakage over temperature.** The hold-up figures above 60 °C rest on a generic model.
- **My own proposal in section 3.4.** Simulated, not reviewed.

## 13. Hardware and firmware: the keep-alive contract

The design document said the keep-alive must never come from a timer. The
merged firmware makes its edges in a GPTimer alarm interrupt. I read
`OutputEnableGuard::step()` on `main`. The handler toggles the pin only while
a heartbeat counter, incremented by the control loop, has changed within the
last 500 ms, and drives it low otherwise. A stuck control loop, a stopped
timer and a halted CPU all end the edges. That is what the hardware rule was
written to guarantee. The two are consistent. The rule needed better wording,
and it now forbids a peripheral that toggles the pin by itself.

On the numbers:

- **The 5 to 50 ms edge requirement was too loose at the slow end.** 10 Hz clears the threshold by 11 % at the slow tolerance corner. The requirement is now an edge every 20 ms. The firmware's 5 ms needs no change.
- **The 0.5 to 0.9 s hold-up is right to within 50 ms.** 0.45 to 0.9 s over -20 to 60 °C, 1.2 s if the pin stops high.
- **The hold-up no longer has to cover a flash erase**, because the interrupt runs through it. The heartbeat timeout does: 400 ms for the longest 4 kB sector erase of a representative part plus one 50 ms control period, against 500 ms. That is 50 ms of margin resting on a data sheet for a part Espressif does not promise to fit.

The full contract is in hardware design section 19.4 and under question 2 of
firmware architecture section 9, in the same words. What is still open
between the two is in hardware design section 19.5.

## 14. Changes proposed and not applied

For whoever takes these up. Each is a recommendation above.

| Finding | Change to `hardware/netlist.yaml` |
|---|---|
| B1 | Section 3.4: add R89, C51, D22, R90, R91, R92. Remove D21, R13. R14 to 1 Mohm, R30 to 110 kohm. New nets `KB_N0`, `KB_N1`, `DRV_EN_RB`. GPIO42 becomes `DRV_EN_RB` in the pin map. |
| M2 | Add F4, same part as F1, between J1 pin 1 and `AC_L`. J1 pin 1 gets a new net, `AC_L_IN`. The specification's protection row changes from one fuse to two. |
| M3 | Either remove D21 and C50 and connect R13 from `DRV_EN` to `RLY_G2`, or add a diode from `RLY_ON` to `RLY_G1` and 1 uF across R12 with R12 at 47 kohm. If B1 is adopted, M3 follows from it: Q2 then holds for its own hold-up and Q1 still releases at once. |
| M6 | R1 to a pulse-rated 2512 part. Same value and footprint. |
| M7 | Add a TVS, SMBJ48CA, from `COM` to `GND`. |
| m4 | K1 coil+ and D5 cathode from `V5` to `V_BUCK`. |
| m5 | C101 to C901 pin 2 from `GND` to `ISNS`. |
| m7 | Add a Schottky, anode `GND`, cathode `SP_OUT`. |
| m9 | Add a 3.0 V open-drain supervisor on `EN`. |
| m10 | Pull resistors on the expander pins. |
| m12 | 0 ohm in series with `USB_DP`, `USB_DM` and `U0TXD`, with unpopulated capacitors on the USB lines. |
| m13 | U7 to 74HC132D. |
