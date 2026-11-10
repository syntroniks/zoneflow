# ZoneFlow firmware architecture

Status: **phase 2** (core hardening, network, REST API, OTA). Covers the
standalone ESP-IDF firmware of system spec section 4.1
(`firmware/zoneflow-fw/`). Nothing described here has run on a board.
Checks done so far: host unit tests (GoogleTest, ASan and UBSan) and
`idf.py build` with ESP-IDF v5.5.6. Section 11 lists what the phase 2
core hardening changed.

Phase 1 delivered the structure, the safety-critical core and tests. Phase 2
hardens the core (section 11) and adds WiFi and provisioning (`zf_net`,
section 12), the REST API, event stream and static web serving (`zf_web`,
section 13) and OTA with rollback (`zf_ota`, section 14). The network security design is in
[firmware security](firmware-security.md). Phase 3 adds MQTT with Home
Assistant discovery (`zf_mqtt`, section 17). The display UI and the web
UI itself are later work. Every module plugs into the interfaces in
section 4 and does not reach around them.

## 1. Modules

```mermaid
flowchart TB
    subgraph ext["Extension modules"]
        net[zf_net<br/>WiFi, provisioning, mDNS]
        web[zf_web<br/>HTTP, REST, SSE stream]
        mqtt[zf_mqtt<br/>MQTT, HA discovery]
        ota[zf_ota<br/>OTA, rollback]
        disp[zf_display<br/>OLED, encoder]
    end

    subgraph svc["AppServices (zf/app/services.hpp)"]
        cmdq[CommandQueue]
        bus[EventBus]
        state[StateStore]
        cfg[ConfigStore]
        time[TimeService]
    end

    subgraph core["zf_core (pure C++, host-tested)"]
        ctrl[Controller]
        drv[ZoneDriver]
        sched[Scheduler]
        diag[Sensors, FlowMeter,<br/>CurrentMonitor, AcMonitor]
        oeg[OutputEnableGuard]
        tz[TimeZone]
    end

    subgraph plat["zf_platform (ESP-IDF)"]
        gpio[GpioOutputs]
        ka[keep-alive timer ISR]
        zc[ZeroCrossInput]
        pw[persist writer task]
        sens[SensorInputs<br/>GPIO, PCNT, ADC1]
        nvs[NvsBlobStorage]
        rtc[Pcf8563Rtc, I2cBus]
        sntp[SNTP]
        safe[restart, brownout,<br/>panic hooks]
    end

    board[zf_board<br/>pins and board constants]

    ext --> svc
    cmdq --> ctrl
    ctrl --> bus
    ctrl --> state
    cfg --> ctrl
    ctrl --> drv
    ctrl --> sched
    ctrl --> diag
    sched --> drv
    sched --> tz
    drv --> oeg
    drv --> gpio
    drv --> zc
    ctrl --> pw
    pw --> nvs
    oeg --> ka
    diag --> sens
    cfg --> nvs
    time --> rtc
    sntp --> time
    plat --> board
```

| Component | Role |
|---|---|
| `zf_core` | All decisions. No ESP-IDF headers. Built by the firmware and by `test/host`. |
| `zf_board` | The board header. Every GPIO number and board constant, plus `static_assert` checks of the pin rules. |
| `zf_platform` | Implements the core's interfaces on ESP-IDF. Holds no policy. |
| `main` | Creates the objects (all static), starts the tasks, calls the extension modules. |
| `zf_net` | WiFi station, BLE and SoftAP provisioning, hostname and mDNS, captive DNS, factory reset. Logic in `net_logic.cpp` (host-tested). |
| `zf_web` | HTTP(S) server, REST API, SSE stream, static files, admin authentication. Logic in `api.cpp`, `auth.cpp`, `crypto.cpp`, `routes.cpp`, `http_util.cpp`, `json_map.cpp` (host-tested). |
| `zf_ota` | Firmware upload and pull, web UI image upload, post-boot self-test and rollback. Logic in `ota_logic.cpp` (host-tested). |
| `zf_mqtt` | MQTT client, topic tree, commands, Home Assistant discovery. Logic in `mqtt_logic.cpp` (host-tested). |
| `zf_display` | Stub here (the display is on another branch). The header lists what the module must do. |

## 2. Threading model

| Task | Priority | Core | Period | Does |
|---|---|---|---|---|
| keep-alive (GPTimer alarm ISR, IRAM) | interrupt | allocating core | 5 ms (`CONFIG_ZF_OE_TOGGLE_PERIOD_MS`) | Calls `OutputEnableGuard::step()` and writes the OE pin register. Runs while the flash cache is off. No heap, no locks, no calls into flash. |
| `zf_control` | 10 | 1 | 50 ms (`CONFIG_ZF_CONTROL_PERIOD_MS`) | Drains the command queue, then `Controller::tick()`. Subscribed to the task watchdog (5 s, panics). |
| `zf_persist` | 3 | 0 | 250 ms poll | Takes the latest runtime state from `PersistMailbox`, builds the JSON and writes NVS. Static stack (6 KB). |
| `zf_evlog` | 2 | 0 | on event | Logs events from its own queue. |
| `main` | 1 | any | 100 ms | Status LED, state log once a minute. |
| lwIP / SNTP | IDF | IDF | on sync | `TimeService::onNetworkSync()` writes the RTC. |
| `zf_net` | 5 | 0 | 1 s or on message, 6 KB | WiFi, provisioning and mDNS calls. IDF event handlers only queue a message. |
| `zf_dns` | 3 | 0 | on packet, 3 KB | Captive-portal DNS, only while the setup access point runs. |
| `httpd` (port 80, and 443 with HTTPS) | 5 | 0 | per request, 12 KB | Every REST request, config writes included; static files; uploads. |
| `zf_web_sse` | 3 | 0 | 250 ms or on event, 6 KB | Formats and sends event-stream messages from its own event queue. |
| `zf_ota_pull` | 4 | any | while pulling, 8 KB | HTTPS download into the passive slot. |
| `zf_ota_test` | 2 | any | 500 ms, at most 60 s after an update | Post-boot self-test, then confirm or roll back. |
| `zf_mqtt` | 3 | 0 | 250 ms or on message, 6 KB | MQTT state, commands, discovery, coalesced config edits (section 17). |
| `mqtt_task` (esp-mqtt) | 5 | any | on socket, 6 KB (IDF default) | MQTT protocol; its event handler only copies into the `zf_mqtt` inbox. |

Rules:

- **One owner.** The control task owns the `Controller`, and through it the
  `ZoneDriver` and `Scheduler`. No other task calls them. The one exception
  is `Controller::emergencyOff()` / `ZoneDriver::emergencyOff()`, which only
  writes GPIO and atomics and may run in any context.
- **Commands in.** Other tasks change behaviour only by posting a `Command`
  to `CommandQueue` (non-blocking, 16 entries, mutex held for one struct
  copy). A full queue returns `false`; the caller reports busy.
- **Events out.** `EventBus::publish()` runs on the control task and calls
  handlers synchronously, without holding the bus lock. A handler must
  return within microseconds: copy the event into the module's own queue
  with zero timeout (`platform::EventQueueSubscriber` does this) and drop
  it if full. Handlers must not block, sleep, take a lock a blocking task
  may hold, or call back into the controller.
- **State snapshot.** The control task publishes a `SystemState` copy to
  `StateStore` whenever something changed and at least once a second.
  `snapshot()` holds a mutex for one struct copy; `sequence()` is a lock-free
  change counter for pollers.
