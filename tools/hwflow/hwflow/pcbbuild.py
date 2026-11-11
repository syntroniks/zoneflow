# SPDX-License-Identifier: Apache-2.0
"""Build the board with KiCad's ``pcbnew`` Python module.

Run with the system interpreter that ships KiCad's bindings
(``/usr/bin/python3 -m hwflow.pcbbuild ...``); only the standard library and
``pcbnew`` are used. The SWIG ``pcbnew`` module works without a display in
KiCad 10.0.8, so no ``xvfb-run`` is needed for these steps.

Sub-commands:
  build   netlist + board.toml -> placed, pre-routed, unfilled board and a Specctra DSN
  finish  import the autorouter's session, remove dangling stubs, save
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import tomllib
import uuid
from pathlib import Path

import pcbnew

from . import dsn as dsn_mod
from . import netlist as netlist_mod

FOOTPRINT_DIR = os.environ.get("KICAD10_FOOTPRINT_DIR", "/usr/share/kicad/footprints")


_NS = uuid.UUID("6f1d3c0e-5a0b-4a52-9a3c-2f8f9a7e0002")


class BoardError(Exception):
    pass


def set_uuid(item, *key) -> None:
    """Give a board item a UUID derived from ``key``.

    KiCad assigns random UUIDs and writes board items sorted by UUID, so
    without this two builds of the same source give two very different files.
    """
    item.SetUuid(pcbnew.KIID(str(uuid.uuid5(_NS, "/".join(str(k) for k in key)))))


def stamp_footprint(fp, key: str) -> None:
    set_uuid(fp, "fp", key)
    for kind, items in (("pad", fp.Pads()), ("gr", fp.GraphicalItems()), ("field", fp.GetFields()),
                        ("zone", fp.Zones()), ("group", fp.Groups())):
        for i, item in enumerate(items):
            set_uuid(item, "fp", key, kind, i)


def stamp_points(pcb_path: Path) -> None:
    """Deterministic UUIDs for footprint ``point`` items, done on the saved file.

    Points are new in KiCad 10 and ``FOOTPRINT.Points()`` returns objects the
    Python wrapper cannot use, so their UUIDs cannot be set through the API.
    """
    import re

    out = []
    fp_uuid = None
    want_fp_uuid = in_point = False
    n = 0
    for line in pcb_path.read_text(encoding="utf-8").split("\n"):
        if line.startswith("\t(footprint "):
            want_fp_uuid, n = True, 0
        elif want_fp_uuid and (m := re.match(r'^\t\t\(uuid "([^"]+)"\)$', line)):
            fp_uuid, want_fp_uuid = m.group(1), False
        elif line == "\t\t(point":
            in_point = True
        elif in_point and re.match(r'^\t\t\t\(uuid "[^"]+"\)$', line):
            line = f'\t\t\t(uuid "{uuid.uuid5(_NS, f"point/{fp_uuid}/{n}")}")'
            in_point = False
            n += 1
        out.append(line)
    pcb_path.write_text("\n".join(out), encoding="utf-8")


def stamp_copper(board) -> None:
    """Deterministic UUIDs for every track and via, keyed by what the item is."""
    seen: dict = {}
    keyed = []
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            k = ("via", t.GetNetname(), t.GetPosition().x, t.GetPosition().y, t.GetWidth(pcbnew.F_Cu))
        else:
            a, b = sorted([(t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)])
            k = ("seg", t.GetNetname(), t.GetLayer(), a, b, t.GetWidth())
        keyed.append((k, t))
    # Two passes. An item may be given the UUID another item still has from an earlier stamping,
    # which KiCad's debug build reports as an assertion. Park them all on unused values first.
    for i, (_, t) in enumerate(keyed):
        set_uuid(t, "parked", i)
    for k, t in keyed:
        n = seen.get(k, 0)
        seen[k] = n + 1
        set_uuid(t, *k, n)


def mm(v: float) -> int:
    return pcbnew.FromMM(float(v))


class Builder:
    def __init__(self, project_dir: Path, design_dir: Path, fab_rules: Path | None = None):
        self.project_dir = project_dir
        self.fab = tomllib.loads(fab_rules.read_text(encoding="utf-8")) if fab_rules else {}
        self.cfg = tomllib.loads((design_dir / "board.toml").read_text(encoding="utf-8"))
        self.model = json.loads((project_dir / "build" / "model.json").read_text(encoding="utf-8"))
        self.name = self.model["name"]
        self.nl = netlist_mod.read(project_dir / "build" / f"{self.name}.net")
        self.pcb_path = project_dir / f"{self.name}.kicad_pcb"
        b = self.cfg["board"]
        self.ox, self.oy = b["origin"]
        self.w, self.h = b["size"]
        # y_up: board coordinates have their origin at the bottom-left corner and y pointing up,
        # the frame of a mechanical drawing. The default is KiCad's: top-left, y down.
        self.y_up = bool(b.get("y_up", False))
        self.fp_dirs = [project_dir / d for d in self.model.get("footprint_dirs", [])]
        self.board = None
        self.optional_texts: set[str] = set()     # UUIDs of [[text]] entries marked optional
        self.fps: dict[str, pcbnew.FOOTPRINT] = {}
        self.nets: dict[str, pcbnew.NETINFO_ITEM] = {}

    def layer(self, name: str) -> int:
        """Layer id for a name such as ``F.Cu`` or ``F.SilkS``.

        In KiCad 10 ``BOARD.GetLayerID`` and ``pcbnew.LayerName`` only know the
        names shown in the GUI ("F.Silkscreen") and give -1 for the file-format
        names ("F.SilkS"). An item saved on layer -1 makes the board unloadable,
        so accept both spellings and refuse anything else.
        """
        if not hasattr(self, "_layers"):
            self._layers = {
                "F.SilkS": pcbnew.F_SilkS, "B.SilkS": pcbnew.B_SilkS,
                "F.CrtYd": pcbnew.F_CrtYd, "B.CrtYd": pcbnew.B_CrtYd,
                "F.Adhes": pcbnew.F_Adhes, "B.Adhes": pcbnew.B_Adhes,
            }
            for i in range(pcbnew.PCB_LAYER_ID_COUNT):
                self._layers[str(self.board.GetLayerName(i))] = i
        if name not in self._layers:
            raise BoardError(f"unknown layer {name!r}")
        return self._layers[name]

    def pt(self, x: float, y: float) -> pcbnew.VECTOR2I:
        """Board coordinates to page coordinates."""
        return pcbnew.VECTOR2I(mm(self.ox + x), mm(self.oy + (self.h - y if self.y_up else y)))

    def offset(self, dx: float, dy: float) -> pcbnew.VECTOR2I:
        """An offset in board axes as a page vector."""
        return pcbnew.VECTOR2I(mm(dx), mm(-dy if self.y_up else dy))

    def rotate(self, dx: float, dy: float, deg: float) -> tuple[float, float]:
        """Rotate an offset in board axes the way KiCad rotates a footprint: counter-clockwise on screen."""
        a = math.radians(deg if self.y_up else -deg)
        return (dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a))

    def load_footprint(self, lib: str, name: str):
        """A footprint from a project-local library if the project has that library, else from KiCad's."""
        for d in self.fp_dirs:
            if (d / f"{lib}.pretty").is_dir():
                return pcbnew.FootprintLoad(str(d / f"{lib}.pretty"), name)
        return pcbnew.FootprintLoad(f"{FOOTPRINT_DIR}/{lib}.pretty", name)

    # ---- board, outline ---------------------------------------------------
    def new_board(self) -> None:
        if self.pcb_path.exists():
            self.pcb_path.unlink()
        # NewBoard loads <name>.kicad_pro beside the board: net classes and fab minimums.
        self.board = pcbnew.NewBoard(str(self.pcb_path))
        b = self.cfg["board"]
        self.board.SetCopperLayerCount(int(b.get("layers", 2)))
        ds = self.board.GetDesignSettings()
        ds.SetBoardThickness(mm(b.get("thickness", 1.6)))
        mask = self.fab.get("mask", {}).get("solder_mask_min_width")
        if mask is not None:
            ds.m_SolderMaskMinWidth = mm(mask)
        ds.SetAuxOrigin(pcbnew.VECTOR2I(mm(self.ox), mm(self.oy + self.h)))
        self._title_block()

    def _title_block(self) -> None:
        """Give the board the title block of the root schematic sheet.

        Tools that read a board take its date from here, and fall back to the
        file's modification time when it is empty, which changes on every build.
        """
        sch = self.project_dir / f"{self.name}.kicad_sch"
        if not sch.exists():
            return
        import re
        m = re.search(r"\(title_block\b(.*?)\n\t\)", sch.read_text(encoding="utf-8"), flags=re.S)
        if not m:
            return
        fields = dict(re.findall(r'\((title|date|rev|company) "([^"]*)"\)', m.group(1)))
        tb = self.board.GetTitleBlock()
        tb.SetTitle(fields.get("title", ""))
        tb.SetDate(fields.get("date", ""))
        tb.SetRevision(fields.get("rev", ""))
        tb.SetCompany(fields.get("company", ""))
        for n, text in re.findall(r'\(comment (\d) "([^"]*)"\)', m.group(1)):
            tb.SetComment(int(n) - 1, text)

    def _edge(self, shape) -> None:
        shape.SetLayer(pcbnew.Edge_Cuts)
        shape.SetWidth(mm(0.1))
        self._n_edge = getattr(self, "_n_edge", 0) + 1
        set_uuid(shape, "edge", self._n_edge)
        self.board.Add(shape)

    def outline(self) -> None:
        r = float(self.cfg["board"].get("corner_radius", 0))
        w, h = self.w, self.h
        corners = [(r, 0, w - r, 0), (w, r, w, h - r), (w - r, h, r, h), (0, h - r, 0, r)]
        for x0, y0, x1, y1 in corners:
            s = pcbnew.PCB_SHAPE(self.board, pcbnew.SHAPE_T_SEGMENT)
            s.SetStart(self.pt(x0, y0))
            s.SetEnd(self.pt(x1, y1))
            self._edge(s)
        if r > 0:
            k = r * (1 - math.sqrt(0.5))
            arcs = [((w - r, 0), (w - k, k), (w, r)), ((w, h - r), (w - k, h - k), (w - r, h)),
                    ((r, h), (k, h - k), (0, h - r)), ((0, r), (k, k), (r, 0))]
            for a, m, e in arcs:
                s = pcbnew.PCB_SHAPE(self.board, pcbnew.SHAPE_T_ARC)
                s.SetArcGeometry(self.pt(*a), self.pt(*m), self.pt(*e))
                self._edge(s)

    # ---- footprints -------------------------------------------------------
    def net(self, name: str) -> pcbnew.NETINFO_ITEM:
        if name not in self.nets:
            n = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(n)
            self.nets[name] = n
        return self.nets[name]

    def groups(self):
        """(schematic sheet, group table) for every [group.NAME] of board.toml.

        The sheet is the table's ``sheet`` key, or NAME. Two groups may place
        parts of one sheet, each with its own origin per instance: the power
        half and the logic half of a channel, on two different pitches.
        """
        return [(g.get("sheet", name), g) for name, g in self.cfg.get("group", {}).items()]

    def placements(self) -> dict[str, tuple[float, float, float, str]]:
        place: dict[str, tuple[float, float, float, str]] = {}
        for ref, v in self.cfg.get("place", {}).items():
            place[ref] = (v[0], v[1], v[2] if len(v) > 2 else 0, v[3] if len(v) > 3 else "F")
        by_local = {(p["sheet"], p["sheet_path"], p["local_ref"]): p["ref"] for p in self.model["parts"]}
        for sheet, group in self.groups():
            for sheet_path, origin in group.get("at", {}).items():
                gx, gy = origin[0], origin[1]
                grot = origin[2] if len(origin) > 2 else 0
                for local, v in group.get("parts", {}).items():
                    key = (sheet, sheet_path, local)
                    if key not in by_local:
                        raise BoardError(f"group {sheet}: no part {local} in sheet instance {sheet_path}")
                    dx, dy = self.rotate(v[0], v[1], grot)
                    rot = (v[2] if len(v) > 2 else 0) + grot
                    place[by_local[key]] = (gx + dx, gy + dy, rot, v[3] if len(v) > 3 else "F")
        return place

    def footprints(self) -> None:
        place = self.placements()
        sheet_of = {p["ref"]: p for p in self.model["parts"]}
        pad_net: dict[tuple[str, str], str] = {}
        for name, nodes in self.nl.nets.items():
            for ref, pin in nodes:
                pad_net[(ref, pin)] = name
        missing = sorted(set(self.nl.comps) - set(place))
        if missing:
            raise BoardError("no placement in board.toml for: " + ", ".join(missing))
        extra = sorted(set(place) - set(self.nl.comps))
        if extra:
            raise BoardError("board.toml places parts that are not in the schematic: " + ", ".join(extra))
        for ref in sorted(self.nl.comps, key=_ref_key):
            comp = self.nl.comps[ref]
            if ":" not in comp.footprint:
                raise BoardError(f"{ref} has no footprint")
            lib, fpname = comp.footprint.split(":", 1)
            fp = self.load_footprint(lib, fpname)
            if fp is None:
                raise BoardError(f"{ref}: footprint {comp.footprint} not found")
            fp.SetFPID(pcbnew.LIB_ID(lib, fpname))
            fp.SetReference(ref)
            fp.SetValue(comp.value)
            for fname, fval in comp.fields.items():
                if fname in ("Reference", "Value"):
                    continue
                if not fp.HasField(fname):
                    fp.SetField(fname, fval)
                    fp.GetField(fname).SetVisible(False)
                fp.GetField(fname).SetText(fval)
            fp.SetPath(pcbnew.KIID_PATH(f"{comp.sheet_tstamps}{comp.tstamp}"))
            fp.SetSheetname(comp.sheet_names if comp.sheet_names == "/" else comp.sheet_names.rstrip("/"))
            fp.SetSheetfile(f"{sheet_of[ref]['sheet']}.kicad_sch")
            if comp.dnp:
                fp.SetDNP(True)
            x, y, rot, side = place[ref]
            fp.SetPosition(self.pt(x, y))
            for pad in fp.Pads():
                name = pad_net.get((ref, pad.GetNumber()))
                if name:
                    pad.SetNet(self.net(name))
            stamp_footprint(fp, f"{comp.sheet_tstamps}{comp.tstamp}")
            # Added before it is flipped: FOOTPRINT.Flip on a footprint that is not on a board
            # crashes KiCad 10.0.8's Python module.
            self.board.Add(fp)
            if side == "B":
                fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
            elif side != "F":
                raise BoardError(f"{ref}: side must be F or B")
            fp.SetOrientationDegrees(rot)
            self.fps[ref] = fp

    # ---- copper -----------------------------------------------------------
    def zones(self) -> None:
        for i, z in enumerate(self.cfg.get("zone", [])):
            inset = float(z.get("inset", 0.0))
            pts = z.get("outline") or [(inset, inset), (self.w - inset, inset),
                                       (self.w - inset, self.h - inset), (inset, self.h - inset)]
            for layer in z["layers"]:
                zone = pcbnew.ZONE(self.board)
                if z["net"] not in self.nets:
                    raise BoardError(f"zone net {z['net']} is not in the netlist")
                zone.SetNet(self.nets[z["net"]])
                zone.SetLayer(self.layer(layer))
                zone.SetZoneName(f"{z['net']}_{layer}")
                for x, y in pts:
                    zone.AppendCorner(self.pt(x, y), -1)
                zone.SetLocalClearance(mm(z.get("clearance", 0.3)))
                zone.SetMinThickness(mm(z.get("min_width", 0.25)))
                zone.SetThermalReliefGap(mm(z.get("thermal_gap", 0.3)))
                zone.SetThermalReliefSpokeWidth(mm(z.get("thermal_width", 0.4)))
                connect = {"thermal": pcbnew.ZONE_CONNECTION_THERMAL, "solid": pcbnew.ZONE_CONNECTION_FULL,
                           "tht_thermal": pcbnew.ZONE_CONNECTION_THT_THERMAL}
                zone.SetPadConnection(connect[z.get("connect", "tht_thermal")])
                zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
                zone.SetAssignedPriority(int(z.get("priority", i)))
                set_uuid(zone, "zone", i, layer)
                self.board.Add(zone)

    def _pad_point(self, spec: str, near=None, refmap=None):
        ref, pad_no = spec.rsplit(".", 1)
        if refmap is not None:
            ref = refmap(ref)
        if ref not in self.fps:
            raise BoardError(f"track: unknown part {ref}")
        pads = [p for p in self.fps[ref].Pads() if p.GetNumber() == pad_no]
        if not pads:
            raise BoardError(f"track: {ref} has no pad {pad_no}")
        if near is not None and len(pads) > 1:
            pads.sort(key=lambda p: (p.GetPosition().x - near.x) ** 2 + (p.GetPosition().y - near.y) ** 2)
        return pads[0]

    def _tracks(self, tracks: list, origin=(0.0, 0.0, 0.0), refmap=None) -> int:
        count = 0
        for t in tracks:
            layer = self.layer(t.get("layer", "F.Cu"))
            other = self.layer(t.get("via_to", "B.Cu"))
            net = None
            if "net" in t:
                # a track that touches no pad names its net
                net = self.board.FindNet(t["net"])
                if net is None:
                    raise BoardError(f"track {t['points']}: no net named {t['net']}")
            pts: list[tuple[pcbnew.VECTOR2I, bool]] = []
            for p in t["points"]:
                if isinstance(p, str):
                    pad = self._pad_point(p, pts[-1][0] if pts else None, refmap)
                    if net is None:
                        net = pad.GetNet()
                    elif pad.GetNetCode() != net.GetNetCode():
                        raise BoardError(f"track {t['points']}: pads are on different nets")
                    pts.append((pad.GetPosition(), False))
                else:
                    via = isinstance(p, dict)
                    xy = p["via"] if via else p
                    dx, dy = self.rotate(xy[0], xy[1], origin[2])
                    pts.append((self.pt(origin[0] + dx, origin[1] + dy), via))
            if net is None:
                raise BoardError(f"track {t['points']}: needs at least one pad to take its net from")
            # NETINFO_ITEM.GetNetClass() returns an unwrapped SWIG pointer in KiCad 10.0.8;
            # asking the net settings by name gives a usable NETCLASS.
            nc = self.board.GetDesignSettings().m_NetSettings.GetEffectiveNetClass(net.GetNetname())
            width = mm(t["width"]) if "width" in t else nc.GetTrackWidth()
            # "via = [diameter, drill]" overrides the via of the net class for this track.
            via_dia, via_drill = (mm(t["via"][0]), mm(t["via"][1])) if "via" in t else \
                (nc.GetViaDiameter(), nc.GetViaDrill())
            for (a, _), (b, b_via) in zip(pts, pts[1:]):
                seg = pcbnew.PCB_TRACK(self.board)
                seg.SetStart(a)
                seg.SetEnd(b)
                seg.SetWidth(width)
                seg.SetLayer(layer)
                seg.SetNet(net)
                seg.SetLocked(True)
                self.board.Add(seg)
                count += 1
                if b_via:
                    v = pcbnew.PCB_VIA(self.board)
                    v.SetPosition(b)
                    v.SetViaType(pcbnew.VIATYPE_THROUGH)
                    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                    v.SetWidth(pcbnew.F_Cu, via_dia)
                    v.SetDrill(via_drill)
                    v.SetNet(net)
                    v.SetLocked(True)
                    self.board.Add(v)
                    if not t.get("stay", False):
                        layer, other = other, layer
        return count

    def preroutes(self) -> int:
        self.board.SynchronizeNetsAndNetClasses(True)
        count = self._tracks(self.cfg.get("track", []))
        by_local = {(p["sheet"], p["sheet_path"], p["local_ref"]): p["ref"] for p in self.model["parts"]}
        for sheet, group in self.groups():
            for sheet_path, origin in group.get("at", {}).items():
                o = (origin[0], origin[1], origin[2] if len(origin) > 2 else 0)
                count += self._tracks(group.get("track", []), o,
                                      lambda r, s=sheet, sp=sheet_path: by_local[(s, sp, r)])
        return count

    # ---- plane fan-out ----------------------------------------------------
    def _clearance(self, net_a: str, net_b: str) -> float:
        """Clearance in mm between two nets: the larger of their net-class clearances, or a guard distance."""
        classes, of = self.cfg.get("netclass", {}), self.model.get("netclass", {})
        default = classes.get("Default", {}).get("clearance", 0.2)
        c = max(classes.get(of.get(n, "Default"), {}).get("clearance", default) for n in (net_a, net_b))
        for g in self.cfg.get("guard", []):
            if g["net"] in (net_a, net_b) and net_a != net_b:
                c = max(c, float(g["distance"]))
        # [[keepaway]] on one layer (see hwflow.layoutrules): a plane via of a net that must stay
        # away keeps that distance from the start.
        import fnmatch
        for k in self.cfg.get("keepaway", []):
            if k.get("scope", "plan") != "layer" or net_a == net_b:
                continue
            for own, other in ((net_a, net_b), (net_b, net_a)):
                if own == k["net"] and any(fnmatch.fnmatchcase(other, p) for p in k["from"]) \
                        and not any(fnmatch.fnmatchcase(other, p) for p in k.get("except", [])):
                    c = max(c, float(k["distance"]))
        return c

    def guards(self) -> int:
        """[[guard]]: keep every other track and via a set distance from the hand-routed tracks of a net.

        A net-class clearance cannot do this: it would also apply at the pads,
        where the neighbours are closer than the guard distance. Instead a rule
        area that forbids tracks and vias is laid along each side of every
        pre-routed segment of the net that is at least ``min_length`` long, on
        that segment's layer, ``trim`` short of the segment's ends so that the
        pads there can still be reached. Pours are allowed in it: the ground
        pour fills both strips. The autorouter gets the strips as keep-outs.
        """
        count = 0
        for g in self.cfg.get("guard", []):
            dist, trim, min_len = float(g["distance"]), float(g.get("trim", 1.5)), float(g.get("min_length", 4.0))
            segs = [t for t in self.board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T and t.GetNetname() == g["net"]]
            if not segs:
                raise BoardError(f"guard: net {g['net']} has no pre-routed track")
            for t in segs:
                ax, ay = pcbnew.ToMM(t.GetStart().x), pcbnew.ToMM(t.GetStart().y)
                bx, by = pcbnew.ToMM(t.GetEnd().x), pcbnew.ToMM(t.GetEnd().y)
                length = math.hypot(bx - ax, by - ay)
                if length < min_len:
                    continue
                ux, uy = (bx - ax) / length, (by - ay) / length
                ax, ay, bx, by = ax + ux * trim, ay + uy * trim, bx - ux * trim, by - uy * trim
                near, far = pcbnew.ToMM(t.GetWidth()) / 2 + 0.25, pcbnew.ToMM(t.GetWidth()) / 2 + dist
                for side in (1, -1):
                    nx, ny = -uy * side, ux * side
                    z = pcbnew.ZONE(self.board)
                    z.SetIsRuleArea(True)
                    z.SetDoNotAllowZoneFills(False)
                    z.SetDoNotAllowTracks(True)
                    z.SetDoNotAllowVias(True)
                    z.SetDoNotAllowPads(False)
                    z.SetDoNotAllowFootprints(False)
                    ls = pcbnew.LSET()
                    ls.AddLayer(t.GetLayer())
                    z.SetLayerSet(ls)
                    z.SetZoneName(f"guard {g['net']}")
                    for px, py in ((ax + nx * near, ay + ny * near), (bx + nx * near, by + ny * near),
                                   (bx + nx * far, by + ny * far), (ax + nx * far, ay + ny * far)):
                        z.AppendCorner(pcbnew.VECTOR2I(mm(px), mm(py)), -1)
                    set_uuid(z, "guard", g["net"], count)
                    self.board.Add(z)
                    count += 1
        return count

    def fanout(self) -> tuple[int, list[str]]:
        """[[plane]]: a short track and a via beside every surface-mount pad of a plane net.

        A net that connects through an inner plane or a pour is not given to the
        autorouter (see ``[route] exclude``). Its through-hole pads reach the
        plane by themselves; every surface-mount pad gets a via here. The via is
        put at the nearest point around the pad where it and its stub keep the
        clearance to every pad, track, via and hole of another net. Returns the
        number of vias and the pads for which no place was found.
        """
        planes = self.cfg.get("plane", [])
        if not planes:
            return 0, []
        to_mm = pcbnew.ToMM
        pads = []       # (net, x0, y0, x1, y1, on_front, on_back, through, pad)
        holes = []      # (x, y, radius) of unplated holes
        keepouts = []   # boxes where a footprint forbids vias
        for fp in self.board.GetFootprints():
            for pad in fp.Pads():
                bb = pad.GetBoundingBox()
                box = (to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()), to_mm(bb.GetBottom()))
                attr = pad.GetAttribute()
                if attr == pcbnew.PAD_ATTRIB_NPTH:
                    holes.append((to_mm(pad.GetPosition().x), to_mm(pad.GetPosition().y),
                                  to_mm(pad.GetDrillSize().x) / 2))
                    continue
                through = attr == pcbnew.PAD_ATTRIB_PTH
                pads.append((pad.GetNetname(), *box, through or pad.IsOnLayer(pcbnew.F_Cu),
                             through or pad.IsOnLayer(pcbnew.B_Cu), through, pad))
            for z in fp.Zones():
                if z.GetIsRuleArea() and z.GetDoNotAllowVias():
                    bb = z.GetBoundingBox()
                    keepouts.append((to_mm(bb.GetLeft()), to_mm(bb.GetTop()), to_mm(bb.GetRight()),
                                     to_mm(bb.GetBottom())))
        segs, vias = [], []   # (net, ax, ay, bx, by, width, layer) and (net, x, y, diameter)
        for t in self.board.GetTracks():
            if t.Type() == pcbnew.PCB_VIA_T:
                vias.append((t.GetNetname(), to_mm(t.GetPosition().x), to_mm(t.GetPosition().y),
                             to_mm(t.GetWidth(pcbnew.F_Cu))))
            else:
                segs.append((t.GetNetname(), to_mm(t.GetStart().x), to_mm(t.GetStart().y), to_mm(t.GetEnd().x),
                             to_mm(t.GetEnd().y), to_mm(t.GetWidth()), t.GetLayer()))
        limits = (self.ox + 0.6, self.oy + 0.6, self.ox + self.w - 0.6, self.oy + self.h - 0.6)

        def d_rect(x, y, r):
            return math.hypot(max(r[0] - x, 0.0, x - r[2]), max(r[1] - y, 0.0, y - r[3]))

        def d_seg(x, y, ax, ay, bx, by):
            vx, vy = bx - ax, by - ay
            n = vx * vx + vy * vy
            t = 0.0 if n == 0 else max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / n))
            return math.hypot(x - ax - t * vx, y - ay - t * vy)

        def stub_clear(c, vx, vy) -> bool:
            """The straight stub from the pad centre to (vx, vy) keeps its distance on the front layer."""
            n = max(1, int(math.hypot(vx - c["cx"], vy - c["cy"]) / 0.1))
            for i in range(n + 1):
                sx, sy = c["cx"] + (vx - c["cx"]) * i / n, c["cy"] + (vy - c["cy"]) * i / n
                if any(math.hypot(sx - hx, sy - hy) < c["w"] / 2 + hr + 0.3 for hx, hy, hr in holes):
                    return False
                for o in c["pads"]:
                    if o[0] != c["net"] and o[5] and \
                            d_rect(sx, sy, o[1:5]) < c["w"] / 2 + self._clearance(c["net"], o[0]):
                        return False
                for v in c["vias"]:
                    if v[0] != c["net"] and math.hypot(sx - v[1], sy - v[2]) < \
                            c["w"] / 2 + v[3] / 2 + self._clearance(c["net"], v[0]):
                        return False
                for g in c["segs"]:
                    if g[0] != c["net"] and g[6] == pcbnew.F_Cu and \
                            d_seg(sx, sy, *g[1:5]) < c["w"] / 2 + g[5] / 2 + self._clearance(c["net"], g[0]):
                        return False
            return True

        def site_ok(c, vx, vy) -> bool:
            """A via of this net may sit at (vx, vy)."""
            r = c["dia"] / 2
            if not (limits[0] <= vx <= limits[2] and limits[1] <= vy <= limits[3]):
                return False
            if c["within"] and not any(b0 <= vx <= b2 and b1 <= vy <= b3 for b0, b1, b2, b3 in c["within"]):
                return False  # the plane is not there
            if d_rect(vx, vy, c["box"]) < r + 0.1:
                return False  # not in the pad itself
            if any(d_rect(vx, vy, k) < r + 0.2 for k in keepouts):
                return False
            if any(math.hypot(vx - hx, vy - hy) < r + hr + 0.3 for hx, hy, hr in holes):
                return False
            for o in c["pads"]:
                if d_rect(vx, vy, o[1:5]) < r + (0.1 if o[0] == c["net"] else self._clearance(c["net"], o[0])):
                    return False
            for v in c["vias"]:
                need = 0.75 if v[0] == c["net"] else r + v[3] / 2 + self._clearance(c["net"], v[0])
                if math.hypot(vx - v[1], vy - v[2]) < need:
                    return False
            for g in c["segs"]:
                if g[0] != c["net"] and d_seg(vx, vy, *g[1:5]) < r + g[5] / 2 + self._clearance(c["net"], g[0]):
                    return False
            return stub_clear(c, vx, vy)

        def find_site(c, pad, reach):
            """(x, y, new via?) for one pad, or None."""
            box, cx, cy = c["box"], c["cx"], c["cy"]
            # A via of the same net that is already within reach is used again.
            for v in sorted(c["vias"], key=lambda v: math.hypot(v[1] - cx, v[2] - cy)):
                if v[0] == c["net"] and d_rect(v[1], v[2], box) <= reach and stub_clear(c, v[1], v[2]):
                    return v[1], v[2], False
            fc = pad.GetParentFootprint().GetPosition()
            off = (cx - to_mm(fc.x), cy - to_mm(fc.y))
            away = math.atan2(off[1], off[0]) if abs(off[0]) + abs(off[1]) > 0.05 else 0.0
            angles = sorted((math.radians(a) for a in range(0, 360, 15)),
                            key=lambda a: abs((a - away + math.pi) % (2 * math.pi) - math.pi))
            step = 0.0
            while step <= reach:
                for a in angles:
                    ux, uy = math.cos(a), math.sin(a)
                    ex = (box[2] - box[0]) / 2 / abs(ux) if abs(ux) > 1e-9 else 1e9
                    ey = (box[3] - box[1]) / 2 / abs(uy) if abs(uy) > 1e-9 else 1e9
                    d = min(ex, ey) + c["dia"] / 2 + 0.1 + step
                    vx, vy = round(cx + d * ux, 3), round(cy + d * uy, 3)
                    if site_ok(c, vx, vy):
                        return vx, vy, True
                step += 0.15
            return None

        made, failed = 0, []
        for plane in planes:
            net_name = plane["net"]
            if net_name not in self.nets:
                raise BoardError(f"plane net {net_name} is not in the netlist")
            dia, drill = plane.get("via", [0.6, 0.3])
            reach = float(plane.get("reach", 2.5))
            skip = set(plane.get("skip", []))
            # "within": rectangles, in board coordinates, that the plane covers. A via goes only there.
            within = []
            for x0, y0, x1, y1 in plane.get("within", []):
                (ax, ay), (bx, by) = self.pt(x0, y0), self.pt(x1, y1)
                within.append((to_mm(min(ax, bx)), to_mm(min(ay, by)), to_mm(max(ax, bx)), to_mm(max(ay, by))))
            targets = [q for q in pads if q[0] == net_name and not q[7] and q[5]]
            targets.sort(key=lambda q: (_ref_key(q[8].GetParentFootprint().GetReference()), q[8].GetNumber(),
                                        q[1], q[2]))
            for q in targets:
                pad = q[8]
                ref = pad.GetParentFootprint().GetReference()
                label = f"{ref}.{pad.GetNumber()}"
                if label in skip or ref in skip:
                    continue
                box = q[1:5]
                near = (box[0] - reach - 2, box[1] - reach - 2, box[2] + reach + 2, box[3] + reach + 2)
                ctx = {
                    "net": net_name, "box": box, "cx": (box[0] + box[2]) / 2, "cy": (box[1] + box[3]) / 2,
                    "within": within,
                    "dia": dia, "w": min(float(plane.get("stub_width", 0.3)), box[2] - box[0], box[3] - box[1]),
                    "pads": [o for o in pads if o[8] is not pad and o[1] < near[2] and o[3] > near[0]
                             and o[2] < near[3] and o[4] > near[1]],
                    "vias": [v for v in vias if near[0] < v[1] < near[2] and near[1] < v[2] < near[3]],
                    "segs": [g for g in segs if min(g[1], g[3]) < near[2] and max(g[1], g[3]) > near[0]
                             and min(g[2], g[4]) < near[3] and max(g[2], g[4]) > near[1]],
                }
                site = find_site(ctx, pad, reach)
                if site is None:
                    failed.append(label)
                    continue
                vx, vy, new = site
                seg = pcbnew.PCB_TRACK(self.board)
                seg.SetStart(pcbnew.VECTOR2I(mm(ctx["cx"]), mm(ctx["cy"])))
                seg.SetEnd(pcbnew.VECTOR2I(mm(vx), mm(vy)))
                seg.SetWidth(mm(ctx["w"]))
                seg.SetLayer(pcbnew.F_Cu)
                seg.SetNet(self.nets[net_name])
                seg.SetLocked(True)
                self.board.Add(seg)
                segs.append((net_name, ctx["cx"], ctx["cy"], vx, vy, ctx["w"], pcbnew.F_Cu))
                if new:
                    v = pcbnew.PCB_VIA(self.board)
                    v.SetPosition(pcbnew.VECTOR2I(mm(vx), mm(vy)))
                    v.SetViaType(pcbnew.VIATYPE_THROUGH)
                    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                    v.SetWidth(pcbnew.F_Cu, mm(dia))
                    v.SetDrill(mm(drill))
                    v.SetNet(self.nets[net_name])
                    v.SetLocked(True)
                    self.board.Add(v)
                    vias.append((net_name, vx, vy, dia))
                    made += 1
        return made, failed

    def rule_areas(self) -> int:
        """[[rule_area]]: a region of some layers where pours, tracks or vias are not allowed."""
        count = 0
        for i, r in enumerate(self.cfg.get("rule_area", [])):
            z = pcbnew.ZONE(self.board)
            z.SetIsRuleArea(True)
            z.SetDoNotAllowZoneFills(bool(r.get("no_pour", False)))
            z.SetDoNotAllowTracks(bool(r.get("no_tracks", False)))
            z.SetDoNotAllowVias(bool(r.get("no_vias", False)))
            z.SetDoNotAllowPads(False)
            z.SetDoNotAllowFootprints(False)
            ls = pcbnew.LSET()
            for layer in r["layers"]:
                ls.AddLayer(self.layer(layer))
            z.SetLayerSet(ls)
            z.SetZoneName(r.get("name", f"rule area {i + 1}"))
            if "rect" in r:
                x0, y0, x1, y1 = r["rect"]
                pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            else:
                pts = r["outline"]
            for x, y in pts:
                z.AppendCorner(self.pt(x, y), -1)
            set_uuid(z, "rule_area", i)
            self.board.Add(z)
            count += 1
        return count

    def silk(self) -> None:
        """Hide or move reference designators on the silkscreen, as board.toml asks."""
        import fnmatch
        cfg = self.cfg.get("silk", {})
        for ref, fp in self.fps.items():
            field = fp.Reference()
            if any(fnmatch.fnmatchcase(ref, pat) for pat in cfg.get("hide", [])):
                field.SetVisible(False)
        moves = dict(cfg.get("move", {}))
        by_local = {(p["sheet"], p["sheet_path"], p["local_ref"]): p["ref"] for p in self.model["parts"]}
        for sheet, group in self.groups():
            for sheet_path, origin in group.get("at", {}).items():
                grot = origin[2] if len(origin) > 2 else 0
                for local, off in group.get("silk_move", {}).items():
                    dx, dy = self.rotate(off[0], off[1], grot)
                    moves[by_local[(sheet, sheet_path, local)]] = [dx, dy, *off[2:]]
        for ref, off in moves.items():
            if ref not in self.fps:
                raise BoardError(f"silk move: unknown part {ref}")
            fp = self.fps[ref]
            pos = fp.GetPosition()
            field = fp.Reference()
            d = self.offset(off[0], off[1])
            field.SetPosition(pcbnew.VECTOR2I(pos.x + d.x, pos.y + d.y))
            if len(off) > 2:
                field.SetTextAngleDegrees(off[2])

    def texts(self) -> None:
        for t in self.cfg.get("text", []):
            item = pcbnew.PCB_TEXT(self.board)
            item.SetText(t["text"])
            item.SetLayer(self.layer(t.get("layer", "F.SilkS")))
            item.SetPosition(self.pt(t["at"][0], t["at"][1]))
            size = mm(t.get("size", 1.0))
            item.SetTextSize(pcbnew.VECTOR2I(size, size))
            item.SetTextThickness(mm(t.get("thickness", 0.15)))
            if item.GetLayer() in (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Fab):
                item.SetMirrored(True)
            if "angle" in t:
                item.SetTextAngleDegrees(float(t["angle"]))
            just = {"left": pcbnew.GR_TEXT_H_ALIGN_LEFT, "right": pcbnew.GR_TEXT_H_ALIGN_RIGHT,
                    "center": pcbnew.GR_TEXT_H_ALIGN_CENTER}[t.get("justify", "center")]
            item.SetHorizJustify(just)
            set_uuid(item, "text", t["text"], t["at"][0], t["at"][1])
            self.board.Add(item)
            if t.get("optional", False):
                self.optional_texts.add(item.m_Uuid.AsString())

    def lines(self) -> None:
        """[[line]]: straight silkscreen strokes, for a mark that is not text (the ground symbol)."""
        for i, ln in enumerate(self.cfg.get("line", [])):
            for j, (a, b) in enumerate(zip(ln["points"], ln["points"][1:])):
                seg = pcbnew.PCB_SHAPE(self.board, pcbnew.SHAPE_T_SEGMENT)
                seg.SetStart(self.pt(a[0], a[1]))
                seg.SetEnd(self.pt(b[0], b[1]))
                seg.SetLayer(self.layer(ln.get("layer", "F.SilkS")))
                seg.SetWidth(mm(ln.get("width", 0.15)))
                set_uuid(seg, "line", i, j)
                self.board.Add(seg)

    def clean_silk(self) -> dict:
        """Remove or move what DRC finds wrong on the silkscreen. See hwflow.silk."""
        from . import silk as silk_mod
        work = self.project_dir / "build" / "silk"
        try:
            return silk_mod.clean(self.board, self.pcb_path, work, self.optional_texts, log=print)
        except silk_mod.SilkError as e:
            raise BoardError(str(e)) from e

    def save(self) -> None:
        stamp_copper(self.board)
        pcbnew.SaveBoard(str(self.pcb_path), self.board)
        stamp_points(self.pcb_path)


