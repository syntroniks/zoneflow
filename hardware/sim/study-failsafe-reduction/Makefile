# STUDY, not the product. See README.md and
# docs/design/failsafe-reduction-study.md. Nothing here is built into a
# board, and nothing here changes hardware/netlist.yaml.
#
#   make        the simulation, then the parts, cost and area table
#   make sim    ngspice runs of the reduced reset path, about 5 minutes
#   make cost   parts, landed cost and area of the variants
#   make area   pads and courtyard areas from the board file (needs KiCad 10)
#
# `make cost` reads results/footprints.csv, which is committed, so it runs
# on a machine without KiCad.

.PHONY: all sim cost area
all: sim cost

sim:
	uv run study.py all

cost:
	uv run --locked --project ../../../tools/cost --with 'pyyaml>=6,<7' python cost.py

area:
	python3 area.py
