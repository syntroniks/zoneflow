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
# Assembly variants (KiCad 10) to make a BOM for, besides the base design.
VARIANTS  ?=
# Set to 1 to fail the build when text or symbols collide on a schematic page.
SCH_LINT  ?=
# Set to 1 to replace the time of export in PDFs, SVGs and reports by the date
# of the title block, so that a rebuild with no change leaves no diff.
STABLE    ?=
# The design netlist a board is drawn from, if it has one (netlist-driven capture).
DESIGN_NETLIST ?=
# Extra flags for the placement export: --smd-only where the through-hole parts are soldered by hand.
POS_FLAGS ?=
# Inner copper layers to plot for review, besides the two outer ones.
INNER_LAYERS ?=
# The mechanical contract the board must agree with, if it has one. With it the
# build fails when a constrained part is off position or a height zone is violated.
MECH_CONTRACT ?=

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
STABILISE = $(if $(STABLE),$(KICAD_PY) -m hwflow.stable $(SCH) $(OUT),@true)
# With STABLE, a plot whose new version draws the same elements keeps its old
# file: two builds of one KiCad release write a page's items in a different order.
PLOTS_STASH = $(if $(STABLE),$(KICAD_PY) -m hwflow.plotcmp stash $(OUT) build/previous-plots,@true)
PLOTS_KEEP = $(if $(STABLE),$(KICAD_PY) -m hwflow.plotcmp keep build/previous-plots $(OUT),@true)
PCB := $(NAME).kicad_pcb
# KiCad rewrites the project file with all its defaults whenever a board is saved. The committed
# one is the file the schematic step writes, so the steps that save a board put that back: CI runs
# the schematic step alone and compares.
PROJECT_FILE = $(UV_PY) -m hwflow project $(DESIGN) . --fab-rules $(FAB_RULES)
# kicad-cli prints wxWidgets debug lines on stderr. They go to a log file so
# that the exit code of kicad-cli still decides whether the step passed.
LOG := 2>> build/kicad-cli.stderr
comma := ,
empty :=
space := $(empty) $(empty)

.PHONY: all schematic footprints layout sch lint erc sch-outputs board route reroute finish patches drc mech rules board-check ibom outputs check committed
.NOTPARALLEL:

all: outputs ibom
	@echo "build complete: ERC and DRC passed, outputs are in $(OUT)/"

# The schematic half only: generate, check, and export the schematic deliverables.
schematic: sch-outputs
	@echo "schematic complete: netlist check and ERC passed, outputs are in $(OUT)/"

build:
	@mkdir -p build/kicad-config $(OUT)/reports $(OUT)/gerbers

# Project-local footprints, if the board has any: design/footprints.py writes
# lib/<Library>.pretty through KiCad's pcbnew module.
FOOTPRINT_LIB ?= lib/ZoneFlow.pretty
footprints: | build
	@if [ -f $(DESIGN)/footprints.py ]; then $(KICAD_PY) $(DESIGN)/footprints.py $(FOOTPRINT_LIB); fi

# Circuit description -> schematic sheets, project file, library tables, DRC
# rules. Fails if the netlist KiCad extracts differs from the description.
# A board whose description repeats or is computed writes design/board.toml from
# design/layout.py. The schematic step reads the net classes from board.toml.
layout: | build
	@if [ -f $(DESIGN)/layout.py ]; then $(KICAD_PY) $(DESIGN)/layout.py . ; fi

sch: footprints layout | build
	$(UV_PY) -m hwflow sch $(DESIGN) . --fab-rules $(FAB_RULES)

# Plot every page and fail on text or symbols that collide. ERC does not look for that.
lint: sch
	$(UV_PY) -m hwflow lint $(DESIGN) .

# Errors and warnings both fail the build.
erc: sch $(if $(SCH_LINT),lint)
	$(KICAD_CLI) sch erc --severity-error --severity-warning --format report \
		-o $(OUT)/reports/erc.rpt $(SCH) $(LOG)
	$(KICAD_CLI) sch erc --severity-error --severity-warning --exit-code-violations --format json \
		-o $(OUT)/reports/erc.json $(SCH) $(LOG)
	$(STABILISE)

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
	$(KICAD_PY) -m hwflow.pcbbuild finish . --session route/$(NAME).ses --design $(DESIGN)

# Close what the autorouter left open (hwflow.patchroute): import the stored session without
# patches, let DRC say what is unconnected, route each such pair, and write $(DESIGN)/patches.toml.
# A patch can cut a pour of another net in two, so the board is then built with the patches and
# looked at again, up to three more times, each pass adding to the file. Needs design/layout.py,
# which is what carries patches.toml into board.toml.
# Run it once after the board has been routed again; then run make.
PATCH_DRC = rm -rf build/patch && mkdir -p build/patch \
	&& cp -r $(PCB) $(NAME).kicad_pro fp-lib-table sym-lib-table lib build/patch/ \
	&& { cp $(NAME).kicad_dru build/patch/ 2>/dev/null; \
	     $(KICAD_CLI) pcb drc --refill-zones --save-board --severity-error --severity-warning --format json \
	     -o build/patch/drc.json build/patch/$(PCB) $(LOG); true; }
