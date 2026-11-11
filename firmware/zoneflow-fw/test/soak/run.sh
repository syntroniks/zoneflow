#!/usr/bin/env bash
# Builds the soak simulation (optimised, with ASan and UBSan) and runs it.
# Arguments go to zf_soak; see README.md:
#   test/soak/run.sh --ci                  the deterministic subset CI runs
#   test/soak/run.sh --long                the long run (hours on 16 cores)
#   test/soak/run.sh --mode chaos --seeds 1-50 --days 30
#   test/soak/run.sh --mode chaos --seed 17 --days 30 --shrink
# ZF_SOAK_SANITIZE=0 builds without the sanitizers (about three times faster).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${here}/build"
flags=""
if [[ "${ZF_SOAK_SANITIZE:-1}" == "1" ]]; then
    flags="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer"
    build="${here}/build-asan"
fi
cmake -S "${here}" -B "${build}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS="${CXXFLAGS:-} ${flags}" -DCMAKE_C_FLAGS="${CFLAGS:-} ${flags}" \
    -DCMAKE_EXE_LINKER_FLAGS="${flags}" >/dev/null
cmake --build "${build}" --parallel >/dev/null
exec "${build}/zf_soak" "$@"
