# SPDX-License-Identifier: Apache-2.0
"""Run Freerouting on a Specctra DSN, with a cache so rebuilds are reproducible.

Freerouting's optimiser is multi-threaded and its result can differ between
runs. The session file is therefore kept in ``<project>/route/`` together with
the hash of the DSN it was made from. If the DSN has not changed the stored
session is reused; ``--force`` routes again.

KiCad writes the value text of every placed part into the DSN, as ``(PN ...)``.
The autorouter does not route by it. A second hash, of the DSN without that
text, is kept beside the first, so that a change of a part's value alone (a
220 pF capacitor that becomes 330 pF in the same footprint) reuses the stored
session too. Anything else that differs still fails the comparison: a part,
a position, a rotation, a footprint, a pad, a net, a rule, a keep-out.

Parts taken off a board after it was routed are a third case. The copper that
was routed round them is still good copper, and routing the board again would
move every other track. ``route/<name>.dsn.delta`` then lists, line by line,
what the DSN of the board as it is now has less and more than the DSN the
session was routed from. The session is reused only if taking those lines back
gives a file with the stored hash: so the delta is exact, it is committed, and
a reviewer reads in it everything the autorouter saw differently. Whether the
old copper still fits the board is for DRC to say, as for the patches.
``--write-delta`` writes the file from the DSN of the routed board.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
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


# A placement as KiCad writes it: (place C28 129800 -95600 front 0 (PN "220p C0G")). The second
# group is the value text, quoted when it has a space in it.
PLACE_VALUE = re.compile(r'^(\s*\(place \S+ -?[\d.]+ -?[\d.]+ \S+ -?[\d.]+) \(PN (?:"[^"]*"|[^\s()"]+)\)\)$', re.M)


def dsn_hash(path: Path, settings: str = "", values: bool = True) -> str:
    """Hash of the DSN without its first line, which holds the file's own path, and of the router settings.

    With ``values=False`` the value text of the placed parts is left out as well: the hash of
    what the autorouter routes.
    """
    return text_hash(_body(path), settings, values)


def text_hash(text: str, settings: str = "", values: bool = True) -> str:
    """The same, of a DSN body that is already in memory."""
    if not values:
        text = PLACE_VALUE.sub(r"\1)", text)
    return hashlib.sha256((text + settings).encode("utf-8")).hexdigest()


def _body(path: Path) -> str:
    """A DSN without its first line, which holds the file's own path."""
    return path.read_text(encoding="utf-8").split("\n", 1)[1]


DELTA_HEAD = """\
# What the Specctra file of the board as it is now has less and more than the file the stored
# session was routed from. WRITTEN BY hwflow.route --write-delta: do not edit.
#
# A line that starts with "-" was in the routed file and is gone. A line that starts with "+" is
# new. The build takes these lines back and reuses the session only if the result has the hash
# in {stamp}, {digest},
# or differs from the file with that hash only in the value text of parts.
#
# It is for parts taken off the board after routing. Every line is something the autorouter saw
# differently from the board that is built: read them all. DRC judges whether the copper fits.
"""
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def make_delta(routed: str, current: str) -> str:
    """The lines that differ between two DSN bodies, as a unified diff with no context lines."""
    diff = difflib.unified_diff(routed.split("\n"), current.split("\n"), n=0, lineterm="")
    return "\n".join(line for line in diff if not line.startswith(("--- ", "+++ "))) + "\n"