PATCH_ROUTE = $(KICAD_PY) -m hwflow.patchroute . $(DESIGN) --drc build/patch/drc.json
patches: route
	$(KICAD_PY) -m hwflow.pcbbuild finish . --session route/$(NAME).ses --design $(DESIGN) --no-patch
	$(PATCH_DRC)
	$(PATCH_ROUTE)
	set -e; for pass in 2 3 4; do \
		$(KICAD_PY) $(DESIGN)/layout.py .; \
		$(KICAD_PY) -m hwflow.pcbbuild build $(DESIGN) . --fab-rules $(FAB_RULES); \
		$(KICAD_PY) -m hwflow.pcbbuild finish . --session route/$(NAME).ses --design $(DESIGN); \
		$(PATCH_DRC); \
		if $(PATCH_ROUTE) --check; then break; fi; \
		$(PATCH_ROUTE) --add; \
	done
	$(KICAD_PY) $(DESIGN)/layout.py .

# The first call fills the copper zones and saves the board. Errors and
# warnings both fail the build; --schematic-parity checks that the board still
# matches the schematic.
drc: finish
	$(KICAD_CLI) pcb drc --refill-zones --save-board --schematic-parity --severity-error --severity-warning \
		--format report -o $(OUT)/reports/drc.rpt $(PCB) $(LOG)
	$(KICAD_CLI) pcb drc --schematic-parity --severity-error --severity-warning --exit-code-violations \
		--format json -o $(OUT)/reports/drc.json $(PCB) $(LOG)
	$(STABILISE)
	$(PROJECT_FILE)

BOM_EXPORT = $(KICAD_CLI) sch export bom --fields 'Value,Reference,Footprint,LCSC,$${QUANTITY}' \
	--labels 'Value,Reference,Footprint,LCSC,Qty' --group-by 'Value,Footprint,LCSC' \
	--ref-range-delimiter '' --exclude-dnp

# Schematic deliverables: the PDF, one SVG per page, and a JLCPCB BOM for each build.
sch-outputs: erc
	$(PLOTS_STASH)
	$(KICAD_CLI) sch export pdf -o $(OUT)/$(NAME)-schematic.pdf $(SCH) $(LOG)
	$(KICAD_CLI) sch export svg -o $(OUT)/schematic-svg/ $(SCH) $(LOG)
	$(BOM_EXPORT) -o build/$(NAME)-bom.csv $(SCH) $(LOG)
	$(KICAD_PY) -m hwflow.fab bom build/$(NAME)-bom.csv $(OUT)/$(NAME)-bom-jlcpcb.csv
	$(foreach v,$(VARIANTS),$(BOM_EXPORT) --variant $(v) -o build/$(NAME)-bom-$(v).csv $(SCH) $(LOG) && \
		$(KICAD_PY) -m hwflow.fab bom build/$(NAME)-bom-$(v).csv $(OUT)/$(NAME)-bom-jlcpcb-$(v).csv &&) true
	$(STABILISE)
	$(PLOTS_KEEP)

# The committed netlist and BOMs against the design netlist. Needs Python only,
# no KiCad, and builds nothing: CI runs it on every pull request.
committed:
	$(UV_PY) -m hwflow committed . --netlist $(DESIGN_NETLIST)

# The built board against the mechanical contract: positions of the constrained
# parts, holes, height zones, keep-outs. Reads the saved board, measures nothing.
mech: drc
	$(KICAD_PY) -m hwflow.pcbbuild dump $(DESIGN) .
	$(if $(MECH_CONTRACT),$(UV_PY) -m hwflow mech . --contract $(MECH_CONTRACT) --heights $(DESIGN)/heights.toml,@true)

# Layout rules that DRC cannot express, from board.toml: nets that must stay apart on every
# layer, circuits that must be a set distance apart, nets that must have no test pad.
rules: drc
	$(KICAD_PY) -m hwflow.layoutrules . $(DESIGN) --report $(OUT)/reports/layout-rules.md

# Everything that judges the board and needs no display: DRC with schematic parity, the
# mechanical contract, the layout rules. CI runs this.
board-check: drc mech rules
	@echo "board check complete: DRC, mechanical contract and layout rules passed"

