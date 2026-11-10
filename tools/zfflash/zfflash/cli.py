"""zfflash command line."""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
from collections.abc import Callable
from pathlib import Path

from . import ports as ports_mod
from .device import Device, DeviceError, NeedsLogin
from .flasher import EsptoolFlasher, Flasher, FlashError
from .http import Http, NetworkError, UrllibHttp
from .release import DEFAULT_SOURCE, Release, ReleaseError, open_release, release_source

DEFAULT_BAUD = 460800

WARNING_FACTORY = (
    "Flashing the factory image replaces everything on the board: configuration, schedules, WiFi credentials,\n"
    "the admin password and the setup code (the board makes a new one). Back up first if you want the schedules."
)


class Env:
    """Everything with a side effect outside this process, so tests can replace it."""

    def __init__(
        self,
        http: Http | None = None,
        flasher: Flasher | None = None,
        lister: Callable[[], list[ports_mod.Port]] = ports_mod.system_ports,
        ask: Callable[[str], str] = input,
        askpass: Callable[[str], str] = getpass.getpass,
        out=None,
        err=None,
        interactive: bool | None = None,
    ):
        self.http = http or UrllibHttp()
        self.flasher = flasher or EsptoolFlasher()
        self.lister = lister
        self.ask = ask
        self.askpass = askpass
        self.out = out or sys.stdout
        self.err = err or sys.stderr
        self.interactive = sys.stdin.isatty() if interactive is None else interactive

    def say(self, text: str = "") -> None:
        print(text, file=self.out)


class UsageError(RuntimeError):
    pass


def _release(args: argparse.Namespace, env: Env) -> Release:
    if args.source and args.release:
        raise UsageError("give --source or --release, not both")
    source = args.source or (release_source(args.release) if args.release else DEFAULT_SOURCE)
    rel = open_release(
        source, env.http, Path(args.cache_dir) if args.cache_dir else None, getattr(args, "sums_sha256", None)
    )
    kind = "checked against its SHA256SUMS" if args.sums_sha256 is None else "SHA256SUMS pinned with --sums-sha256"
    env.say(f"Release {rel.version} from {source} ({kind})")
    if rel.info.get("prerelease"):
        env.say("This is a pre-release.")
    return rel


def _confirm(env: Env, args: argparse.Namespace, question: str) -> None:
    if args.yes:
        return
    if not env.interactive:
        raise UsageError("this changes the device; pass --yes to confirm when not at a terminal")
    if env.ask(f"{question} [y/N] ").strip().lower() not in ("y", "yes"):
        raise UsageError("cancelled")


# ---- commands ---------------------------------------------------------------


def cmd_ports(args, env: Env) -> int:
    ports = env.lister()
    cands = {p.device for p in ports_mod.candidates(ports)}
    if not ports:
        env.say("no serial ports")
    for p in ports:
        env.say(f"{'*' if p.device in cands else ' '} {p}")
    env.say("* = looks like a ZoneFlow board")
    return 0


def cmd_fetch(args, env: Env) -> int:
    rel = _release(args, env)
    fl = rel.flavour(args.flavour)
    for kind, meta in fl["images"].items():
        path = rel.fetch(meta["file"])
        env.say(f"ok  {kind:16} {meta['file']}  sha256 {meta['sha256'][:16]}...  -> {path}")
    return 0


def _partition(fl: dict, name: str) -> dict:
    for p in fl.get("partitions", []):
        if p["name"] == name:
            return p
    raise UsageError(f"the release has no {name!r} partition entry")


