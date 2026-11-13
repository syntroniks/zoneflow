# Fail-safe reduction study: independent review

Status: **review of a paper study, 2026-11-12. It changes nothing** in the
specification, the netlist, the board, the firmware or the enclosure. It
reviews the [fail-safe reduction study](failsafe-reduction-study.md) for the
owner, who will decide from the two documents. Nothing here has been built or
measured. Every claim below names the document I read or the run I made.

**Does a correction change the study's recommendation?** No correction
reverses it. The study's first conclusion stands: the proposal as quoted is
not a fail-safe. Its second, "V2 if the fail-safe is to shrink at all", is
weaker than it reads, because the one-chip detector has more single faults
that leave a valve on than the eleven-fault check found, and one of its
protective parts does less than the study says. Since the study was written
the owner has said that V1 "sounds promising". The study advises against V1's
main saving, the relay. This review therefore treats V1 at the same depth as
V2. The corrections to the study are dated notes inside it and are listed in
section 14.

## Summary

**What the owner asked for, and what the documents say.** The idea was to
tie the output expander's reset to the processor's reset and let the chip's
own watchdogs do the rest. The study says that does not work, and it is
right. I checked every link in Espressif's manual, the data sheets and the
source of the ESP-IDF version this firmware builds with. A crash, a watchdog
time-out and a restart reset the two processor cores and leave the pins
alone. The reset pin does not move and the pins keep driving. A valve would
stay open.

**V1 is the variant the owner now favours.** One chip watches the
keep-alive and resets the expander. There is no switch in the supply of the
opto LEDs, no relay and no fast over-current trip. Against the board on
`main` it has 31 soldered parts fewer, 282 where there are 313, and costs
about one dollar less per controller.

**What V1 means in the field.** With V1 there are faults after which one
valve waters until someone unplugs the transformer or shuts the water off.
No firmware and no restart can stop it. The controller can only report it,
because the current measurement still works. The faults are of four kinds:

- **An output part killed by a surge or an overload**: a triac, its TVS or its opto-triac shorted. The prior-art survey lists this as the most common field failure of such controllers. Nine outputs, three such parts each.
- **A valve drive pin held low**: an expander pin stuck low, or a valve drive net bridged to ground. In V1 the opto LEDs are fed from the 3.3 V rail all the time, so this opens the valve from the moment of the fault, with the controller idle.
- **An expander that does not obey its reset.**
- **A fault in the new watchdog chip or its timing parts** that holds the enable high. Then a valve stays open only if the firmware also stops during a run. A firmware that restarts can close the valve again through the expander, which still answers.

Revision A has an answer in hardware to every one of these. Section 6 lists
them all for V1, with how the owner would notice and how long the valve
runs.

**A shorted valve wire** no longer ends within 20 ms. It lasts until the
firmware reacts or the self-resetting fuse of that output opens. On a
transformer of 40 VA or more a board fuse may open first, by the design
document's own estimate. The controller is then dark until someone replaces
a fuse. The valves are off.

**The parts can be put back one at a time.** A decision on V1 needs this
table more than anything else in the review. Costs are per controller at 5,
25 and 100 pieces by the repository's cost model, which nobody has compared
with a real order.

| Put back | Covers | Parts | USD at 5 / 25 / 100 |
|---|---|---|---|
| Nothing: V1 as the study draws it | | 282 on the board | -1.01 / -0.86 / -0.76 against revision A |
| **The over-current comparator on the watchdog chip's clear input** (not in the study) | A shorted valve wire ends in one or two half-cycles without firmware, as today. No flag, no red LED | +2 | +0.04 / +0.02 / +0.04 |
| **The relay K1 on an expander bit**, with no detector of its own | A shorted triac, TVS or opto-triac, and a valve pin held low: the valve is off whenever no run is in progress, and firmware refuses the next run | +6 | +1.00 / +0.82 / +0.66 |
| The LED supply switch | An expander that ignores its reset, a valve pin held low | +5 | +0.35 / +0.23 / +0.21 |
| Firmware only: a limit on single current samples | A shorted wire, if the firmware runs. Does not exist today | 0 | 0 |
| Firmware only: an alarm when current flows with every output off | Tells the owner. Stops nothing | 0 | 0 |

**What I would tell the owner, in three sentences.** If the aim is fewer
parts, take V1 with the comparator and the relay put back: 23 parts fewer
than today, about the price of today's board, and still an answer in
hardware to the two failures an installer is most likely to meet, a shorted
wire and a triac killed by a surge. Bare V1 is more than most of the open designs in the prior-art
survey have, but it breaks safety rules 1, 3 and 6 of the specification as
written, so choose it only if the specification is rewritten to say that one
fault can leave a valve on. Whatever is
chosen, the fail-safe is a tenth of this board, so the part count will not
come from here, and the watchdog chip has to be measured on a bench before
any board depends on it.

**One thing concerns the board as it is today.** The reset supervisor U12
trips at 3.08 V, not at the 3.0 V the design document says. At the corner of
the tolerances there are 12 mV between the lowest 3.3 V rail and the level
at which the supervisor lets the processor start, and 44 to 59 mV before a
dip resets it. The design document says 90 and 140 mV. Section 11 has the
numbers, the choice and the text of an issue. It does not block ordering
bare boards. It should be settled before parts are assembled.

**Where this review is weak.** The fault injection uses the study's model of
the watchdog chip, and no data sheet describes the inside of that chip. The
rows that depend on it are marked. I selected no new part and laid nothing
out.

## 1. Method, and what I read and ran

I did not start from the study's sources. I downloaded the makers'
documents again, read the passages myself and compared them with what the
study says they say. Section 15 lists each document with its revision.

