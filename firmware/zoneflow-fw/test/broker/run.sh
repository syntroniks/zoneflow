#!/usr/bin/env bash
# Broker integration test of the zf_mqtt logic (not part of CI).
#  1. Downloads and builds Mosquitto 2.1.2 (broker and libmosquitto) into
#     ./build/ unless MOSQUITTO_BUILD points at an existing build.
#  2. Builds ./harness: zf_mqtt logic + zf_core Controller on the host.
#  3. Runs check.py (paho-mqtt through uv), which starts the broker and the
#     harness and checks what arrives on the broker.
# Needs CMake >= 3.24, a C/C++ compiler, curl and uv.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${here}/build"
mkdir -p "${build}"
version=2.1.2
# SHA-256 of mosquitto-2.1.2.tar.gz as downloaded from mosquitto.org on
# 2026-11-10 (the site publishes GPG signatures, not hashes).
sha=fd905380691ac65ea5a93779e8214941829e3d6e038d5edff9eac5fd74cbed02
src="${build}/mosquitto-${version}"
mbuild="${MOSQUITTO_BUILD:-${build}/mosquitto-build}"
if [ ! -x "${mbuild}/src/mosquitto" ]; then
    if [ ! -d "${src}" ]; then
        curl -sfL -o "${build}/m.tgz" "https://mosquitto.org/files/source/mosquitto-${version}.tar.gz"
        echo "${sha}  ${build}/m.tgz" | sha256sum -c -
        tar -xzf "${build}/m.tgz" -C "${build}"
    fi
    cmake -S "${src}" -B "${mbuild}" -DCMAKE_BUILD_TYPE=Release -DWITH_TLS=OFF -DWITH_CJSON=OFF \
        -DDOCUMENTATION=OFF -DWITH_WEBSOCKETS=OFF -DWITH_APPS=OFF -DWITH_PLUGINS=OFF -DWITH_TESTS=OFF >/dev/null
    cmake --build "${mbuild}" --parallel --target mosquitto libmosquitto >/dev/null
fi
cmake -S "${here}" -B "${build}/harness" -DCMAKE_BUILD_TYPE=Debug \
    -DMOSQUITTO_SRC="${src}" -DMOSQUITTO_LIB="${mbuild}/lib/libmosquitto.so" >/dev/null
cmake --build "${build}/harness" --parallel >/dev/null
LD_LIBRARY_PATH="${mbuild}/lib" uv run --no-project --with 'paho-mqtt==2.1.0' python "${here}/check.py" \
    "${mbuild}/src/mosquitto" "${build}/harness/harness"