def cmd_flash(args, env: Env) -> int:
    rel = _release(args, env)
    fl = rel.flavour(args.flavour)
    chip = fl["chip"]
    wanted = list(dict.fromkeys(args.images))
    for kind in wanted:
        if kind not in fl["images"]:
            raise UsageError(f"flavour {fl['id']} has no {kind!r} image")
        if kind != "factory" and "partitions" not in fl:
            raise UsageError(
                f"the {fl['id']} flavour only supports 'flash factory' (its partition layout is not published)"
            )
    if "factory" in wanted and len(wanted) > 1:
        raise UsageError("the factory image already contains the application and web UI; flash it alone")
    files = {kind: rel.fetch(fl["images"][kind]["file"]) for kind in wanted}  # checks every SHA-256 first
    port = ports_mod.choose(args.port, env.lister)
    env.say(f"Port {port}, chip {chip}, baud {args.baud}")
    if "factory" in wanted:
        env.say(WARNING_FACTORY)
    env.say("Flash with the valves unpowered: unplug 24 VAC and the valve wires. USB alone powers the logic.")
    _confirm(env, args, f"Flash {', '.join(wanted)} of {rel.version} to {port}?")
    for kind in wanted:
        meta = fl["images"][kind]
        if kind == "app":
            # The bootloader picks the slot named in otadata. Blank otadata, then write ota_0, so that the
            # image just written is the one that boots whatever slot the board ran from before.
            od = _partition(fl, "otadata")
            env.say(f"Erasing otadata (0x{od['offset']:x}) so slot ota_0 boots")
            env.flasher.erase_region(port, args.baud, chip, od["offset"], od["size"])
        env.say(f"Writing {kind} ({meta['file']}) at 0x{meta['offset']:x}")
        env.flasher.write(port, args.baud, chip, [(meta["offset"], files[kind])])
    env.say("Done. The board resets itself; if it does not, press the reset button or replug USB.")
    if "app" in wanted:
        env.say("Written over USB, the update has no rollback: the board will not return to the old slot on its own.")
    return 0


def cmd_erase_nvs(args, env: Env) -> int:
    rel = _release(args, env)
    fl = rel.flavour(args.flavour)
    nvs = _partition(fl, "nvs")
    port = ports_mod.choose(args.port, env.lister)
    env.say(
        f"Erasing the nvs partition (0x{nvs['offset']:x}, {nvs['size']} bytes) on {port}.\n"
        "This deletes the configuration, schedules, WiFi credentials, admin password and the setup code.\n"
        "It is a factory reset done from the outside; the firmware and web UI stay. The board makes a\n"
        "new setup code and starts WiFi setup again."
    )
    _confirm(env, args, f"Erase NVS on {port}?")
    env.flasher.erase_region(port, args.baud, fl["chip"], nvs["offset"], nvs["size"])
    env.say("Done. Reset the board (button or replug USB).")
    return 0


def _device(args, env: Env, *, needs_login: bool) -> Device:
    dev = Device.at(args.host, env.http)
    password = os.environ.get("ZF_PASSWORD")
    if password is None and needs_login:
        if not env.interactive:
            raise UsageError("this needs the admin password: set ZF_PASSWORD (it is never taken from a flag)")
        password = env.askpass("Admin password: ")
    if password is not None:
        dev.login(password)
    return dev


def cmd_backup(args, env: Env) -> int:
    dev = _device(args, env, needs_login=False)
    try:
        body = dev.backup()
    except NeedsLogin:
        if not env.interactive:
            raise
        dev.login(env.askpass("Admin password: "))
        body = dev.backup()
    out = Path(args.out)
    if out.exists() and not args.force:
        raise UsageError(f"{out} exists; use --force to overwrite")
    out.write_bytes(body)
    doc = json.loads(body)
    env.say(f"Saved {out}: configuration of {doc['hostname']}, firmware {doc['firmware_version']}.")
    env.say("The backup holds schedules and settings. It never contains WiFi credentials, the admin password,")
    env.say("MQTT password or the setup code: set those up again on a new or reset board.")
    return 0


def cmd_restore(args, env: Env) -> int:
    path = Path(args.file)
    try:
        body = path.read_bytes()
        doc = json.loads(body)
    except (OSError, ValueError) as e:
        raise UsageError(f"cannot read {path}: {e}") from e
    if doc.get("format") != "zoneflow-backup":
        raise UsageError(f"{path} is not a ZoneFlow backup (format is {doc.get('format')!r})")
    if doc.get("format_version", 1) != 1:
        raise UsageError(f"backup format_version {doc.get('format_version')} is not supported by this zfflash")
    dev = _device(args, env, needs_login=True)
    _confirm(env, args, f"Replace the configuration of {args.host} with {path.name}?")
    dev.restore(body)
    env.say("Restored. The controller applied the configuration.")
    return 0


