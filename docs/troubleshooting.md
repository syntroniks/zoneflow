# Troubleshooting

!!! warning "Written from the design; not yet verified on hardware"
    The messages on this page are the ones in the firmware: the display's
    string table (`zf_ui/strings.hpp`), the web interface's
    (`web/src/i18n/en.ts`) and the fault codes in `zf_core`. What causes
    each one comes from the [hardware design](design/hardware-design.md)
    and the firmware source. No fault has been produced on a real board.

!!! danger "If water will not stop"
    Unplug the transformer. With no 24 VAC no valve can open. Then close
    the water supply if it keeps flowing, and read
    [a valve does not close](#a-valve-does-not-close).

## How to read a fault

Each fault is shown with a title that says what happened and a line that
says what to check.

- **Display (standalone):** Home shows the fault in red, and **Faults** opens it. **Menu → Faults** lists them all, one row each. A fault on the valve board adds a line "Step N: check name" for whoever repairs it.
- **Web interface:** **Needs attention** on the dashboard. The history page records every fault with its time.
- **ESPHome:** the `Faults` text entity, `Fault` and `Zone N fault` binary sensors, and the display's status line.
- **MQTT and REST:** the fault codes in the right-hand column of the [fault table](#fault-messages), for example `zone_open_circuit`.

### How to clear a fault

| Kind | Display | Web interface | ESPHome | API, MQTT |
|---|---|---|---|---|
| Most faults | **Faults →** the fault **→ Clear**, or **Clear all faults** | **Clear faults** and confirm | `Clear faults` button, or hold **Home** for 3 s | `POST /api/v1/faults/clear`, `<root>/cmd/clear_faults` |
| Safety circuit failed its test | **Repaired**, then **Yes, repaired** | Confirm on the controller, or `POST /api/v1/faults/acknowledge-repair` | `Acknowledge repair (press twice)` within 10 s | `POST /api/v1/faults/acknowledge-repair` |
| No 24 V AC supply, Clock not set, Valve current not measured, Sensor supply overloaded, Mains signal wrong | Clear by themselves when the cause goes | The same | The same | The same |

Clearing releases every latched fault and lockout at once. A fault whose
cause is still there comes back at once. Fix the cause first.

## Fault messages

The display's title is on the left, the code used by the API and MQTT on the
right. "Zone N" is the zone's number; a zone-type fault on the master output
reads "Master valve: ...".

| Display title | Web interface title | Code | Clears itself |
|---|---|---|---|
| [Zone N: no current](#zone-n-no-current) | No current through a valve | `zone_open_circuit` | No |
| [Zone N: current too high](#zone-n-current-too-high) | Too much current through a valve | `zone_over_current` | No |
| [Zone N: short circuit](#zone-n-short-circuit) | Short circuit | `short_circuit` | No |
| [Protection tripped](#protection-tripped) | Over-current protection tripped | `over_current_trip` | No |
| [Master valve locked](#master-valve-locked) | Master valve locked out | `master_locked_out` | No |
| [Current with all valves off](#current-with-all-valves-off) | Current with every valve off | `current_while_idle` | No |
| [No 24 V AC supply](#no-24-v-ac-supply) | No 24 V AC power | `ac_lost` | Yes |
| [Water flows with all valves off](#water-flows-with-all-valves-off) | Water flowing with every valve closed | `flow_without_zones` | No |
| [Clock not set](#clock-not-set) | The clock is not set | `time_invalid` | Yes |
| [Settings could not be read](#settings-could-not-be-read) | The stored settings could not be read | `config_invalid` | No |
| [Settings could not be saved](#settings-could-not-be-saved) | Settings could not be saved | `storage_error` | No |
| [Valve current not measured](#valve-current-not-measured) | Valve current is not being measured | `current_sense_unavailable` | Yes |
| [Sensor supply overloaded](#sensor-supply-overloaded) | Sensor supply overloaded | `sensor_supply` | Yes |
| [Zone N: valve current lopsided](#zone-n-valve-current-lopsided) | Valve current in one direction only | `zone_dc_current` | No |
| [Mains signal wrong](#mains-signal-wrong) | Mains timing signal wrong | `mains_signal` | Yes |
| [Safety circuit failed its test](#safety-circuit-failed-its-test) | Safety circuit failed its self-test | `keepalive_detector` | No, needs a confirmed repair |
| [Valve driver not responding](#valve-driver-not-responding) | Valve driver not responding | `output_expander` | No |
| [Valve relay stuck closed](#valve-relay-stuck-closed) | Valve relay stuck closed | `relay_welded` | No |
| [Valve output stuck on](#valve-output-stuck-on) | Valve output stuck on | `output_shorted` | No |
| [Protection did not reset](#protection-did-not-reset) | Over-current protection did not reset | `trip_latch_stuck` | No |

### Valve and wiring faults

#### Zone N: no current

> The valve drew no current, so it did not open. Check: valve wire may be
> disconnected or cut, or the solenoid has failed.

- **Detected:** the zone was on, but after the 500 ms inrush allowance the current stayed below the zone's minimum (80 mA by default). The zone stops and is locked out.
- **Likely causes:** a wire loose at the terminal or at the valve; a cut cable; a corroded splice; a burnt-out solenoid; the zone has no valve; the minimum is set above what this valve draws.
- **Check:**
    1. Unplug the transformer.
    2. Pull the valve plug (J2 or J3) off the board.
    3. Measure the resistance between COM and the zone's terminal on the plug.
    4. No continuity: the wire or the solenoid is open. Measure again at the valve box, across the solenoid's two leads, to find which.
    5. A reading: compare it with the valve maker's data for that solenoid. The repository gives no resistance range.
    6. If the reading is right, check the zone's current limits (web interface, **Zones**). A valve with a small solenoid can draw less than 80 mA.
- **Clear:** fix the cause, then clear the fault.

#### Zone N: current too high

> The zone was switched off and locked. Check: damaged solenoid, or two
> valves on one terminal.

- **Detected:** the current stayed above the zone's maximum (700 mA by default) after the inrush allowance, but below the short-circuit limit (3000 mA).
- **Likely causes:** a damaged solenoid with shorted turns; two valves on one zone terminal; water in a splice; the maximum is set too low for this valve.
- **Check:**
    1. Unplug the transformer and pull the valve plug.
    2. Measure the resistance between COM and the zone's terminal. Compare with the valve maker's data. A value well below it points to the solenoid or the wiring.
    3. Check that only one valve is on this zone wire.
    4. Open the splices at the valve box and look for water.
- **Clear:** fix the cause, then clear the fault. If the valve is right and draws more than the default, raise the zone's maximum in the web interface.

#### Zone N: short circuit

> All valves were switched off and this zone is locked. Check: the zone
> wire may touch the common wire.

- **Detected:** the firmware measured more than the short-circuit limit, 3000 mA RMS, with this zone on. Everything stops and the zone is locked out.
- **Likely causes:** the zone wire touches the common wire, in the cable or at a splice; a cable cut by a spade; a solenoid shorted internally.
- **Check:**
    1. Unplug the transformer and pull the valve plug.
    2. Measure the resistance between COM and the zone's terminal. Near zero confirms a short.
    3. Disconnect the valve at the valve box and measure again. Still near zero: the cable is shorted. Now open: the solenoid is shorted.
- **Clear:** fix the cause, then clear the fault.

#### Protection tripped

> A short made the board switch all valves off for a moment. The other zones
> carry on. A zone with three trips in a day stays off. Check: valve wires
> touching each other or the common wire. Then clear the fault.

- **Detected:** the hardware over-current cut-off tripped. It removes drive from every output about 70 microseconds after the current passes 4.0 to 5.4 A peak, and the relay in the valve common opens. Firmware blames the output switched on in the last 500 ms; otherwise it marks every output that was on as suspect. It then carries on with the others.
- **Likely causes:** as for a short circuit, but the short draws so much current that the hardware acts first: a dead short on a short cable from a large transformer.
- **Check:** as for [short circuit](#zone-n-short-circuit).
- **Clear:** fix the cause, then clear the fault. After three trips in one day an output stays off until cleared.

#### Master valve locked

> The master valve failed several times in a row, so no zone can run.
> Check: master valve wiring and solenoid. Then clear the fault.

- **Detected:** three short-circuit or over-current faults in a row on the master output (the default). Because every zone needs the master, no zone runs.
- **Likely causes:** as for the zone faults above, on the MV wire or the master valve or pump-start relay coil.
- **Check:** measure between COM and MV on the unplugged J2 plug, as above.
- **Clear:** fix the cause, then clear the fault.

#### Zone N: valve current lopsided

> The valve current flowed in one direction only, so the output that drives
> it is failing. The zone was switched off. The board needs service.

- **Detected:** the zone's current had a DC part: the output switch conducts on one half of the AC cycle only.
- **Likely causes:** the zone's triac or its opto-triac is failing.
- **Check:** nothing to check in the wiring. The board needs repair of that output.
- **Clear:** latched. Clearing lets the zone try again; it will come back if the part is failing.

### Supply and board faults

#### No 24 V AC supply

> Valves cannot open. Watering resumes when power returns. Check:
> transformer plugged in, fuse, supply wires.

- **Detected:** the 24 VAC signal has had no edge for 25 ms. The signal is taken after fuse F4, so a blown F4 reads the same as no transformer.
- **Likely causes:** the transformer is unplugged or its socket is off; the board runs from USB alone; a loose wire in the J1 plug; a blown fuse; a failed transformer.
- **Check:**
    1. Check the transformer is plugged in and the socket is on.
    2. Measure AC volts across the two wires at the J1 plug, plug pulled. The supply is 24 VAC nominal; an unloaded transformer can read higher, up to the 32 VAC the board is designed for.
    3. Unplug the transformer. Take both fuses out of the fuse well and check each for continuity. Either can open on a fault, so check both.
    4. If a fuse has opened, find out why before you replace it: a short on a valve wire, or a wire from the transformer or COM touching a sensor GND terminal.
- **Clear:** clears itself when 24 VAC returns. Scheduled starts missed meanwhile are skipped.

#### Current with all valves off

> A valve output may be stuck on. If water is flowing, unplug the 24 V
> supply. The board needs service.

- **Detected:** current above the idle threshold while no output is on.
- **Likely causes:** an output's triac, surge clamp or snubber capacitor has failed short. That is the usual way a triac dies after a surge or an overload.
- **Check:** if water flows, unplug the transformer. Then see [valve output stuck on](#valve-output-stuck-on).
- **Clear:** the board needs repair.

#### Valve output stuck on

> Current flowed with every valve off: a valve output on the board is
> shorted. The relay was opened and nothing runs. The board needs service.

- **Detected:** at the start of a run, with the relay closed and every output off, current flowed (self-test step 6).
- **Likely causes:** a shorted triac, surge clamp, opto-triac or snubber capacitor on one output.
- **What protects you:** the relay in the valve common is open, so no valve has power, whatever the shorted part does.
- **Clear:** the board needs repair. Nothing runs until then.

#### Valve relay stuck closed

> Current flowed although the valve relay should have been open: a welded
> relay contact or a bridged jumper. Nothing runs. The board needs service.

- **Detected:** before a run, with the relay told to stay open, the firmware switched the first output on for 100 ms and measured current (self-test step 5).
- **Likely causes:** the relay contact welded, for example after breaking a short-circuit current; the relay's bypass jumper bridged; the relay's driver transistor shorted.
- **Clear:** the board needs repair.

#### Protection did not reset

> The short-circuit protection stayed tripped after the valves were switched
> off. Nothing runs. Clear the fault to try again. If it comes back, the
> board needs service.

- **Detected:** the over-current latch was still set 3 seconds after the keep-alive stopped. The latch clears only when the keep-alive stops.
- **Likely causes:** a short still present that trips the latch again, or a fault in the latch or its detector.
- **Clear:** clear the fault to try again. If it comes back, the board needs repair.

#### Valve driver not responding

> The chip that drives the valves did not answer or did not read back what
> was written. All valves were switched off. Clear the fault to try again.
> If it comes back, the board needs service.

- **Detected:** the output expander did not acknowledge, or its registers did not read back as written, or the I2C bus was stuck.
- **Likely causes:** a fault on the board, or a device on the Qwiic connector holding the bus.
- **Check:** unplug anything on the Qwiic connector, then clear the fault.
- **Clear:** clear the fault to try again.

#### Safety circuit failed its test

> The circuit that turns the valves off if the controller stops failed its
> self-test. Nothing runs. Repair the board, then confirm it here.

- **Detected:** one of the two keep-alive detectors did not behave as designed in a self-test: it was on when it should be off, did not come on within 150 ms, or did not let go within its time window when the pulses stopped.
- **Why it matters:** those detectors are what turn the valves off when the firmware crashes. With one of them unreliable, a crash could leave a valve on.
- **Clear:** this fault does not clear with one press. After the board has been repaired and checked, confirm the repair: on the display, **Repaired**, then **Yes, repaired** (Cancel is focused first). In ESPHome, press `Acknowledge repair (press twice)` twice within 10 seconds. The full self-test runs before any valve opens.

![Fault detail for a failed safety circuit](design/display-ui/fault-detail-detector.png){ width="300" }

The "Step N" line names the failed check. For whoever repairs the board:

| Line | Meaning | Section of the hardware design |
|---|---|---|
| Step 1: `idle_drive_enabled`, `idle_relay_on`, `idle_expander_answers` | A detector, or the expander, was on with the keep-alive stopped | 19.7 step 1 |
| Step 2: `pin_high_drive_enabled`, `pin_high_relay_on` | A detector came on with the keep-alive pin held high; a shorted pump capacitor or an open bleed resistor | 19.7 step 2 |
| Step 3: `start_drive_timeout`, `start_relay_timeout`, `start_no_acknowledge` | A detector or the expander did not come on within 150 ms of the first pulse | 19.7 step 3 |
| Step 4: `expander_inputs`, `expander_outputs`, `expander_config` | The expander's pins or registers did not read as expected | 19.7 steps 4a to 4c |
| Step 5: `relay_welded` | Current with the relay open | 19.7 step 5 |
| Step 6: `relay_no_readback`, `output_shorted` | The relay bit did not read back, or current with no output on | 19.7 step 6 |
| Step 7: `run_readback`, `run_drive_lost`, `run_relay_lost` | During a run, a register or a detector changed by itself | 19.7 step 7 |
| Step 8: `end_no_acknowledge` | The expander did not accept the end-of-run writes | 19.7 step 8 |
| Step 9: `stop_drive_release`, `stop_relay_release`, `stop_expander_answers` | A detector let go outside its window (0.3 to 1.0 s, 0.08 to 1.0 s), or the expander still answered | 19.7 step 9 |

Steps 1, 2 and 9 raise "Safety circuit failed its test" and need the
confirmed repair. Other steps raise the matching fault above.

#### Mains signal wrong

> The 24 V AC timing signal is not at mains frequency. Zero-cross switching
> and current readings may be off. The board needs service.

- **Detected:** the 24 VAC timing signal is present but not at 50 or 60 Hz. A reading of 100 Hz, or edges that follow the keep-alive, means the timing signal is bridged to the keep-alive signal on the board.
- **Clear:** clears itself if the signal returns to normal. If it stays, the board needs repair.

#### Valve current not measured

> The board gets no current reading. Zones still run, but a broken or
> shorted valve is not noticed. If it stays, the board needs service.

- **Detected:** the firmware gets no valid readings from the current measurement.
- **What still works:** zones run, and the hardware over-current cut-off still acts on a short. Open circuits and over-current below the hardware limit are not detected.
- **Clear:** clears itself. If it stays, the board needs repair.

### Sensor and flow faults

#### Water flows with all valves off

> The flow meter counts water although nothing is running. Check: a leak,
> or a valve that does not close.

- **Detected:** the flow meter measured more than 0.5 L/min for 60 seconds with every valve closed (defaults).
- **Likely causes:** a leak downstream of the meter; a valve that does not close fully; someone using a tap on the same line.
- **Check:** look at each zone in the garden; close the supply and see whether the flow stops.
- **Clear:** fix the cause, then clear the fault.

#### Sensor supply overloaded

> The 5 V terminal for sensors is overloaded or too hot. Check: sensor wiring
> and how much current the sensors draw (100 mA at most).

- **Detected:** the current-limited switch on the 5V terminal reports over-current or over-temperature. It limits at 116 to 158 mA, and a resettable fuse behind it can trip earlier when the enclosure is hot.
- **Likely causes:** a shorted sensor cable; a sensor that draws more than 100 mA; the 5V wire on a GND terminal.
- **Check:** unplug the sensor plug (J4), measure the resistance between the 5V and GND wires of the flow meter cable. Near zero is a short.
- **Clear:** clears itself when the overload goes.

### Settings and clock faults

#### Clock not set

> Programs do not start until the time is known. Connect WiFi, or set the
> time under Setup, Time.

- **Detected:** the controller has no valid time: no network time yet, and the backup clock is not set or lost its time.
- **Likely causes:** a new board; the CR2032 cell is missing, flat or loose.
- **Check:** connect the controller to your network, or set the time by hand (**Menu → Setup → Time → Set the clock**; web interface **Settings → Time**). If the clock is lost after every power cut, replace the cell ([assembly, Later](enclosure/assembly.md#later)).
- **Clear:** clears itself once the time is set. Manual runs work meanwhile.

#### Settings could not be read

> The stored settings were rejected and defaults are in use. Set up zones and
> programs again, or restore a backup.

- **Detected:** the stored configuration failed validation at start-up. The controller runs with defaults.
- **Fix:** restore a backup ([backup and restore](guide/backup-restore.md)), or set the zones and programs again. A backup made by newer firmware is refused by older firmware.

#### Settings could not be saved

> Writing to memory failed. Try again. If it keeps failing, the board needs
> service.

- **Fix:** try again. If it keeps failing, the board's flash memory needs checking.

## Other symptoms

### The display is dark

- The screen dims and then turns off after a while without input (set under **Menu → Setup → Display**). Turn or press the knob: the first input only wakes the screen.
- Nothing on the screen and the status light dark: no power. Check the transformer and both fuses, as for [no 24 V AC supply](#no-24-v-ac-supply). On USB alone the display still works.
- The status light flashes but the screen stays dark: the display cable may be out of its connector. Open the fascia and check the cable lock ([assembly](enclosure/assembly.md#3-display)).

### Home says "Controller not responding"

![Controller not responding](design/display-ui/home-no-data.png){ width="300" }

The display has had no status from the controller core for 5 seconds. It
does not know the valve state and offers **Stop**. Press Stop. If it stays,
unplug the transformer and plug it in again; the hardware turns every valve
off when the firmware stops.

### A zone will not start

The display, web interface and API say why. The reasons are:

| Message | Meaning |
|---|---|
| zone locked by a fault / is locked out by a fault | A fault on this zone is latched. Fix it and clear faults. |
| zone is resting / resting after its maximum run time | The zone reached its maximum run time, counted across runs, and must stay closed for the rest time (30 minutes by default). |
| zone disabled | Enable it in Zones. |
| No 24 V AC supply / no power | See [no 24 V AC supply](#no-24-v-ac-supply). |
| Update running / maintenance | A firmware update is in progress. Watering is off until it ends. |
| Master valve locked | See [master valve locked](#master-valve-locked). |
| The valve board failed a self-test | See the fault, then acknowledge it. |

### A program did not run

The history page (web interface) and **Schedule status** (ESPHome) give the
reason a start was skipped:

| Reason | What to do |
|---|---|
| rain delay | A rain delay is set. End it, or wait. |
| rain sensor (wet) | The rain sensor reads wet. If it is dry outside, check the sensor's contact type setting and its wiring. |
| no 24 V AC | The transformer was off at the start time. |
| update running | A firmware update was in progress. |
| already running | The program was still running from its previous start. |
| queue full | Too many runs were queued. |
| nothing to run | The program has no zone with a run time, or every zone in it is disabled. |
| master valve locked | See [master valve locked](#master-valve-locked). |
| board fault | The valve board has a latched fault. |

Also check that the clock is set, the time zone is right, and the program is
enabled. After a power cut the controller runs a missed start late only if
it is at most 60 minutes old; older starts are skipped and recorded as
missed.

### The rain sensor always reads wet, or never

1. Check the contact type setting: most sensors are normally closed.
2. With a normally closed setting and nothing connected, the input reads wet. Turn the sensor off in the settings if none is connected.
3. Pull the sensor plug (J4) and measure the sensor between its RAIN and GND wires: dry and wet should give opposite readings (continuity, then none, or the reverse).
4. The input waits 2 seconds before it changes state.

### The flow meter reads nothing

1. Check that the flow meter is switched on in the settings and the pulses per litre are set.
2. A powered meter needs the 5V terminal, which is off on USB power alone.
3. Check the meter's wiring against [wiring, flow meter](getting-started/wiring.md#5-flow-meter).

### A valve does not close

1. Unplug the transformer. If the valve still runs, the valve itself is stuck open. That is a valve problem: see the valve maker's instructions.
2. If the valve closes with the transformer unplugged and opens again when you plug it in with no zone running: the board should report [current with all valves off](#current-with-all-valves-off) or [valve output stuck on](#valve-output-stuck-on) and open its relay. Leave the transformer unplugged and have the board repaired.

### A fuse keeps opening

A fuse that opens again after replacement points to a short:

- Between a valve wire and COM, in the cable, a splice or a solenoid.
- A wire from the transformer or COM touching a sensor GND terminal. COM is the other transformer wire.
- With a computer on USB: an earth fault path. Read the [USB rules](safety.md#usb-and-earthing) and pull the USB cable.

Check each with the transformer unplugged and the plugs off the board.

### The web interface does not open

- Use the IP address from your router, or from **Menu → Setup → Network** on the display. Some networks block the `.local` name.
- The controller must be on the same network as your phone or computer.
- If the page says it lost its connection, it shows the last known state in grey and reconnects by itself.
- After a restart or an update everyone is signed out: sign in again.

### Joining the network fails

See [joining your network, if it does not work](getting-started/provisioning.md#if-it-does-not-work).

### Installing firmware fails

See [installing firmware](firmware/web-installer.md#if-the-browser-cannot-connect)
and [recover a board that does not start](firmware/releases.md#recover-a-board-that-does-not-start).

## Still stuck

Open an issue at
[github.com/syntroniks/zoneflow/issues](https://github.com/syntroniks/zoneflow/issues)
with the fault title, the "Step" line if there is one, the firmware version
(**Menu → Setup → About**), and what you measured.
