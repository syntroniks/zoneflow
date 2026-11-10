"""Dry-run verification of a packaged release.

Everything here reads the *output* files, not the build directory, so it checks what
would be published. Each check that passes is reported; the first failure raises.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path
from typing import Any

from . import flavours, images, manifest, partitions
from .package import BOOTLOADER_OFFSET, FLASH_SIZES, sha256_file
from .release import RELEASE_JSON, SUMS, check_sums


class VerifyError(RuntimeError):
    pass


Report = Callable[[str], None]

# esp_image_header_t flash size nibble (esptool: DETECTED_FLASH_SIZES).
FLASH_SIZE_CODE = {"1MB": 0, "2MB": 1, "4MB": 2, "8MB": 3, "16MB": 4, "32MB": 5}


def _need(cond: bool, message: str) -> None:
    if not cond:
        raise VerifyError(message)


def verify_directory(directory: Path, partitions_csv: Path, report: Report = print) -> None:
    """Verify an assembled release directory (release.json, SHA256SUMS, images, manifests)."""
    _need((directory / RELEASE_JSON).is_file(), f"{RELEASE_JSON} not found in {directory}")
    rel = json.loads((directory / RELEASE_JSON).read_text())
    check_sums(directory)
    report(f"ok  {SUMS} matches every file in the directory ({len(list(directory.iterdir())) - 1} files)")
    table_csv = partitions.parse_csv(partitions_csv.read_text())
    known = flavours.load()
    for fl in rel["flavours"]:
        _need(fl["id"] in known and known[fl["id"]].enabled, f"flavour {fl['id']!r} is not enabled in flavours.toml")
        report(f"--- flavour {fl['id']}")
        if "partitions" in fl:
            _verify_standalone(directory, rel, fl, table_csv, report)
        else:
            _verify_prebuilt(directory, rel, fl, report)


def _verify_prebuilt(directory: Path, rel: dict[str, Any], fl: dict[str, Any], report: Report) -> None:
    chip = fl["chip"]
    factory = (directory / fl["images"]["factory"]["file"]).read_bytes()
    app = (directory / fl["images"]["app"]["file"]).read_bytes()
    images.check_chip(factory, chip, "factory image")
    images.check_chip(app, chip, "application image")
    images.check_digest(app, "application image")
    report(f"ok  factory and application images are {chip} images; application digest is valid")
    _verify_manifest(directory, rel, fl, report)


def _verify_manifest(directory: Path, rel: dict[str, Any], fl: dict[str, Any], report: Report) -> None:
    man = json.loads((directory / fl["manifest"]).read_text())
    manifest.validate(man, directory)
    report(f"ok  {fl['manifest']} is valid against the ESP Web Tools schema and its part files exist")
    _need(man["version"] == rel["version"], f"{fl['manifest']} version {man['version']!r} != {rel['version']!r}")
    build = man["builds"][0]
    _need(len(man["builds"]) == 1 and build["chipFamily"] == fl["chip_family"], "manifest chip family is wrong")
    factory = fl["images"]["factory"]
    _need(
        build["parts"] == [{"path": factory["file"], "offset": 0}],
        "manifest must flash the factory image, alone, at offset 0",
    )
    report("ok  manifest flashes the factory image at offset 0 for the right chip family")
    if not fl["improv"]:
        _need(man.get("new_install_improv_wait_time") == 0, "firmware has no Improv but the manifest probes for it")
        report("ok  manifest does not probe for Improv (the firmware does not implement it)")


def _verify_standalone(
    directory: Path, rel: dict[str, Any], fl: dict[str, Any], csv_table: list[partitions.Partition], report: Report
) -> None:
    chip = fl["chip"]
    imgs = fl["images"]

    def blob(kind: str) -> bytes:
        return (directory / imgs[kind]["file"]).read_bytes()

    for e in imgs.values():
        p = directory / e["file"]
        _need(p.is_file(), f"{e['file']} is missing")
        _need(p.stat().st_size == e["size"] and sha256_file(p) == e["sha256"], f"{e['file']} differs from release.json")
    report(f"ok  release.json size and SHA-256 of all {len(imgs)} images match the files")

    factory = blob("factory")
    flash_size = FLASH_SIZES[fl["flash"]["size"]]
    _need(len(factory) == flash_size, f"factory image is {len(factory)} bytes, flash is {flash_size}")
    report(f"ok  factory image is {len(factory)} bytes = the {fl['flash']['size']} flash, so it defines every sector")

    # Partition table, as the chip reads it from flash.
    pt_off = imgs["partition-table"]["offset"]
    in_image = partitions.parse_binary(factory[pt_off : pt_off + partitions.TABLE_SIZE])
    _need(in_image == csv_table, "partition table inside the factory image differs from partitions.csv")
    _need(
        in_image == [partitions.Partition(**{k: v for k, v in p.items()}) for p in fl["partitions"]],
        "release.json partitions differ",
    )
    report(
        f"ok  partition table at 0x{pt_off:x} in the factory image: {len(in_image)} entries, MD5 valid, "
        "identical to partitions.csv and release.json"
    )
    _need(max(p.end for p in in_image) <= flash_size, "a partition lies beyond the end of flash")

    # Bootloader.
    bl_off = imgs["bootloader"]["offset"]
    _need(bl_off == BOOTLOADER_OFFSET[chip], f"bootloader offset 0x{bl_off:x} is not the {chip} ROM load address")
    hdr = images.check_chip(factory[bl_off:], chip, "bootloader")
    images.check_digest(factory[bl_off : bl_off + imgs["bootloader"]["size"]], "bootloader")
    _need(hdr.flash_size_code == FLASH_SIZE_CODE[fl["flash"]["size"]], "bootloader header flash size does not match")
    report(
        f"ok  bootloader at 0x{bl_off:x}: {chip} image header, SHA-256 valid, flash size field {fl['flash']['size']}"
    )

    # Every image sits at the offset of its partition and matches the standalone file.
    slot = partitions.by_name(in_image, "ota_0")
    wants = {
        "otadata": partitions.by_name(in_image, "otadata"),
        "app": slot,
        "www": partitions.by_name(in_image, "www"),
    }
    for kind, part in wants.items():
        _need(
            imgs[kind]["offset"] == part.offset, f"{kind} offset 0x{imgs[kind]['offset']:x} != partition {part.label()}"
        )
        _need(imgs[kind]["size"] <= part.size, f"{kind} image does not fit partition {part.name}")
    report("ok  app, otadata and www offsets equal the ota_0, otadata and www partition offsets")

    covered: list[tuple[int, int]] = []
    for kind in ("bootloader", "partition-table", "otadata", "app", "www"):
        off, data = imgs[kind]["offset"], blob(kind)
        _need(
            factory[off : off + len(data)] == data,
            f"{kind} bytes in the factory image differ from {imgs[kind]['file']}",
        )
        covered.append((off, off + len(data)))
    covered.sort()
    for (_, a_end), (b_start, _) in zip(covered, covered[1:]):
        _need(a_end <= b_start, "two images overlap in the factory image")
    pos = 0
    for start, end in covered + [(flash_size, flash_size)]:
        _need(set(factory[pos:start]) <= {0xFF}, f"factory image has data that belongs to no image before 0x{start:x}")
        pos = end
    report(
        "ok  factory image = the five images at their offsets, every other byte 0xff (including ota_1, nvs, coredump)"
    )

    # Application image in slot ota_0.
    app = factory[slot.offset : slot.offset + imgs["app"]["size"]]
    images.check_chip(app, chip, "application image")
    images.check_digest(app, "application image")
    desc = images.parse_app_desc(app)
    _need(desc.project_name == fl["project_name"], f"project name {desc.project_name!r} != {fl['project_name']!r}")
    _need(desc.version == rel["version"], f"firmware version {desc.version!r} != release {rel['version']!r}")
    _need(imgs["app"]["size"] <= slot.size, "application image does not fit the ota_0 slot")
    report(
        f"ok  application image at 0x{slot.offset:x}: {chip} chip id, SHA-256 valid, "
        f"project {desc.project_name!r}, version {desc.version!r} = release, "
        f"{imgs['app']['size']} of {slot.size} bytes of the slot"
    )

    # Web UI image.
    www = blob("www")
    www_part = wants["www"]
    _need(
        len(www) == www_part.size and images.is_littlefs(www),
        "www image is not a LittleFS image the size of its partition",
    )
    report(f"ok  www image is a LittleFS image of {len(www)} bytes = the www partition")

    # The OTA file is the app slot content, so what the web UI uploads is what USB flashes.
    _need(fl["images"]["app"]["file"].endswith("-app.bin"), "application file name is unexpected")
    report("ok  the OTA upload file is byte-identical to the application image inside the factory image")

    _verify_manifest(directory, rel, fl, report)