- **Config writes.** `ConfigStore::update()` validates, writes NVS, then swaps
  the in-memory copy. Writers serialise on a writer-only mutex that is held
  across the flash write. Neither the control task nor the keep-alive
  interrupt ever takes it. The NVS lock inside ESP-IDF is the other lock
  held across a flash write; only config writers and `zf_persist` take it.
  Readers (`get()`) use a separate mutex held for a copy.
- **No allocation in the safety path.** Zone driver, output-enable guard,
  keep-alive interrupt, command queue, event bus and `Controller::tick()`
  use fixed arrays. Config JSON handling (cJSON) allocates, and only runs on
  config load and update, on the caller's task. Runtime state is handed to
  the persist writer as a plain struct (below). A host test counts
  `operator new` calls inside 1200 control ticks that include a scheduled
  start and repeated state changes: zero.
- **Runtime state off the control task.** `Controller` posts a
  `SchedulerPersist` copy (plain data: 8 + 8 + 16 × 16 = 272 bytes) to its
  `PersistMailbox` at most every 2 s; the mutex is held for that copy only,
  and a newer copy replaces one not yet taken. `zf_persist` takes it, builds
  the JSON and writes NVS. A failed write comes back through the mailbox
  and raises `storage_error` on the next tick.

#### Why NVS garbage collection can no longer starve the heartbeat

Before, the control task built JSON and called `nvs_set_blob` itself, so a
write that triggered NVS garbage collection (page erase plus copying live
entries) held the control loop, and with it the heartbeat, for the whole
operation, and it waited on the NVS lock whenever a config write on
another task held it. Now:

1. The control task never calls NVS and never takes the NVS lock or the
   config writer lock. The only lock it shares with `zf_persist` is the
   mailbox mutex, which neither side holds across anything but a struct
   copy. A stalled writer cannot block `post()`.
2. `zf_persist` runs at priority 3 on core 0; the control task runs at
   priority 10 on core 1. Between flash operations the control task runs
   as usual.
3. What remains is the flash operation itself: while one sector is erased
   or written, the cache is off and both cores stall, the control task
   included. The heartbeat then ages by at most one flash operation plus
   one control period. The keep-alive no longer stalls with it (section 3),
   so OE keeps its edges as long as that stays under the 500 ms heartbeat
   timeout. NVS erases one 4 KB sector at a time; the worst-case sector
   erase time of the module flash must come from its datasheet (open
   question 2).

The host test `PersistHandOff.StalledFlashWriteDoesNotStallTheControlLoop`
runs the hand-off with a storage `write()` that blocks, as during garbage
collection, on a second thread. During 1200 ticks (60 s of control time,
including a scheduled start) with the writer stuck, the heartbeat is fresh
on every tick with a zone open, the control thread never calls storage, and
the last posted state is the one stored once the write is released.

## 3. Safety design

How the firmware implements spec section 2:

| Spec rule | Firmware |
|---|---|
| 1. Outputs off by default | `GpioOutputs::init()` is the first call in `app_main`: it sets the output latch to off, then enables the drivers (pin goes from high impedance straight to driven off). `ZoneDriver::init()` forces all off again. Hardware pull-downs must hold outputs off before that (see open questions). |
| 2. No strapping or glitching pins on outputs | `zf_board` `static_assert`s: no strapping (0, 3, 45, 46), USB (19, 20) or flash/PSRAM (26 to 37) pins, no output or OE on a pin with a high-level power-up glitch (18, 19, 20; ESP32-S3 datasheet v2.2 table 2-2), no duplicates. |
| 3. Hardware output enable | Firmware keeps toggling a keep-alive signal, and hardware removes gate drive within about 1 s after toggling stops. `OutputEnableGuard` toggles only while (a) an output is on or about to be, (b) the control loop heartbeat is younger than `CONFIG_ZF_OE_HEARTBEAT_TIMEOUT_MS` (500 ms), (c) it is not tripped. The edges come from a GPTimer alarm interrupt every 5 ms that keeps running through flash erases (see "Keep-alive from a timer" below). A stuck control loop, a stopped timer or a halted CPU all stop the toggling. A static-level variant exists as a Kconfig option for other hardware; rev A uses toggle mode. |
| 4. Maximum run time | Per zone, not per run (spec v0.3 rule 4 as amended). The driver adds up each zone's on-time across runs until the zone has been closed for `driver.min_rest_s` (default 1800 s). `ZoneDriver::start()` clamps every request to what is left of min(zone `max_run_s`, 4 h); the driver times the run and closes the valve, and `tick()` independently closes a zone whose accumulated on-time reaches the limit (`max_run_time`). The zone then refuses to start (`resting`) until it has been closed for the rest time. Stop and restart, pause and resume, back-to-back programs and config reloads do not reset the count. |

#### Keep-alive from a timer

The OE waveform is generated by a GPTimer alarm interrupt
(`CONFIG_GPTIMER_ISR_CACHE_SAFE=y`), not by a task. The interrupt handler is
in IRAM and the driver keeps it enabled while the flash cache is off, so OE
keeps its edges while NVS or OTA erase flash. Everything the handler
touches is in IRAM or DRAM: `OutputEnableGuard::step()` is forced inline
(the linked ELF shows `onKeepAlive` in IRAM with no call instructions), the
guard is a static object, and the pin write is a register write.

A peripheral generating the waveform must not keep a dead control loop's
outputs alive. It does not, because the timer does not decide the level;
`step()` does, on every alarm:

- The control task increments a heartbeat counter every tick.
- `step(period)` compares the counter with the value it saw last time. If
  the counter moved, the age restarts at 0; if not, the age grows by one
  period (5 ms). It needs no clock, so there is no time base to share or
  wrap.
- Once the age exceeds the timeout (500 ms), `step()` returns low on every
  alarm and OE gets no more edges; the hardware then removes gate drive
  within its hold time (about 1 s, spec rule 3). The age saturates just
  above the timeout, so it never wraps back to fresh; only a new heartbeat
  restarts the edges.

So when the control loop dies (hang, crash before the panic handler, a
priority problem), the edges stop at most 500 ms plus one period after its
last tick, although the timer keeps firing. A flash operation stalls the
control loop the same way; the edges continue through stalls shorter than
the timeout and stop for longer ones (`OutputEnableTimer.*` host tests).
The restart and brownout handlers trip the guard; they and the panic
handler also drive OE low directly.

Other limits enforced in the zone driver:

- **Concurrency.** At most `driver.max_concurrent` zones open or waiting
  (default 2, configurable 1 to 2, from spec section 1). Master is extra.
- **Master sequencing.** Master opens `pre_open_ms` before the first zone and
  closes `post_close_ms` after the last one. A zone starting during the
  post-close delay keeps the master on.
- **Inter-zone delay.** A zone opens no sooner than `inter_zone_delay_ms`
  after the previous zone closed.

Fault paths, each ending with outputs off:

