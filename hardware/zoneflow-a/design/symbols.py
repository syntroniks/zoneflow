# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Project-local schematic symbols for ZoneFlow rev A.

``write()`` generates ``lib/ZoneFlow.kicad_sym`` from this file. A symbol is
here for one of three reasons: KiCad's library has none for the part, the
stock symbol prints pin numbers on top of each other, or the stock symbol
numbers its units differently from the datasheet the design document quotes.

Pin numbers and names are from the datasheet named beside each symbol. The
pin table of every part was checked against its package drawing in hardware
review A1, section 7.
"""

from pathlib import Path

from hwflow import symgen
from hwflow.symgen import box, derive, power

LIB = "ZoneFlow"

IN, OUT, BI, PAS, PWR, OC = "input", "output", "bidirectional", "passive", "power_in", "open_collector"


def esp32_s3_wroom_1():
    # Espressif ESP32-S3-WROOM-1 datasheet v1.8, table 3-1. Pins grouped by what this board uses them for.
    def io(pin, gpio):
        return (str(pin), f"IO{gpio}", BI)

    left = [
        ("2", "3V3", PWR), ("3", "EN", IN), None,
        io(27, 0), io(15, 3), io(26, 45), io(16, 46), None,
        ("13", "IO19/D-", BI), ("14", "IO20/D+", BI), None,
        ("37", "TXD0", BI), ("36", "RXD0", BI), None,
        io(39, 1), io(38, 2), io(7, 7), None,
        io(31, 38), io(33, 40), io(34, 41), io(35, 42), None,
        ("28", "IO35", PAS), ("29", "IO36", PAS), ("30", "IO37", PAS),
    ]
    right = [
        io(12, 8), io(17, 9), None,
        io(24, 47), io(25, 48), None,
        io(18, 10), io(19, 11), io(20, 12), io(21, 13), io(22, 14), io(23, 21), None,
        io(8, 15), io(9, 16), io(10, 17), io(11, 18), io(32, 39), None,
        io(4, 4), io(5, 5), io(6, 6),
    ]
    bottom = [(["1", "40", "41"], "GND", PWR)]
    return box("ESP32-S3-WROOM-1", "U", "ESP32-S3-WROOM-1", left, right, bottom=bottom, min_width=30.48,
               footprint="ZoneFlow:ESP32-S3-WROOM-1",
               datasheet="https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf",
               description="ESP32-S3 WiFi and Bluetooth LE module, PCB antenna")


def tca9539():
    # TI SCPS202C, pin functions table, TSSOP-24 (PW).
    left = [("22", "SCL", IN), ("23", "SDA", BI), None, ("3", "~{RESET}", IN), ("1", "~{INT}", OC), None,
            ("21", "A0", IN), ("2", "A1", IN)]
    right = [(str(4 + i), f"P0{i}", BI) for i in range(8)] + [None] + [(str(13 + i), f"P1{i}", BI) for i in range(8)]
    return box("TCA9539PWR", "U", "TCA9539PWR", left, right, top=[("24", "VCC", PWR)], bottom=[("12", "GND", PWR)],
               min_width=30.48, footprint="Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm",
               datasheet="https://www.ti.com/lit/ds/symlink/tca9539.pdf",
               description="16-bit I2C I/O expander with reset and interrupt, TSSOP-24")


def tps2553():
    # TI SLVS841, DBV package: 1 IN, 2 GND, 3 EN (active high on the TPS2553), 4 FAULT, 5 ILIM, 6 OUT.
    return box("TPS2553DBV", "U", "TPS2553DBVR", [("1", "IN", PWR), None, ("3", "EN", IN), ("4", "~{FAULT}", OC)],
               [("6", "OUT", "power_out"), None, None, ("5", "ILIM", PAS)], bottom=[("2", "GND", PWR)],
               min_width=20.32, footprint="Package_TO_SOT_SMD:SOT-23-6",
               datasheet="https://www.ti.com/lit/ds/symlink/tps2553.pdf",
               description="Adjustable current-limited power switch, SOT-23-6")


def tlv803e():
    # TI TLV803E data sheet, DBZ (SOT-23) pinout without R or V suffix: 1 GND, 2 RESET, 3 VDD.
    return box("TLV803EA30DBZ", "U", "TLV803EA30DBZR", [("3", "VDD", PWR)], [("2", "~{RESET}", OC)],
               bottom=[("1", "GND", PWR)], min_width=20.32, footprint="Package_TO_SOT_SMD:SOT-23",
               datasheet="https://www.ti.com/lit/ds/symlink/tlv803e.pdf",
               description="3.0 V supply supervisor, open-drain active-low reset, 200 ms delay")


def usb_c_16p():
    # HRO TYPE-C-31-M-12 drawing, pin table. Every contact is its own pin, so no pin numbers are stacked.
    right = [(["A4", "A9", "B4", "B9"], "VBUS", PAS), None, (["A7", "B7"], "D-", PAS), (["A6", "B6"], "D+", PAS),
             None, ("A5", "CC1", PAS), ("B5", "CC2", PAS), None, ("A8", "SBU1", PAS), ("B8", "SBU2", PAS)]
    bottom = [(["A1", "A12", "B1", "B12"], "GND", PAS), None, ("SH", "SHIELD", PAS)]
    return box("USB_C_Receptacle_16P", "J", "USB-C 16P", [], right, bottom=bottom, min_width=17.78,
               footprint="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
               description="USB Type-C receptacle, USB 2.0, 16 contacts")


def fpc_12():
    # XUNPU FPC-05F-12PH20, 12 contacts. Pin names are the panel's (HS20HS072RX datasheet, interface
    # table), because the panel tail is what plugs in. Grouped by function with room between the groups.
    left = [("2", "CS", PAS), ("3", "RS", PAS), ("4", "SCL", PAS), ("5", "SDA", PAS), ("6", "RST", PAS), None, None,
            ("8", "IOVCC", PAS), ("9", "VCC", PAS), None, None, None, None, None, None, None,
            ("10", "LEDA", PAS), ("11", "LEDK", PAS), None, ("7", "NC", "no_connect")]
    return box("FPC_12_TFT", "J", "FPC 12P 0.5", left, [], bottom=[(["1", "12"], "GND", PAS)], min_width=15.24,
               footprint="ZoneFlow:FPC_XUNPU_FPC-05F-12PH20",
               description="12-way 0.5 mm FPC connector for the display tail; pins named as on the panel")


def tft_panel():
    # Not a board part: the panel plugs into the FPC connector at final assembly. Listed for the BOM only.
    return box("TFT_Panel_FPC12", "DS", "TFT panel", min_width=30.48, title="TFT panel, 12-way FPC tail",
               description="Display panel with a 12-way 0.5 mm FPC tail; no footprint on the board")


def symbols():
    return [
        esp32_s3_wroom_1(), tca9539(), tps2553(), tlv803e(), usb_c_16p(), fpc_12(), tft_panel(),
        # TI SLCS006S numbers the comparators 1 to 4 with outputs on pins 1, 2, 14 and 13. KiCad's LM2901
        # has them as units B, A, D, C. The design document says U6A to U6D in TI's order, so the units are
        # renumbered: A = pins 1, 6, 7; B = 2, 4, 5; C = 14, 8, 9; D = 13, 10, 11.
        derive("Comparator:LM2901", "LM2901", unit_map={1: 2, 2: 1, 3: 4, 4: 3},
               footprint="Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
               datasheet="https://www.ti.com/lit/ds/symlink/lm2901.pdf",
               description="Quad comparator, open-collector outputs, SOIC-14. Units in the order of the TI datasheet"),
        # Nexperia 74HC132: same pinout as the 74LS132 KiCad draws, Schmitt-trigger inputs.
        derive("74xx:74LS132", "74HC132", footprint="Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
               datasheet="https://assets.nexperia.com/documents/data-sheet/74HC_HCT132.pdf",
               description="Quad 2-input NAND, Schmitt-trigger inputs, SO14"),
        # ST DocID030319, SOT-223: 1 GND, 2 VOUT, 3 VIN, tab VOUT. Same pinout as the AMS1117 symbol.
        derive("Regulator_Linear:AMS1117-3.3", "LDL1117S33R", footprint="Package_TO_SOT_SMD:SOT-223-3_TabPin2",
               datasheet="https://www.st.com/resource/en/datasheet/ldl1117.pdf",
               description="1.2 A low-dropout regulator, 3.3 V fixed, SOT-223"),
        # ST DocID7472 rev 12, DPAK: lead 1 A1 (T1), tab and lead 2 A2 (T2), lead 3 gate.
        derive("Triac_Thyristor:BT136-600", "T810-600B", footprint="Package_TO_SOT_SMD:TO-252-2",
               datasheet="https://www.st.com/resource/en/datasheet/t810.pdf", pin_types={"3": PAS},
               description="8 A 600 V three-quadrant triac, DPAK. Pin 1 A1, pin 2 and tab A2, pin 3 gate"),
        # NXP PCF8563 rev 11.1, table 3. KiCad's symbol draws a clock wedge on CLKOUT that runs into the
        # pin name; this copy draws a plain pin.
        derive("Timer_RTC:PCF8563T", "PCF8563T", footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
               datasheet="https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf", pin_shapes={"7": "line"},
               description="Real-time clock and calendar, I2C, SO8"),
        power("3V3", "3.3 V logic rail"), power("V5", "5 V bus after the OR-ing diodes"),
        power("V_BUCK", "Buck converter output, 5.28 V, present only with 24 VAC"),
        power("VIN_DC", "Rectified unregulated supply, 26 to 44 V"), power("VBUS", "USB VBUS"),
        power("V_DRV", "Opto LED supply, on only while the fail-safe circuit allows drive"),
    ]


def write(board_dir: Path) -> None:
    symgen.write_lib(Path(board_dir) / "lib" / f"{LIB}.kicad_sym", symbols())
