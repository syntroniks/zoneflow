#!/usr/bin/env bash
# Builds and runs the host unit tests of the ESPHome package's C++ logic under
# ASan and UBSan, like firmware/zoneflow-fw/test/host/run.sh. Needs CMake >= 3.24,
# a C++20 compiler, uv or PyYAML (board header) and network access on the first
# run (GoogleTest, cJSON).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${here}/build"
cmake -S "${here}" -B "${build}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="${CXXFLAGS:-} -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "${build}" --parallel
ctest --test-dir "${build}" --output-on-failure "$@"