| Event | Action |
|---|---|
| Short circuit (above `short_circuit_ma`, 2 samples, checked even during inrush) | All off, queue stopped, zone locked out, fault latched. On the master: counted towards the master lockout. |
| Over-current (above the summed window after settling, 3 samples; short-circuit samples count towards them) | Same as short circuit. |
| Hardware over-current trip (comparator latch, read every tick) | All off in firmware state, queue stopped, `over_current_trip` latched. Blamed: every output switched on within `settle_ms` before the trip, else the output switched on most recently. Blamed zones lock out; a blamed master counts towards the master lockout. Starts are refused while the latch is set. The latch is released on a later tick, only when the driver has nothing open or waiting, right after all outputs are driven off again; a latch that comes back is released at most once a second and reported once. |
| Master lockout (`master.fault_lockout_count` consecutive faults blamed on the master, default 3) | `master_locked_out` latched. With the master enabled, every start is refused: scheduled starts are skipped (`master_locked_out`), manual runs return `master_locked_out`, the driver refuses to open anything. A master session of at least `settle_ms` without a fault resets the count. |
| Open circuit (below the summed window) | That zone off, queue moves to the next entry, fault latched. Master or unattributable: all off. |
| Current with every output off | Fault only. Firmware already has everything off; the output stage needs a human. If the current then rises past `short_circuit_ma`, `short_circuit` is reported as well. |
| Current sense unavailable (ADC init or calibration failed, or no reading) | `current_sense_unavailable` raised; zones still run (policy below). |
| 24 VAC lost (60 ms debounce) | All off, queue cleared, starts blocked until AC returns (500 ms debounce). |
| Maintenance command (OTA, factory reset) | All off, starts blocked. |
| `esp_restart()` (includes the reboot after OTA) | Shutdown handler trips OE and forces outputs off. |
| Panic, abort, task or interrupt watchdog | `-Wl,--wrap=esp_panic_handler`: an IRAM wrapper forces OE and outputs off by direct register writes, then runs the IDF handler. Checked in the linked ELF: the IDF call site loads `__wrap_esp_panic_handler`. |
| Brownout | Brownout interrupt callback (IRAM) forces outputs off before the reset. |

Latched faults clear with the `ClearFaults` command. Lockouts (zones and
master) clear with it. A current fault whose cause is still present is
raised again on the next sample. `ac_lost`, `time_invalid` and
`current_sense_unavailable` are conditions: they clear themselves when the
cause goes away, and `ClearFaults` does not hide them.

**Current sense unavailable: policy.** Zones keep running. Watering
matters more than diagnostics, and a controller that refuses to water
because a diagnostic failed is the failure users notice. The condition is
raised as a fault (status LED blinks, `activeFaults` bit, event), so the
board gets looked at. Without current readings, short-circuit protection
rests on the hardware over-current trip and the per-output series element
of spec 3.1, and the maximum run time still applies. With
`diagnostics.current_enabled = false` the condition is not raised.

## 4. Interfaces for later modules

Every module receives `zf::AppServices` (`zf/app/services.hpp`):

```cpp
struct AppServices {
    EventBus &events;       // subscribe(handler, ctx, mask)
    StateStore &state;      // snapshot(), sequence()
    ICommandSink &commands; // post(Command::runZone(z, s)), ...
    ConfigStore &config;    // get(), toJson(), update(), updateFromJson()
    TimeService &time;      // status(), setManual()
};
```

| Need | Use |
|---|---|
| Show status | `state.snapshot()`; poll `state.sequence()` or wake on an event. |
| Follow changes | `platform::EventQueueSubscriber` with an `EventMask`, read from the module's task. |
| Run, stop, pause, resume, skip, rain delay, clear faults, maintenance | `commands.post(Command::...)`. A command the controller refuses publishes `CommandRejected` with the command, its zone or program and the `RunError` (for example `locked_out`, `zone_resting`, `master_locked_out`, `inhibited`). Subscribe to it to answer an API call. |
| Read config | `config.get()` (typed) or `config.toJson()` (REST, backup). |
| Change config | `config.updateFromJson(json)` or `config.update(cfg)`; on success post `CommandType::ReloadConfig`. A failed validation returns the JSON path and a message for the API response. |
| Set the clock | `time.setManual(utc)`. |

Event types: `ZoneChanged`, `MasterChanged`, `QueueChanged`, `ProgramStarted`,
`ProgramSkipped`, `ProgramMissed`, `SensorChanged`, `FaultRaised`,
`FaultCleared`, `RainDelayChanged`, `ConfigApplied`, `TimeStatusChanged`,
`MaintenanceChanged`, `CommandRejected`, `RunDropped` (a queued run whose
zone refused to start: locked out, resting, master locked out) and
`PauseExpired`. `toString()` gives stable lowercase names for APIs.

New enum values are appended, never inserted. ESP-IDF builds with
`-Werror=switch`, so a `switch` over `EventType`, `FaultCode`, `RunError`,
`StartError` or `SkipReason` without a `default` must list every value.

`SystemState` also carries, per zone, `restRemainingSec` (resting after
reaching the maximum run time), and `masterLockedOut` and
`masterFaultCount`.

Adding state a module owns (for example network status) means adding a field
to `SystemState` and a command or setter that the control task applies.
Modules do not write `StateStore` themselves.

Phase 2 exception: network status lives in `zf_net`'s own `NetStatusStore`
(same pattern as `StateStore`: one writer, a mutex held for a copy, a
sequence counter) instead of `SystemState`, because `zf_core` was being
hardened on another branch at the same time. Readers use `net::status()` and
`net::statusSequence()`; the event stream merges it into its `state`
message.

Reviewed again after the core hardening merged: moving it into `SystemState`
is **not** a clean, additive change, so it stays in `zf_net`:

- Only the control task writes `SystemState`, and it builds each snapshot
  from its own members. The network status comes from the `zf_net` task. It
  would have to reach the controller either as a command (a `Command` is a
  small struct in a 16-entry queue; the status is about 200 bytes of SSID,
  addresses and counters) or as a new `ControllerDeps` source read under a
  lock on every publish. The second changes `ControllerDeps`, so every place
  that constructs a `Controller` (`app_main`, the host tests, other
  branches) changes too.
- Readers that want to wake on a network change would need a new
  `EventType`. Appending one breaks every exhaustive `switch` on another
  branch again (`-Werror=switch`), right after the hardening did the same.
- Every reader copies `SystemState`, the display at its frame rate. Most do
  not need the network block.

A reader that needs both takes `state.snapshot()` and `net::status()` and
compares `state.sequence()` and `net::statusSequence()`.

## 5. Scheduler

### Programs

A program has an id (stable, non-zero), name, enabled flag, up to 4 start
times (local "HH:MM"; a time listed twice counts once), a day rule, per-zone durations, a seasonal-adjust flag
and an obey-sensor flag. Day rules:

- **weekdays**: any set of days.
- **interval**: every N days (1 to 365) counted from an anchor date.
- **odd / even**: day of month. Odd runs on the 31st and the 1st back to back.

Zones of a program run one after another in zone order. Programs that start
at the same time queue behind each other; they do not overlap. The run queue
holds up to 64 entries.

### Run queue

`pause` closes the running zone and keeps its remaining time; `resume`
reopens it for the remainder. `skip` drops the head entry and starts the
next. `stop` closes everything and empties the queue.

A paused queue is stopped, not resumed, after `schedule.pause_timeout_min`
(default 120, range 1 to 1440), and `PauseExpired` is published. The
timeout runs from the first pause; pausing again does not extend it. Why
stop: someone paused on purpose (mowing, working in a bed, children
playing), and watering that restarts by itself hours later could soak
them or a lawn they left wet. A paused queue also blocks later scheduled
starts, so leaving it paused stops the schedule with no fault. Stopping
clears both problems: nothing starts unexpectedly, and the next scheduled
start runs as usual.

