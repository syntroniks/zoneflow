# ZoneFlow enclosure. `make` rebuilds everything in output/ from
# ../hardware/mechanical.yaml, the board file, params.scad, zoneflow.scad and
# testfit.scad, and fails if the interference and clearance check fails.
#
# `make test-fit` builds only what the test fit needs and packs the print
# files into build/zoneflow-test-fit-print-files.zip.
#
# Two steps read the board with KiCad 10: board_layout.json (KICAD_PY, the
# Python that has the pcbnew module) and the detailed view-only board model
# (KICAD_CLI). Their results are committed, so `make -o board_layout.json -o
# $(DETAIL).stl` builds the rest on a machine without KiCad.
#
# OpenSCAD must have the Manifold backend: a 2024 or later build. The
# 2021.01 release that most distributions ship does not. `make openscad`
# fetches the snapshot this was built with into .cache/ (not committed).
# Snapshots are removed from the server after a while; any later build works
# with `make OPENSCAD=/path/to/openscad`.

SNAPSHOT        := OpenSCAD-2026.11.06-x86_64.AppImage
SNAPSHOT_URL    := https://files.openscad.org/snapshots/$(SNAPSHOT)
SNAPSHOT_SHA256 := cd7ef8690fbaef32904160d9a87173b426d380b228cc5ef312e169fe9fc4bcde

OPENSCAD ?= $(if $(wildcard .cache/$(SNAPSHOT)),$(CURDIR)/.cache/$(SNAPSHOT),openscad)
OSC      := $(OPENSCAD) --backend=manifold
# This OpenSCAD renders PNG off screen with no display. If yours cannot, run
# `make XVFB="xvfb-run -a" -j1`; xvfb-run is not safe to run in parallel.
XVFB     ?=
UV       ?= uv
PYTHON   ?= python3
KICAD_PY ?= python3
KICAD_CLI ?= kicad-cli

