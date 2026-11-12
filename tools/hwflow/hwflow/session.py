# SPDX-License-Identifier: Apache-2.0
"""Changes to a Specctra session before KiCad imports it.

The stored session names the nets and the parts of the board it was routed
for. A board that has lost parts since (``route/<name>.dsn.delta``, see
``hwflow.route``) may have lost nets with them: where a series resistor went,
the two nets on its pads are one net now, under one of the two names. KiCad
10.0.8 imports the copper of a net it does not know all the same and gives it
the net of whatever pad it touches. That is a guess this flow does not build
on. ``[patches.renamed]`` of board.toml says which net the copper of each lost
net belongs to, and a net of the session that is neither on the board nor in
that table stops the build.

Only the standard library is used.
"""

from __future__ import annotations

import re

NET = re.compile(r'^(\s*\(net )("[^"]*"|[^\s()"]+)\s*$', re.M)
PLACE = re.compile(r'^\s*\(place ("[^"]*"|[^\s()"]+) [^\n]*\)\s*\n', re.M)


class SessionError(Exception):
    pass


def _name(token: str) -> str:
    return token[1:-1] if token.startswith('"') else token


def _token(name: str) -> str:
    return name if re.fullmatch(r'[^\s()"]+', name) and "/" not in name else f'"{name}"'


def nets(text: str) -> list[str]:
    """The nets that have copper in a session, in file order."""
    return [_name(m.group(2)) for m in NET.finditer(text)]


def translate(text: str, board_nets: set[str], board_refs: set[str], renamed: dict[str, str]) -> tuple[str, dict]:
    """The session for a board that has lost parts since it was routed.

    The copper of each net in ``renamed`` gets the name the board has for it
    now. The placements of parts the board no longer has are left out: KiCad
    would skip them with a warning. Raises SessionError for a net of the
    session that the board does not have and ``renamed`` does not name, for a
    rename to a net that is not on the board, and for a rename of a net that
    is still there.
    """
    in_session = set(nets(text))
    stale = sorted(old for old in renamed if old in board_nets)
    if stale:
        raise SessionError("[patches.renamed] names nets that are still on the board: " + ", ".join(stale))
    unused = sorted(old for old in renamed if old not in in_session)
    if unused:
        raise SessionError("[patches.renamed] names nets the stored session has no copper of: " + ", ".join(unused))
    nowhere = sorted({new for new in renamed.values() if new not in board_nets})
    if nowhere:
        raise SessionError("[patches.renamed] renames to nets that are not on the board: " + ", ".join(nowhere))
    unknown = sorted(n for n in in_session if n not in board_nets and n not in renamed)
    if unknown:
        raise SessionError("the stored session has copper of nets that are not on the board: " + ", ".join(unknown)
                           + ". If parts were taken off since it was routed, say in [patches.renamed] of "
                             "board.toml which net each one's copper belongs to now")

    def swap(m: re.Match) -> str:
        name = _name(m.group(2))
        return m.group(1) + (_token(renamed[name]) if name in renamed else m.group(2))

    out = NET.sub(swap, text)
    dropped: list[str] = []

    def place(m: re.Match) -> str:
        ref = _name(m.group(1))
        if ref in board_refs:
            return m.group(0)
        dropped.append(ref)
        return ""

    out = PLACE.sub(place, out)
    return out, {"renamed": {old: new for old, new in renamed.items()}, "placements_left_out": sorted(dropped)}
