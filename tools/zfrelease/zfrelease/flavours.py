"""The flavour list, from flavours.toml."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path

DEFAULT_PATH = Path(__file__).resolve().parent.parent / "flavours.toml"


@dataclass(frozen=True)
class Flavour:
    id: str
    enabled: bool
    name: str
    description: str
    chip: str
    project_name: str
    improv: bool
    next_step: str


def load(path: Path = DEFAULT_PATH) -> dict[str, Flavour]:
    data = tomllib.loads(path.read_text())
    out: dict[str, Flavour] = {}
    for fid, t in data.items():
        out[fid] = Flavour(
            id=fid,
            enabled=bool(t["enabled"]),
            name=t["name"],
            description=t["description"],
            chip=t["chip"],
            project_name=t.get("project_name", ""),
            improv=bool(t["improv"]),
            next_step=t["next_step"],
        )
    return out


def get(fid: str, path: Path = DEFAULT_PATH) -> Flavour:
    flavours = load(path)
    if fid not in flavours:
        raise KeyError(f"no flavour {fid!r} in {path}; known: {', '.join(flavours)}")
    return flavours[fid]
