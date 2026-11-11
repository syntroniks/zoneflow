# ZoneFlow browser demo

The standalone firmware in a browser tab: the controller with its display
and knob, a garden for it to water, and the web interface it serves. For
people who want to see what ZoneFlow does without a board. The page for
visitors, with the limits of the demo, is
[`docs/try-it.md`](../../../../docs/try-it.md).

No ZoneFlow board has been built. The demo runs the firmware's logic on
simulated hardware; it is not a test of hardware.

## How it works

```text
 index.html, demo.js                          app.html (in a frame)
 +------------------------------------+       +---------------------------+
 | controller    garden     log       |       | the web interface, as     |
 | (canvas,      (SVG)                |       | built for the controller  |
 |  knob, LEDs)                       |       | (../../www)               |
 |      ^   |       ^                 |       |   fetch, EventSource      |
 |      |   v       |                 |       |        |  shim.js         |
 |  zf_frame, zf_turn, zf_world ...   | <-----+--------+                  |
 |  zf_request, zf_stream_*           |       +---------------------------+
 |      zoneflow-demo.wasm            |
 +------------------------------------+
```

`zoneflow-demo.wasm` is `device.cpp` linked with the firmware's own sources:

| From | What |
|---|---|
| `components/zf_core` | Controller, scheduler, zone driver, output-stage sequencing, history |
| `components/zf_display` | Display logic and LVGL view |
| `components/zf_web` | `api.cpp`, `auth.cpp`, `routes.cpp`, `json_map.cpp` and the rest of the REST API (not `web.cpp`, the ESP-IDF HTTP server) |
| `test/host` | `UiHarness` and `FakeBoard`: the controller on a model of the rev A output stage, as the host tests and the desktop simulator use it |
| `sim/` | `Screen` (LVGL on a framebuffer) and `Controls` (encoder and button decoding) |

`device.cpp` is the part written for the demo. It stands in for `web.cpp`
(request to `ApiRequest`, the event-stream task), `app_main.cpp` (start-up
order, history recorder) and the hardware (valve currents, the over-current
comparator, the flow meter). Where it copies the device's behaviour it says
which file it follows. If the device code changes there, this file must
follow by hand; nothing checks that.

The web interface is not rebuilt or patched. `assemble.ts` copies `../../www`
and writes `app.html` from its `index.html` with two changes: asset paths
are relative, and `shim.js` loads first. The shim makes the frame's address
`/` (the interface's router expects to be at the root of the controller's
address) and routes `fetch`, `XMLHttpRequest` and `EventSource` for
`/api/v1` to the module in the page around it. The session cookie lives in
`demo.js`, since the frame has no real cookie for the controller.

Time: `demo.js` calls `zf_step` for every 50 ms of simulated time. The
controller's clock starts at the visitor's time, in the visitor's time zone
(from the table in `web/src/lib/timezones.ts`). The event stream is paced in
real time, as a browser of a real device would see it.

Storage: `zf_storage` returns the two NVS namespaces of the device (core
and `zf_web`) as hex; the page keeps them in `localStorage` and hands them
back at the next start. A restart from the menu, the web interface or the
display goes through the same path.

## Build and run

```sh
sim/web/build.sh --emsdk    # first time: fetches Emscripten 7.0.0 into sim/.cache/
sim/web/build.sh            # builds into sim/web/dist/
sim/web/build.sh --serve    # builds, then serves http://127.0.0.1:8788
```

Needs CMake 3.24 or newer, Node 22.18 or newer, and `uv` or PyYAML (the
board header is generated from `hardware/pinmap.yaml`). The first build
downloads LVGL 9.6.0 (about 110 MB) unless `ZF_LVGL_DIR` points at a copy.
`dist/`, `sim/build-web/` and `sim/.cache/` are not committed.

The documentation site picks `dist/` up when it is built
(`tools/mkdocs_hooks/demo.py`) and publishes it at `demo/`. The docs
workflow builds the demo first.

## Test

`build.sh` runs `test/smoke.mjs` under Node after it links the module: boot,
sign in, run a zone through the REST API, check the simulated valve, the
display and the event stream, cut a wire and expect a fault, restart from
stored settings. It proves that the module works. It does not test the page
or the firmware's logic (the host tests do the latter).

The page has no automated test. It was exercised in Chromium only.

## Files

| File | Contents |
|---|---|
| `device.cpp` | The module's glue and the `zf_*` functions the page calls |
| `build.sh`, `assemble.ts` | Build; assembly of `dist/` |
| `page/index.html`, `demo.css`, `demo.js` | The page: controller, time, conditions, storage, bridge to the frame |
| `page/garden.js` | The garden drawing |
| `page/narrate.js` | Wording of the log, from the firmware's events |
| `page/shim.js` | Runs in the frame before the web interface |
| `test/smoke.mjs` | Smoke test of the module |