def cmd_ota(args, env: Env) -> int:
    rel = _release(args, env)
    fl = rel.flavour(args.flavour)
    if args.image not in ("app", "www") or args.image not in fl["images"]:
        raise UsageError(f"flavour {fl['id']} has no {args.image!r} image to upload")
    data = rel.fetch(fl["images"][args.image]["file"]).read_bytes()
    dev = _device(args, env, needs_login=True)
    _confirm(
        env, args, f"Upload {args.image} {rel.version} to {args.host}? Watering stops while the controller updates."
    )
    dev.upload(args.image, data, force=args.force)
    if args.image == "app":
        env.say("Accepted. The controller restarts into the new image and rolls back by itself if the self-test fails.")
        env.say("Sign in again afterwards: a restart ends every session.")
    else:
        env.say("Accepted. The new web UI is served at once.")
    return 0


# ---- argument parsing -------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="zfflash",
        description="Flash, update, factory-reset, back up and restore a ZoneFlow controller.",
        epilog="Run it with `uv run zfflash` from tools/zfflash. Release files are checked against the published "
        "SHA256SUMS before anything is written.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp: argparse.ArgumentParser, release: bool = True) -> None:
        if release:
            sp.add_argument("--source", help=f"release directory or URL (default {DEFAULT_SOURCE})")
            sp.add_argument("--release", metavar="TAG", help="a GitHub release tag, e.g. v0.1.0, instead of the latest")
            sp.add_argument(
                "--sums-sha256", metavar="HEX", help="expected SHA-256 of SHA256SUMS, from the release notes"
            )
            sp.add_argument("--cache-dir", help="where downloads are kept (default ~/.cache/zfflash)")
            sp.add_argument("--flavour", default="standalone")

    def serial(sp: argparse.ArgumentParser) -> None:
        sp.add_argument("--port", help="serial port; found automatically when one board is plugged in")
        sp.add_argument("--baud", type=int, default=DEFAULT_BAUD)
        sp.add_argument("-y", "--yes", action="store_true", help="do not ask for confirmation")

    sp = sub.add_parser("ports", help="list serial ports and mark the ones that look like a board")
    sp.set_defaults(fn=cmd_ports)

    sp = sub.add_parser("fetch", help="download a release and verify every file; flash nothing")
    common(sp)
    sp.set_defaults(fn=cmd_fetch)

    sp = sub.add_parser("flash", help="write images over USB")
    common(sp)
    serial(sp)
    sp.add_argument(
        "images",
        nargs="+",
        choices=["factory", "app", "www"],
        help="factory: whole flash of a blank board. app, www: update only that part and keep the settings",
    )
    sp.set_defaults(fn=cmd_flash)

    sp = sub.add_parser("erase-nvs", help="erase the settings partition over USB (factory reset)")
    common(sp)
    serial(sp)
    sp.set_defaults(fn=cmd_erase_nvs)

    sp = sub.add_parser("backup", help="save the configuration through the REST API")
    sp.add_argument("--host", required=True, help="zoneflow-xxxx.local, an IP address or a full URL")
    sp.add_argument("--out", required=True)
    sp.add_argument("--force", action="store_true", help="overwrite the output file")
    sp.set_defaults(fn=cmd_backup)

    sp = sub.add_parser("restore", help="load a backup through the REST API (admin password needed)")
    sp.add_argument("--host", required=True)
    sp.add_argument("--file", required=True)
    sp.add_argument("-y", "--yes", action="store_true")
    sp.set_defaults(fn=cmd_restore)

    sp = sub.add_parser("ota", help="upload an image to the controller's REST API (admin password needed)")
    common(sp)
    sp.add_argument("--host", required=True)
    sp.add_argument("--image", choices=["app", "www"], default="app")
    sp.add_argument("--force", action="store_true", help="update even while a zone runs (outputs are forced off)")
    sp.add_argument("-y", "--yes", action="store_true")
    sp.set_defaults(fn=cmd_ota)
    return p


def main(argv: list[str] | None = None, env: Env | None = None) -> int:
    env = env or Env()
    args = build_parser().parse_args(argv)
    try:
        return args.fn(args, env)
    except (UsageError, ReleaseError, ports_mod.PortError, FlashError, DeviceError, NetworkError) as e:
        print(f"zfflash: {e}", file=env.err)
        return 1


if __name__ == "__main__":
    sys.exit(main())