def _ref_key(ref: str):
    head = ref.rstrip("0123456789")
    return (head, int(ref[len(head):] or 0))


def remove_dangling(board, conn) -> int:
    """Delete autorouted track stubs that end in mid-air. Freerouting leaves a few.

    Locked (pre-routed) tracks are never removed. Repeats until nothing changes,
    because removing one stub can expose the next segment of the same stub.
    """
    removed = 0
    while True:
        board.BuildConnectivity()
        bad = [t for t in board.GetTracks()
               if t.Type() == pcbnew.PCB_TRACE_T and not t.IsLocked()
               and conn.TestTrackEndpointDangling(t, False)]
        if not bad:
            return removed
        # BOARD.Delete, not BOARD.Remove. Remove hands the C++ object to Python;
        # when the proxy is garbage-collected KiCad 10.0.8's wrappers break
        # (BOARD.Tracks() then returns a bare SwigPyObject) and the interpreter
        # segfaults on exit.
        n = len(bad)
        for t in bad:
            board.Delete(t)
        del bad
        removed += n


def cmd_build(args) -> int:
    b = Builder(Path(args.project_dir), Path(args.design_dir),
                Path(args.fab_rules) if args.fab_rules else None)
    b.new_board()
    b.outline()
    b.footprints()
    n_pre = b.preroutes()
    n_guard = b.guards()
    n_fan, no_via = b.fanout()
    if no_via:
        sys.stderr.write("fan-out: no place for a plane via at: " + ", ".join(no_via) + "\n")
    b.silk()
    b.texts()
    b.lines()
    silk_stats = b.clean_silk() if b.cfg.get("silk", {}).get("clean", False) else None
    b.save()
    if silk_stats is not None:
        (Path(args.project_dir) / "build" / "silk.json").write_text(json.dumps(silk_stats, indent=1) + "\n",
                                                                    encoding="utf-8")
        print(f"silkscreen: clean after {silk_stats['rounds']} DRC rounds; {silk_stats['graphics_removed']} outline "
              f"strokes removed, {len(silk_stats['refs_moved'])} designators moved, "
              f"{len(silk_stats['refs_hidden'])} hidden, {len(silk_stats['texts_removed'])} optional texts dropped")
    dsn = Path(args.project_dir) / "build" / f"{b.name}.dsn"
    layers = b.board.GetCopperLayerCount()
    outer_only = layers > 2 and b.cfg.get("route", {}).get("outer_layers_only", False)
    if outer_only:
        # Freerouting 2.5.0 routes signals on every copper layer it is told
        # about, plane or not. Showing it a 2-layer board keeps the inner
        # layers free for the pours added below; its vias still go through.
        b.board.SetCopperLayerCount(2)
    ok = pcbnew.ExportSpecctraDSN(b.board, str(dsn))
    if outer_only:
        b.board.SetCopperLayerCount(layers)
    if not ok:
        raise BoardError("Specctra DSN export failed")
    route = b.cfg.get("route", {})
    n_excluded, fixed_classes = dsn_mod.fix_nets(dsn, route.get("exclude", []))
    names = ["F.Cu"] + [f"In{i}.Cu" for i in range(1, layers - 1)] + ["B.Cu"]

    def to_dsn(x: float, y: float) -> tuple[float, float]:
        p = b.pt(x, y)
        return pcbnew.ToMM(p.x) * 1000.0, -pcbnew.ToMM(p.y) * 1000.0

    dsn_mod.add_keepouts(dsn, route.get("keepout", []), to_dsn)
    # Read by hwflow.route, which hands them to the autorouter in its settings file.
    (Path(args.project_dir) / "build" / "router.json").write_text(
        json.dumps(dsn_mod.router_settings(names, route, fixed_classes), indent=1, sort_keys=True) + "\n",
        encoding="utf-8")
    # Zones are added after the DSN is written: the autorouter then routes
    # GND like any other net, and the pour only adds copper on top.
    b.zones()
    b.rule_areas()
    b.save()
    print(f"board: {len(b.fps)} footprints placed, {n_pre} pre-routed track segments, {n_fan} plane vias, "
          f"{n_guard} guard strips, {len(list(b.board.Zones()))} zones and rule areas; DSN written to {dsn}, "
          f"{n_excluded} nets kept from the autorouter")
    return 0


