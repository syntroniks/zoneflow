# ESPHome

!!! warning "Built and host-tested, never run on a board"
    The package in `firmware/esphome/` compiles for the rev A board, and its C++ logic
    passes host tests on a simulated board. No ZoneFlow board exists, so nothing here has
    been flashed or measured. The checks that were run are listed under
    [What has been checked](#what-has-been-checked). Tracking issue:
    [#7](https://github.com/syntroniks/zoneflow/issues/7).

The ESPHome package is the firmware for people who run Home Assistant. It uses the
ESPHome `sprinkler` component for zone cycles, shows every zone as a Home Assistant
`valve`, and adds what the stock component lacks: an on-device scheduler that works with
Home Assistant down, rain lockout and rain delay, flow metering with an unscheduled-flow
alarm, per-zone current with open and short detection, and a hard per-zone maximum run
time.

Underneath the YAML, two external components (`zoneflow` and `zoneflow_schedule`) do what
YAML cannot do safely: the hardware keep-alive, the output expander, the valve-common
relay, the self-tests and the current measurement. They use the same C++ code as the
[standalone firmware](standalone.md) for that, built from the same files.

## Which firmware should I use?

| | Standalone firmware | ESPHome package |
|---|---|---|
| **Needs Home Assistant** | No | Yes, for setup and control. Schedules run without it. |
| **Set-up** | Phone app over Bluetooth LE, or setup access point | Improv over USB or Bluetooth LE, then adopt in the ESPHome dashboard |
| **Control** | Own web UI, REST API, MQTT | Home Assistant entities and automations |
| **Programs** | 16, up to 4 start times each, per-zone durations | 3, two start times each, a zone list and a run-time percentage |
| **Day rules** | Weekdays, every N days (1 to 365), odd, even | Every day, weekdays, weekends, odd, even, every 2 or 3 days, any set of weekdays |
| **Display** | Full menu interface: edit schedules, settings, network | Status screen and manual runs |
| **Run history** | Kept on the device | Home Assistant's history |
| **Updates** | OTA with automatic rollback; release files with SHA-256 checksums, images not signed | ESPHome OTA, safe mode after repeated boot failures |
| **Customising** | Configuration through the UI and API | Edit the YAML, add any ESPHome component |
| **Safety layer** | `zf_core` output stage, zone driver, current checks | The same `zf_core` code |

Choose the ESPHome package if Home Assistant already runs your house and you want the
controller in your dashboards and automations. Choose the standalone firmware if you want
the controller to work on its own, with its own interface and no hub.

## Install

### From the web installer

The [web installer](web-installer.md) lists the ESPHome firmware once a release
includes it. It flashes a factory image of the 8-zone package; on a 4-zone board the
firmware reads the build strap and offers zones 1 to 4 only.

1. **Flash.** Connect the board over USB-C, choose "ZoneFlow ESPHome package" and
   install.
2. **WiFi.** The installer offers WiFi set-up over the cable (Improv Serial). You can
   also do it later over Bluetooth LE with an Improv client: press **Home** on the
   controller to allow it, the permission lasts two minutes. If neither works, the
   controller opens a WiFi access point called "ZoneFlow setup".
3. **Adopt.** Home Assistant finds the controller. Adopt it in the ESPHome dashboard,
   which writes a device file that uses this package from GitHub.

### With `esphome run`

A device file of nine lines is enough:

```yaml
substitutions:
  name: garden
  zoneflow_ref: v0.2.0
  zone_1_name: Front lawn
  zone_2_name: Back lawn
  zone_3_name: Vegetables
packages:
  zoneflow: github://syntroniks/zoneflow/firmware/esphome/zoneflow.yaml@v0.2.0
# No wifi: block. WiFi is set over Improv (USB, or Bluetooth LE after pressing Home).
```

Keep `zoneflow_ref` and the `@` tag the same: the first picks the C++ components, the
second the YAML.

!!! note "No release has been tagged yet"
    `v0.2.0` stands for the tag of a release, and the project has published none
    ([issue #8](https://github.com/syntroniks/zoneflow/issues/8)). Until it has, a tag
    cannot be fetched: write `main` in both places. `main` moves with every merge, so a
    build from it is not a fixed version.

Then:

```sh
esphome run garden.yaml
```

To add an API encryption key, OTA password or anything else ESPHome offers, add it to
the same file. Entries there override the package.

Use `zoneflow-4zone.yaml` instead of `zoneflow.yaml` for a 4-zone build. The 8-zone file
also works on a 4-zone board, but shows four zones that will never run.

### Substitutions

| Substitution | Default | What it does |
|---|---|---|
| `name` | `zoneflow` | Node name and host name |
| `friendly_name` | `ZoneFlow` | Name shown in Home Assistant and on the display |
| `zone_1_name` to `zone_8_name` | `Zone 1` ... | Zone names |
| `timezone` | `UTC` | Time zone for schedules, for example `Europe/Berlin`. Set it. |
| `max_run_time` | `60min` | Hard limit per zone (1 min to 4 h), see [Safety](#safety) |
| `max_run_minutes` | `60` | Upper end of the run duration entities; keep it equal to `max_run_time` |
| `rest_time` | `30min` | How long a zone stays closed after reaching its limit |
| `flow_pulses_per_litre` | `450` | Flow meter K-factor, from its datasheet |
| `leak_threshold_lpm` | `0.5` | Flow with every valve shut above this, for 60 s, raises the alarm |
| `zoneflow_ref` | `main` | Git tag of the C++ components |

## Entities

Names below use the default zone names.

### Per zone

| Entity | Type | Notes |
|---|---|---|
| `Zone 1` | valve | Open runs the zone for its run duration through the sprinkler controller. Close stops it. "Opening" while the self-test runs. |
| `Zone 1 run` | switch | The sprinkler controller's zone switch |
| `Zone 1 enabled` | switch | Leaves the zone out of full cycles and programs |
| `Zone 1 run duration` | number | Minutes, 1 to `max_run_minutes` |
| `Zone 1 status` | text | `off`, `starting`, `open`, `resting 23 min`, `locked out`, `fault: open_circuit`, `not fitted` |
| `Zone 1 fault` | binary sensor | On while the zone has a fault: locked out, or (an open circuit) until its next run draws current |
| `Zone 1 current` | sensor | Mean current of the last run with no other output changing, in mA |

### Controller

| Entity | Type | Notes |
|---|---|---|
| `All zones` | switch | Starts or stops a full cycle |
| `Auto advance`, `Standby`, `Queue enabled` | switch | Sprinkler controller settings |
| `Seasonal adjustment` | number | Multiplier on every run duration, 0 to 2.5 |
| `Repeat cycles` | number | Sprinkler repeat count |
| `Stop all` | button | Stops every zone and the cycle |
| `Fault` | binary sensor | Any latched fault or locked-out zone |
| `Faults` | text | What is wrong, in the words of the standalone display and web interface, for example `Zone 3: short circuit, No 24 V AC supply`. A failed self-test names its step: `Safety signal disturbed (step 2b)`. `No faults` when there is none. |
| `Valve board fault` | binary sensor | The output stage failed a self-test or read-back |
| `Clear faults` | button | Clears lockouts and latched faults, not a fault of a safety layer (self-test steps 1, 2, 2b, 5b, 9) |
| `Acknowledge repair (press twice)` | button | Clears a fault of a safety layer: press, then press again within 10 s. The board then learns its detector release times again. |
| `Run self-test` | button | Runs the polling cycle of hardware design 19.6 now |
| `Valve current` | sensor | Total valve current, RMS, mA, every second |
| `Master valve`, `Valve common relay` | binary sensor | Output states |
| `24 V AC supply` | binary sensor | From the zero-cross signal |
| `Water used`, `Flow rate` | sensor | From the flow meter, L and L/min |
| `Water flows with all valves off` | binary sensor | Flow with every valve shut, latched until cleared |
| `Rain sensor` | binary sensor | The rain sensor, after the type setting and a 2 s debounce |
| `Rain sensor enabled` | switch | Off by default. With nothing connected, a normally-closed sensor reads "wet". |
| `Rain sensor type` | select | Normally closed (most sensors) or normally open |
| `Rain delay` | number | Hours. Set it to pause schedules; it counts down and survives a restart. |
| `Spare input` | binary sensor | The AUX terminal |
| `Display backlight` | light | |
| Diagnostics | | `Output stage`, `Mains frequency`, `Current noise floor`, `Self-tests passed`, `Sensor supply overloaded`, `Valve current not measured`, `Home button`, firmware version, WiFi signal, uptime, restart and safe-mode buttons |

### Schedule

Three programs, A, B and C. For each:

| Entity | Type | Notes |
|---|---|---|
| `Program A enabled` | switch | Off by default |
| `Program A start 1`, `start 2` | time | Local start times |
| `Program A start 2 enabled` | switch | Off by default, so start 2 does nothing until you turn it on |
| `Program A days` | select | Every day, Weekdays, Weekends, Odd days, Even days, Every 2 days, Every 3 days, Custom |
| `Program A Sunday` ... `Saturday` | switch | The days for "Custom" |
| `Program A zones` | text | `all`, or zone numbers such as `1, 2, 5` |
| `Program A run time` | number | Percent of each zone's run duration, 10 to 200 |
| `Program A run now` | button | Runs the program now |
| `Schedule status` | text | The last thing the scheduler did, for example `Program A skipped: rain sensor` |
| `Next scheduled run` | text | For example `Sat 06:00 Program A` |

## The scheduler

The scheduler runs on the controller. It needs a valid clock and nothing else: no
network and no Home Assistant.

- **Clock.** At boot the controller reads the PCF8563 real-time clock, which runs from a
  coin cell. When SNTP synchronises, it writes the time back to the RTC. Without a valid
  clock no scheduled start runs; manual runs still work. Set `timezone`.
- **A start.** Once a second the scheduler checks each enabled program. When a start time
  comes on a day the rule allows, it queues the program's zones on the sprinkler
  controller, in zone order. Each zone runs for its run duration times the seasonal
  adjustment times the program's run-time percentage. Disabled zones are left out.
- **Runs at most once.** The last start each program ran is kept in flash. A reboot, a
  clock set back, or the repeated hour when daylight saving time ends never runs a start
  twice. A start in the hour skipped when it begins runs at the end of the gap.
- **Missed starts.** After a power cut, the newest missed start of a program runs late if
  it is at most 60 minutes old. Older ones are reported in `Schedule status` and skipped.
  Editing a program never starts a time that has already passed. These rules are the
  standalone firmware's.
- **Skipped starts.** A start is skipped, with the reason in `Schedule status`, when the
  rain sensor is enabled and wet, a rain delay is set, the controller has no 24 VAC, the
  output stage has a fault, or a manual run is in progress. A start while another
  program runs is queued behind it.
- **Rain.** When the rain sensor turns wet, a scheduled run in progress stops. Setting a
  rain delay does the same. Manual runs are not affected by either.

## Safety

The [specification](../design/system-spec.md) section 2 rules apply to this firmware as
to the other. What the package does for each:

- **Outputs off by default.** No valve is on a GPIO. The valves are bits of an I/O
  expander that the hardware holds in reset whenever the keep-alive signal stops, so a
  board that is unpowered, in reset, crashed or being flashed has every valve off.
- **Keep-alive.** A 1 ms timer interrupt makes every keep-alive edge, one every 5 ms, and
  only while the ESPHome main loop has proved it is running within the last 500 ms
  (hardware design 19.4). If the loop stalls for longer, for example on a blocking
  component, the edges stop and the board removes valve drive and opens the
  valve-common relay in hardware: no valve current 1.34 s after the last heartbeat at the
  latest, 1.76 s if the pin stopped high (hardware design 19.4, with the corner results of
  review A2). When the loop comes back, the output
  stage finds drive gone and latches a fault, so a person looks at it. The interrupt code
  is in IRAM, keeps running while flash is written, and makes no function calls; this was
  checked in the linked firmware with `objdump`.
- **Self-test.** At start-up, before every run, after every run and every 10 minutes
  between runs, the output stage tests both keep-alive detectors and the expander, and
  before every run it checks the relay for a welded contact and the outputs for a short
  (hardware design 19.7). First it checks the supply on `VIN_MON` (GPIO3): under 0.60 V
  or above 1.50 V it waits and the `Faults` text says `24 V AC supply too low` or
  `too high`. Before every run it holds the keep-alive pin high for 2 s (step 2; with
  `self_test_before_every_run: false` only at start-up and once a day) and then, before
  every run whatever that option says, lets it float for 0.5 s (step 2b): nothing may
  drive the keep-alive net meanwhile. The display's `on_float_test` hook runs the
  backlight at half brightness at 4.7 kHz and redraws the display during those 0.5 s,
  and the component streams through a PSRAM buffer and sends 0x55 on UART0 (a terminal
  on J8 shows a line of U; the logger is on USB, so UART0 is free; a YAML that opens
  UART0 itself keeps it, and the log says the test runs without that load), so that a
  leak from one of those signals onto the keep-alive net shows. Then it checks that the
  relay driver lets go when its control is released undriven (step 5b). After every run
  it times both detectors and compares the times with fixed limits and with the board's
  own times (0.40 to 1.70 times), which it learns with 24 V AC on while the chip's
  temperature sensor reads 25 to 50 °C, and keeps in flash. Until then the `Faults` text
  says `Safety timing not learned`. The component reads the chip's temperature itself; do
  not add ESPHome's `internal_temperature` sensor, which would take the sensor from it.
- **Keep-alive pin.** GPIO17 is UART1's default transmit pin. The package refuses a
  `logger:` with `hardware_uart: UART1`, and the output stage checks every 50 ms that no
  peripheral is routed to the pin (hardware design 19.1 rule 16).
- **Maximum run time.** Each zone has a hard limit (`max_run_time`, at most 4 h). The
  limit counts on-time across runs: back-to-back programs, repeated manual runs and
  automations all add up until the zone has been closed for `rest_time`. A zone that
  reached its limit stays closed for the rest time, and a software restart does not
  shorten the rest. A power cut does not either: the budget is also kept in flash, two
  copies written a few times per run, and what the last write may have missed counts as
  used (firmware architecture section 20). This sits below the sprinkler component, so
  no automation or Home Assistant call gets past it.
- **Stagger and concurrency.** Two outputs never switch on within 100 ms. At most one
  zone runs at a time by default (two zones plus the master is the board's limit).
- **Master valve.** Off by default, as in the standalone firmware, so a controller
  with nothing on the MV terminal reports no master fault. With a master valve or pump
  relay wired, set the substitution `master_valve: "true"` in your device file. The
  zoneflow component then opens the master valve 1 s before the first zone and closes it
  2 s after the last. The sprinkler component's pump options are not used for it,
  because the output layer has to keep the stagger and the master lockout.
- **Current faults.** The board measures one total current. The firmware compares it
  with the sum of the windows of the outputs that are on (80 to 700 mA per output by
  default) after 500 ms of inrush blanking. Below the window is an open circuit: that zone
  stops and is reported, and is not locked out (the same policy as the standalone
  firmware since the soak found them differing: an open wire is a fault in the garden,
  and once it is fixed the next run clears the fault). Above it, or above 3 A at any time,
  everything stops and the zone is locked out. A current in one direction only (a
  half-dead output on the board) stops the zone and locks it out. A current that rises
  with no output switched stops everything; a shorted output on the board is then found
  within one current window and the relay opens. A hardware over-current trip blames the output switched on in the
  last 500 ms; three trips in a day lock it out. Repeated faults on the master valve lock
  it out, and with it every zone.
- **Open circuit with the master valve on.** With the master valve on, the master's own
  current is in the measurement. A typical master draws more than the 80 mA lower limit
  of a zone, so an open zone wire is then not found. Set `min_current` for the zones
  (through `zone_overrides` in the `zoneflow:` block) close to the `Zone N current` they
  show, minus a margin, once the board runs.

### Faults and who clears them

| Fault | What happens | Clear |
|---|---|---|
| Zone open | Zone off, `Zone N fault` on, not locked out | Clears itself at the zone's next run that draws current; or `Clear faults` |
| Zone short, over-current, DC current, trip | Zone (or everything) off, zone locked out, `Zone N fault` on | `Clear faults`, or hold **Home** for 3 s |
| Output stage (expander, relay, read-back) | Everything off, nothing starts | `Clear faults` |
| Safety layer (self-test steps 1, 2, 2b, 5b, 9): `Safety circuit failed its test`, `Safety signal disturbed`, `Valve relay driver faulty`; and `Power input circuit failed`, `Logic supply too high`, `Protection held on` | Everything off, nothing starts, the board needs repair | `Acknowledge repair`, pressed twice within 10 s; the board then learns its detector times again |
| No 24 V AC supply, 24 V AC supply too low or too high, valve current not measured, sensor supply overloaded, safety timing not learned | Reported; no 24 V AC and a supply out of range also block starts | Clear themselves |
| Water flows with all valves off | Reported | `Clear faults` |

A new fault that stops everything also stops the sprinkler cycle, so it does not move on
to the next zone, and fires the `on_fault` automation of the `zoneflow:` block. The status
LED blinks while any fault is latched. A latched output-stage fault survives a software
restart.

### What happens when

| Event | Policy |
|---|---|
| **Boot** | Keep-alive low, expander in reset, every valve off. Start-up self-test once 24 VAC is present. No run resumes. |
| **OTA update** | Every valve off and starts blocked before the first byte is written. An aborted update unblocks. |
| **Restart, shutdown** | Keep-alive low and tripped in the shutdown hook, before the reset. |
| **Crash** | The interrupt stops with the CPU, the keep-alive stops, the hardware removes drive. ESPHome's crash handler already wraps the panic handler, so the package adds no hook there. |
| **Home Assistant disconnects** | Nothing changes. A run continues to its end; schedules keep running. `api: reboot_timeout: 0s`. |
| **WiFi lost** | Nothing changes. `wifi: reboot_timeout: 0s`, so the controller never restarts in the middle of a run. |
| **24 VAC lost** | Everything off, starts blocked until it returns. |
| **Repeated boot failures** | ESPHome safe mode after 10 failed boots: only WiFi and OTA run, so the valves stay off. |

## Display and controls

The display shows the controller name and the time, the running zone with its time left
(or "Idle" and the next scheduled run), the valve current and flow, and one status line:
the first fault, no 24 VAC, rain, rain delay, or "Ready".

| Control | Action |
|---|---|
| Turn the encoder | Choose a manual run: All zones, a zone, or Program A to C |
| Push the encoder | Start the choice. A zone runs for its run duration. |
| **Back** | Cancel the choice; with nothing chosen, stop everything |
| **Home** | Back to the status screen |
| **Home**, held 3 s | Clear faults (not a detector fault) |
| **Home**, during WiFi set-up | Allows Improv over Bluetooth LE for two minutes |

The backlight dims to 30 % after a minute without input and turns off after five. Any
input wakes it, and an input while it is off only wakes it.

The encoder push and the two buttons have no filter on the board; the package debounces
them with two filters on each binary sensor, `delayed_on: 5ms` and `delayed_off: 20ms`.
That is the timing of the standalone firmware: a press counts once the contact has read
closed for 5 ms, and a release 20 ms after it first read open, so the chatter of a
contact gives one press. There is one difference. ESPHome looks at a pin on each pass of
its main loop, about every 16 ms and less often while it draws the display, where the
standalone firmware samples every 5 ms from a timer. A press that begins and ends
between two passes is not seen, and a spike that a pass happens to catch counts as a
press. Neither is new with this change, and neither has been tried on a board.

The panel's mirroring and colour inversion are copied from the standalone firmware,
which marks them as a guess until a panel has been seen. The display runs at 10 MHz SPI:
the ST7789V is guaranteed to 15 MHz, the ESP32-S3 divider cannot make that, and the next
step is 16 MHz.

## Home Assistant examples

### Dashboard

```yaml
type: vertical-stack
cards:
  - type: tile
    entity: binary_sensor.garden_fault
  - type: entities
    title: Zones
    entities:
      - valve.garden_front_lawn
      - sensor.garden_front_lawn_status
      - valve.garden_back_lawn
      - sensor.garden_back_lawn_status
      - valve.garden_vegetables
      - sensor.garden_vegetables_status
  - type: entities
    title: Schedule
    entities:
      - switch.garden_program_a_enabled
      - time.garden_program_a_start_1
      - select.garden_program_a_days
      - text.garden_program_a_zones
      - number.garden_seasonal_adjustment
      - number.garden_rain_delay
      - sensor.garden_next_scheduled_run
      - sensor.garden_schedule_status
  - type: history-graph
    hours_to_show: 24
    entities:
      - sensor.garden_valve_current
      - sensor.garden_flow_rate
```

### Automations

Rain delay from a forecast:

```yaml
alias: Irrigation, skip a day when rain is forecast
triggers:
  - trigger: time
    at: "05:00:00"
conditions:
  - condition: numeric_state
    entity_id: sensor.rain_forecast_today_mm
    above: 5
actions:
  - action: number.set_value
    target:
      entity_id: number.garden_rain_delay
    data:
      value: 24
```

A notification on any fault:

```yaml
alias: Irrigation fault
triggers:
  - trigger: state
    entity_id: binary_sensor.garden_fault
    to: "on"
actions:
  - action: notify.mobile_app_phone
    data:
      title: Irrigation controller
      message: "{{ states('sensor.garden_faults') }}"
```

Seasonal adjustment by month:

```yaml
alias: Irrigation seasonal adjustment
triggers:
  - trigger: time
    at: "00:05:00"
actions:
  - action: number.set_value
    target:
      entity_id: number.garden_seasonal_adjustment
    data:
      value: >
        {{ {6: 1.3, 7: 1.5, 8: 1.3, 4: 0.7, 10: 0.6}.get(now().month, 1.0) }}
```

## How it differs from the standalone firmware

The safety layer is the same code. Everything a person sees is different.

- **Shared.** The output stage (`zf::hw::OutputStage`: expander register sequence,
  read-backs, relay sequencing, weld and shorted-output checks, self-test, polling cycle,
  over-current latch), the keep-alive guard (`zf::OutputEnableGuard`), the zone driver
  (`zf::ZoneDriver`: maximum run time with rest, stagger, master sequencing), the current
  calculation (`zf::CurrentSense`) and verdicts (`zf::CurrentMonitor`), the zero-cross
  tracker, the 24 VAC debounce, the restart record and the day rules (`zf::DayRule`).
- **Written for ESPHome.** The glue that the standalone firmware has in its controller,
  cut down to the hardware (which zone a fault belongs to, lockouts, trips per day,
  master lockout, leak alarm), and the scheduler, which drives the sprinkler component
  from entities instead of the standalone firmware's run queue.
- **Different on the device.** AC_ZC is sampled every 1 ms in the keep-alive interrupt
  instead of by a GPIO interrupt, because ESPHome installs the GPIO interrupt service
  without the IRAM flag and would refuse its own pins if another component installed it
  first. That resolution is enough for 24 VAC presence and the mains frequency check;
  zero-cross switching is off. There is no panic hook (see the table above). The I2C bus
  is ESPHome's, which has no bus-clear call after start-up: a stuck bus ends in the fault
  state, as it should, but is not cleared by firmware.

### Why the code is shared this way

ESPHome external components are copied into the build, so a copy of `zf_core` in
`firmware/esphome/components/` would drift. Instead, the `zoneflow` component adds
`firmware/zoneflow-fw/components/zf_core` to the ESP-IDF build as a local component, by
path. That directory already builds as an ESP-IDF component for the standalone
firmware, so both firmwares compile the same files with the same `CMakeLists.txt`.
ESPHome fetches a git source as a whole repository, so the path exists for users too; a
config that copies only `firmware/esphome/components` fails with a message that says
why. The cost is that the ESPHome build compiles all of `zf_core`, including parts it
does not use, which the linker then drops.

The ESPHome host tests run the ESPHome glue on the standalone firmware's simulated board
(`firmware/zoneflow-fw/test/host/fake_board.hpp`), so a change to `zf_core` is tested by
both suites. The esphome workflow runs on changes to `zf_core` as well.

Pin numbers come from `hardware/pinmap.yaml` through
`firmware/esphome/zoneflow/board-rev-a.yaml`, which
`firmware/esphome/tools/gen_board_yaml.py` writes. The script checks the pin map with the
standalone firmware's generator functions, and CI fails if the file is out of date.

## What has been checked

| Check | Result |
|---|---|
| `esphome config` and `esphome compile`, ESPHome 2026.10.2, ESP-IDF 5.5.6 | Pass for `examples/zoneflow-8zone.yaml`, `examples/zoneflow-4zone.yaml` and `factory.yaml`. The 8-zone application is 1,387,659 bytes (17 % of its 7.75 MB app partition); static RAM 166,019 of 341,760 bytes. |
| Keep-alive interrupt in the linked ELF | `on_timer` at 0x40376664 (IRAM), 338 instructions with the float test; no call instruction between its entry and `retw.n` |
| Rule 16 | `esphome config` refuses a `logger:` with `hardware_uart: UART1` (checked with a scratch copy of the 8-zone example) |
| Host tests, `firmware/esphome/test/host/run.sh` (ASan, UBSan) | 45 tests pass: 27 on the supervisor (start-up self-test, a run with master and relay, the maximum run time and rest across runs and a restart, open and shorted valves, three trips, a detector fault that needs a confirmed repair, a stalled main loop, loss of 24 VAC, OTA maintenance, at most three valve bits low, a 4-zone board, the leak alarm, the fault texts, from review A2 the float test, the gate bleed check, commissioning and rule 16, and from the firmware follow-up to the hardware answers an open valve that is stopped and reported without a lock-out, a DC component in the valve current, an output that failed short during another zone's run, a low supply and a lost measurement, and the run budget kept through a power cut) and 18 on the schedule logic (day rules, missed starts, DST, clock set back, edits, zone lists, next start) |
| `tools/gen_board_yaml.py --check` | The pin substitutions match `hardware/pinmap.yaml` |
| `zfrelease` tests, installer page test | Pass with the ESPHome flavour enabled |
| Release path, run locally | `zfrelease package-prebuilt` on the factory build (project name `zoneflow` checked in the image), `assemble` and `verify`: every check passes. The standalone half of a release was not built locally; CI builds both. |

Not checked, because it needs a board: everything about real timing (the I2C bus, the ADC
windows, the detector times), the display orientation, Improv over BLE and USB, the
encoder direction, how the push contacts chatter, the flow meter, and how long
ESPHome's main loop really stalls on this hardware. If a component in your YAML blocks the loop for more than 500 ms, the
valves stop and the output stage latches a fault.

## For developers

```sh
cd firmware/esphome
uv sync                                     # ESPHome 2026.10.2, pinned in pyproject.toml
uv run esphome compile examples/zoneflow-8zone.yaml
test/host/run.sh                            # host tests
uv run --script tools/gen_board_yaml.py     # after a pin map change
```

`firmware/esphome/README.md` describes the layout.
