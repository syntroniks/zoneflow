# STUDY, not the product. See README.md and
# docs/design/failsafe-reduction-study.md. Nothing here is built into a
# board, and nothing here changes hardware/netlist.yaml.
#
#   make        the simulation, then the parts, cost and area table
#   make sim    ngspice runs of the reduced reset path, about 5 minutes
#   make cost   parts, landed cost and area of the variants
#   make area   pads and courtyard areas from the board file (needs KiCad 10)
#   make review the additions of docs/design/failsafe-reduction-review.md:
#               the wider fault injection on V1 and V2, about 6 minutes, and
#               the table of V1 with one thing put back at a time
#
# `make cost` reads results/footprints.csv, which is committed, so it runs
# on a machine without KiCad. `make` does not run `make review`.

.PHONY: all sim cost area review wide options
all: sim cost

sim:
	uv run study.py all

cost:
	uv run --locked --project ../../../tools/cost --with 'pyyaml>=6,<7' python cost.py

area:
	python3 area.py

review: wide options

wide:
	uv run faults_wide.py

options:
	uv run --locked --project ../../../tools/cost --with 'pyyaml>=6,<7' python review_options.py
