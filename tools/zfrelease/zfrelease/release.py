"""Assemble flavour fragments into release.json and SHA256SUMS; stage a release for the site."""

from __future__ import annotations

import datetime as dt
import json
import os
import shutil
from importlib import metadata
from pathlib import Path
from typing import Any

from . import flavours, manifest
from .package import sha256_file

SCHEMA = 1
SUMS = "SHA256SUMS"
RELEASE_JSON = "release.json"
REPO = "syntroniks/zoneflow"


class ReleaseError(RuntimeError):
    pass


def write_sums(directory: Path) -> None:
    lines = []
    for p in sorted(directory.iterdir()):
        if p.is_file() and p.name != SUMS:
            lines.append(f"{sha256_file(p)}  {p.name}")
    (directory / SUMS).write_text("\n".join(lines) + "\n")


def read_sums(directory: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for n, line in enumerate((directory / SUMS).read_text().splitlines(), 1):
        if not line.strip():
            continue
        digest, sep, name = line.partition("  ")
        if not sep or len(digest) != 64 or "/" in name:
            raise ReleaseError(f"{SUMS} line {n} is not '<sha256>  <file name>'")
        out[name] = digest
    return out


def assemble(
    *,
    directory: Path,
    version: str,
    tag: str,
    commit: str,
    prerelease: bool,
    flavours_toml: Path = flavours.DEFAULT_PATH,
) -> dict[str, Any]:
    wanted = [f for f in flavours.load(flavours_toml).values() if f.enabled]
    found: list[dict[str, Any]] = []
    for f in wanted:
        frag = directory / f"flavour-{f.id}.json"
        if not frag.is_file():
            raise ReleaseError(f"flavour {f.id!r} is enabled in flavours.toml but {frag.name} is missing")
        data = json.loads(frag.read_text())
        man = json.loads((directory / data["manifest"]).read_text())
        manifest.validate(man, directory)
        if man["version"] != version:
            raise ReleaseError(f"{data['manifest']} is for version {man['version']!r}, release is {version!r}")
        found.append(data)
    extra = sorted(
        p.name
        for p in directory.glob("flavour-*.json")
        if p.stem.removeprefix("flavour-") not in {f.id for f in wanted}
    )
    if extra:
        raise ReleaseError(f"fragments for flavours that are not enabled: {', '.join(extra)}")
    for data in found:  # the fragment is folded into release.json and not shipped
        (directory / f"flavour-{data['id']}.json").unlink()

    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    built = dt.datetime.fromtimestamp(int(epoch), dt.UTC) if epoch else dt.datetime.now(dt.UTC)
    rel = {
        "schema": SCHEMA,
        "version": version,
        "tag": tag,
        "prerelease": prerelease,
        "commit": commit,
        "repository": REPO,
        "built_utc": built.strftime("%Y-%m-%dT%H:%M:%SZ"),
        # The esptool that made the factory image; the installer page quotes it in the manual commands.
        "tools": {"esptool": metadata.version("esptool")},
        "flavours": found,
    }
    (directory / RELEASE_JSON).write_text(json.dumps(rel, indent=2) + "\n")
    write_sums(directory)
    return rel


def check_sums(directory: Path) -> None:
    """Every file in the directory matches SHA256SUMS, and every listed file exists."""
    sums = read_sums(directory)
    present = {p.name for p in directory.iterdir() if p.is_file() and p.name != SUMS}
    if present != set(sums):
        diff = sorted(present ^ set(sums))
        raise ReleaseError(f"{SUMS} and the directory disagree about: {', '.join(diff)}")
    for name, digest in sums.items():
        if sha256_file(directory / name) != digest:
            raise ReleaseError(f"{name} does not match {SUMS}")


def stage_site(release_dir: Path, site_dir: Path) -> Path:
    """Copy a published release into the built docs site, after checking its checksums.

    The result is ``<site>/firmware/releases/latest/``, which the installer page reads.
    """
    check_sums(release_dir)
    rel = json.loads((release_dir / RELEASE_JSON).read_text())
    if rel.get("schema") != SCHEMA:
        raise ReleaseError(f"release.json schema {rel.get('schema')!r} is not {SCHEMA}")
    dest = site_dir / "firmware" / "releases" / "latest"
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for p in release_dir.iterdir():
        if p.is_file():
            shutil.copyfile(p, dest / p.name)
    return dest


def notes(directory: Path) -> str:
    """Release notes that GitHub's generated change list is appended to."""
    rel = json.loads((directory / RELEASE_JSON).read_text())
    sums = sha256_file(directory / SUMS)
    lines = [
        f"ZoneFlow firmware {rel['version']}" + (" (pre-release)" if rel["prerelease"] else ""),
        "",
        "> **No ZoneFlow board has run this firmware.** CI built it and checked that the files are well formed "
        "(partition table, image headers, checksums, manifest). Whether it boots needs a board.",
        "",
        "Install: https://syntroniks.github.io/zoneflow/firmware/web-installer/ (Chromium on a desktop), "
        "`tools/zfflash`, or `esptool`. Update a running controller with the `-app.bin` file in its web "
        "interface. See https://syntroniks.github.io/zoneflow/firmware/releases/.",
        "",
        "| Flavour | Install (whole flash) | Update (keeps settings) |",
        "|---|---|---|",
    ]
    for f in rel["flavours"]:
        app = f["images"]["app"]["file"]
        lines.append(f"| {f['name']} | `{f['images']['factory']['file']}` | `{app}` |")
    lines += [
        "",
        f"Built from commit `{rel['commit']}`. Check downloads with `sha256sum --check SHA256SUMS`.",
        "SHA-256 of `SHA256SUMS` itself, to compare with the copy on the project site (`zfflash --sums-sha256`):",
        "",
        f"    {sums}",
        "",
        "The images are not signed.",
        "",
    ]
    return "\n".join(lines)
