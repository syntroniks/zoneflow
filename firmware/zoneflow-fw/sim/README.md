# ZoneFlow display simulator

The display of the standalone firmware, running on a desktop. It is built
from the same sources as the device:

| Part | Device | Simulator |
|---|---|---|
| Controller core (`zf_core`) | on the control task | the same code, stepped 50 ms at a time |
| Display logic (`zf_display/src/ui`) | UI task | the same code |
| Drawing (`zf_display/src/view`, LVGL 9.6.0) | UI task, 320x240 RGB565, 24-line stripes | the same code, same colour format and stripe size |
| Panel, backlight, encoder, buttons | `zf_display/src/port` | a framebuffer in memory; SDL2 window and keyboard |
| Valves, sensors, clock, storage | `zf_platform` | the fakes of the host tests (`test/host/fakes.hpp`) |

So a press of Stop in the simulator goes through `CommandQueue` into the real
`Controller`, and the screen changes because the controller published a new
snapshot, as on the device. What the simulator cannot show: panel colours and
viewing angle, real backlight levels, drawing speed, and the feel of the
encoder. No hardware exists yet to compare against.

## Run it

```sh
firmware/zoneflow-fw/sim/run.sh                        # window
firmware/zoneflow-fw/sim/run.sh --wizard               # window, first-boot wizard
firmware/zoneflow-fw/sim/run.sh --screenshots out/     # headless, one PNG per scene
firmware/zoneflow-fw/sim/run.sh --report               # headless, LVGL memory use
```

Needs CMake 3.24 or newer and a C++20 compiler. The window needs SDL2
(`libsdl2-dev` on Debian and Ubuntu, `sdl2` on Arch); without it, or with
`ZF_SIM_HEADLESS=1`, the build is headless only.

The first run downloads LVGL 9.6.0 (about 110 MB) and cJSON. To skip the LVGL
download, build the firmware once (the script then uses
`managed_components/lvgl__lvgl`) or set `ZF_LVGL_DIR` to an LVGL 9.6.0 source
tree. A tree of another version is refused.

## Keys in the window

| Key | Control |
|---|---|
| Left / Right, Up / Down, mouse wheel | turn the encoder (hold a key to spin; fast spinning accelerates in value pickers) |
| Enter, Space | press the encoder (held 3 s on Setup, Network, Show setup code: shows the code) |
| Backspace, Esc | Back button (hold to repeat) |
| H, M | Manual/Home button (hold 0.8 s: stop all valves) |
| F | cut or restore the wire of zone 3 (run it to see the fault) |
| R | rain sensor wet or dry |
| A | 24 V AC supply present or missing |
| W | WiFi connected or not |
| Q | quit |

Keys pass through the same debounce, long-press, detent and acceleration
code as the device's controls (`zf_ui/input.hpp`). The window dims and goes
dark with the simulated backlight.

## Screenshots

`--screenshots <dir>` walks every screen in representative states
(`scenes.cpp`): idle, watering, queued, paused, rain delay, faults, each
picker, the program editor, setup and the wizard. Each scene is reached with
the inputs a user would give, so every picture is a state the device can be
in. The pictures under `docs/design/display-ui/` are this output at
`--scale 2` (the default).

CI runs the walk on every pull request and fails if it crashes or a file
cannot be written. It does not compare pixels. To refresh the committed
pictures after a change to the interface:

```sh
firmware/zoneflow-fw/sim/run.sh --screenshots docs/design/display-ui
```

and look at them before committing.

## Files

| File | Contents |
|---|---|
| `CMakeLists.txt`, `run.sh` | build; `lv_conf.h` is LVGL's configuration here and must agree with the `CONFIG_LV_*` lines in `sdkconfig.defaults` |
| `sim.cpp`, `sim.hpp` | LVGL display on a framebuffer, PNG writer, the demo garden |
| `scenes.cpp` | the screenshot walk |
| `window_sdl.cpp` | SDL2 window and keyboard |
| `main.cpp` | command line |
| `controls.hpp` | encoder and buttons, through the device's input decoding; shared with the browser demo |
| `web/` | the browser demo: this simulator, the REST API and the web interface as one page. See [`web/README.md`](web/README.md) |
