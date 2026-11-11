"""What the design document said about each fault before review A2.

Transcribed by hand from sections 7.3 and 8.5 of
docs/design/hardware-design.md as merged in pull request #20 (commit
5b0c4bf). `failsafe.py compare` sets each row against the simulation of the
same netlist.

A row: fault id in the simulation -> (layer the author says is lost, test
the author says finds it). Layer: "" none, "A" gate drive stays (detector A
or what it drives stuck on), "B" relay path stays (detector B, Q1, Q2 or K1
stuck on). Test: the step names of run.py, "" for "not found", "any" where
the author names a symptom that is not one step.
"""

A, B, NONE = "A", "B", ""
IDLE, HIGH, START, WELD, RELAY, RUN, STOP, CURRENT = "1 idle", "2 pin-high", "3 start", "5 weld check", "6 relay", "7 run", "9 stop", "valve current"

AUTHOR: dict[str, tuple[str, str | None]] = {}


def rows(layer: str, test: str | None, *fids: str) -> None:
    for fid in fids:
        AUTHOR[fid] = (layer, test)


# ---- section 8.5, "Detector A and the logic behind it" -------------------
rows(NONE, START, "R29.open", "C22.open", "D7.1-3.short", "D7.2-3.short", "D7.1.open", "D7.2.open", "D7.3.open", "C23.short", "C24.short", "R35.open")
rows(NONE, START, "C23.open")            # "expander does not stay configured"
rows(NONE, "", "R32.open", "R33.open", "R36.open")
rows(NONE, STOP, "R30.open", "R91.open")  # slow, not stuck: "gate drive off after 1.7 s"
rows(A, STOP, "R31.open")                 # "R31 opens while drive is enabled"; from power-up: never enables
rows(A, IDLE, "R34.open")
rows(A, HIGH, "C22.short")
rows(A, IDLE, "U6.out1_off", "U6.3.open", "U6.12.open")
rows(A, IDLE, "U7.y3_low", "U7.y4_high")
rows(NONE, START, "U7.y3_high", "U7.y4_low")
rows(NONE, "", "U7.y2_high")
rows(NONE, "", "Q3.S-D.short")
rows(NONE, "", "R25.open", "R26.open", "C21.open")
rows(B, IDLE, "U5.reset_ignored")         # "RLY_ON stays high, so Q1 stays on. K1 opens through Q2"
rows(NONE, RUN, "U5.z1_low")
rows(B, WELD, "U5.rly_high", "R12.open")
rows(NONE, "", "RN1.1.open", "R96.open")
# ---- section 8.5, "Detector B, the relay and its drive" ------------------
rows(NONE, START, "R89.open", "C51.open", "D22.1.open", "D22.2.open", "D22.3.open", "D22.1-3.short", "C50.short")
rows(NONE, START, "D22.2-3.short", "C50.open")
rows(NONE, STOP, "R14.open", "R90.open")
rows(B, HIGH, "C51.short")
rows(B, IDLE, "Q2.S-D.short", "Q2.G-D.short")
rows(NONE, START, "Q2.G-S.short", "Q2.G.open.hold", "Q2.G.open.hi", "Q2.G.open.lo")
rows(B, WELD, "Q1.S-D.short")
rows(NONE, CURRENT, "Q1.D.open", "Q1.S.open", "R11.open")
rows(B, WELD, "K1.welded")
rows(NONE, CURRENT, "K1.coil.open")
rows(NONE, "", "D5.open")               # "weld check, or the idle test, when one of them has failed": not by itself
rows(NONE, "any", "D5.short")
rows(NONE, IDLE, "R93.open", "R94.open", "R95.short")   # false alarm on the safe side
rows(NONE, "", "R95.open")
rows(NONE, START, "R92.open")
# ---- section 8.5, "Resistor shorts", by inspection in the document -------
rows(NONE, "", "R29.short", "R89.short", "R31.short", "R92.short", "R94.short")
rows(NONE, START, "R30.short", "R91.short", "R14.short", "R90.short", "R35.short", "R33.short", "R34.short")
rows(A, IDLE, "R32.short", "R36.short")
rows(NONE, "any", "R93.short", "R12.short")
rows(NONE, "", "R11.short")
# ---- section 8.5, "What A and B share" and the first table ----------------
# "No effect" on the valves; the document makes no claim about detection.
rows(NONE, None, "U4.35.drive.hi", "U4.35.drive.lo", "U4.7.drive.hi", "U4.7.drive.lo", "U4.33.drive.hi", "U4.33.drive.lo")
rows(B, "any", "KA~AC_ZC.net")            # "can hold B on and cannot enable A: one detector, not both"
rows("AB", "", "KA~TFT_BL.0.01", "KA~TFT_BL.1000")   # "would run both pumps. Must be made impossible by layout"
rows(NONE, None, "rail.3V3.high", "rail.V_BUCK.off")
rows(NONE, RELAY, "output.short")
