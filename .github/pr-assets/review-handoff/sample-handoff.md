# ZoneFlow rev A: hand-off of the pre-order review, 2026-11-11

The review walkthrough of the ZoneFlow documentation wrote this file in the owner's browser. It holds the owner's
decisions on the 47 steps of the pre-order review of the revision A board. It is complete in itself: give it to a
coding agent that has the repository.

**31 pass, 4 accepted, 5 fail, 0 rejected, 6 not sure, 1 not answered.**

## For the agent: read this first

- **Repository:** `syntroniks/zoneflow`. **Board:** revision A, `hardware/zoneflow-a/zoneflow-a.kicad_pcb`.
- **What was reviewed:** the board file with SHA-256 `dc9a8c8055d5005cff9c28093b499e9143985e990f8f2122e0469c5dfdc7f1cf`.
- **The walkthrough page was built from commit** `fd6bfc462dc3a061607a085f14156c3c83aea4f4 with local changes`.
- **Rules:** read `CLAUDE.md` at the root of the repository before anything else and follow it: the Git rules, the
  honesty rules and the hardware flow. Then read `docs/design/system-spec.md`. No board has been made. Do not write
  that anything works or is tested unless you ran the check, and name the check.

Do these in order.

1. **Check that you have the board that was reviewed.** `sha256sum hardware/zoneflow-a/zoneflow-a.kicad_pcb` must give the hash above.
   If it does not, check out the commit that has it, or stop and say so. Do not act on a review of another board.
2. **Record the review.** Save this file in the repository's root and run
   `uv run --project tools/reviewwalk reviewwalk record zoneflow-rev-a-review-handoff-2026-11-11.md`. It checks the decisions against
   the schema and the board, and writes `docs/design/reviews/2026-11-11-pre-order.md` with the JSON beside it. Commit
   those in a pull request of their own, without this file. Then post a comment on issue #33 with the
   counts and with the failed, rejected and unsure steps.
3. **For each step under "Needs a change"** open one pull request with that one change. Use the remedy the owner
   chose. Where the owner chose none, propose one in the pull request and say why. The files named are where the change
   is made. Never edit a generated file by hand. A change to the board needs `make` in `hardware/zoneflow-a/` and
   the generated files committed with it, the pictures in `output/review/` among them. Name the step in the pull request.
4. **For each step under "Not sure" and "Not answered"** ask the owner on issue #33. Do not guess,
   and change nothing for these steps.
5. **Change nothing for a passed or accepted step.**
6. **Never order boards,** and do not change any text that tells readers not to order. Ordering is the owner's decision.
7. **After your changes are merged** the board file has another hash. Tell the owner which steps to look at again. The
   walkthrough page takes this file back in and warns that the board has changed.

The JSON follows `docs/hardware/review/handoff.schema.json`, version 1. Positions are in millimetres from the lower left
corner of the board seen from the component side, x to the right and y up.

# Decisions as data

