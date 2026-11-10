"""Find a release, download it and check it against its published checksums."""

from __future__ import annotations

import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .http import Http, NetworkError

REPO = "syntroniks/zoneflow"
DEFAULT_SOURCE = "https://syntroniks.github.io/zoneflow/firmware/releases/latest/"
RELEASE_JSON = "release.json"
SUMS = "SHA256SUMS"


class ReleaseError(RuntimeError):
    pass


def release_source(tag: str) -> str:
    return f"https://github.com/{REPO}/releases/download/{tag}/"


def cache_root() -> Path:
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "zfflash"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_sums(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        digest, sep, name = line.partition("  ")
        if not sep or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest) or "/" in name:
            raise ReleaseError(f"{SUMS} line {n} is not '<sha256>  <file name>'")
        out[name] = digest
    return out


@dataclass
class Release:
    """A release whose release.json and SHA256SUMS agree. Files are fetched and checked on demand."""

    source: str  # URL ending in "/" or a directory path
    info: dict[str, Any]
    sums: dict[str, str]
    cache: Path
    http: Http

    @property
    def version(self) -> str:
        return self.info["version"]

    def flavour(self, fid: str) -> dict[str, Any]:
        for f in self.info["flavours"]:
            if f["id"] == fid:
                return f
        ids = ", ".join(f["id"] for f in self.info["flavours"])
        raise ReleaseError(f"release {self.version} has no flavour {fid!r} (it has: {ids})")

    def fetch(self, name: str) -> Path:
        """Return a local copy of a release file whose SHA-256 matches SHA256SUMS and release.json."""
        want = self.sums.get(name)
        if want is None:
            raise ReleaseError(f"{name} is not listed in {SUMS}; refusing to use it")
        for flavour in self.info["flavours"]:
            meta = flavour["files"].get(name)
            if meta and meta["sha256"] != want:
                raise ReleaseError(f"{SUMS} and {RELEASE_JSON} disagree about {name}; refusing to use it")
        dest = self.cache / name
        if dest.is_file() and sha256_file(dest) == want:
            return dest
        self.cache.mkdir(parents=True, exist_ok=True)
        if is_url(self.source):
            self.http.download(self.source + name, dest)
        else:
            src = Path(self.source) / name
            if not src.is_file():
                raise ReleaseError(f"{src} not found")
            dest.write_bytes(src.read_bytes())
        got = sha256_file(dest)
        if got != want:
            dest.unlink()
            raise ReleaseError(f"{name}: SHA-256 is {got}, the published checksum is {want}. Nothing was flashed.")
        return dest


def is_url(source: str) -> bool:
    return source.startswith(("http://", "https://"))


def _read(source: str, name: str, http: Http) -> bytes:
    if is_url(source):
        r = http.request("GET", source + name)
        if r.status != 200:
            raise ReleaseError(f"GET {source}{name}: HTTP {r.status}")
        return r.body
    p = Path(source) / name
    if not p.is_file():
        raise ReleaseError(f"{p} not found")
    return p.read_bytes()


def open_release(source: str, http: Http, cache_dir: Path | None = None, sums_sha256: str | None = None) -> Release:
    """Read release.json and SHA256SUMS from ``source``.

    ``sums_sha256`` pins SHA256SUMS to a digest obtained somewhere else (the release notes
    carry it). Without it, the checksums protect against damage in transit, not against whoever
    controls ``source``.
    """
    if is_url(source) and not source.endswith("/"):
        source += "/"
    try:
        sums_raw = _read(source, SUMS, http)
        info_raw = _read(source, RELEASE_JSON, http)
    except NetworkError as e:
        raise ReleaseError(str(e)) from e
    if sums_sha256 is not None and hashlib.sha256(sums_raw).hexdigest() != sums_sha256.lower():
        raise ReleaseError(f"{SUMS} does not have SHA-256 {sums_sha256}")
    sums = parse_sums(sums_raw.decode())
    if sums.get(RELEASE_JSON) != hashlib.sha256(info_raw).hexdigest():
        raise ReleaseError(f"{RELEASE_JSON} does not match {SUMS}")
    info = json.loads(info_raw)
    if info.get("schema") != 1:
        raise ReleaseError(f"release.json schema {info.get('schema')!r} is newer than this zfflash understands (1)")
    cache = (cache_dir or cache_root()) / info["version"]
    return Release(source, info, sums, cache, http)
