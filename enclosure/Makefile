# ZoneFlow enclosure. `make` rebuilds everything in output/ from
# ../hardware/mechanical.yaml, params.scad and zoneflow.scad, and fails if the
# interference check fails.
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

YAML  := ../hardware/mechanical.yaml
SRC   := zoneflow.scad params.scad board_params.scad $(wildcard fonts/*.ttf)
OUT   := output
PARTS := base fascia cover bridge knob button-back button-home lightpipe

STL   := $(PARTS:%=$(OUT)/zoneflow-%.stl)
3MF   := $(PARTS:%=$(OUT)/zoneflow-%.3mf)
VIEWS := front three-quarter rear-top exploded wiring-open section section-side board front-2.4
PNG   := $(VIEWS:%=$(OUT)/render-%.png)

RENDER := $(XVFB) $(OSC) --render --colorscheme=Tomorrow
WALL   := -D wallview=true

.PHONY: all params models renders template check docs-images clean openscad
all: params models renders template check docs-images

params: board_params.scad
board_params.scad: $(YAML) tools/gen_params.py
	$(UV) run -q tools/gen_params.py $(YAML) $@

models: $(STL) $(3MF)
$(OUT)/zoneflow-%.stl: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="print_$(subst -,_,$*)"' --export-format binstl -o $@ zoneflow.scad
$(OUT)/zoneflow-%.3mf: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="print_$(subst -,_,$*)"' -o $@ zoneflow.scad

# Cameras: eye x,y,z then centre x,y,z. With wallview the product stands as
# on a wall: x right, z up, the front faces -y.
renders: $(PNG)
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

# 1:1 drilling template.
template: $(OUT)/drill-template.svg $(OUT)/drill-template.pdf
$(OUT)/drill-template.svg: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="template"' -o $@ zoneflow.scad
$(OUT)/drill-template.pdf: $(SRC) | $(OUT)
	$(OSC) -q -D 'part="template"' -O export-pdf/paper-size=a4 -O export-pdf/show-scale=false -O export-pdf/show-filename=false -O export-pdf/show-grid=false -O export-pdf/fill=true -o $@ zoneflow.scad

# Copies of the renders for the documentation site.
DOCIMG := ../docs/assets/enclosure
docs-images: $(PNG)
	mkdir -p $(DOCIMG)
	cp $(PNG) $(DOCIMG)/

check: $(OUT)/check-report.md
$(OUT)/check-report.md: $(SRC) tools/check.py | $(OUT)
	python3 tools/check.py --openscad "$(OPENSCAD)" --out $@

$(OUT):
	mkdir -p $@

openscad: .cache/$(SNAPSHOT)
.cache/$(SNAPSHOT):
	mkdir -p .cache
	curl -fL -o $@.part $(SNAPSHOT_URL)
	echo "$(SNAPSHOT_SHA256)  $@.part" | sha256sum -c -
	chmod +x $@.part
	mv $@.part $@

clean:
	rm -rf $(OUT) board_params.scad