def cmd_finish(args) -> int:
    project_dir = Path(args.project_dir)
    model = json.loads((project_dir / "build" / "model.json").read_text(encoding="utf-8"))
    pcb_path = project_dir / f"{model['name']}.kicad_pcb"
    board = pcbnew.LoadBoard(str(pcb_path))
    before = len(list(board.GetTracks()))
    if args.session:
        if not pcbnew.ImportSpecctraSES(board, str(args.session)):
            raise BoardError("Specctra session import failed")
    after = len(list(board.GetTracks()))
    unrouted, patched = 0, 0
    if args.design_dir:
        # [[unroute]] and [[patch]] of board.toml: what the autorouter left open or did badly is
        # taken out and written by hand. This happens after the import, so the stored session
        # stays valid; the patches belong to that session and DRC fails if they no longer fit.
        b = Builder(project_dir, Path(args.design_dir))
        b.board = board
        b.fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
        import fnmatch
        pats = [p for u in b.cfg.get("unroute", []) for p in u["nets"]]
        if pats:
            gone = [t for t in board.GetTracks() if not t.IsLocked()
                    and any(fnmatch.fnmatchcase(t.GetNetname(), p) for p in pats)]
            unrouted = len(gone)
            for t in gone:
                board.Delete(t)
            del gone
        patched = b._tracks(b.cfg.get("patch", []))
    removed = remove_dangling(board, board.GetConnectivity())
    tracks = list(board.GetTracks())
    stats = {
        "pre_routed_items": before,
        "items_after_session_import": after,
        "dangling_stubs_removed": removed,
        "autorouted_items_taken_out": unrouted,
        "patch_segments": patched,
        "track_segments": sum(1 for t in tracks if t.Type() != pcbnew.PCB_VIA_T),
        "vias": sum(1 for t in tracks if t.Type() == pcbnew.PCB_VIA_T),
        "track_length_mm": round(sum(pcbnew.ToMM(t.GetLength()) for t in tracks if t.Type() != pcbnew.PCB_VIA_T), 1),
    }
    stamp_copper(board)
    # Zones are filled by `kicad-cli pcb drc --refill-zones --save-board` in the
    # next build step: that is KiCad's own headless fill, and DRC needs the
    # zones filled anyway.
    pcbnew.SaveBoard(str(pcb_path), board)
    stamp_points(pcb_path)
    (project_dir / "build" / "route-stats.json").write_text(json.dumps(stats, indent=1) + "\n", encoding="utf-8")
    print(f"board: {stats['track_segments']} track segments, {stats['vias']} vias "
          f"({before} items were pre-routed, {removed} dangling stubs removed, {unrouted} autorouted items "
          f"taken out and {patched} segments patched by hand)")
    return 0


