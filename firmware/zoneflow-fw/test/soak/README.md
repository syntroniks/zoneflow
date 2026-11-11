# System soak simulation

`zf_soak` runs the firmware's framework-free layers for simulated weeks
against the simulated rev A board and a model of the world around it, and
checks invariants on every simulated tick. It is deterministic: a seed and
the options fix every event. It is a simulation. Nothing here has run on a
board, and it counts as none of the bench checks of system spec section 8.

```
test/soak/run.sh --ci                         # the subset CI runs (a few minutes)
test/soak/run.sh --long --jobs 16             # the long run (hours)
test/soak/run.sh --mode chaos --seeds 1-200 --days 30
test/soak/run.sh --mode chaos --seed 17 --days 30 --shrink
test/soak/run.sh --mode chaos --seed 17 --days 30 --trace
ZF_SOAK_SANITIZE=0 test/soak/run.sh ...       # without ASan and UBSan, about 3x faster
```

`run.sh` builds an optimised binary with ASan and UBSan (`build-asan/`, or
`build/` without them) and passes its arguments on. Seeds run in parallel
processes (`--jobs`, default: every core). The summary lists, per mode, the
seeds, simulated days, control ticks, the slowest tick, every violated
invariant with the seeds that hit it and one example, and the counters.
`--traces` prints the events before each seed's first violation.

## What runs

| Rig | Firmware code | Glue (as on the device) |
|---|---|---|
| Standalone (`standalone.cpp`) | `Controller` with `Scheduler`, `ZoneDriver` (zero-cross switching on in 30 % of configurations, on a modelled AC_ZC), `CurrentMonitor`, `CurrentSense`, sensors and `AcMonitor`; `hw::OutputStage` with its self-test, commissioning and `BlobReferenceStore`; `OutputEnableGuard`; `ConfigStore`; `PersistMailbox` and `persistWriteOnce` (the runtime record and the run budget, two copies each); `HistoryLog`; `TimeService`; `RetainedRecord`; `web::Api` and `AuthService`; `mqtt::parseCommand`, `PendingEdits`, `RateLimiter` | `app_main.cpp` boot order; the control task (8 commands, `setTimeStatus`, `tick` every 50 ms); `zf_persist` every 250 ms; `zf_history` with its 32-entry queue; `zf_isense` windows every 100 ms, with `VIN_MON`; `zf_mqtt`'s inbox of 8 messages, drained every 250 ms by `handleMessage`, and coalesced edits; the keep-alive interrupt every 5 ms |
| ESPHome (`esphome.cpp`) | `zfe::Supervisor` from `firmware/esphome/components/zoneflow`, with the same output stage, driver and current code; the `zoneflow_schedule` component's `schedule_logic` with two random programs | `zoneflow.cpp`'s retained record in RTC memory and its run budget in flash, a 50 ms tick; the schedule evaluated once a second; the ESPHome sprinkler component is played by a timer that starts zones and stops them when their time is up |

The board is `test/host/fake_board.hpp` (both detectors with the corner
release times of review A2, the latch, a register-level TCA9539 in reset
while `DRV_EN` is low, the relay). It outlives a firmware boot: after a
restart its keep-alive pin belongs to the new firmware's guard, and while
no firmware runs the pin is low.

## The world (`world.*`, `scenario.*`)

