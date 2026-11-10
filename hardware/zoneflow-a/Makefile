# ZoneFlow rev A, the product board. `make` regenerates the KiCad project and
# every output from the files in design/ and from ../netlist.yaml, and fails
# if the netlist check, the page lint, ERC or (once the board is laid out)
# DRC fail. See docs/design/hardware-toolchain.md.
NAME := zoneflow-a
VARIANTS := 4-zone
SCH_LINT := 1
STABLE := 1
DESIGN_NETLIST := ../netlist.yaml
# Phase 1: the schematic. The layout targets take over when the board exists.
.DEFAULT_GOAL := schematic
include ../flow.mk
