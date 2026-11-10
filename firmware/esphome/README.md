# ZoneFlow ESPHome package

User guide: [docs/firmware/esphome.md](../../docs/firmware/esphome.md). Nothing here has
run on a board.

## Layout

| Path | What |
|---|---|
| `zoneflow.yaml`, `zoneflow-4zone.yaml` | Entry files users point `packages:` at. They add the C++ components from GitHub at `zoneflow_ref`. |
| `factory.yaml` | The image the release publishes and the web installer flashes |
| `zoneflow/package-8.yaml`, `package-4.yaml` | The whole package for each build, without a component source |
| `zoneflow/base.yaml` | Board, network, provisioning, time, the `zoneflow` hub and controller entities |
| `zoneflow/board-rev-a.yaml` | Pins, I2C addresses and expander bits. Generated from `hardware/pinmap.yaml` by `tools/gen_board_yaml.py`; do not edit. |
| `zoneflow/zone.yaml` | One zone's entities, included per zone |
| `zoneflow/sprinkler-8.yaml`, `sprinkler-4.yaml` | The `sprinkler` controller |
| `zoneflow/schedule.yaml`, `program.yaml` | The scheduler and the entities of each program |
| `zoneflow/display.yaml` | Display, encoder, buttons, backlight |
| `components/zoneflow/` | Hub component. `supervisor.*` is framework-free glue around `zf_core`; `board_io.*` is the keep-alive interrupt, ADC sampler and expander bus; `zoneflow.*` wires it to ESPHome. |
| `components/zoneflow_schedule/` | Scheduler. `schedule_logic.*` is framework-free and host-tested. |
| `examples/` | Configs CI compiles, with the local components; `minimal.yaml` is the user file of the guide |
| `test/host/` | GoogleTest host tests, run on the standalone firmware's simulated board |

The `zoneflow` component builds `firmware/zoneflow-fw/components/zf_core` as an ESP-IDF
component by path. It is the same code the standalone firmware runs; do not copy it here.

## Checks

```sh
uv sync                                        # ESPHome, pinned in pyproject.toml
uv run esphome compile examples/zoneflow-8zone.yaml
uv run esphome compile examples/zoneflow-4zone.yaml
test/host/run.sh
uv run --script tools/gen_board_yaml.py --check
uv run ruff check components tools
```

CI runs all of them (`.github/workflows/esphome.yml`).