```json
{
 "schema": "zoneflow-review-handoff",
 "schema_version": 1,
 "repository": "syntroniks/zoneflow",
 "issue": 33,
 "board": {
  "revision": "A",
  "file": "hardware/zoneflow-a/zoneflow-a.kicad_pcb",
  "sha256": "dc9a8c8055d5005cff9c28093b499e9143985e990f8f2122e0469c5dfdc7f1cf"
 },
 "site_commit": "fd6bfc462dc3a061607a085f14156c3c83aea4f4 with local changes",
 "reviewed_on": "2026-11-11",
 "summary": {
  "steps": 47,
  "pass": 31,
  "accepted": 4,
  "fail": 5,
  "rejected": 0,
  "not_sure": 6,
  "not_answered": 1
 },
 "decisions": [
  {
   "step": "k-open",
   "title": "Open the board and run the design rule check yourself",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-valve",
   "title": "Look at one valve output on all four layers",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-ka-pin10",
   "title": "The keep-alive track from module pin 10 to R33 and R29",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-ka-n0",
   "title": "Two I2C conductors 0.2 mm from the node behind R29",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "not_sure",
   "note": "Note for k-ka-n0: looked at it twice.",
   "designators": [
    "C22",
    "R29"
   ],
   "nets": [
    "/Failsafe/KA_N0",
    "/QW_SDA",
    "/I2C0_SCL"
   ],
   "position": {
    "x_mm": 104.9,
    "y_mm": 88.3,
    "kicad_x_mm": 194.9,
    "kicad_y_mm": 71.7
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/k-ka-n0-node.svg",
     "view": "top",
     "box_mm": [
      96,
      84.4,
      107.2,
      91.4
     ]
    }
   ],
   "files": [
    "hardware/zoneflow-a/design/layout.py",
    "docs/design/hardware-layout.md"
   ],
   "source": "Layout review, section 17, item 7, second place, and section 6",
   "findings": [
    "L22"
   ]
  },
  {
   "step": "k-q2-gate",
   "title": "Ground round the gate node of Q2",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "fail",
   "note": "Note for k-q2-gate: looked at it twice.",
   "designators": [
    "Q2",
    "D22",
    "C50",
    "RN2"
   ],
   "nets": [
    "/Expander/RLY_G2",
    "V_DRV",
    "3V3"
   ],
   "position": {
    "x_mm": 53.6,
    "y_mm": 72.2,
    "kicad_x_mm": 143.6,
    "kicad_y_mm": 87.8
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/k-q2-gate-gate.svg",
     "view": "top",
     "box_mm": [
      40.5,
      68.5,
      58.5,
      81
     ]
    }
   ],
   "files": [
    "hardware/zoneflow-a/design/layout.py",
    "docs/design/hardware-layout.md"
   ],
   "source": "Layout review, section 17, item 7, third place",
   "findings": [
    "L22"
   ],
   "remedy": {
    "choice": "agent",
    "text": ""
   }
  },
  {
   "step": "k-u1-vias",
   "title": "Six vias under the buck converter",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-screws",
   "title": "No copper under the two screw heads",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-hole-b",
   "title": "Five tracks under the display carrier's leg at hole B",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "accepted",
   "note": ""
  },
  {
   "step": "k-patches",
   "title": "The 17 connections the autorouter could not close",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "k-dev-r33",
   "title": "Accept or reject: R33 is 12 mm from the pin it should sit at",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "accepted",
   "note": ""
  },
  {
   "step": "k-dev-j7",
   "title": "Accept or reject: the display bus is 61 to 72 mm long",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "not_sure",
   "note": "Note for k-dev-j7: looked at it twice.",
   "designators": [
    "J7",
    "U4"
   ],
   "nets": [
    "/TFT_SCLK",
    "/TFT_MOSI",
    "/TFT_CS",
    "/TFT_DC",
    "/TFT_RST",
    "/KA"
   ],
   "position": {
    "x_mm": 80,
    "y_mm": 90,
    "kicad_x_mm": 170,
    "kicad_y_mm": 70
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/k-dev-j7-bus.svg",
     "view": "top",
     "box_mm": [
      54,
      76,
      106,
      102
     ]
    }
   ],
   "files": [
    "hardware/netlist.yaml",
    "hardware/mechanical.yaml",
    "hardware/zoneflow-a/design/layout.py",
    "docs/design/hardware-layout.md"
   ],
   "source": "Finding L17. Layout document, section 14, item 4",
   "findings": [
    "L17"
   ]
  },
  {
   "step": "k-dev-adc",
   "title": "Accept or reject: the ADC filter capacitors are 19 mm from their pins",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "accepted",
   "note": ""
  },
  {
   "step": "k-open-risks",
   "title": "Three risks the review left open: see each and accept it",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "decision",
   "decision": "accepted",
   "note": ""
  },
  {
   "step": "k-silk",
   "title": "The terminal labels as the installer reads them",
   "group": "kicad",
   "group_title": "In KiCad",
   "kind": "check",
   "decision": "fail",
   "note": "Note for k-silk: looked at it twice.",
   "designators": [
    "J1",
    "J2",
    "J3",
    "J4"
   ],
   "nets": [],
   "position": {
    "x_mm": 57,
    "y_mm": 8,
    "kicad_x_mm": 147,
    "kicad_y_mm": 152
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/k-silk-left.svg",
     "view": "top",
     "box_mm": [
      1,
      -2.5,
      58,
      17.5
     ]
    },
    {
     "file": "hardware/zoneflow-a/output/review/k-silk-right.svg",
     "view": "top",
     "box_mm": [
      55,
      -2.5,
      114.5,
      17.5
     ]
    }
   ],
   "files": [
    "hardware/zoneflow-a/design/layout.py",
    "docs/design/hardware-layout.md"
   ],
   "source": "Layout review, section 17, item 6",
   "findings": [
    "L10",
    "L15"
   ],
   "remedy": {
    "choice": "listed",
    "id": "fix-label",
    "text": "Move or narrow the label named in the note, in `design/layout.py`."
   }
  },
  {
   "step": "s-d25",
   "title": "D25, the 6 V clamp: which pad is the cathode",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u11",
   "title": "U11, the sensor supply switch TPS2553",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u5",
   "title": "U5, the I/O expander TCA9539",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u6",
   "title": "U6, the quad comparator LM2901",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "not_sure",
   "note": "Note for s-u6: looked at it twice.",
   "designators": [
    "U6"
   ],
   "nets": [],
   "position": {
    "x_mm": 108,
    "y_mm": 78.9,
    "kicad_x_mm": 198,
    "kicad_y_mm": 81.1
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/s-u6-pads.svg",
     "view": "top"
    }
   ],
   "files": [
    "hardware/netlist.yaml",
    "hardware/zoneflow-a/design/parts.toml",
    "hardware/bom-preliminary.csv",
    "docs/design/hardware-design.md"
   ],
   "source": "Layout review, section 4: nine makers' sheets not opened",
   "findings": [
    "L23"
   ]
  },
  {
   "step": "s-u7",
   "title": "U7, the fail-safe logic 74HC132",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u8",
   "title": "U8, the current-sense amplifier MCP6002",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u10",
   "title": "U10, the clock PCF8563",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-u3",
   "title": "U3, the 3.3 V regulator LDL1117",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-ao3400",
   "title": "Q1, Q2 and Q4, the N-channel MOSFET AO3400A",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "fail",
   "note": "Note for s-ao3400: looked at it twice.",
   "designators": [
    "Q1",
    "Q2",
    "Q4"
   ],
   "nets": [],
   "position": {
    "x_mm": 53.6,
    "y_mm": 72.2,
    "kicad_x_mm": 143.6,
    "kicad_y_mm": 87.8
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/s-ao3400-pads.svg",
     "view": "top"
    }
   ],
   "files": [
    "hardware/netlist.yaml",
    "hardware/zoneflow-a/design/parts.toml",
    "hardware/bom-preliminary.csv",
    "docs/design/hardware-design.md"
   ],
   "source": "Layout review, section 4: nine makers' sheets not opened",
   "findings": [
    "L23"
   ],
   "remedy": {
    "choice": "agent",
    "text": ""
   }
  },
  {
   "step": "s-ao3401",
   "title": "Q3, the P-channel MOSFET AO3401A",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "s-usblc6",
   "title": "U2 and U9, the ESD arrays USBLC6-2SC6",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "not_sure",
   "note": "Note for s-usblc6: looked at it twice.",
   "designators": [
    "U2",
    "U9"
   ],
   "nets": [],
   "position": {
    "x_mm": 3.3,
    "y_mm": 17.7,
    "kicad_x_mm": 93.3,
    "kicad_y_mm": 142.3
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/s-usblc6-u2.svg",
     "view": "top"
    },
    {
     "file": "hardware/zoneflow-a/output/review/s-usblc6-u9.svg",
     "view": "top"
    }
   ],
   "files": [
    "hardware/netlist.yaml",
    "hardware/zoneflow-a/design/parts.toml",
    "hardware/bom-preliminary.csv",
    "docs/design/hardware-design.md"
   ],
   "source": "Layout review, section 4: checked \"from memory\"",
   "findings": [
    "L23"
   ]
  },
  {
   "step": "s-bat54s",
   "title": "The six BAT54S diode pairs",
   "group": "sheets",
   "group_title": "With the datasheets",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-scale",
   "title": "Print both sides at true size and check the scale",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-headers",
   "title": "The four terminal headers J1 to J4",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-j7",
   "title": "The display connector J7",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-sw3",
   "title": "The encoder SW3",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-bt1",
   "title": "The coin cell holder BT1",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "p-j5",
   "title": "The USB-C receptacle J5",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "fail",
   "note": "Note for p-j5: looked at it twice.",
   "designators": [
    "J5"
   ],
   "nets": [],
   "position": {
    "x_mm": 8,
    "y_mm": 2.67,
    "kicad_x_mm": 98,
    "kicad_y_mm": 157.33
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/p-j5-lands.svg",
     "view": "top",
     "box_mm": [
      1.5,
      -2.5,
      14.5,
      9
     ]
    }
   ],
   "files": [
    "hardware/zoneflow-a/design/footprints.py",
    "hardware/zoneflow-a/design/footprints.md",
    "hardware/mechanical.yaml"
   ],
   "source": "Layout review, section 17, item 1",
   "findings": [
    "L23"
   ],
   "remedy": {
    "choice": "listed",
    "id": "footprint",
    "text": "Correct the footprint in `design/footprints.py` and its row in `design/footprints.md`, from the maker's drawing."
   }
  },
  {
   "step": "p-bottom",
   "title": "The relay K1 and the capacitor C1 on the bottom print",
   "group": "print",
   "group_title": "On the 1:1 print",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "h-display",
   "title": "The display panel's flat cable against J7",
   "group": "hand",
   "group_title": "With parts in hand",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "h-heights",
   "title": "Measure the parts KiCad has no model for",
   "group": "hand",
   "group_title": "With parts in hand",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-commands",
   "title": "Run the two commands that guard the order",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-options",
   "title": "Board options that match the calculations",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-layers",
   "title": "The four copper layers in the right order",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-assembly",
   "title": "Assembly options: top side only, rails on the two long sides",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "not_sure",
   "note": "Note for o-assembly: looked at it twice.",
   "designators": [],
   "nets": [],
   "position": null,
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/o-assembly-edges.svg",
     "view": "top",
     "box_mm": [
      -9,
      -6,
      123,
      122
     ]
    }
   ],
   "files": [
    "hardware/zoneflow-a/design/layout.py",
    "docs/hardware/ordering.md"
   ],
   "source": "Layout review, section 17, item 3. Finding L20",
   "findings": [
    "L20"
   ]
  },
  {
   "step": "o-bom",
   "title": "Take six lines out of the assembly",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-prev-d25",
   "title": "Placement preview: D25, cathode bar at the left",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "fail",
   "note": "Note for o-prev-d25: looked at it twice.",
   "designators": [
    "D25"
   ],
   "nets": [
    "V_BUCK"
   ],
   "position": null,
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/o-prev-d25-d25.svg",
     "view": "top"
    }
   ],
   "files": [
    "hardware/rules/jlcpcb-cpl-rotations.csv",
    "docs/hardware/ordering.md"
   ],
   "source": "Layout review, section 17, item 5, and section 16",
   "findings": [
    "L23",
    "L1"
   ],
   "remedy": {
    "choice": "agent",
    "text": ""
   }
  },
  {
   "step": "o-prev-leds",
   "title": "Placement preview: the eleven LEDs, cathode at the left",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-prev-triacs",
   "title": "Placement preview: the nine triacs, tab towards the terminals",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-prev-optos",
   "title": "Placement preview: the nine opto-triacs, pin 1 at the lower left",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-prev-big3",
   "title": "Placement preview: the module, the display connector and the cell holder",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "pass",
   "note": ""
  },
  {
   "step": "o-prev-rest",
   "title": "Placement preview: every other polarised part against the pin 1 table",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "not_sure",
   "note": "Note for o-prev-rest: looked at it twice.",
   "designators": [],
   "nets": [],
   "position": null,
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/assembly-top.svg"
    }
   ],
   "files": [
    "hardware/rules/jlcpcb-cpl-rotations.csv",
    "docs/hardware/ordering.md"
   ],
   "source": "Layout review, section 17, item 5, and section 5, \"What this does not prove\"",
   "findings": [
    "L1",
    "L23"
   ]
  },
  {
   "step": "o-ask",
   "title": "Two questions for JLCPCB that nobody has asked",
   "group": "order",
   "group_title": "On JLCPCB's order pages",
   "kind": "check",
   "decision": "not_answered",
   "note": "",
   "designators": [
    "C1",
    "K1"
   ],
   "nets": [],
   "position": {
    "x_mm": 20,
    "y_mm": 28,
    "kicad_x_mm": 110,
    "kicad_y_mm": 132
   },
   "pictures": [
    {
     "file": "hardware/zoneflow-a/output/review/o-ask-access.svg",
     "view": "top",
     "box_mm": [
      -2,
      14.5,
      42,
      41.5
     ]
    }
   ],
   "files": [
    "docs/design/mechanical.md",
    "docs/hardware/ordering.md"
   ],
   "source": "Layout review, section 17, closing paragraph. Layout document, section 14, items 2 and 8",
   "findings": [
    "L6"
   ]
  }
 ]
}
```

