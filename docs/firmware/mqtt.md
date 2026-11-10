# MQTT and Home Assistant

!!! warning "Not tested on hardware or with Home Assistant"
    The MQTT client is part of the standalone firmware, but no board exists
    yet, so the ESP-IDF client code has never run. What has been checked:
    host unit tests of the topic, payload, command and discovery logic, and a
    host build of that logic talking to a local **Mosquitto 2.1.2** broker
    (`firmware/zoneflow-fw/test/broker/run.sh`, see
    [what the broker test covers](#what-has-been-tested)). No real Home
    Assistant instance has received the discovery document; its shape is
    checked against the Home Assistant 2026.11.1 MQTT documentation.
    Tracked in [issue #6](https://github.com/syntroniks/zoneflow/issues/6).

The standalone firmware can publish its state to an MQTT broker and take
commands from it. With Home Assistant discovery turned on, Home Assistant
creates one device with all zones, programs, sensors and diagnostics, and
no YAML.

MQTT is optional. Watering, schedules and safety limits run on the
controller whether a broker is reachable or not.

## Setup

### 1. A broker

Any MQTT 3.1.1 broker works. The usual choice with Home Assistant is the
**Mosquitto broker** app in Home Assistant (Settings > Apps), or Mosquitto
on another machine. A minimal `mosquitto.conf` for a LAN:

```text
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Create a user for the controller:

```sh
mosquitto_passwd -c /etc/mosquitto/passwd zoneflow
```

### 2. The controller

Set the broker with the [REST API](api.md) (`PUT /api/v1/mqtt`, admin
login needed) or in the web UI once it exists:

```sh
curl -X PUT http://zoneflow-xxxx.local/api/v1/mqtt \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"enabled": true, "host": "192.168.1.10", "port": 1883,
       "username": "zoneflow", "password": "the password"}'
```

`GET /api/v1/mqtt` then shows the settings and the connection state
(`status.state` is `connected` when it works, `last_error` says why not).

| Setting | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Turn the client on. |
| `host` | | Broker name or IP address, without `mqtt://` or a port. |
| `port` | `1883` | Use `8883` for TLS on most brokers. |
| `username` | | Empty: connect without a user name. |
| `password` | | Write-only. Never returned by the API and not in the configuration backup. `null` removes it. |
| `tls` | `false` | Connect with TLS. |
| `ca` | `bundle` | With TLS: `bundle` checks the broker against the public CAs in the ESP-IDF certificate bundle; `custom` checks it against `ca_cert`. |
| `ca_cert` | | Write-only PEM CA certificate for `ca: custom`, for a broker with its own CA (up to 6 KB). |
| `client_id` | `zoneflow-<id>` | MQTT client id. `<id>` is the WiFi MAC address as 12 hex digits. |
| `base_topic` | `zoneflow/<id>` | Root of every topic below. |
| `discovery` | `true` | Home Assistant discovery. |
| `discovery_prefix` | `homeassistant` | Must match the prefix set in Home Assistant's MQTT options. |
| `ha_zone_entity` | `valve` | `valve` or `switch`, see [zones as valves](#zones-as-valves-or-switches). |

The settings other than `password` and `ca_cert` are part of the
configuration document (`mqtt` object, configuration schema 2) and of its
backup. The password and certificate are stored separately and must be set
again after a restore or a factory reset.

### 3. Home Assistant

1. Add the **MQTT** integration (Settings > Devices & services) and point it
   at the same broker. Discovery is on by default with the prefix
   `homeassistant`.
2. The controller appears as one device named after `device_name`. If it
   does not, check that the discovery prefix matches and that Home
   Assistant's birth message is on (MQTT options); see
   [discovery](#home-assistant-discovery).

## Topic reference

`<root>` is `base_topic`, by default `zoneflow/<id>`, for example
`zoneflow/a1b2c3d4e5f6`. All payloads are JSON unless noted. Zone numbers
are 1-based, as printed on the terminal block. The topic names and
payload fields are stable: fields may be added, never renamed.

### State (published by the controller)

| Topic | Retained | Sent | Payload |
|---|---|---|---|
| `<root>/status` | yes | on connect; last will | `online` or `offline` (plain text) |
| `<root>/device` | yes | on change, every 5 s | device: firmware, IP, RSSI, uptime, time status, master, inhibits |
| `<root>/zone/<n>` | yes | on change; every 5 s while running or resting | one zone |
| `<root>/program/<id>` | yes | on change | one program |
| `<root>/queue` | yes | on change; every 5 s while running | run queue |
| `<root>/sensors` | yes | on change; current every 5 s | rain, spare, 24 VAC, valve current |
| `<root>/flow` | yes | every 5 s when changed | flow rate and volume |
| `<root>/faults` | yes | on change | active faults |
| `<root>/rain_delay` | yes | on change; every 5 s while active | rain delay |
| `<root>/update` | yes | on change | firmware version and update progress |
| `<root>/event` | no | per event | controller events |
| `<root>/result` | no | per command | outcome of each command |

Examples:

`<root>/zone/1`:

```json
{"zone": 1, "name": "Lawn", "enabled": true, "state": "open", "valve": "open",
 "switch": "ON", "queued": true, "remaining_s": 412, "default_run_s": 600,
 "default_run_min": 10, "max_run_s": 3600, "last_stop": "none",
 "fault": "none", "fault_active": false, "locked_out": false,
 "resting": false, "rest_remaining_s": 0}
```

`state` is `off`, `waiting` (accepted, waiting for the master valve or the
delay between zones) or `open`. `valve` is the same in Home Assistant's
words (`closed`, `opening`, `open`).

`<root>/device`:

```json
{"name": "Back garden", "firmware": "0.3.0", "hostname": "zoneflow-e5f6",
 "ip": "192.168.1.30", "rssi": -61, "uptime_s": 86400, "time_status": "synced",
 "utc": 1790000000, "maintenance": false,
 "master": {"enabled": true, "on": true, "locked_out": false, "fault_count": 0},
 "inhibit": {"sensor": false, "no_ac": false, "maintenance": false, "master_lockout": false},
 "zone_count": 8, "config_revision": 12}
```

`<root>/program/3`:

```json
{"id": 3, "name": "Morning", "enabled": true, "running": false}
```

`<root>/queue`:

```json
{"state": "running", "length": 2, "current_remaining_s": 412, "pause_timeout_min": 120,
 "items": [{"zone": 1, "duration_s": 600, "source": "schedule", "program_id": 3},
           {"zone": 2, "duration_s": 300, "source": "schedule", "program_id": 3}]}
```

`<root>/sensors`:

```json
{"rain": {"enabled": true, "active": false}, "spare": {"enabled": false, "active": false},
 "ac_present": true, "valve_current_ma": 310, "current_sense_available": true,
 "over_current_tripped": false}
```

`valve_current_ma` is `null` when there is no reading.

`<root>/flow`:

```json
{"enabled": true, "rate_lpm": 11.25, "total_l": 1532.4}
```

`total_l` counts from boot.

`<root>/faults`:

```json
{"active": ["zone_open_circuit"], "zones": [{"zone": 4, "fault": "zone_open_circuit",
 "locked_out": false}], "master_locked_out": false, "any": true}
```

`<root>/rain_delay`:

```json
{"active": true, "until_utc": 1790086400, "remaining_s": 86000, "hours": 24}
```

`<root>/update`:

```json
{"installed_version": "0.3.0", "latest_version": "0.3.0", "title": "ZoneFlow firmware",
 "release_url": "https://github.com/syntroniks/zoneflow/releases",
 "in_progress": false, "update_percentage": null}
```

`<root>/event`:

```json
{"type": "zone_changed", "utc": 1790000000, "zone": 1, "state": "open", "reason": "none"}
```

Event types and fields are the same as in the REST event stream
([API reference](api.md#live-event-stream)).

### Commands (sent to the controller)

| Topic | Payload | Does |
|---|---|---|
| `<root>/cmd/zone/<n>/run` | seconds, `600`, or `{"duration_s": 600}` | Run zone n (1 to 14 400 s). |
| `<root>/cmd/zone/<n>/set` | `OPEN`/`ON` or `CLOSE`/`OFF`/`STOP` | Open runs the zone for its `default_run_s`; close stops it. Used by Home Assistant. |
| `<root>/cmd/zone/<n>/stop` | anything not empty | Stop zone n if it is the running queue entry. |
| `<root>/cmd/zone/<n>/default_run_s` | seconds | Set the zone's default run time (stored in the configuration). |
| `<root>/cmd/program/<id>/run` | anything not empty | Run program id now. |
| `<root>/cmd/program/<id>/enabled` | `ON`/`OFF` | Enable or disable program id (stored). |
| `<root>/cmd/stop_all` | anything not empty | Stop everything and empty the queue. |
| `<root>/cmd/pause` | anything not empty | Pause the queue. |
| `<root>/cmd/resume` | anything not empty | Resume it. |
| `<root>/cmd/skip` | anything not empty | Skip the running entry. |
| `<root>/cmd/rain_delay` | hours `24`, `{"hours": 24}` or `{"until_utc": 1790086400}`; `0` clears | Set or clear the rain delay (at most 720 h). |
| `<root>/cmd/clear_faults` | anything not empty | Clear latched faults and lockouts. |

Every command gets one message on `<root>/result`:

```json
{"command": "zone_run", "status": "queued", "error": null, "zone": 1}
```

| `status` | Meaning |
|---|---|
| `queued` | Handed to the controller. If the controller refuses it, a `rejected` message follows, usually within half a second. |
| `applied` | Configuration edit accepted; written to flash within 2 s. |
| `rejected` | The controller refused it; `error` is its reason: `locked_out`, `zone_resting`, `master_locked_out`, `inhibited`, `already_queued`, `queue_full`, `nothing_to_run`, `unknown_program`, ... |
| `invalid` | Bad topic or payload; `error` says what, for example `duration_out_of_range`, `no_such_zone`, `zone_disabled`, `not_running`, `empty_payload`. |
| `ignored_retained` | The message was retained; see below. |
| `rate_limited` | More than the command budget (10 at once, then 5 per second). |
| `busy` | The command queue was full; try again. |

A rejection by the controller looks like this, and is sent for every
refused command, whichever interface sent it:

```json
{"command": "run_zone", "status": "rejected", "error": "zone_resting", "zone": 3}
```

### Safety rules for commands

- **Retained messages are never commands.** A retained `run` left on the
  broker would otherwise start watering every time the controller
  reconnects. Send commands without the retain flag.
- **Empty messages are never commands.** Deleting a retained message (an
  empty retained publish) reaches every connected client as an empty
  message; it must not start or stop anything. This rule was added after
  the broker test showed that clearing a retained `run` started the zone.
- **Limits are the controller's.** An MQTT run is clamped like any other:
  to the zone's `max_run_s` and to what is left of its run-time budget
  before the rest time (spec section 2, rule 4). A zone that is locked out
  or resting is refused (`rejected`).
- **A flood cannot starve the controller.** At most 10 commands at once and
  5 per second are taken; the rest are answered `rate_limited`. The control
  loop never waits on MQTT; the MQTT task posts to the same 16-entry queue
  as the REST API.

## Home Assistant discovery

The controller sends one **device discovery** message (format of the Home
Assistant MQTT documentation for 2026.11) to
`<prefix>/device/zoneflow_<id>/config`:

- when it connects to the broker, and
- 1 to 5 s (random) after Home Assistant's birth message (`online` on
  `<prefix>/status`), as the Home Assistant documentation recommends.

It is not retained, so no stale discovery stays on the broker. If Home
Assistant's birth message is turned off, save the MQTT settings again
after restarting Home Assistant; any configuration change resends
discovery.

### Entities

| Entity | Type | Notes |
|---|---|---|
| Each enabled zone | `valve` (or `switch`) | Open runs the zone for its run time. |
| *Zone* run time | `number`, minutes, 1 to 240 | The zone's `default_run_s`. Configuration entity. |
| *Zone* fault | `binary_sensor`, problem | Attributes: fault, locked out, resting, rest left. Diagnostic. |
| Run *program* | `button` | One per program. |
| *Program* enabled | `switch` | Configuration entity. |
| Stop all, Pause, Resume, Skip, Clear faults | `button` | |
| Rain delay | `number`, hours, 0 to 720 | 0 clears it. |
| Rain sensor | `binary_sensor`, moisture | Only when the rain input is enabled. |
| Spare input | `binary_sensor` | Only when enabled. |
| Flow rate | `sensor`, volume flow rate, L/min, measurement | Only when the flow meter is enabled. |
| Water used | `sensor`, water, L, total increasing | Counts from boot. |
| Valve current | `sensor`, current, mA, measurement | Diagnostic. |
| 24 VAC supply | `binary_sensor`, power | Diagnostic. |
| Fault | `binary_sensor`, problem | Any fault; the list is in its attributes. |
| Queue | `sensor`, enum: idle, running, paused | Queue items in its attributes. |
| WiFi signal | `sensor`, signal strength, dBm | Diagnostic. |
| Firmware version | `sensor` | Diagnostic. |
| Firmware | `update` | Shows the installed version and the progress of an update started from the web UI or REST API. It has no install button: installing stays behind the controller's admin login. |

All entities share the availability topic `<root>/status`, so they show as
unavailable when the controller is off line.

A zone that is disabled, or above `zone_count`, and a deleted program are
removed from Home Assistant: the controller sends the discovery message
once with that component reduced to its `platform`, then again without it.
A program deleted while the controller was switched off is not known at the
next start and stays in Home Assistant as unavailable; delete it there.

### Zones as valves or switches

Zones are `valve` entities by default. A zone is a water valve with three
commands that match the controller: open (run for the zone's run time),
close (stop) and stop. A valve also shows `opening` while the controller
waits for the master valve or the delay between zones, which a switch
cannot show. Valve entities exist since Home Assistant 2024.1.

Set `ha_zone_entity` to `switch` for dashboards, cards or automations built
for switches (for example older irrigation cards or `switch.turn_on`
automations). ON runs the zone for its run time, OFF stops it.

## Example automations

Entity ids depend on the device and zone names; replace `back_garden_*`
with yours.

Water zone 2 for 15 minutes at sunset, whatever its run time setting is:

```yaml
automation:
  - alias: "Beds at sunset"
    triggers:
      - trigger: sun
        event: sunset
    actions:
      - action: mqtt.publish
        data:
          topic: zoneflow/a1b2c3d4e5f6/cmd/zone/2/run
          payload: '{"duration_s": 900}'
```

Set a 48-hour rain delay when the forecast says rain:

```yaml
automation:
  - alias: "Rain delay from forecast"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.rain_probability_today
        above: 70
    actions:
      - action: number.set_value
        target:
          entity_id: number.back_garden_rain_delay
        data:
          value: 48
```

Tell me when a zone faults:

```yaml
automation:
  - alias: "ZoneFlow fault"
    triggers:
      - trigger: state
        entity_id: binary_sensor.back_garden_fault
        to: "on"
    actions:
      - action: notify.notify
        data:
          message: >
            ZoneFlow fault: {{ state_attr('binary_sensor.back_garden_fault', 'active') }}
```

Report rejected commands:

```yaml
automation:
  - alias: "ZoneFlow command rejected"
    triggers:
      - trigger: mqtt
        topic: zoneflow/a1b2c3d4e5f6/result
    conditions:
      - condition: template
        value_template: "{{ trigger.payload_json.status == 'rejected' }}"
    actions:
      - action: notify.notify
        data:
          message: "ZoneFlow refused {{ trigger.payload_json.command }}: {{ trigger.payload_json.error }}"
```

## Connection behaviour

- The client starts once the controller has an IP address.
- After a lost connection it retries after 2 s, doubling up to 5 minutes,
  each delay varied by up to 20 % so many devices do not retry together.
- Last will: the broker publishes `offline` (retained) on `<root>/status`
  if the controller drops off without saying goodbye.
- While the broker is slow or away, nothing piles up in memory: the
  controller remembers only which state topics changed and sends their
  newest value when it can. Events and command results are not kept.
- Changing `base_topic` leaves the old retained topics on the broker.
  Remove them with `mosquitto_pub -r -n -t <old topic>` or a broker tool.

## What has been tested

| Check | Covers |
|---|---|
| Host unit tests (`test/host/mqtt_*_test.cpp`, `web_mqtt_api_test.cpp`) | Topic names, every payload, command parsing and limits, retained and empty messages, rate limiter, reconnect backoff, configuration schema 2 and its migration, discovery document against the documented Home Assistant options, removal of components, REST settings with a write-only password. |
| Broker test (`test/broker/run.sh`, Mosquitto 2.1.2) | A host build of the MQTT logic and the real controller core, connected to Mosquitto: availability and last will, discovery published and not retained, a retained command left on the broker ignored, clearing it not treated as a command, a run that opens a zone, a controller rejection on the result topic, invalid commands, a 200-message flood rate-limited, discovery again after a Home Assistant birth message, retained state for a late subscriber. |
| Not tested | The ESP-IDF client code (`zf_mqtt/src/mqtt.cpp`) and esp-mqtt on a board, TLS, a real Home Assistant, memory use on the device. |
