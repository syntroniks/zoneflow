#!/usr/bin/env bash
# Builds the browser demo into sim/web/dist/ (not committed):
#   1. the firmware as a WebAssembly module (Emscripten),
#   2. the web interface (the same `npm run build` the firmware uses),
#   3. the page around them.
#
#   sim/web/build.sh               build
#   sim/web/build.sh --serve       build, then serve dist/ on http://127.0.0.1:8788
#   sim/web/build.sh --emsdk       fetch the pinned Emscripten into sim/.cache/ first
#
# Needs CMake >= 3.24, Node >= 22.18, uv or PyYAML (board header) and Emscripten (emcc on PATH, or
# the copy --emsdk fetches). The version below is the one the demo is built
# and smoke-tested with; CI uses the same.
set -euo pipefail
EMSDK_VERSION=7.0.0

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sim="$(dirname "${here}")"
fw="$(dirname "${sim}")"
cache="${sim}/.cache"
serve=0
for arg in "$@"; do
    case "${arg}" in
    --emsdk)
        mkdir -p "${cache}"
        [[ -d "${cache}/emsdk" ]] || git clone --depth 1 https://github.com/emscripten-core/emsdk.git "${cache}/emsdk"
        "${cache}/emsdk/emsdk" install "${EMSDK_VERSION}"
        "${cache}/emsdk/emsdk" activate "${EMSDK_VERSION}"
        ;;
    --serve) serve=1 ;;
    *)
        echo "unknown argument: ${arg}" >&2
        exit 2
        ;;
    esac
done

if ! command -v emcmake >/dev/null; then
    if [[ -f "${cache}/emsdk/emsdk_env.sh" ]]; then
        # shellcheck disable=SC1091
        EMSDK_QUIET=1 source "${cache}/emsdk/emsdk_env.sh"
    else
        echo "Emscripten not found. Install it, or run: sim/web/build.sh --emsdk" >&2
        exit 1
    fi
fi

version="$(git -C "${fw}" describe --tags --always --dirty 2>/dev/null || echo unknown)"
args=("-DCMAKE_BUILD_TYPE=Release" "-DZF_DEMO_VERSION=${version}")
if [[ -n "${ZF_LVGL_DIR:-}" ]]; then
    args+=("-DZF_LVGL_DIR=${ZF_LVGL_DIR}")
elif [[ -f "${fw}/managed_components/lvgl__lvgl/lvgl.h" ]]; then
    args+=("-DZF_LVGL_DIR=${fw}/managed_components/lvgl__lvgl")
fi
emcmake cmake -S "${sim}" -B "${sim}/build-web" "${args[@]}" >/dev/null
cmake --build "${sim}/build-web" --parallel
node "${here}/test/smoke.mjs" "${sim}/build-web/zoneflow-demo.js"

if [[ ! -d "${fw}/web/node_modules" ]]; then
    (cd "${fw}/web" && npm ci --no-audit --no-fund)
fi
(cd "${fw}/web" && npm run build)

node "${here}/assemble.ts"
echo "demo written to ${here}/dist"
if [[ "${serve}" == 1 ]]; then
    exec python3 -m http.server 8788 --bind 127.0.0.1 --directory "${here}/dist"
fi