YAML    := ../hardware/mechanical.yaml
PCB     := ../hardware/zoneflow-a/zoneflow-a.kicad_pcb
HEIGHTS := ../hardware/zoneflow-a/design/heights.toml
SRC   := zoneflow.scad params.scad board_params.scad testfit.scad testfit_params.scad $(wildcard fonts/*.ttf)
OUT   := output
PARTS := base fascia cover bridge knob button-back button-home lightpipe

STL   := $(PARTS:%=$(OUT)/zoneflow-%.stl)
3MF   := $(PARTS:%=$(OUT)/zoneflow-%.3mf)
VIEWS := front three-quarter rear-top exploded wiring-open section section-side board front-2.4
PNG   := $(VIEWS:%=$(OUT)/render-%.png)

# The test-fit kit: stand-ins for the board, its plugs and the display panel.
TF       := $(OUT)/test-fit
TF_PARTS := board c1 k1 plug-2p plug-4p plug-7p panel
TF_STL   := $(TF_PARTS:%=$(TF)/zoneflow-testfit-%.stl)
TF_3MF   := $(TF_PARTS:%=$(TF)/zoneflow-testfit-%.3mf)
TF_VIEWS := kit board-top board-bottom step-1-board step-2-display step-3-fascia step-4-plugs step-5-cover \
            check-terminals check-display check-knob check-buttons check-leds check-fuses check-bottom
TF_PNG   := $(TF_VIEWS:%=$(TF)/render-%.png)
# The board as KiCad models it, for viewing and for the check. Not for printing.
DETAIL   := $(TF)/view-only/zoneflow-a-board-detailed-view-only
# One set of plates for a 220 x 220 mm bed, one for a 250 x 250 mm bed: plates.toml.
PLATE_NAMES := 1-test-fit-kit 2-body 3-front 4-clear \
               250-a1-base-and-small-parts 250-a2-fascia-and-cover 250-b-test-fit-kit
PLATES   := $(PLATE_NAMES:%=$(OUT)/plates/zoneflow-plate-%.3mf)
PLATE_PNG := $(PLATE_NAMES:%=$(OUT)/plates/zoneflow-plate-%.png)
PLATE_SCAD := $(PLATE_NAMES:%=build/plates/zoneflow-plate-%.scad)
ZIP      := build/zoneflow-test-fit-print-files.zip

RENDER := $(XVFB) $(OSC) --render --colorscheme=Tomorrow
WALL   := -D wallview=true

.PHONY: all params models renders template check docs-images tf-docs-images clean openscad test-fit plates detail zip
all: params models plates detail renders template check docs-images zip

test-fit: params models plates detail check tf-docs-images zip

params: board_params.scad board_layout.json testfit_params.scad
board_params.scad: $(YAML) tools/gen_params.py
	$(UV) run -q tools/gen_params.py $(YAML) $@

# Where the laid-out board puts every part. The one step that needs KiCad's Python.
board_layout.json: $(PCB) tools/board_layout.py
	$(KICAD_PY) tools/board_layout.py $(PCB) $@

# The stand-ins of the test-fit board: positions from the board file, bodies
# from the contract and the height table. Stops if board and contract disagree.
testfit_params.scad $(TF)/stand-ins.md &: board_layout.json $(YAML) $(HEIGHTS) tools/gen_testfit.py | $(TF)
	$(UV) run -q tools/gen_testfit.py board_layout.json $(YAML) $(HEIGHTS) testfit_params.scad $(TF)/stand-ins.md

# 3MF is written from the STL by tools/threemf.py: OpenSCAD's own export puts
# the time and random identifiers in the file, so it never built twice alike.
models: $(STL) $(3MF) $(TF_STL) $(TF_3MF)
# OpenSCAD's STL goes through tools/clean_stl.py, which welds vertices that
# differ in the last bit and stops unless the result is one closed solid.
$(OUT)/zoneflow-%.stl: $(SRC) tools/clean_stl.py | $(OUT)
	@mkdir -p build/raw
	$(OSC) -q -D 'part="print_$(subst -,_,$*)"' --export-format binstl -o build/raw/zoneflow-$*.stl zoneflow.scad
	$(UV) run -q tools/clean_stl.py build/raw/zoneflow-$*.stl $@
$(OUT)/zoneflow-%.3mf: $(OUT)/zoneflow-%.stl tools/threemf.py
	$(PYTHON) tools/threemf.py single $< $@ --title "ZoneFlow enclosure: $*" \
		--description "In print orientation. Nothing has been printed by the project."
$(TF)/zoneflow-testfit-%.stl: $(SRC) tools/clean_stl.py | $(TF)
	@mkdir -p build/raw
	$(OSC) -q -D 'part="print_tf_$(subst -,_,$*)"' --export-format binstl -o build/raw/zoneflow-testfit-$*.stl zoneflow.scad
	$(UV) run -q tools/clean_stl.py build/raw/zoneflow-testfit-$*.stl $@
$(TF)/zoneflow-testfit-%.3mf: $(TF)/zoneflow-testfit-%.stl tools/threemf.py
	$(PYTHON) tools/threemf.py single $< $@ --title "ZoneFlow TEST FIT: $*" \
		--description "Stand-in for a dry fit. Not a circuit board. In print orientation."

# One 3MF per print batch, parts laid out on the bed plates.toml names. It
# fails if a plate does not fit its bed with the margin and gap given there.
plates: $(PLATES)
$(PLATES) &: plates.toml tools/threemf.py $(STL) $(TF_STL)
	$(PYTHON) tools/threemf.py plates plates.toml $(OUT)

# Each plate from straight above, with the bed outline and the margin drawn,
# all at one scale. The scene puts the middle of the bed at the origin.
$(PLATE_SCAD) &: plates.toml tools/threemf.py $(STL) $(TF_STL)
	$(PYTHON) tools/threemf.py scenes plates.toml $(OUT) build/plates
$(OUT)/plates/zoneflow-plate-%.png: build/plates/zoneflow-plate-%.scad
	$(RENDER) -q --projection=o --camera=0,-9,700,0,-9,0 --imgsize=1300,1400 -o $@ $<

# The board with every part model KiCad has, as STL and 3MF. For viewing and
# for the check, not for printing: its bodies are not all closed solids. The
# STEP of the same export is ../hardware/zoneflow-a/output/zoneflow-a.step.
# kicad-cli writes ASCII STL; the file kept is the same triangles in binary.
detail: $(DETAIL).stl $(DETAIL).3mf
$(DETAIL).stl $(DETAIL)-missing-models.txt &: $(PCB) tools/threemf.py | $(TF)
	mkdir -p build $(dir $(DETAIL))
	$(KICAD_CLI) pcb export stl --subst-models --drill-origin --force -o build/board-detailed.stl $(PCB) > build/kicad-cli-stl.log 2>&1
	$(PYTHON) tools/threemf.py binary build/board-detailed.stl $(DETAIL).stl --header "ZoneFlow rev A board from KiCad. VIEW ONLY, not for printing."
	{ grep -o 'Could not add 3D model for [A-Za-z0-9]*' build/kicad-cli-stl.log | sed 's/.* //' | sort -V | tr '\n' ' '; echo; } > $(DETAIL)-missing-models.txt
$(DETAIL).3mf: $(DETAIL).stl
	$(PYTHON) tools/threemf.py single $< $@ --title "ZoneFlow rev A board from KiCad: VIEW ONLY, not for printing" \
		--description "Exported by kicad-cli with the part models KiCad has. Bodies are not all closed solids. To print, use the test-fit board."

# Cameras: eye x,y,z then centre x,y,z. With wallview the product stands as
# on a wall: x right, z up, the front faces -y.
renders: $(PNG) $(TF_PNG) $(PLATE_PNG)
$(OUT)/render-front.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="assembly"' --projection=o --camera=57,-600,39,57,0,39 --imgsize=1200,1600 -o $@ zoneflow.scad
$(OUT)/render-front-2.4.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="assembly"' -D 'panel="2.4"' --projection=o --camera=57,-600,39,57,0,39 --imgsize=1200,1600 -o $@ zoneflow.scad
$(OUT)/render-three-quarter.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="assembly"' --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(OUT)/render-rear-top.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="assembly"' --projection=p --camera=270,330,330,57,-5,60 --imgsize=1500,1300 -o $@ zoneflow.scad
$(OUT)/render-exploded.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="exploded"' --projection=p --camera=680,-420,220,57,-75,22 --imgsize=1800,1600 -o $@ zoneflow.scad
$(OUT)/render-wiring-open.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="open"' --projection=p --camera=250,-520,-150,57,0,30 --imgsize=1500,1600 -o $@ zoneflow.scad
$(OUT)/render-section.png: $(SRC) | $(OUT)
	$(RENDER) -q -D 'part="section"' --projection=o --camera=57,-95,2,57,88,2 --imgsize=2000,900 -o $@ zoneflow.scad
$(OUT)/render-section-side.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="section_side"' --projection=o --camera=520,3,39,15,3,39 --imgsize=1000,1900 -o $@ zoneflow.scad
$(OUT)/render-board.png: $(SRC) | $(OUT)
	$(RENDER) -q $(WALL) -D 'part="board"' --projection=p --camera=300,-400,250,57,-8,50 --imgsize=1500,1400 -o $@ zoneflow.scad

# The test-fit kit and the dry fit, step by step. The check views are thin
# slices, so that the gap between two parts shows as a white line.
TF_SCENE := $(RENDER) -q -D 'part="tf_scene"'
TF_SLICE := $(TF_SCENE) -D tf_cutting=true --projection=o
$(TF)/render-kit.png: $(SRC) | $(TF)
	$(RENDER) -q -D 'part="tf_kit"' --projection=p --camera=250,-230,210,57,45,0 --imgsize=1600,1300 -o $@ zoneflow.scad
$(TF)/render-board-top.png: $(SRC) | $(TF)
	$(RENDER) -q -D 'part="tf_board"' --projection=p --camera=57,-70,250,57,52,0 --imgsize=1600,1400 -o $@ zoneflow.scad
$(TF)/render-board-bottom.png: $(SRC) | $(TF)
	$(RENDER) -q -D 'part="tf_underside"' --projection=p --camera=57,-70,250,57,52,0 --imgsize=1600,1400 -o $@ zoneflow.scad
$(TF)/render-step-1-board.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=1 --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(TF)/render-step-2-display.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=2 --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(TF)/render-step-3-fascia.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=3 --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(TF)/render-step-4-plugs.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=4 --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(TF)/render-step-5-cover.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=5 --projection=p --camera=340,-490,200,57,0,36 --imgsize=1500,1600 -o $@ zoneflow.scad
$(TF)/render-check-terminals.png: $(SRC) | $(TF)
	$(TF_SCENE) $(WALL) -D tf_step=4 --projection=p --camera=57,-150,-95,57,-6,-2 --imgsize=1800,1000 -o $@ zoneflow.scad
$(TF)/render-check-display.png: $(SRC) | $(TF)
	$(TF_SLICE) -D 'tf_cut=["y",88.4]' --camera=36,-18,5,36,88.4,5 --imgsize=1800,900 -o $@ zoneflow.scad
$(TF)/render-check-knob.png: $(SRC) | $(TF)
	$(TF_SLICE) -D 'tf_cut=["y",88.4]' --camera=85,-7,12,85,88.4,12 --imgsize=1500,1100 -o $@ zoneflow.scad
$(TF)/render-check-buttons.png: $(SRC) | $(TF)
	$(TF_SLICE) -D 'tf_cut=["y",61.5]' --camera=35,-26,6,35,61.5,6 --imgsize=1800,800 -o $@ zoneflow.scad
$(TF)/render-check-leds.png: $(SRC) | $(TF)
	$(TF_SLICE) -D 'tf_cut=["y",42]' -D tf_slice=0.4 --camera=57,-55,5,57,42,5 --imgsize=2000,600 -o $@ zoneflow.scad
$(TF)/render-check-fuses.png: $(SRC) | $(TF)
	$(TF_SLICE) $(WALL) -D 'tf_cut=["x",16.1]' -D tf_slice=0.5 --camera=116,3,4,16,3,4 --imgsize=1500,1300 -o $@ zoneflow.scad
$(TF)/render-check-bottom.png: $(SRC) | $(TF)
	$(TF_SLICE) -D 'tf_cut=["y",30]' --camera=20,-47,-9,20,30,-9 --imgsize=1800,1100 -o $@ zoneflow.scad

# 1:1 drilling template.
template: $(OUT)/drill-template.svg $(OUT)/drill-template.pdf
$(OUT)/drill-template.svg: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="template"' -o $@ zoneflow.scad
$(OUT)/drill-template.pdf: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="template"' -O export-pdf/paper-size=a4 -O export-pdf/show-scale=false -O export-pdf/show-filename=false -O export-pdf/show-grid=false -O export-pdf/fill=true -o $@ zoneflow.scad

# Copies of the renders for the documentation site.
DOCIMG := ../docs/assets/enclosure
docs-images: $(PNG) tf-docs-images
	mkdir -p $(DOCIMG)
	cp $(PNG) $(DOCIMG)/
tf-docs-images: $(TF_PNG) $(PLATE_PNG)
	mkdir -p $(DOCIMG)/test-fit $(DOCIMG)/plates
	cp $(TF_PNG) $(DOCIMG)/test-fit/
	cp $(PLATE_PNG) $(DOCIMG)/plates/

check: $(OUT)/check-report.md
$(OUT)/check-report.md: $(SRC) tools/check.py board_layout.json plates.toml $(STL) $(TF_STL) $(3MF) $(TF_3MF) $(PLATES) $(DETAIL).stl | $(OUT)
	$(UV) run -q tools/check.py --openscad "$(OPENSCAD)" --out $@

# Every print file in one archive, with a list of what is in it. The
# documentation build makes the same archive and publishes it with the site.
zip: $(ZIP)
$(ZIP): tools/pack.py $(STL) $(3MF) $(TF_STL) $(TF_3MF) $(PLATES) $(PLATE_PNG) $(DETAIL).stl $(DETAIL).3mf $(OUT)/check-report.md
	$(PYTHON) tools/pack.py $@

$(OUT) $(TF):
	mkdir -p $@

openscad: .cache/$(SNAPSHOT)
.cache/$(SNAPSHOT):
	mkdir -p .cache
	curl -fL -o $@.part $(SNAPSHOT_URL)
	echo "$(SNAPSHOT_SHA256)  $@.part" | sha256sum -c -
	chmod +x $@.part
	mv $@.part $@

clean:
	rm -rf $(OUT) build board_params.scad board_layout.json testfit_params.scad
