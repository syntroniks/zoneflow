"""Synthetic ESP32-S3 build output: small enough to be quick, real enough for esptool merge-bin."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

import pytest

from zfrelease import partitions

# Small layout on 4 MB flash. Names and types follow firmware/zoneflow-fw/partitions.csv.
CSV = """\
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x6000
otadata,  data, ota,      0xf000,   0x2000
ota_0,    app,  ota_0,    0x20000,  0x20000
ota_1,    app,  ota_1,    0x40000,  0x20000
www,      data, littlefs, 0x60000,  0x20000
coredump, data, coredump, 0x80000,  0x20000
"""
VERSION = "1.2.3"
CHIP_S3 = 9


def image(chip: int = CHIP_S3, payload: int = 600, desc: tuple[str, str] | None = None) -> bytes:
    """An image header, one segment, optional esp_app_desc_t, checksum padding and the SHA-256."""
    header = struct.pack("<BBBBIBBBBHBHH4xB", 0xE9, 1, 2, 0x2F, 0x40000400, 0, 0, 0, 0, chip, 0, 0, 0xFFFF, 1)
    body = b""
    if desc:
        version, project = desc
        body = struct.pack(
            "<IIII32s32s16s16s32s32s",
            0xABCD5432,
            0,
            0,
            0,
            version.encode(),
            project.encode(),
            b"12:00:00",
            b"Nov 10 2026",
            b"v5.5.6",
            b"\x11" * 32,
        ).ljust(256, b"\0")
    data = body + bytes(range(256)) * (payload // 256 + 1)
    data = data[: max(payload, len(body))]
    seg = struct.pack("<II", 0x3C000020, len(data))
    raw = header + seg + data
    raw += b"\0" * (-(len(raw) + 1) % 16) + b"\xaa"
    return raw + hashlib.sha256(raw).digest()


def partition_table(csv_text: str = CSV) -> bytes:
    out = b""
    for p in partitions.parse_csv(csv_text):
        out += struct.pack("<HBBII16sI", 0x50AA, p.type, p.subtype, p.offset, p.size, p.name.encode(), 0)
    out += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(out).digest()
    return out.ljust(0xC00, b"\xff")


def make_build(tmp: Path, *, version: str = VERSION, project: str = "zoneflow-fw", app_offset: str = "0x20000") -> Path:
    b = tmp / "build"
    (b / "bootloader").mkdir(parents=True)
    (b / "partition_table").mkdir()
    (b / "bootloader" / "bootloader.bin").write_bytes(image(payload=300))
    (b / "partition_table" / "partition-table.bin").write_bytes(partition_table())
    (b / "ota_data_initial.bin").write_bytes(b"\xff" * 0x2000)
    (b / "zoneflow-fw.bin").write_bytes(image(desc=(version, project)))
    www = bytearray(b"\xff" * 0x20000)
    www[:16] = b"\x08\x00\x00\x00\xf0\x0f\xff\xf7littlefs"
    (b / "www.bin").write_bytes(www)
    args = {
        "flash_settings": {"flash_mode": "dio", "flash_size": "4MB", "flash_freq": "80m"},
        "flash_files": {
            "0x0": "bootloader/bootloader.bin",
            app_offset: "zoneflow-fw.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0xf000": "ota_data_initial.bin",
            "0x60000": "www.bin",
        },
        "bootloader": {"offset": "0x0", "file": "bootloader/bootloader.bin"},
        "app": {"offset": app_offset, "file": "zoneflow-fw.bin"},
        "partition-table": {"offset": "0x8000", "file": "partition_table/partition-table.bin"},
        "otadata": {"offset": "0xf000", "file": "ota_data_initial.bin"},
        "www": {"offset": "0x60000", "file": "www.bin"},
        "extra_esptool_args": {"chip": "esp32s3"},
    }
    (b / "flasher_args.json").write_text(json.dumps(args))
    return b


@pytest.fixture
def csv_path(tmp_path: Path) -> Path:
    p = tmp_path / "partitions.csv"
    p.write_text(CSV)
    return p


@pytest.fixture
def build_dir(tmp_path: Path) -> Path:
    return make_build(tmp_path)
