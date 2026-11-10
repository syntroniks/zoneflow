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
        self.board = None
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
        """Board coordinates (origin top-left of the outline) to page coordinates."""
        return pcbnew.VECTOR2I(mm(self.ox + x), mm(self.oy + y))

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

    def placements(self) -> dict[str, tuple[float, float, float, str]]:
        place: dict[str, tuple[float, float, float, str]] = {}
        for ref, v in self.cfg.get("place", {}).items():
            place[ref] = (v[0], v[1], v[2] if len(v) > 2 else 0, v[3] if len(v) > 3 else "F")
        by_local = {(p["sheet"], p["sheet_path"], p["local_ref"]): p["ref"] for p in self.model["parts"]}
        for sheet, group in self.cfg.get("group", {}).items():
            for sheet_path, origin in group.get("at", {}).items():
                gx, gy = origin[0], origin[1]
                grot = origin[2] if len(origin) > 2 else 0
                for local, v in group.get("parts", {}).items():
                    key = (sheet, sheet_path, local)
                    if key not in by_local:
                        raise BoardError(f"group {sheet}: no part {local} in sheet instance {sheet_path}")
                    dx, dy = _rotate(v[0], v[1], grot)
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
            fp = pcbnew.FootprintLoad(f"{FOOTPRINT_DIR}/{lib}.pretty", fpname)
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
            if side == "B":
                fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
            elif side != "F":
                raise BoardError(f"{ref}: side must be F or B")
            fp.SetOrientationDegrees(rot)
            for pad in fp.Pads():
                name = pad_net.get((ref, pad.GetNumber()))
                if name:
                    pad.SetNet(self.net(name))
            stamp_footprint(fp, f"{comp.sheet_tstamps}{comp.tstamp}")
            self.board.Add(fp)
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
                    dx, dy = _rotate(xy[0], xy[1], origin[2])
                    pts.append((self.pt(origin[0] + dx, origin[1] + dy), via))
            if net is None:
                raise BoardError(f"track {t['points']}: needs at least one pad to take its net from")
            # NETINFO_ITEM.GetNetClass() returns an unwrapped SWIG pointer in KiCad 10.0.8;
            # asking the net settings by name gives a usable NETCLASS.
            nc = self.board.GetDesignSettings().m_NetSettings.GetEffectiveNetClass(net.GetNetname())
            width = mm(t["width"]) if "width" in t else nc.GetTrackWidth()
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
                    v.SetWidth(pcbnew.F_Cu, nc.GetViaDiameter())
                    v.SetDrill(nc.GetViaDrill())
                    v.SetNet(net)
                    v.SetLocked(True)
                    self.board.Add(v)
                    layer, other = other, layer
        return count

    def preroutes(self) -> int:
        self.board.SynchronizeNetsAndNetClasses(True)
        count = self._tracks(self.cfg.get("track", []))
        by_local = {(p["sheet"], p["sheet_path"], p["local_ref"]): p["ref"] for p in self.model["parts"]}
        for sheet, group in self.cfg.get("group", {}).items():
            for sheet_path, origin in group.get("at", {}).items():
                o = (origin[0], origin[1], origin[2] if len(origin) > 2 else 0)
                count += self._tracks(group.get("track", []), o,
                                      lambda r, s=sheet, sp=sheet_path: by_local[(s, sp, r)])
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
        for sheet, group in self.cfg.get("group", {}).items():
            for sheet_path, origin in group.get("at", {}).items():
                grot = origin[2] if len(origin) > 2 else 0
                for local, off in group.get("silk_move", {}).items():
                    dx, dy = _rotate(off[0], off[1], grot)
                    moves[by_local[(sheet, sheet_path, local)]] = [dx, dy, *off[2:]]
        for ref, off in moves.items():
            if ref not in self.fps:
                raise BoardError(f"silk move: unknown part {ref}")
            fp = self.fps[ref]
            pos = fp.GetPosition()
            field = fp.Reference()
            field.SetPosition(pcbnew.VECTOR2I(pos.x + mm(off[0]), pos.y + mm(off[1])))
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
            set_uuid(item, "text", t["text"], t["at"][0], t["at"][1])
            self.board.Add(item)

    def save(self) -> None:
        stamp_copper(self.board)
        pcbnew.SaveBoard(str(self.pcb_path), self.board)
        stamp_points(self.pcb_path)


def _rotate(dx: float, dy: float, deg: float) -> tuple[float, float]:
    """Rotate an offset the way KiCad rotates a footprint (positive is counter-clockwise on screen)."""
    a = math.radians(deg)
    return (dx * math.cos(a) + dy * math.sin(a), -dx * math.sin(a) + dy * math.cos(a))


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
    b.silk()
    b.texts()
    b.save()
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
    # Zones are added after the DSN is written: the autorouter then routes
    # GND like any other net, and the pour only adds copper on top.
    b.zones()
    b.save()
    print(f"board: {len(b.fps)} footprints placed, {n_pre} pre-routed track segments, "
          f"{len(list(b.board.Zones()))} zones; DSN written to {dsn}")
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
    removed = remove_dangling(board, board.GetConnectivity())
    tracks = list(board.GetTracks())
    stats = {
        "pre_routed_items": before,
        "items_after_session_import": after,
        "dangling_stubs_removed": removed,
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
          f"({before} items were pre-routed, {removed} dangling stubs removed)")
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
    s.set_defaults(fn=cmd_finish)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except BoardError as e:
        sys.stderr.write(f"board error: {e}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