def undo_delta(current: str, delta: str) -> str:
    """The routed DSN body, from the current one and the delta. ValueError if the delta is not of this file."""
    cur = current.split("\n")
    out: list[str] = []
    pos = 0
    hunks = [line for line in delta.split("\n") if line and not line.startswith("#")]
    i = 0
    while i < len(hunks):
        m = HUNK.match(hunks[i])
        if not m:
            raise ValueError(f"not a hunk header: {hunks[i][:60]!r}")
        i += 1
        n_cur = int(m.group(4)) if m.group(4) is not None else 1
        n_old = int(m.group(2)) if m.group(2) is not None else 1
        # With no line on one side, a unified diff gives the line before the place.
        start = int(m.group(3)) - 1 if n_cur else int(m.group(3))
        old = [line[1:] for line in hunks[i:i + n_old]]
        new = [line[1:] for line in hunks[i + n_old:i + n_old + n_cur]]
        if (len(old), len(new)) != (n_old, n_cur) or any(not line.startswith("-") for line in hunks[i:i + n_old]) \
                or any(not line.startswith("+") for line in hunks[i + n_old:i + n_old + n_cur]):
            raise ValueError(f"hunk at line {m.group(3)} is not complete")
        i += n_old + n_cur
        if start < pos or cur[start:start + n_cur] != new:
            raise ValueError(f"the lines at {m.group(3)} of this DSN are not the ones the delta has")
        out += cur[pos:start] + old
        pos = start + n_cur
    return "\n".join(out + cur[pos:])


def delta_counts(delta: str) -> tuple[int, int]:
    """How many lines the delta says are gone, and how many are new."""
    lines = [line for line in delta.split("\n") if line and not line.startswith(("#", "@@"))]
    return sum(line.startswith("-") for line in lines), sum(line.startswith("+") for line in lines)


