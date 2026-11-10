# SPDX-License-Identifier: Apache-2.0
"""Minimal S-expression reader and writer for KiCad files.

Atoms are kept as text so that a value read from a KiCad library is written
back byte-for-byte. A quoted string is a plain ``str``; a bare atom (keyword
or number) is a ``Sym``.
"""

from __future__ import annotations

from typing import Iterator


class Sym(str):
    """A bare (unquoted) atom: a keyword such as ``symbol`` or a number."""

    __slots__ = ()

    def __repr__(self) -> str:
        return f"Sym({str.__repr__(self)})"


Node = "list | str | Sym"

_ESCAPES = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}


def _tokens(text: str) -> Iterator[tuple[str, str]]:
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == "(":
            yield "(", c
            i += 1
        elif c == ")":
            yield ")", c
            i += 1
        elif c == '"':
            i += 1
            out = []
            while True:
                c = text[i]
                if c == "\\":
                    nxt = text[i + 1]
                    out.append(_ESCAPES.get(nxt, "\\" + nxt))
                    i += 2
                elif c == '"':
                    i += 1
                    break
                else:
                    out.append(c)
                    i += 1
            yield "str", "".join(out)
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            yield "sym", text[i:j]
            i = j


def loads(text: str):
    """Parse one S-expression and return it as nested lists."""
    stack: list[list] = [[]]
    for kind, value in _tokens(text):
        if kind == "(":
            stack.append([])
        elif kind == ")":
            done = stack.pop()
            if not stack:
                raise ValueError("unbalanced ')'")
            stack[-1].append(done)
        elif kind == "str":
            stack[-1].append(value)
        else:
            stack[-1].append(Sym(value))
    if len(stack) != 1 or len(stack[0]) != 1:
        raise ValueError("expected exactly one top-level expression")
    return stack[0][0]


def quote(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{s}"'


def fmt(x: float) -> Sym:
    """Format a coordinate the way KiCad does: at most 4 decimals, no trailing zeros."""
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    if s in ("-0", ""):
        s = "0"
    return Sym(s)


def _atom(a) -> str:
    if isinstance(a, Sym):
        return str(a)
    if isinstance(a, str):
        return quote(a)
    if isinstance(a, bool):
        return "yes" if a else "no"
    if isinstance(a, int):
        return str(a)
    if isinstance(a, float):
        return str(fmt(a))
    raise TypeError(f"cannot serialise {a!r}")


def dumps(node, indent: int = 0) -> str:
    """Serialise nested lists. A list with no sub-lists stays on one line."""
    pad = "\t" * indent
    if not isinstance(node, list):
        return pad + _atom(node)
    if not any(isinstance(c, list) for c in node):
        return pad + "(" + " ".join(_atom(c) for c in node) + ")"
    head = []
    rest = list(node)
    while rest and not isinstance(rest[0], list):
        head.append(_atom(rest.pop(0)))
    lines = [pad + "(" + " ".join(head)]
    for child in rest:
        lines.append(dumps(child, indent + 1))
    lines.append(pad + ")")
    return "\n".join(lines)


def find(node: list, key: str):
    """First child list whose head is ``key``, or None."""
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def find_all(node: list, key: str) -> list:
    return [c for c in node if isinstance(c, list) and c and c[0] == key]
