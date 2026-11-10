"""Fakes for the three outside layers: HTTP, esptool and the serial port list."""

from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path

import pytest

from zfflash.cli import Env
from zfflash.http import Response
from zfflash.ports import Port

BOARD = Port("/dev/ttyACM0", "USB JTAG/serial debug unit", 0x303A, 0x1001)
OTHER = Port("/dev/ttyUSB9", "Some modem", 0x1234, 0x5678)


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class FakeFlasher:
    def __init__(self):
        self.calls: list[tuple] = []

    def write(self, port, baud, chip, parts):
        self.calls.append(("write", port, baud, chip, [(o, p.name, p.read_bytes()) for o, p in parts]))

    def erase_region(self, port, baud, chip, offset, size):
        self.calls.append(("erase", port, baud, chip, offset, size))


class FakeHttp:
    """routes: (method, url) -> Response or callable(headers, data) -> Response."""

    def __init__(self, routes=None):
        self.routes = routes or {}
        self.calls: list[dict] = []

    def request(self, method, url, *, headers=None, data=None, timeout=30.0):
        self.calls.append({"method": method, "url": url, "headers": headers or {}, "data": data})
        r = self.routes.get((method, url))
        if r is None:
            return Response(404, b'{"error":{"code":"not_found","message":"no such route"}}')
        return r(headers or {}, data) if callable(r) else r

    def download(self, url, dest, *, timeout=60.0):
        self.calls.append({"method": "DOWNLOAD", "url": url})
        r = self.routes.get(("GET", url))
        if r is None or r.status != 200:
            from zfflash.http import NetworkError

            raise NetworkError(f"GET {url}: HTTP {getattr(r, 'status', 404)}")
        dest.write_bytes(r.body)


FILES = {
    "zoneflow-standalone-1.2.3-factory.bin": b"F" * 5000,
    "zoneflow-standalone-1.2.3-app.bin": b"A" * 3000,
    "zoneflow-standalone-1.2.3-www.bin": b"W" * 2000,
}


def release_files(files: dict[str, bytes] | None = None) -> dict[str, bytes]:
    """The files of a release: images, release.json and SHA256SUMS, as zfrelease writes them."""
    files = dict(files or FILES)
    meta = {k: {"size": len(v), "sha256": sha(v)} for k, v in files.items()}
    info = {
        "schema": 1,
        "version": "1.2.3",
        "tag": "v1.2.3",
        "prerelease": False,
        "flavours": [
            {
                "id": "standalone",
                "chip": "esp32s3",
                "partitions": [
                    {"name": "nvs", "offset": 0x9000, "size": 0x15000},
                    {"name": "otadata", "offset": 0x1E000, "size": 0x2000},
                    {"name": "ota_0", "offset": 0x20000, "size": 0x400000},
                ],
                "images": {
                    kind: {"file": name, "offset": offset, **meta[name]}
                    for kind, offset in (("factory", 0), ("app", 0x20000), ("www", 0x820000))
                    for name in [f"zoneflow-standalone-1.2.3-{kind}.bin"]
                },
                "files": meta,
            }
        ],
    }
    out = dict(files)
    out["release.json"] = json.dumps(info).encode()
    out["SHA256SUMS"] = "".join(f"{sha(v)}  {k}\n" for k, v in sorted(out.items())).encode()
    return out


@pytest.fixture
def release_dir(tmp_path: Path) -> Path:
    d = tmp_path / "release"
    d.mkdir()
    for name, data in release_files().items():
        (d / name).write_bytes(data)
    return d


class Console:
    def __init__(self):
        self.out, self.err = io.StringIO(), io.StringIO()


@pytest.fixture
def make_env(tmp_path):
    def make(*, http=None, ports=None, interactive=False, answers=(), password="pw"):
        console = Console()
        flasher = FakeFlasher()
        asked: list[str] = []
        it = iter(answers)

        def ask(q):
            asked.append(q)
            return next(it, "n")

        env = Env(
            http=http or FakeHttp(),
            flasher=flasher,
            lister=lambda: [BOARD] if ports is None else ports,
            ask=ask,
            askpass=lambda _prompt: password,
            out=console.out,
            err=console.err,
            interactive=interactive,
        )
        env.console, env.flasher_fake, env.asked = console, flasher, asked
        return env

    return make
