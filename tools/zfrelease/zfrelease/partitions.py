"""ESP-IDF partition tables: the CSV source and the binary the chip reads.

Both are parsed here so the release checks do not depend on ESP-IDF being
installed. Binary layout: ESP-IDF v5.5.6 ``components/partition_table/gen_esp32part.py``
(32-byte entries, magic ``0xAA50``, MD5 entry magic ``0xEBEB``).
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

ENTRY_SIZE = 32
ENTRY_MAGIC = 0x50AA  # bytes AA 50 read as little-endian
MD5_MAGIC = b"\xeb\xeb"
TABLE_SIZE = 0xC00  # 3072 bytes: room for 95 entries and the MD5 entry

TYPES = {"app": 0x00, "data": 0x01, "bootloader": 0x02, "partition_table": 0x03}
SUBTYPES = {
    0x00: {"factory": 0x00, "test": 0x20, **{f"ota_{i}": 0x10 + i for i in range(16)}},
    0x01: {
        "ota": 0x00,
        "phy": 0x01,
        "nvs": 0x02,
        "coredump": 0x03,
        "nvs_keys": 0x04,
        "efuse": 0x05,
        "undefined": 0x06,
        "esphttpd": 0x80,
        "fat": 0x81,
        "spiffs": 0x82,
        "littlefs": 0x83,
    },
}


class PartitionError(ValueError):
    pass


@dataclass(frozen=True)
class Partition:
    name: str
    type: int
    subtype: int
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size

    def label(self) -> str:
        return (
            f"{self.name} (type 0x{self.type:02x}, subtype 0x{self.subtype:02x}) at 0x{self.offset:x}+0x{self.size:x}"
        )


def _number(text: str, what: str) -> int:
    t = text.strip().lower()
    if not t:
        raise PartitionError(f"{what} is empty; give every offset and size explicitly")
    mult = 1
    if t.endswith("k"):
        mult, t = 1024, t[:-1]
    elif t.endswith("m"):
        mult, t = 1024 * 1024, t[:-1]
    try:
        return int(t, 0) * mult
    except ValueError as e:
        raise PartitionError(f"{what} is not a number: {text!r}") from e


def parse_csv(text: str) -> list[Partition]:
    """Parse a partition CSV. Every offset must be given: the release depends on them."""
    out: list[Partition] = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            raise PartitionError(f"line {lineno}: need name, type, subtype, offset, size")
        name, typ, sub, off, size = cols[:5]
        try:
            t = TYPES[typ] if typ in TYPES else int(typ, 0)
            if sub in SUBTYPES.get(t, {}):
                st = SUBTYPES[t][sub]
            else:
                st = int(sub, 0)
        except ValueError as e:
            raise PartitionError(f"line {lineno}: unknown type or subtype {typ!r}/{sub!r}") from e
        out.append(Partition(name, t, st, _number(off, f"line {lineno} offset"), _number(size, f"line {lineno} size")))
    check_layout(out)
    return out


def check_layout(parts: list[Partition]) -> None:
    names = [p.name for p in parts]
    if len(set(names)) != len(names):
        raise PartitionError("duplicate partition name")
    ordered = sorted(parts, key=lambda p: p.offset)
    for a, b in zip(ordered, ordered[1:]):
        if a.end > b.offset:
            raise PartitionError(f"partitions overlap: {a.label()} and {b.label()}")


def parse_binary(data: bytes) -> list[Partition]:
    """Parse the partition table as the chip reads it, and check its MD5 entry."""
    out: list[Partition] = []
    md5_ok: bool | None = None
    for pos in range(0, len(data) - ENTRY_SIZE + 1, ENTRY_SIZE):
        entry = data[pos : pos + ENTRY_SIZE]
        if entry == b"\xff" * ENTRY_SIZE:
            break
        if entry[:2] == MD5_MAGIC:
            md5_ok = hashlib.md5(data[:pos]).digest() == entry[16:32]
            continue
        magic, typ, sub, off, size, raw_name, _flags = struct.unpack("<HBBII16sI", entry)
        if magic != ENTRY_MAGIC:
            raise PartitionError(f"bad partition entry magic 0x{magic:04x} at table offset 0x{pos:x}")
        name = raw_name.split(b"\0", 1)[0].decode("ascii", "replace")
        out.append(Partition(name, typ, sub, off, size))
    if md5_ok is False:
        raise PartitionError("partition table MD5 entry does not match its content")
    if md5_ok is None:
        raise PartitionError("partition table has no MD5 entry")
    return out


def by_name(parts: list[Partition], name: str) -> Partition:
    for p in parts:
        if p.name == name:
            return p
    raise PartitionError(f"no partition named {name!r}")


def to_json(p: Partition) -> dict:
    return {"name": p.name, "type": p.type, "subtype": p.subtype, "offset": p.offset, "size": p.size}
