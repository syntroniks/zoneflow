# Shared build rules for a ZoneFlow board directory. A board Makefile sets
# NAME (the KiCad project name) and includes this file. See
# docs/design/hardware-toolchain.md.
#
# The source of truth is $(DESIGN)/circuit.py and $(DESIGN)/board.toml.
# Everything else in the board directory is generated.

HW_ROOT   := $(dir $(lastword $(MAKEFILE_LIST)))
HWFLOW    := $(HW_ROOT)../tools/hwflow
DESIGN    ?= design
OUT       ?= output
FAB_RULES ?= $(HW_ROOT)rules/jlcpcb.toml
ROTATIONS ?= $(HW_ROOT)rules/jlcpcb-cpl-rotations.csv
CACHE     ?= $(HW_ROOT).cache
GERBER_LAYERS ?= F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts

# Generation runs in the locked uv environment. The board steps need KiCad's
# pcbnew module, which only the system interpreter has.
UV_PY     := uv run --project $(HWFLOW) --frozen --no-dev python
KICAD_PY  ?= /usr/bin/python3
KICAD_CLI ?= kicad-cli

# Do not read or write the user's KiCad settings: a build-local configuration
# gives the same result on every machine.
export KICAD_CONFIG_HOME := $(abspath build/kicad-config)
export PYTHONPATH := $(abspath $(HWFLOW))
export PYTHONDONTWRITEBYTECODE := 1

SCH := $(NAME).kicad_sch
PCB := $(NAME).kicad_pcb
# kicad-cli prints wxWidgets debug lines on stderr. They go to a log file so
# that the exit code of kicad-cli still decides whether the step passed.
LOG := 2>> build/kicad-cli.stderr

.PHONY: all sch erc board route reroute finish drc outputs check
.NOTPARALLEL:

all: outputs
	@echo "build complete: ERC and DRC passed, outputs are in $(OUT)/"

build:
	@mkdir -p build/kicad-config $(OUT)/reports $(OUT)/gerbers

# Circuit description -> schematic sheets, project file, library tables, DRC
# rules. Fails if the netlist KiCad extracts differs from the description.
sch: | build
	$(UV_PY) -m hwflow sch $(DESIGN) . --fab-rules $(FAB_RULES)

# Errors and warnings both fail the build.
erc: sch
	$(KICAD_CLI) sch erc --severity-error --severity-warning --format report \
		-o $(OUT)/reports/erc.rpt $(SCH) $(LOG)
	$(KICAD_CLI) sch erc --severity-error --severity-warning --exit-code-violations --format json \
		-o $(OUT)/reports/erc.json $(SCH) $(LOG)

# Netlist + board.toml -> placed, pre-routed board and a Specctra DSN.
board: erc
	$(KICAD_PY) -m hwflow.pcbbuild build $(DESIGN) . --fab-rules $(FAB_RULES)

# Autoroute. Reuses route/$(NAME).ses while the DSN is unchanged.
route: board
	$(KICAD_PY) -m hwflow.route . --cache $(CACHE) $(ROUTE_FLAGS)

# Throw the stored routing away and route again.
reroute:
	$(MAKE) ROUTE_FLAGS=--force all

# Import the routed session and remove dangling stubs.
finish: route
	$(KICAD_PY) -m hwflow.pcbbuild finish . --session route/$(NAME).ses

# The first call fills the copper zones and saves the board. Errors and
# warnings both fail the build; --schematic-parity checks that the board still
# matches the schematic.
drc: finish
	$(KICAD_CLI) pcb drc --refill-zones --save-board --schematic-parity --severity-error --severity-warning \
		--format report -o $(OUT)/reports/drc.rpt $(PCB) $(LOG)
	$(KICAD_CLI) pcb drc --schematic-parity --severity-error --severity-warning --exit-code-violations \
		--format json -o $(OUT)/reports/drc.json $(PCB) $(LOG)

outputs: drc
	$(KICAD_CLI) sch export pdf -o $(OUT)/$(NAME)-schematic.pdf $(SCH) $(LOG)
	# Gerbers, drill and placement all use the drill/place origin, which the
	# board builder puts on the bottom-left corner of the outline.
	$(KICAD_CLI) pcb export gerbers --layers "$(GERBER_LAYERS)" --subtract-soldermask --no-protel-ext \
		--use-drill-file-origin -o $(OUT)/gerbers/ $(PCB) $(LOG)
	$(KICAD_CLI) pcb export drill --format excellon --excellon-separate-th --drill-origin plot \
		--generate-map --map-format pdf -o $(OUT)/gerbers/ $(PCB) $(LOG)
	$(KICAD_CLI) pcb export pos --format csv --units mm --side both --exclude-dnp --use-drill-file-origin \
		-o build/$(NAME)-pos.csv $(PCB) $(LOG)
	$(KICAD_PY) -m hwflow.fab cpl build/$(NAME)-pos.csv $(OUT)/$(NAME)-cpl-jlcpcb.csv --rotations $(ROTATIONS)
	$(KICAD_CLI) sch export bom --fields 'Value,Reference,Footprint,LCSC,$${QUANTITY}' \
		--labels 'Value,Reference,Footprint,LCSC,Qty' --group-by 'Value,Footprint,LCSC' \
		--ref-range-delimiter '' --exclude-dnp -o build/$(NAME)-bom.csv $(SCH) $(LOG)
	$(KICAD_PY) -m hwflow.fab bom build/$(NAME)-bom.csv $(OUT)/$(NAME)-bom-jlcpcb.csv
	$(KICAD_CLI) pcb export step --subst-models --force -o $(OUT)/$(NAME).step $(PCB) $(LOG)
	$(KICAD_CLI) pcb render --side top --quality high --width 1600 --height 1200 \
		-o $(OUT)/$(NAME)-top.png $(PCB) $(LOG)
	$(KICAD_CLI) pcb render --side bottom --quality high --width 1600 --height 1200 \
		-o $(OUT)/$(NAME)-bottom.png $(PCB) $(LOG)
	# Flat views for review: copper and silkscreen per side, and one SVG per schematic page.
	$(KICAD_CLI) pcb export svg --layers "F.Cu,F.Silkscreen,Edge.Cuts" --mode-single --page-size-mode 2 \
		--exclude-drawing-sheet -o $(OUT)/$(NAME)-front.svg $(PCB) $(LOG)
	$(KICAD_CLI) pcb export svg --layers "B.Cu,B.Silkscreen,Edge.Cuts" --mode-single --page-size-mode 2 \
		--exclude-drawing-sheet -o $(OUT)/$(NAME)-back.svg $(PCB) $(LOG)
	$(KICAD_CLI) sch export svg -o $(OUT)/schematic-svg/ $(SCH) $(LOG)
	$(KICAD_PY) -m hwflow.report . $(OUT)

# Unit tests, KiCad round-trip tests, and proof that ERC and DRC catch
# deliberately broken inputs. Needs the board to have been built.
check:
	cd $(HWFLOW) && uv run --frozen pytest -q