| Part | Model |
|---|---|
| Valves | 150 to 450 mA each, three times that for 100 ms of inrush; flow 3 to 23 l/min each |
| Wiring faults | open, short (6 A), over-current (1.25 A), intermittent (on and off every 20 ms to 3 s), half-wave (one triac polarity), from a minute to days |
| Board faults, one at a time | every fault `fake_board.hpp` models: either detector stuck on, on with the pin high, never on, releasing late; latch stuck; relay welded; one triac failed short; expander ignoring reset or never active; Q3 shorted; `V_DRV` never on; R12 open; `B_RB` stuck low; Q2 gate-drain short; the keep-alive net disconnected or bridged to a toggling or a high net (strong and weak); an expander pin stuck low or high; the sensor supply fault. Appear at random, repaired after hours to days, then a person acknowledges the repair |
| Detector corner | typical, latest release, earliest release, slow charge (review A2), per run |
| Comparator | trips at a peak of 4.0 to 5.4 A per run (hardware design 19.3 item 12) |
| Transformer and mains | 50 or 60 Hz; sags to 17 to 22 V; 32 V high line; drop-outs of 20 ms to 2 h; zero-cross bridged to the keep-alive (100 Hz) |
| ADC | windows of 500 samples per channel, computed from the waveform, clipped at 1.75 V, biased 740 to 840 mV, noise 0.3 to 15.3 mV; calibration lost for a second to a day |
| I2C | transient no-acknowledge and timeout, a stuck bus, corrupted reads of a register |
| Sensors | rain contact wet for hours with bounce at both edges; leaks with every valve shut; flow meter at 450 pulses per litre |
| Flash (NVS) | every write keeps the flash busy 1 to 8 ms, 30 to 60 ms for 11 % of writes, 100 to 400 ms for 1 %, and stalls the control task for that long. A write in progress at a reset or power loss leaves the old value (60 %), the new one (25 %) or a torn or bit-flipped record (15 %, beyond what NVS promises). Bit rot in a stored value |
| RTC memory | kept by a software reset (torn by 5 % of resets), random after power-on |
| Clocks | RTC drifting by tens of ppm, stopping, losing its time; system clock kept by a software reset, 1970 after power-on; SNTP every hour while WiFi is up; someone setting the clock by hand, up to three days wrong |
| Firmware halts | the control task hangs (the keep-alive interrupt runs on; the task watchdog restarts it after 5 s); the processor freezes with the keep-alive pin high or low for 0.3 to 8 s, then a watchdog reset |
| Restarts | software restarts (API, update, panic), brownouts, power cuts of 0.2 s to 6 h, and restarts 5 ms to 3 s after a scheduled start |
| Users | display commands, REST calls through `web::Api` (runs, stops, queue, rain delay, faults, repair, zone and program edits, whole configurations, `PUT /time`, reads of status, queue, faults and history), MQTT messages through `parseCommand` (valid, invalid, retained, oversized, empty); floods of 20 to 200 at once through every interface; configuration edits (whole documents, time zone only, limits), 15 % of them invalid (out of range, newer schema, truncated, wrong type) or 200 levels deep; maintenance on and off |
| Time zones | UTC, Berlin, London, New York, Los Angeles, Sydney, Auckland, Santiago (changes at 24:00), Lord Howe (a 30-minute DST), St John's, India, Nepal, Brazil |

## Invariants

Checked every 5 ms (physical) or after every control tick (firmware state).
Each traces to a requirement.