# Decisions as text

## Needs a change (5)

### Ground round the gate node of Q2

- **Step:** `k-q2-gate`, In KiCad
- **Decision:** fail
- **Owner's note:** Note for k-q2-gate: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** Q2, D22, C50, RN2
- **Nets:** `/Expander/RLY_G2`, `V_DRV`, `3V3`
- **Position:** (53.6, 72.2) mm from the lower left corner of the board. KiCad's default display shows (143.6, 87.8)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-q2-gate-gate.svg`
- **Comes from:** Layout review, section 17, item 7, third place. Findings L22

### The terminal labels as the installer reads them

- **Step:** `k-silk`, In KiCad
- **Decision:** fail
- **Owner's note:** Note for k-silk: looked at it twice.
- **Remedy:** Chosen from the list (`fix-label`): Move or narrow the label named in the note, in `design/layout.py`.
- **Parts:** J1, J2, J3, J4
- **Position:** (57, 8) mm from the lower left corner of the board. KiCad's default display shows (147, 152)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-silk-left.svg`, `hardware/zoneflow-a/output/review/k-silk-right.svg`
- **Comes from:** Layout review, section 17, item 6. Findings L10, L15

### Q1, Q2 and Q4, the N-channel MOSFET AO3400A

- **Step:** `s-ao3400`, With the datasheets
- **Decision:** fail
- **Owner's note:** Note for s-ao3400: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** Q1, Q2, Q4
- **Position:** (53.6, 72.2) mm from the lower left corner of the board. KiCad's default display shows (143.6, 87.8)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-ao3400-pads.svg`
- **Comes from:** Layout review, section 4: nine makers' sheets not opened. Findings L23

