#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyyaml>=6,<7"]
# ///
"""Generate the ESPHome board substitutions from hardware/pinmap.yaml.

The ESPHome package reads every GPIO number, I2C address and expander bit from
firmware/esphome/zoneflow/board-rev-a.yaml, which this script writes. It checks
the pin map with the same functions as the standalone firmware's board header
generator (firmware/zoneflow-fw/tools/gen_board_header.py), so both firmwares
reject the same mistakes.

    uv run --script firmware/esphome/tools/gen_board_yaml.py           # write
    uv run --script firmware/esphome/tools/gen_board_yaml.py --check   # CI: fail if stale
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PINMAP = REPO / "hardware" / "pinmap.yaml"
OUTPUT = REPO / "firmware" / "esphome" / "zoneflow" / "board-rev-a.yaml"
HEADER_GEN = REPO / "firmware" / "zoneflow-fw" / "tools" / "gen_board_header.py"

# Signal in the pin map -> substitution name. Every GPIO the package uses.
GPIO_SUBS = {
    "ISENSE_LO": "zf_pin_isense_lo",
    "ISENSE_HI": "zf_pin_isense_hi",
    "SENS_RAIN": "zf_pin_rain",
    "SENS_FLOW": "zf_pin_flow",
    "SENS_AUX": "zf_pin_aux",
    "RLY_B_RB": "zf_pin_relay_readback",
    "I2C0_SDA": "zf_pin_i2c_sda",
    "I2C0_SCL": "zf_pin_i2c_scl",
    "TFT_CS": "zf_pin_tft_cs",
    "TFT_MOSI": "zf_pin_tft_mosi",
    "TFT_SCLK": "zf_pin_tft_sclk",
    "TFT_DC": "zf_pin_tft_dc",
    "TFT_RST": "zf_pin_tft_reset",
    "ENC_A": "zf_pin_encoder_a",
    "ENC_B": "zf_pin_encoder_b",
    "ENC_SW": "zf_pin_encoder_switch",
    "BTN_BACK": "zf_pin_button_back",
    "TFT_BL": "zf_pin_backlight",
    "KEEPALIVE": "zf_pin_keepalive",
    "BTN_HOME": "zf_pin_button_home",
    "OC_FAULT_N": "zf_pin_overcurrent",
    "AC_ZC": "zf_pin_zero_cross",
    "DRV_EN_RB": "zf_pin_drive_readback",
    "SENS_PWR_EN": "zf_pin_sensor_supply",
    "STATUS_LED": "zf_pin_status_led",
    "QWIIC_SDA": "zf_pin_qwiic_sda",
    "QWIIC_SCL": "zf_pin_qwiic_scl",
}
BIT_SUBS = {
    **{f"ZONE{i}": f"zf_bit_zone{i}" for i in range(1, 9)},
    "MASTER": "zf_bit_master",
    "COM_RELAY": "zf_bit_relay",
    "VARIANT_8ZONE_N": "zf_bit_variant_8zone_n",
    "SENS_PWR_FAULT_N": "zf_bit_sensor_fault_n",
}


def _header_generator():
    spec = importlib.util.spec_from_file_location("gen_board_header", HEADER_GEN)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def generate(pinmap: Path) -> str:
    gen = _header_generator()
    data = gen.load(pinmap)
    gpio = gen.check_gpio(data)
    i2c = gen.check_i2c(data)
    gen.check_expander(data, i2c)
    by_signal = {g["signal"]: g for g in gpio}
    bits = {b["signal"]: b for b in data["io_expander"]["bits"]}
    devices = {d["name"]: (bus, d) for bus in data["i2c"] for d in bus.get("devices", [])}

    missing = [s for s in GPIO_SUBS if s not in by_signal] + [s for s in BIT_SUBS if s not in bits]
    if missing:
        raise gen.PinmapError(f"signals the ESPHome package needs are not in the pin map: {', '.join(missing)}")

    bus0, expander = devices["io_expander"]
    _, rtc = devices["rtc"]
    lines = [
        "# GENERATED from hardware/pinmap.yaml by firmware/esphome/tools/gen_board_yaml.py.",
        "# Do not edit. Change the pin map and run the script; CI fails when this file is stale.",
        f"# Board: {data['board']} revision {data['revision']}, {data['module']}.",
        "substitutions:",
    ]
    for signal, sub in GPIO_SUBS.items():
        lines.append(f'  {sub}: "GPIO{by_signal[signal]["gpio"]}"  # {signal}')
    for signal, sub in BIT_SUBS.items():
        b = bits[signal]
        lines.append(f'  {sub}: "{b["port"]}.{b["bit"]}"  # {signal}, expander port.bit')
    lines += [
        f'  zf_i2c_frequency: "{bus0["speed_hz"]}"',
        f'  zf_expander_address: "0x{expander["address"]:02X}"  # {expander["part"]}',
        f'  zf_rtc_address: "0x{rtc["address"]:02X}"  # {rtc["part"]}',
        f'  zf_max_outputs_on: "{data["limits"]["max_outputs_on"]}"',
        f'  zf_adc_attenuation: "{data["adc"]["attenuation_db"]}dB"',
    ]
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pinmap", type=Path, default=PINMAP)
    ap.add_argument("--output", type=Path, default=OUTPUT)
    ap.add_argument("--check", action="store_true", help="fail if the output differs instead of writing it")
    args = ap.parse_args(argv)
    text = generate(args.pinmap)
    current = args.output.read_text() if args.output.exists() else None
    if args.check:
        if current != text:
            print(f"{args.output} is stale; run {Path(__file__).name} and commit the result", file=sys.stderr)
            return 1
        return 0
    if current != text:
        args.output.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
