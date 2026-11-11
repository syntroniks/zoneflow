# ZoneFlow rev A, the product board. `make` regenerates the KiCad project and
# every output from the files in design/ and from ../netlist.yaml, and fails
# if the netlist check, the page lint, ERC or (once the board is laid out)
# DRC fail. See docs/design/hardware-toolchain.md.
NAME := zoneflow-a
VARIANTS := 4-zone
SCH_LINT := 1
STABLE := 1
DESIGN_NETLIST := ../netlist.yaml
# The board must agree with the mechanical contract: `make` fails if a constrained part is off
# position, a height zone is violated or a part is on the wrong side.
MECH_CONTRACT := ../mechanical.yaml
# Four copper layers.
GERBER_LAYERS := F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts
INNER_LAYERS := In1.Cu In2.Cu
# C1, K1 and the terminal headers are soldered by hand after assembly: they are not in the placement file.
POS_FLAGS := --smd-only
# On two routing layers the autorouter needs about 70 s for a pass of this board and 30 passes.
ROUTE_LIMITS := --timeout 7200 --passes 30
# The pre-order review walkthrough of the documentation is about this board.
REVIEW := 1
include ../flow.mk