### The USB-C receptacle J5

- **Step:** `p-j5`, On the 1:1 print
- **Decision:** fail
- **Owner's note:** Note for p-j5: looked at it twice.
- **Remedy:** Chosen from the list (`footprint`): Correct the footprint in `design/footprints.py` and its row in `design/footprints.md`, from the maker's drawing.
- **Parts:** J5
- **Position:** (8, 2.67) mm from the lower left corner of the board. KiCad's default display shows (98, 157.33)
- **Files a change would be made in:** `hardware/zoneflow-a/design/footprints.py`, `hardware/zoneflow-a/design/footprints.md`, `hardware/mechanical.yaml`
- **Pictures:** `hardware/zoneflow-a/output/review/p-j5-lands.svg`
- **Comes from:** Layout review, section 17, item 1. Findings L23

### Placement preview: D25, cathode bar at the left

- **Step:** `o-prev-d25`, On JLCPCB's order pages
- **Decision:** fail
- **Owner's note:** Note for o-prev-d25: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** D25
- **Nets:** `V_BUCK`
- **Files a change would be made in:** `hardware/rules/jlcpcb-cpl-rotations.csv`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-prev-d25-d25.svg`
- **Comes from:** Layout review, section 17, item 5, and section 16. Findings L23, L1

## Not sure: ask the owner (6)

### Two I2C conductors 0.2 mm from the node behind R29

- **Step:** `k-ka-n0`, In KiCad
- **Decision:** not sure
- **Owner's note:** Note for k-ka-n0: looked at it twice.
- **Parts:** C22, R29
- **Nets:** `/Failsafe/KA_N0`, `/QW_SDA`, `/I2C0_SCL`
- **Position:** (104.9, 88.3) mm from the lower left corner of the board. KiCad's default display shows (194.9, 71.7)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-ka-n0-node.svg`
- **Comes from:** Layout review, section 17, item 7, second place, and section 6. Findings L22

