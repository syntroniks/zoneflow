# SPDX-License-Identifier: Apache-2.0
"""Run Freerouting on a Specctra DSN, with a cache so rebuilds are reproducible.

Freerouting's optimiser is multi-threaded and its result can differ between
runs. The session file is therefore kept in ``<project>/route/`` together with
the hash of the DSN it was made from. If the DSN has not changed the stored
session is reused; ``--force`` routes again.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

VERSION = "2.5.0"
JAR_URL = f"https://github.com/freerouting/freerouting/releases/download/v{VERSION}/freerouting-{VERSION}.jar"
JAR_SHA256 = "f6f51bb02245e8e717f9359bd260cc9c5c0b1bc0acc8b7cb2cd5b8ffeb5de3c7"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def dsn_hash(path: Path) -> str:
    """Hash of the DSN without its first line, which holds the file's own path."""
    text = path.read_text(encoding="utf-8").split("\n", 1)[1]
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def fetch_jar(cache: Path) -> Path:
    jar = cache / f"freerouting-{VERSION}.jar"
    if jar.exists() and sha256(jar) == JAR_SHA256:
        return jar
    cache.mkdir(parents=True, exist_ok=True)
    log = cache / "download.log"
    tmp = jar.with_suffix(".part")
    with urllib.request.urlopen(JAR_URL) as r, open(tmp, "wb") as out, open(log, "w") as lg:
        total = int(r.headers.get("Content-Length", 0))
        done = 0
        last = 0.0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            if time.time() - last > 1:
                lg.write(f"{done}/{total} bytes\n")
                lg.flush()
                last = time.time()
        lg.write(f"{done}/{total} bytes, done\n")
    if sha256(tmp) != JAR_SHA256:
        tmp.unlink()
        raise SystemExit(f"checksum mismatch for {JAR_URL}")
    tmp.rename(jar)
    return jar


def parse_log(text: str) -> dict:
    stats: dict = {}
    m = re.findall(r"Auto-routing pass #(\d+).*?\((\d+) unrouted and (\d+) violations\)", text)
    if m:
        stats["autoroute_passes"] = int(m[-1][0])
        stats["freerouting_unrouted"] = int(m[-1][1])
        stats["freerouting_violations"] = int(m[-1][2])
    m = re.search(r"Auto-routing stage started.*?for (\d+) unrouted items", text)
    if m:
        stats["connections_to_route"] = int(m.group(1))
    m = re.search(r"finished with state: (\w+) \(elapsed: ([\d.]+) seconds", text)
    if m:
        stats["state"] = m.group(1)
        stats["seconds"] = float(m.group(2))
    return stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.route")
    ap.add_argument("project_dir")
    ap.add_argument("--cache", required=True, help="directory for the Freerouting jar")
    ap.add_argument("--passes", type=int, default=20)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--force", action="store_true", help="route again even if the DSN is unchanged")
    args = ap.parse_args(argv)

    project = Path(args.project_dir)
    name = json.loads((project / "build" / "model.json").read_text())["name"]
    dsn = project / "build" / f"{name}.dsn"
    route_dir = project / "route"
    route_dir.mkdir(exist_ok=True)
    ses = route_dir / f"{name}.ses"
    stamp = route_dir / f"{name}.dsn.sha256"
    stats_file = route_dir / f"{name}.freerouting.json"
    digest = dsn_hash(dsn)

    if not args.force and ses.exists() and stamp.exists() and stamp.read_text().strip() == digest:
        print(f"route: DSN unchanged, reusing {ses}")
        return 0

    jar = fetch_jar(Path(args.cache))
    out = project / "build" / f"{name}.ses"
    log = project / "build" / "freerouting.log"
    cmd = ["java", "-Djava.awt.headless=true", "-jar", str(jar), "--gui.enabled=false",
           "-de", str(dsn), "-do", str(out), "-mp", str(args.passes)]
    t0 = time.time()
    with open(log, "w") as lg:
        r = subprocess.run(cmd, stdout=lg, stderr=subprocess.STDOUT, timeout=args.timeout)
    if r.returncode != 0 or not out.exists():
        sys.stderr.write(f"Freerouting failed, see {log}\n")
        return 1
    stats = parse_log(log.read_text(errors="replace"))
    stats["freerouting_version"] = VERSION
    stats["wall_seconds"] = round(time.time() - t0, 1)
    shutil.copyfile(out, ses)
    stamp.write_text(digest + "\n")
    stats_file.write_text(json.dumps(stats, indent=1, sort_keys=True) + "\n")
    print(f"route: Freerouting {VERSION} finished in {stats['wall_seconds']} s: {stats}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
