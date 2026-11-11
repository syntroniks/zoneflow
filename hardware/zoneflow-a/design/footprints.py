# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Project-local footprints for ZoneFlow rev A.

Run by ``make`` (``/usr/bin/python3 design/footprints.py lib/ZoneFlow.pretty``):
it writes ``lib/ZoneFlow.pretty`` through KiCad's ``pcbnew`` module. A
footprint is here only where KiCad's library has none for the ordering code
in the BOM, or where the stock one does not match the maker's drawing.

Every number below is from the drawing named beside it. footprints.md, in
this directory, lists each drawing, what was checked against it and what
could not be read. Nothing here has been checked against a physical part.

Frame: millimetres, x right, y down, seen from the component side.
"""

import sys
from pathlib import Path

from hwflow.fpgen import Footprint, write_library


def terminal_header(poles: int) -> Footprint:
    """Cixi Kefa KF2EDGR-3.81, right-angle pluggable header. Drawing KF2EDGR-3.81 rev A, 2021-03-13.

    Pitch 3.81, PCB hole 1.40 +0.10/-0, body P x 3.81 + 0.80 long and 9.20 deep, pin row 1.30 from
    the rear face, 7.00 high, pins 0.80 square. Origin on pin 1; the mating face points to +y,
    which is the board's terminal edge when the footprint is not rotated.
    """
    name = f"KF2EDGR-3.81-{poles}P"
    f = Footprint(name, f"Cixi Kefa KF2EDGR-3.81-{poles}P pluggable header, {poles} poles, 3.81 mm, right angle, "
                        "drawing KF2EDGR-3.81 rev A", "terminal block header pluggable 3.81 15EDG", kind="tht")
    pitch, end = 3.81, (3.81 + 0.80) / 2  # the body overhangs the first and last pin by 2.305
    for i in range(poles):
        f.tht(str(i + 1), i * pitch, 0, 2.3, 1.4, "rect" if i == 0 else "circle")
    x0, x1, y0, y1 = -end, (poles - 1) * pitch + end, -1.30, 9.20 - 1.30
    # 0.25 around the body: the contract puts J2 and J3 end to end with 0.6 mm between the bodies.
    f.outline(x0, y0, x1, y1, courtyard=0.25)
    f.line("F.Fab", [(x0, 4.0), (x1, 4.0)], 0.1)  # the step in the housing, to tell front from back
    f.line("F.SilkS", [(x0 - 0.5, -0.8), (x0 - 0.5, 0.8)], 0.2)  # pin 1 mark
    f.labels((x0 + 1.5, -2.6), ((x0 + x1) / 2, y1 + 1.6), ((x0 + x1) / 2, 5.5))
    # The Phoenix MC 1,5/x-G-3,81 header has the same pitch and nearly the same body. Stand-in for the STEP export.
    f.model(f"Connector_Phoenix_MC.3dshapes/PhoenixContact_MC_1,5_{poles}-G-3.81_1x{poles:02d}_P3.81mm_Horizontal.step")
    return f


def fpc_12() -> Footprint:
    """XUNPU FPC-05F-12PH20, 12-way 0.5 mm flip-lock FPC connector, bottom contact, 2.0 mm high.

    Maker drawing "FPC 0.5PH H2.0", part FPC-05F-NPH20, rev A (page 1 of the product specification):
    body (N - 1) x 0.5 + 4.90 = 10.40 long, 5.12 deep; recommended land pattern: 12 lands 0.30 x 1.25
    at 0.50 pitch, two mounting lands 2.00 x 2.50 whose outer edges are 2.69 beyond the end land
    centres and whose near edges are 0.70 from the land row.

    Orientation and numbering follow the mechanical contract: origin at the body centre, the FPC
    enters from +x, pad 1 is at +y (towards the terminal edge of the board when not rotated).
    The drawing does not dimension the body against the lands. The body is placed so that its rear
    edge is 0.305 in front of the land centres, which is what the LCSC library footprint for C2856799
    has and what the drawing shows to scale.
    """
    f = Footprint("FPC_XUNPU_FPC-05F-12PH20", "XUNPU FPC-05F-12PH20, FPC connector 12-way 0.5 mm, bottom contact, "
                  "flip lock, 2.0 mm high. FPC enters from +x. Pad 1 at +y", "FPC FFC 0.5mm 12 flip lock")
    half_d, half_l = 5.12 / 2, 10.40 / 2
    land_x = -half_d - 0.305
    for k in range(12):
        f.smd(str(k + 1), land_x, 2.75 - 0.5 * k, 1.25, 0.30, "rect")
    mount_x = land_x + 1.25 / 2 + 0.70 + 2.50 / 2
    mount_y = 2.75 + 2.69 - 2.00 / 2
    for sy in (-1, 1):
        f.smd("", mount_x, sy * mount_y, 2.50, 2.00, "rect")
    f.rect("F.Fab", -half_d, -half_l, half_d, half_l, 0.1)
    f.line("F.Fab", [(half_d - 1.2, -half_l), (half_d - 1.2, half_l)], 0.1)  # the flip lock, at the mouth
    for sy in (-1, 1):  # silkscreen at the two ends only: lands and the FPC occupy the long sides
        y = sy * (half_l + 0.12)
        f.line("F.SilkS", [(-half_d + 1.6, y), (half_d + 0.12, y)], 0.12)
    f.line("F.SilkS", [(land_x - 1.1, 3.2), (land_x - 1.1, 2.3)], 0.2)  # pin 1 mark
    f.courtyard(land_x - 0.875, -(mount_y + 1.25), half_d + 0.25, mount_y + 1.25)
    f.labels((0, -half_l - 2.2), (0, half_l + 2.4))
    return f


def cr2032_holder() -> Footprint:
    """Q&J CR2032-BS-6-1 (drawing QJ.BS-6, 2011-02-14). Housing 22.1 x 16, 31.3 over the pins, 5.5 high.

    PCB layout: two lands, 32 +0.2/-0 over their outer edges and 26 +0/-0.2 between them, 4.2 wide.
    The drawing marks the right-hand land "+" and the left "-" in its top view. Pad 1 is "+".
    """
    f = Footprint("BatteryHolder_QJ_CR2032-BS-6-1", "Q&J CR2032-BS-6-1 coin cell holder, SMD, drawing QJ.BS-6. "
                  "Pad 1 is the positive contact", "battery holder CR2032 coin cell")
    f.smd("1", 14.5, 0, 3.0, 4.2, "rect")
    f.smd("2", -14.5, 0, 3.0, 4.2, "rect")
    f.rect("F.Fab", -11.05, -8.0, 11.05, 8.0, 0.1)
    f.circle("F.Fab", 0, 0, 10.0, 0.1)  # the cell, 20 mm
    f.line("F.Fab", [(-15.65, -1.75), (-11.05, -1.75)], 0.1)
    f.line("F.Fab", [(-15.65, 1.75), (-11.05, 1.75)], 0.1)
    f.line("F.Fab", [(15.65, -1.75), (11.05, -1.75)], 0.1)
    f.line("F.Fab", [(15.65, 1.75), (11.05, 1.75)], 0.1)
    f.rect("F.SilkS", -11.17, -8.12, 11.17, 8.12, 0.12)
    f.line("F.SilkS", [(16.6, -3.4), (16.6, -2.4)], 0.15)  # "+" beside pad 1
    f.line("F.SilkS", [(16.1, -2.9), (17.1, -2.9)], 0.15)
    f.courtyard(-16.25, -8.25, 16.25, 8.25)
    f.labels((0, -9.3), (0, 9.4))
    return f


def encoder_ec11j() -> Footprint:
    """ALPS EC11J1525402, reflow rotary encoder with push switch. ALPS product sheet, "EC11 Series", 2018-03-30.

    Land drawing, tolerance 0.1: five terminal lands 1.3 x 2.4. A, C, B at x = -2.5, 0, +2.5 with
    their outer edges 8.5 from the shaft axis; D and E (the switch) at x = -2.5, +2.5 with outer
    edges 8.2 from it on the other side. Frame lands: four corner lands 2.5 x 3.2 (7.5 between
    them, 12.5 over them, 12.6 between and 19 over the two rows) and two side lands 3.75 x 4
    (9 between, 16.5 over). Holes: 2.1 on the axis and, for the EC11J15 type, 1.6 at 4.5 from
    it towards A, C, B. Four areas beside the side lands are marked "do not solder and wiring".

    Pads S1 and S2 are the drawing's D and E. The frame lands have no pad number and no net.
    The drawing is a 512 pixel image; the position of the four no-wiring areas along x could
    not be read and is an estimate.
    """
    f = Footprint("RotaryEncoder_Alps_EC11J15", "ALPS EC11J15 series reflow encoder with switch (EC11J1525402). "
                  "A, C, B at +y. S1 and S2 are the switch terminals D and E", "rotary encoder switch EC11J SMD")
    for num, x in (("A", -2.5), ("C", 0.0), ("B", 2.5)):
        f.smd(num, x, 8.5 - 1.2, 1.3, 2.4, "rect")
    for num, x in (("S1", -2.5), ("S2", 2.5)):
        f.smd(num, x, -(8.2 - 1.2), 1.3, 2.4, "rect")
    for sx in (-1, 1):
        for sy in (-1, 1):
            f.smd("", sx * 5.0, sy * 7.9, 2.5, 3.2, "rect")
        f.smd("", sx * 6.375, 0, 3.75, 4.0, "rect")
    f.npth(0, 0, 2.1)
    f.npth(0, 4.5, 1.6)
    for sx in (-1, 1):
        for sy in (-1, 1):
            xs, ys = sorted((sx * 3.75, sx * 6.25)), sorted((sy * 2.75, sy * 5.0))
            f.keepout([(xs[0], ys[0]), (xs[1], ys[0]), (xs[1], ys[1]), (xs[0], ys[1])], layers=("F.Cu",),
                      name="EC11J no wiring")
    f.rect("F.Fab", -5.85, -6.0, 5.85, 6.0, 0.1)
    f.circle("F.Fab", 0, 0, 3.0, 0.1)
    f.line("F.SilkS", [(-5.97, -6.1), (-5.97, -2.4)], 0.12)
    f.line("F.SilkS", [(-5.97, 2.4), (-5.97, 6.1)], 0.12)
    f.line("F.SilkS", [(5.97, -6.1), (5.97, -2.4)], 0.12)
    f.line("F.SilkS", [(5.97, 2.4), (5.97, 6.1)], 0.12)
    # 0.1 beyond the side lands, not the usual 0.25: the mechanical contract puts the module's pad
    # row 0.25 mm from the right-hand side land (encoder at x = 85.0, module at x = 103.0).
    f.courtyard(-8.35, -9.75, 8.35, 9.75)
    f.labels((0, -10.8), (0, 10.9), (0, -2.5))
    # KiCad's EC11E is the through-hole relative with the same 20 mm shaft. Stand-in for the STEP export.
    f.model("Rotary_Encoder.3dshapes/RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm.step")
    return f


def tact_6x6() -> Footprint:
    """XKB TS-1102S-C-M-B, 6 x 6 mm tactile switch, 10 mm high (code M). Drawing TS-1102S-C-M-B rev A1, 2025-06-03.

    Body 6.00 x 6.00, 3.30 high, actuator 3.50 diameter, 8.60 over the terminals. Lands 2.60 x 1.40:
    12.00 over and 6.80 between them along the terminals, 5.90 over and 3.10 between the two rows.
    Terminals 1 and 2 (one row) are joined inside, 3 and 4 (the other row) likewise; the switch is
    between the rows. Pad 1 is the drawing's 1 and 2, pad 2 its 3 and 4.
    """
    f = Footprint("SW_Push_XKB_TS-1102S", "XKB TS-1102S 6x6 mm SMD tactile switch (TS-1102S-C-M-B, 10 mm high). "
                  "Pad 1: terminals 1 and 2, pad 2: terminals 3 and 4", "tactile switch 6x6 SMD")
    for sx in (-1, 1):
        f.smd("1", sx * 4.7, -2.25, 2.6, 1.4, "rect")
        f.smd("2", sx * 4.7, 2.25, 2.6, 1.4, "rect")
    f.rect("F.Fab", -3.0, -3.0, 3.0, 3.0, 0.1)
    f.circle("F.Fab", 0, 0, 1.75, 0.1)
    f.line("F.SilkS", [(-3.12, -3.12), (3.12, -3.12)], 0.12)
    f.line("F.SilkS", [(-3.12, 3.12), (3.12, 3.12)], 0.12)
    f.line("F.SilkS", [(-3.12, -1.3), (-3.12, 1.3)], 0.12)
    f.line("F.SilkS", [(3.12, -1.3), (3.12, 1.3)], 0.12)
    f.courtyard(-6.25, -3.25, 6.25, 3.25)
    f.labels((0, -4.3), (0, 4.4))
    return f


def cap_lying() -> Footprint:
    """Rubycon 63ZLJ470M12.5X20 lying on its side, leads bent through 90 degrees. Rubycon ZLJ series sheet.

    Case 12.5 x 20: 13.0 diameter and 22.0 long at most, lead pitch 5.0, lead 0.6. The Rubycon PX part of the
    same case (21.5 long at most) fits the same holes. Holes 1.0 mm as the mechanical contract gives them.
    The can starts 4.25 from the lead holes, which is where the contract puts it. Pad 1 is positive.
    Drawn as if on the top side; the board places it on the bottom, which mirrors it.
    """
    f = Footprint("CP_Radial_D12.5mm_L20mm_P5.00mm_LyingDown", "Radial electrolytic capacitor 12.5 x 20 mm lying on "
                  "its side, 5.0 mm lead pitch (Rubycon 63ZLJ470M12.5X20). Pad 1 positive", "capacitor electrolytic "
                  "radial horizontal", kind="tht")
    f.tht("1", 0, -2.5, 2.0, 1.0, "rect")
    f.tht("2", 0, 2.5, 2.0, 1.0, "circle")
    x0, x1 = 4.25, 4.25 + 22.0
    f.rect("F.Fab", x0, -6.5, x1, 6.5, 0.1)
    f.line("F.Fab", [(0, -2.5), (x0, -2.5)], 0.1)
    f.line("F.Fab", [(0, 2.5), (x0, 2.5)], 0.1)
    f.rect("F.SilkS", x0 - 0.12, -6.62, x1 + 0.12, 6.62, 0.12)
    f.line("F.SilkS", [(1.3, -2.5), (x0 - 0.12, -2.5)], 0.12)
    f.line("F.SilkS", [(1.3, 2.5), (x0 - 0.12, 2.5)], 0.12)
    f.line("F.SilkS", [(-1.9, -3.0), (-1.9, -2.0)], 0.15)  # "+" beside pad 1
    f.line("F.SilkS", [(-2.4, -2.5), (-1.4, -2.5)], 0.15)
    f.text("F.SilkS", "+", x0 + 1.5, -5.0, 1.2)
    f.courtyard(-1.25, -6.75, x1 + 0.25, 6.75)
    f.labels((x0 + 11, -7.8), (x0 + 11, 7.9), (x0 + 11, 0))
    return f


def mounting_hole(top_keepout: float) -> Footprint:
    """3.4 mm unplated hole for an M3 screw, hardware/mechanical.yaml "holes".

    No copper. The courtyard on each side is the contract's keep-out for components: ``top_keepout``
    in diameter on the top (9.0 at holes A and B, 7.0 at C and D) and 10.0 on the bottom.
    """
    f = Footprint(f"MountingHole_3.4mm_M3_Keepout{top_keepout:g}mm", "3.4 mm unplated hole for M3, component keep-out "
                  f"{top_keepout:g} mm on top and 10 mm on the bottom (mechanical contract)", "mounting hole M3",
                  kind="other")
    import pcbnew
    f.fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_EXCLUDE_FROM_BOM)
    f.npth(0, 0, 3.4)
    f.circle("F.CrtYd", 0, 0, top_keepout / 2, 0.05)
    f.circle("B.CrtYd", 0, 0, 5.0, 0.05)
    f.circle("Cmts.User", 0, 0, 1.7, 0.1)
    f.labels((0, -top_keepout / 2 - 1.0), (0, top_keepout / 2 + 1.0))
    return f


def esp32_module() -> Footprint:
    """ESP32-S3-WROOM-1. Espressif module datasheet v1.8, figure 10, recommended PCB land pattern.

    KiCad's own footprint was checked against that figure and is used unchanged for its pads: module
    18 x 25.5, lands 1.5 x 0.9 at 1.27 pitch on x = +/-8.75, first row 7.49 below the antenna end,
    twelve lands along the bottom starting 2.015 from the side, ground pad of nine 0.9 squares
    centred 7.5 from the left edge and 10.29 from the bottom edge. Its keep-out, 15 mm around the
    6 mm antenna end, is on the outer layers only. This copy puts it on every copper layer.
    """
    f = Footprint("ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1 module, land pattern of datasheet figure 10, antenna "
                  "keep-out on all copper layers", "ESP32-S3 WiFi BLE module", from_stock="RF_Module:ESP32-S3-WROOM-1")
    zones = f.zones()
    if len(zones) != 1:
        raise SystemExit("the stock ESP32-S3-WROOM-1 footprint no longer has exactly one keep-out")
    import pcbnew
    zones[0].SetLayerSet(pcbnew.LSET.AllCuMask())
    zones[0].SetZoneName("Antenna keep-out")
    # The stock courtyard is the module body 0.25 beyond the pads plus the keep-out bar. Redrawn with
    # 0.1 beyond the pads at the sides, because the contract puts the encoder's side land 0.25 mm
    # from the pad row. The bar and the bottom edge are as in the stock footprint.
    if f.clear("F.CrtYd") != 8:
        raise SystemExit("the stock ESP32-S3-WROOM-1 courtyard no longer has eight lines")
    f.line("F.CrtYd", [(-24.0, -27.75), (24.0, -27.75), (24.0, -6.75), (9.6, -6.75), (9.6, 13.45),
                       (-9.6, 13.45), (-9.6, -6.75), (-24.0, -6.75), (-24.0, -27.75)], 0.05)
    return f


def opto_smdip6() -> Footprint:
    """onsemi MOC3023SR2M, 6-pin surface-mount DIP. onsemi case 646BY, "PDIP6 8.51x6.35, 2.54P", issue A, 2019.

    KiCad's SMDIP-6_W9.53mm with a larger courtyard. Its lands agree with onsemi's (footprints.md,
    section 2), but its courtyard is 8.17 long, and the drawing gives the body, dimension D, as
    8.13 to 8.89. Two parts placed courtyard to courtyard could touch. The courtyard here is the
    largest body, 8.89, plus 0.25 at each end. Height, dimension A, is 4.80 at most.
    """
    f = Footprint("SMDIP-6_onsemi_646BY", "onsemi 6-pin SMD DIP, case 646BY (MOC3023SR2M). Lands of KiCad's "
                  "SMDIP-6_W9.53mm, courtyard for the 8.89 mm maximum body", "optocoupler SMD DIP-6",
                  from_stock="Package_DIP:SMDIP-6_W9.53mm")
    if f.clear("F.CrtYd") == 0:
        raise SystemExit("the stock SMDIP-6_W9.53mm footprint has no courtyard to replace")
    f.courtyard(-6.04, -4.70, 6.04, 4.70)
    return f


def footprints() -> list[Footprint]:
    return [terminal_header(2), terminal_header(4), terminal_header(7), fpc_12(), cr2032_holder(), encoder_ec11j(),
            tact_6x6(), cap_lying(), mounting_hole(9.0), mounting_hole(7.0), esp32_module(), opto_smdip6()]


if __name__ == "__main__":
    write_library(Path(sys.argv[1]), footprints())