def _extent(items) -> list[float] | None:
    """Bounding box [x0, y0, x1, y1] in page millimetres of some board items, or None."""
    ext = None
    for g in items:
        b = g.GetBoundingBox()
        e = (b.GetLeft(), b.GetTop(), b.GetRight(), b.GetBottom())
        ext = e if ext is None else (min(ext[0], e[0]), min(ext[1], e[1]), max(ext[2], e[2]), max(ext[3], e[3]))
    return None if ext is None else [pcbnew.ToMM(v) for v in ext]


def cmd_dump(args) -> int:
    """Write build/placement.json: where every footprint, pad and hole of the saved board is.

    Positions are in board coordinates, the frame of board.toml. The mechanical
    check reads this file, so that it needs neither pcbnew nor the board file.
    """
    project_dir = Path(args.project_dir)
    b = Builder(project_dir, Path(args.design_dir))
    board = pcbnew.LoadBoard(str(b.pcb_path))

    def xy(px: float, py: float) -> list[float]:
        return [round(px - b.ox, 4), round((b.h - (py - b.oy)) if b.y_up else (py - b.oy), 4)]

    def box(e) -> list[float] | None:
        if e is None:
            return None
        (x0, y0), (x1, y1) = xy(e[0], e[1]), xy(e[2], e[3])
        return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]

    parts = []
    for fp in sorted(board.GetFootprints(), key=lambda f: _ref_key(f.GetReference())):
        back = fp.IsFlipped()
        shapes = [g for g in fp.GraphicalItems() if g.GetClass() == "PCB_SHAPE"]
        fab = [g for g in shapes if g.GetLayer() == (pcbnew.B_Fab if back else pcbnew.F_Fab)]
        yard = [g for g in shapes if g.GetLayer() == (pcbnew.B_CrtYd if back else pcbnew.F_CrtYd)]
        pads = []
        for p in fp.Pads():
            pos, size = p.GetPosition(), p.GetBoundingBox()
            pads.append({
                "number": p.GetNumber(), "net": p.GetNetname(), "at": xy(pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)),
                "size": [round(pcbnew.ToMM(size.GetWidth()), 4), round(pcbnew.ToMM(size.GetHeight()), 4)],
                "drill": round(pcbnew.ToMM(p.GetDrillSize().x), 4),
                "plated": p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH,
                "through": p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH),
            })
        pos = fp.GetPosition()
        parts.append({
            "ref": fp.GetReference(), "footprint": str(fp.GetFPID().GetUniStringLibId()),
            "value": fp.GetValue(), "side": "bottom" if back else "top",
            "at": xy(pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)), "rot": fp.GetOrientationDegrees(),
            "dnp": fp.IsDNP(), "body": box(_extent(fab)), "courtyard": box(_extent(yard)), "pads": pads,
        })
    edge = [d for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]
    out = {
        "name": b.name, "frame": "y up, origin bottom-left" if b.y_up else "y down, origin top-left",
        "outline": box(_extent(edge)), "layers": board.GetCopperLayerCount(),
        "thickness": pcbnew.ToMM(board.GetDesignSettings().GetBoardThickness()), "parts": parts,
    }
    dest = project_dir / "build" / "placement.json"
    dest.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    print(f"dump: {len(parts)} footprints -> {dest}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.pcbbuild")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("build")
    s.add_argument("design_dir")
    s.add_argument("project_dir")
    s.add_argument("--fab-rules")
    s.set_defaults(fn=cmd_build)
    s = sub.add_parser("finish")
    s.add_argument("project_dir")
    s.add_argument("--session")
    s.add_argument("--design", dest="design_dir",
                   help="design directory: apply [[unroute]] and [[patch]] of board.toml")
    s.set_defaults(fn=cmd_finish)
    s = sub.add_parser("dump")
    s.add_argument("design_dir")
    s.add_argument("project_dir")
    s.set_defaults(fn=cmd_dump)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except BoardError as e:
        sys.stderr.write(f"board error: {e}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
