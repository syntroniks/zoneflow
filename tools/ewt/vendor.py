#!/usr/bin/env python3
"""Vendor ESP Web Tools into the docs site, pinned by version and hash. Standard library only.

    python3 tools/ewt/vendor.py check              # CI: the vendored files are the pinned ones
    python3 tools/ewt/vendor.py update 10.4.0      # download, verify against npm, write files and VENDOR.json

The installer page loads this copy, not a CDN, so the page cannot change unless this
directory changes in a pull request. `check` fails when a file differs from VENDOR.json, when a file
is missing or extra, or when the page does not load the pinned version with its integrity hash.
"""

from __future__ import annotations

import base64
import hashlib
import io
import json
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "docs" / "assets" / "vendor" / "esp-web-tools"
PAGE = ROOT / "docs" / "firmware" / "web-installer.md"
ENTRY = "install-button.js"


def sri(path: Path) -> str:
    return "sha384-" + base64.b64encode(hashlib.sha384(path.read_bytes()).digest()).decode()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def files_in(directory: Path) -> list[Path]:
    return sorted(p for p in directory.rglob("*") if p.is_file() and p.name != "VENDOR.json")


def update(version: str) -> None:
    meta = json.load(urllib.request.urlopen(f"https://registry.npmjs.org/esp-web-tools/{version}"))
    tarball, integrity = meta["dist"]["tarball"], meta["dist"]["integrity"]
    data = urllib.request.urlopen(tarball).read()
    algo, _, want = integrity.partition("-")
    got = base64.b64encode(hashlib.new(algo, data).digest()).decode()
    if got != want:
        sys.exit(f"tarball {algo} {got} does not match npm's {want}")
    target = DEST / version
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for member in tar.getmembers():
            name = member.name.removeprefix("package/")
            if member.isfile() and (name.startswith("dist/web/") or name == "LICENSE"):
                out = target / name.removeprefix("dist/web/")
                out.write_bytes(tar.extractfile(member).read())
    vendor = {
        "package": "esp-web-tools",
        "version": version,
        "tarball": tarball,
        "integrity": integrity,
        "licence": "Apache-2.0 (LICENSE in this directory)",
        "source": "dist/web/ of the npm package, unmodified; LICENSE from the package root",
        "entry": ENTRY,
        "entry_sri": sri(target / ENTRY),
        "files": {str(p.relative_to(target)): sha256(p) for p in files_in(target)},
    }
    (target / "VENDOR.json").write_text(json.dumps(vendor, indent=2) + "\n")
    print(f"vendored esp-web-tools {version}: {len(vendor['files'])} files")
    print(f"update the version and integrity in {PAGE.relative_to(ROOT)}: {vendor['entry_sri']}")


def check() -> int:
    versions = [p for p in DEST.iterdir() if p.is_dir()] if DEST.exists() else []
    if len(versions) != 1:
        print(f"expected exactly one vendored version in {DEST}, found {len(versions)}")
        return 1
    d = versions[0]
    vendor = json.loads((d / "VENDOR.json").read_text())
    problems = []
    if d.name != vendor["version"]:
        problems.append(f"directory {d.name} but VENDOR.json says {vendor['version']}")
    have = {str(p.relative_to(d)): sha256(p) for p in files_in(d)}
    for name in sorted(set(have) | set(vendor["files"])):
        if have.get(name) != vendor["files"].get(name):
            problems.append(f"{name}: {'missing' if name not in have else 'extra' if name not in vendor['files'] else 'changed'}")
    if sri(d / ENTRY) != vendor["entry_sri"]:
        problems.append("entry script does not match entry_sri")
    page = PAGE.read_text()
    src = f"../../assets/vendor/esp-web-tools/{vendor['version']}/{ENTRY}"
    if src not in page:
        problems.append(f"{PAGE.name} does not load {src}")
    if f'integrity="{vendor["entry_sri"]}"' not in page:
        problems.append(f"{PAGE.name} does not carry the integrity hash {vendor['entry_sri']}")
    for p in problems:
        print("vendor check:", p)
    if not problems:
        print(f"esp-web-tools {vendor['version']}: {len(have)} files match VENDOR.json; page loads it with SRI")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "check":
        sys.exit(check())
    if len(sys.argv) == 3 and sys.argv[1] == "update":
        update(sys.argv[2])
        sys.exit(0)
    sys.exit(__doc__)