An entry whose zone refuses to start when it reaches the head of the
queue (locked out, resting, master locked out) is dropped with a
`RunDropped` event and the queue moves on. `RunZone` on a zone that is
locked out or resting is refused at once (`locked_out`, `zone_resting`).

### Adjustments and inhibits

| Condition | Scheduled starts | Running scheduled entries | Manual runs |
|---|---|---|---|
| Seasonal percent (0 to 250) | Durations scaled, rounded to the second | | Manual program runs scaled; manual zone runs not |
| Rain delay until a time | Skipped (`rain_delay`) | Removed when the delay is set | Allowed |
| Rain sensor (or spare in "inhibit" mode) active | Skipped if the program obeys the sensor | Removed when the sensor goes active | Allowed |
| No 24 VAC | Skipped (`no_ac`) | Cleared | Refused |
| Maintenance | Skipped | Cleared | Refused |
| Same program already queued | Skipped (`already_queued`) | | Refused |

### Time, DST and clock jumps

Start times are local wall-clock times converted with a POSIX TZ string
(`timezone` in config, for example `CET-1CEST,M3.5.0,M10.5.0/3`). The core has
its own TZ implementation so the host and the device compute the same
instants.

- **Spring forward.** A start inside the skipped hour runs at the end of the
  gap (02:30 on the Berlin change day runs at 03:00 CEST).
- **Fall back.** A start inside the repeated hour runs once, at the first
  occurrence.
- **Each start runs at most once.** For every program the scheduler stores
  the last start it consumed as a (local day, minute) key and persists it in
  NVS. Setting the clock back, rebooting right after a start, or a DST
  repeat never runs a start twice.
- **Clock set back by more than 24 h** (for example an RTC that was in the
  future, corrected by SNTP): the stored keys are clamped to the new time so
  the schedule resumes. The same check runs on the first valid time after
  boot, for keys stored while the clock was wrong.
- **No valid clock.** If the RTC is missing or reports lost time and SNTP has
  not synced, no scheduled start runs. Manual runs still work.
- **Time zone change.** The stored keys are local (day, minute) values. On
  a change of zone, `Scheduler::configure` moves each key to the same
  instant in the new zone (old zone to UTC, UTC to new zone). Without this,
  a move west made the last consumed key read as a later local time than
  now, and the next start counted as already run. Re-keying was chosen
  over storing UTC instants because it keeps the persisted format. An
  unchanged zone leaves the keys alone, so a key in a DST gap is not
  shifted.

### Missed-start policy

The scheduler evaluates the window between its last evaluation and now, once
a second. After a power cut, at boot, or after the clock jumps forward, that
window can contain starts that did not run on time.

1. Only starts within the last 24 hours are considered.
2. Of the starts in the window, only the newest per program can run.
3. It runs (late, `ProgramStarted` with `late = true`) if it is no older
   than `schedule.missed_start_grace_min` (default 60, range 0 to 720).
   With 0, nothing is caught up.
4. Every other start in the window is reported once as `ProgramMissed` and
   not run.
5. A program with no stored key (never ran on this device, or runtime state
   lost) gets no catch-up and no missed reports.
6. Editing the configuration never triggers a start whose time has passed.

Example: a program at 06:00 with the default grace. Power fails at 05:30 and
returns at 06:20: the 06:00 start runs at 06:20. Power returns at 07:30
instead: the start is reported missed and the next one runs as usual.

## 6. Configuration

Stored as JSON in NVS (namespace `zoneflow`, key `config`). The same JSON is
the REST and backup format. Abbreviated example:

```json
{
  "schema": 2,
  "device_name": "zoneflow",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
  "zone_count": 8,
  "zones": [{"name": "Lawn", "enabled": true, "max_run_s": 3600,
             "current_min_ma": 80, "current_max_ma": 700, "default_run_s": 600}],
  "master": {"enabled": true, "pre_open_ms": 1000, "post_close_ms": 2000,
             "fault_lockout_count": 3,
             "current_min_ma": 80, "current_max_ma": 700},
  "driver": {"max_concurrent": 2, "inter_zone_delay_ms": 2000, "min_rest_s": 1800,
             "zero_cross_switching": false},
  "sensors": {
    "rain": {"enabled": true, "contact": "nc", "debounce_ms": 2000},
    "spare": {"enabled": false, "contact": "no", "debounce_ms": 200, "function": "none"},
    "flow": {"enabled": false, "pulses_per_litre": 450, "leak_threshold_lpm": 0.5, "leak_detect_s": 60}
  },
  "diagnostics": {"current_enabled": true, "settle_ms": 500, "idle_max_ma": 60,
                  "short_circuit_ma": 3000, "confirm_samples": 3},
  "schedule": {
    "seasonal_percent": 100,
    "missed_start_grace_min": 60,
    "pause_timeout_min": 120,
    "programs": [{"id": 1, "name": "Lawn", "enabled": true, "starts": ["06:00"],
                  "days": {"type": "weekdays", "weekdays": ["mon", "wed", "fri"]},
                  "durations_s": [600, 0, 300], "seasonal": true, "obey_sensor": true}]
  },
  "mqtt": {"enabled": false, "host": "", "port": 1883, "username": "", "tls": false,
           "ca": "bundle", "client_id": "", "base_topic": "", "discovery": true,
           "discovery_prefix": "homeassistant", "ha_zone_entity": "valve"}
}
```

- **Missing fields** take their defaults; unknown fields are ignored.
- **Validation** rejects the whole document on the first error and returns
  its JSON path (for example `schedule.programs[0].starts[1]`) and a message.
  Range limits are in `zf_core/src/config.cpp`. A document nested more than
  16 levels deep is rejected before parsing: cJSON recurses once per level.
- **Migration.** `schema` is required. An older schema runs the steps in
  `kMigrations` (`config_json.cpp`), one version at a time, and the result
  is written back. Schema 2 (phase 3) added the `mqtt` object and
  `zones[].default_run_s`; step 1 to 2 writes both with their defaults.
  Tested in `mqtt_config_test.cpp` (a stored schema 1 document loads as
  `Migrated` and is written back as schema 2; a schema 1 backup restores).
  A firmware older than phase 3 reads a schema 2 document best-effort
  (newer schema) and refuses to write it, so after a rollback past phase 3
  configuration edits fail until the newer firmware is back or the
  configuration is reset.
- **Newer schema** (firmware rolled back after an OTA): parsed best-effort,
  used, and not written back, so the newer firmware finds its data intact.
  The same document sent through `updateFromJson()` (REST, MQTT, restore)
  is rejected with path `schema` and "schema N is newer than this firmware
  supports (2)": writing it would drop every field this firmware does not
  know.
- **New fields in this phase** (schema stays 1; absent fields take their
  defaults, so older documents still load):

  | Field | Default | Range | Meaning |
  |---|---|---|---|
  | `driver.min_rest_s` | 1800 | 60 to 86400 | Closed time that ends a zone's run of back-to-back runs, and the rest after it reached its maximum run time. |
  | `driver.zero_cross_switching` | false | | Turn outputs on at a mains zero crossing when the board has the signal. |
  | `master.fault_lockout_count` | 3 | 1 to 10 | Consecutive current faults on the master that lock it out. |
  | `schedule.pause_timeout_min` | 120 | 1 to 1440 | A paused queue is stopped after this long. |

  Why 30 minutes of rest: the default maximum run time is 60 minutes, so a
  zone can be on for at most 60 of any 90 minutes, and typical programs
  (10 to 30 minutes per zone) never meet the limit. Two programs that share
  a zone within half an hour share one budget.
