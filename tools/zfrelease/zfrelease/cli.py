"""zfrelease: package, assemble, verify and stage ZoneFlow firmware releases."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from . import flavours, package, release, verify, version


def _version_for(args: argparse.Namespace) -> str:
    if args.version:
        return args.version
    if args.tag:
        return version.from_tag(args.tag)
    return version.dev_version(args.commit)


def cmd_version(args: argparse.Namespace) -> int:
    """Print the firmware version for a tag or commit; with --github-output also set outputs."""
    if args.tag:
        v = version.from_tag(args.tag)
        tag = args.tag.removeprefix("refs/tags/")
    else:
        v, tag = version.dev_version(args.commit), ""
    print(v)
    out = os.environ.get("GITHUB_OUTPUT")
    if args.github_output and out:
        with open(out, "a") as f:
            f.write(f"version={v}\ntag={tag}\nprerelease={'true' if version.is_prerelease(v) else 'false'}\n")
    return 0


def cmd_package(args: argparse.Namespace) -> int:
    frag = package.package_standalone(
        build_dir=Path(args.build_dir),
        partitions_csv=Path(args.partitions),
        version=_version_for(args),
        out_dir=Path(args.out),
        flavour_id=args.flavour,
        commit=args.commit,
    )
    print(f"packaged {frag['id']}: {', '.join(sorted(frag['files']))}")
    return 0


def cmd_package_prebuilt(args: argparse.Namespace) -> int:
    frag = package.package_prebuilt(
        flavour_id=args.flavour,
        factory=Path(args.factory),
        app=Path(args.app),
        version=_version_for(args),
        out_dir=Path(args.out),
        commit=args.commit,
    )
    print(f"packaged {frag['id']}: {', '.join(sorted(frag['files']))}")
    return 0


def cmd_assemble(args: argparse.Namespace) -> int:
    v = _version_for(args)
    rel = release.assemble(
        directory=Path(args.dir),
        version=v,
        tag=args.tag.removeprefix("refs/tags/") if args.tag else "",
        commit=args.commit,
        prerelease=version.is_prerelease(v),
    )
    print(f"assembled release {rel['version']} with flavours: {', '.join(f['id'] for f in rel['flavours'])}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    verify.verify_directory(Path(args.dir), Path(args.partitions))
    print("all checks passed")
    return 0


def cmd_stage_site(args: argparse.Namespace) -> int:
    dest = release.stage_site(Path(args.release_dir), Path(args.site))
    rel = json.loads((dest / release.RELEASE_JSON).read_text())
    print(f"staged release {rel['version']} into {dest}")
    return 0


def cmd_notes(args: argparse.Namespace) -> int:
    print(release.notes(Path(args.dir)), end="")
    return 0


def cmd_flavours(args: argparse.Namespace) -> int:
    for f in flavours.load().values():
        print(f"{f.id}\t{'enabled' if f.enabled else 'disabled'}\t{f.name}")
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="zfrelease", description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    def ver_args(sp: argparse.ArgumentParser) -> None:
        sp.add_argument("--tag", help="release tag, e.g. v0.1.0 or refs/tags/v0.1.0 (sets the version)")
        sp.add_argument("--version", help="explicit version (tests only; releases take it from the tag)")
        sp.add_argument("--commit", default="", help="commit SHA; names development versions")

    sp = sub.add_parser("version", help="print the firmware version for a tag or commit")
    sp.add_argument("--tag")
    sp.add_argument("--commit", default="")
    sp.add_argument("--github-output", action="store_true")
    sp.set_defaults(fn=cmd_version)

    sp = sub.add_parser("package", help="package the standalone firmware from an ESP-IDF build")
    ver_args(sp)
    sp.add_argument("--build-dir", required=True)
    sp.add_argument("--partitions", required=True)
    sp.add_argument("--out", required=True)
    sp.add_argument("--flavour", default="standalone")
    sp.set_defaults(fn=cmd_package)

    sp = sub.add_parser("package-prebuilt", help="package a flavour that has its own merged image (ESPHome)")
    ver_args(sp)
    sp.add_argument("--flavour", required=True)
    sp.add_argument("--factory", required=True)
    sp.add_argument("--app", required=True)
    sp.add_argument("--out", required=True)
    sp.set_defaults(fn=cmd_package_prebuilt)

    sp = sub.add_parser("assemble", help="write release.json and SHA256SUMS from the flavour fragments")
    ver_args(sp)
    sp.add_argument("dir")
    sp.set_defaults(fn=cmd_assemble)

    sp = sub.add_parser("verify", help="dry-run checks of an assembled release directory")
    sp.add_argument("dir")
    sp.add_argument("--partitions", required=True)
    sp.set_defaults(fn=cmd_verify)

    sp = sub.add_parser("stage-site", help="copy a downloaded release into the built docs site")
    sp.add_argument("release_dir")
    sp.add_argument("site")
    sp.set_defaults(fn=cmd_stage_site)

    sp = sub.add_parser("notes", help="print the release notes for an assembled directory")
    sp.add_argument("dir")
    sp.set_defaults(fn=cmd_notes)

    sp = sub.add_parser("flavours", help="list the flavours")
    sp.set_defaults(fn=cmd_flavours)

    args = p.parse_args(argv)
    try:
        return args.fn(args)
    except (
        package.PackageError,
        release.ReleaseError,
        verify.VerifyError,
        version.VersionError,
        KeyError,
        FileNotFoundError,
    ) as e:
        print(f"zfrelease: {e}", file=sys.stderr)
        return 1
    except ValueError as e:  # image, partition and manifest errors
        print(f"zfrelease: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
