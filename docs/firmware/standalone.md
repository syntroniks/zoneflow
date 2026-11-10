# Standalone firmware

!!! note "Phase 2, never run on hardware"
    `firmware/zoneflow-fw/` has the zone driver, scheduler, configuration and diagnostics (phase 1), and WiFi with BLE and SoftAP provisioning, the [REST API](api.md) with a live event stream, and OTA with rollback (phase 2), and [MQTT with Home Assistant discovery](mqtt.md) (phase 3). The display UI and the browser UI are not written yet. Nothing has run on a board; the logic is unit-tested on a PC. Work is tracked in [issue #6](https://github.com/syntroniks/zoneflow/issues/6); the design is in [firmware architecture](../design/firmware-architecture.md) and [firmware security](../design/firmware-security.md).

Planned scope, from the [specification](../design/system-spec.md):

- Zone driver that follows the safety rules (outputs off by default, hardware output-enable, maximum run time)
- Scheduler with programs, start times, weekday, interval and odd-even rules, per-zone durations, seasonal adjustment, rain delay and sensor inhibit. It runs from the RTC when the network is down
- Persistent configuration with a versioned schema
- WiFi provisioning over Bluetooth LE, with a fallback access point
- Local web UI, a documented REST API and a live status stream
- MQTT with Home Assistant discovery
- OTA update with rollback
- Colour display interface driven by the encoder and buttons: zone status, manual run, schedule viewing and editing, rain delay, faults, network setup
- Flow and current diagnostics: per-zone learned current, fault reporting
- Host-side unit tests for the scheduler and other pure logic
