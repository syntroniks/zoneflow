"""Firmware version strings. The released version comes from the git tag, nowhere else."""

from __future__ import annotations

import re

# esp_app_desc_t.version is 32 bytes including the NUL.
MAX_LEN = 31
_TAG = re.compile(r"^v(\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*)?)$")


class VersionError(ValueError):
    pass


def from_tag(ref: str) -> str:
    """``v1.2.3`` or ``refs/tags/v1.2.3-rc.1`` -> ``1.2.3`` / ``1.2.3-rc.1``."""
    tag = ref.removeprefix("refs/tags/")
    m = _TAG.match(tag)
    if not m:
        raise VersionError(
            f"{tag!r} is not a release tag: expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease"
        )
    version = m.group(1)
    if len(version) > MAX_LEN:
        raise VersionError(f"version {version!r} is {len(version)} characters; the firmware holds at most {MAX_LEN}")
    return version


def dev_version(commit: str) -> str:
    """Version of a build that is not a release (pull request, manual run on a branch)."""
    short = re.sub(r"[^0-9a-f]", "", commit.lower())[:7]
    if not short:
        raise VersionError("need a commit hash for a development version")
    return f"0.0.0-dev.{short}"


def is_prerelease(version: str) -> bool:
    return "-" in version