- **Invalid stored config:** defaults are used, `config_invalid` fault is
  raised, the stored copy is kept for diagnosis until the next update.
- **Runtime state** (rain delay, last consumed start per program) lives under
  key `runtime`, written at most every 2 s and only when changed.

## 7. Sensors and diagnostics

- **Rain and spare inputs.** Dry contacts, NO or NC, debounced (default
  2000 ms rain, 200 ms spare). The first reading after boot is taken as-is so
  a wet sensor inhibits from the start.
- **Flow.** PCNT counts pulses in hardware (falling edge, 1 us glitch
  filter). Litres = pulses / `pulses_per_litre`; rate over the last 5 s (one sample per second).
  Flow above `leak_threshold_lpm` for `leak_detect_s` with every valve shut
  raises `flow_without_zones`.
- **Valve current.** One total current from the low-side shunt (spec 3.1).
  A shorted valve or wire shuts the zone off and is reported (spec 1).
  Expected window = sum of
  the min/max of every output that is on. Comparisons start `settle_ms` after
  any output change; the short-circuit limit applies at all times. A config
  reload does not restart a confirmation in progress. A fault
  is attributed to the output switched on most recently. Per-output current
  observed when the output runs alone (or with the master, minus the
  master's share) is recorded as the basis for learned limits later.
- **24 VAC present.** Loss declared after 60 ms, return after 500 ms. With
  no AC (USB-only power) the current checks are suspended.
- **Current sense unavailable.** `SensorSample::currentSenseAvailable` is
  false when the ADC or its calibration could not be set up, or a tick gave
  no reading. Policy in section 3.
- **Hardware over-current trip.** `SensorSample::overCurrentTripped` reads
  the comparator latch; `IOutputHal::clearOverCurrentTrip()` pulses its
  release. Handling in section 3.
- **Zero-cross switching.** With `driver.zero_cross_switching` on and a
  zero-cross input attached (`IZeroCrossHal`, GPIO interrupt timestamps on
  the device), `ZoneDriver` turns each output on at the next mains zero
  crossing plus the board's detector offset. `zeroCrossWaitUs()` is a pure
  function (host-tested): it waits at most one crossing interval, and
  returns 0 (switch now) with no signal, an interval outside 7 to 11 ms, or
  no edge for three intervals. Turning off never waits: a triac stops at
  the next current zero by itself, and the off paths must stay immediate.
  The wait is a busy wait on the control task, at most 10 ms at 50 Hz per
  output switched on; an output that follows another at once is already
  on the crossing.

## 8. Timekeeping

- **Boot.** Read the RTC. If the chip answers, its voltage-low flag is clear
  and the time is after 2025-01-01, set the system clock from it (status
  `rtc`). Otherwise status `unset` and the `time_invalid` condition is
  raised.
- **SNTP.** Starts on the first `IP_EVENT_STA_GOT_IP` (`pool.ntp.org`). Each
  sync sets status `synced` and writes the RTC.
- **Representation.** The system clock and the RTC hold UTC. Local time
  exists only inside the scheduler and for display.
- **Provisional RTC: NXP PCF8563** (I2C address 0x51, register map from the
  NXP datasheet rev 11.1), chosen because it and the BM8563 clone are widely
  stocked. The driver is one file (`time_storage.cpp`, `Pcf8563Rtc`) behind
  `IRtc`. No part number or LCSC code is chosen here; that is the hardware
  lead's call.

## 9. Open questions for the hardware team

1. **Pin map.** Everything in `zoneflow_rev_a.hpp` is provisional. Please
   publish `hardware/pinmap.yaml`; the board header will follow it. The
   `static_assert`s will flag any pin that breaks the rules above.
2. **Keep-alive circuit.** Spec v0.2 fixes toggle mode and a removal time
   of about 1 s. Firmware produces an edge every 5 ms while an output is
   wanted and stops toggling when everything is off. The edges now come
   from an IRAM-safe timer interrupt and continue through flash erases,
   so the hold time no longer has to cover a sector erase. Still needed:
   the minimum edge rate the circuit accepts, and the module flash's
   worst-case 4 KB sector-erase time (from its datasheet). The control
   loop stalls for that long; if it can exceed about 450 ms (the 500 ms
   heartbeat timeout minus one control period), OE drops during the erase
   and `CONFIG_ZF_OE_HEARTBEAT_TIMEOUT_MS` has to be raised.
3. **Pull-downs.** Firmware assumes external pull-downs on every output and
   on OE so outputs stay off in reset and while unpowered. Confirm.
4. **Current-sense front end.** Spec v0.2: low-side shunt in the shared
   triac return, rectifying/averaging front end, biased away from the
   non-linear ADC region near 0 V. Firmware reads it on ADC1 with a
   placeholder scale of 1 mA per mV and a bias offset of 0 mV
   (`kValveCurrentMaPerMv`, `kValveCurrentOffsetMv`), 16 samples averaged
   per 50 ms tick. Needed: shunt value, gain, bias voltage, full scale and
   filter time constant, or the external ADC if one is used.
5. **24 VAC-present signal.** Assumed active low (optocoupler pulling down).
   Polarity and response time on loss?
6. **Field input polarity.** Assumed: contact closed pulls the pin low,
   external pull-ups present, protection network in series. The flow input
   must pass 200 Hz.
7. **RTC choice and backup.** PCF8563 assumed at 0x51. Battery or supercap,
   and expected hold-up time?
8. **OLED address.** 0x3C assumed. SSD1306 or SH1106?
9. **GPIO15/16 for I2C** assume no 32 kHz crystal on the module pins.
10. **Valve current windows.** Defaults are 80 to 700 mA per output and
    3000 mA short circuit. The prior-art survey cites 200 to 400 mA for
    typical solenoids and an OpenSprinkler default limit of 1200 mA. Please
    confirm against the triac stage and the spec ratings (0.5 A continuous,
    1.5 A inrush for 100 ms per output).
11. **Per-output LEDs.** The spec asks for one LED per output. Firmware
    assumes they follow the gate drive in hardware and only drives the
    power/status LED. If they need their own GPIOs, the pin map has to
    change.
12. **Hardware over-current trip.** Firmware now expects a latching
    comparator: trip status on GPIO18 (assumed active low, open drain) and
    a release pulse on GPIO47 (assumed active high, 10 us). Both pins,
    polarities and the pulse width are placeholders. Needed: the latch
    circuit, its trip threshold, whether it removes gate drive from every
    output or only the faulty one (firmware assumes all), and its minimum
    release pulse.
13. **Zero-cross detector.** Firmware expects one pulse per zero crossing
    (two per mains period) on GPIO48, rising edge at the crossing, offset
    0 us. Needed: the circuit, the edge that marks the crossing and the
    detector's lead or lag (`kZeroCrossOffsetUs`).
14. **Pin budget.** The provisional map now uses every free GPIO of the
    WROOM-1-N16R8 except UART0 (43, 44). The TFT (SPI: clock, data,
    chip select, data/command, backlight PWM; spec v0.3) and a second
    button need about six more. Options: an I2C GPIO expander for the
    buttons and encoder, sharing the I2C pins, or moving the status LED
    or AC-present input. The display owner and the hardware lead need to
    settle this in `hardware/pinmap.yaml`.

## 10. Decisions made in phase 1

- **ESP-IDF v5.5.6**, the newest v5.x release at the time of writing.
  C++ is compiled as `gnu++2b` (IDF default); the core also builds as C++20
  on the host. No exceptions, no RTTI in core code.
- **cJSON** for configuration JSON: it ships with ESP-IDF; host tests fetch
  the same version (1.7.19).
- **Own POSIX TZ code** instead of newlib `localtime`, for host/device parity
  and testability.
- **Single sequential queue.** Programs never overlap. The concurrency limit
  of 2 is a safety ceiling in the driver; parallel programs can be added
  later without changing the driver.
- **Keep-alive as a task**, as the brief asked. Replaced in phase 2 by an
  IRAM-safe timer interrupt (section 3).

## 11. Phase 2 core hardening

The design concerns raised by the phase 1 review (pull request #17), as
decided by the orchestrator, and where each is handled:

| Concern | Change | Where |
|---|---|---|
| Max run time per run, not per zone | Per-zone on-time budget across runs, rest time `driver.min_rest_s`; spec rule 4 amended | Section 3, rule 4 |
| No master lockout | `master.fault_lockout_count`, `master_locked_out` | Section 3, fault paths |
| Pause has no timeout | `schedule.pause_timeout_min`, stop not resume | Section 5, run queue |
| Persistence on the control task | `PersistMailbox` and `zf_persist` | Section 2 |
| Current diagnostics switch off silently | `current_sense_unavailable`, zones still run | Section 3 |
| No hardware over-current input | Trip input, latch release, handling | Sections 3, 7, open question 12 |
| `RunZone` on a locked-out zone returns `Ok` | `RunError::LockedOut` and `CommandRejected` | Section 4 |
| Newer schema written back through the API | Rejected | Section 6 |
| Keep-alive pauses during flash erases | GPTimer interrupt, `step()` | Section 3 |
| (new) Switching at a random mains phase | Zero-cross hook, off by default | Section 7 |
| Idle current rising to a short is not reported | `short_circuit` after `current_while_idle` | Section 3 |
| Time zone change skips a start | Keys re-keyed on a zone change | Section 5 |
| Spec v0.3 drift (core only) | WROOM-1-N16R8: octal PSRAM, 16 MB flash, 4 MB OTA slots, 7.75 MB assets partition; board asserts for GPIO33..37 | `sdkconfig.defaults`, `partitions.csv`, board header |

Not done here: the display driver (owned by `zf_display`), and the stack
budgets for phase 2 modules (review concern 6) and request body caps
(concern 7), which belong to the modules that receive requests.

## 12. Network (`zf_net`)

Pure logic in `net_logic.hpp` (host-tested), ESP-IDF glue in `net.cpp`.

### Station

- Credentials are stored by the WiFi driver (`WIFI_STORAGE_FLASH`). At every
  start the station config is rewritten with `bssid_set = false`,
  `channel = 0`, all-channel scan and connect-by-signal, so each connect
  picks the strongest access point of the SSID. No BSSID or channel is ever
  stored (prior-art recommendation 8, OpenSprinkler #413). 802.11k/v are on
  so mesh systems can steer the station. Minimum security WPA2-PSK (WPA3-SAE
  accepted), PMF capable; an open network is used only when no password is
  stored.
- **Reconnect backoff** (`StationPolicy`, `Backoff`): 1 s, doubling to a
  5-minute cap, each delay scaled by a random 80 to 120 % so devices that
  lost the same AP do not retry in step. Reset on every IPv4 address.
  Retries never stop and never fall back to an access point: a router outage
  must not turn the controller into an open setup network.
- **Hostname** `zoneflow-xxxx` (last two MAC bytes, lower-case hex), used for
  DHCP and mDNS. **mDNS** (`espressif/mdns` 1.14.1): `zoneflow-xxxx.local`,
  `_http._tcp` and `_zoneflow._tcp` on port 80 with TXT `api=/api/v1`,
  `fw=<version>`, `id=<mac>`.
- **SNTP** is unchanged: `zf_platform` starts it on the first
  `IP_EVENT_STA_GOT_IP` and hands each sync to `TimeService`.

### Provisioning

| Item | Decision |
|---|---|
| Identity | At first boot: 10-character setup code (50 bits, hardware RNG with the radio on), SRP6a salt and verifier from `esp_srp_gen_salt_verifier` for user `wifiprov`. Stored in NVS namespace `zf_net`; kept by a factory reset. |
| Boot plan (`ProvisioningFsm::plan`) | Re-provision request stored → that transport. No credentials → BLE (SoftAP if BLE is not built in). Otherwise station, and the BT controller memory is released at once. |
| BLE | ESP-IDF unified provisioning (`wifi_provisioning`), Security 2, device name `ZoneFlow-XXXX`, scheme handler `FREE_BTDM` so BT memory is released when provisioning ends. |
| SoftAP | Same manager, SoftAP scheme, SSID `ZoneFlow-XXXX`, WPA2 with the setup code as passphrase. Shares `zf_web`'s HTTP server (`wifi_prov_scheme_softap_set_httpd_handle`), which serves the captive setup page at `/setup`; a DNS responder on 192.168.4.1:53 answers every A query with 192.168.4.1. |
| Windows | Unprovisioned: BLE for 10 min, then SoftAP until provisioned (one-way: BLE memory is gone). Re-provisioning a configured device: 15 min, then back to the stored network without a restart. A connected phone or AP client holds the window open. |
| Failures | Wrong password or AP not found: the manager's state machine is reset so the user can retry in the same session. |
| QR payload | `{"ver":"v1","name":"ZoneFlow-XXXX","username":"wifiprov","pop":"<code>","transport":"ble"}`, the format of the ESP-IDF `wifi_prov_mgr` example and Espressif's apps. `net::setupInfo()` returns it for the display; nothing sends it over the network. |
| Re-provision | `net::requestProvisioning(transport)` stores the request and restarts. The display module binds it to a long-press; the API has `POST /network/reprovision`. |
| Factory reset | `net::factoryReset()`: maintenance on, wait until the snapshot shows every output off (5 s max), keep the identity in RAM, `nvs_flash_erase()`, write the identity back, restart. Power loss between erase and rewrite gives a new setup code. |

### Status

`NetStatus` (state, provisioning transport, SSID, BSSID, channel, RSSI, IPv4,
reconnect attempt and delay, last disconnect reason, mDNS) is published to
`NetStatusStore` when a field changes (RSSI by 5 dB or more) and at least
every 30 s. See section 4 for why it is not in `SystemState` yet.

## 13. Web server and API (`zf_web`)

- **Server.** `esp_http_server` on port 80, 12 KB task stack, up to 7
  sockets, LRU purge on. With HTTPS on, `esp_https_server` on 443 (4 sockets)
  serves everything and port 80 answers 301, except while the setup access
  point runs.
- **Routing.** One table (`routes.cpp`) gives each route its method, path,
  access level (Public, Read, Admin, Provisioning), body type and body cap.
  `Api::admit()` applies, in order: Host check, route match (404/405), Origin
  check for writes, session, CSRF header for cookie sessions, provisioning
  state, body size from `Content-Length`, content type. Only then is the body
  read. The HTTP glue registers five wildcard handlers for `/api/v1/*`, three
  exact handlers (events, firmware upload, UI upload) and `/*` for files.
- **OpenAPI.** `docs/firmware/openapi.json` (OpenAPI 3.1) documents every
  route. `web_openapi_test.cpp` checks both directions plus security, body
  type, `x-max-body-bytes` and response type per route.
- **Mapping to the core.** Reads use `state.snapshot()` and `config.get()`.
  Commands use `commands.post(Command::...)` and answer 202, or 503 when the
  queue is full. Configuration edits (zone settings, programs, time zone)
  patch the JSON document from `config.toJson()` and write it back with
  `config.updateFromJson()`, so all validation stays in `zf_core`; the error
  path is made relative to the edited object. A successful write posts
  `ReloadConfig`. Zone numbers are 1-based in the API.
- **Stop one zone.** The core has no "remove this queue entry" command, so
  `POST /zones/{n}/stop` skips the head entry when that zone is running and
  answers 409 otherwise.
- **Run commands and `CommandRejected`.** `RunZone` and `RunProgram` are the
  commands the controller can refuse. The API first answers what the snapshot
  already shows (locked-out zone, resting zone with `Retry-After`, locked-out
  master, maintenance, no 24 VAC). Otherwise it arms a `CommandWatch` (an
  `EventBus` subscriber that only stores atomics on the control task), posts
  the command and waits at most 250 ms: a matching `CommandRejected` becomes
  an error with the controller's `RunError` as `code`; a snapshot published
  after the post plus one more control period without a rejection becomes
  202 `"confirmed": true`; a timeout becomes 202 `"confirmed": false`. Commands
  carry no request id, so matching is by command type, zone and program, and
  the API runs one such wait at a time. Tested end to end with the real
  `Controller` (`ApiWithController.*`). Details and the error codes:
  [API reference, run commands](../firmware/api.md#run-commands).
- **Hardening state in the API.** Zones show `resting` and
  `rest_remaining_s`; the master shows `locked_out` and `fault_count`;
  `inhibit.master_lockout`; sensors show `current_sense_available` and
  `over_current_tripped`; the queue shows `pause_timeout_min`. New events
  `command_rejected`, `run_dropped` and `pause_expired` go to the stream with
  their data. A restore or `PUT /config` of a newer schema answers 400
  `unsupported_schema` with the core's path (`schema`) and message.
- **Event stream: SSE**, not WebSocket (reasons in the
  [API reference](../firmware/api.md#live-event-stream)). The bus handler is
  `platform::EventQueueSubscriber` (copy into a 32-entry queue, never waits).
  The `zf_web_sse` task turns events into `event` messages, sends a `state`
  message when the snapshot changes (at most every 500 ms, at least every
  30 s), `network` and `ota` messages on change and a keep-alive comment after
  15 s. Each stream is an `httpd_req_async_handler_begin()` request owned by
  that task; at most 3. A send that fails ends the stream. Sends happen
  outside the stream lock, so a slow client delays only the stream task.
- **Static files** from the `www` LittleFS partition (`joltwallet/littlefs`
  1.22.3), with gzip variants, immutable caching under `/assets/` and a
  client-side-routing fallback; a built-in page when the partition is empty.
  Contract for the UI: [web UI contract](../firmware/web-ui-contract.md).
- **Settings** (`protect_reads`, `https`, `check_host`) and the password
  record live in NVS namespace `zf_web`, not in the configuration, so a
  backup never carries them.

## 14. OTA (`zf_ota`)

- **Upload** (`POST /firmware/upload`, streamed in 4 KB chunks) and **pull**
  (`POST /firmware/pull`, `esp_https_ota` with the certificate bundle,
  redirects followed, `https://` only).
- **Gate** (`mayStart`): refused while another update runs, or while a zone
  is open or waiting, the master is on or the queue is running, unless forced.
- **Before writing:** post `StopAll` and `SetMaintenance(true)`, then poll the
  snapshot until maintenance is on and every output is off (`outputsSafe`,
  5 s limit, else `outputs_not_off`). Maintenance stays on until the restart;
  on failure it is released. Flash writes stall both cores, so the outputs
  are off before the first erase (see open question 2).
- **Image checks:** the first 288 bytes (image header, first segment header,
  `esp_app_desc_t`) are held back until chip id (ESP32-S3) and project name
  (`zoneflow-fw`) match; nothing is written for a wrong image. `esp_ota_end`
  then checks the image hash before the boot slot changes.
- **Swap:** `esp_ota_set_boot_partition`, restart after 1 s so the HTTP
  answer goes out. The restart shutdown handler forces outputs off again.
- **Rollback.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` was already on.
  `ota::markRunningAppValid()` (unchanged call in `app_main`) now starts a
  self-test when the image is pending verification: confirm once the state
  sequence advanced 5 times (control loop alive) and the web server started;
  roll back at once if the stored configuration was rejected
  (`config_invalid`), or after 60 s. A crash before confirmation also rolls
  back (bootloader). Network connectivity is not a condition: a router outage
  during an update must not undo it.
- **Web UI image** (`POST /firmware/assets`): same gate and maintenance; the
  first chunk must carry the LittleFS magic at offset 8 (LittleFS SPEC.md);
  each 4 KB sector is erased just before it is written.
- **Status** in `GET /firmware` and the `ota` stream message: phase, error,
  bytes, new version, running and next slot, image state, version in the
  other slot.

## 15. Memory and flash budget

Measured by `idf.py size` (ESP-IDF v5.5.6, esp32s3, `-Os`, WROOM-1-N16R8
configuration from `sdkconfig.defaults`):

| | `main` after the core hardening (`d6685a3`) | This branch |
|---|---|---|
| Application image | 442 615 B | 1 501 983 B (35.8 % of a 4 MB OTA slot) |
| Flash code (`.text`) | 280 816 B | 1 046 276 B |
| Flash data (`.rodata`) | 84 136 B | 316 916 B |
| Internal RAM used statically (DIRAM `.text` + `.data` + `.bss`) | 93 663 B | 171 623 B |
| Internal RAM left for heap and stacks (DIRAM remaining) | 248 097 B | 170 137 B |

Most of the growth is the WiFi and BLE stacks, mbedTLS (TLS server and
client, X.509 writing), the provisioning manager, the HTTP servers, mDNS and
LittleFS. The image leaves about 2.6 MB of headroom in each OTA slot.

The module has 8 MB of octal PSRAM, and `main` enables it for `malloc`
(`CONFIG_SPIRAM_USE_MALLOC`). Blocks of 16 KB or more go to PSRAM
(`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`), and 32 KB of internal RAM is
kept for DMA and drivers (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`).
WiFi and lwIP buffers stay internal (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`
is off).

Heap at run time has not been measured; no board exists. Estimates, with
their sources:

| Consumer | Estimate | Basis |
|---|---|---|
| Free internal heap after static allocation | about 166 KB | DIRAM remaining in the table above, before any task or driver allocates |
| WiFi driver (station) | 40 to 60 KB internal, rough | 10 static RX buffers of about 1.6 KB (`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10`) are allocated at start; up to 32 dynamic RX and 32 dynamic TX buffers come and go with traffic; plus driver state. The range is a guess to be replaced by a measurement |
| NimBLE + BLE controller, provisioning only | about 60 to 100 KB | Released after provisioning; the prior-art survey cites about 110 KB for BLE provisioning |
| Task stacks added in phase 2 | 35 KB internal | httpd 12 + SSE 6 + net 6 + DNS 3 (setup only) + OTA pull 8 (while pulling) |
| One TLS session (HTTPS on, or OTA pull) | 20 to 40 KB | mbedTLS with `CONFIG_MBEDTLS_DYNAMIC_BUFFER`; worst case is a 16 KB record each way, which lands in PSRAM |
| Config copies during a request | up to about 20 KB | JSON text (up to 16 KB body, in PSRAM at that size) plus a cJSON tree of small blocks and one `Config` struct (about 2 KB) |

The tightest case for internal RAM is a BLE provisioning session (BT stack
and WiFi together). With large blocks now in PSRAM it should fit, but it
must be measured on the first board: `GET /api/v1/info` reports `heap.free` and
`heap.min_free` (all 8-bit heap, PSRAM included), `heap.internal_free` and
`heap.internal_min_free` (internal RAM only) and `http_stack_free_min`. If internal RAM is short, turning on
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` is the next lever.

## 16. Decisions made in phase 2

- **SSE for the live stream**, not WebSocket: one direction is enough, it
  reuses HTTP authentication and the Host check, and browsers reconnect by
  themselves.
- **Own SHA-256/HMAC/PBKDF2** (`crypto.cpp`, 200 lines) instead of mbedTLS
  for the password hash, so host tests and the device run the same code;
  checked against RFC 4231 and RFC 7914 vectors. mbedTLS is used for TLS and
  the certificate.
- **One admin password, RAM sessions**, CSRF token plus SameSite=Strict
  cookie, Host-header check. See [firmware security](firmware-security.md).
- **Configuration edits as JSON patches** of the stored document, so the API
  adds no second validator.
- **Network status outside `SystemState`**, also after the core hardening
  (section 4 gives the reasons).
- **Run commands wait up to 250 ms for a verdict** rather than always
  answering 202, so a caller learns about a locked-out or resting zone from
  the answer; the stream still carries every rejection.
- **Requests run on the HTTP task with a 12 KB stack** rather than a separate
  worker task: one place to size, measured at run time.
- **Managed components** `espressif/mdns` 1.14.1 and `joltwallet/littlefs`
  1.22.3, pinned exactly in each component's `idf_component.yml`. mDNS left
  ESP-IDF core in v5.0; LittleFS was never in it.

## 17. MQTT (`zf_mqtt`)

User guide, topic reference and examples: [MQTT and Home
Assistant](../firmware/mqtt.md). Pure logic in `mqtt_logic.hpp`
(host-tested), ESP-IDF glue in `mqtt.cpp`.

### Structure

- **Only `AppServices`.** State from `state.snapshot()`, events from an
  `EventQueueSubscriber` (32 entries), commands through `commands.post()`,
  settings from `config.get()`, configuration edits through
  `config.update()` followed by `ReloadConfig`. Network facts (IP, RSSI,
  MAC) come from `net::status()`, update progress from `ota::status()`.
- **Two tasks.** esp-mqtt runs the protocol on its own task. Its event
  handler copies each message (topic up to 160 bytes, payload up to 256)
  or connection event into an 8-entry inbox with zero timeout and returns;
  a full inbox drops the message and counts it. The `zf_mqtt` task does
  everything else.
- **Settings.** `Config::mqtt` (schema 2), validated by the core like every
  other field. The password and a custom CA certificate live in NVS
  namespace `zf_mqtt` and are set only through `PUT /api/v1/mqtt`, so the
  configuration document, `GET /config` and the backup never carry them.
  A change of connection settings or secrets stops and recreates the
  client; any other configuration change resends discovery and state.

### Connection

- The client is created once the station has an IPv4 address.
- esp-mqtt's fixed-delay reconnect is off; `ConnectionPolicy` (the
  `net::Backoff` of section 12) retries after 2 s, doubling to 5 min,
  scaled by 80 to 120 %, reset on every connection.
- Last will: `offline`, retained, QoS 1 on `<root>/status`; `online` is
  published retained on connect. Keep-alive 60 s, network timeout 5 s.
- TLS: `esp_crt_bundle_attach` or the stored PEM CA.

### Bounded memory

The broker can be slow or away for days. Nothing grows with that time:

- Retained state is not queued. `markChanges()` sets one bit per topic
  (`Dirty`); when a topic is sent, its payload is built from the newest
  snapshot. A failed publish leaves its bit set.
- Events and command results go out only while connected and are dropped
  otherwise.
- QoS 0 publishes are written straight to the socket (at most the 5 s
  network timeout, on the `zf_mqtt` task only). QoS 1 messages (availability,
  discovery) wait in the esp-mqtt outbox, capped at 64 KB (`outbox.limit`).
- The discovery document is built once per send: about 15 KB with the
  default configuration, 26.5 KB with 8 zones, 16 programs and every
  sensor (measured on the host). Blocks of 16 KB or more go to PSRAM
  (section 15).

### Commands and the control loop

- `parseCommand()` validates topic and payload against the configuration
  (zone number, enabled zone, program id, 1 to 14 400 s, 0 to 720 h) and
  maps them to `Command`s. The zone driver still clamps every run to the
  zone's maximum and remaining budget (section 3, rule 4).
- **Retained and empty messages are never commands.** A retained run
  would start watering at every reconnect. An empty message is what every
  subscriber receives when someone clears a retained message; the broker
  test found that it started the zone with its default run time before
  this rule.
- A token bucket (10 at once, 5 per second) answers the rest with
  `rate_limited`. The control task drains at most 8 commands per 50 ms tick
  from a 16-entry queue, so MQTT can never hold it up; a full queue is
  answered `busy`. Host test `MqttRateLimiter.FloodNeverFillsTheCommandQueue`.
- Every command gets a `<root>/result` message. Refusals by the controller
  come from the `CommandRejected` event, so they are reported whichever
  interface sent the command.
- Configuration edits from MQTT (zone run time, program enable) are
  collected and written at most every 2 s (`PendingEdits`): each write
  erases flash.

### Home Assistant discovery

One device discovery message (Home Assistant 2026.11.1 documentation,
`<prefix>/device/zoneflow_<id>/config`), not retained, sent on every
connect and 1 to 5 s after a Home Assistant birth message. Components that
are no longer wanted are sent once as `{"platform": ...}` and then
omitted (`DiscoveryPlanner`), the removal procedure the documentation
gives. `mqtt_discovery_test.cpp` checks the document against the options
and device classes documented for each platform
(`test/host/data/ha_mqtt_discovery.json`, with its source).

Why `valve` for zones: Home Assistant's valve entity has open, close and
stop and an `opening` state, which matches a zone that waits for the
master or the inter-zone delay. `ha_zone_entity: switch` is offered for
dashboards and automations built on switches.

The `update` entity has no `command_topic`: Home Assistant shows the
version and the progress of an update started elsewhere but cannot
install one. Firmware installs stay behind the admin login of the REST API
([firmware security](firmware-security.md), section 4.7).

### Verification

- Host tests: `mqtt_logic_test.cpp`, `mqtt_discovery_test.cpp`,
  `mqtt_config_test.cpp`, `web_mqtt_api_test.cpp`.
- Broker test (`test/broker/run.sh`, not in CI): the logic and the real
  `Controller` against Mosquitto 2.1.2, through libmosquitto. It replaces
  `mqtt.cpp` with a short loop of its own, so esp-mqtt and the glue are
  only compiled, never run.
- `idf.py build`: image 1 561 027 bytes (37.2 % of a 4 MB slot), 59 044
  bytes more than phase 2; DIRAM used 174 387 bytes (phase 2: 171 623).
