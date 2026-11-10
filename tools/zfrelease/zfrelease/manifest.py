"""ESP Web Tools manifests.

The schema in ``schemas/esp-web-tools-manifest.schema.json`` is written from the
``Manifest`` and ``Build`` interfaces in esp-web-tools 10.4.0 ``src/const.ts``
(the version the installer page vendors). ESP Web Tools publishes no JSON Schema.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

CHIP_FAMILY = {"esp32s3": "ESP32-S3"}


class ManifestError(ValueError):
    pass


def schema_path() -> Path:
    here = Path(__file__).resolve().parent.parent / "schemas" / "esp-web-tools-manifest.schema.json"
    if here.exists():
        return here
    raise ManifestError("schema file not found next to the package")


def load_schema() -> dict[str, Any]:
    return json.loads(schema_path().read_text())


def build(name: str, version: str, chip: str, parts: list[tuple[str, int]], improv: bool) -> dict[str, Any]:
    manifest: dict[str, Any] = {"name": name, "version": version}
    if not improv:
        # 0 turns the Improv probe off. The firmware does not speak it, so the
        # probe would only delay the end of the install by a few seconds.
        manifest["new_install_improv_wait_time"] = 0
    manifest["builds"] = [
        {"chipFamily": CHIP_FAMILY[chip], "parts": [{"path": path, "offset": offset} for path, offset in parts]}
    ]
    return manifest


def validate(manifest: Any, directory: Path | None = None) -> None:
    """Check against the schema and, with a directory, that every part file exists."""
    import jsonschema

    try:
        jsonschema.validate(manifest, load_schema())
    except jsonschema.ValidationError as e:
        where = "/".join(str(p) for p in e.absolute_path) or "(root)"
        raise ManifestError(f"manifest invalid at {where}: {e.message}") from e
    if directory is not None:
        for b in manifest["builds"]:
            for part in b["parts"]:
                if not (directory / part["path"]).is_file():
                    raise ManifestError(f"manifest part {part['path']!r} is not in {directory}")
