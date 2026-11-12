# SPDX-License-Identifier: Apache-2.0
"""Read a PNG with the standard library, and find the board in KiCad's top render.

The orientation picture puts numbered boxes over ``zoneflow-a-top.png``. For that
the place of the board in the picture has to be known. It is measured: the
background of the render is one flat colour, and the board is everything else.
"""

from __future__ import annotations

import os
import struct
import zlib


def read(path):
    """An 8-bit RGB or RGBA PNG as (width, height, channels, rows of bytes)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos, idat, head = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + length
    width, height, depth, colour, _, _, interlace = head
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB or RGBA without interlace is read")
    n = 3 if colour == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * n
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(n, stride):
                line[i] = (line[i] + line[i - n]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - n] if i >= n else 0) + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - n] if i >= n else 0
                b = prev[i]
                c = prev[i - n] if i >= n else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, n, rows


_edges = {}


def find_edges(path):
    """Where the board outline is in a top render: pixel positions of its four edges.

    Parts overhang the top and bottom edges (the module, the terminal headers), so
    the top edge is where most columns begin and the bottom edge where the shortest
    columns end.
    """
    stat = os.stat(path)
    key = (str(path), stat.st_mtime_ns, stat.st_size)
    if key not in _edges:
        _edges[key] = _find_edges(path)
    return dict(_edges[key])


def find_board(path, board_w, board_h, tolerance=0.008):
    """The same, and the result must have the proportions of the board, or this fails."""
    edges = find_edges(path)
    kx = (edges["right"] - edges["left"]) / board_w
    ky = (edges["bottom"] - edges["top"]) / board_h
    if abs(kx / ky - 1) > tolerance:
        raise ValueError(f"{path}: the board found is {edges['right'] - edges['left']} x {edges['bottom'] - edges['top']} "
                         f"pixels, which is not {board_w} x {board_h} mm at one scale")
    return edges


def _find_edges(path):
    width, height, n, rows = read(path)
    bg = bytes(rows[0][:3])

    def is_board(x, y):
        o = x * n
        return max(abs(rows[y][o] - bg[0]), abs(rows[y][o + 1] - bg[1]), abs(rows[y][o + 2] - bg[2])) > 14

    tops, bots, cols = {}, {}, []
    for x in range(width):
        ys = [y for y in range(height) if is_board(x, y)]
        if len(ys) > height * 0.3:
            cols.append(x)
            tops[x], bots[x] = ys[0], ys[-1]
    if not cols:
        raise ValueError(f"{path}: no board found against the background")
    left, right = cols[0], cols[-1] + 1
    inner = [x for x in cols if left + (right - left) * 0.03 < x < right - (right - left) * 0.03]
    top = sorted(tops[x] for x in inner)[int(len(inner) * 0.95)]
    bottom = sorted(bots[x] for x in inner)[int(len(inner) * 0.05)] + 1
    return {"width": width, "height": height, "left": left, "right": right, "top": top, "bottom": bottom}