### Accept or reject: the display bus is 61 to 72 mm long

- **Step:** `k-dev-j7`, In KiCad
- **Decision:** not sure
- **Owner's note:** Note for k-dev-j7: looked at it twice.
- **Parts:** J7, U4
- **Nets:** `/TFT_SCLK`, `/TFT_MOSI`, `/TFT_CS`, `/TFT_DC`, `/TFT_RST`, `/KA`
- **Position:** (80, 90) mm from the lower left corner of the board. KiCad's default display shows (170, 70)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/mechanical.yaml`, `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-dev-j7-bus.svg`
- **Comes from:** Finding L17. Layout document, section 14, item 4. Findings L17

### U6, the quad comparator LM2901

- **Step:** `s-u6`, With the datasheets
- **Decision:** not sure
- **Owner's note:** Note for s-u6: looked at it twice.
- **Parts:** U6
- **Position:** (108, 78.9) mm from the lower left corner of the board. KiCad's default display shows (198, 81.1)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-u6-pads.svg`
- **Comes from:** Layout review, section 4: nine makers' sheets not opened. Findings L23

### U2 and U9, the ESD arrays USBLC6-2SC6

- **Step:** `s-usblc6`, With the datasheets
- **Decision:** not sure
- **Owner's note:** Note for s-usblc6: looked at it twice.
- **Parts:** U2, U9
- **Position:** (3.3, 17.7) mm from the lower left corner of the board. KiCad's default display shows (93.3, 142.3)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-usblc6-u2.svg`, `hardware/zoneflow-a/output/review/s-usblc6-u9.svg`
- **Comes from:** Layout review, section 4: checked "from memory". Findings L23

