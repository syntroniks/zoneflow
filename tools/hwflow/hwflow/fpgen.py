# SPDX-License-Identifier: Apache-2.0
"""Write project-local KiCad footprints from a description in Python.

Runs under the system interpreter that carries KiCad's ``pcbnew`` module, so
that KiCad itself writes the ``.kicad_mod`` files. A board's
``design/footprints.py`` builds each footprint with the small ``Footprint``
class here: pads from the dimensions of the maker's drawing, outline,
courtyard, keep-out areas and a 3D model where KiCad ships one.

Coordinates are millimetres in KiCad's footprint frame: x to the right, y
down, seen from the component side. Every item gets a UUID derived from the
footprint's name and the item's position in it, so that regenerating a
library changes nothing in the files.
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pcbnew

STOCK = os.environ.get("KICAD10_FOOTPRINT_DIR", "/usr/share/kicad/footprints")
MODELS = "${KICAD10_3DMODEL_DIR}"
_NS = uuid.UUID("6f1d3c0e-5a0b-4a52-9a3c-2f8f9a7e0003")

_LAYER = {
    "F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu, "F.SilkS": pcbnew.F_SilkS, "B.SilkS": pcbnew.B_SilkS,
    "F.Fab": pcbnew.F_Fab, "B.Fab": pcbnew.B_Fab, "F.CrtYd": pcbnew.F_CrtYd, "B.CrtYd": pcbnew.B_CrtYd,
    "F.Mask": pcbnew.F_Mask, "B.Mask": pcbnew.B_Mask, "F.Paste": pcbnew.F_Paste, "Dwgs.User": pcbnew.Dwgs_User,
    "Cmts.User": pcbnew.Cmts_User,
}
_SHAPE = {"rect": pcbnew.PAD_SHAPE_RECT, "roundrect": pcbnew.PAD_SHAPE_ROUNDRECT, "circle": pcbnew.PAD_SHAPE_CIRCLE,
          "oval": pcbnew.PAD_SHAPE_OVAL}
_ATTR = {"smd": pcbnew.FP_SMD, "tht": pcbnew.FP_THROUGH_HOLE, "other": 0}


def _mm(v: float) -> int:
    return pcbnew.FromMM(float(v))


def _v(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(_mm(x), _mm(y))


class Footprint:
    def __init__(self, name: str, description: str, tags: str = "", kind: str = "smd",
                 from_stock: str | None = None):
        """``from_stock`` ("Library:Name") starts from a copy of a stock KiCad footprint."""
        self.name = name
        if from_stock:
            lib, stock_name = from_stock.split(":", 1)
            self.fp = pcbnew.FootprintLoad(f"{STOCK}/{lib}.pretty", stock_name)
            if self.fp is None:
                raise SystemExit(f"stock footprint {from_stock} not found")
        else:
            self.fp = pcbnew.FOOTPRINT(None)
            self.fp.SetAttributes(_ATTR[kind])
            self.fp.SetReference("REF**")
        self.fp.SetValue(name)
        self.fp.SetLibDescription(description)
        self.fp.SetKeywords(tags)
        self.fp.Value().SetLayer(pcbnew.F_Fab)

    # ---- pads -------------------------------------------------------------
    def _pad(self, number: str, x: float, y: float, w: float, h: float, shape: str):
        p = pcbnew.PAD(self.fp)
        p.SetNumber(number)
        p.SetShape(pcbnew.F_Cu, _SHAPE[shape])
        p.SetSize(pcbnew.F_Cu, _v(w, h))
        if shape == "roundrect":
            p.SetRoundRectRadiusRatio(pcbnew.F_Cu, min(0.25, 0.25 / min(w, h)))
        p.SetPosition(_v(x, y))
        self.fp.Add(p)
        return p

    def smd(self, number: str, x: float, y: float, w: float, h: float, shape: str = "roundrect") -> None:
        """A surface-mount pad. ``number`` "" makes a mechanical land that has no net."""
        p = self._pad(number, x, y, w, h, shape)
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetLayerSet(pcbnew.PAD.SMDMask())

    def tht(self, number: str, x: float, y: float, diameter: float, drill: float, shape: str = "circle") -> None:
        p = self._pad(number, x, y, diameter, diameter, shape)
        p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        p.SetDrillSize(_v(drill, drill))
        p.SetLayerSet(pcbnew.PAD.PTHMask())

    def npth(self, x: float, y: float, drill: float) -> None:
        """An unplated hole with no copper."""
        p = self._pad("", x, y, drill, drill, "circle")
        p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        p.SetDrillSize(_v(drill, drill))
        p.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())

    # ---- graphics ---------------------------------------------------------
    def _shape(self, kind, layer: str, width: float):
        s = pcbnew.PCB_SHAPE(self.fp, kind)
        s.SetLayer(_LAYER[layer])
        s.SetWidth(_mm(width))
        self.fp.Add(s)
        return s

    def line(self, layer: str, points, width: float = 0.12) -> None:
        for a, b in zip(points, points[1:]):
            s = self._shape(pcbnew.SHAPE_T_SEGMENT, layer, width)
            s.SetStart(_v(*a))
            s.SetEnd(_v(*b))

    def rect(self, layer: str, x0: float, y0: float, x1: float, y1: float, width: float = 0.12) -> None:
        s = self._shape(pcbnew.SHAPE_T_RECT, layer, width)
        s.SetStart(_v(x0, y0))
        s.SetEnd(_v(x1, y1))

    def circle(self, layer: str, x: float, y: float, radius: float, width: float = 0.12) -> None:
        s = self._shape(pcbnew.SHAPE_T_CIRCLE, layer, width)
        s.SetCenter(_v(x, y))
        s.SetEnd(_v(x + radius, y))

    def text(self, layer: str, text: str, x: float, y: float, size: float = 1.0, angle: float = 0) -> None:
        t = pcbnew.PCB_TEXT(self.fp)
        t.SetText(text)
        t.SetLayer(_LAYER[layer])
        t.SetPosition(_v(x, y))
        t.SetTextSize(pcbnew.VECTOR2I(_mm(size), _mm(size)))
        t.SetTextThickness(_mm(0.15))
        t.SetTextAngleDegrees(angle)
        if layer.startswith("B."):
            t.SetMirrored(True)
        self.fp.Add(t)

    def outline(self, x0: float, y0: float, x1: float, y1: float, silk_gap: float = 0.12,
                courtyard: float = 0.25) -> None:
        """Body rectangle on F.Fab, the same on the silkscreen just outside it, and a courtyard around it."""
        self.rect("F.Fab", x0, y0, x1, y1, 0.1)
        self.rect("F.SilkS", x0 - silk_gap, y0 - silk_gap, x1 + silk_gap, y1 + silk_gap, 0.12)
        self.courtyard(x0 - courtyard, y0 - courtyard, x1 + courtyard, y1 + courtyard)

    def courtyard(self, x0: float, y0: float, x1: float, y1: float, layer: str = "F.CrtYd") -> None:
        self.rect(layer, round(x0, 2), round(y0, 2), round(x1, 2), round(y1, 2), 0.05)

    def labels(self, ref_at, value_at, fab_ref_at=(0, 0)) -> None:
        self.fp.Reference().SetPosition(_v(*ref_at))
        self.fp.Value().SetPosition(_v(*value_at))
        self.text("F.Fab", "${REFERENCE}", fab_ref_at[0], fab_ref_at[1], 0.8)

    # ---- rule areas and models -------------------------------------------
    def keepout(self, points, layers=("F.Cu", "B.Cu"), all_copper: bool = False, tracks: bool = True,
                vias: bool = True, pour: bool = True, pads: bool = False, footprints: bool = False,
                name: str = "") -> None:
        z = pcbnew.ZONE(self.fp)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(tracks)
        z.SetDoNotAllowVias(vias)
        z.SetDoNotAllowZoneFills(pour)
        z.SetDoNotAllowPads(pads)
        z.SetDoNotAllowFootprints(footprints)
        if all_copper:
            z.SetLayerSet(pcbnew.LSET.AllCuMask())
        else:
            ls = pcbnew.LSET()
            for layer in layers:
                ls.AddLayer(_LAYER[layer])
            z.SetLayerSet(ls)
        if name:
            z.SetZoneName(name)
        for x, y in points:
            z.AppendCorner(_v(x, y), -1)
        self.fp.Add(z)

    def model(self, path: str, offset=(0, 0, 0), rotate=(0, 0, 0)) -> None:
        """A STEP model from KiCad's own 3D library (path below the 3D model directory)."""
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = f"{MODELS}/{path}"
        m.m_Offset = pcbnew.VECTOR3D(*offset)
        m.m_Rotation = pcbnew.VECTOR3D(*rotate)
        self.fp.Models().push_back(m)

    def zones(self):
        return list(self.fp.Zones())

    # ---- output -----------------------------------------------------------
    def _stamp(self) -> None:
        def uid(*key):
            return pcbnew.KIID(str(uuid.uuid5(_NS, "/".join(str(k) for k in (self.name, *key)))))

        self.fp.SetUuid(uid("fp"))
        for kind, items in (("pad", self.fp.Pads()), ("gr", self.fp.GraphicalItems()), ("field", self.fp.GetFields()),
                            ("zone", self.fp.Zones())):
            for i, item in enumerate(items):
                item.SetUuid(uid(kind, i))

    def save(self, lib_dir: Path) -> None:
        lib_dir = Path(lib_dir).resolve()
        io = pcbnew.PCB_IO_MGR.FindPlugin(pcbnew.PCB_IO_MGR.KICAD_SEXP)
        lib_dir.mkdir(parents=True, exist_ok=True)  # a .pretty library is a directory of .kicad_mod files
        self.fp.SetFPID(pcbnew.LIB_ID(lib_dir.stem, self.name))
        self._stamp()
        io.FootprintSave(str(lib_dir), self.fp)
        # KiCad gives some items a fresh UUID as it saves. Number them all in file order instead.
        path = lib_dir / f"{self.name}.kicad_mod"
        count = iter(range(1 << 30))
        text = re.sub(r'\(uuid "[0-9a-f-]+"\)',
                      lambda m: f'(uuid "{uuid.uuid5(_NS, f"{self.name}/{next(count)}")}")',
                      path.read_text(encoding="utf-8"))
        path.write_text(text, encoding="utf-8")


def write_library(lib_dir: Path, footprints: list[Footprint]) -> None:
    """Write every footprint and remove files of footprints that are no longer defined."""
    lib_dir = Path(lib_dir)
    for fp in footprints:
        fp.save(lib_dir)
    keep = {f"{fp.name}.kicad_mod" for fp in footprints}
    for f in lib_dir.glob("*.kicad_mod"):
        if f.name not in keep:
            f.unlink()
    print(f"footprints: {len(footprints)} written to {lib_dir}")
