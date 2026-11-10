"""Read the headers of ESP32-S3 images.

Layouts: ESP-IDF v5.5.6 ``esp_app_format.h`` (image header, ``esp_app_desc_t``).
The firmware's own OTA check reads the same 288 bytes
(``components/zf_ota/include/zf_ota/ota_logic.hpp``).
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

IMAGE_MAGIC = 0xE9
APP_DESC_MAGIC = 0xABCD5432
CHIP_IDS = {"esp32": 0x0000, "esp32s2": 0x0002, "esp32c3": 0x0005, "esp32s3": 0x0009}
HEADER_LEN = 24  # esp_image_header_t
SEGMENT_HEADER_LEN = 8
APP_DESC_OFFSET = HEADER_LEN + SEGMENT_HEADER_LEN  # first segment of an app starts with esp_app_desc_t
APP_DESC_LEN = 256
DIGEST_LEN = 32


class ImageError(ValueError):
    pass


@dataclass(frozen=True)
class ImageHeader:
    segment_count: int
    spi_mode: int
    flash_size_code: int
    chip_id: int
    append_digest: bool


@dataclass(frozen=True)
class AppDesc:
    version: str
    project_name: str
    idf_ver: str


def _cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def parse_header(data: bytes, what: str = "image") -> ImageHeader:
    if len(data) < HEADER_LEN:
        raise ImageError(f"{what}: shorter than an image header")
    magic, segs, mode, size_freq, _entry, _wp, _drv0, _drv1, _drv2, chip, _minrev, _minfull, _maxfull, digest = (
        struct.unpack("<BBBBIBBBBHBHH4xB", data[:HEADER_LEN])
    )
    if magic != IMAGE_MAGIC:
        raise ImageError(f"{what}: image magic is 0x{magic:02x}, expected 0xe9")
    return ImageHeader(segs, mode, size_freq >> 4, chip, bool(digest))


def parse_app_desc(data: bytes, what: str = "app image") -> AppDesc:
    if len(data) < APP_DESC_OFFSET + APP_DESC_LEN:
        raise ImageError(f"{what}: too short to hold an app description")
    magic, _secure, _r1, _r2, version, project, _time, _date, idf, _sha = struct.unpack(
        "<IIII32s32s16s16s32s32s", data[APP_DESC_OFFSET : APP_DESC_OFFSET + 176]
    )
    if magic != APP_DESC_MAGIC:
        raise ImageError(f"{what}: no app description (magic 0x{magic:08x}); is this an application image?")
    return AppDesc(_cstr(version), _cstr(project), _cstr(idf))


def check_digest(data: bytes, what: str = "image") -> None:
    """The SHA-256 the bootloader checks: appended after the checksum byte."""
    hdr = parse_header(data, what)
    if not hdr.append_digest:
        raise ImageError(f"{what}: image header says no SHA-256 digest is appended")
    if hashlib.sha256(data[:-DIGEST_LEN]).digest() != data[-DIGEST_LEN:]:
        raise ImageError(f"{what}: appended SHA-256 does not match the image")


def check_chip(data: bytes, chip: str, what: str = "image") -> ImageHeader:
    hdr = parse_header(data, what)
    want = CHIP_IDS[chip]
    if hdr.chip_id != want:
        raise ImageError(f"{what}: chip id 0x{hdr.chip_id:04x}, expected 0x{want:04x} ({chip})")
    return hdr


def is_littlefs(data: bytes) -> bool:
    """A LittleFS superblock carries the string ``littlefs`` at offset 8 of its first block."""
    return data[8:16] == b"littlefs"
