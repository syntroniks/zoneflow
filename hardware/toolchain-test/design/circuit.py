# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Toolchain test circuit. Not the ZoneFlow board: part values are plausible, not designed.

Every LCSC number below was checked against an LCSC or JLCPCB listing; the
sources are in parts-sources.md beside this file.

Coordinates are millimetres on the schematic page, on the 1.27 mm grid.
``G`` is 2.54 mm, so ``(20 * G, 30 * G)`` is the point (50.8, 76.2).
"""

from hwflow.design import DOWN, LEFT, UP, Design

G = 2.54

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"


def rails(s):
    """Power nets used on a sheet. The net class applies to the whole net."""
    return (s.power("GND", netclass="Power"), s.power("+3V3", netclass="Power"),
            s.power("VBUS", netclass="Power"))


def power_usb(s):
    gnd, v33, vbus = rails(s)
    dp = s.port("USB_DP", "bidirectional", side="right")
    dm = s.port("USB_DM", "bidirectional", side="right")
    cc1, cc2 = s.net("CC1"), s.net("CC2")

    s.frame(10 * G, 14 * G, 46 * G, 46 * G, "USB-C receptacle, CC pull-downs, ESD")
    j1 = s.part("J1", "Connector:USB_C_Receptacle_USB2.0_16P", at=(16 * G, 30 * G), value="USB-C 16P",
                footprint="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", lcsc="C165948")
    s.connect(vbus, j1["VBUS"], stub=2 * G)
    s.connect(cc1, j1["CC1"], stub=2 * G)
    s.connect(cc2, j1["CC2"], stub=2 * G)
    s.connect(dp, j1["A6"], j1["B6"], stub=2 * G)
    s.connect(dm, j1["A7"], j1["B7"], stub=2 * G)
    s.connect(gnd, j1["GND"], j1["SHIELD"])
    s.nc(j1["SBU1"], j1["SBU2"])

    r1 = s.part("R1", "Device:R", at=(32 * G, 22 * G), value="5k1", footprint=R0603, lcsc="C23186")
    r2 = s.part("R2", "Device:R", at=(38 * G, 22 * G), value="5k1", footprint=R0603, lcsc="C23186")
    s.connect(cc1, r1[1])
    s.connect(cc2, r2[1])
    s.connect(gnd, r1[2], r2[2])

    u1 = s.part("U1", "Power_Protection:USBLC6-2SC6", at=(36 * G, 36 * G), lcsc="C7519",
                ref_at=(37 * G, 33 * G), value_at=(37 * G, 34 * G))
    s.connect(dp, u1[1], u1[6], stub=2 * G)
    s.connect(dm, u1[3], u1[4], stub=2 * G)
    s.connect(vbus, u1["VBUS"])
    s.connect(gnd, u1["GND"])

    s.frame(54 * G, 14 * G, 100 * G, 46 * G, "3.3 V regulator")
    u2 = s.part("U2", "Regulator_Linear:AMS1117-3.3", at=(76 * G, 26 * G), lcsc="C6186")
    c1 = s.part("C1", "Device:C", at=(64 * G, 30 * G), value="10u", footprint=C0805, lcsc="C15850")
    c2 = s.part("C2", "Device:C", at=(88 * G, 30 * G), value="22u", footprint=C0805, lcsc="C45783")
    s.wire(c1[1], u2["VI"], route="vh", net=vbus)
    s.wire((60 * G, 26 * G), (64 * G, 26 * G), net=vbus)
    s.label(vbus, (60 * G, 26 * G), UP)
    s.wire(u2["VO"], c2[1], route="hv", net=v33)
    s.wire((88 * G, 26 * G), (92 * G, 26 * G), net=v33)
    s.label(v33, (92 * G, 26 * G), UP)
    s.connect(gnd, u2["GND"], c1[2], c2[2])

    # ERC needs a power source on nets fed through a connector.
    f1 = s.part("#FLG1", "power:PWR_FLAG", at=(60 * G, 38 * G))
    f2 = s.part("#FLG2", "power:PWR_FLAG", at=(68 * G, 38 * G))
    s.connect(vbus, f1[1])
    s.connect(gnd, f2[1])
    s.text("USB powers the logic rail only.", (56 * G, 44 * G))


def mcu(s):
    gnd, v33, _ = rails(s)
    dp = s.port("USB_DP", "bidirectional", side="left")
    dm = s.port("USB_DM", "bidirectional", side="left")
    z1 = s.port("Z1", "output")
    z2 = s.port("Z2", "output")
    en, boot = s.net("EN"), s.net("BOOT")
    sda, scl = s.net("SDA"), s.net("SCL")

    u1 = s.part("U1", "RF_Module:ESP32-S3-WROOM-1", at=(58 * G, 34 * G), value="ESP32-S3-WROOM-1-N8",
                lcsc="C2913198", ref_at=(53 * G, 22.5 * G), value_at=(59 * G, 22.5 * G))
    s.connect(v33, u1["3V3"])
    s.connect(gnd, u1.pins("GND"))
    s.connect(en, u1["EN"], stub=2 * G)
    s.connect(boot, u1["IO0"], stub=2 * G)
    s.connect(z1, u1["IO4"], stub=2 * G)
    s.connect(z2, u1["IO5"], stub=2 * G)
    s.connect(sda, u1["IO8"], stub=2 * G)
    s.connect(scl, u1["IO9"], stub=2 * G)
    s.connect(dm, u1["USB_D-"], stub=2 * G)
    s.connect(dp, u1["USB_D+"], stub=2 * G)
    s.nc_rest(u1)

    s.frame(8 * G, 14 * G, 40 * G, 36 * G, "Reset and boot")
    r1 = s.part("R1", "Device:R", at=(16 * G, 21 * G), value="10k", footprint=R0603, lcsc="C25804")
    c3 = s.part("C3", "Device:C", at=(16 * G, 28 * G), value="1u", footprint=C0603, lcsc="C15849")
    sw1 = s.part("SW1", "Switch:SW_Push", at=(24 * G, 28 * G), rot=90, value="RESET",
                 footprint="Button_Switch_SMD:SW_SPST_TL3342", lcsc="C2886898")
    s.connect(v33, r1[1])
    s.wire(r1[2], c3[1], net=en)
    s.wire(sw1[2], (16 * G, 24.5 * G), route="vh", net=en)
    s.wire((16 * G, 24.5 * G), (12 * G, 24.5 * G), net=en)
    s.label(en, (12 * G, 24.5 * G), LEFT)
    s.connect(gnd, c3[2], sw1[1])
    sw2 = s.part("SW2", "Switch:SW_Push", at=(34 * G, 28 * G), rot=90, value="BOOT",
                 footprint="Button_Switch_SMD:SW_SPST_TL3342", lcsc="C2886898")
    s.connect(boot, sw2[2])
    s.connect(gnd, sw2[1])

    s.frame(8 * G, 42 * G, 40 * G, 60 * G, "Decoupling")
    c1 = s.part("C1", "Device:C", at=(14 * G, 51 * G), value="10u", footprint=C0805, lcsc="C15850")
    c2 = s.part("C2", "Device:C", at=(22 * G, 51 * G), value="100n", footprint=C0603, lcsc="C14663")
    s.connect(v33, c1[1], c2[1])
    s.connect(gnd, c1[2], c2[2])

    s.frame(80 * G, 14 * G, 108 * G, 40 * G, "I2C expansion header")
    r3 = s.part("R3", "Device:R", at=(86 * G, 22 * G), value="4k7", footprint=R0603, lcsc="C23162")
    r4 = s.part("R4", "Device:R", at=(92 * G, 22 * G), value="4k7", footprint=R0603, lcsc="C23162")
    s.connect(v33, r3[1], r4[1])
    s.connect(sda, r3[2])
    s.connect(scl, r4[2])
    # Mirrored top to bottom so GND (pin 1) is the lowest pin: SCL, SDA, +3V3, GND.
    j1 = s.part("J1", "Connector_Generic:Conn_01x04", at=(102 * G, 32 * G), mirror="x", value="I2C",
                footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical", lcsc="C2905435")
    s.connect(scl, j1[4], stub=2 * G)
    s.connect(sda, j1[3], stub=2 * G)
    s.wire(j1[2], (92 * G, 31 * G), route="hv", net=v33)
    s.label(v33, (92 * G, 31 * G), UP)
    s.wire(j1[1], (97 * G, 35 * G), route="hv", net=gnd)
    s.label(gnd, (97 * G, 35 * G), DOWN)


def ac_channel(s):
    """One isolated 24 VAC output. Instantiated once per zone from the root sheet."""
    gnd, _, _ = rails(s)
    ctrl = s.port("CTRL", "input")
    hot = s.port("AC_HOT", "passive", netclass="AC24")
    com = s.port("AC_COM", "passive", netclass="AC24")
    out = s.net("OUT", netclass="AC24")
    gate = s.net("GATE", netclass="AC24")
    drive = s.net("DRIVE", netclass="AC24")
    snub = s.net("SNUB", netclass="AC24")

    y = 29 * G  # main signal row
    r1 = s.part("R1", "Device:R", at=(23 * G, y), rot=90, value="220R", footprint=R0603, lcsc="C22962")
    u1 = s.part("U1", "Relay_SolidState:MOC3023M", at=(30 * G, y + G), value="MOC3023",
                footprint="Package_DIP:SMDIP-6_W9.53mm", lcsc="C115469",
                ref_at=(28 * G, y - 1.25 * G), value_at=(28 * G, y + 3.25 * G))
    s.wire(r1[2], u1[1])
    s.wire(r1[1], (19 * G, y), net=ctrl)
    s.label(ctrl, (19 * G, y), LEFT)
    s.wire(u1[2], (26 * G, y + 4 * G), route="hv", net=gnd)
    s.label(gnd, (26 * G, y + 4 * G), DOWN)
    s.nc(u1[3], u1[5])

    r2 = s.part("R2", "Device:R", at=(23 * G, y + 9 * G), rot=90, value="1k", footprint=R0603, lcsc="C21190")
    d1 = s.part("D1", "Device:LED", at=(28 * G, y + 9 * G), rot=180, value="GREEN",
                footprint="LED_SMD:LED_0603_1608Metric", lcsc="C72043")
    s.wire(r2[2], d1["A"])
    s.wire(r2[1], (20.5 * G, y), route="hv", net=ctrl)
    s.wire(d1["K"], (31 * G, y + 10 * G), route="hv", net=gnd)
    s.label(gnd, (31 * G, y + 10 * G), DOWN)

    r3 = s.part("R3", "Device:R", at=(37 * G, y), rot=90, value="180R", footprint=R0603, lcsc="C22828")
    q1 = s.part("Q1", "Triac_Thyristor:Z0103MN", at=(47 * G, y + 3 * G), footprint="Package_TO_SOT_SMD:SOT-223",
                lcsc="C163595", ref_at=(48.5 * G, y + 2.5 * G), value_at=(48.5 * G, y + 3.5 * G))
    s.wire(u1[6], r3[1], net=drive)
    s.wire(r3[2], q1["A2"], route="hv", net=hot)
    s.wire((47 * G, y), (47 * G, y - 3 * G), net=hot)
    s.label(hot, (47 * G, y - 3 * G), UP)
    s.wire(u1[4], q1["G"], route="hv", net=gate)

    r4 = s.part("R4", "Device:R", at=(55 * G, y + 1.5 * G), value="100R", footprint="Resistor_SMD:R_1206_3216Metric",
                lcsc="C17901")
    c1 = s.part("C1", "Device:C", at=(55 * G, y + 5.5 * G), value="10n", footprint=C0805, lcsc="C1710")
    s.wire((47 * G, y), r4[1], route="hv", net=hot)
    s.wire(r4[2], c1[1], net=snub)

    yo = y + 8 * G  # switched output row
    s.wire(q1["A1"], (47 * G, yo), net=out)
    s.wire((47 * G, yo), (70 * G, yo), net=out)
    s.wire(c1[2], (55 * G, yo), net=out)
    s.label(out, (48 * G, yo))
    d2 = s.part("D2", "Device:D_TVS", at=(62 * G, yo + 3 * G), rot=90, value="SMBJ33CA", footprint="Diode_SMD:D_SMB",
                lcsc="C78420")
    j1 = s.part("J1", "Connector:Screw_Terminal_01x02", at=(72 * G, yo), value="VALVE",
                footprint="TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
                lcsc="C474952")
    s.wire(d2[2], (62 * G, yo), net=out)
    s.wire((70 * G, yo), j1[1], net=out)
    yc = yo + 6 * G  # common return row
    s.wire(d2[1], (62 * G, yc), net=com)
    s.wire(j1[2], (68 * G, yc), route="hv", net=com)
    s.wire((56 * G, yc), (68 * G, yc), net=com)
    s.label(com, (56 * G, yc), LEFT)

    s.text("Opto-triac drives the gate; the R-C pair is a snubber across the triac; D2 clamps the valve line.",
           (19 * G, y - 6 * G))


def build() -> Design:
    d = Design("toolchain_test", "ZoneFlow hardware toolchain test", rev="0.1", date="2026-11-10",
               company="ZoneFlow (open hardware, CERN-OHL-P-2.0)")
    usb = d.sheet("power_usb", "USB-C input and 3.3 V regulator", build=power_usb)
    esp = d.sheet("mcu", "ESP32-S3 module, reset, boot, I2C", build=mcu)
    chan = d.sheet("ac_channel", "Isolated 24 VAC output channel", build=ac_channel)

    r = d.root
    dp, dm = r.net("USB_DP"), r.net("USB_DM")
    z1, z2 = r.net("Z1"), r.net("Z2")
    hot = r.net("AC_24V", netclass="AC24")
    com = r.net("AC_COM", netclass="AC24")

    r.instantiate(usb, "POWER", at=(12 * G, 16 * G), width=14 * G, stub=4 * G, ref_offset=10,
                  connect={"USB_DP": dp, "USB_DM": dm})
    r.instantiate(esp, "MCU", at=(46 * G, 16 * G), width=14 * G, stub=4 * G, ref_offset=30,
                  connect={"USB_DP": dp, "USB_DM": dm, "Z1": z1, "Z2": z2})
    r.instantiate(chan, "CH1", at=(84 * G, 16 * G), width=14 * G, stub=4 * G, ref_offset=100,
                  connect={"CTRL": z1, "AC_HOT": hot, "AC_COM": com})
    r.instantiate(chan, "CH2", at=(84 * G, 30 * G), width=14 * G, stub=4 * G, ref_offset=200,
                  connect={"CTRL": z2, "AC_HOT": hot, "AC_COM": com})

    r.frame(10 * G, 42 * G, 44 * G, 58 * G, "24 VAC input")
    j1 = r.part("J1", "Connector:Screw_Terminal_01x02", at=(30 * G, 50 * G), mirror="y", value="24VAC",
                footprint="TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
                lcsc="C474952")
    r.connect(hot, j1[1], stub=2 * G)
    r.connect(com, j1[2], stub=2 * G)

    r.frame(50 * G, 42 * G, 74 * G, 58 * G, "Mechanical")
    for i, x in enumerate((56, 64)):
        r.part(f"H{i + 1}", "Mechanical:MountingHole", at=(x * G, 50 * G), value="M3",
               footprint="MountingHole:MountingHole_3.2mm_M3", in_bom=False)
    r.text("Each sheet instance adds an offset to its references: POWER 10, MCU 30, CH1 100, CH2 200.",
           (12 * G, 10 * G))
    r.text("CH1 and CH2 are two instances of one file, ac_channel.kicad_sch.", (12 * G, 12 * G))
    return d
