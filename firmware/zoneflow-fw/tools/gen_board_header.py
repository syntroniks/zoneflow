#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml>=6,<7"]
# ///
# SPDX-License-Identifier: Apache-2.0
"""Generate the firmware's pin header from hardware/pinmap.yaml.

The pin map is the hardware team's source of truth for every GPIO, I2C
device and I/O expander bit. The firmware does not keep its own copy: every
build (ESP-IDF, host tests, display simulator) runs this script and includes
its output, `zf_board/pinmap_generated.hpp`. The hand-written board header
(`zf_board/zoneflow_rev_a.hpp`) refers to the generated names and adds the
firmware's own constants and compile-time checks.

    gen_board_header.py --pinmap hardware/pinmap.yaml --output build/.../pinmap_generated.hpp

The script checks the map before it writes anything and exits non-zero with
a message on the first problem: unknown keys or values, a GPIO used twice, an
analogue input outside ADC1, a pin the module does not bring out, an I2C
device or expander bit listed twice, a valve function on a GPIO.

The output only changes when the input does, so the build does not recompile
when nothing changed.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import yaml

DIRECTIONS = {"in", "out", "bidir", "analog_in"}
ACTIVE = {"high", "low", "n/a"}
# ESP32-S3: GPIO0..21 and 26..48 exist; 26..32 carry the module's flash and
# 33..37 its octal PSRAM (N16R8). The pin map is expected to list none of
# 22..37.
MODULE_GPIO = set(range(0, 22)) | set(range(38, 49))
ADC1_GPIO = set(range(1, 11))


class PinmapError(Exception):
    pass


def cxx_name(signal: str) -> str:
    """KEEPALIVE -> Keepalive, OC_FAULT_N -> OcFaultN, I2C0_SDA -> I2c0Sda."""
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*", signal):
        raise PinmapError(f"signal name {signal!r} is not UPPER_SNAKE_CASE")
    return "".join(part.capitalize() for part in signal.split("_"))


def cxx_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def require(entry: dict, key: str, where: str):
    if key not in entry:
        raise PinmapError(f"{where}: missing key {key!r}")
    return entry[key]


def load(path: Path) -> dict:
    data = yaml.safe_load(path.read_text())
    if not isinstance(data, dict):
        raise PinmapError("top level is not a mapping")
    if data.get("schema_version") != 1:
        raise PinmapError(f"schema_version {data.get('schema_version')!r}; this generator knows version 1")
    return data


def check_gpio(data: dict) -> list[dict]:
    gpios = require(data, "gpio", "pinmap")
    seen_pins: dict[int, str] = {}
    seen_signals: set[str] = set()
    out = []
    for i, g in enumerate(gpios):
        where = f"gpio[{i}]"
        pin = require(g, "gpio", where)
        signal = require(g, "signal", where)
        where = f"GPIO{pin} ({signal})"
        direction = require(g, "direction", where)
        active = require(g, "active", where)
        if not isinstance(pin, int) or pin not in MODULE_GPIO:
            raise PinmapError(f"{where}: not a GPIO the N16R8 module brings out")
        if pin in seen_pins:
            raise PinmapError(f"{where}: GPIO already used by {seen_pins[pin]}")
        if signal in seen_signals:
            raise PinmapError(f"{where}: signal listed twice")
        if direction not in DIRECTIONS:
            raise PinmapError(f"{where}: direction {direction!r} not one of {sorted(DIRECTIONS)}")
        if active not in ACTIVE:
            raise PinmapError(f"{where}: active {active!r} not one of {sorted(ACTIVE)}")
        channel = None
        if "adc_channel" in g:
            m = re.fullmatch(r"ADC1_CH(\d)", str(g["adc_channel"]))
            if not m:
                raise PinmapError(f"{where}: adc_channel {g['adc_channel']!r} is not ADC1 (ADC2 is shared with WiFi)")
            channel = int(m.group(1))
            if channel != pin - 1:
                raise PinmapError(f"{where}: ADC1 channel {channel} is GPIO{channel + 1}, not GPIO{pin}")
        if direction == "analog_in" and (pin not in ADC1_GPIO or channel is None):
            raise PinmapError(f"{where}: analogue input must be on ADC1 (GPIO1..10) with adc_channel")
        if re.search(r"ZONE|MASTER|VALVE|RLY_ON|COM_RELAY", signal):
            raise PinmapError(f"{where}: valve or relay function on a GPIO; rev A drives them from the expander")
        seen_pins[pin] = signal
        seen_signals.add(signal)
        out.append({"gpio": pin, "signal": signal, "direction": direction, "active": active, "adc_channel": channel})
    return out


def check_i2c(data: dict) -> list[dict]:
    buses = require(data, "i2c", "pinmap")
    names: set[str] = set()
    out = []
    for i, b in enumerate(buses):
        where = f"i2c[{i}]"
        bus = require(b, "bus", where)
        m = re.fullmatch(r"I2C(\d)", str(bus))
        if not m:
            raise PinmapError(f"{where}: bus {bus!r} is not I2C0 or I2C1")
        devices = []
        addresses: set[int] = set()
        for j, d in enumerate(b.get("devices") or []):
            dw = f"{bus} device[{j}]"
            name = require(d, "name", dw)
            addr = require(d, "address", dw)
            if not isinstance(addr, int) or not 0x08 <= addr <= 0x77:
                raise PinmapError(f"{dw}: address {addr!r} is not a 7-bit device address")
            if addr in addresses:
                raise PinmapError(f"{dw}: address 0x{addr:02x} used twice on {bus}")
            if name in names:
                raise PinmapError(f"{dw}: device name {name!r} used twice")
            names.add(name)
            addresses.add(addr)
            devices.append({"name": name, "part": str(d.get("part", "")), "address": addr})
        out.append(
            {
                "bus": bus,
                "port": int(m.group(1)),
                "sda": require(b, "sda_gpio", where),
                "scl": require(b, "scl_gpio", where),
                "speed_hz": require(b, "speed_hz", where),
                "devices": devices,
            }
        )
    return out


def check_expander(data: dict, i2c: list[dict]) -> dict:
    ex = require(data, "io_expander", "pinmap")
    device = require(ex, "device", "io_expander")
    found = [d for b in i2c for d in b["devices"] if d["name"] == device]
    if len(found) != 1:
        raise PinmapError(f"io_expander: device {device!r} is not on any I2C bus")
    seen: dict[tuple[int, int], str] = {}
    signals: set[str] = set()
    bits = []
    for i, b in enumerate(require(ex, "bits", "io_expander")):
        where = f"io_expander bit[{i}]"
        port = require(b, "port", where)
        bit = require(b, "bit", where)
        signal = require(b, "signal", where)
        direction = require(b, "direction", where)
        active = require(b, "active", where)
        if port not in (0, 1) or bit not in range(8):
            raise PinmapError(f"{where}: P{port}{bit} does not exist on a 16-bit expander")
        if (port, bit) in seen:
            raise PinmapError(f"{where}: P{port}{bit} already used by {seen[(port, bit)]}")
        if signal in signals:
            raise PinmapError(f"{where}: signal {signal} listed twice")
        if direction not in ("in", "out") or active not in ACTIVE:
            raise PinmapError(f"{where}: direction {direction!r} or active {active!r} not allowed")
        seen[(port, bit)] = signal
        signals.add(signal)
        bits.append({"port": port, "bit": bit, "signal": signal, "direction": direction, "active": active})
    if len(bits) != 16:
        raise PinmapError(f"io_expander: {len(bits)} bits listed, a TCA9539 has 16")
    return {"device": found[0], "bits": bits}


def generate(data: dict, source: str) -> str:
    gpio = check_gpio(data)
    i2c = check_i2c(data)
    expander = check_expander(data, i2c)
    limits = data.get("limits", {})
    adc = data.get("adc", {})
    by_signal = {g["signal"]: g for g in gpio}

    lines: list[str] = []
    w = lines.append
    w("// SPDX-License-Identifier: Apache-2.0")
    w("//")
    w(f"// GENERATED from {source} by firmware/zoneflow-fw/tools/gen_board_header.py.")
    w("// Do not edit and do not commit: every build regenerates it. Change the pin")
    w("// map, or the hand-written board header zf_board/zoneflow_rev_a.hpp.")
    w("#pragma once")
    w("")
    w("#include <array>")
    w("#include <cstdint>")
    w("")
    w("namespace zf::board::pinmap {")
    w("")
    w(f"inline constexpr const char *kBoard = {cxx_string(str(data.get('board', '')))};")
    w(f"inline constexpr const char *kRevision = {cxx_string(str(data.get('revision', '')))};")
    w(f"inline constexpr const char *kModule = {cxx_string(str(data.get('module', '')))};")
    w("")
    w("enum class Direction : uint8_t { In, Out, Bidir, AnalogIn };")
    w("enum class Active : uint8_t { High, Low, None };")
    w("")
    w("struct GpioEntry {")
    w("    int gpio;")
    w("    const char *signal;")
    w("    Direction direction;")
    w("    Active active;")
    w("    int adcChannel; // ADC1 channel, -1 if none")
    w("};")
    w("")
    w("// ---- GPIO, one constant per signal ----")
    dir_map = {"in": "In", "out": "Out", "bidir": "Bidir", "analog_in": "AnalogIn"}
    act_map = {"high": "High", "low": "Low", "n/a": "None"}
    for g in gpio:
        name = cxx_name(g["signal"])
        w(f"inline constexpr int k{name}Gpio = {g['gpio']};")
        if g["active"] != "n/a":
            w(f"inline constexpr bool k{name}ActiveHigh = {'true' if g['active'] == 'high' else 'false'};")
        if g["adc_channel"] is not None:
            w(f"inline constexpr int k{name}AdcChannel = {g['adc_channel']};")
    w("")
    w(f"inline constexpr std::array<GpioEntry, {len(gpio)}> kGpio = {{{{")
    for g in gpio:
        w(
            f"    {{{g['gpio']}, {cxx_string(g['signal'])}, Direction::{dir_map[g['direction']]}, "
            f"Active::{act_map[g['active']]}, {g['adc_channel'] if g['adc_channel'] is not None else -1}}},"
        )
    w("}};")
    w("")
    if adc:
        w("// ---- ADC ----")
        w(f"inline constexpr int kAdcAttenuationDb = {int(adc.get('attenuation_db', 0))};")
        w("")
    w("// ---- I2C ----")
    for b in i2c:
        p = cxx_name(b["bus"])
        if by_signal.get(f"{b['bus']}_SDA", {}).get("gpio", b["sda"]) != b["sda"]:
            raise PinmapError(f"{b['bus']}: sda_gpio {b['sda']} differs from the GPIO table")
        if by_signal.get(f"{b['bus']}_SCL", {}).get("gpio", b["scl"]) != b["scl"]:
            raise PinmapError(f"{b['bus']}: scl_gpio {b['scl']} differs from the GPIO table")
        w(f"inline constexpr int k{p}BusPort = {b['port']};")
        w(f"inline constexpr int k{p}BusSdaGpio = {b['sda']};")
        w(f"inline constexpr int k{p}BusSclGpio = {b['scl']};")
        w(f"inline constexpr uint32_t k{p}BusSpeedHz = {b['speed_hz']};")
        for d in b["devices"]:
            w(f"inline constexpr uint8_t k{cxx_name(d['name'].upper())}Address = 0x{d['address']:02x}; // {d['part']} on {b['bus']}")
    w("")
    w("// ---- I/O expander bits ----")
    w("struct ExpanderBit {")
    w("    uint8_t port;")
    w("    uint8_t bit;")
    w("    bool output;    // the firmware may drive it")
    w("    bool activeLow; // asserted when the pin is low")
    w("};")
    for b in expander["bits"]:
        w(
            f"inline constexpr ExpanderBit k{cxx_name(b['signal'])}Bit = {{{b['port']}, {b['bit']}, "
            f"{'true' if b['direction'] == 'out' else 'false'}, {'true' if b['active'] == 'low' else 'false'}}};"
        )
    w("")
    if limits:
        w("// ---- limits ----")
        if "max_outputs_on" in limits:
            w(f"inline constexpr int kMaxOutputsOn = {int(limits['max_outputs_on'])};")
        if "max_current_per_output_a" in limits:
            w(f"inline constexpr int kMaxCurrentPerOutputMa = {round(float(limits['max_current_per_output_a']) * 1000)};")
        if "hardware_overcurrent_trip_a_peak" in limits:
            w(
                "inline constexpr int kHardwareOvercurrentTripMaPeak = "
                f"{round(float(limits['hardware_overcurrent_trip_a_peak']) * 1000)};"
            )
        w("")
    w("} // namespace zf::board::pinmap")
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pinmap", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args(argv)
    try:
        text = generate(load(args.pinmap), "hardware/pinmap.yaml")
    except (PinmapError, yaml.YAMLError) as e:
        print(f"{args.pinmap}: {e}", file=sys.stderr)
        return 1
    if args.output.exists() and args.output.read_text() == text:
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
