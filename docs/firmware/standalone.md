# Standalone firmware

!!! note "Not written yet"
    This page is not written yet. The firmware is tracked in [issue #6](https://github.com/syntroniks/zoneflow/issues/6). No firmware has been written or run on hardware.

Planned scope, from the [specification](../design/system-spec.md):

- Zone driver that follows the safety rules (outputs off by default, hardware output-enable, maximum run time)
- Scheduler with programs, start times, weekday, interval and odd-even rules, per-zone durations, seasonal adjustment, rain delay and sensor inhibit. It runs from the RTC when the network is down
- Persistent configuration with a versioned schema
- WiFi provisioning over Bluetooth LE, with a fallback access point
- Local web UI, a documented REST API and a live status stream
- MQTT with Home Assistant discovery
- OTA update with rollback
- OLED and local controls: status, manual run, network info
- Flow and current diagnostics: per-zone learned current, fault reporting
- Host-side unit tests for the scheduler and other pure logic