# Interactive HTML BOM (github.com/openscopeproject/InteractiveHtmlBom, MIT). Fetched once into the
# cache, like the autorouter. It imports wx but opens no window.
IBOM_VERSION ?= 2.12.0
IBOM_DIR = $(CACHE)/ibom-$(IBOM_VERSION)
$(IBOM_DIR)/InteractiveHtmlBom/generate_interactive_bom.py:
	uv pip install --quiet --no-deps --python $(KICAD_PY) --target $(IBOM_DIR) InteractiveHtmlBom==$(IBOM_VERSION)

ibom: drc $(IBOM_DIR)/InteractiveHtmlBom/generate_interactive_bom.py
	INTERACTIVE_HTML_BOM_NO_DISPLAY=1 PYTHONPATH=$(IBOM_DIR):$(PYTHONPATH) $(KICAD_PY) -m InteractiveHtmlBom.generate_interactive_bom \
		--no-browser --dest-dir $(abspath $(OUT)) --name-format "$(NAME)-ibom" --include-tracks --include-nets \
		--extra-fields LCSC $(PCB) >> build/ibom.log 2>&1
	$(STABILISE)
	$(PROJECT_FILE)

outputs: drc mech rules sch-outputs
	# Gerbers, drill and placement all use the drill/place origin, which the
	# board builder puts on the bottom-left corner of the outline.
	$(KICAD_CLI) pcb export gerbers --layers "$(GERBER_LAYERS)" --subtract-soldermask --no-protel-ext \
		--use-drill-file-origin -o $(OUT)/gerbers/ $(PCB) $(LOG)
	$(KICAD_CLI) pcb export drill --format excellon --excellon-separate-th --drill-origin plot \
		--generate-map --map-format pdf -o $(OUT)/gerbers/ $(PCB) $(LOG)
	$(KICAD_CLI) pcb export pos --format csv --units mm --side both --exclude-dnp --use-drill-file-origin \
		$(POS_FLAGS) -o build/$(NAME)-pos.csv $(PCB) $(LOG)
	# A correction row may name one LCSC number: the BOM says which part has which.
	$(KICAD_PY) -m hwflow.fab cpl build/$(NAME)-pos.csv $(OUT)/$(NAME)-cpl-jlcpcb.csv --rotations $(ROTATIONS) \
		--bom $(OUT)/$(NAME)-bom-jlcpcb.csv
	# One placement file per build variant: the base file without the parts that variant leaves out.
	$(foreach v,$(VARIANTS),$(KICAD_PY) -m hwflow.fab cpl build/$(NAME)-pos.csv \
		$(OUT)/$(NAME)-cpl-jlcpcb-$(v).csv --rotations $(ROTATIONS) --bom $(OUT)/$(NAME)-bom-jlcpcb.csv \
		--model build/model.json --variant $(v) &&) true
	# Where pin 1 of every polarised part is: what to compare the assembler's placement preview with.
	$(KICAD_PY) -m hwflow.fab polarity build/placement.json $(OUT)/$(NAME)-pin1.csv
	# Assembly drawings: part outlines and every reference designator, one page per side.
	# kicad-cli plots a board PDF without its drawing sheet unless asked for one.
	$(KICAD_CLI) pcb export pdf --layers "F.Fab,Edge.Cuts" --mode-single --black-and-white \
		-o $(OUT)/$(NAME)-assembly-top.pdf $(PCB) $(LOG)
	$(KICAD_CLI) pcb export pdf --layers "B.Fab,Edge.Cuts" --mode-single --black-and-white --mirror \
		-o $(OUT)/$(NAME)-assembly-bottom.pdf $(PCB) $(LOG)
	# The board as a PDF, one page per layer, for a reviewer without KiCad.
	$(KICAD_CLI) pcb export pdf --layers "F.Cu,$(subst $(space),$(comma),$(INNER_LAYERS))$(if $(INNER_LAYERS),$(comma))B.Cu,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,F.Paste" \
		--common-layers "Edge.Cuts" --mode-multipage -o $(OUT)/$(NAME)-board.pdf $(PCB) $(LOG)
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
	$(foreach l,$(INNER_LAYERS),$(KICAD_CLI) pcb export svg --layers "$(l),Edge.Cuts" --mode-single \
		--page-size-mode 2 --exclude-drawing-sheet -o $(OUT)/$(NAME)-$(l).svg $(PCB) $(LOG) &&) true
	$(KICAD_PY) -m hwflow.report . $(OUT)
	# What was routed by hand and what by the autorouter, and the things to look at.
	$(KICAD_PY) -m hwflow.routereport . $(DESIGN) $(OUT)/reports/routing.md
	$(STABILISE)
	$(PROJECT_FILE)

# Unit tests, KiCad round-trip tests, and proof that ERC and DRC catch
# deliberately broken inputs. Needs the board to have been built.
check:
	cd $(HWFLOW) && uv run --frozen pytest -q
