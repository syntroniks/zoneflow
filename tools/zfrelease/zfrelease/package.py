"""Turn a firmware build into release files.

Standalone firmware: reads the ESP-IDF build directory (``flasher_args.json``),
checks every offset against ``partitions.csv`` and the partition table the build
produced, merges one factory image with ``esptool merge-bin``, and writes the
OTA image, per-image copies, checksums and an ESP Web Tools manifest.

Offsets are never written down in this tool. They come from the build's
``flasher_args.json`` and are compared with ``partitions.csv`` and the binary
partition table; a disagreement stops the release.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from . import flavours, images, manifest, partitions

# The ROM bootloader of the ESP32-S3 loads the second stage from flash offset 0.
# A property of the chip (ESP32-S3 TRM), not of this project.
BOOTLOADER_OFFSET = {"esp32s3": 0x0}
FLASH_SIZES = {"4MB": 4 << 20, "8MB": 8 << 20, "16MB": 16 << 20, "32MB": 32 << 20}

# flasher_args.json key -> short name used in file names and release.json.
IMAGE_KEYS = {
    "bootloader": "bootloader",
    "partition-table": "partition-table",
    "otadata": "otadata",
    "app": "app",
    "www": "www",
}


class PackageError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_name(flavour: str, version: str, kind: str) -> str:
    return f"zoneflow-{flavour}-{version}-{kind}.bin"


def manifest_name(flavour: str) -> str:
    return f"manifest-{flavour}.json"


def _entry(path: Path, offset: int | None) -> dict[str, Any]:
    e: dict[str, Any] = {"file": path.name, "size": path.stat().st_size, "sha256": sha256_file(path)}
    if offset is not None:
        e["offset"] = offset
    return e


def read_flasher_args(build_dir: Path) -> dict[str, Any]:
    p = build_dir / "flasher_args.json"
    if not p.is_file():
        raise PackageError(f"{p} not found: build the firmware first (idf.py build)")
    return json.loads(p.read_text())


def merge_bin(chip: str, out: Path, settings: dict[str, str], placed: list[tuple[int, Path]]) -> None:
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "merge-bin",
        "-o",
        str(out),
        "--flash-mode",
        settings["flash_mode"],
        "--flash-freq",
        settings["flash_freq"],
        "--flash-size",
        settings["flash_size"],
        "--pad-to-size",
        settings["flash_size"],  # esptool takes a size name such as 16MB here
    ]
    for offset, path in sorted(placed):
        cmd += [hex(offset), str(path)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise PackageError(f"esptool merge-bin failed ({proc.returncode}):\n{proc.stdout}{proc.stderr}")


def package_standalone(
    *,
    build_dir: Path,
    partitions_csv: Path,
    version: str,
    out_dir: Path,
    flavour_id: str = "standalone",
    commit: str = "",
    flavours_toml: Path = flavours.DEFAULT_PATH,
) -> dict[str, Any]:
    fl = flavours.get(flavour_id, flavours_toml)
    chip = fl.chip
    args = read_flasher_args(build_dir)
    if args.get("extra_esptool_args", {}).get("chip") != chip:
        raise PackageError(f"build is for {args.get('extra_esptool_args', {}).get('chip')!r}, flavour needs {chip!r}")
    settings = args["flash_settings"]
    flash_size = FLASH_SIZES.get(settings["flash_size"])
    if flash_size is None:
        raise PackageError(f"unsupported flash size {settings['flash_size']!r}")

    # 1. Where the build says each image goes.
    placed: dict[str, tuple[int, Path]] = {}
    for key, short in IMAGE_KEYS.items():
        if key not in args:
            raise PackageError(
                f"flasher_args.json has no {key!r} entry"
                + (": the web UI was not packed (build firmware/zoneflow-fw/web first)" if key == "www" else "")
            )
        offset = int(args[key]["offset"], 0)
        path = build_dir / args[key]["file"]
        if not path.is_file():
            raise PackageError(f"{path} is missing")
        placed[short] = (offset, path)
    listed = {int(o, 0) for o in args["flash_files"]}
    if listed != {o for o, _ in placed.values()}:
        raise PackageError("flash_files in flasher_args.json lists images this tool does not know about")

    # 2. Compare with the partition table source and the table the build wrote.
    table = partitions.parse_csv(partitions_csv.read_text())
    built = partitions.parse_binary(placed["partition-table"][1].read_bytes())
    if built != table:
        raise PackageError("the partition table the build wrote differs from partitions.csv")
    expect = {
        "bootloader": BOOTLOADER_OFFSET[chip],
        "otadata": partitions.by_name(table, "otadata").offset,
        "app": partitions.by_name(table, "ota_0").offset,
        "www": partitions.by_name(table, "www").offset,
    }
    for short, want in expect.items():
        got = placed[short][0]
        if got != want:
            raise PackageError(f"{short} is flashed at 0x{got:x} by the build but the partition table says 0x{want:x}")
    www = partitions.by_name(table, "www")
    if placed["www"][1].stat().st_size != www.size:
        raise PackageError("www.bin is not the size of the www partition")
    app_part = partitions.by_name(table, "ota_0")
    if placed["app"][1].stat().st_size > app_part.size:
        raise PackageError("the application image does not fit its OTA slot")
    if max(p.end for p in table) > flash_size:
        raise PackageError(f"the partition table extends beyond {settings['flash_size']} of flash")

    # 3. Sanity-check the inputs before a release file depends on them.
    app_bytes = placed["app"][1].read_bytes()
    images.check_chip(app_bytes, chip, "application image")
    images.check_digest(app_bytes, "application image")
    desc = images.parse_app_desc(app_bytes)
    if desc.project_name != fl.project_name:
        raise PackageError(f"application project name is {desc.project_name!r}, expected {fl.project_name!r}")
    if desc.version != version:
        raise PackageError(
            f"firmware version is {desc.version!r} but the release is {version!r}: "
            "write the version to firmware/zoneflow-fw/version.txt before building"
        )

    # 4. Write release files.
    out_dir.mkdir(parents=True, exist_ok=True)
    files: dict[str, dict[str, Any]] = {}
    imgs: dict[str, dict[str, Any]] = {}
    for short, (offset, src) in placed.items():
        dest = out_dir / file_name(fl.id, version, "app" if short == "app" else short)
        shutil.copyfile(src, dest)
        imgs[short] = _entry(dest, offset)
    factory = out_dir / file_name(fl.id, version, "factory")
    merge_bin(chip, factory, settings, [(o, out_dir / imgs[s]["file"]) for s, (o, _) in placed.items()])
    if factory.stat().st_size != flash_size:
        raise PackageError(f"factory image is {factory.stat().st_size} bytes, expected {flash_size}")
    imgs["factory"] = _entry(factory, 0)

    man = manifest.build(fl.name, version, chip, [(imgs["factory"]["file"], 0)], fl.improv)
    manifest.validate(man)
    man_path = out_dir / manifest_name(fl.id)
    man_path.write_text(json.dumps(man, indent=2) + "\n")

    for e in imgs.values():
        files[e["file"]] = {"size": e["size"], "sha256": e["sha256"]}
    files[man_path.name] = {"size": man_path.stat().st_size, "sha256": sha256_file(man_path)}

    fragment = {
        "id": fl.id,
        "name": fl.name,
        "description": fl.description,
        "chip": chip,
        "chip_family": manifest.CHIP_FAMILY[chip],
        "project_name": fl.project_name,
        "improv": fl.improv,
        "next_step": fl.next_step,
        "manifest": man_path.name,
        "flash": {
            "size": settings["flash_size"],
            "size_bytes": flash_size,
            "mode": settings["flash_mode"],
            "freq": settings["flash_freq"],
        },
        "partitions": [partitions.to_json(p) for p in table],
        "images": imgs,
        "files": files,
        "commit": commit,
    }
    (out_dir / f"flavour-{fl.id}.json").write_text(json.dumps(fragment, indent=2) + "\n")
    return fragment


def package_prebuilt(
    *,
    flavour_id: str,
    factory: Path,
    app: Path,
    version: str,
    out_dir: Path,
    commit: str = "",
    flavours_toml: Path = flavours.DEFAULT_PATH,
) -> dict[str, Any]:
    """For a flavour whose build tool already makes a merged image (ESPHome does).

    ``factory`` is flashed at offset 0, ``app`` is the OTA image. The partition layout is
    that build tool's business; only the image headers are checked here.
    """
    fl = flavours.get(flavour_id, flavours_toml)
    chip = fl.chip
    fb, ab = factory.read_bytes(), app.read_bytes()
    images.check_chip(fb, chip, "factory image")  # a merged image starts with the bootloader
    images.check_chip(ab, chip, "application image")
    images.check_digest(ab, "application image")
    if fl.project_name:
        desc = images.parse_app_desc(ab)
        if desc.project_name != fl.project_name:
            raise PackageError(f"application project name is {desc.project_name!r}, expected {fl.project_name!r}")

    out_dir.mkdir(parents=True, exist_ok=True)
    imgs: dict[str, dict[str, Any]] = {}
    for kind, src, offset in (("factory", factory, 0), ("app", app, None)):
        dest = out_dir / file_name(fl.id, version, kind)
        shutil.copyfile(src, dest)
        imgs[kind] = _entry(dest, offset)
    man = manifest.build(fl.name, version, chip, [(imgs["factory"]["file"], 0)], fl.improv)
    manifest.validate(man)
    man_path = out_dir / manifest_name(fl.id)
    man_path.write_text(json.dumps(man, indent=2) + "\n")
    files = {e["file"]: {"size": e["size"], "sha256": e["sha256"]} for e in imgs.values()}
    files[man_path.name] = {"size": man_path.stat().st_size, "sha256": sha256_file(man_path)}
    fragment = {
        "id": fl.id,
        "name": fl.name,
        "description": fl.description,
        "chip": chip,
        "chip_family": manifest.CHIP_FAMILY[chip],
        "project_name": fl.project_name,
        "improv": fl.improv,
        "next_step": fl.next_step,
        "manifest": man_path.name,
        "images": imgs,
        "files": files,
        "commit": commit,
    }
    (out_dir / f"flavour-{fl.id}.json").write_text(json.dumps(fragment, indent=2) + "\n")
    return fragment
