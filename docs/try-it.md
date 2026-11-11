---
title: Try it in your browser
hide:
  - toc
---

# Try it in your browser

<p class="zf-lead">The controller's firmware, running in this browser tab. Turn the knob, open the web interface, cut a valve wire and see what the controller does about it. No hardware, no account, nothing to install.</p>

<p markdown="0"><a class="md-button md-button--primary" href="../demo/">Open the live demo</a></p>

!!! danger zf-status "A simulation. Not a test of hardware."
    No ZoneFlow board has been built. The demo shows how the firmware's logic
    behaves on simulated hardware. It says nothing about whether a real board
    works, and nothing in it is a measurement.

## What you get

Three views of one simulated controller, side by side:

- **The controller on the wall.** The 320 x 240 display, the knob, the Back
  and Manual buttons and the output lights. The display is drawn by the same
  code and the same LVGL version as on the device.
- **The garden.** Six zones with valves, a flow meter and a rain sensor. A
  sprinkler runs when the simulated triac conducts, the valve-common relay is
  closed, 24 VAC is present and the wire is whole.
- **The phone.** The web interface, the same build the controller serves,
  talking to the firmware's own REST API and event stream.

You can also change the world around the controller: make it rain, switch the
WiFi off, unplug the transformer, cut or short a valve wire, spring a leak,
or let an output device fail short. Time runs at up to 600 times normal
speed, and **Next watering** runs the clock forward to the next start.

The demo garden has an admin password, `zoneflow`, and signs the phone in for
you. Settings and the event history are kept in your browser's storage, as
they would be in the controller's flash. Nothing is sent anywhere.

## What runs, and what is simulated

| Part | In the demo |
|---|---|
| Scheduler, zone driver, run-time limits, fault handling (`zf_core`) | The firmware's code, compiled to WebAssembly |
| Output-stage sequencing and fail-safe self-tests | The firmware's code, on a model of the rev A output stage |
| Display logic and drawing (`zf_display`, LVGL 9.6.0) | The firmware's code |
| REST API, sessions, password hashing, CSRF check (`zf_web`) | The firmware's code |
| Web interface | The same files the controller serves |
| Event history | The firmware's code, stored in the browser instead of flash |
| Valves, currents, relay, keep-alive detectors, over-current comparator | A model: the one the firmware's host tests use |
| Flow meter, rain sensor, 24 VAC present | Values the page sets |
| HTTP server, event-stream task, start-up wiring | Rewritten for the browser, following the device code |
| WiFi, Bluetooth LE provisioning, TLS, MQTT, firmware update | Not in the demo |

So the demo can show you what the controller does when a wire is cut. It
cannot show you that a real wire, triac or relay behaves like the model. The
valve currents (about 250 mA) and flows are invented values of a plausible
size.

Limits worth knowing:

- **Timing is not real.** The firmware runs in 50 ms steps on one thread. On
  the device, several tasks run on two cores.
- **The network is not real.** "WiFi down" stops the page from answering the
  phone. No radio, router or reconnect logic is involved.
- **A power cut is not a power cut.** Unplugging the transformer removes
  24 VAC from the valves; the controller stays up, as a real one does on USB.
  Use *Power-cycle the controller* in the demo's menu for a restart.
- **Firmware and web-interface uploads are switched off.** There is no flash
  to write.
- **The display's look is approximate.** Colours, viewing angle, backlight
  levels and drawing speed of the real panel are not modelled.

## What has been checked

- CI builds the demo on every pull request and runs a smoke test of the
  WebAssembly module under Node
  (`firmware/zoneflow-fw/sim/web/test/smoke.mjs`): boot, sign in, run a zone
  through the REST API, see the simulated valve open and the display draw,
  see a cut wire raise a fault, restart with the stored settings.
- The page was exercised in Chromium at desktop, tablet and phone widths.
  It has not been tried in Firefox or Safari.

The firmware logic itself is covered by the host unit tests, not by the demo.

## Run it yourself

```sh
firmware/zoneflow-fw/sim/web/build.sh --emsdk    # first time: fetches Emscripten
firmware/zoneflow-fw/sim/web/build.sh --serve    # http://127.0.0.1:8788
```

How it is put together:
[`firmware/zoneflow-fw/sim/web/README.md`](https://github.com/syntroniks/zoneflow/blob/main/firmware/zoneflow-fw/sim/web/README.md).
