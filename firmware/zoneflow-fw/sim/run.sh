#!/usr/bin/env bash
# Builds and runs the display simulator. Arguments go to the simulator:
#   ./sim/run.sh                        interactive window (needs SDL2)
#   ./sim/run.sh --screenshots <dir>    headless: a PNG of every screen
#   ./sim/run.sh --report               headless: LVGL memory use
#   ./sim/run.sh --help
# Needs CMake >= 3.24 and a C++20 compiler. The first run downloads LVGL
# (about 110 MB) unless ZF_LVGL_DIR points at an LVGL 9.6.0 source tree or
# the firmware has been built (managed_components/lvgl__lvgl).
# ZF_SIM_HEADLESS=1 builds without SDL2.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${here}/build"
args=()
if [[ -n "${ZF_LVGL_DIR:-}" ]]; then
    args+=("-DZF_LVGL_DIR=${ZF_LVGL_DIR}")
elif [[ -f "${here}/../managed_components/lvgl__lvgl/lvgl.h" ]]; then
    args+=("-DZF_LVGL_DIR=${here}/../managed_components/lvgl__lvgl")
fi
if [[ -n "${ZF_SIM_HEADLESS:-}" ]]; then
    args+=("-DZF_SIM_SDL=OFF")
fi
cmake -S "${here}" -B "${build}" "${args[@]}" >/dev/null
cmake --build "${build}" --parallel >/dev/null
exec "${build}/zoneflow-sim" "$@"