### Assembly options: top side only, rails on the two long sides

- **Step:** `o-assembly`, On JLCPCB's order pages
- **Decision:** not sure
- **Owner's note:** Note for o-assembly: looked at it twice.
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-assembly-edges.svg`
- **Comes from:** Layout review, section 17, item 3. Finding L20. Findings L20

### Placement preview: every other polarised part against the pin 1 table

- **Step:** `o-prev-rest`, On JLCPCB's order pages
- **Decision:** not sure
- **Owner's note:** Note for o-prev-rest: looked at it twice.
- **Files a change would be made in:** `hardware/rules/jlcpcb-cpl-rotations.csv`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/assembly-top.svg`
- **Comes from:** Layout review, section 17, item 5, and section 5, "What this does not prove". Findings L1, L23

## Not answered (1)

### Two questions for JLCPCB that nobody has asked

- **Step:** `o-ask`, On JLCPCB's order pages
- **Decision:** not answered
- **Owner's note:** none
- **Parts:** C1, K1
- **Position:** (20, 28) mm from the lower left corner of the board. KiCad's default display shows (110, 132)
- **Files a change would be made in:** `docs/design/mechanical.md`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-ask-access.svg`
- **Comes from:** Layout review, section 17, closing paragraph. Layout document, section 14, items 2 and 8. Findings L6

## Passed and accepted: change nothing (35)

- pass: Open the board and run the design rule check yourself (`k-open`)
- pass: Look at one valve output on all four layers (`k-valve`)
- pass: The keep-alive track from module pin 10 to R33 and R29 (`k-ka-pin10`)
- pass: Six vias under the buck converter (`k-u1-vias`)
- pass: No copper under the two screw heads (`k-screws`)
- accepted: Five tracks under the display carrier's leg at hole B (`k-hole-b`)
- pass: The 17 connections the autorouter could not close (`k-patches`)
- accepted: Accept or reject: R33 is 12 mm from the pin it should sit at (`k-dev-r33`)
- accepted: Accept or reject: the ADC filter capacitors are 19 mm from their pins (`k-dev-adc`)
- accepted: Three risks the review left open: see each and accept it (`k-open-risks`)
- pass: D25, the 6 V clamp: which pad is the cathode (`s-d25`)
- pass: U11, the sensor supply switch TPS2553 (`s-u11`)
- pass: U5, the I/O expander TCA9539 (`s-u5`)
- pass: U7, the fail-safe logic 74HC132 (`s-u7`)
- pass: U8, the current-sense amplifier MCP6002 (`s-u8`)
- pass: U10, the clock PCF8563 (`s-u10`)
- pass: U3, the 3.3 V regulator LDL1117 (`s-u3`)
- pass: Q3, the P-channel MOSFET AO3401A (`s-ao3401`)
- pass: The six BAT54S diode pairs (`s-bat54s`)
- pass: Print both sides at true size and check the scale (`p-scale`)
- pass: The four terminal headers J1 to J4 (`p-headers`)
- pass: The display connector J7 (`p-j7`)
- pass: The encoder SW3 (`p-sw3`)
- pass: The coin cell holder BT1 (`p-bt1`)
- pass: The relay K1 and the capacitor C1 on the bottom print (`p-bottom`)
- pass: The display panel's flat cable against J7 (`h-display`)
- pass: Measure the parts KiCad has no model for (`h-heights`)
- pass: Run the two commands that guard the order (`o-commands`)
- pass: Board options that match the calculations (`o-options`)
- pass: The four copper layers in the right order (`o-layers`)
- pass: Take six lines out of the assembly (`o-bom`)
- pass: Placement preview: the eleven LEDs, cathode at the left (`o-prev-leds`)
- pass: Placement preview: the nine triacs, tab towards the terminals (`o-prev-triacs`)
- pass: Placement preview: the nine opto-triacs, pin 1 at the lower left (`o-prev-optos`)
- pass: Placement preview: the module, the display connector and the cell holder (`o-prev-big3`)