| Id | Invariant | Requirement |
|---|---|---|
| S1 | An output is energised only while the firmware commands it, or for at most 2.5 s while it finds and stops a fault. A failed-short triac that waters during another zone's run must be stopped within 2.5 s too (`S1-shorted-output`; gap 2, closed); counted, not failed, only when it began within 1.5 s of an output change or without a usable current measurement (`gap.shorted_output_in_settle_or_unmeasured_ms`) | Spec 2 rules 1, 3, 6 |
| S2 | After a halt, no output is energised later than the last edge on the keep-alive net plus the hold-up of a working detector layer at the net's level (0.83 s low, 1.25 s high for gate drive; 1.45 s and 2.19 s for the relay), with any one board fault present (a strong bridge of the net to a self-toggling net defeats both layers and is counted, not checked); and while the timer interrupt runs, no keep-alive edge later than 505 ms after the last heartbeat. A reset that pulls a frozen high pin low adds an edge (counted) | Spec 2 rule 3, hardware design 19.4 |
| S3 | At most `driver.max_concurrent` zones commanded and energised; never more than three valve bits driven by any write | Spec 1, hardware design 19.1 rule 3 |
| S4 | A zone's commanded on-time, added over runs until it has been closed for the minimum rest, never exceeds its maximum run time plus 120 ms and one tick per restart, across software restarts. Energised time after a halt or a stop by the keep-alive is counted (`s4.halt_tail_ms`, `s4.uncommanded_tail_ms`), bounded by S1 and S2. Across a power cycle or a torn retained record too (`S4-budget-power-cycle`; gap 3, closed: the budget is in flash as well) | Spec 2 rule 4, architecture 20 |
| S5 | A zone is never open while locked out, while the master it needs is locked out, in maintenance, without 24 VAC, or without the output stage in `Ready` | Architecture 3, fault paths |
| S6 | The relay closes at least 20 ms before the first valve and never onto a driven valve; no two outputs switch on within 100 ms; while the keep-alive runs, the relay is released 40 ms after the last valve and never under one; on the fault path no single-register write releases the relay or some outputs while others stay driven (`S6-fault-path-split`; gap 6, closed: one pair write) | Hardware design 19.1 rules 4 and 10 |
| S7 | A zone the driver has open is open in the published state snapshot, and in `GET /api/v1/status` (sampled every 10 minutes of a run) | Display and API report what is driven |
| S8 | Every scheduled start is started, skipped with a reason or reported missed, once: no start runs twice, none is reported missed after it ran, none is lost. Local time from glibc, day rules written again here, not the firmware's code. Exclusions, each counted: a program edited within 70 s, 25 h after a time zone change, a program with no stored key after boot (policy 5), starts older than a day (policy 1). After the clock is put back by less than a day, a start it passes again is reported missed, not run (policy 8; `S8-rerun-after-clock-put-back`, `S8-lost`; gap 4, closed). A runtime record damaged beyond NVS's guarantee never runs a start twice (policy 7; `S8-duplicate-damaged-record`; gap 9, closed). Counted as a gap, not failed: a start run again after a restart cut it before any valve opened (`gap.rerun_of_start_cut_before_watering`) | Architecture 5 |
| S9 | The configuration in effect after every boot validates; a healthy board leaves its start-up test within a minute of a boot | Persistent data never makes the controller unsafe or unbootable |
| S10 | NVS holds at most 32 keys and 40 kB; the live heap at the end is within 4 MB of day 2; the oracle's own queues are bounded; ASan's leak check at exit | No unbounded growth |
| S11 | No heap allocation inside `Controller::tick()` (the harness's own bookkeeping excluded); no flash write from the control task while an output is energised; optionally a host-time bound per tick (`--max-tick-us`) | Architecture 2 |
| L | Liveness mode: with no faults, every due start runs on time (not late, skipped or missed) and every zone of it completes its planned time to within a second, before the queue could have finished | Architecture 5 |
| M1 | Every MQTT message the inbox took is handled once, in order, within 600 ms of the MQTT task running; the inbox never holds more than its 8 (a full inbox drops, counted as `mqtt.inbox_full`) | Architecture 18 |
| Z1 | With zero-cross switching, the driver never waits longer than one crossing interval for a crossing | Architecture 7 |
| E8 | ESPHome mode: the schedule component's logic never runs a start key twice, and a start whose minute the device ran through with a valid clock runs in it | ESPHome guide, scheduler |

## Modes

| Mode | What happens |
|---|---|
| `chaos` | Everything above at once, at rates of hours to days |
| `liveness` | No faults, no users, no restarts: programs at 00:00, 01:30, 02:30, 05:00, 06:00, 07:30, 12:00, 18:45, 21:00 and 23:59 local, in any of the time zones, for a simulated year |
| `halt` | Every two minutes a run starts; 5 to 20 s later one board fault appears (the fault cycles with the seed through every modelled fault); 0 to 400 ms later the firmware hangs or freezes; then a watchdog restart and a repair. Checks S2 with each single fault present while a valve is on |
| `esphome` | The supervisor glue with the world faults, restarts and halts, and random sprinkler starts and stops, faults cleared and repairs confirmed |

## The CI subset and the long run

| Run | Chaos | Liveness | Halt | ESPHome |
|---|---|---|---|---|
| `--ci` (CI, ASan and UBSan, 4 processes) | seeds 1 to 24, 2 days each | seeds 1 and 2, 45 days each | 54 scenarios (every board fault twice), 0.02 days each | seeds 1 to 4, 1 day each |
| `--long` (`run_long.sh`, no sanitizers) | seeds 1 to 2000, 14 days each | seeds 1 to 32, 365 days each | 1080 scenarios (every board fault 40 times), 0.05 days each | seeds 1 to 160, 5 days each |

## Shrinking

`--shrink` reruns a failing seed with stop-at-first, cuts every event after
the first violation, then drops each event class (`Scenario::kinds()`) whose
removal keeps the same violation, and prints the minimal command line and
its trace. Each defect found this way also has a focused regression test in
`test/host` (named in the comment above it).