- **Reset behaviour.** ESP32-S3 Technical Reference Manual v1.8, chapters 6, 7, 8, 13 and 24. ESP32-S3 Series Datasheet v2.2. The module data sheet v1.8. ESP-IDF v5.5.6 at commit 880a203a, the files the study names, in the installed tree. esptool 4.13.0 as installed with it. The ESP32-S3 errata, which the study had not read. The configuration of a firmware build made from `sdkconfig.defaults`, to see that each option takes effect.
- **Parts.** Nexperia 74LVC1G123 rev 8 and TI SN74LVC1G123 SCES586E, complete. TI TLV803E SLVSES2J and TI TLV803 (SBVS157, as LCSC's copy ZHCS187C). ST LDL1117 (DocID030319 rev 3). TI TCA9539 SCPS202C and TI TPS3701 SBVS240C for the pins the reset path uses.
- **Stock and prices.** The JLCPCB parts search on 2026-11-12 at 20:08 UTC, for every part number of the study's section 4.2 and for the two supervisors of section 11.
- **The repository.** `system-spec.md` v0.7, the sections of `hardware-design.md` that the study cites (4.2, 4.3, 4.7, 6.6, 7, 8, 9, 10, 12, 19.2, 19.4), `hardware/netlist.yaml`, `firmware/zoneflow-fw/sdkconfig.defaults` and `components/zf_platform/src/safety.cpp`, issue #31.

What I ran:

- **`make -C hardware/sim/study-failsafe-reduction`**: the 349 runs of the study and its cost table. `git status` afterwards showed no difference in any result file. The study's numbers reproduce.
- **`make -C hardware/sim/study-failsafe-reduction review`**: my additions. `faults_wide.py` injects 114 faults into V2 and 88 into V1, each through seven runs, and one run of an option the study does not have. `review_options.py` prices V1 with one thing put back at a time. The files and their limits are in the `README.md` of that directory.
- **`uv run mkdocs build --strict`** and `python3 tools/docs/check_links.py site`, for this page.

What I did not do: measure anything, read the mask ROM, read a capacitor
data sheet, select a new part, or change a design file.

Severities in section 2: **blocking** means it must be settled before the
thing it concerns is built or ordered. **Major** changes what a variant can
claim. **Minor** is a correction that changes no conclusion. **Note** is
context.

## 2. Findings

| No. | Severity | Concerns | Finding | Evidence |
|---|---|---|---|---|
| R1 | note | The proposal as quoted | **The study's headline is correct.** `EN` is an input, no internal reset drives it, and with this firmware a panic, a task or interrupt watchdog time-out and `esp_restart()` are CPU resets through which the pins keep their state. | Section 4 |
| R2 | major | V1, V2, V3 | **The timing node of the one-shot fails in the unsafe direction.** A short, a leak of about 100 kohm or less across the timing capacitor, an open joint at pin 7, or the timing pin bridged to `3V3` each hold `DRV_EN` high after the keep-alive stops. The study found the short only. In the pump of revision A the same faults of the reservoir end drive. A cracked ceramic capacitor is a known way for the leak to arise. | Section 6, rows `ct_leak_*`, `wd_p7_open_holds`, `wd_p78_bridge`, `rt1_short`. Model |
| R3 | major | V1, V2, V3 | **The second supervisor U_SV is not shown to do its job where the study needs it.** The study fits it because the one-shot's output is not specified under 1.65 V. Under 1.7 V the TLV803E is guaranteed to sink 15 uA with its output at 0.3 V, and 0.5 mA at 1.7 V. Through R_S of 1 kohm a high one-shot output asks it for 1.2 to 1.6 mA. The model gives it a 30 ohm switch down to 0.7 V. | SLVSES2J section 7.5, rows VPOR and VOL; `study.py` lines 276 to 280 |
| R4 | major | V2 | **In V2, 14 rows of the fault table, eight distinct single faults, leave a valve on after the keep-alive stops, not two.** The study's count was of eleven faults it chose and it says so. In seven of the eight K1 stays closed too, because K1 hangs on the same signal. | Section 6.2 |
| R5 | major | V1 | **In V1, a valve drive pin held low opens the valve at once and for good, with the controller idle.** The opto LEDs are on `3V3` all the time. A stuck expander pin, a drive net bridged to ground, or 220 ohm of residue to ground gives 5.7 to 13.9 mA. The study's fault table has this as "on, for good" under a row about the expander's reset, and its summary does not name it. | Section 6.1, rows `u5_p00_low`, `zd_leak_*`, `u5_p34_bridge`. Circuit |
| R6 | major | V1, and the study's section 9 | **Without K1, a short on a 75 VA transformer may open a fuse whatever removes the gate drive.** The study's columns "with the latch" and "firmware, 30 ms" assume the triac stops at the first current zero. The design document does not rely on that on a large transformer and has K1 break the current. V1 has no K1. | Hardware design 6.6, "I do not rely on the triac stopping" |
| R7 | major, for revision A | The board on `main` | **The study is right about U12, and the margins are worse in one respect than it says.** Option "30" is 3.08 V. Release margin 12 mV, dip margin 44 to 59 mV. The fallback the design names does not reset the processor before its minimum supply of 3.0 V. | Section 11 |
| R8 | minor | The study's summary | **V1 saves 31 soldered parts, not 32.** JP1 is a pad pattern. The study's own section 7.1 says so; its summary table and the text under it count it. | `results/review-options.md` |
| R9 | minor | V2 firmware | **V2 still needs the gate bleed check, step 5b.** The study's firmware table drops it with detector B. R12 open still leaves the gate of Q1 charged, and K1 closed, when the expander is reset. | Section 6.2, row `r12_open` |
| R10 | minor | V2 | **A bridge across R25, or between pins 1 and 2 of Q3, makes Q_INV short `3V3` to ground whenever drive is enabled.** In revision A a gate output drives that node and limits the current. V2 needs a resistor in the drain of Q_INV. No valve runs with the fault. | Reading of the V2 circuit |
| R11 | minor | V1, V2 | **U_SV has the threshold of U12, so finding R7 applies to it.** At the same corner a dip of 44 to 59 mV resets the expander and drops the valves while the processor runs on. | Section 11 |
| R12 | minor | V1, V2 firmware | **A release test before a valve opens would find every fault of R2 before it matters.** The study tests the detector after the run. A few edges, then a stop, then the read-back must fall within 0.35 s: about half a second per run. The exposure shrinks from "a fault since the last run" to "a fault during this run". | Section 6.3 |
| R13 | note | An option the study lacks | **The over-current comparator can act on the clear input of the one-shot.** The net exists already and two parts stay. The expander's reset is the memory. | Section 7.3, `results/trip-via-clear.md` |
| R14 | note | The one-shot | **The Nexperia part has a retrigger time that grows with the capacitor**: 10 ms typical at 100 uF and 1 kohm, so about 0.5 ms at 4.7 uF if it scales, with no maximum given. The TI sheet prints 1.4 us for 100 uF and 1 kohm. Edges 10 ms apart are not affected. The model has no such dead time. | Nexperia rev 8, table 9; SCES586E section 5.6 |
| R15 | note | The one-shot | **The TSSOP-8 the study picks has one maker in stock.** JLCPCB lists two under "VSSOP-8-0.5mm": Nexperia C77273, 2,504 pieces, and TI C352969, 1,730 pieces. I did not compare the two package drawings. | JLCPCB parts search, 2026-11-12 |
| R16 | note | The pin | **GPIO17 is an RTC pin.** A Core Reset resets "the whole digital system except RTC". If firmware ever drove the pin through the RTC IO functions, a Core Reset would not release it. It does not today. The study's rule about the hold function needs this beside it. | Datasheet v2.2 table 2-1; manual v1.8 section 7.1.3 |
| R17 | note | Not established | **The errata have nothing on resets, watchdogs or pin states.** Eight entries, none relevant. | Section 12 |
| R18 | note | The proposal as quoted | **The time a crash leaves the pins driving is longer than a restart.** This firmware writes a core dump to flash inside the panic handler before it restarts. | `sdkconfig.defaults`; ESP-IDF `panic.c` lines 429 to 439 |
| R19 | minor | The study's parts lists | **Not in the lists**: a decoupling capacitor for U_SV, which U12 has (C54). Four test pads go with the removed nets: TP8, TP9, TP11, TP24. K1 is in the mechanical contract and has a part of the test-fit kit. | `hardware/netlist.yaml`, `hardware/mechanical.yaml` |
| R20 | minor | V1 | **In V1 the read-back sits on `DRV_EN` itself.** A GPIO42 driven high by mistake lifts `DRV_EN` to 0.58 V through R92 against R_S, which is safe. Any cure for R3 that makes R_S large makes this unsafe. The two have to be solved together. | Section 6.1, row `gpio42_high` |

## 3. Study claims, checked

"Confirmed" means I read the passage or made the run and it says what the
study says.

| Study claim | Verdict | Source |
|---|---|---|
| `EN` (CHIP_PU) is an input | Confirmed | Datasheet v2.2 table 2-10: type "I", "High: on, enables the chip (powered up). Low: off, disables the chip (powered down)" |
| No internal reset pulls `EN` low | Not established, as the study says. No document names a driver on the pin, and none says outright that there is none | Manual v1.8 table 7.1-1 lists 17 sources, all internal |
| Four reset levels, and what each resets | Confirmed, word for word | Manual v1.8 section 7.1.3 |
| `esp_restart()` is a CPU reset of both cores | Confirmed | `system_internal.c` lines 72 to 165; ESP-IDF `misc_system_api.rst`: "both CPUs are reset" |
| A panic restarts through the same function, and the digital-core restart is for the ESP32 only | Confirmed | `panic_handler.c` lines 321 to 330 |
| This firmware panics on a task watchdog time-out, 5 s | Confirmed in a built configuration | `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` |
| Interrupt watchdog: panic at 800 ms, Core Reset at the second stage | Confirmed | `int_wdt.c`; `CONFIG_ESP_INT_WDT_TIMEOUT_MS=800`; manual 13.2.2.2 |
| Brownout as configured: an interrupt handler makes a Core Reset | Confirmed. The option has no prompt in ESP-IDF, so I checked that it takes effect: it does | `brownout.c` lines 44 to 83; built `sdkconfig.h` |
| The RTC watchdog is switched off before the application starts | Confirmed | `startup_funcs.c` lines 180 to 189 |
| The super watchdog is set to feed itself | Confirmed | `bootloader_esp32s3.c` lines 126 to 131; manual 13.3.2.2 |
| esptool enters download mode by a Core Reset and switches two watchdogs off | Confirmed | Manual table 7.1-1, code 0x15; esptool 4.13.0 `esp32s3.py` lines 331 to 347 |
| Pins keep driving through a CPU Reset | Inferred, as the study says. I agree with the inference: the manual limits a CPU Reset to the core, and the restart code resets Wi-Fi, Bluetooth, timers, SPI, UART, PWM and the crypto blocks, and writes no GPIO output register | `system_internal.c` lines 34 to 66 |
| A Core Reset releases the pins | Confirmed for pins used as digital GPIOs. See R16 | Manual 7.1.3 and 6.9 |
| Power-up glitches: GPIO17 low for 60 us | Confirmed. The table is for power-up only | Datasheet v2.2 table 2-2 |
| LEDC is not reset by a restart | Confirmed: no LEDC bit in the reset masks | `system_internal.c` lines 44 to 62 |
| Any code can switch the watchdogs off; OpenOCD does | Confirmed for the write key. The OpenOCD sentence I did not read again | Manual 13.2.2.3 |
| One-shot: `tW = K x REXT x CEXT`, K = 1 above 10 nF, 1.0 ms typical and 1.05 ms at most at 10 kohm and 0.1 uF, no minimum | Confirmed | Nexperia rev 8, table 9 and note 3 |
| One-shot input thresholds, supply range, "power-on reset on outputs", the trigger by a rising clear input | Confirmed. TI adds "glitch-free power-up reset on outputs" | Nexperia rev 8 tables 4, 6, 8; SCES586E page 1 |
| Pulse 147 to 289 ms, 235 ms nominal | Confirmed by my arithmetic: 0.9 x 49.5 kohm x 3.3 uF = 147.0 ms, 1.1 x 50.5 kohm x 5.2 uF = 288.9 ms, 50 kohm x 4.7 uF = 235 ms. The ranges of K and of the capacitor are allowances, not data | Study 6.2 |
| Release 0.14 to 0.29 s after the last edge; 0.80 s after the control loop dies | Confirmed by the rerun; 0.5 s + 0.005 s + 0.29 s = 0.795 s | `results/release-summary.md` |
| U_SV: "its output is defined from a supply of 0.7 V" | True and incomplete. R3 | SLVSES2J 7.5 |
| "Two of the eleven faults stick this detector on" | Confirmed for the eleven. R2 and R4 for the rest | `results/faults.md` |
| Fifteen component faults stick detector A of revision A | Confirmed by counting the rows | Hardware design 8.5 |
| The fault currents, PTC times and I²t figures of section 9 | Confirmed against the design document: 7.2² x 0.6 = 31, 13.2² x 0.18 = 31, 21.9² x 0.07 = 34 A²s; fuse 20.16 A²s. The columns for 20 ms and 30 ms assume the triac stops. R6 | Hardware design 4.3, 6.6 |
| Prices and stock of the new parts | Confirmed today: C513291 3,442 pieces, 0.5578 USD at 1 and 0.3304 at 100; C5218924 7,125 pieces, 0.2372 at 100. The nine other part numbers of section 4.2 match to within two pieces of stock | JLCPCB parts search, 2026-11-12 20:08 UTC |
| The cost and area table | Confirmed by the rerun, except the part count of V1. R8 | `results/variants.md` |
| U12 trips at 3.08 V | Confirmed. R7 | SLVSES2J table 12-1 |
| "The margin under a 3.20 V rail is then about 10 mV" | Right for the release threshold. Before a dip resets the board there are 44 to 59 mV. Section 11 | SLVSES2J 7.5; DocID030319 table 4 |
| "The 2.93 V part would restore the margin" | Right on the upper side. Wrong as a complete fix: it trips below the processor's minimum supply. Section 11 | SBVS157 section 7.5; module data sheet table 6-2 |

**Where the study is too pessimistic.** Nowhere that I found.

**Where it is too optimistic.** R3, R6, and the words "found by the
read-back at the end of the run or before the next" for the stuck faults. A
fault of R2 that arises while the board is idle passes the idle test at the
next start, because the one-shot's output is low until its first trigger.
It is found at the end of that run. Section 6.3.

## 4. The reset path, link by link

The brief asks which reset level this firmware produces. From the
built configuration and the source:

| Event | What runs | Level | Pins |
|---|---|---|---|
| Exception, `abort()`, failed assertion | The firmware's own hook first, which drives the outputs off (`safety.cpp`, `__wrap_esp_panic_handler`). Then the core dump to flash. Then `panic_restart()` | CPU Reset | Keep their last state. The keep-alive pin was driven low by the hook |
| Task watchdog, 5 s | The same path as a panic (`CONFIG_ESP_TASK_WDT_PANIC=y`) | CPU Reset | The same |
| Interrupt watchdog, 800 ms | First stage: the same path. Second stage, 1.6 s later, if the handler cannot run: timer group reset | CPU Reset, then Core Reset | Kept, then released |
| `esp_restart()`, also after an update | Shutdown handler, which trips the guard and drives the outputs off. Then `esp_restart_noos()` | CPU Reset | Kept |
| Brownout seen by the chip, about 2.44 V | Interrupt handler, then `esp_rom_software_reset_system()` | Core Reset | Released |
| RTC watchdog in the bootloader or in a stuck panic handler | Hardware | System Reset | Released |
| USB flashing | The USB controller resets the chip on the host's command | Core Reset | Released |

So the study's table 2.2 is right for the configuration as built. Two
additions:

- **The memory protection option is on** (`CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=y`). In older versions of ESP-IDF that turned a panic restart into a reset of the digital core. In v5.5.6 it does not: `panic_restart()` has that branch for the ESP32 only. This is the point at which a newer or older ESP-IDF could change the answer, and it is worth a line in the firmware's notes.
- **The reset level of a crash is a property of the software version, not of the chip.** A design in which a valve depends on it would change behaviour with an update of ESP-IDF. That is one more reason against the proposal as quoted, and the study does not give it.

**The expander through a "CPU Reset".** With its reset on `EN`, the expander
sees nothing: `EN` does not move. With its reset on a circuit that needs
edges, the edges stop, because the restart code resets the timer groups and
the panic handler runs with interrupts off. Both statements of the study
are correct for this configuration.

## 5. V2 recomputed and the model read

**Timing.** The arithmetic is in section 3. Its inputs are allowances more
than data:

- **K has no limits.** The sheet gives K = 1 as a typical constant, one maximum of +5 % at one test point, and curves up to 0.1 uF. 0.9 to 1.1 is the study's allowance and it says so.
- **The capacitor range, 3.3 to 5.2 uF,** is the design document's allowance for the same part number, not a curve.
- **Leakage does not move it.** The TI sheet limits the current into the timing pin to 0.25 uA, which is 12 mV across 50 kohm. This supports the choice of 50 kohm and is not in the study.
- **The firmware's time-out is the larger part.** From a dead control loop to no drive is 0.80 s in V1 and V2 against 1.34 s in revision A. Of the 0.80 s, 0.5 s is the firmware's time-out.

**The model.** I read `study.py` line by line against the data sheets.

| Question of the brief | What the model does | Verdict |
|---|---|---|
| Retriggering | A trigger sets the output, discharges the capacitor to 15 % of the rail through 30 ohm, then lets it charge to the level that makes the width K x R x C. A trigger during the pulse discharges it again | Correct in kind. No dead time after a trigger (R14). The discharge takes 0.27 ms in the model, of the size the Nexperia figure suggests |
| Output during power-up and below 1.65 V | Two cases: low below 1.0 V, or high below 1.65 V | A fair bracket. TI states a glitch-free power-up reset, which supports the first case on the way up. Nothing covers a rail that falls with the output high |
| The pin in high impedance | A switch of 100 Gohm and R33 | Correct |
| Glitch pulses at reset | Not modelled. The static run has one edge and one pulse | Acceptable for GPIO17: its power-up glitch is a low level. One difference from revision A matters: any single rising edge gives a full pulse of 0.15 to 0.29 s, where revision A needs sustained edges. The study says so. With the expander empty nothing follows |
| The clear input | A rising clear input with the trigger input high starts a pulse | Correct, per the function table |
| The second supervisor | A 30 ohm switch, closed below the threshold, valid from 0.7 V | **Too strong below 1.7 V.** R3. The brownout row "falls to 1.5 V and stays", which the study gives as the reason for the part, is not closed by the data sheet |
| The expander | Bits are set if reset is above 0.7 x VCC and cleared below 0.3 x VCC | Conservative |

**What R3 means.** With `3V3` held between about 0.75 V and 1.65 V and the
one-shot's output high there, the supervisor is guaranteed to sink 15 uA and
the one-shot can source more than a milliamp through R_S. Whether `DRV_EN`
is low is then a property of typical parts, not of the data sheets. The
state is narrow: the 3.3 V rail has to rest there while 24 VAC is present.
A larger R_S would close it on paper, 110 kohm for 15 uA at 1.65 V, and
opens R20. I did not design a cure.

## 6. The wider fault injection

The study says in its section 10.2 that somebody has to repeat the fault
injection of review A2 on the new circuit. `faults_wide.py` does that on the
study's own deck, for V2 and for V1. V1 is that deck without the inverter,
Q3 and the relay, with the opto LEDs on `3V3` and the read-back on `DRV_EN`,
as the study's section 7.1 describes it.

Each fault is present from power-up and goes through three crashes with a
valve open (the pin stops low, high, high-impedance) and through four tests
a firmware could make. A valve counts as on while the opto LED current is
above 0.5 mA, the study's limit. The complete tables are
`hardware/sim/study-failsafe-reduction/results/faults-wide-v1.md` and
`faults-wide-v2.md`. The basis column says whether a row depends on the
model of the one-shot.

### 6.1 V1: every fault after which a valve stays on

88 faults, 82 simulated. 30 simulated faults leave the valve on. 21 of them
are single faults of a component, a pin or a net, which are twelve distinct
faults: the table has several leak values and names some nodes twice. The
other nine are the bridges from a push-pull net onto the keep-alive net and
two double faults. The list below groups them by
what the owner would see. "For good" means until the transformer is
unplugged or the water is shut off.

**Group A. An output part has failed short.** Not simulated: no model is
needed. Revision A and V2 open K1.

| Fault | The valve | How the owner notices | How long |
|---|---|---|---|
| A triac shorted T2 to T1 | On from that moment, whatever the controller does | Water outside the schedule. Firmware measures current with every output off and can show a fault, send it by MQTT and refuse to run. It cannot close the valve | For good |
| The TVS of an output shorted | The same | The same | For good |
| The output side of an opto-triac shorted | The same: the triac fires every half-cycle | The same | For good |
| A snubber capacitor shorted | About 0.3 A through 47 ohm, which may hold a valve open | The same, if the current is above the idle threshold of 30 mA | For good |

**Group B. A valve drive pin is held low.** Circuit, no model. Revision A
and V2 have the LED supply off at rest and K1 open.

| Fault | The valve | How the owner notices | How long |
|---|---|---|---|
| An expander valve pin stuck low, or a valve drive net shorted to ground | On from that moment: 13.9 mA in the opto LED | As group A | For good |
| A leak from a valve drive net to ground | 5.7 mA at 220 ohm, which fires the opto-triac for certain. 1.9 mA at 1 kohm and 0.95 mA at 2.2 kohm, which may. 0.46 mA at 4.7 kohm | As group A, if the valve opens | For good |
| Pins 3 and 4 of the expander bridged, `DRV_EN` to the drive net of zone 1 | 1.9 mA in the LED of zone 1 whenever the keep-alive is stopped. No valve runs when asked | The start test fails. Zone 1 may water | For good |
| The expander without supply or ground | Not established | | |

**Group C. The expander does not obey its reset.** Circuit, no model.
Revision A and V2 switch the LED supply off.

| Fault | The valve | How the owner notices | How long |
|---|---|---|---|
| The expander ignores its reset input | Stays on after the keep-alive stops | At the next start the expander answers with the keep-alive stopped, and the firmware reports it. That needs a firmware that starts | Until a firmware that runs writes the expander, or the 3.3 V rail is cycled. For good if the firmware stays down |
| The reset pin open and floating high, or opened during a run and holding its level | The same | The same; at the stop test if it opened during a run | The same |

**Group D. The enable stays high after the keep-alive stops.** A valve stays
on only if the firmware stops as well. Revision A opens K1 through its
second detector. V2 has no answer either.

| Fault | Basis | The valve after a crash | Found by | How long |
|---|---|---|---|---|
| The timing capacitor shorted, or pins 6 and 7 of the one-shot bridged | model | Stays on | The stop test, at the end of the first run after the fault. The pin-high test | Until a firmware that runs again writes the expander, which still answers. For good if none does: a hang with no reset, a restart loop, download mode. Today's firmware is not written for this case |
| A leak across the timing capacitor of 100 kohm or less | model | Stays on. At 150 kohm off after 0.58 s, at 220 kohm after 0.41 s, where 0.28 s is normal | The same | The same |
| Pin 7 open and the node inside holding its level | model | Stays on | The same | The same |
| The timing pin bridged to `3V3`: pins 7 and 8, or a bridge across R_T1 or R_T2 | model, not established | Stays on in the model, which cannot end a pulse before the capacitor is discharged. The sheets do not say what the part does | The same | The same |
| The output of the one-shot stuck high, or shorted to `3V3` | model | Stays on | The idle test | The same |
| `DRV_EN` shorted to `3V3`, or pins 2 and 3 of U_SV joined | circuit | Stays on | The idle test | The same |
| A leak from `3V3` to `DRV_EN` under about 2.3 kohm | circuit | Stays on: the one-shot pulls `DRV_EN` low through 1 kohm only. 1 kohm gives 1.65 V, above the expander's low level | The stop test | The same |
| The ground pin of the one-shot open | reading, not established | Count it as stays on | The idle test | The same |
| Both timing resistors open, a double fault | model | Stays on | The stop test | The same |

**Group E. The keep-alive net is fed by something else.** As in revision A,
with nearly the same limits.

| Fault | The valve after a crash | Found by |
|---|---|---|
| The net bridged to a push-pull output that toggles by itself, through up to 3.3 kohm, with the pin high-impedance | Stays on. At 4.7 kohm it does not | The float test |
| The same through 0 ohm with the pin driven low | Stays on | The idle test |
| The net bridged to `ENC_B` or `BTN_BACK`, its neighbours on the module | Off: a contact behind 13 kohm cannot reach the threshold | Not found, and harmless |
| The same with the 10 kohm series resistor shorted too, a double fault | Stays on with the pin high-impedance | The float test |
| The trigger input open and picking up a signal, or input A open with the pin stopped high | Not established. A floating Schmitt input is not in any sheet | |

**Group F. Firmware.** Firmware that is wrong and keeps the keep-alive
going holds a valve open in every variant and in revision A. No hardware in
this project answers that.

**Faults that cost protection without a symptom in V1**, each of which
matters with a second fault only: U13 unable to act (its output open, R13
open, R100 or C55 shorted, R53 shorted, the clear pin open and high), U_SV
unable to act, and R33 open. No test can raise or lower `3V3`, so nothing
reports the first two groups. This is as in revision A for U13.

**Everything else in the V1 table** ends with no valve running, which the
start test finds, or with no effect.

### 6.2 V2

114 faults, 103 simulated. 23 leave the valve on,
14 of them single faults, which are eight distinct ones. Seven are group D
of section 6.1: the LED supply switch covers groups B and C, and K1 covers
group A while no run is in progress. Group E is the same. The eighth is the
bridge between pins 3 and 4 of the expander, which behaves differently
here: the LED current of zone 1 flows into `DRV_EN` and holds it at about
1.8 V, enough to keep the inverter and with it the LED supply on. 1.9 mA
stays in that LED after the keep-alive stops. No valve runs when asked, so
the start test finds it.

In every fault of group D, K1 stays closed as well. The study says so in
its fault table ("K1 hangs on the same part and stays closed too"). So in
V2 a fault of group D together with a crash is a valve that stays on, and
with a shorted output device as a third fault there would be nothing left.

17 more faults leave something on with the valve off. They are the
ones a self-test has to find while the other path still works:

| What stays | Faults | Found by |
|---|---|---|
| The LED supply, with the expander reset | Q_INV shorted or its gate floating high, Q3 shorted or its gate floating low, R25 open, `DRV_EN_N` shorted to ground, `DRV_EN` joined to `DRV_EN_N` | The idle test: the read-back is high at rest. R25 open: the stop test |
| The expander's bits and K1, with the LED supply off | The expander ignores its reset; its reset pin open and high, or opened and holding | The expander answers with the keep-alive stopped |
| K1 alone | `RLY_ON` stuck high, Q1 shorted, its gate joined to its drain, floating high or opened and holding, R12 open | Not by the four tests of the injection. The weld check and step 5b, which measure current (R9) |
| The LED supply, fed through R92 | GPIO42 driven high by mistake, a firmware fault: 0.7 mA at most, as in revision A | The idle test |

**A valve pin stuck low** opens its valve whenever the keep-alive runs, so
also during the self-test and during the run of another zone, and the valve
closes when the keep-alive stops. Firmware finds it by current with K1
closed and every output off.

**Faults by reading that the deck cannot hold** (R10): R25 shorted, pins 1
and 2 of Q3 bridged, R26 or C21 shorted, and Q3 with gate and drain joined
each put `3V3` to ground through a MOSFET when drive is enabled. The board
resets at the first keep-alive edge, every time. No valve runs.

### 6.3 What the tests find, and when

The four tests of the injection are what the read-back on GPIO42 and the
expander's acknowledge allow. Three things follow that the study does not
say.

1. **The idle test does not see group D's timing faults after a power cycle.** The one-shot's output is low until its first trigger, whatever is wrong with the timing node. Only the faults that hold `DRV_EN` high directly show at rest.
2. **The stop test sees them, but after the run.** A fault that arose while the board was idle is found at the end of the next run. If the firmware stops during that run, the valve stays on. The study's sentence "a fault and a crash in the same run" should read "a crash in the first run after the fault".
3. **A release test before the valve opens finds them in time** (R12). Make edges for 50 ms, stop, and require the read-back low within 0.35 s, before anything is written to the expander. In V1 the same test proves the only layer there is, every run.

One timing resistor open doubles the pulse. With fixed limits the stop test
finds it at the long corner (0.59 s) and not at the short one (0.29 s). The
study's "found by nothing unless firmware compares with a stored time" is
right for half the range.

## 7. V1 in an installer's terms

### 7.1 What V1 gives up

| What happens in the field | Revision A | V1 | Cheapest single cover |
|---|---|---|---|
| **A triac, TVS or opto-triac fails short.** The usual end of an output after a surge on a long valve wire | K1 is open between runs, so the valve is off. At the next start firmware sees current with every output off, opens K1 and refuses to run | That zone waters from that moment, for good. The display and the app can say so | **K1 on an expander bit**, section 7.2 |
| **An expander pin sticks low, or a drive net is bridged to ground** | The LED supply is off at rest and K1 is open. Firmware finds it at the next start | That zone waters from that moment, for good | K1 on an expander bit, or the LED supply switch |
| **The expander ignores its reset** | The LED supply switch turns the valve off. K1 opens through detector B | A valve that was open at a crash stays open until a firmware that runs closes it | The LED supply switch |
| **A dead short on a valve wire** | Drive removed after 66 us, K1 opens, over within two half-cycles. Fuses hold. Latched, red LED | Firmware ends it after about 0.2 s, or the output's PTC after 0.07 to 0.6 s. See section 7.4 for the fuses | The comparator on the clear input, section 7.3 |
| **The watchdog circuit itself fails stuck** | The other detector turns the valve off in each of 57 such faults | Group D of section 6.1: with a crash in the same run the valve stays on | None short of a second detector, which is V3. The test of R12 narrows it for nothing |
| **The 3.3 V regulator shorts** | U13 removes drive | U13 clears the one-shot. The same, on a new path, by simulation | Kept in V1 |

What a master valve does for V1: where one is fitted, a zone that is stuck
on passes water only while the master is open. That is overwatering whenever
any zone runs, and not a flood. The specification does not require a master
valve, and its own output can fail the same way.

### 7.2 K1 on an expander bit

This is the relay of V2 without the LED supply switch: K1, D5, Q1, R11, R12
and the clamp D23 stay, six soldered parts and the jumper pads JP1. Q1 sits
on the expander bit `RLY_ON` with its source at ground. There is no second
detector.

- **What it covers.** Group A and most of group B. Firmware closes K1 only for a run. At rest, and after any reset of the expander, K1 is open and no valve has power, whatever the output parts and the valve pins do. A stuck valve pin cannot hold K1, because `RLY_ON` is another pin.
- **What it does not cover.** Groups C and D: an expander that keeps its bits keeps `RLY_ON` too. A valve stuck on during a run in which the firmware also dies.
- **Cost.** +1.00, +0.82 and +0.66 USD per controller at 5, 25 and 100 against V1, which puts V1 with the relay within 10 cents of revision A. K1 is the one through-hole part besides C1 that the builder may have to solder, and it is in the mechanical contract and the test-fit kit.
- **Firmware.** The weld check, step 5b and the check for current with K1 closed and every output off stay as they are.

### 7.3 The comparator on the clear input

Not in the study. In revision A the window comparator U6B and U6C pulls the
net `OC_TRIP_N` low when the current passes 4.0 to 5.4 A peak. The study
turns that same net into the clear input of the one-shot, with R53 as its
pull-up and U13 on it, and removes R52 and C33, the filter in front of the
comparator. If R52 and C33 stay, the comparator clears the one-shot.

- **What happens on a short.** The clear input goes low, `DRV_EN` falls, the expander is reset and forgets its bits. The opto LED is dark 0.15 ms after the trip in the run. When the current falls the comparator lets go and drive returns, 3 to 8 ms later, with nothing switched on. The expander's empty registers are the memory that the latch of revision A was. `results/trip-via-clear.md` has 16 runs: two variants, two corners, four trip lengths. The comparator and the current are not in that deck; `hardware/sim/isense.cir` has their 66 us.
- **What it costs.** Two parts that are on the board today, about 4 cents, no new net. U6 is on the board in every variant because its fourth comparator is the zero-cross detector, and the threshold chain R48 to R51 stays for the amplifier's bias.
- **What it lacks against the latch.** The red LED D8 and the flag on GPIO40. Firmware learns of the trip when it finds the expander's registers at their defaults with the keep-alive running. R87 kept as well would put the net on GPIO40, where an edge interrupt can record the pulse: one more part.
- **A rule for firmware.** An expander found reset during a run is a trip. Firmware must not write the outputs again at once, or it re-fires into the short every few milliseconds.
- **Not checked.** The low level of the LM2901 against the lowest falling threshold of the one-shot, 1.08 V. Revision A drives a 74HC132 from the same output, and I did not read the LM2901 sheet.

It applies to V2 and V3 as well.

### 7.4 A dead short without the latch: does a fuse open?

The fuses F1 and F4 are 3 A time-lag parts in holders, one in each
transformer leg, with a melting I²t of 20.16 A²s. The user replaces them
without soldering. Every current of the board passes through both, so an
open fuse leaves the controller dark, unless USB powers it, and the valves
off. The figures are the design document's estimates for a short at the
terminals. A short behind 30 m of cable is milder in every row.

| Transformer | Fault current, rms | Bare V1: what ends it | I²t | A fuse |
|---|---|---|---|---|
| 20 VA | 7.2 A | Firmware after two 100 ms windows | 10.4 A²s | Holds |
| 20 VA, firmware down | 7.2 A | The PTC after about 0.6 s | 31 A²s | May open |
| 40 VA | 13.2 A | The PTC after about 0.18 s, before the firmware | 31 A²s | May open |
| 75 VA | 21.9 A | The PTC after about 0.07 s | 34 A²s | May open |

With the comparator of section 7.3, or with a firmware limit on single
samples, gate drive goes within 20 to 30 ms, and the figures fall to 1.0 to
1.6, 3.5 to 5.2 and 9.6 to 14.4 A²s. **That assumes the triac stops at the
next current zero** (R6). The design document expects it to on a 20 VA
transformer and on a 40 VA one behind a cable, and says of a 75 VA
transformer shorted at the terminals that the triac "is likely to conduct
on". Revision A has K1 to break that current. V1 has not. So:

- **20 VA and 40 VA**: the comparator or a fast firmware limit keeps the fuses, by the design document's estimate.
- **75 VA, short at the terminals**: a fuse may open in V1 whatever removes the gate drive.
- **In every case the output survives by calculation**, with a margin of 1.8 to 3.1 in current, and that rests on a bench test that has not been run (risk 4 of the register).

The documents support "may open" and nothing firmer. The fuse's sheet gives 20 to
100 ms at 800 % of its rating and 0.2 to 3 s at 300 %, and the PTC times
above 8 A are scaled from one point.

### 7.5 The choices

Parts and cost are against revision A on `main`, 8-zone build, from
`results/review-options.md`.

| Choice | Soldered parts | USD at 5 / 25 / 100 | Faults that leave a valve on for good |
|---|---|---|---|
| Revision A | 313 | 0 | A bridge onto the keep-alive net |
| V1 | 282 | -1.01 / -0.86 / -0.76 | Groups A, B, C, D, E |
| V1 with the comparator | 284 | -0.97 / -0.84 / -0.72 | The same. A shorted wire no longer waits for firmware |
| V1 with K1 | 288 | -0.02 / -0.04 / -0.10 | C, D, E |
| V1 with the LED supply switch | 287 | -0.67 / -0.63 / -0.55 | A, D, E |
| **V1 with the comparator and K1** | **290** | **+0.02 / -0.02 / -0.06** | **C, D, E** |
| V1 with all three, which is V2 with the fast trip | 295 | +0.37 / +0.23 / +0.16 | D, E |

An active-high load switch in one package could replace Q3, Q_INV and R25,
and make the LED supply switch two parts. I did not select one, so it has
no number here.

## 8. Current sense in V1

**What the specification promises**: measure total solenoid current, and
detect an open valve, a shorted valve and loss of 24 VAC.

**What V1 touches.** Only the latch parts R52, C33, R54, D8 and R87. The
shunt R37, both amplifiers of U8, both ADC channels and the bias chain stay.
So every diagnostic that firmware makes today it can make in V1:

| Diagnostic | In V1 | Change |
|---|---|---|
| Open valve: no current with the output on | Works | None |
| Shorted valve or wire: current over the limit | Works, by firmware alone | It is now the only path, unless the comparator of section 7.3 stays. Firmware rule 19 lets valves run without current readings (hardware design 19.1). With no hardware trip behind it, that rule needs another look |
| Current with every output off | Works. The shunt is in the common return and each TVS returns to it | It becomes an alarm. In revision A it opens K1 |
| The noise floor "with the relay open" (hardware design 19.2) | No relay | Measure it with every output off. A leaking output device then raises the floor and cannot be told from noise below the idle threshold |
| The weld check | Gone with K1 | |

**The minimum circuit, if the dual range goes too** (issue #31 lists it
with the latch). One difference amplifier on the shunt, its ADC filter, and
a bias: R37, U8 with C25, four gain resistors and two capacitors, R46 with
C30, and the chain R48 to R51 with C32, which also makes the reference of
the zero-cross comparator. That removes the second channel, R42 to R45, C28,
C29, R47 and C31: eight parts and one ADC pin, 16, 12 and 14 cents. The
second half of U8 is wired as a follower and costs nothing.

Which gain to keep is the question:

- **With the comparator of section 7.3**, the amplifier has to be the low-gain one, because the trip level of 4.6 A peak needs its range. Then 50 mA, the smallest load of the brief, is 6.7 mV rms or 17 counts, and the idle threshold of 30 mA is 10 counts (hardware design 10.5 and 19.2). The data sheet gives no noise figure for the ADC. An open valve of 150 mA or more is still far from zero. A 50 mA load is not, and the idle threshold has to rise.
- **Without the comparator**, one amplifier with a gain near 3.3 covers the 1.5 A continuous limit and clips inrush and shorts, which firmware can read as "over range". 50 mA is then about 41 counts. This is my arithmetic from the figures of 10.3 and 10.5, not a design.

**My view.** Keep both channels in V1. Eight parts buy the resolution that
the open-valve and the all-outputs-off checks need, and the second of those
is the only thing in V1 that tells the owner about groups A and B. Decide
it on the bench, from the noise floor of the low-gain channel.

## 9. Specification sentences that V1 breaks

The study's section 7.5 has rules 3 and 6 and two rows of section 3.1. It
misses the first, the fourth, the fifth and the sixth row below. Nothing is
changed by this review. If V1 is chosen, the pull request that changes the
netlist changes these.

| Where | Today | With V1 |
|---|---|---|
| Section 1, diagnostics | "A shorted valve or wire must not destroy the output; the board shuts the zone off and reports it." | "A shorted valve or wire must not destroy the output; firmware shuts the zone off and reports it. On a transformer of 40 VA or more a fuse may open first, and the controller is then without power until the fuse is replaced." With the comparator of section 7.3 the first sentence can stay as it is, and the second becomes "On a 75 VA transformer..." |
| Rule 1 | "With the MCU in reset, unpowered, crashed or being flashed, every valve output is off. This is enforced in hardware, not only in firmware." | "With the MCU in reset, unpowered, crashed or being flashed, every valve output is off, unless an output device has failed short, a valve drive pin is held low or the output expander does not obey its reset. One circuit enforces this in hardware. Nothing backs it up." |
| Rule 3 | "...gate drive to every triac is removed within about one second, with no firmware involved. The same signal holds the output expander in reset, so no output state survives a restart. A second detector on the same keep-alive signal, sharing no part with the first, releases the relay of rule 6 in about the same time. No single component fault in either detector, or in the logic behind them, may leave a valve on after the keep-alive stops. Firmware tests both detectors before and after every run." | "A hardware output-enable holds the output expander in reset. Firmware must keep toggling a keep-alive signal; when the toggling stops (crash, hang, reset, flashing), the expander is reset within about half a second, with no firmware involved, so that no output state survives a restart. The opto-triac LEDs are fed from the logic rail and are dark only because the expander's pins are. A fault in the detector can leave the expander enabled, and a valve then stays on if the firmware stops in the same run. Firmware tests the detector before every run, so that such a fault is found before a valve opens." |
| Rule 4 | "Firmware enforces a per-zone maximum run time that a schedule or API call cannot exceed." | Unchanged, with a sentence added: "It does not hold after one of the faults named in rule 1." |
| Rule 6 | The whole rule | Removed. In its place: "There is no cut-off in the valve common. An output device that has failed short energises its valve until the transformer is disconnected. Firmware measures the current with every output off, raises an alarm and refuses to run." With K1 on an expander bit: "A relay feeds the COM terminals. It is closed only while the output-enable is active and firmware has asked for it. It removes power from every valve when an output device has failed short, which resetting the expander cannot do." |
| 3.1, output stage | "The opto-triac LEDs are powered through the fail-safe gate of section 2, not from an unguarded pin." | "The opto-triac LEDs are powered from the 3.3 V rail and switched by expander pins that the fail-safe gate of section 2 holds in reset." |
| 3.1, protection | "surge clamp on the valve common" | Goes with the relay: the valve common is then one leg of the AC input and shares its clamp. With K1 kept it stays |
| 3.1, current sense | "A hardware comparator on the same signal latches and removes gate drive without firmware." | Removed. With the comparator of section 7.3: "A hardware comparator on the same signal resets the output expander without firmware." |
| 3.1, valve common | "Relay in the feed to the COM terminals, held by its own keep-alive detector" | The row goes. With K1 kept: "Relay in the feed to the COM terminals, held by the output-enable" |
| 3.1, MCU module | "the valve outputs are on an I2C expander that the output-enable holds in reset" | Stays true |
| 3.1, logic supply | "A detector on the 3.3 V rail that removes the output-enable by hardware while that rail is above its range" | Stays true: U13 on the clear input |

Rule 2 and rule 5 are not touched. The statement "about one second" of rule
3 becomes "about half a second" because the one-shot is faster, and that is
the one sentence V1 improves.

## 10. The part count of V1, and issue #31

**Against the board on `main` after pull request #55**, 8-zone build, by
the bill of materials that the cost tool reads: 313 soldered parts today.

**Removed, 40 designators, 39 of them soldered parts:**

- **Keep-alive detector A**, 12: R29, C22, D7, C23, R30, R91, R31, R32, C24, R34, R35, R36.
- **The gates**, 2: U7, C20.
- **The LED supply switch**, 4: Q3, R25, R26, C21.
- **The relay detector B with its transistor and read-back**, 10: Q2, R89, C51, D22, R14, R90, C50, R93, R94, R95.
- **The relay**, 6 and the pads JP1: K1, D5, Q1, R11, R12, D23.
- **The latch**, 5: R52, C33, R54, D8, R87.

**Added, 8:** U_WD, a decoupling capacitor for it, R_T1, R_T2, C_T, R_S,
R_PD, U_SV. Seven of them are part numbers the board has already. The
one-shot is the one new line.

**Net: 31 fewer, 282 on the board.** 70 SMT joints fewer of 840. The study's
summary says 32 (R8). One part more if U_SV gets its own decoupling
capacitor (R19). Four of the 30 test pads go: TP8, TP9, TP11 and TP24. Two
processor pins come free, GPIO7 and GPIO40.

**In proportion.** Issue #31 records that the owner expects about half the
parts to go. V1 is a tenth. The same issue counts 99 parts in the output
channels.

**What the same re-layout would touch anyway**, from the candidates of
issue #31:

| Candidate of issue #31 | Same blocks as V1? | Comment |
|---|---|---|
| A watchdog IC in place of the discrete detector | It is V1 | "Re-do the single-fault analysis", the issue says. Section 6 is a first pass at it, on a model |
| The over-current latch and the dual-range current sense | Yes, block G beside U6 and U8 | The latch goes in V1. The second range is eight parts more, section 8 |
| The relay and the second detector | It is V1 | |
| The zone LED in series with the opto LED | The nets, yes: V1 moves all nine LED supplies from `V_DRV` to `3V3` | Not examined here. Two LED voltages in series on a 3.3 V rail need checking against the data sheets first |
| Resistor arrays | Partly: R_T1, R_T2 and R_PD are three 100 kohm | One array for three resistors |
| Moving `SENS_FLT_N` and `VARIANT_N` to processor pins, finding m11 of review A1 | Yes: GPIO7 and GPIO40 come free | They can then be read without the keep-alive running, and the procedure of hardware design 19.6 goes |
| The quad comparator U6 | Yes | In bare V1 one of its four comparators is used. A single comparator would be a new part type and saves no part |
| Debounce, sensor supply, snubbers, opto-triacs | No | |

## 11. U12: the supervisor's threshold

This concerns revision A as it is, whatever is decided about the fail-safe.

### 11.1 What the documents say

**The part.** U12 is a TI TLV803EA30DBZR, LCSC C5218924. From TI SLVSES2J
(revised May 2021):

- **Threshold.** Table 12-1 decodes option "30" as 3.08 V. The orderable list has no 3.0 V option between "29", 2.93 V, and "30".
- **Accuracy.** Section 7.5: -2 to +2 % from -40 to 125 °C, 0.5 % typical.
- **Hysteresis.** 0.9 / 1.2 / 1.5 % of the threshold.
- **Low supply.** The output is controlled from 0.7 V.
- **Speed.** 10 us of glitch immunity at 5 % overdrive, 30 us typical and 50 us at most from the threshold to the output.

**The rail.** U3 is an ST LDL1117S33R. From DocID030319 rev 3, table 4: -3
to +3 % from -40 to 125 °C at 10 mA, -2 to +2 % at 25 °C, and a load
regulation of 5 mV typical and 15 mV at most from 10 mA to 1.2 A.

**The processor.** The module needs 3.0 to 3.6 V (module data sheet v1.8,
table 6-2). Its flash needs 2.7 V (table 6-8). Espressif's hardware design
guidelines ask for a power monitor with a threshold "around 3.0 V".

### 11.2 The numbers

| | Hardware design 12.4 | From the data sheets |
|---|---|---|
| Falling threshold | 3.0 V, 2.94 to 3.06 V | 3.08 V: 3.08 x 0.98 = 3.018 V to 3.08 x 1.02 = 3.142 V |
| Highest release level | 3.11 V | 3.142 x 1.015 = 3.189 V |
| Lowest rail | 3.20 V | 3.3 x 0.97 = 3.201 V at 10 mA. 3.186 V with the full load regulation |
| **Rail above the release level** | **90 mV** | **12 mV** |
| **A dip that resets the board** | 140 mV ("a dip that reaches 3.06 V") | **59 mV at light load, 44 mV at full load** |
| Trip above the processor's 3.0 V minimum | none, by its own figures: 2.94 V | 18 mV at least |

With typical parts the dip margin is 3.30 - 3.08 = 220 mV. The small
figures need a regulator at the bottom of its range and a supervisor at the
top of its own. The two tolerance bands touch: a window of 200 mV between
3.0 and 3.2 V has to hold a part that takes 123 mV for its accuracy and
47 mV for its hysteresis.

**So the study is right**, and its "about 10 mV" is the release margin. The
figure that decides whether a dip resets the board is the 44 to 59 mV.

### 11.3 The consequence

- **A reset on a supply dip.** A WiFi transmit burst steps the load by up to 340 mA (hardware design 4.2). The design document expects dips of "tens of millivolts" and calls it a bench item. Tens of millivolts are now the whole margin at the corner, where they were a fraction of it.
- **A reset is safe and not harmless.** The valves close. The board restarts in a few seconds and a run is lost. If the dip comes with every WiFi association, the board restarts in a loop and does not water. That is an outage, on some boards and not on others.
- **A board that starts late.** With 12 mV between the rail and the release level, a board at the corner may come out of reset late or not at all.
- **A bring-up step that fails on a good board.** Step 2 of hardware design section 21 expects `EN` to fall "when `3V3` passes 2.94 to 3.06 V". A typical part trips at 3.08 V.
- **In V1 and V2** the second supervisor has the same threshold (R11).

### 11.4 The fix, and its other side

The design document names the fallback: "The 2.93 V version, TLV803SDBZR,
C132016, is the fallback and fits the same pads." I read that part's sheet
and that of the 2.93 V option of U12's own family.

| | TLV803EA29DBZR | TLV803SDBZR |
|---|---|---|
| LCSC, stock at JLCPCB, 2026-11-12 | C1852114, 1,211 | C132016, 34,997 |
| USD at 100 | 0.31 | 0.14 at 150 |
| Family, document | That of U12, SLVSES2J | The older TLV803, SBVS157 |
| Pins in SOT-23 | 1 GND, 2 RESET, 3 VDD: as U12 | 1 GND, 2 RESET, 3 VDD: as U12 |
| Falling threshold | 2.871 to 2.989 V | 2.87 to 2.99 V |
| Release level, highest | 3.034 V | about 3.03 V: 40 mV typical, no maximum given |
| Rail above the release level | 167 mV | about 170 mV |
| A dip that resets the board | 212 mV, 197 mV at full load | 211 mV, 196 mV |
| **Trip below the processor's 3.0 V minimum** | **by 11 to 129 mV** | **by 10 to 130 mV** |
| Above the flash's 2.7 V | 171 mV at least | 170 mV at least |
| Output controlled from | 0.7 V | 1.1 V |
| Glitch that it answers | 10 us at 5 % overdrive | 1 us typical at 8 % |
| Delay after recovery | 130 / 200 / 270 ms | 120 / 200 / 280 ms |

**Both fit the pads and both restore the upper margin. Neither resets the
processor before its supply leaves the recommended range.** Between 2.87 and
3.0 V the module runs outside table 6-2 with no reset. No standard option of
either family sits inside the window with its tolerance, so there is no
drop-in part that meets both sides by data sheet. The choice is which side
to give up:

- **Keep the 3.08 V part.** It always resets above 3.0 V. Correct the documents, and measure on each board at bring-up the rail and the level at which `EN` falls. A board with less than, say, 100 mV between them gets the other part. No change to the bill of materials.
- **Fit the TLV803EA29DBZR.** The margin against dips is then 197 mV or more on every board. The processor may see 2.87 to 3.0 V before it is reset. What that risks is what Espressif names for an unstable supply: a start that fails, or a flash erase that does not complete. The flash itself is inside its range. The valves do not depend on it: the keep-alive fail-safe needs no supervisor. TI's own example in the same sheet pairs this option with a 3.3 V rail (section 9.2.2).

**My recommendation: the TLV803EA29DBZR.** It is the same family, so every
other figure of 12.4 stays true, and 1,211 pieces are enough for the batches
the specification names. A reset that comes 130 mV late on a falling rail
costs less than a board that restarts on its own transmit bursts. If a
reset above 3.0 V on every part is held to be required, it takes a
supervisor of about 1 % or a regulator of about 1 %, which is a change to the
board and not to one line. I selected neither.

**Does it block a board order?** Not the bare boards: the pads are the
same. It should be decided before the parts are assembled, because it is
one line of the bill of materials then and a rework afterwards. The
documents are wrong either way and need the correction.

### 11.5 Issue text, ready to post

Title: `U12 trips at 3.08 V, not 3.0 V: margins in hardware design 12.4 are wrong`

```text
Hardware design 12.4 gives the supervisor U12 (TLV803EA30DBZR, C5218924) a
falling threshold of "3.0 V, 2.94 to 3.06 V", a release at 3.11 V at most
and 90 mV under the lowest 3V3 rail. The TI data sheet says otherwise.

From TI SLVSES2J (May 2021):
- Table 12-1: threshold option "30" is 3.08 V. There is no 3.0 V option.
- Section 7.5: accuracy -2 to +2 % over temperature, hysteresis 0.9 / 1.2 / 1.5 %.

From ST DocID030319 rev 3, table 4, for U3 (LDL1117S33R): -3 to +3 % over
temperature, load regulation 15 mV at most.

Corrected numbers:
- Falling threshold 3.018 to 3.142 V (3.08 V -2 / +2 %).
- Release level up to 3.142 x 1.015 = 3.189 V.
- Lowest rail 3.3 x 0.97 = 3.201 V, 3.186 V with the full load regulation.
- Rail above the release level: 12 mV, not 90 mV.
- Dip that resets the board: 59 mV at light load, 44 mV at full load, not 140 mV.
- With typical parts: 220 mV.

Consequences:
- At the corner of the tolerances a WiFi transmit burst can reset the board.
  The valves close, so it is safe, but a board that does this at every
  association restarts in a loop and does not water.
- Bring-up step 2 (section 21) expects EN to fall at 2.94 to 3.06 V. A good
  board trips near 3.08 V and fails the step as written.

Proposed change (one BOM line, same pads, no board change):
- U12: TLV803EA29DBZR, LCSC C1852114, 1,211 in stock at JLCPCB on 2026-11-12,
  0.31 USD at 100. Same family and data sheet, same pinout (1 GND, 2 RESET,
  3 VDD). Falling threshold 2.871 to 2.989 V, release up to 3.034 V.
  Rail above release 167 mV, dip margin 197 to 212 mV.
- Known cost: it trips 11 to 129 mV below the module's 3.0 V minimum supply
  (module data sheet v1.8, table 6-2). The flash minimum of 2.7 V
  (table 6-8) keeps 171 mV. The part 12.4 names as fallback, TLV803SDBZR
  C132016, has the same threshold range and is the second choice: 34,997 in
  stock, output valid from 1.1 V instead of 0.7 V.
- Alternative with no BOM change: keep the 3.08 V part, correct the
  documents, and screen each board at bring-up for at least 100 mV between
  the rail and the level at which EN falls.

To change in the same pull request:
- hardware/netlist.yaml: U12 value and note ("supervisor 3.0 V ...").
- hardware-design.md 12.1 table, 12.4 table and text, section 21 step 2,
  section 23 sources. 12.3 quotes the release delay, which does not change.
- parts.toml and the cost data for the new LCSC number.

Not blocking for bare boards. Decide before assembly is ordered.

Source: docs/design/failsafe-reduction-review.md, section 11.
```

## 12. What the study left "not established"

| Item | Can a document settle it? | What I found |
|---|---|---|
| N1, `EN` stays high in every internal reset | No | The data sheet types the pin as an input and the manual lists no reset that acts on it. The documents say no more. Bench |
| N3, glitches on resets other than power-up | No | Table 2-2 is headed "power-up glitches". Bench |
| N5, pins through a CPU Reset | Partly | The restart code and the manual agree, and ESP-IDF documents the reset as "both CPUs are reset". The mask ROM is not published. Bench, and it is one scope shot |
| N9, a stopped crystal | No | The clock glitch detector acts on pulses under 3 ns, by the data sheet's section 4.1.4.8 and the manual's chapter 24. Nothing covers a clock that stops |
| N11, what the super watchdog still detects | No | The manual says it "can also feed" itself "without any interaction with CPU". Nothing says what then makes it fire |
| N13, latch-up | No | |
| N14, watchdogs in download mode | Partly | The manual enables the timer-group and RTC watchdogs "during the flash boot process" (13.2.2.4) and esptool switches the RTC and super watchdogs off because they "can then reset the board during flashing". So after a USB reset the chip may leave download mode by itself. For a valve that changes nothing: neither path writes the expander |
| The errata, "not read" | Yes | Read: CACHE-126, RTC-126, ANALOG-160, LCD-239, USBOTG-4289, RMT-176, TOUCH-100, ADC-183. None concerns a reset, a watchdog, a brownout or the state of a pin. RMT-176 is one more reason not to make the keep-alive with RMT, which the contract forbids already |
| W1, the lowest K | No | No minimum in either maker's sheet |
| W2, the one-shot under 1.65 V | Partly | TI states "glitch-free power-up reset on outputs". That covers a rising supply for the TI part. A falling supply with the output high is in no sheet |
| W4, W5, W6 | Not needed for V1 or V2 | Not followed |
| C1, a fuse on a dead short | No | Section 7.4 has what the sheets give |

New items from this review: the one-shot with its timing pin tied to the
rail, with an open ground and with an open trigger input, and the sink
current of U_SV between 0.7 and 1.7 V. Each is a bench measurement on a
handful of parts.

## 13. Recommendation

**The proposal as quoted: do not build it.** I agree with the study without
reservation.

**Revision A.** It is the only variant with a completed fault analysis, a
layout and a self-test written against it. If the first boards are to be
ordered soon, they should be revision A with the U12 question settled.
Issue #31 records that decision already.

**If the fail-safe is to shrink before the first order**, in the order I
would choose:

1. **V1 with the comparator and K1** (section 7.5): 290 parts, 23 fewer than today, the cost of revision A within a few cents. It keeps a hardware answer to a shorted wire and to a shorted output device, and to a valve pin held low. It gives up the second detector, the LED supply switch, the red LED and the fault flag. A valve can stay on after a fault in the one-shot or in the expander together with a firmware that stops.
2. **V2 with the comparator** if five more parts are acceptable: it adds the LED supply switch and with it the answer to an expander that keeps its bits.
3. **Bare V1** only with the specification rewritten as in section 9 and the owner's acceptance of groups A to D in writing. Most of the open designs in the prior-art survey have less than bare V1. It is not the product the specification describes today.

**Not V3.** The study is right that ten parts do not justify a new part
type and a new analysis.

**Conditions for any variant with the one-shot:**

- **Bench first.** The pulse width, the supply behaviour and the faults of section 12 on real parts, before a board depends on them. The study's section 11 has the list, and this review adds four items.
- **The release test before every run** (R12).
- **R3 and R20 solved together**, or the low-rail case accepted as a named risk.
- **A resistor in the drain of Q_INV**, if the LED supply switch stays (R10).
- **The VSSOP-8 footprint**, if the two makers' drawings agree: it has two sources in stock (R15).

**Where I differ from the study.** It recommends V2 and would decide the
latch later. I would keep the fast trip in every variant, because section
7.3 gets it for two parts, and I rate the one-chip detector lower than the
study does, because of R2. On K1 we agree. The study's weakest point, by its
own account, is that the saving is small. That has not changed. These
variants differ in what the specification can promise. In part count the
three I list remove 18 to 31 of 313.

## 14. Corrections made to the study

Each is a dated note in
[`failsafe-reduction-study.md`](failsafe-reduction-study.md), marked
"Review, 2026-11-12". The original text stands beside it.

| Where | What the note says | Finding |
|---|---|---|
| The top | That it has been reviewed, and that no correction reverses its recommendation | |
| Summary table and the text under it | V1 is 31 parts, not 32 | R8 |
| Summary, "the second layer" | The wider injection, and what "the same run" means | R4, section 6.3 |
| Section 2.2 | GPIO17 is an RTC pin | R16 |
| Section 4.2 | The retrigger time; the VSSOP-8 | R14, R15 |
| Section 5, fault table | A valve pin held low in V1; the shorted wire without K1 | R5, R6 |
| Section 6.1 | What U_SV is guaranteed to sink | R3 |
| Section 6.4 | The timing node; one resistor open | R2 |
| Section 7.6 | Step 5b stays in V2; the release test | R9, R12 |
| Section 9 | The 20 ms and 30 ms columns assume the triac stops; the comparator on the clear input | R6, R13 |
| Section 12 | The errata have been read | R17 |
| Section 13 | The two margins of U12, and the fallback's other side | R7 |

## 15. Sources

Read on 2026-11-12. PDFs downloaded from the makers except where a copy is
named.

**Espressif.**

- ESP32-S3 Series Datasheet, v2.2. Tables 2-1, 2-2, 2-10. Sections 4.1.4.8, 5.
- ESP32-S3 Technical Reference Manual, v1.8. Sections 6.9, 7.1, 8.2, 13.1 to 13.3, 24. Table 7.1-1.
- ESP32-S3-WROOM-1 and WROOM-1U Datasheet, v1.8. Tables 6-2, 6-8.
- ESP32-S3 Series SoC Errata, the "latest" release of the online document: the summary and every description.
- ESP Hardware Design Guidelines, ESP32-S3, schematic checklist, the "latest" release: the note on CHIP_PU and a power monitor.
- ESP-IDF v5.5.6, commit 880a203ae603d08a94d06aa01e0d6761958e5e9e: `esp_system/port/soc/esp32s3/system_internal.c`, `esp_system/port/panic_handler.c`, `esp_system/panic.c`, `esp_system/int_wdt.c`, `esp_system/task_wdt/task_wdt.c`, `esp_system/startup_funcs.c`, `esp_hw_support/power_supply/brownout.c` and its `Kconfig.power`, `bootloader_support/src/esp32s3/bootloader_esp32s3.c`, `docs/en/api-reference/system/misc_system_api.rst`.
- esptool 4.13.0: `reset.py`, `targets/esp32s3.py`.

**Parts.**

- Nexperia 74LVC1G123, rev 8, 14 August 2023, LCSC's copy for C513291. nexperia.com refused the download. Tables 3 to 10, figures 10 to 16.
- TI SN74LVC1G123, SCES586E, revised March 2024. Sections 4, 5.5, 5.6, 7, 8.
- TI TLV803E, TLV809E, TLV810E, SLVSES2J, revised May 2021. Sections 5, 7.5, 7.6, 8.4, 9.2, table 12-1.
- TI TLV803, TLV853, TLV863, SBVS157, as LCSC's copy ZHCS187C for C132016. Sections 5, 6, 7.5, 7.6. The headings of that copy are in Chinese and its tables in English.
- ST LDL1117, DocID030319 rev 3, LCSC's copy for C435835. Table 4.
- TI TCA9539, SCPS202C. The input levels and the reset pulse duration in section 6, and section 8.3.2.
- TI TPS3701, SBVS240C. Not read beyond its first pages for this review.

**Stock and prices.** The JLCPCB parts search, 2026-11-12, 20:08 UTC:
C513291, C123302, C5218924, C132016, C1852114, C20917, C2066812, C130064,
C5152526, C20032, C7719, C69492, C46043, C93238, C77273, C352969.

**This repository**, at commit 14012e8: the documents and files named in
section 1.

**Not read**: the LM2901 sheet, a data sheet for the 4.7 uF capacitor, the
fuse and PTC sheets (their figures are the design document's), the mask
ROM.