def write_settings(config_home: Path, router: dict) -> Path:
    """Freerouting's settings file, in a directory of the build, with its usage reporting off.

    Freerouting 2.5.0 reports usage to its maker's server unless told not to:
    the command line with the design file's name, the host name, and board
    statistics. The defaults are ``disable_analytics: false`` and
    ``allow_telemetry: true``. This file turns both off. The caller also
    points the Java proxy settings at a closed local port, so that nothing can
    leave even if a later version ignores the file.
    """
    path = config_home / "freerouting" / "freerouting.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    settings = {
        "version": VERSION,
        "profile": {"id": "00000000-0000-0000-0000-000000000000", "email": "", "allow_telemetry": False,
                    "allow_contact": False},
        "usage_and_diagnostic_data": {"disable_analytics": True, "track_window_changed": False,
                                      "track_button_clicked": False},
        "router": router,
    }
    path.write_text(json.dumps(settings, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return path


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
    ap.add_argument("--stored-only", action="store_true",
                    help="never run the autorouter: fail unless the stored session was made from this DSN (for CI)")
    ap.add_argument("--write-delta", metavar="ROUTED_DSN",
                    help="write route/<name>.dsn.delta from the DSN the stored session was routed from, and stop. "
                         "Refused unless that file has the stored hash")
    args = ap.parse_args(argv)

    project = Path(args.project_dir)
    name = json.loads((project / "build" / "model.json").read_text())["name"]
    dsn = project / "build" / f"{name}.dsn"
    route_dir = project / "route"
    route_dir.mkdir(exist_ok=True)
    ses = route_dir / f"{name}.ses"
    stamp = route_dir / f"{name}.dsn.sha256"
    stats_file = route_dir / f"{name}.freerouting.json"
    router_file = project / "build" / "router.json"
    router = json.loads(router_file.read_text(encoding="utf-8")) if router_file.exists() else {}
    settings = json.dumps(router, sort_keys=True) if router else ""
    digest = dsn_hash(dsn, settings)
    # The same without the value text of the parts. The first stamp stays the name of the stored
    # session, which the patches of board.toml carry; this one only widens what may reuse it.
    stamp_novalues = route_dir / f"{name}.dsn.novalues.sha256"
    digest_novalues = dsn_hash(dsn, settings, values=False)
    # What the board has less and more than the routed one, if parts were taken off it since.
    delta_file = route_dir / f"{name}.dsn.delta"

    if args.write_delta:
        routed = Path(args.write_delta)
        stored = stamp.read_text().strip() if stamp.exists() else "none"
        # The routed file itself, or one that differs from it only in the value text of parts.
        same = dsn_hash(routed, settings) == stored or (
            stamp_novalues.exists() and stamp_novalues.read_text().strip() == dsn_hash(routed, settings, values=False))
        if not same:
            sys.stderr.write(f"route: {routed} is not the file the stored session was routed from.\n"
                             f"  its hash with these router settings: {dsn_hash(routed, settings)}\n"
                             f"  hash stored with the session:        {stored}\n")
            return 1
        delta = make_delta(_body(routed), _body(dsn))
        delta_file.write_text(DELTA_HEAD.format(stamp=stamp.name, digest=stored) + delta, encoding="utf-8")
        gone, new = delta_counts(delta)
        print(f"route: {delta_file} written: {gone} lines of the routed DSN are gone, {new} are new")
        return 0

    delta_problem = ""
    if not args.force and ses.exists() and stamp.exists():
        stored = stamp.read_text().strip()
        if stored == digest:
            print(f"route: DSN unchanged, reusing {ses}")
            return 0
        if stamp_novalues.exists() and stamp_novalues.read_text().strip() == digest_novalues:
            print(f"route: the DSN differs from the routed one only in the value text of parts, reusing {ses}")
            return 0
        if delta_file.exists():
            delta = delta_file.read_text(encoding="utf-8")
            try:
                routed = undo_delta(_body(dsn), delta)
            except ValueError as e:
                delta_problem = f"{delta_file} does not fit this DSN: {e}"
            else:
                if stored == text_hash(routed, settings):
                    gone, new = delta_counts(delta)
                    print(f"route: the DSN differs from the routed one by {delta_file} and nothing else "
                          f"({gone} lines gone, {new} new), reusing {ses}")
                    return 0
                blind = text_hash(routed, settings, values=False)
                if stamp_novalues.exists() and stamp_novalues.read_text().strip() == blind:
                    gone, new = delta_counts(delta)
                    print(f"route: the DSN differs from the routed one by {delta_file} and the value text of parts "
                          f"({gone} lines gone, {new} new), reusing {ses}")
                    return 0
                delta_problem = f"{delta_file} fits this DSN, but taking it back does not give the routed file"
    if delta_problem:
        sys.stderr.write(f"route: {delta_problem}\n")
    if args.stored_only:
        stored = stamp.read_text().strip() if stamp.exists() else "none"
        sys.stderr.write(f"route: the session stored in {route_dir} was not made from the board these sources give.\n"
                         f"  hash of this DSN and router settings: {digest}\n"
                         f"  hash stored with the session:         {stored}\n"
                         "Run make in the board directory on a machine with Java and commit route/ and output/.\n")
        return 1

    jar = fetch_jar(Path(args.cache))
    out = project / "build" / f"{name}.ses"
    log = project / "build" / "freerouting.log"
    config_home = (project / "build" / "freerouting-home").resolve()
    write_settings(config_home, router)
    env = dict(os.environ)
    for var in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"):
        env[var] = str(config_home)
    # No way out: any HTTP or HTTPS request goes to a closed port on this machine.
    blocked = [f"-D{scheme}.proxy{key}={value}" for scheme in ("http", "https")
               for key, value in (("Host", "127.0.0.1"), ("Port", "9"))]
    cmd = ["java", "-Djava.awt.headless=true", *blocked, "-jar", str(jar), "--gui.enabled=false",
           "-de", str(dsn), "-do", str(out), f"--router.autorouter.max_passes={args.passes}"]
    t0 = time.time()
    with open(log, "w") as lg:
        r = subprocess.run(cmd, stdout=lg, stderr=subprocess.STDOUT, timeout=args.timeout, env=env)
    if r.returncode != 0 or not out.exists():
        sys.stderr.write(f"Freerouting failed, see {log}\n")
        return 1
    stats = parse_log(log.read_text(errors="replace"))
    stats["freerouting_version"] = VERSION
    stats["wall_seconds"] = round(time.time() - t0, 1)
    shutil.copyfile(out, ses)
    delta_file.unlink(missing_ok=True)          # the new session was routed from this very DSN
    stamp.write_text(digest + "\n")
    stamp_novalues.write_text(digest_novalues + "\n")
    stats_file.write_text(json.dumps(stats, indent=1, sort_keys=True) + "\n")
    print(f"route: Freerouting {VERSION} finished in {stats['wall_seconds']} s: {stats}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
